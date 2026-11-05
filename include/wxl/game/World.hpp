// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <cstddef>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/CGGameUI.hpp"
#include "wxl/offsets/CGObjectData.hpp"
#include "wxl/offsets/CGObject_C.hpp"
#include "wxl/offsets/CGUnit_C.hpp"
#include "wxl/offsets/CGWorldFrame.hpp"
#include "wxl/offsets/CMap.hpp"
#include "wxl/offsets/CWorld.hpp"
#include "wxl/offsets/World.hpp"
#include "wxl/offsets/free/C.hpp"
#include "wxl/offsets/free/S.hpp"

/// Selection GUIDs, GUID-to-object resolution, map loading and the world scene render.
namespace wxl::game::world
{
    namespace off = wxl::offsets;

    // CGObject_C virtual slots: name anchor, world position, facing.
    inline constexpr unsigned kSlotNamePosition = 8, kSlotPosition = 11, kSlotFacing = 13;
    using PositionFn = void(__thiscall*)(void* self, float out[3]);
    using FacingFn   = float(__thiscall*)(void* self);

    /// Renders a world frame from its active camera: scene update (frustum, viewer, day/night), render, then
    /// restores projection and view. Unlike RenderScene, it needs no setup from the caller.
    inline void RenderWorldFrame(void* frame)
    { Native<off::CGWorldFrame::RenderWorldFn>(off::CGWorldFrame::RenderWorld)(frame); }

    /// The loaded map's id, or -1 when none is loaded.
    inline int CurrentMapId()
    { return Read<int>(off::s_mapId); }

    /// Sets the world position terrain streams around; set it before EnterMap.
    inline void SetStreamFocus(const float pos[3])
    {
        Write<float>(off::World::s_currentWorldPos, pos[0]);
        Write<float>(off::World::s_currentWorldPos + sizeof(float), pos[1]);
        Write<float>(off::World::s_currentWorldPos + 2 * sizeof(float), pos[2]);
    }

    /// Loads a map (mapDir under World\\Maps, e.g. "Azeroth"), replacing the current one, and streams the tiles
    /// around the focus before returning. Needs no player or server session.
    inline void EnterMap(const char* mapDir, int mapId)
    { Native<off::CMap::LoadFn>(off::CMap::Load)(mapDir, mapId); }

    /// Sets the draw distance in yards, clamped to what the loaded map allows. Call after EnterMap; unset, nothing
    /// is drawn. A projection built alongside should use the same distance.
    inline void SetFarClip(float yards)
    { Native<off::World::SetFarClipFn>(off::World::SetFarClip)(yards); }

    /// Near plane the engine pairs with the far clip.
    constexpr float kNearClip = 0.2f;

    /// Per-category render switches; bit 1 gates the solid terrain draw.
    inline uint32_t Enables()
    { return Read<uint32_t>(off::CWorld::enables); }

    /// Builds the frustum and viewer globals RenderScene needs, and advances day/night. target is any point along
    /// the line of sight; focus is where terrain streams, usually eye.
    inline void PrepareScene(const float eye[3], const float target[3], const float focus[3])
    { Native<off::CWorld::UpdateFn>(off::CWorld::Update)(eye, target, focus); }

    /// Draws terrain, map objects and doodads around viewerPos; flags 0 is neutral. Needs PrepareScene and the
    /// camera matrices (wxl::game::camera) set first.
    inline void RenderScene(const float viewerPos[3], uint32_t flags)
    { Native<off::CWorld::RenderFn>(off::CWorld::Render)(viewerPos, flags); }

    /// GUID of the unit under the cursor.
    inline unsigned long long MouseoverGuid()
    { return Read<unsigned long long>(off::CGGameUI::m_currentObjectTrack); }

    inline unsigned long long TargetGuid()
    { return Read<unsigned long long>(off::CGGameUI::m_lockedTarget); }

    inline unsigned long long ActivePlayerGuid()
    { return Native<off::ClntObjMgrGetActivePlayerFn>(off::ClntObjMgrGetActivePlayer)(); }

    /// The object for guid, or null when absent or outside typeMask (kTypeMask* values).
    inline void* ResolveObject(unsigned long long guid, unsigned typeMask)
    { return Native<off::ClntObjMgrObjectPtrFn>(off::ClntObjMgrObjectPtr)(guid, typeMask, "wxl", 0); }

    inline void UnitPosition(void* unit, float out[3])
    {
        const float* p = &At<const float>(unit, off::CGUnit_C::fields::m_movementData_m_position);
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
    }

    constexpr unsigned kTypeMaskObject        = 0x01;
    constexpr unsigned kTypeMaskItem          = 0x02;
    constexpr unsigned kTypeMaskContainer     = 0x04;
    constexpr unsigned kTypeMaskUnit          = 0x08;
    constexpr unsigned kTypeMaskPlayer        = 0x10;
    constexpr unsigned kTypeMaskGameObject    = 0x20;
    constexpr unsigned kTypeMaskDynamicObject = 0x40;
    constexpr unsigned kTypeMaskCorpse        = 0x80;

    /// The object's GUID, or 0 when obj is null.
    inline unsigned long long Guid(void* obj)
    { return obj ? At<uint64_t>(At<void*>(obj, off::CGObject_C::fields::m_obj), off::CGObjectData::fields::m_guid) : 0ull; }

    /// The object's kTypeMask* bits, or 0 when obj is null.
    inline unsigned TypeMask(void* obj)
    { return obj ? At<uint32_t>(At<void*>(obj, off::CGObject_C::fields::m_obj), off::CGObjectData::fields::m_type) : 0u; }

    /// Any object's world position; the origin when obj is null or its type does not implement it.
    inline void Position(void* obj, float out[3])
    {
        out[0] = out[1] = out[2] = 0.0f;
        if (obj) Virtual<PositionFn>(obj, kSlotPosition)(obj, out);
    }

    /// Facing in radians counter-clockwise from +X (heading = (cos, sin)); 0 when obj is null.
    inline float Facing(void* obj)
    {
        return obj ? Virtual<FacingFn>(obj, kSlotFacing)(obj) : 0.0f;
    }

    /// The point above the object where its name is drawn; the origin when obj is null.
    inline void NamePosition(void* obj, float out[3])
    {
        out[0] = out[1] = out[2] = 0.0f;
        if (obj) Virtual<PositionFn>(obj, kSlotNamePosition)(obj, out);
    }

    /// Entry that walks the resident objects; it faults without a world session (the manager is read unchecked).
    constexpr uintptr_t kEnumObjectsSeam = off::ClntObjMgrEnumVisibleObjects;

    using EnumObjectsFn = off::ClntObjMgrEnumVisibleObjectsFn;
    /// Per-object callback; returning zero stops the walk.
    using EnumStepFn = int(__cdecl*)(uint32_t guidLow, uint32_t guidHigh, void* user);

    /// Calls fn(guid) per resident object; returning false stops. Main thread only.
    template <class Fn>
    void ForEachObject(Fn fn)
    {
        struct Step
        {
            static int __cdecl Visit(uint32_t low, uint32_t high, void* user)
            { return (*static_cast<Fn*>(user))((static_cast<unsigned long long>(high) << 32) | low) ? 1 : 0; }
        };
        Native<off::ClntObjMgrEnumVisibleObjectsFn>(off::ClntObjMgrEnumVisibleObjects)(&Step::Visit, &fn);
    }

    /// Calls fn(guid, object) per resident object in typeMask; returning false stops. Main thread only.
    template <class Fn>
    void ForEachObject(unsigned typeMask, Fn fn)
    {
        ForEachObject([&](unsigned long long guid) {
            void* obj = ResolveObject(guid, typeMask);
            return obj ? fn(guid, obj) : true;
        });
    }
}
