// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/CGWorldFrame.hpp"
#include "wxl/offsets/CSimpleTop.hpp"
#include "wxl/offsets/World.hpp"
#include "wxl/offsets/free/S.hpp"

/// World picks: what lies under a screen point or the cursor, or between two world points.
/// The screen picks use the client's own cursor hit test; in-world only, a miss otherwise.
namespace wxl::game::world
{
    namespace off = wxl::offsets;

    struct Vec3 { float x; float y; float z; };

    constexpr int      kHitMiss         = 0;
    constexpr int      kHitTerrainOrWmo = 3;
    /// What the cursor pick traces against.
    constexpr uint32_t kCursorPickMask  = 0x1000124u;

    namespace detail
    {
        // HitTestPoint's result: {objLo, objHi, posX, posY, posZ, t}, then the near/far ray.
        constexpr int kPickResultInts = 12;
        constexpr int kPickObjLo      = 0;
        constexpr int kPickObjHi      = 1;
        constexpr int kPickPos        = 2;
        constexpr int kPickT          = 5;
        /// Floats the intersect may write for a hit point (more than three).
        constexpr int kIntersectScratchFloats = 16;
    }

    /// type: 0 miss, 2 M2/doodad, 3 terrain or WMO. objLo/objHi: object handle, zero for terrain.
    /// t: distance along the ray.
    struct WorldHit { int type; Vec3 pos; void* objLo; void* objHi; float t; };

    /// Picks along the ray through a screen point in device (DDC) pixels; returns the hit type.
    inline int Pick(float ddcX, float ddcY, WorldHit& out)
    {
        out = WorldHit{};
        void* wf = Read<void*>(off::CGWorldFrame::s_currentWorldFrame);
        if (!wf) return kHitMiss;

        int result[detail::kPickResultInts] = { 0 };
        const int type = Native<off::CGWorldFrame::HitTestPointFn>(off::CGWorldFrame::HitTestPoint)(wf, nullptr, ddcX, ddcY, 0, result);
        if (type == kHitMiss) return kHitMiss;

        out.type  = type;
        out.objLo = reinterpret_cast<void*>(result[detail::kPickObjLo]);
        out.objHi = reinterpret_cast<void*>(result[detail::kPickObjHi]);
        out.pos   = *reinterpret_cast<Vec3*>(&result[detail::kPickPos]);
        out.t     = *reinterpret_cast<float*>(&result[detail::kPickT]);
        return type;
    }

    /// Traces from one world point to another against terrain and WMOs (not M2s): kHitTerrainOrWmo with
    /// t the fraction of the segment, or kHitMiss.
    inline int TraceLine(const float from[3], const float to[3], WorldHit& out,
                         uint32_t mask = kCursorPickMask)
    {
        out = WorldHit{};

        float scratch[detail::kIntersectScratchFloats] = { 0.0f };
        float fraction = 1.0f; // the whole segment

        if (!Native<off::World::IntersectFn>(off::World::Intersect)(from, to, scratch, &fraction, mask, 0))
            return kHitMiss;

        out.type = kHitTerrainOrWmo;
        out.pos  = Vec3{ from[0] + (to[0] - from[0]) * fraction,
                         from[1] + (to[1] - from[1]) * fraction,
                         from[2] + (to[2] - from[2]) * fraction };
        out.t    = fraction;
        return out.type;
    }

    /// The cursor position in device (DDC) pixels; false when no world frame is up.
    inline bool CursorDdc(float& ddcX, float& ddcY)
    {
        void* wf = Read<void*>(off::CGWorldFrame::s_currentWorldFrame);
        if (!wf) return false;
        void* input = At<void*>(wf, off::CGWorldFrame::fields::m_simpleTop);
        if (!input) return false;

        // Normalized cursor to device pixels, as the client does before its hit test.
        const float ndcX = At<float>(input, off::CSimpleTop::fields::m_mousePosition_m_x);
        const float ndcY = At<float>(input, off::CSimpleTop::fields::m_mousePosition_m_y);
        const float ddcWidth = Read<float>(off::s_ddcWidthScale);
        const float ddcHeight = Read<float>(off::s_ddcHeightScale);
        if (ddcWidth <= 0.0f || ddcHeight <= 0.0f) return false;

        ddcX = ndcX * ddcWidth;
        ddcY = ndcY * ddcHeight;
        return true;
    }

    /// Picks under the cursor; returns the hit type.
    inline int PickCursor(WorldHit& out)
    {
        float x, y;
        if (!CursorDdc(x, y)) { out = WorldHit{}; return kHitMiss; }
        return Pick(x, y, out);
    }
}
