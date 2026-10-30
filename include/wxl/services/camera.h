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
//   set_view_rolled              set_view with a roll: the camera turned around the direction it
//                                looks (abi 0.1.29)
//   roll                         the roll the camera draws with this frame (abi 0.1.29)
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
// The roll is an angle in radians around the direction the camera looks, from the camera the
// client would build for that direction: its right level with the ground, its up as near the
// world's up as the direction allows. Positive rolls the camera to its right, as an aircraft
// banking right: the top of the screen leans right and the world on the screen turns the other
// way. 0 is exactly the picture set_view gives. The roll goes where the client keeps the camera's
// up (the third row of CSimpleCamera::m_facing), which its view matrix is built from
// (CSimpleCamera::SetGxProjectionAndView looks along Forward() with Up()): the world, its picking
// under the pointer (CGWorldFrame::HitTestPoint casts its ray through that matrix) and what the
// client projects with it (the name plates, the sound's listener) turn together. The interface is
// drawn with a projection of its own and does not turn.
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

    /// The field of view it draws with, in radians: the client's FOV(), of which its projection
    /// takes 0.6 as the vertical angle (CSimpleCamera::SetGxProjectionAndView).
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

    /**
     * @brief set_view with a roll. Abi 0.1.29.
     * @param position  where the camera is
     * @param forward   the direction it looks; its length does not matter, a zero one is
     *                  WXL_E_INVALID
     * @param fov       the field of view in radians, or 0 to keep the client's; past 0 to pi is
     *                  WXL_E_INVALID
     * @param roll      radians around `forward`, positive to the camera's right; 0 is set_view's
     *                  picture. Any finite angle, taken modulo a turn; a non-finite one is
     *                  WXL_E_INVALID
     */
    wxl_result(WXL_CALL* set_view_rolled)(wxl_vec3 position, wxl_vec3 forward, double fov, double roll);

    /// The roll the world camera draws with this frame, in radians from -pi to pi, positive to
    /// its right: 0 for the client's own camera on level ground. Abi 0.1.29.
    wxl_result(WXL_CALL* roll)(double* out);
} wxl_camera_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_CAMERA_H
