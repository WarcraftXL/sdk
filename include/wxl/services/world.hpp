// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.world: questions about the world's geometry.

#ifndef WXL_SERVICE_WORLD_HPP
#define WXL_SERVICE_WORLD_HPP

#include <cstdint>
#include <optional>

#include "wxl/sdk.hpp"
#include "wxl/services/rows.hpp"
#include "wxl/services/world.h"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_world_api>
    {
#define WXL_API_TABLE "wxl/services/world.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief The typed facade over wxl.world.
     *
     *     // Is anything solid between the player's eyes and a point?
     *     if (auto hit = wxl::WorldService::Trace(eyes, point, WXL_TRACE_LINE_OF_SIGHT))
     *         Log::Info("blocked at %.1f %.1f %.1f", hit->x, hit->y, hit->z);
     *
     *   Trace(start, finish[, flags])  the first point of the world's geometry between the two,
     *                                or nothing when the way is clear
     *
     * flags is the client's hit-test mask (WXL_TRACE_CAMERA, WXL_TRACE_LINE_OF_SIGHT,
     * WXL_TRACE_LIQUID); 0 is the camera's. Units and players are never met.
     */
    class WorldService : public detail::Facade<WorldService, wxl_world_api>,
                         public api::facade::Methods<wxl_world_api>
    {
    public:
        static constexpr const char* kName    = WXL_WORLD_SERVICE;
        static constexpr uint32_t    kVersion = WXL_WORLD_SERVICE_VERSION;

        /// The first point of the world's geometry from start to finish; nothing when the way is
        /// clear, off the main thread, out of the world or on a runtime without the service.
        static std::optional<wxl_vec3> Trace(wxl_vec3 start, wxl_vec3 finish, uint32_t flags = WXL_TRACE_CAMERA)
        {
            const wxl_world_api* api = Raw();
            if (!detail::Carries(api, &wxl_world_api::trace)) return std::nullopt;
            wxl_vec3 hit{};
            if (api->trace(start, finish, flags, &hit) != WXL_TRUE) return std::nullopt;
            return hit;
        }
    };
} // namespace wxl

#endif // WXL_SERVICE_WORLD_HPP
