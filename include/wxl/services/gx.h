// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The graphics service: the client's Direct3D 9 device, and the models it draws with it.
//
// Not portable. It is published only by a runtime that can see the device and the model renderer
// from the inside -- the recompiled client, running on its D3D9 device -- and a mod that needs it
// requires it in its manifest. Everything here is raw: the device is the client's own, its state
// is the client's own, and what a mod changes on it the client does not know about until the mod
// says so through invalidate_state.
//
// Calls are made on the client's main thread, from inside a frame.

#ifndef WXL_SERVICE_GX_H
#define WXL_SERVICE_GX_H

#include "wxl/abi.h"

#define WXL_GX_SERVICE         "wxl.gx"
#define WXL_GX_SERVICE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One batch of a model the client has just drawn.
 *
 * Handed to a watcher right after the client's DrawIndexedPrimitive returned, with everything it
 * drew with -- vertex declaration, streams, indices, shaders and their constants, textures, render
 * states -- still bound. Issuing the same draw again with these numbers redraws the batch exactly;
 * changing the render target or the pixel shader first draws it somewhere else, or differently.
 */
typedef struct wxl_gx_model_draw
{
    uint32_t struct_size;
    void*    model;           ///< the model the batch belongs to (opaque)
    void*    root_model;      ///< the top of its attachment chain: what object_model answers
    uint32_t primitive_type;  ///< a D3DPRIMITIVETYPE
    int32_t  base_vertex;
    uint32_t min_index;
    uint32_t vertex_count;
    uint32_t start_index;
    uint32_t primitive_count;
    uint32_t blend_mode;      ///< the batch's material blend: 0 opaque, 1 alpha key, 2 and up blended
    uint32_t viewport_x;      ///< the device's viewport for the draw, as D3DVIEWPORT9 holds it:
    uint32_t viewport_y;      ///< setting a render target resets the viewport, and a pure device
    uint32_t viewport_width;  ///< cannot be asked for it back
    uint32_t viewport_height;
    float    viewport_min_z;
    float    viewport_max_z;
} wxl_gx_model_draw;

typedef void(WXL_CALL* wxl_gx_model_draw_fn)(const wxl_gx_model_draw* draw, void* user);

typedef struct wxl_gx_api
{
    uint32_t struct_size;

    /// The client's IDirect3DDevice9, or null when it runs on no D3D9 device or has none yet.
    void*(WXL_CALL* d3d9_device)(void);

    /**
     * @brief The model an object draws with, as root_model names it in a wxl_gx_model_draw:
     *        what its model is attached to, followed up to the top. A mounted player answers
     *        its mount. Null for an object with no model, and for a handle that is not in view.
     */
    void*(WXL_CALL* object_model)(wxl_handle object);

    /**
     * @brief Calls @p fn after every model batch the client draws, until unwatched.
     *
     * Every batch of every model: a watcher returns at once for the ones it does not want. The
     * call runs under the crash guard; a watcher that faults disables its mod.
     */
    wxl_result(WXL_CALL* watch_model_draws)(wxl_gx_model_draw_fn fn, void* user, uint32_t* out_token);
    void(WXL_CALL* unwatch_model_draws)(uint32_t token);

    /**
     * @brief Tells the client that device state changed behind it.
     *
     * The client keeps a copy of what it last set on the device and sends only what differs. A
     * mod that set anything itself -- a render state, a shader, a texture, a vertex declaration,
     * a stream, the render target and with it the viewport -- calls this once it is done, and the
     * client sends all of its own state again before its next draw. The render target itself is
     * not resent: a mod puts back the one it found.
     */
    void(WXL_CALL* invalidate_state)(void);
} wxl_gx_api;

#ifdef __cplusplus
}
#endif

#endif
