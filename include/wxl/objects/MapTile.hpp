// SPDX-License-Identifier: GPL-3.0-or-later
// wxl::MapTile: a handle over one resident map tile, a slot of the client's 64x64 tile grid.

#pragma once

#include <cstdint>

#include "wxl/game/Adt.hpp"
#include "wxl/offsets/CMapArea.hpp"

namespace wxl
{
    /// A resident map tile, one "<Map>_%d_%d.adt": its file state and its two file name numbers.
    /// The terrain itself is reached through MapChunk.
    class MapTile
    {
    public:
        /// Tiles per axis: both file name numbers are 0..kGridDim-1.
        static constexpr uint32_t kGridDim = game::adt::kGridDim;

        MapTile() = default;
        /// Wraps a raw CMapArea pointer (may be null).
        explicit MapTile(void* raw) : m_raw(raw) {}

        /// The tile at a grid slot, the SECOND file name number first: "Azeroth_31_48.adt" is
        /// Slot(48, 31). Null when out of range or not resident.
        static MapTile Slot(uint32_t tileSecond, uint32_t tileFirst)
        {
            return MapTile(game::adt::TileSlot(tileSecond, tileFirst));
        }

        /// True when the handle holds a tile: that slot is resident.
        explicit operator bool() const { return m_raw != nullptr; }
        /// The raw engine pointer, for the bindings that take one.
        void* Raw() const { return m_raw; }

        /// The first %d of the file name; -1 when null.
        int32_t TileFirst() const { return m_raw ? View()->m_index[0] : -1; }
        /// The second %d of the file name, the grid row; -1 when null.
        int32_t TileSecond() const { return m_raw ? View()->m_index[1] : -1; }
        /// The byte size of the raw file buffer; 0 when null or not allocated.
        uint32_t FileSize() const { return m_raw ? View()->m_fileSize : 0u; }
        /// True when the raw file buffer is still allocated.
        bool HasFileData() const { return m_raw && View()->m_fileBuffer != nullptr; }
        /// True while the whole-file async read is in flight.
        bool IsReading() const { return m_raw && View()->m_asyncObject != nullptr; }

        bool operator==(const MapTile& t) const { return m_raw == t.m_raw; }
        bool operator!=(const MapTile& t) const { return m_raw != t.m_raw; }

    protected:
        const wxl::offsets::layout::CMapArea* View() const
        {
            return static_cast<const wxl::offsets::layout::CMapArea*>(m_raw);
        }

        void* m_raw = nullptr;
    };
}
