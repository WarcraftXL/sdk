// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The action service: what the player can do, done through the client's own functions.
//
// Each call does what a click, a key or a slash command of the player does, through the
// function the client runs for it, with the client's checks: a spell out of range, an item on
// cooldown or a target out of reach is refused by the client as it would refuse the player,
// and what the server is told is what it is told then. Nothing is checked beyond that: an
// action needs no key press and no confirmation.
//
//   target        targets an object, as a left click on it does
//   clear_target  clears the target, as Escape does
//   interact      a right click on an object: talks to an NPC, loots a corpse, opens a chest,
//                 uses a game object; the object becomes the target
//   cast_spell    casts a spell the player knows, at a unit or at the current target, as the
//                 client's CastSpellByID does
//   stop_casting  stops the cast or channel in progress, as the client's SpellStopCasting does
//   use_item      uses an item the player holds, on a unit or on what the item picks itself
//   follow        follows a unit, as /follow does
//   assist        targets what a unit targets, as /assist does
//   move_to       walks the player to a point, as a click-to-move click on the ground does
//   face          turns the player to a facing, as steering with the mouse does, and tells the
//                 server
//
// Main thread only: an action is refused off it with WXL_E_INVALID, and the runtime's log says
// why. Each answers WXL_OK when the client took the action, WXL_E_FAILED when the client
// refused it (nothing in view under the handle, no spell slot, a game object it will not use),
// WXL_E_NOT_FOUND for a handle gone stale, WXL_E_INVALID for an object of the wrong kind, and
// WXL_E_UNSUPPORTED while out of the world.

#ifndef WXL_SERVICE_ACTION_H
#define WXL_SERVICE_ACTION_H

#include "wxl/abi.h"

#define WXL_ACTION_SERVICE         "wxl.action"
#define WXL_ACTION_SERVICE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wxl_action_api
{
    uint32_t struct_size;

    /// Targets an object, as a left click on it does: CGGameUI::Target.
    wxl_result(WXL_CALL* target)(wxl_handle object);

    /// Clears the target, as Escape does; WXL_OK with nothing targeted too.
    wxl_result(WXL_CALL* clear_target)(void);

    /**
     * @brief Right-clicks an object: CGGameUI::OnSpriteRightClick.
     *
     * What follows is the object's own: an NPC opens its dialogue, a corpse its loot, a chest
     * opens, a game object is used. The object becomes the target. WXL_E_FAILED when the client
     * does not hold the object.
     */
    wxl_result(WXL_CALL* interact)(wxl_handle object);

    /**
     * @brief Casts a spell the player knows, as CastSpellByID does: Spell_C::CastSpell.
     * @param target  the unit to cast it at, or WXL_HANDLE_NONE for the current target, or for
     *                the player itself when the spell takes none.
     */
    wxl_result(WXL_CALL* cast_spell)(uint32_t spell_id, wxl_handle target);

    /// Stops the cast or the channel in progress, or the repeating spell: as SpellStopCasting.
    /// WXL_E_FAILED when there was none.
    wxl_result(WXL_CALL* stop_casting)(void);

    /**
     * @brief Uses an item the player holds: CGItem_C::UseItem.
     * @param target  the unit to use it on, or WXL_HANDLE_NONE for what the item picks itself.
     */
    wxl_result(WXL_CALL* use_item)(wxl_handle item, wxl_handle target);

    /// Follows a unit, as /follow does: CGUnit_C::SetTrackingTarget on the unit the player
    /// moves. WXL_E_FAILED for a unit the client will not follow.
    wxl_result(WXL_CALL* follow)(wxl_handle unit);

    /// Targets what a unit targets, as /assist does. WXL_E_FAILED when it targets nothing in
    /// view.
    wxl_result(WXL_CALL* assist)(wxl_handle unit);

    /// Walks the player to a point, as a click-to-move click on the ground does:
    /// CGPlayer_C::ClickToMove. WXL_E_FAILED for a point the client will not walk to.
    wxl_result(WXL_CALL* move_to)(wxl_vec3 point);

    /**
     * @brief Turns the player to a facing, as steering with the mouse does:
     *        CGInputControl::CameraTurnPlayer, which moves the character at once and tells the
     *        server.
     * @param facing  radians, the client's: 0 is north (+x), growing towards the west (+y)
     *
     * WXL_E_FAILED when the client will not turn the player now: dead, stunned, sitting, on a
     * flight path, or following a script's spline. Abi 0.1.23.
     */
    wxl_result(WXL_CALL* face)(double facing);
} wxl_action_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_ACTION_H
