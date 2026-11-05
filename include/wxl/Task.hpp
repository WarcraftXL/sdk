// SPDX-License-Identifier: GPL-3.0-or-later
// Coroutines: code that waits for frames, seconds or a signal, resumed on the main thread by the tick.

#pragma once

#include "wxl/Signal.hpp"
#include "wxl/objects/Object.hpp"

#include <algorithm>
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace wxl
{
    namespace detail
    {
        struct TaskState
        {
            bool running  = true;  ///< between a resume and the next suspension
            bool orphaned = false; ///< its Task was destroyed while it ran: it ends at its next wait
        };

        /// Something a coroutine waits on, that the scheduler resumes.
        class Waiter
        {
        public:
            virtual ~Waiter() = default;
            /// Advances by a frame of `delta` seconds; true when the wait is over.
            virtual bool Tick(float delta) = 0;

            std::coroutine_handle<> handle;
            TaskState*              state = nullptr;
            uint64_t                since = 0; ///< the frame it began in: it is due at a later one
        };

        /// Resumes this extension's waiting coroutines from the frame tick; connected once something waits.
        class Scheduler
        {
        public:
            static Scheduler& Get()
            {
                static Scheduler scheduler;
                return scheduler;
            }

            void Add(Waiter* w)
            {
                w->since = m_frames;
                m_waiting.push_back(w);
                if (!m_tick.Connected())
                {
                    auto tick = SignalOf<::wxl::Update>().Priority(priority::First).Connect(
                        [this](const events::UpdateArgs& a) { OnTick(a.dt); });
                    if (tick) m_tick = std::move(*tick);
                }
            }

            void Remove(Waiter* w) { m_waiting.erase(std::remove(m_waiting.begin(), m_waiting.end(), w), m_waiting.end()); }
            std::size_t Waiting() const { return m_waiting.size(); }

        private:
            /// Waiters added while this runs wait for the next frame; one removed meanwhile is skipped.
            /// The due list keeps its capacity: no allocation per frame once it has grown.
            void OnTick(float delta)
            {
                ++m_frames;
                m_due.clear();
                for (Waiter* w : m_waiting) // Tick never adds or removes a waiter
                    if (w->since < m_frames && w->Tick(delta)) m_due.push_back(w);
                for (Waiter* w : m_due)
                {
                    if (std::find(m_waiting.begin(), m_waiting.end(), w) == m_waiting.end()) continue;
                    Remove(w);
                    Resume(w);
                }
            }

            static void Resume(Waiter* w)
            {
                if (w->state) w->state->running = true;
                w->handle.resume(); // may destroy the frame, and `w` with it
            }

            std::vector<Waiter*> m_waiting;
            std::vector<Waiter*> m_due;
            Connection           m_tick;
            uint64_t             m_frames = 0;
        };
    }

    /// A coroutine: what a function using co_await returns. It runs at once until its first wait;
    /// destroying it ends the coroutine where it waits. Move-only. For example:
    ///     wxl::Task Countdown(wxl::Unit unit) { if (!co_await wxl::Delay(1.0f, unit)) co_return; ... }
    class [[nodiscard]] Task
    {
    public:
        struct promise_type
        {
            detail::TaskState state;

            Task get_return_object() { return Task(std::coroutine_handle<promise_type>::from_promise(*this)); }
            std::suspend_never initial_suspend() noexcept { return {}; }
            auto final_suspend() noexcept
            {
                struct Final
                {
                    bool await_ready() const noexcept { return false; }
                    void await_suspend(std::coroutine_handle<promise_type> h) noexcept
                    {
                        h.promise().state.running = false;
                        if (h.promise().state.orphaned) h.destroy();
                    }
                    void await_resume() const noexcept {}
                };
                return Final{};
            }
            void return_void() {}
            void unhandled_exception()
            {
                try
                {
                    throw;
                }
                catch (const std::exception& e)
                {
                    Log::Error("a task threw: {}", e.what());
                    throw;
                }
            }
        };

        Task() = default;
        Task(Task&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
        Task& operator=(Task&& other) noexcept
        {
            if (this != &other)
            {
                Cancel();
                m_handle = std::exchange(other.m_handle, {});
            }
            return *this;
        }
        Task(const Task&)            = delete;
        Task& operator=(const Task&) = delete;
        ~Task() { Cancel(); }

        bool Done() const { return !m_handle || m_handle.done(); }

        /// Ends the coroutine where it waits; from inside it, at its next wait.
        void Cancel()
        {
            if (!m_handle) return;
            auto h = std::exchange(m_handle, {});
            if (h.promise().state.running) h.promise().state.orphaned = true;
            else h.destroy();
        }

    private:
        explicit Task(std::coroutine_handle<promise_type> h) : m_handle(h) {}
        std::coroutine_handle<promise_type> m_handle;
    };

    namespace detail
    {
        inline bool Suspend(Waiter* w, std::coroutine_handle<Task::promise_type> h)
        {
            w->handle         = h;
            w->state          = &h.promise().state;
            w->state->running = false;
            if (w->state->orphaned)
            {
                h.destroy();
                return true;
            }
            Scheduler::Get().Add(w);
            return true;
        }

        /// The next frame, or `seconds` of them; watching an object, Result<void> (Expired once it is gone).
        template <bool Watched>
        class DelayWaiter : public Waiter
        {
        public:
            explicit DelayWaiter(float seconds, Object watch = {}) : m_left(seconds), m_watch(watch) {}
            DelayWaiter(const DelayWaiter&)            = delete;
            DelayWaiter& operator=(const DelayWaiter&) = delete;
            ~DelayWaiter() override { Scheduler::Get().Remove(this); }

            bool Tick(float delta) override
            {
                if constexpr (Watched)
                    if (!m_watch.Exists()) return true;
                m_left -= delta;
                return m_left <= 0;
            }

            bool await_ready() const noexcept { return false; }
            bool await_suspend(std::coroutine_handle<Task::promise_type> h) { return Suspend(this, h); }
            auto await_resume() const
            {
                if constexpr (Watched) return m_watch.Exists() ? Result<void>() : Result<void>(Error::Expired);
            }

        private:
            float  m_left;
            Object m_watch;
        };

        template <class P>
        struct NextState
        {
            Waiter*          waiter = nullptr; ///< null once the wait is over
            std::optional<P> payload;
            Error            error = Error::Expired;
            bool             done  = false;
        };

        /// A signal's next payload as a wait: the payload, Error::Timeout, or Error::Expired when the
        /// connection ended first.
        template <class P>
        class NextAwaiter : public Waiter
        {
        public:
            NextAwaiter(Signal<P> signal, float timeout) : m_signal(std::move(signal)), m_left(timeout) {}
            NextAwaiter(const NextAwaiter&)            = delete;
            NextAwaiter& operator=(const NextAwaiter&) = delete;
            ~NextAwaiter() override
            {
                Scheduler::Get().Remove(this);
                if (m_state) m_state->waiter = nullptr;
            }

            bool Tick(float delta) override
            {
                if (m_state->done) return true;
                if (!m_connection.Connected())
                {
                    m_state->done  = true;
                    m_state->error = Error::Expired;
                    return true;
                }
                if (m_left <= 0) return false; // no timeout: only the signal wakes it
                m_left -= delta;
                if (m_left > 0) return false;
                m_state->done  = true;
                m_state->error = Error::Timeout;
                m_connection.Disconnect();
                return true;
            }

            bool await_ready() const noexcept { return false; }
            bool await_suspend(std::coroutine_handle<Task::promise_type> h)
            {
                m_state     = std::make_shared<NextState<P>>();
                auto state = m_state;
                auto conn  = m_signal.Once().Connect([state](const P& p) {
                    if (state->done || !state->waiter) return;
                    state->payload = p;
                    state->done    = true;
                    Waiter* w      = std::exchange(state->waiter, nullptr);
                    Scheduler::Get().Remove(w);
                    if (w->state) w->state->running = true;
                    w->handle.resume(); // inside the signal's handler, the payload copied
                });
                if (!conn)
                {
                    m_state->done  = true;
                    m_state->error = conn.Code();
                    return false; // resumes at once, with the error
                }
                m_connection    = std::move(*conn);
                m_state->waiter = this;
                return Suspend(this, h);
            }

            Result<P> await_resume()
            {
                if (m_state->payload) return std::move(*m_state->payload);
                return m_state->error;
            }

        private:
            Signal<P>                     m_signal;
            float                         m_left;
            std::shared_ptr<NextState<P>> m_state;
            Connection                    m_connection;
        };
    }

    /// Waits for the next frame.
    inline detail::DelayWaiter<false> NextFrame() { return detail::DelayWaiter<false>(0.0f); }
    /// Waits for the next frame: Result<void>, Error::Expired when `watch` is gone by then.
    inline detail::DelayWaiter<true> NextFrame(Object watch) { return detail::DelayWaiter<true>(0.0f, watch); }
    /// Waits `seconds` of game time, at least one frame.
    inline detail::DelayWaiter<false> Delay(float seconds) { return detail::DelayWaiter<false>(seconds); }
    /// Waits `seconds`: Result<void>, Error::Expired as soon as `watch` is gone.
    inline detail::DelayWaiter<true> Delay(float seconds, Object watch) { return detail::DelayWaiter<true>(seconds, watch); }

    template <class P>
    detail::NextAwaiter<P> Signal<P>::Next(float timeout) const
    {
        return detail::NextAwaiter<P>(*this, timeout);
    }
}
