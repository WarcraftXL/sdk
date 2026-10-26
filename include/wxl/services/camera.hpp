// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.camera: where the world camera is, and placing it instead of the
// client.

#ifndef WXL_SERVICE_CAMERA_HPP
#define WXL_SERVICE_CAMERA_HPP

#include <cstdint>

#include "wxl/sdk.hpp"
#include "wxl/services/camera.h"
#include "wxl/services/rows.hpp"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_camera_api>
    {
#define WXL_API_TABLE "wxl/services/camera.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief The typed facade over wxl.camera.
     *
     *     class Behind : public wxl::RenderScript
     *     {
     *         void OnCameraUpdate(uint64_t, float) override
     *         {
     *             const auto pivot = wxl::CameraService::Pivot();
     *             if (!pivot) return;
     *             wxl_vec3 eye = *pivot;
     *             eye.z += 10.0f;                                // ten yards above the head
     *             wxl::CameraService::SetView(eye, {0.0f, 0.0f, -1.0f}, 0.0);
     *         }
     *     };
     *
     *   Position(), Forward(), Up()      where the camera is this frame (Expected<wxl_vec3>)
     *   Fov()                            its field of view in radians (Expected<double>)
     *   Pivot()                          the point the client's camera turns around
     *   Cinematic()                      whether a cinematic has the view
     *   SetView(position, forward, fov)  places the camera until ReleaseView(); fov 0 keeps the
     *                                    client's
     *   ReleaseView()                    gives it back to the client
     *   SetViewRolled(position, forward, fov, roll)
     *                                    SetView turned around the direction it looks, roll in
     *                                    radians, positive to the camera's right (abi 0.1.29)
     *   Roll()                           the roll it draws with this frame (abi 0.1.29)
     *
     * A view set from OnCameraUpdate applies to the frame being prepared. The reads answer
     * WXL_E_UNSUPPORTED out of the world, and on a runtime older than abi 0.1.24 (0.1.29 for
     * SetViewRolled and Roll).
     */
    class CameraService : public detail::Facade<CameraService, wxl_camera_api>,
                          public api::facade::Methods<wxl_camera_api>
    {
    public:
        static constexpr const char* kName    = WXL_CAMERA_SERVICE;
        static constexpr uint32_t    kVersion = WXL_CAMERA_SERVICE_VERSION;
    };
} // namespace wxl

#endif // WXL_SERVICE_CAMERA_HPP
