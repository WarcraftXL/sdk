// SPDX-License-Identifier: GPL-3.0-or-later
// The args of the render events: the frame, the device, the world passes and liquids.

#pragma once

#include <cstdint>

namespace wxl::events
{
    /// OnFrame: `device` is the IDirect3DDevice9, null when the device backend creates none.
    struct FrameArgs { void* device; };

    /// OnEndScene: the frame is complete and not yet presented; `device` as in FrameArgs.
    struct EndSceneArgs { void* device; };

    /// OnDeviceLost (release D3DPOOL_DEFAULT resources) and OnDeviceReset (recreate them).
    struct DeviceResetArgs
    {
        void* device;
        void* params; ///< the D3DPRESENT_PARAMETERS of the reset
    };

    /// OnWorldRender: the world pass starts.
    struct WorldRenderArgs { void* device; };

    /// OnWorldRenderEnd: the world is drawn, the interface is not yet.
    struct WorldRenderEndArgs { void* device; };

    /// OnWorldSceneEnd: the camera matrices that drew the world are still on the device.
    struct WorldSceneEndArgs
    {
        void* device;
        void* sceneDepth; ///< the world's depth-stencil surface, may be null
    };

    /// OnLiquidRender, read-only.
    struct LiquidRenderArgs
    {
        void*    bank;
        void*    transform;
        int      passType; ///< 0 main, 1 secondary
        uint32_t instanceCount;
    };
}
