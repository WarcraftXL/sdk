// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.gx: the client's Direct3D 9 device.

#ifndef WXL_SERVICE_GX_HPP
#define WXL_SERVICE_GX_HPP

#include <cstdint>

#include "wxl/sdk.hpp"
#include "wxl/services/gx.h"
#include "wxl/services/rows.hpp"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_gx_api>
    {
#define WXL_API_TABLE "wxl/services/gx.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief The typed facade over wxl.gx, used from a RenderScript's OnModelBatchDrawn to draw a
     *        batch again with state of the mod's own.
     *
     *   D3d9Device()       the client's IDirect3DDevice9, null for none (cast it)
     *   InvalidateState()  the client sends all of its device state again before its next draw
     *
     * A mod that changes device state calls InvalidateState once it is done, and puts back the
     * render target it found.
     */
    class GxService : public detail::Facade<GxService, wxl_gx_api>, public api::facade::Methods<wxl_gx_api>
    {
    public:
        static constexpr const char* kName    = WXL_GX_SERVICE;
        static constexpr uint32_t    kVersion = WXL_GX_SERVICE_VERSION;

        /// The calling mod's shader provider (abi 0.1.43): @p fn is shown each program the device
        /// makes and may answer other DX9 bytecode for it; null takes it off.
        static wxl_result SetShaderProvider(wxl_gx_shader_fn fn, void* user = nullptr)
        {
            const wxl_gx_api* api = Raw();
            if (!detail::Carries(api, &wxl_gx_api::set_shader_provider)) return WXL_E_UNSUPPORTED;
            if (!fn) return api->set_shader_provider(nullptr);
            wxl_gx_shader_provider provider{};
            provider.struct_size = sizeof provider;
            provider.fn          = fn;
            provider.user        = user;
            return api->set_shader_provider(&provider);
        }
    };
} // namespace wxl

#endif // WXL_SERVICE_GX_HPP
