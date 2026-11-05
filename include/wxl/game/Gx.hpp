// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/CGxDevice.hpp"
#include "wxl/offsets/CGxDeviceD3d.hpp"
#include "wxl/offsets/free/G.hpp"

/// The engine device, its IDirect3DDevice9 and the scene-clear seam.
namespace wxl::game::gx
{
    namespace off = wxl::offsets;

    /// The live IDirect3DDevice9, or null until graphics is up.
    inline void* RawDevice()
    {
        void* g = Read<void*>(off::g_theGxDevicePtr);
        if (!g) return nullptr;
        return At<void*>(g, off::CGxDeviceD3d::fields::m_d3dDevice);
    }

    /// The engine's CGxDevice, which owns the D3D9 device, or null until graphics is up.
    inline void* RawGraphicsDevice()
    { return Read<void*>(off::g_theGxDevicePtr); }

    /// True when the engine device is up with no IDirect3DDevice9 behind it (a non-D3D9 backend).
    inline bool DeviceUpWithoutD3d9()
    {
        void* g = RawGraphicsDevice();
        return g && !RawDevice() && At<uint32_t>(g, off::CGxDevice::fields::m_context) != 0;
    }

    /// Entry of every scene clear; a detour can drop flag bits to narrow a clear.
    constexpr uintptr_t kSceneClearSeam = off::GxSceneClear;

    using SceneClearFn = off::GxSceneClearFn;
    /// Flag bits of a scene clear's first argument.
    namespace clear
    {
        constexpr uint32_t kColor = 0x1;
        constexpr uint32_t kDepth = 0x2;
    }
}
