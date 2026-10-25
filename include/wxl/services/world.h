// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The world service: questions about the world's geometry.
//
//   trace   the first point of the world's geometry on a segment, as the client's own line tests
//           find it (World::Intersect): the ground, buildings and the collision of the objects
//           placed in the world, with no unit and no player in it
//
// What a trace meets is the client's hit-test mask. The presets are the masks the client uses
// itself; any other mask the client takes is passed on as it is.
//
// Main thread only: a call off it answers WXL_E_INVALID. Out of the world nothing is loaded and
// nothing is met.

#ifndef WXL_SERVICE_WORLD_H
#define WXL_SERVICE_WORLD_H

#include "wxl/abi.h"

#define WXL_WORLD_SERVICE         "wxl.world"
#define WXL_WORLD_SERVICE_VERSION 1

/// What the client's camera stops at: the ground, buildings, doodads, and the transports.
/// The mask of a trace given 0.
#define WXL_TRACE_CAMERA 0x00100171u
/// What a spell's line of sight and a missile's flight stop at.
#define WXL_TRACE_LINE_OF_SIGHT 0x00100111u
/// The surface of water, lava and slime alone.
#define WXL_TRACE_LIQUID 0x00020000u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wxl_world_api
{
    uint32_t struct_size;

    /**
     * @brief The first point of the world's geometry between two points.
     * @param flags  the client's hit-test mask: WXL_TRACE_CAMERA, WXL_TRACE_LINE_OF_SIGHT,
     *               WXL_TRACE_LIQUID or another the client takes; 0 is WXL_TRACE_CAMERA
     * @param hit    where the segment first meets it, written only when it does
     * @return WXL_TRUE when the segment meets something; WXL_FALSE when it is clear, off the
     *         main thread, or out of the world
     */
    wxl_bool(WXL_CALL* trace)(wxl_vec3 start, wxl_vec3 finish, uint32_t flags, wxl_vec3* hit);
} wxl_world_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_WORLD_H
