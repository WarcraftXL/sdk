// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The camera service: where the world camera is, and placing it instead of the client.
//
// The client places its world camera once a frame (CGCamera::UpdateCallback): it follows the
// object it is set on, the player most of the time, at the distance, pitch and yaw the player
// chose, pulled in by the world's geometry. Everything drawn that frame is seen from where it
// left the camera, and the client's own picking -- what is under the mouse, where a click lands --
// casts its rays from there too.
//
//   position, forward, up, fov   where the camera is this frame, as the client draws from it
//   pivot                        the point the client's camera turns around: the followed
//                                object's position, raised to the height the client looks from
//   cinematic                    whether a cinematic or a scripted model camera has the view
//   set_view                     places the camera from now on: a position, a direction, and a
//                                field of view or 0 to keep the client's
//   release_view                 gives the camera back to the client
//
// The view a mod sets is applied at the end of every update of the camera until it releases it,
// after the CameraUpdate event, so a mod that recomputes it there each frame moves with the
// frame. The client still runs its own camera underneath: releasing the view puts back the
// client's, which never stopped following the player. Only one view is applied: the one set
// last; when its mod releases it or goes, the one set before, if its mod still holds it.
//
// Coordinates are the client's world coordinates, in yards: x north, y west, z up. A direction
// need not be of unit length. The field of view is in radians, the angle the client's own
// "cameraFov" setting is.
//
// Main thread only: a call off it answers WXL_E_INVALID. Out of the world (no camera yet) the
// reads answer WXL_E_UNSUPPORTED.

#ifndef WXL_SERVICE_CAMERA_H
#define WXL_SERVICE_CAMERA_H

#include "wxl/abi.h"

#define WXL_CAMERA_SERVICE         "wxl.camera"
#define WXL_CAMERA_SERVICE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wxl_camera_api
{
    uint32_t struct_size;

    /// Where the world camera is this frame.
    wxl_result(WXL_CALL* position)(wxl_vec3* out);

    /// The direction it looks, of unit length, in world coordinates.
    wxl_result(WXL_CALL* forward)(wxl_vec3* out);

    /// The direction the top of the screen is, of unit length, in world coordinates.
    wxl_result(WXL_CALL* up)(wxl_vec3* out);

    /// The field of view it draws with, in radians.
    wxl_result(WXL_CALL* fov)(double* out);

    /**
     * @brief The point the client's camera turns around: the followed object's position raised
     *        to the height the client looks from (higher for a tauren than a gnome, higher on a
     *        mount). WXL_E_NOT_FOUND when the camera follows nothing.
     */
    wxl_result(WXL_CALL* pivot)(wxl_vec3* out);

    /// Whether a cinematic or a model camera of the client's has the view, which the view a mod
    /// sets does not override.
    wxl_bool(WXL_CALL* cinematic)(void);

    /**
     * @brief Places the world camera instead of the client, from the next update on, until the
     *        mod releases it.
     * @param position  where the camera is
     * @param forward   the direction it looks; its length does not matter, a zero one is
     *                  WXL_E_INVALID
     * @param fov       the field of view in radians, or 0 to keep the client's; past 0 to pi is
     *                  WXL_E_INVALID
     */
    wxl_result(WXL_CALL* set_view)(wxl_vec3 position, wxl_vec3 forward, double fov);

    /// Gives the camera back to the client. WXL_E_NOT_FOUND when the mod holds no view.
    wxl_result(WXL_CALL* release_view)(void);
} wxl_camera_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_CAMERA_H
