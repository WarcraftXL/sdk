// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.action: what the player can do, through the client's own functions.

#ifndef WXL_SERVICE_ACTION_HPP
#define WXL_SERVICE_ACTION_HPP

#include <cstdint>

#include "wxl/objects.hpp"
#include "wxl/sdk.hpp"
#include "wxl/services/action.h"
#include "wxl/services/rows.hpp"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_action_api>
    {
#define WXL_API_TABLE "wxl/services/action.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief The typed facade over wxl.action.
     *
     *     void OnTargetChanged(wxl::Object, wxl::Object current) override
     *     {
     *         wxl::Unit unit{current};
     *         if (unit.Reaction().ValueOr(WXL_REACTION_NEUTRAL) <= WXL_REACTION_HOSTILE)
     *             wxl::ActionService::CastSpell(133, unit);   // a fireball at it
     *     }
     *
     * Every method answers a wxl_result, as action.h says: WXL_OK when the client took the
     * action, WXL_E_FAILED when it refused it, WXL_E_NOT_FOUND for an object gone, WXL_E_INVALID
     * for an object of the wrong kind or a call off the main thread, WXL_E_UNSUPPORTED on a
     * runtime without the service (older than abi 0.1.18) or out of the world.
     *
     *   Target(object)        targets it, as a left click does
     *   ClearTarget()         clears the target
     *   Interact(object)      right-clicks it: talk, loot, open, use
     *   CastSpell(id[, unit]) casts a spell, at the unit or the current target
     *   StopCasting()         stops the cast in progress
     *   UseItem(item[, unit]) uses an item, on the unit or what the item picks
     *   Follow(unit)          follows it
     *   Assist(unit)          targets what it targets
     *   MoveTo(point)         walks there, as click-to-move does
     *   Face(radians)         turns the player that way, as steering with the mouse does (abi 0.1.24)
     *
     * The objects have the same as methods: unit.Select(), unit.Interact(), unit.CastAt(id),
     * item.Use(), unit.Follow(), unit.Assist().
     */
    class ActionService : public detail::Facade<ActionService, wxl_action_api>,
                          public api::facade::Methods<wxl_action_api>
    {
    public:
        static constexpr const char* kName    = WXL_ACTION_SERVICE;
        static constexpr uint32_t    kVersion = WXL_ACTION_SERVICE_VERSION;

        /// Casts a spell the player knows at a unit, or at the current target for a null one.
        static wxl_result CastSpell(uint32_t spell_id, Object target = Object())
        {
            const wxl_action_api* api = Raw();
            if (!detail::Carries(api, &wxl_action_api::cast_spell)) return WXL_E_UNSUPPORTED;
            return api->cast_spell(spell_id, target.Handle());
        }

        /// Uses an item the player holds, on a unit, or on what the item picks for a null one.
        static wxl_result UseItem(Object item, Object target = Object())
        {
            const wxl_action_api* api = Raw();
            if (!detail::Carries(api, &wxl_action_api::use_item)) return WXL_E_UNSUPPORTED;
            return api->use_item(item.Handle(), target.Handle());
        }
    };
} // namespace wxl

#endif // WXL_SERVICE_ACTION_HPP
