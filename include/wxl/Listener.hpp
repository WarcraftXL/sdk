// SPDX-License-Identifier: GPL-3.0-or-later
// On<Tag>(handler): a bus event connected for the extension, its handler kept in a fixed slot.

#pragma once

#include "wxl/Core.hpp"
#include "wxl/Events.hpp"
#include "wxl/Log.hpp"
#include "wxl/Result.hpp"
#include "wxl/Signal.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace wxl
{
    namespace detail
    {
        /// The bytes a handler's captures may take: a pointer or a few values. Larger state goes behind a pointer.
        inline constexpr std::size_t kCaptureSize = 32;
        /// The handlers an extension may have connected at once.
        inline constexpr std::size_t kListenerSlots = 128;

        struct ListenerSlot
        {
            alignas(8) unsigned char capture[kCaptureSize];
            void (*destroy)(void* capture) = nullptr; ///< null: a signal's connection, its handler held by the signal
            const char*    name = "";
            WXL_Connection id{};
            bool           used = false;
            bool           once = false;
        };
        inline ListenerSlot g_listeners[kListenerSlots];

        /// A free slot; one holding a signal's connection that has ended is free again.
        inline ListenerSlot* FreeSlot()
        {
            for (ListenerSlot& s : g_listeners)
            {
                if (s.used && !s.destroy && Api()->Connected(s.id) != WXL_OK) s.used = false;
                if (!s.used) return &s;
            }
            return nullptr;
        }

        template <class E, class F>
        void WXL_CDECL InvokeListener(void* user, const void* raw)
        {
            auto& slot = *static_cast<ListenerSlot*>(user);
            if (slot.once)
            {
                slot.once = false;
                Api()->Disconnect(slot.id);
            }
            auto& handler = *std::launder(reinterpret_cast<F*>(slot.capture));
            const auto& args = *static_cast<const typename E::Args*>(raw);
            Call(slot.name, [&] {
                if constexpr (std::is_invocable_v<F&, const typename E::Args&>) handler(args);
                else handler();
            });
        }

        inline void WXL_CDECL ReleaseListener(void* user)
        {
            auto& slot = *static_cast<ListenerSlot*>(user);
            if (slot.destroy) slot.destroy(slot.capture);
            slot = ListenerSlot{};
        }

        template <class E, class F>
        Result<WXL_Connection> Listen(F&& handler, int32_t priority, bool once)
        {
            using Fn = std::decay_t<F>;
            static_assert(std::is_invocable_v<Fn&, const typename E::Args&> || std::is_invocable_v<Fn&>,
                          "On<Tag>: the handler takes `const Tag::Args&` (or `auto&`), or nothing");
            static_assert(sizeof(Fn) <= kCaptureSize,
                          "On<Tag>: the handler's captures take more than 32 bytes; capture a pointer to the state instead");
            static_assert(alignof(Fn) <= 8, "On<Tag>: the handler's captures need more than 8-byte alignment");
            if (!Has(&WXL_Api::Connect)) return Error::Unsupported;
            ListenerSlot* slot = FreeSlot();
            if (!slot) return Error::InvalidArgument;
            ::new (static_cast<void*>(slot->capture)) Fn(std::forward<F>(handler));
            slot->destroy = [](void* p) { std::launder(static_cast<Fn*>(p))->~Fn(); };
            slot->name    = E::kName;
            slot->used    = true;
            slot->once    = once;
            const int status = Api()->Connect(static_cast<uint32_t>(E::kEvent), WXL_Handle{}, priority,
                                              &InvokeListener<E, Fn>, slot, &ReleaseListener, &slot->id);
            if (status == WXL_OK) return slot->id;
            ReleaseListener(slot);
            return static_cast<Error>(status);
        }

        /// Ends every handler the extension connected with On; their releases free the slots.
        inline void StopListeners()
        {
            if (!Has(&WXL_Api::Disconnect)) return;
            for (ListenerSlot& s : g_listeners)
            {
                if (!s.used) continue;
                const bool signal = !s.destroy;
                Api()->Disconnect(s.id);
                if (signal) s = ListenerSlot{};
            }
        }

        inline std::size_t ListenerCount()
        {
            std::size_t n = 0;
            for (const ListenerSlot& s : g_listeners) n += s.used && Api()->Connected(s.id) == WXL_OK;
            return n;
        }

        inline void LogRefusal(const char* what, Error error)
        {
            Log::Error("On<{}>: {}", what, error == Error::InvalidArgument && !FreeSlot()
                                               ? "every handler slot is taken (128 per extension)"
                                               : Message(error));
        }
    }

    /// Raises an event to every handler, core or extension: `Emit<WmoRootLoad>({ root });`.
    template <detail::EventTag E>
    void Emit(const typename E::Args& args)
    {
        if (detail::Has(&WXL_Api::Emit)) Api()->Emit(static_cast<uint32_t>(E::kEvent), &args);
    }

    /// A handler connected with On: Stop() ends it early; it ends anyway when the extension stops.
    class Listener
    {
    public:
        Listener() = default;
        explicit Listener(WXL_Connection id) : m_id(id) {}

        void Stop()
        {
            if (m_id.generation != 0 && detail::Has(&WXL_Api::Disconnect)) Api()->Disconnect(m_id);
            m_id = {};
        }

        bool Active() const
        {
            return m_id.generation != 0 && detail::Has(&WXL_Api::Connected) && Api()->Connected(m_id) == WXL_OK;
        }

    private:
        WXL_Connection m_id{};
    };

    /// Calls `handler(const Tag::Args&)` on each event, until the extension stops:
    ///     On<WorldEnter>([](auto& e) { Log::Info("map {}", e.mapId); });
    /// A failure is logged; TryOn answers it instead.
    template <detail::EventTag E, class F>
    Listener On(F&& handler, int32_t priority = priority::Default)
    {
        auto id = detail::Listen<E>(std::forward<F>(handler), priority, false);
        if (!id) detail::LogRefusal(E::kName, id.Code());
        return Listener(id.ValueOr(WXL_Connection{}));
    }

    /// The same, for the first event only.
    template <detail::EventTag E, class F>
    Listener Once(F&& handler, int32_t priority = priority::Default)
    {
        auto id = detail::Listen<E>(std::forward<F>(handler), priority, true);
        if (!id) detail::LogRefusal(E::kName, id.Code());
        return Listener(id.ValueOr(WXL_Connection{}));
    }

    /// On, answering why it could not connect rather than logging it.
    template <detail::EventTag E, class F>
    Result<Listener> TryOn(F&& handler, int32_t priority = priority::Default)
    {
        auto id = detail::Listen<E>(std::forward<F>(handler), priority, false);
        if (!id) return id.Code();
        return Listener(*id);
    }

    /// A signal (an object's, Game()'s, a setting's), answering why it could not connect.
    template <class P, class F>
    Result<Listener> TryOn(const Signal<P>& signal, F&& handler)
    {
        if (!detail::Has(&WXL_Api::Connect)) return Error::Unsupported;
        detail::ListenerSlot* slot = detail::FreeSlot();
        if (!slot) return Error::InvalidArgument;
        auto connection = signal.Connect(std::forward<F>(handler));
        if (!connection) return connection.Code();
        *slot = detail::ListenerSlot{};
        slot->id   = connection->Release();
        slot->name = signal.Name();
        slot->used = true;
        return Listener(slot->id);
    }

    /// A signal connected until the extension stops, or its subject expires:
    ///     On(unit.HealthChanged(), [](auto& c) { Log::Info("{} -> {}", c.previous, c.value); });
    template <class P, class F>
    Listener On(const Signal<P>& signal, F&& handler)
    {
        auto listener = TryOn(signal, std::forward<F>(handler));
        if (!listener) detail::LogRefusal(signal.Name(), listener.Code());
        return listener ? *listener : Listener();
    }
}
