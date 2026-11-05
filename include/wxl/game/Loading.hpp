// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "wxl/game/Binding.hpp"
#include "wxl/game/World.hpp"
#include "wxl/offsets/CMap.hpp"
#include "wxl/offsets/CMapArea.hpp"
#include "wxl/offsets/World.hpp"
#include "wxl/offsets/free/A.hpp"

/// World tick, terrain tile state, async file reads and load-state globals.
namespace wxl::game::world
{
    namespace off = wxl::offsets;

    /// Terrain tile edge in yards, and the grid origin (32 tiles); tile index = (origin - world) / size.
    inline constexpr float kTileSizeYards   = 1600.0f / 3.0f;
    inline constexpr float kGridOriginYards = 32.0f * kTileSizeYards;

    /// Runs one world tick plus the loading-screen synchronous drain.
    inline void Tick(int param)
    {
        Native<off::CMap::PrepareUpdateFn>(off::CMap::PrepareUpdate)(param);
    }

    // Terrain tile (wxl::offsets::layout::CMapArea) fields; area may be null.
    /// The tile's in-flight async read, null while idle.
    inline void* TileAsyncRead(const void* area)
    {
        return area ? static_cast<const wxl::offsets::layout::CMapArea*>(area)->m_asyncObject : nullptr;
    }

    /// Records the tile's async read; does not start or cancel one.
    inline void SetTileAsyncRead(void* area, void* value)
    {
        if (area)
            static_cast<wxl::offsets::layout::CMapArea*>(area)->m_asyncObject = value;
    }

    /// The tile's open file handle, null when none is open.
    inline void* TileFileHandle(const void* area)
    {
        return area ? static_cast<const wxl::offsets::layout::CMapArea*>(area)->m_file : nullptr;
    }

    inline void SetTileFileHandle(void* area, void* value)
    {
        if (area)
            static_cast<wxl::offsets::layout::CMapArea*>(area)->m_file = value;
    }

    /// The tile's raw ADT file buffer, null before a read allocates it.
    inline void* TileFileBuffer(const void* area)
    {
        return area ? static_cast<const wxl::offsets::layout::CMapArea*>(area)->m_fileBuffer : nullptr;
    }

    inline void SetTileFileBuffer(void* area, void* value)
    {
        if (area)
            static_cast<wxl::offsets::layout::CMapArea*>(area)->m_fileBuffer = value;
    }

    inline uint32_t TileFileSize(const void* area)
    {
        return area ? static_cast<const wxl::offsets::layout::CMapArea*>(area)->m_fileSize : 0;
    }

    inline void SetTileFileSize(void* area, uint32_t value)
    {
        if (area)
            static_cast<wxl::offsets::layout::CMapArea*>(area)->m_fileSize = value;
    }

    /// The loaded map's name, the stem of its ADT files (e.g. "Azeroth"); empty before a map loads.
    inline const char* MapName()
    {
        return reinterpret_cast<const char*>(off::CMap::mapName);
    }

    /// The ADT tile under the focus position, as the two numbers of <Map>_<first>_<second>.adt.
    inline void CurrentTile(int& first, int& second)
    {
        const float px = Read<float>(off::World::s_currentWorldPos);
        const float py = Read<float>(off::World::s_currentWorldPos + sizeof(float));
        first  = static_cast<int>((kGridOriginYards - py) / kTileSizeYards);
        second = static_cast<int>((kGridOriginYards - px) / kTileSizeYards);
    }

    /// Pumps the async queues until no file read is pending.
    inline void AsyncWaitAll()
    {
        Native<off::AsyncFileReadWaitAllFn>(off::AsyncFileReadWaitAll)();
    }

    /// True while an async file read is pending.
    inline bool AsyncPending()
    {
        return Native<off::AsyncFileReadIsReadingFn>(off::AsyncFileReadIsReading)() != 0;
    }

    /// Services the async queues once.
    inline void AsyncServiceQueues()
    {
        Native<off::AsyncFileReadPollHandlerFn>(off::AsyncFileReadPollHandler)(0, 0);
    }

    /// True while the blocking load drain runs.
    inline bool LoadActive()
    {
        return Read<uint32_t>(off::CMap::bPreload) != 0;
    }

    /// The focus position: centre of the load box, usually the player.
    inline void FocusPos(float out[3])
    {
        out[0] = Read<float>(off::World::s_currentWorldPos);
        out[1] = Read<float>(off::World::s_currentWorldPos + sizeof(float));
        out[2] = Read<float>(off::World::s_currentWorldPos + 2 * sizeof(float));
    }
}
