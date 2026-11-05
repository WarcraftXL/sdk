// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <cstddef>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/CGxAppRenderState.hpp"
#include "wxl/offsets/CGxDevice.hpp"
#include "wxl/offsets/CGxDeviceD3d.hpp"
#include "wxl/offsets/CGxDeviceD3d9Ex.hpp"
#include "wxl/offsets/CGxMatrixStack.hpp"
#include "wxl/offsets/CGxVertexAttrib.hpp"
#include "wxl/offsets/free/W.hpp"

/// Fields and shared engine functions of the CGxDevice a WXL_GraphicsDeviceApi backend implements.
/// Valid only after WXL_GraphicsDeviceApi::InitBaseDevice has run the base constructor.
namespace wxl::game::gxdevice
{
    namespace off = wxl::offsets;

    constexpr uint32_t kIdentityFlag = 1u; ///< in a matrix stack's per-level flags

    /// The field of type T at offset in an engine record.
    template <class T>
    inline T& Field(void* object, size_t offset)
    {
        return wxl::game::At<T>(object, offset);
    }

    template <class T>
    inline const T& Field(const void* object, size_t offset)
    {
        return wxl::game::At<T>(object, offset);
    }

    /// Non-zero while the device is usable.
    inline bool HasContext(const void* device) { return Field<uint32_t>(device, off::CGxDevice::fields::m_context) != 0; }

    /// The 16-byte value of state `id`, or null before the base constructor ran.
    inline const void* RenderStateValue(const void* device, unsigned id)
    {
        const auto* table = Field<const uint8_t*>(device, off::CGxDevice::fields::m_appRenderStates_m_data);
        if (!table || id >= Field<uint32_t>(device, off::CGxDevice::fields::m_appRenderStates_m_count)) return nullptr;
        return table + id * off::CGxAppRenderState::fields::kSize + off::CGxAppRenderState::fields::m_value;
    }

    /// State `id` read as T (int32_t, float or a pointer); T{} when the table is absent.
    template <class T>
    inline T RenderState(const void* device, unsigned id)
    {
        const void* value = RenderStateValue(device, id);
        return value ? *static_cast<const T*>(value) : T{};
    }

    /// Master-enable bitmask: bit N off forces state N's category to its disabled value.
    inline uint32_t MasterEnables(const void* device)
    {
        return Field<uint32_t>(device, off::CGxDevice::fields::m_appMasterEnables);
    }

    inline bool MasterEnabled(const void* device, unsigned bit)
    {
        return (MasterEnables(device) & (1u << bit)) != 0;
    }

    /// The top matrix of transform stack `xform`, 16 row-major floats.
    inline const float* XformTop(const void* device, unsigned xform)
    {
        const auto* stack = static_cast<const uint8_t*>(device) + off::CGxDevice::fields::m_xforms
                          + xform * off::CGxMatrixStack::fields::kSize;
        const uint32_t top = *reinterpret_cast<const uint32_t*>(stack + off::CGxMatrixStack::fields::m_level);
        return reinterpret_cast<const float*>(stack + off::CGxMatrixStack::fields::m_mtx + top * kMatrixFloats * sizeof(float));
    }

    /// Whether that top matrix is flagged identity.
    inline bool XformTopIsIdentity(const void* device, unsigned xform)
    {
        const auto* stack = static_cast<const uint8_t*>(device) + off::CGxDevice::fields::m_xforms
                          + xform * off::CGxMatrixStack::fields::kSize;
        const uint32_t top = *reinterpret_cast<const uint32_t*>(stack + off::CGxMatrixStack::fields::m_level);
        return (*reinterpret_cast<const uint32_t*>(stack + off::CGxMatrixStack::fields::m_flags + top * sizeof(uint32_t)) & kIdentityFlag) != 0;
    }

    /// The projection in the API's own depth range.
    inline const float* ApiProjection(const void* device)
    {
        return &Field<const float>(device, off::CGxDevice::fields::m_projNative);
    }

    /// The descriptor of attribute slot `attrib`: {attribute, data format, offset, stride}.
    inline const uint32_t* AttribSlot(const void* device, unsigned attrib)
    {
        return &Field<const uint32_t>(device, off::CGxDevice::fields::m_primVertexFormatAttrib + attrib * off::CGxVertexAttrib::fields::kSize);
    }

    /// The CGxBuf bound to attribute `attrib`, or null.
    inline void* AttribBuf(const void* device, unsigned attrib)
    {
        return Field<void*>(const_cast<void*>(device), off::CGxDevice::fields::m_primVertexFormatBuf + attrib * sizeof(void*));
    }

    /// One bit per attribute slot the current vertex layout uses.
    inline uint32_t AttribMask(const void* device) { return Field<uint32_t>(device, off::CGxDevice::fields::m_primVertexMask); }

    /// The engine window procedure; it chains to the client's own.
    inline void* WindowProc() { return reinterpret_cast<void*>(off::CGxDeviceD3d9Ex::WindowProcD3d); }

    /// Registers the engine window class; returns its atom, 0 on failure.
    inline uint16_t RegisterWindowClass()
    {
        return Native<off::WindowClassCreate_68EB20Fn>(off::WindowClassCreate_68EB20)();
    }

    /// Creates the game window, the HWND going to the device; `format` (a CGxFormat) is adjusted in place.
    inline bool CreateGameWindow(void* device, void* format)
    {
        return Native<off::CGxDeviceD3d::ICreateWindowFn>(off::CGxDeviceD3d::ICreateWindow)(device, nullptr, format);
    }

    /// Sets defWindow and curWindow from a CRect {minY, minX, maxY, maxX}.
    inline void SetDefWindow(void* device, const float rect[4])
    {
        Native<off::CGxDevice::DeviceSetDefWindowFn>(off::CGxDevice::DeviceSetDefWindow)(device, nullptr, rect);
    }

    /// Sleeps to honour the frame-rate caps; call right before presenting.
    inline void WaitForFpsCap(void* device)
    {
        Native<off::CGxDevice::WaitForFPSCapFn>(off::CGxDevice::WaitForFPSCap)(device, nullptr);
    }

    /// Runs CGxDevice::~CGxDevice; the memory stays the caller's.
    inline void DestroyBase(void* device)
    {
        Native<off::CGxDevice::dtorFn>(off::CGxDevice::dtor)(device, nullptr);
    }
}
