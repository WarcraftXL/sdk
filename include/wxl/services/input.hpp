// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.input: the pointer, for a mod that steers with the mouse.

#ifndef WXL_SERVICE_INPUT_HPP
#define WXL_SERVICE_INPUT_HPP

#include <cstdint>

#include "wxl/sdk.hpp"
#include "wxl/services/input.h"
#include "wxl/services/rows.hpp"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_input_api>
    {
#define WXL_API_TABLE "wxl/services/input.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief The typed facade over wxl.input.
     *
     *     wxl::InputService::CaptureCursor(true);   // hidden, held at the centre
     *
     *     void OnMouseMotion(int32_t dx, int32_t dy) override { yaw_ -= dx * 0.003f; }
     *
     *   CaptureCursor(bool)   holds the pointer at the centre of the window, or lets it go
     *   CursorCaptured()      whether some mod holds it
     *   PointerOverWorld()    whether the pointer is over the world, not the interface
     *   BlockMouseLook(bool)  keeps the game out of its own mouse look; clicks still act (0.1.25)
     *   MouseLookBlocked()    whether some mod does
     *   CaptureCursorAt(x, y) holds it at a point of the window instead, fractions from the
     *                         top-left corner, (0.5, 0.5) the centre (0.1.29)
     *   CapturePoint()        where it is held (Expected<wxl_vec3>: x, y, z 0) (0.1.29)
     *
     * A held pointer is where the game picks and clicks: the object under it (at the centre of
     * the screen, or the point given) is the mouseover, and a click lands on it. The motion arrives as WorldScript's
     * OnMouseMotion, the wheel as OnMouseWheel. CaptureCursor answers WXL_E_UNSUPPORTED on a
     * runtime older than abi 0.1.24.
     */
    class InputService : public detail::Facade<InputService, wxl_input_api>,
                         public api::facade::Methods<wxl_input_api>
    {
    public:
        static constexpr const char* kName    = WXL_INPUT_SERVICE;
        static constexpr uint32_t    kVersion = WXL_INPUT_SERVICE_VERSION;
    };
} // namespace wxl

#endif // WXL_SERVICE_INPUT_HPP
