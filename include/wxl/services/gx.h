// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The graphics service: which device the client draws with, and its Direct3D 9 device for a mod
// that draws with it.
//
//   d3d9_device       the client's IDirect3DDevice9; null on any other device
//   invalidate_state  the client sends its own device state again before its next draw
//   api               the API the client's device runs on (version 2)
//
// The model batches the client draws reach a mod as the event ModelBatchDrawn (Render family):
// the numbers of the device's draw, with everything it drew with still bound on Direct3D 9, so
// that the same draw issued again redraws the batch, into another render target or with another
// pixel shader (an outline, a glow). On the Vulkan renderer (api answers WXL_GX_BACKEND_VULKAN),
// a mod draws through wxl.render instead, and redraws a batch with its draw_batch.
//
// Raw: the device and its state are the client's own, and what a mod changes on it the
// client does not know about until the mod says so through invalidate_state. Calls are made on
// the client's main thread, from inside a frame.

#ifndef WXL_SERVICE_GX_H
#define WXL_SERVICE_GX_H

#include "wxl/abi.h"
#include "wxl/events.h"

#define WXL_GX_SERVICE         "wxl.gx"
#define WXL_GX_SERVICE_VERSION 2

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wxl_gx_api
{
    uint32_t struct_size;

    /// The client's IDirect3DDevice9, or null when it runs on no D3D9 device or has none yet.
    void*(WXL_CALL* d3d9_device)(void);

    /**
     * @brief Tells the client that device state changed behind it.
     *
     * The client keeps a copy of what it last set on the device and sends only what differs. A
     * mod that set anything itself -- a render state, a shader, a texture, a vertex declaration,
     * an index buffer, a stream, the render target and with it the viewport -- calls this once it
     * is done, and the client sends all of its own state again before its next draw. The render
     * target itself is not resent: a mod puts back the one it found.
     */
    void(WXL_CALL* invalidate_state)(void);

    // --- version 2 (abi 0.1.33) -------------------------------------------------------------

    /**
     * @brief The API the client's device runs on: a wxl_gx_backend (events.h), NONE before it
     *        has one. Under Vulkan, d3d9_device is null, and what a mod recorded itself through
     *        wxl.vk is followed by invalidate_state, as on Direct3D 9: the renderer binds its
     *        own state again before its next draw.
     */
    uint32_t(WXL_CALL* api)(void);
} wxl_gx_api;

#ifdef __cplusplus
}
#endif

#endif
