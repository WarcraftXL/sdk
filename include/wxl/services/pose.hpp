// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.pose: a unit's head turned by a mod.

#ifndef WXL_SERVICE_POSE_HPP
#define WXL_SERVICE_POSE_HPP

#include <cstdint>

#include "wxl/sdk.hpp"
#include "wxl/services/pose.h"
#include "wxl/services/rows.hpp"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_pose_api>
    {
#define WXL_API_TABLE "wxl/services/pose.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief The typed facade over wxl.pose.
     *
     *     // The player looks 30 degrees to its right and a little up, reached in a tenth of a second.
     *     wxl::PoseService::Head(wxl::Player::Local(), -0.52, 0.1, 0.1);
     *     ...
     *     wxl::PoseService::ReleaseHead(wxl::Player::Local());
     *
     *   Head(unit, yaw, pitch, smoothing)   turns the head from where the body faces and holds it
     *   ReleaseHead(unit)                   gives it back to the animation
     *
     * Radians: a positive yaw to the unit's left, a positive pitch up; smoothing in seconds (half
     * the way). WXL_E_UNSUPPORTED on a runtime older than abi 0.1.25.
     */
    class PoseService : public detail::Facade<PoseService, wxl_pose_api>, public api::facade::Methods<wxl_pose_api>
    {
    public:
        static constexpr const char* kName    = WXL_POSE_SERVICE;
        static constexpr uint32_t    kVersion = WXL_POSE_SERVICE_VERSION;
    };
} // namespace wxl

#endif // WXL_SERVICE_POSE_HPP
