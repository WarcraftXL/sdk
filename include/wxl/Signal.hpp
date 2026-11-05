// SPDX-License-Identifier: GPL-3.0-or-later
// Typed signals over the core's event bus, and the connections they hand out.

#pragma once

#include "wxl/Events.hpp"
#include "wxl/Core.hpp"
#include "wxl/Log.hpp"
#include "wxl/Result.hpp"

#include <exception>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace wxl
{
    /// Handler order: lower first, equal ones in connection order.
    namespace priority
    {
        inline constexpr int32_t First   = WXL_PRIORITY_FIRST;
        inline constexpr int32_t Default = WXL_PRIORITY_DEFAULT;
        inline constexpr int32_t Last    = WXL_PRIORITY_LAST;
    }

    /// A live connection, ended when destroyed or when its subject expires. Move-only: keep it as a
    /// member of what the handler works on.
    class [[nodiscard]] Connection
    {
    public:
        Connection() = default;
        explicit Connection(WXL_Connection id) : m_id(id) {}
        Connection(const Connection&)            = delete;
        Connection& operator=(const Connection&) = delete;
        Connection(Connection&& other) noexcept : m_id(std::exchange(other.m_id, WXL_Connection{})) {}
        Connection& operator=(Connection&& other) noexcept
        {
            if (this != &other)
            {
                Disconnect();
                m_id = std::exchange(other.m_id, WXL_Connection{});
            }
            return *this;
        }
        ~Connection() { Disconnect(); }

        /// Ends it now. Inside its own handler: that call finishes, no other comes.
        void Disconnect()
        {
            if (m_id.generation != 0 && detail::Has(&WXL_Api::Disconnect)) Api()->Disconnect(m_id);
            m_id = {};
        }

        bool Connected() const
        {
            return m_id.generation != 0 && detail::Has(&WXL_Api::Connected) && Api()->Connected(m_id) == WXL_OK;
        }

        /// Gives it up without ending it: it lasts until its subject expires.
        WXL_Connection Release() { return std::exchange(m_id, WXL_Connection{}); }

        WXL_Connection Id() const { return m_id; }

    private:
        WXL_Connection m_id{};
    };

    /// What a signal's source makes of one raw event: deliver it, skip it, or end the connection.
    enum class Deliver { Yes, Skip, End };

    namespace detail
    {
        /// Runs a handler; a std::exception leaving it is logged, then rethrown to the core's guard.
        template <class F>
        void Call(const char* where, F&& f)
        {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
            try
            {
                f();
            }
            catch (const std::exception& e)
            {
                Log::Error("{} threw: {}", where, e.what());
                throw;
            }
#else
            f();
#endif
        }

        template <class P>
        struct Slot
        {
            std::function<Deliver(const void*, P&)>    source; ///< null: the raw args are a P
            std::vector<std::function<bool(const P&)>> filters;
            std::function<void(const P&)>              handler;
            const char*                                name = "";
            bool                                       once = false;
            bool                                       done = false;
            WXL_Connection                             self{};

            void End()
            {
                done = true;
                if (detail::Has(&WXL_Api::Disconnect)) Api()->Disconnect(std::exchange(self, WXL_Connection{}));
            }

            void Hand(const P& p)
            {
                for (const auto& filter : filters)
                    if (!filter(p)) return;
                if (once) End();
                Call(name, [&] { handler(p); });
            }
        };

        template <class P>
        void WXL_CDECL Invoke(void* user, const void* raw)
        {
            auto* slot = static_cast<Slot<P>*>(user);
            if (slot->done) return;
            if (!slot->source)
            {
                slot->Hand(*static_cast<const P*>(raw));
                return;
            }
            P p{};
            switch (slot->source(raw, p))
            {
            case wxl::Deliver::Yes: slot->Hand(p); return;
            case wxl::Deliver::Skip: return;
            case wxl::Deliver::End: slot->End(); return;
            }
        }

        template <class P>
        void WXL_CDECL Release(void* user)
        {
            delete static_cast<Slot<P>*>(user);
        }

        template <class P>
        class NextAwaiter; // wxl/Task.hpp
    }

    /// A bus event, maybe about one object, maybe reshaped by a source. Priority(), Filter() and
    /// Once() answer refined copies; On(signal, handler) connects it for the extension, Connect() for
    /// a Connection the caller keeps.
    template <class P>
    class Signal
    {
    public:
        using Payload = P;
        using Source  = std::function<Deliver(const void* raw, P& out)>;

        Signal(events::Event event, WXL_Handle subject, Source source, const char* name)
            : m_event(event), m_subject(subject), m_source(std::move(source)), m_name(name)
        {
        }

        /// Lower runs first (priority::First...); equal ones in connection order.
        Signal Priority(int32_t priority) const
        {
            Signal copy    = *this;
            copy.m_priority = priority;
            return copy;
        }

        /// Calls the handler only for payloads `predicate` accepts. Filters add up.
        template <class Predicate>
        Signal Filter(Predicate&& predicate) const
        {
            Signal copy = *this;
            copy.m_filters.emplace_back(std::forward<Predicate>(predicate));
            return copy;
        }

        /// Ends the connection at the first payload that passes the filters.
        Signal Once() const
        {
            Signal copy = *this;
            copy.m_once  = true;
            return copy;
        }

        /// Connects `handler`. Fails with Unsupported before Bind, or with the subject's error.
        template <class Handler>
        Result<Connection> Connect(Handler&& handler) const
        {
            if (!detail::Has(&WXL_Api::Connect)) return Error::Unsupported;
            auto slot = std::make_unique<detail::Slot<P>>();
            slot->source  = m_source;
            slot->filters = m_filters;
            slot->handler = std::function<void(const P&)>(std::forward<Handler>(handler));
            slot->name    = m_name;
            slot->once    = m_once;
            WXL_Connection id{};
            const int status = Api()->Connect(static_cast<uint32_t>(m_event), m_subject, m_priority, &detail::Invoke<P>,
                                              slot.get(), &detail::Release<P>, &id);
            if (status != WXL_OK) return static_cast<Error>(status);
            slot.release()->self = id; // the core releases it from now on
            return Connection(id);
        }

        /// In a wxl::Task: the next payload that passes the filters (wxl/Task.hpp).
        detail::NextAwaiter<P> Next(float timeout = 0) const;

        const char* Name() const { return m_name; }

    private:
        template <class>
        friend class detail::NextAwaiter;

        events::Event                              m_event;
        WXL_Handle                                 m_subject{};
        Source                                     m_source;
        const char*                                m_name     = "";
        int32_t                                    m_priority = priority::Default;
        bool                                       m_once     = false;
        std::vector<std::function<bool(const P&)>> m_filters;
    };

    /// A bus event as a signal of its args struct, optionally about one object; objects and Game() hand
    /// these out. In an extension, On(signal, handler) connects one.
    template <detail::EventTag E>
    Signal<typename E::Args> SignalOf(WXL_Handle subject = {})
    {
        return Signal<typename E::Args>(E::kEvent, subject, nullptr, E::kName);
    }
}
