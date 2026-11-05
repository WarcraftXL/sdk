// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/CMap.hpp"

/// Typed wrappers over the client's terrain (ADT) functions.
namespace wxl::game::adt
{
    namespace off = wxl::offsets;

    /// Tiles per axis of a map: CMap::m_areaTable is kGridDim x kGridDim tile pointers.
    inline constexpr uint32_t kGridDim = 64;

    /// The map chunk at a world position, or null when it is not parsed yet.
    inline void* GetChunk(float* pos)
    {
        return Native<off::CMap::GetChunkAtPositionFn>(off::CMap::GetChunkAtPosition)(pos);
    }

    /// Counts placed objects overlapping the chunk that are still loading; progressOut receives how many of
    /// total have loaded.
    inline int NearObjectCount(void* chunk, int* progressOut, int total)
    {
        return Native<off::CMap::ComputeMapObjLoadProgressFn>(off::CMap::ComputeMapObjLoadProgress)(chunk, progressOut, total);
    }

    /// The tile (wxl::offsets::layout::CMapArea) for <Map>_<tileFirst>_<tileSecond>.adt, or null when out of
    /// range or not resident.
    inline void* TileSlot(uint32_t tileSecond, uint32_t tileFirst)
    {
        if (tileSecond >= kGridDim || tileFirst >= kGridDim)
            return nullptr;
        return Read<void*>(off::CMap::m_areaTable + (tileSecond * kGridDim + tileFirst) * sizeof(void*));
    }
}
