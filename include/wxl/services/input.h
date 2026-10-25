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
//
// A captured pointer stays where the game sees it: at the centre. The client goes on picking
// what is under it every frame and a click lands there, so the object at the centre of the screen
// is the mouseover, the target of a left click and the object of a right click, and nothing tells
// the game that a button is held. The mouse's own motion still arrives, as the MouseMotion event
// (raw input, in the device's counts); the wheel as MouseWheel, which a handler may keep from the
// game. The window losing the focus lets the pointer go for as long as it is away; it is held
// again when the window comes back.
//
// Capturing is counted per mod: the pointer is held while any mod holds it, and a mod's capture
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
} wxl_input_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_INPUT_H
