// SPDX-License-Identifier: GPL-3.0-or-later
// wxl::Extension: the class an extension derives, with a virtual per event (Overrides.def) and its lifecycle.

#pragma once

#include "wxl/Core.hpp"
#include "wxl/Events.hpp"
#include "wxl/Listener.hpp"
#include "wxl/Log.hpp"
#include "wxl/Signal.hpp"

#include <cstdint>
#include <type_traits>

namespace wxl
{
    class Extension;

    namespace detail
    {
        template <class T>
        class Lifecycle;
    }

    /// The base of an extension: override OnStart and the events it needs; only those are connected.
    /// Export<MyExtension> makes it the DLL's entry point.
    class Extension
    {
    public:
        Extension()                            = default;
        Extension(const Extension&)            = delete;
        Extension& operator=(const Extension&) = delete;
        virtual ~Extension()                   = default;

        /// The core has loaded the extension: connect, hook, publish here. No device and no world yet.
        virtual void OnStart() {}
        /// The extension stops (the process exits): release what outlives it. Its handlers end after it.
        virtual void OnStop() {}

#define WXL_OVERRIDE(Tag, params, args) virtual void On##Tag params {}
#include "wxl/Overrides.def"
#undef WXL_OVERRIDE

        /// The extension's name and version, from its wxl.json.
        const char* Name() const { return ExtensionName(); }
        const char* Version() const { return ExtensionVersion(); }

        /// The events whose virtual this extension overrides, connected since it started.
        uint32_t Connected() const
        {
            uint32_t n = 0;
            for (const WXL_Connection& c : m_events)
                n += c.generation != 0 && detail::Has(&WXL_Api::Connected) && Api()->Connected(c) == WXL_OK;
            return n;
        }

    private:
        template <class>
        friend class detail::Lifecycle;

        WXL_Connection m_events[static_cast<uint32_t>(events::Event::Count)] = {};
    };

    namespace detail
    {
        /// Whether S declares its own OnTag: then &S::OnTag is not Extension's (or is out of reach,
        /// being private), so its event is connected.
        template <class S>
        struct Overrides
        {
#define WXL_OVERRIDE(Tag, params, args)                                                              \
    static constexpr bool Tag = [] {                                                               \
        if constexpr (requires { &S::On##Tag; })                                                   \
            return !std::is_same_v<decltype(&S::On##Tag), decltype(&Extension::On##Tag)>;          \
        else                                                                                       \
            return true;                                                                           \
    }();
#include "wxl/Overrides.def"
#undef WXL_OVERRIDE
        };

#define WXL_OVERRIDE(Tag, params, args)                                                              \
    inline void WXL_CDECL Dispatch##Tag(void* user, const void* raw)                               \
    {                                                                                              \
        const auto& a = *static_cast<const typename ::wxl::Tag::Args*>(raw);                       \
        Call("On" #Tag, [&] { static_cast<Extension*>(user)->On##Tag args; });                     \
    }
#include "wxl/Overrides.def"
#undef WXL_OVERRIDE

        /// Starts and stops one extension of type T: its overridden events, its On handlers, OnStart, OnStop.
        template <class T>
        class Lifecycle
        {
        public:
            static_assert(std::is_base_of_v<Extension, T>, "the extension derives from wxl::Extension");

            /// Connects the events T overrides, then runs OnStart.
            static void Start(T& extension)
            {
                Extension& base = extension;
                if (Has(&WXL_Api::Connect))
                {
#define WXL_OVERRIDE(Tag, params, args)                                                              \
    if constexpr (Overrides<T>::Tag)                                                               \
        Api()->Connect(static_cast<uint32_t>(::wxl::Tag::kEvent), WXL_Handle{}, priority::Default,   \
                       &Dispatch##Tag, &base, nullptr,                                             \
                       &base.m_events[static_cast<uint32_t>(::wxl::Tag::kEvent)]);
#include "wxl/Overrides.def"
#undef WXL_OVERRIDE
                }
                base.OnStart();
            }

            /// Runs OnStop under the guard, then ends the overridden events and every On handler.
            static void Stop(T& extension)
            {
                Extension& base = extension;
                Guarded([&] { base.OnStop(); });
                for (WXL_Connection& c : base.m_events)
                {
                    if (c.generation != 0 && Has(&WXL_Api::Disconnect)) Api()->Disconnect(c);
                    c = {};
                }
                StopListeners();
            }
        };
    }
}
