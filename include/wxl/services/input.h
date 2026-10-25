// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The input service: the pointer, for a mod that steers with the mouse.
//
//   capture_cursor      hides the pointer and holds it at the centre of the game's window,
//                       or lets it go
//   cursor_captured     whether some mod holds it there
//   pointer_over_world  whether the pointer is over the world, with no frame of the interface
//                       under it
//   block_mouse_look    keeps the game out of its own mouse look while a mod holds it: a held
//                       mouse button turns nothing, hides nothing, and a click still acts
//   mouse_look_blocked  whether some mod holds that
//
// A captured pointer stays where the game sees it: at the centre. The client goes on picking
// what is under it every frame and a click lands there, so the object at the centre of the screen
// is the mouseover, the target of a left click and the object of a right click, and nothing tells
// the game that a button is held. The mouse's own motion still arrives, as the MouseMotion event
// (raw input, in the device's counts); the wheel as MouseWheel, which a handler may keep from the
// game. The window losing the focus lets the pointer go for as long as it is away; it is held
// again when the window comes back.
//
// Blocking the game's mouse look: the game turns its camera with the left button held and its
// character with the right, hiding the pointer and forgetting what is under it while it does.
// Blocked, a button the game would start that with is never held for it: the press only notes
// what is under the pointer, and a release within the game's own click time (0.8 s) does what
// the click would have done (the left one selects, the right one acts: talk, loot, attack). A
// longer hold does nothing, as in the game. Its own MouselookStart and the move-with-both-buttons
// are kept out too. Blocking while the game is already looking stops it, with no click.
//
// Capturing and blocking are counted per mod: the pointer is held while any mod holds it, and a mod's capture
// goes with it when it is disabled or unloaded.
//
// Main thread only: a call off it answers WXL_E_INVALID.

#ifndef WXL_SERVICE_INPUT_H
#define WXL_SERVICE_INPUT_H

#include "wxl/abi.h"

#define WXL_INPUT_SERVICE         "wxl.input"
#define WXL_INPUT_SERVICE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wxl_input_api
{
    uint32_t struct_size;

    /**
     * @brief Hides the pointer and holds it at the centre of the game's window, or lets it go.
     *
     * WXL_TRUE captures it for the calling mod (again: nothing more), WXL_FALSE gives the mod's
     * capture back; the pointer is shown and moves again once no mod holds it.
     */
    wxl_result(WXL_CALL* capture_cursor)(wxl_bool captured);

    /// Whether some mod holds the pointer.
    wxl_bool(WXL_CALL* cursor_captured)(void);

    /// Whether the pointer is over the 3D world rather than a frame of the interface. False out
    /// of the world.
    wxl_bool(WXL_CALL* pointer_over_world)(void);

    /// Keeps the game out of its own mouse look for the calling mod (WXL_TRUE), or gives that back
    /// (WXL_FALSE); the game looks again once no mod blocks it. Abi 0.1.25.
    wxl_result(WXL_CALL* block_mouse_look)(wxl_bool blocked);

    /// Whether some mod keeps the game out of its mouse look. Abi 0.1.25.
    wxl_bool(WXL_CALL* mouse_look_blocked)(void);
} wxl_input_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_INPUT_H
