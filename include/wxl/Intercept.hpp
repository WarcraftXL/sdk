// SPDX-License-Identifier: GPL-3.0-or-later
// Interceptors: guarded handlers Before, Replace and After a client function, with its typed arguments.

#pragma once

#include "wxl/Core.hpp"
#include "wxl/Signal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace wxl
{
    template <class Fn, auto Address, int Convention>
    class Interceptor;

    /// One call of an intercepted function: its arguments, its result, and what the handlers decided.
    template <class R, class... A>
    class Call
    {
    public:
        static constexpr std::size_t kArgCount = sizeof...(A);
        using Next = R (*)(A...);

        explicit Call(Next next, A... args) : m_next(next), m_args(args...) {}

        /// The I-th argument; writable in a Before handler.
        template <std::size_t I>
        auto& Arg() { return std::get<I>(m_args); }
        template <std::size_t I>
        const auto& Arg() const { return std::get<I>(m_args); }

        /// In a Before handler: skips the function, answering `value`.
        template <class V = R>
        void Return(V value) requires(!std::is_void_v<R>)
        {
            m_result   = static_cast<R>(value);
            m_returned = true;
        }
        void Return() requires std::is_void_v<R> { m_returned = true; }

        /// Whether a Before handler skipped the function.
        bool Returned() const { return m_returned; }

        /// The result, in an After handler (writable), or after Return().
        template <class V = R>
        V& Result() requires(!std::is_void_v<R>) { return *m_result; }

        /// In a Replace handler: calls the function (the rest of its chain) with the current arguments.
        R Original() { return std::apply(m_next, m_args); }

    private:
        template <class, auto, int>
        friend class Interceptor;

        using Outcome = std::conditional_t<std::is_void_v<R>, char, std::optional<R>>;

        Next             m_next;
        std::tuple<A...> m_args;
        Outcome          m_result{};
        bool             m_returned = false;
    };

    namespace detail
    {
        /// The address an interceptor is keyed by: an offsets constant, or a function of the extension's own.
        template <auto Address>
        uintptr_t AddressOf()
        {
            if constexpr (std::is_integral_v<decltype(Address)>) return static_cast<uintptr_t>(Address);
            else return reinterpret_cast<uintptr_t>(Address);
        }

        enum Convention { kCdecl, kStdcall, kFastcall };

        template <class Fn>
        struct Signature;
        template <class R, class... A>
        struct Signature<R(__cdecl*)(A...)>
        {
            using Result = R;
            using Call   = wxl::Call<R, A...>;
            static constexpr int kConvention = kCdecl;
        };
        template <class R, class... A>
        struct Signature<R(__stdcall*)(A...)>
        {
            using Result = R;
            using Call   = wxl::Call<R, A...>;
            static constexpr int kConvention = kStdcall;
        };
        template <class R, class... A>
        struct Signature<R(__fastcall*)(A...)>
        {
            using Result = R;
            using Call   = wxl::Call<R, A...>;
            static constexpr int kConvention = kFastcall;
        };
    }

    /// One handler of an interceptor; ends it when destroyed. Move-only.
    class [[nodiscard]] Interception
    {
    public:
        Interception() = default;
        Interception(void (*remove)(uint32_t), uint32_t id) : m_remove(remove), m_id(id) {}
        Interception(const Interception&)            = delete;
        Interception& operator=(const Interception&) = delete;
        Interception(Interception&& o) noexcept : m_remove(std::exchange(o.m_remove, nullptr)), m_id(o.m_id) {}
        Interception& operator=(Interception&& o) noexcept
        {
            if (this != &o)
            {
                Remove();
                m_remove = std::exchange(o.m_remove, nullptr);
                m_id     = o.m_id;
            }
            return *this;
        }
        ~Interception() { Remove(); }

        /// Ends the handler now.
        void Remove()
        {
            if (m_remove) std::exchange(m_remove, nullptr)(m_id);
        }
        explicit operator bool() const { return m_remove != nullptr; }

    private:
        void (*m_remove)(uint32_t) = nullptr;
        uint32_t m_id              = 0;
    };

    /// The interceptor of the function at `Address` whose pointer type is Fn (the offsets' `...Fn`
    /// alias); one per extension and function, handed out by wxl::Intercept<Fn, Address>().
    template <class Fn, auto Address, int Convention = detail::Signature<Fn>::kConvention>
    class Interceptor
    {
        using Sig   = detail::Signature<Fn>;
        using R     = typename Sig::Result;
        using CallT = typename Sig::Call;

    public:
        static Interceptor& Get()
        {
            static Interceptor instance;
            return instance;
        }

        /// Runs before the function, in priority order; may change the arguments or Return().
        template <class H>
        Interception Before(H&& handler, int32_t priority = WXL_PRIORITY_DEFAULT)
        {
            return Add(kBefore, priority, std::function<void(CallT&)>(std::forward<H>(handler)), {});
        }

        /// Runs instead of the function; only the first by priority does.
        template <class H>
        Interception Replace(H&& handler, int32_t priority = WXL_PRIORITY_DEFAULT)
        {
            return Add(kReplace, priority, {}, std::function<R(CallT&)>(std::forward<H>(handler)));
        }

        /// Runs after the function, in priority order; may change the Result().
        template <class H>
        Interception After(H&& handler, int32_t priority = WXL_PRIORITY_DEFAULT)
        {
            return Add(kAfter, priority, std::function<void(CallT&)>(std::forward<H>(handler)), {});
        }

        /// Whether the detour is in place: false before Bind, or when the core refused the address.
        bool Attached() const { return m_next != nullptr; }

        /// The handlers in place.
        std::size_t Count() const { return m_handlers ? m_handlers->size() : 0; }

    private:
        enum Kind { kBefore, kReplace, kAfter };

        struct Handler
        {
            uint32_t                    id;
            Kind                        kind;
            int32_t                     priority;
            std::function<void(CallT&)> observe;
            std::function<R(CallT&)>    replace;
        };
        using Handlers = std::vector<Handler>;

        static constexpr std::size_t kNameSize = 48;

        Interception Add(Kind kind, int32_t priority, std::function<void(CallT&)> observe,
                         std::function<R(CallT&)> replace)
        {
            if (!Attach()) return {};
            const uint32_t id   = ++m_lastId;
            auto           next = m_handlers ? std::make_shared<Handlers>(*m_handlers) : std::make_shared<Handlers>();
            auto at = std::upper_bound(next->begin(), next->end(), priority,
                                       [](int32_t p, const Handler& x) { return p < x.priority; });
            next->insert(at, Handler{ id, kind, priority, std::move(observe), std::move(replace) });
            m_handlers = std::move(next);
            return Interception(&RemoveHandler, id);
        }

        static void RemoveHandler(uint32_t id)
        {
            Interceptor& self = Get();
            if (!self.m_handlers) return;
            auto next = std::make_shared<Handlers>(*self.m_handlers);
            next->erase(std::remove_if(next->begin(), next->end(), [id](const Handler& h) { return h.id == id; }),
                        next->end());
            if (next->empty()) self.m_handlers.reset();
            else self.m_handlers = std::move(next);
        }

        bool Attach()
        {
            if (m_next) return true;
            if (!detail::Has(&WXL_Api::InterceptAttach)) return false;
            char            name[kNameSize];
            const uintptr_t address = detail::AddressOf<Address>();
            std::snprintf(name, sizeof name, "intercept 0x%08X", static_cast<unsigned>(address));
            return Api()->InterceptAttach(name, address, reinterpret_cast<void*>(Detour()),
                                          reinterpret_cast<void**>(&m_next)) != 0 && m_next;
        }

        /// A handler, under the guard: false when it faulted.
        template <class F>
        static bool Run(F&& f)
        {
            return Guarded([&] { detail::Call("an interceptor", f); });
        }

        template <class... A>
        static R Forward(A... a) { return Get().m_next(a...); }

        /// The Before handlers until one returns; a faulting one's changes are undone.
        static void RunBefore(const Handlers& handlers, CallT& call)
        {
            for (const Handler& h : handlers)
            {
                if (h.kind != kBefore) continue;
                if (call.m_returned) return;
                const auto args   = call.m_args;
                const auto result = call.m_result;
                if (Run([&] { h.observe(call); })) continue;
                call.m_args     = args;
                call.m_result   = result;
                call.m_returned = false;
            }
        }

        /// The first Replace handler, or the function when there is none or it faulted.
        static void RunReplace(const Handlers& handlers, CallT& call)
        {
            const auto replace = std::find_if(handlers.begin(), handlers.end(),
                                              [](const Handler& h) { return h.kind == kReplace; });
            const bool has = replace != handlers.end();
            if constexpr (std::is_void_v<R>)
            {
                if (!has || !Run([&] { replace->replace(call); })) call.Original();
            }
            else
            {
                if (!has || !Run([&] { call.m_result = replace->replace(call); })) call.m_result = call.Original();
            }
        }

        /// The After handlers; a faulting one's change to the result is undone.
        static void RunAfter(const Handlers& handlers, CallT& call)
        {
            for (const Handler& h : handlers)
            {
                if (h.kind != kAfter) continue;
                const auto result = call.m_result;
                if (!Run([&] { h.observe(call); })) call.m_result = result;
            }
        }

        template <class... A>
        R Dispatch(A... args)
        {
            static thread_local int depth = 0; // a call from inside a handler passes straight through
            if (!m_handlers || depth > 0) return m_next(args...);
            ++depth;
            struct Leave { int& d; ~Leave() { --d; } } leave{ depth };

            // A handler may add or remove handlers: this call keeps the list it started with.
            const std::shared_ptr<const Handlers> handlers = m_handlers;
            CallT call(&Forward<A...>, args...);
            RunBefore(*handlers, call);
            if (!call.m_returned) RunReplace(*handlers, call);
            RunAfter(*handlers, call);
            if constexpr (!std::is_void_v<R>) return *call.m_result;
        }

        template <class... A>
        static R __cdecl ThunkCdecl(A... a) { return Get().Dispatch(a...); }
        template <class... A>
        static R __stdcall ThunkStdcall(A... a) { return Get().Dispatch(a...); }
        template <class... A>
        static R __fastcall ThunkFastcall(A... a) { return Get().Dispatch(a...); }

        template <class Sig2>
        struct Thunks;
        template <class... A>
        struct Thunks<R(__cdecl*)(A...)> { static Fn Make() { return &ThunkCdecl<A...>; } };
        template <class... A>
        struct Thunks<R(__stdcall*)(A...)> { static Fn Make() { return &ThunkStdcall<A...>; } };
        template <class... A>
        struct Thunks<R(__fastcall*)(A...)> { static Fn Make() { return &ThunkFastcall<A...>; } };

        /// The thunk of Fn's exact convention the core attaches as the detour.
        static Fn Detour() { return Thunks<Fn>::Make(); }

        std::shared_ptr<const Handlers> m_handlers; ///< replaced whole on a change; null when empty
        uint32_t                        m_lastId = 0;
        Fn                              m_next   = nullptr;
    };

    /// The interceptor of a client function, by its offsets entry:
    ///     wxl::Intercept<CGChat::AddChatMessageFn, CGChat::AddChatMessage>().Before([](auto& call) { ... });
    template <class Fn, auto Address>
    Interceptor<Fn, Address>& Intercept()
    {
        return Interceptor<Fn, Address>::Get();
    }
}
