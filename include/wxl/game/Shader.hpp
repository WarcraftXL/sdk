// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/CMapObj.hpp"
#include "wxl/offsets/CShaderEffect.hpp"
#include "wxl/offsets/CWorldScene.hpp"

/// Raw reads of the engine's shader-permutation selection state.
namespace wxl::game::shader
{
    namespace off = wxl::offsets;

    /// Slots in the exterior effect table; the last is the two-layer composite effect.
    constexpr unsigned kExteriorEffectCount = 7;
    /// The alternate table has the same slots at the same positions.
    constexpr unsigned kAltEffectCount = kExteriorEffectCount;

    /// The effect collection the last activate selected, or null before the first one of the frame.
    inline const void* ActiveCollection()
    { return Read<const void*>(off::CShaderEffect::s_curEffect); }

    /// Shadow tier, 0..2.
    inline int ShadowTier() { return Read<int>(off::CShaderEffect::s_shadowValue); }

    /// Pixel-side shadow group index.
    inline int ShadowGroup() { return Read<int>(off::CShaderEffect::s_usePcfFiltering); }

    /// Light/fog bit, 0 or 1.
    inline int LightBit() { return Read<int>(off::CMapObj::s_lightingMode); }

    /// Permutation sub-index, 0..14.
    inline int SubIndex() { return Read<int>(off::CWorldScene::s_fogPermute); }

    /// True when effects are bound from shader collections rather than fixed function.
    inline bool ProgrammablePathActive()
    { return Read<uint32_t>(off::CShaderEffect::s_enableShaders) != 0; }

    /// Slot `index` of the exterior effect table; null when out of range or not registered yet.
    inline const void* ExteriorEffect(unsigned index)
    {
        if (index >= kExteriorEffectCount) return nullptr;
        return reinterpret_cast<const void* const*>(off::CMapObj::s_classicShaders)[index];
    }

    /// Slot `index` of the alternate table (map objects with the MOHD unified-render-path flag); null when out of range or
    /// not registered yet.
    inline const void* AltEffect(unsigned index)
    {
        if (index >= kAltEffectCount) return nullptr;
        return reinterpret_cast<const void* const*>(off::CMapObj::s_unifiedShaders)[index];
    }
}
