// SPDX-License-Identifier: GPL-3.0-or-later
// wxl::MapChunk: a handle over one runtime terrain chunk (a parsed MCNK) and its placed doodads.

#pragma once

#include "wxl/game/Adt.hpp"
#include "wxl/game/Doodad.hpp"
#include "wxl/objects/Doodad.hpp"
#include "wxl/objects/Object.hpp"

namespace wxl
{
    /// A runtime terrain chunk: the heightmap and collision under a world position, and its doodads.
    class MapChunk
    {
    public:
        /// The most doodads one ForEachDoodad visits (each call starts at the list head).
        static constexpr int kDoodadBatch = 256;

        MapChunk() = default;
        /// Wraps a raw CMapChunk pointer (may be null).
        explicit MapChunk(void* raw) : m_raw(raw) {}

        /// The chunk under a world position; null when it is not parsed yet.
        static MapChunk At(const Vec3& pos)
        {
            float p[3] = { pos.x, pos.y, pos.z };
            return MapChunk(game::adt::GetChunk(p));
        }

        /// The chunk under a world position given as x, y, z.
        static MapChunk At(const float pos[3])
        {
            float p[3] = { pos[0], pos[1], pos[2] };
            return MapChunk(game::adt::GetChunk(p));
        }

        /// True when the handle holds a chunk: its heightmap and collision are resident.
        explicit operator bool() const { return m_raw != nullptr; }
        /// The raw engine pointer, for the bindings that take one.
        void* Raw() const { return m_raw; }

        /// Writes up to `maxCount` placed doodad pointers (never creatures) into `out`; answers how many.
        int Doodads(void** out, int maxCount) const
        {
            return game::doodad::EnumerateChunk(m_raw, out, 0, maxCount);
        }

        /// Calls fn(Doodad) per placed doodad, at most kDoodadBatch; answers how many.
        ///     chunk.ForEachDoodad([](Doodad d) { Log::Info("{}", d.ModelName().Text()); });
        template <class Fn>
        int ForEachDoodad(Fn&& fn) const
        {
            void* batch[kDoodadBatch];
            const int n = game::doodad::EnumerateChunk(m_raw, batch, 0, kDoodadBatch);
            for (int i = 0; i < n; ++i) fn(Doodad(batch[i]));
            return n;
        }

        /// Placed objects over the chunk still loading (0 when null); the progress against `total` goes
        /// to `progressOut`.
        int NearObjectCount(int* progressOut, int total) const
        {
            return m_raw ? game::adt::NearObjectCount(m_raw, progressOut, total) : 0;
        }

        /// Same count, without the progress output.
        int NearObjectCount(int total) const
        {
            int progress = 0;
            return NearObjectCount(&progress, total);
        }

        bool operator==(const MapChunk& c) const { return m_raw == c.m_raw; }
        bool operator!=(const MapChunk& c) const { return m_raw != c.m_raw; }

    protected:
        void* m_raw = nullptr;
    };
}
