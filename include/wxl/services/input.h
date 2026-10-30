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
//   capture_cursor_at   capture_cursor at a chosen point of the window rather than its centre
//                       (abi 0.1.29)
//   capture_point       where the captured pointer is held (abi 0.1.29)
//
// A captured pointer stays where the game sees it: at the centre, or at the point a mod chose
// (capture_cursor_at). The client goes on picking what is under it every frame and a click lands
// there, so the object at that point of the screen is the mouseover, the target of a left click
// and the object of a right click (and of WorldClickRequest), and nothing tells the game that a
// button is held. A point is given as fractions of the window's client area from its top-left
// corner: x from 0 (left edge) to 1 (right edge), y from 0 (top edge) to 1 (bottom edge); the
// centre is (0.5, 0.5). The pointer sits on the pixel holding that point, so it follows the window
// when it is resized. When several mods hold the pointer, it is held at the point of the one that
// captured or moved it last; when that one lets go, at the point of the one before. The mouse's own motion still arrives, as the MouseMotion event
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
     * WXL_TRUE captures it for the calling mod at the centre (again: nothing more than moving
     * the mod's hold back to the centre), WXL_FALSE gives the mod's capture back; the pointer is
     * shown and moves again once no mod holds it.
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

    /**
     * @brief Hides the pointer and holds it at a point of the game's window, for the calling
     *        mod; a mod that holds it already moves its hold there. Abi 0.1.29.
     * @param x  from the left edge of the window's client area, 0 to 1
     * @param y  from its top edge, 0 to 1; (0.5, 0.5) is the centre, where capture_cursor holds it
     *
     * Outside 0 to 1, or not a number, is WXL_E_INVALID. capture_cursor(WXL_TRUE) is
     * capture_cursor_at(0.5, 0.5); capture_cursor(WXL_FALSE) gives the hold back.
     */
    wxl_result(WXL_CALL* capture_cursor_at)(double x, double y);

    /// Where the captured pointer is held, as capture_cursor_at takes it: x and y, z 0.
    /// WXL_E_NOT_FOUND when no mod holds the pointer. Abi 0.1.29.
    wxl_result(WXL_CALL* capture_point)(wxl_vec3* out);
} wxl_input_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_INPUT_H
