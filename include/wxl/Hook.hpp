// SPDX-License-Identifier: GPL-3.0-or-later
// wxl::Hook<Fn>: a raw detour and the next link of its chain (low_level, unguarded).

#pragma once

#include "wxl/Core.hpp"

#include <cstdint>
#include <type_traits>

namespace wxl
{
    /// A raw detour of the function type Fn; needs "low_level" in wxl.json. The detour runs unguarded
    /// (a fault ends the game): prefer wxl::Intercept. Its link is taken back when the extension is disabled.
    template <class Fn>
    class Hook
    {
    public:
        /// The hooked function's type, with the pointer stripped when Fn is an alias for one.
        using Function = std::remove_pointer_t<Fn>;

        /// On a hook point the core knows by name. False before Bind, or for an unknown name.
        bool Attach(const char* pointName, Function* detour, int priority = WXL_HOOK_DEFAULT_PRIORITY)
        {
            const WXL_Api* api = Api();
            return api && api->HookAttachByName(pointName, reinterpret_cast<void*>(detour),
                                                reinterpret_cast<void**>(&m_original), priority) != 0;
        }

        /// On an address; `label` names it in the log.
        bool Attach(const char* label, uintptr_t target, Function* detour, int priority = WXL_HOOK_DEFAULT_PRIORITY)
        {
            const WXL_Api* api = Api();
            return api && api->HookAttach(label, target, reinterpret_cast<void*>(detour),
                                          reinterpret_cast<void**>(&m_original), priority) != 0;
        }

        /// A resident hook on an address: it stays attached when the extension is disabled or faults, and
        /// the DLL stays loaded. Its detour must not rely on the rest of the extension. False on a core
        /// without resident hooks.
        bool AttachResident(const char* label, uintptr_t target, Function* detour,
                            int priority = WXL_HOOK_DEFAULT_PRIORITY)
        {
            const WXL_Api* api = Api();
            return api && detail::Has(&WXL_Api::HookAttachEx) &&
                   api->HookAttachEx(label, target, reinterpret_cast<void*>(detour),
                                     reinterpret_cast<void**>(&m_original), priority, WXL_HOOK_RESIDENT) != 0;
        }

        explicit operator bool() const { return m_original != nullptr; }

        /// The next link, ending at the engine function; null until attached.
        Function* Original() const { return m_original; }

        /// Calls the next link. Not calling it skips the parties behind and the engine function.
        template <class... Args>
        decltype(auto) operator()(Args&&... args) const
        {
            return m_original(static_cast<Args&&>(args)...);
        }

    private:
        Function* m_original = nullptr;
    };
}
