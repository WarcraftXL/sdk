// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/CMapObj.hpp"
#include "wxl/offsets/CMapObjDef.hpp"
#include "wxl/offsets/CMapObjGroup.hpp"
#include "wxl/offsets/CWorldScene.hpp"

/// Typed wrappers over the client's WMO (map object) functions.
namespace wxl::game::wmo
{
    namespace off = wxl::offsets;

    /// The outdoor-render gate's offset in CWorldScene::frustumPortalView.
    inline constexpr uintptr_t kOutdoorGateOffset = 0x10;

    /// One WMO root (CMapObj): its file buffer and group array. Every getter returns 0 or null on a null root.
    class WmoRoot
    {
    public:
        /// Wraps a raw CMapObj pointer (may be null).
        explicit WmoRoot(void* root) : m_root(root) {}

        void* raw() const { return m_root; }

        explicit operator bool() const { return m_root != nullptr; }

        void* GetBuffer() const
        {
            return m_root ? At<void*>(m_root, off::CMapObj::fields::m_pWmoData) : nullptr;
        }

        /// Buffer size in bytes: the bound of the native chunk walk.
        uint32_t GetSize() const
        {
            return m_root ? At<uint32_t>(m_root, off::CMapObj::fields::m_wmoFileSize) : 0;
        }

        /// Sets the buffer size after reshaping the buffer in place.
        void SetSize(uint32_t size)
        {
            if (m_root) At<uint32_t>(m_root, off::CMapObj::fields::m_wmoFileSize) = size;
        }

        uint32_t GetGroupCount() const
        {
            return m_root ? At<uint32_t>(m_root, off::CMapObj::fields::m_mapObjGroupCount) : 0;
        }

        /// Group object at i, or null when out of range.
        void* GetGroupAt(uint32_t i) const
        {
            if (!m_root || i >= GetGroupCount())
                return nullptr;
            return (&At<void*>(m_root, off::CMapObj::fields::m_mapObjGroupArray))[i];
        }

        /// The group's resident state; nonzero force makes it resident.
        unsigned GetGroupResident(unsigned groupIndex, unsigned force) const
        {
            return Native<off::CMapObj::GetGroupFn>(off::CMapObj::GetGroup)(m_root, nullptr, groupIndex, force);
        }

        /// Resolves a material's texture-name offsets; materialIndex is not bounds-checked.
        void ResolveMaterialTexture(int materialIndex)
        {
            Native<off::CMapObj::CreateMaterialFn>(off::CMapObj::CreateMaterial)(m_root, nullptr, materialIndex);
        }

    private:
        void* m_root;
    };

    /// One WMO group (CMapObjGroup): its file buffer. Every getter returns 0 or null on a null group.
    class WmoGroup
    {
    public:
        /// Wraps a raw CMapObjGroup pointer (may be null).
        explicit WmoGroup(void* group) : m_group(group) {}

        void* raw() const { return m_group; }

        explicit operator bool() const { return m_group != nullptr; }

        void* GetBuffer() const
        {
            return m_group ? At<void*>(m_group, off::CMapObjGroup::fields::m_filePtr) : nullptr;
        }

        /// Buffer size in bytes: the bound of the native sub-chunk walk.
        uint32_t GetSize() const
        {
            return m_group ? At<uint32_t>(m_group, off::CMapObjGroup::fields::m_fileSize) : 0;
        }

        /// Sets the buffer size after reshaping the buffer in place.
        void SetSize(uint32_t size)
        {
            if (m_group) At<uint32_t>(m_group, off::CMapObjGroup::fields::m_fileSize) = size;
        }

    private:
        void* m_group;
    };

    /// File path of the WMO the camera is inside, or null outdoors.
    inline const char* GetCurrentInteriorPath()
    {
        void* instance = Read<void*>(off::CWorldScene::s_viewerMapObjDef);
        if (!instance)
            return nullptr;
        void* root = At<void*>(instance, off::CMapObjDef::fields::m_owner);
        if (!root)
            return nullptr;
        return static_cast<char*>(root) + off::CMapObj::fields::m_wmoName;
    }

    /// Global outdoor-render gate; negative suppresses the exterior pass while the camera is inside a WMO.
    inline float GetOutdoorGateValue()
    {
        return Read<float>(off::CWorldScene::frustumPortalView + kOutdoorGateOffset);
    }
}
