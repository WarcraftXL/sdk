// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The animation service: an animation of the game played on a unit by a mod, through the
// client's own animation of units.
//
//   play     plays an animation (an AnimationData id) on a unit: once, or looped until stopped
//   stop     ends the calling mod's animation on a unit and hands the unit back to the game
//   playing  the animation a mod plays on a unit, as it was asked for
//   current  the animation the unit's model shows now, whoever chose it
//
// --- How it plays ------------------------------------------------------------------------------
//
// The unit plays the animation as it plays an emote: the client's own CGUnit_C::PlayBaseAnimation
// chooses the bones (the whole body standing still; the upper body only while the unit moves,
// over the legs' run), the model's nearest animation when it has not that one (AnimationData's
// fallback, unless WXL_ANIMATION_EXACT), the weapons sheathed or drawn as AnimationData says, and
// a blend from the pose it leaves. Nothing is sent to the server: the animation is seen on this
// client only.
//
// The game's own choices come first. Moving, fighting, casting, an emote, sitting or dying
// replace the mod's animation as they replace an emote; a one-shot so replaced has ended, a loop
// comes back by itself as soon as the unit is idle again (standing, swimming or hovering with
// nothing else to show). WXL_ANIMATION_OVERRIDE, for a loop, plays it again every time the game
// replaced it, at once, death excepted: while the unit moves the client then puts it on the upper
// body over the legs' movement. A one-shot ignores it: replaced, it has ended. A one-shot ends after one play of its length (the speed counted in); a
// model whose sequence would go on is handed back to the game then.
//
// A unit plays one mod animation at a time: a play replaces the one before, whichever mod's. The
// animations of a mod end when it is disabled or unloaded (wxl.lua's, when that Lua mod goes),
// and a unit's when it leaves view. UnitAnimationEnded says when and why each one ended.
//
// Animation ids are AnimationData.dbc's (enumerated in the guides): 0 Stand, 16 AttackUnarmed,
// 17 Attack1H, 18 Attack2H, 53 SpellCastDirected, 62 EmoteWork (the client's mining swing),
// 136 EmoteWorkNoSheathe (the same with the weapon kept in hand), ...
//
// Main thread only: a call off it answers WXL_E_INVALID.

#ifndef WXL_SERVICE_ANIMATION_H
#define WXL_SERVICE_ANIMATION_H

#include "wxl/abi.h"

#define WXL_ANIMATION_SERVICE         "wxl.animation"
#define WXL_ANIMATION_SERVICE_VERSION 1

/// play's flags.
#define WXL_ANIMATION_LOOP     0x1u ///< played again at each end, until stopped
#define WXL_ANIMATION_NO_BLEND 0x2u ///< starts at once, without the blend from the pose the unit leaves
#define WXL_ANIMATION_OVERRIDE 0x4u ///< a loop played again at once whenever the game replaced it, death excepted; a one-shot ignores it
#define WXL_ANIMATION_EXACT    0x8u ///< this animation only: no fallback to the model's nearest one
#define WXL_ANIMATION_FLAGS    0xFu ///< every flag play knows; another bit is refused

/// The highest anim id play takes: AnimationData.dbc's ids of 3.3.5a end below it.
#define WXL_ANIMATION_MAX_ID 505u
/// The speeds play takes, 1 the animation's own.
#define WXL_ANIMATION_MIN_SPEED 0.05
#define WXL_ANIMATION_MAX_SPEED 20.0

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wxl_animation_api
{
    uint32_t struct_size;

    /**
     * @brief Plays an animation on a unit (a creature or a player in view), as the game plays an
     *        emote, and keeps it as the header says.
     * @param anim_id  an AnimationData id, 0 to WXL_ANIMATION_MAX_ID
     * @param flags    WXL_ANIMATION_* bits; 0 plays it once, blended, yielding to the game
     * @param speed    1 the animation's own; 0 is 1; WXL_ANIMATION_MIN_SPEED to WXL_ANIMATION_MAX_SPEED
     *
     * WXL_E_NOT_FOUND for a unit gone; WXL_E_INVALID for an object that is no unit, an id, a flag
     * or a speed out of range; WXL_E_FAILED when the client did not play it: its model not loaded
     * yet, the unit dead, or (with WXL_ANIMATION_EXACT) the model without that animation.
     */
    wxl_result(WXL_CALL* play)(wxl_handle unit, uint32_t anim_id, uint32_t flags, double speed);

    /// Ends the calling mod's animation on a unit and has the game choose the unit's animation
    /// again at once. WXL_E_NOT_FOUND when the calling mod plays none on it.
    wxl_result(WXL_CALL* stop)(wxl_handle unit);

    /// The animation a mod plays on a unit, as play was asked for it (before any fallback).
    /// WXL_E_NOT_FOUND when no mod plays one on it.
    wxl_result(WXL_CALL* playing)(wxl_handle unit, uint32_t* out_anim_id);

    /// The animation the unit's model shows now (its upper body's when the client plays one
    /// there, else the whole body's), whoever chose it. WXL_E_FAILED while its model loads.
    wxl_result(WXL_CALL* current)(wxl_handle unit, uint32_t* out_anim_id);
} wxl_animation_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_ANIMATION_H
