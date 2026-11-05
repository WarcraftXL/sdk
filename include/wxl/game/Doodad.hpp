// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <cstddef>
#include <windows.h>

#include "wxl/game/Adt.hpp"
#include "wxl/game/Binding.hpp"
#include "wxl/offsets/CM2Model.hpp"
#include "wxl/offsets/CM2Shared.hpp"
#include "wxl/offsets/CMapBaseObjLink.hpp"
#include "wxl/offsets/CMapChunk.hpp"
#include "wxl/offsets/CMapDoodadDef.hpp"
#include "wxl/offsets/M2Data.hpp"

/// Placed map doodads (CMapDoodadDef): enumerate those near a position, read and move their transform.
/// Every pointer read is guarded and every walk bounded, so a stale list yields fewer results, never a fault.
namespace wxl::game::doodad
{
    namespace off = wxl::offsets;

    namespace detail
    {
        constexpr uintptr_t kUserLow           = 0x10000;    ///< below: never a heap pointer
        constexpr uintptr_t kUserHigh          = 0x7FFF0000; ///< the end of the 32-bit user range
        constexpr float     kMapExtent         = 64000.0f;   ///< |coordinate| bound of any map
        constexpr ptrdiff_t kMaxPathLength     = 512;
        constexpr uint32_t  kMaxLinkOffset     = 0x400;      ///< a real link offset is small
        constexpr uint32_t  kLinkNextOffset    = 4;          ///< the next pointer after the link offset
        constexpr int       kMaxLinksWalked    = 8192;
        constexpr int       kMaxChunksAround   = 16;
        constexpr float     kMaxCenterDistance = 300.0f;     ///< yards from the position
        constexpr float     kMaxModelExtent    = 100000.0f;  ///< yards per axis
        constexpr float     kMinModelSpan      = 1e-4f;      ///< below: a degenerate point
        constexpr int       kTranslationRow    = 12;         ///< m[12..14] of a row-major matrix

        /// Coarse check that p lies in the 32-bit user address range.
        inline bool Plausible(const void* p)
        {
            const uintptr_t a = reinterpret_cast<uintptr_t>(p);
            return a > kUserLow && a < kUserHigh;
        }

        /// Per-thread cache of the last few VirtualQuery regions; entries live for one GetTickCount64 millisecond.
        struct RegionCache
        {
            struct Entry { uintptr_t base = 0; uintptr_t end = 0; DWORD protect = 0; };
            static constexpr size_t kEntries = 4;
            Entry    e[kEntries];
            size_t   next  = 0;
            uint64_t stamp = 0;
        };
        inline RegionCache& Cache() { thread_local RegionCache c; return c; }

        /// True when [p, p+n) is committed with a protection in allow.
        inline bool Accessible(const void* p, size_t n, DWORD allow)
        {
            if (!Plausible(p)) return false;
            const uintptr_t a = reinterpret_cast<uintptr_t>(p);

            RegionCache& c = Cache();
            const uint64_t now = GetTickCount64();
            if (c.stamp != now) { c = RegionCache{}; c.stamp = now; }
            for (const RegionCache::Entry& hit : c.e)
                if (hit.end && a >= hit.base && a + n <= hit.end)
                    return (hit.protect & allow) != 0;

            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(p, &mbi, sizeof mbi) == 0) return false;
            if (mbi.State != MEM_COMMIT) return false;
            if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
            const uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            c.e[c.next] = { reinterpret_cast<uintptr_t>(mbi.BaseAddress), end, mbi.Protect };
            c.next = (c.next + 1) % RegionCache::kEntries;
            if ((mbi.Protect & allow) == 0) return false;
            return a + n <= end;
        }
        inline bool Readable(const void* p, size_t n)
        {
            return Accessible(p, n, PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
        }
        inline bool Writable(const void* p, size_t n)
        {
            return Accessible(p, n, PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
        }

        inline float* F(void* d, size_t o) { return reinterpret_cast<float*>(reinterpret_cast<char*>(d) + o); }
        /// Pointer slot at d+o, or null when the slot is not readable.
        inline void* P(void* d, size_t o)
        {
            void* slot = reinterpret_cast<char*>(d) + o;
            return Readable(slot, sizeof(void*)) ? At<void*>(d, o) : nullptr;
        }

        /// Finite and within the map bounds (self-compare rejects NaN).
        inline bool SaneCoord(float v) { return v == v && v > -kMapExtent && v < kMapExtent; }
        namespace dd = off::CMapDoodadDef::fields;
        /// True when d's position is readable and in bounds.
        inline bool SaneDoodad(void* d)
        {
            return Readable(d, dd::m_position_z + sizeof(float)) && SaneCoord(*F(d, dd::m_position_x))
                && SaneCoord(*F(d, dd::m_position_y)) && SaneCoord(*F(d, dd::m_position_z));
        }
    }

    inline bool IsValid(void* d) { return detail::SaneDoodad(d); }

    /// The doodad's model instance (CMapDoodadDef::m_model), or null while it loads; it holds the live world matrix.
    inline void* Instance(void* d)
    {
        if (!detail::Readable(d, off::CMapDoodadDef::fields::m_model + sizeof(void*))) return nullptr;
        return At<void*>(d, off::CMapDoodadDef::fields::m_model);
    }

    /// Copies the model's file name without its directory into out; false while the model loads.
    inline bool ModelName(void* d, char* out, size_t cap)
    {
        if (!out || cap == 0) return false;
        out[0] = '\0';
        void* inst = Instance(d);
        if (!inst) return false;
        void* model = detail::P(inst, off::CM2Model::fields::m_shared);
        if (!model) return false;
        const char* path = static_cast<const char*>(model) + off::CM2Shared::fields::m_fileName;
        if (!detail::Readable(path, 1)) return false;
        const char* name = path;
        for (const char* s = path; detail::Readable(s, 1) && *s && (s - path) < detail::kMaxPathLength; ++s)
            if (*s == '\\' || *s == '/') name = s + 1;
        size_t i = 0;
        for (; i + 1 < cap && detail::Readable(name + i, 1) && name[i]; ++i) out[i] = name[i];
        out[i] = '\0';
        return i > 0;
    }

    /// Appends the chunk's placed doodads to out[n..maxCount) and returns the new count.
    inline int EnumerateChunk(void* chunk, void** out, int n, int maxCount)
    {
        if (!detail::Readable(chunk, off::CMapChunk::fields::m_doodadDefLinkList_m_terminator_m_next + sizeof(void*))) return n;
        const uint32_t linkOff = At<uint32_t>(chunk, off::CMapChunk::fields::m_doodadDefLinkList);
        if (linkOff > detail::kMaxLinkOffset) return n;
        uintptr_t node = reinterpret_cast<uintptr_t>(detail::P(chunk, off::CMapChunk::fields::m_doodadDefLinkList_m_terminator_m_next));
        for (int i = 0; (node & 1) == 0 && node != 0 && i < detail::kMaxLinksWalked && n < maxCount; ++i)
        {
            void* d = detail::P(reinterpret_cast<void*>(node), off::CMapBaseObjLink::fields::m_owner);
            if (detail::SaneDoodad(d)) out[n++] = d;
            node = reinterpret_cast<uintptr_t>(detail::P(reinterpret_cast<void*>(node), linkOff + detail::kLinkNextOffset));
        }
        return n;
    }

    /// Collects the doodads of the chunk under pos; returns the count.
    inline int EnumerateAt(float pos[3], void** out, int maxCount)
    {
        return EnumerateChunk(adt::GetChunk(pos), out, 0, maxCount);
    }

    /// Collects the doodads of the 3x3 chunks around center, step apart (one chunk is 33.33 yd); returns the count.
    inline int EnumerateAround(float center[3], float step, void** out, int maxCount)
    {
        void* seen[detail::kMaxChunksAround]; int seenN = 0, n = 0;
        for (int gy = -1; gy <= 1; ++gy)
            for (int gx = -1; gx <= 1; ++gx)
            {
                float q[3] = { center[0] + gx * step, center[1] + gy * step, center[2] };
                void* chunk = adt::GetChunk(q);
                if (!detail::Plausible(chunk)) continue;
                bool dup = false;
                for (int k = 0; k < seenN; ++k) if (seen[k] == chunk) dup = true;
                if (dup) continue;
                if (seenN < detail::kMaxChunksAround) seen[seenN++] = chunk;
                n = EnumerateChunk(chunk, out, n, maxCount);
                if (n >= maxCount) return n;
            }
        return n;
    }

    /// World position, or zeros when unreadable.
    inline void Position(void* d, float out[3])
    {
        if (!detail::Readable(d, off::CMapDoodadDef::fields::m_scale + sizeof(float))) { out[0] = out[1] = out[2] = 0.0f; return; }
        const float* p = detail::F(d, off::CMapDoodadDef::fields::m_position_x);
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
    }

    /// Scale, or 1.0 when unreadable.
    inline float Scale(void* d)
    {
        return detail::Readable(d, off::CMapDoodadDef::fields::m_scale + sizeof(float)) ? *detail::F(d, off::CMapDoodadDef::fields::m_scale) : 1.0f;
    }

    /// Bounding-sphere centre (m_sphere); the position when unreadable or over kMaxCenterDistance from it.
    inline void Center(void* d, float out[3])
    {
        float pos[3];
        Position(d, pos);
        if (detail::Readable(d, off::CMapDoodadDef::fields::m_sphere_c_z + sizeof(float)))
        {
            const float* c = detail::F(d, off::CMapDoodadDef::fields::m_sphere_c_x);
            const float c0 = c[0];
            const float c1 = c[1];
            const float c2 = c[2];
            const float dx = c0 - pos[0], dy = c1 - pos[1], dz = c2 - pos[2];
            if (detail::SaneCoord(c0) && detail::SaneCoord(c1) && detail::SaneCoord(c2) &&
                dx * dx + dy * dy + dz * dz < detail::kMaxCenterDistance * detail::kMaxCenterDistance)
            {
                out[0] = c0; out[1] = c1; out[2] = c2; return;
            }
        }
        out[0] = pos[0]; out[1] = pos[1]; out[2] = pos[2];
    }

    /// World-space bounding box (m_bbox); false when unreadable.
    inline bool BBox(void* d, float mn[3], float mx[3])
    {
        if (!detail::Readable(d, off::CMapDoodadDef::fields::m_bbox_t_z + sizeof(float))) return false;
        const float* b = detail::F(d, off::CMapDoodadDef::fields::m_bbox_b_x);
        const float* t = detail::F(d, off::CMapDoodadDef::fields::m_bbox_t_x);
        mn[0] = b[0]; mn[1] = b[1]; mn[2] = b[2];
        mx[0] = t[0]; mx[1] = t[1]; mx[2] = t[2];
        return true;
    }

    /// Model-space bounding box from the model header (M2Data m_bounds); transform by WorldMatrix for world space.
    /// False while the model loads or when the box is NaN, inverted, degenerate or absurdly large.
    inline bool LocalBounds(void* d, float lo[3], float hi[3])
    {
        void* inst = Instance(d);
        if (!inst) return false;
        void* model = detail::P(inst, off::CM2Model::fields::m_shared);
        if (!model) return false;
        void* hdr = detail::P(model, off::CM2Shared::fields::m_data);
        if (!hdr) return false;
        namespace m2 = off::M2Data::fields;
        if (!detail::Readable(hdr, m2::m_bounds_m_extent_t_z + sizeof(float))) return false;

        const float* b = detail::F(hdr, m2::m_bounds_m_extent_b_x);
        const float* t = detail::F(hdr, m2::m_bounds_m_extent_t_x);
        lo[0] = b[0]; lo[1] = b[1]; lo[2] = b[2];
        hi[0] = t[0]; hi[1] = t[1]; hi[2] = t[2];

        float span = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            if (lo[i] != lo[i] || hi[i] != hi[i]) return false; // NaN
            if (hi[i] < lo[i]) return false;
            const float e = hi[i] - lo[i];
            if (e > detail::kMaxModelExtent) return false;
            span += e;
        }
        return span > detail::kMinModelSpan;
    }

    /// The instance's live world matrix (CM2Model::m_worldTransform): 16 floats, row-major, translation in m[12..14].
    inline bool WorldMatrix(void* d, float m[16])
    {
        void* inst = Instance(d);
        if (!inst) return false;
        const float* w = detail::F(inst, off::CM2Model::fields::m_worldTransform);
        if (!detail::Readable(w, kMatrixFloats * sizeof(float))) return false;
        for (int i = 0; i < kMatrixFloats; ++i) m[i] = w[i];
        return true;
    }

    /// Writes the instance's world matrix, which moves the model, and mirrors it into the doodad's m_mat and position.
    inline void SetWorldMatrix(void* d, const float m[16])
    {
        if (void* inst = Instance(d))
        {
            float* w = detail::F(inst, off::CM2Model::fields::m_worldTransform);
            if (detail::Writable(w, kMatrixFloats * sizeof(float)))
                for (int i = 0; i < kMatrixFloats; ++i) w[i] = m[i];
        }

        float* mat = detail::F(d, off::CMapDoodadDef::fields::m_mat);
        float* pos = detail::F(d, off::CMapDoodadDef::fields::m_position_x);
        if (detail::Writable(mat, kMatrixFloats * sizeof(float)))
            for (int i = 0; i < kMatrixFloats; ++i) mat[i] = m[i];
        if (detail::Writable(pos, 3 * sizeof(float)))
        {
            const float* t = m + detail::kTranslationRow;
            pos[0] = t[0]; pos[1] = t[1]; pos[2] = t[2];
        }
    }

    /// Translates the doodad: rewrites the translation of the instance matrix, m_mat and the position.
    inline void SetPosition(void* d, const float p[3])
    {
        if (void* inst = Instance(d))
        {
            float* w = detail::F(inst, off::CM2Model::fields::m_worldTransform);
            if (detail::Writable(w + detail::kTranslationRow, 3 * sizeof(float)))
            {
                float* t = w + detail::kTranslationRow;
                t[0] = p[0]; t[1] = p[1]; t[2] = p[2];
            }
        }
        float* pos = detail::F(d, off::CMapDoodadDef::fields::m_position_x);
        float* mat = detail::F(d, off::CMapDoodadDef::fields::m_mat);
        if (detail::Writable(pos, 3 * sizeof(float)))
        {
            pos[0] = p[0]; pos[1] = p[1]; pos[2] = p[2];
        }
        if (detail::Writable(mat + detail::kTranslationRow, 3 * sizeof(float)))
        {
            float* t = mat + detail::kTranslationRow;
            t[0] = p[0]; t[1] = p[1]; t[2] = p[2];
        }
    }
}
