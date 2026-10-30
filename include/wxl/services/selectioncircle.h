// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The selection circle service: the circles the game projects on the ground under a unit, hidden
// or drawn in another texture, colour or size.
//
//   hide              hides the circles of some kinds, for units of some reactions
//   set_style         draws them in another texture, colour or size
//   reset             gives them back the game's look
//   hide_object       hides every circle drawn for one object
//   set_object_style  draws them in another texture, colour or size
//   reset_object      gives them back
//   reset_all         gives back everything the calling mod changed
//   drawn             how many circles the last world frame drew
//   hidden            how many circles the last world frame left out
//
// The kinds, as the game draws them, all with the same texture (Textures\UnitSelectTexture.blp)
// added onto the ground, the terrain and the buildings under them:
//
//   WXL_SELECTIONCIRCLE_TARGET       the circle under the player's target, a unit or a corpse, in
//                                    the colour of the unit's reaction (red hostile, yellow neutral,
//                                    green friendly; a corpse in its own), with the ring that closes on it
//                                    when the player starts attacking it. The game draws none under
//                                    the player itself, nor while its ObjectSelectionCircle CVar is 0.
//   WXL_SELECTIONCIRCLE_PET_TARGET   the blue ring that closes on the pet's new target for a second
//   WXL_SELECTIONCIRCLE_MOVE_MARKER  the pulsing marker where click-to-move or an attack move goes
//
// A mask of kinds: 0 is every kind. A mask of reactions, of the unit the circle is drawn for
// towards the active player: WXL_SELECTIONCIRCLE_REACTION(WXL_REACTION_HOSTILE) and the like, and
// WXL_SELECTIONCIRCLE_NO_REACTION for a circle under no unit (a corpse, the move marker) or a unit
// whose reaction cannot be read; 0 is every reaction and none.
//
// A style: `texture`, the path of a texture file (BLP) of the client's search path drawn in place
// of the game's, empty for the game's; it is added onto the ground like the game's, so black is
// nothing and white the colour at full, and its edge should be black. `color`, 0xAARRGGBB, in
// place of the game's colour, 0 for the game's: the alpha scales the circle's brightness, and the
// game's own fades (the closing rings, the marker) still apply over it. `scale`, the circle's
// size against the game's (1 the game's, 0 for the game's), up to WXL_SELECTIONCIRCLE_MAX_SCALE.
//
// Which look a circle takes: an object's own (hide_object, set_object_style) before its kind's and
// reaction's (hide, set_style); among several mods' of the same level, the last set. A mod's
// changes go when it resets them, when it is disabled or unloaded; an object's when it leaves
// view. A texture is loaded at set_style and drawn once loaded: the game's look until then.
//
// Main thread only: a call off it answers WXL_E_INVALID.

#ifndef WXL_SERVICE_SELECTIONCIRCLE_H
#define WXL_SERVICE_SELECTIONCIRCLE_H

#include "wxl/abi.h"

#define WXL_SELECTIONCIRCLE_SERVICE         "wxl.selectioncircle"
#define WXL_SELECTIONCIRCLE_SERVICE_VERSION 1

/// The kinds of circle, as a mask.
#define WXL_SELECTIONCIRCLE_TARGET      0x1u
#define WXL_SELECTIONCIRCLE_PET_TARGET  0x2u
#define WXL_SELECTIONCIRCLE_MOVE_MARKER 0x4u
#define WXL_SELECTIONCIRCLE_ALL_KINDS   0x7u

/// A reaction (a wxl_reaction, 0 hated to 7 exalted) as a bit of a mask of reactions.
#define WXL_SELECTIONCIRCLE_REACTION(reaction) (1u << (reaction))
/// A circle under no unit, or under a unit whose reaction cannot be read.
#define WXL_SELECTIONCIRCLE_NO_REACTION  0x100u
#define WXL_SELECTIONCIRCLE_ALL_REACTIONS 0x1FFu

/// The largest scale a style draws a circle at.
#define WXL_SELECTIONCIRCLE_MAX_SCALE 8.0f
/// How many objects can have a look of their own at once, all mods together.
#define WXL_SELECTIONCIRCLE_MAX_OBJECTS 64

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wxl_selectioncircle_api
{
    uint32_t struct_size;

    /**
     * @brief Hides the circles of these kinds drawn for units of these reactions.
     * @param kinds      a mask of WXL_SELECTIONCIRCLE_TARGET...; 0 for every kind
     * @param reactions  a mask of WXL_SELECTIONCIRCLE_REACTION(...) and _NO_REACTION; 0 for all
     *
     * WXL_E_INVALID for a bit that names no kind or reaction.
     */
    wxl_result(WXL_CALL* hide)(uint32_t kinds, uint32_t reactions);

    /**
     * @brief Draws the circles of these kinds, for units of these reactions, in another look.
     * @param texture  a texture file drawn in place of the game's; empty for the game's
     * @param color    0xAARRGGBB in place of the game's colour; 0 for the game's
     * @param scale    the size against the game's, up to WXL_SELECTIONCIRCLE_MAX_SCALE; 0 for the
     *                 game's
     *
     * WXL_E_INVALID for a bit that names no kind or reaction, or a scale that is no number, below
     * 0 or above the maximum; WXL_E_NOT_FOUND for a texture the client makes nothing of.
     */
    wxl_result(WXL_CALL* set_style)(uint32_t kinds, uint32_t reactions, wxl_str texture, uint32_t color, float scale);

    /// Gives the circles of these kinds and reactions back the game's look, as far as the
    /// calling mod changed it (hide or set_style). WXL_E_INVALID for a bit that names nothing.
    wxl_result(WXL_CALL* reset)(uint32_t kinds, uint32_t reactions);

    /// Hides every circle drawn for an object: under it as the target, as the pet's target, or
    /// the move marker of the unit moving. WXL_E_NOT_FOUND for an object gone, WXL_E_INVALID when
    /// WXL_SELECTIONCIRCLE_MAX_OBJECTS objects have a look of their own already.
    wxl_result(WXL_CALL* hide_object)(wxl_handle target);

    /// Draws every circle drawn for an object in another look, as set_style does.
    /// WXL_E_NOT_FOUND for an object gone or a texture the client makes nothing of, WXL_E_INVALID for a scale out of range or when
    /// WXL_SELECTIONCIRCLE_MAX_OBJECTS objects have a look of their own already.
    wxl_result(WXL_CALL* set_object_style)(wxl_handle target, wxl_str texture, uint32_t color, float scale);

    /// Gives an object's circles back the look of their kind. WXL_E_NOT_FOUND when the calling
    /// mod had not changed it.
    wxl_result(WXL_CALL* reset_object)(wxl_handle target);

    /// Gives back everything the calling mod changed.
    void(WXL_CALL* reset_all)(void);

    /// How many circles the last world frame drew (the closing ring of an attack counts as one
    /// more): 0 where none was drawn.
    uint32_t(WXL_CALL* drawn)(void);

    /// How many circles the last world frame left out because a mod hid them.
    uint32_t(WXL_CALL* hidden)(void);
} wxl_selectioncircle_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_SELECTIONCIRCLE_H
