// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The pose service: a unit's head turned by a mod, over what its animation does.
//
//   head          turns a unit's head by a yaw and a pitch from where its body faces, reached
//                 smoothly and held until released
//   release_head  gives the head back to the animation, smoothly
//
// The head is the model's head key bone (the bone the game itself twists when a unit turns), and
// the turn is applied after the game has animated the unit for the frame, every frame, before it
// is drawn. Angles are in radians: a positive yaw turns the head to the unit's left (the client's
// facings grow that way), a positive pitch raises it. They are held to what a neck allows: a yaw
// within 100 degrees either way, a pitch within 80; a mod keeps its own narrower limits. A head
// is one mod's: the last mod to turn it holds it. A mod's heads are released when it is disabled
// or unloaded, and a unit's when it leaves view.
//
// Main thread only: a call off it answers WXL_E_INVALID.

#ifndef WXL_SERVICE_POSE_H
#define WXL_SERVICE_POSE_H

#include "wxl/abi.h"

#define WXL_POSE_SERVICE         "wxl.pose"
#define WXL_POSE_SERVICE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wxl_pose_api
{
    uint32_t struct_size;

    /**
     * @brief Turns a unit's head from where its body faces, and holds it there.
     * @param yaw        radians, positive to the unit's left
     * @param pitch      radians, positive up
     * @param smoothing  seconds to cover half the way from where the head is; 0 at once
     *
     * WXL_E_NOT_FOUND for a unit gone, WXL_E_INVALID for an object that is no unit or a value
     * that is no number.
     */
    wxl_result(WXL_CALL* head)(wxl_handle unit, double yaw, double pitch, double smoothing);

    /// Gives a unit's head back to its animation, as smoothly as it was turned. WXL_E_NOT_FOUND
    /// when the calling mod does not hold it.
    wxl_result(WXL_CALL* release_head)(wxl_handle unit);
} wxl_pose_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_POSE_H
