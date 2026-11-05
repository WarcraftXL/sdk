// SPDX-License-Identifier: GPL-3.0-or-later
// The wxl.graphics.device service: an extension supplies the engine's CGxDevice backend.

#ifndef WXL_GRAPHICS_DEVICE_API_H
#define WXL_GRAPHICS_DEVICE_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WXL_GRAPHICS_DEVICE_API_NAME "wxl.graphics.device"
#define WXL_GRAPHICS_DEVICE_API_VERSION 2

/// Replaces CGxDevice::New* for `api` (0 OpenGL, 1 D3D9, 2 D3D9Ex) and only constructs the device; the core
/// then calls its DeviceCreate (slot 10), deleting it (slot 8) on failure. NULL: the stock device.
typedef void* (__cdecl* WXL_DeviceCreateFn)(int api, void* wndProc, void* context);

typedef struct WXL_GraphicsDeviceApi
{
    uint32_t structSize;
    uint32_t apiVersion;

    /// Registers the device factory. One at a time: a second registration replaces the first.
    int (__cdecl* RegisterFactory)(WXL_DeviceCreateFn factory, const char* name);

    /// Unregisters `factory` when it is the registered one: the engine goes back to its own.
    void (__cdecl* UnregisterFactory)(WXL_DeviceCreateFn factory);

    /// Runs the client's CGxDevice constructor on `dev`, then its default pools and stream buffers.
    void (__cdecl* InitBaseDevice)(void* dev);

    /// The engine's base CGxDevice vtable, which a backend copies and overrides slot by slot.
    const void* const* (__cdecl* GetBaseVTable)(void);
} WXL_GraphicsDeviceApi;

#ifdef __cplusplus
}
#endif

#endif // WXL_GRAPHICS_DEVICE_API_H
