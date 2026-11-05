// SPDX-License-Identifier: GPL-3.0-or-later
// wxl::Wmo and wxl::WmoGroup: handles over a map-object root and one of its groups (OnWmoRootLoad, OnWmoGroupLoad).

#pragma once

#include <cstdint>

#include "wxl/game/Wmo.hpp"

namespace wxl
{
    /// One map-object group: the raw sub-chunk buffer the client's walk reads.
    class WmoGroup
    {
    public:
        WmoGroup() = default;
        /// Wraps a raw WMO group pointer (may be null).
        explicit WmoGroup(void* raw) : m_raw(raw) {}

        /// True when the handle holds an object.
        explicit operator bool() const { return m_raw != nullptr; }
        /// The raw engine pointer, for the bindings that take one.
        void* Raw() const { return m_raw; }

        /// The sub-chunk buffer; null for a null handle.
        void* Buffer() const { return game::wmo::WmoGroup(m_raw).GetBuffer(); }
        /// The buffer's byte size, the bound the client's walk reads to.
        uint32_t Size() const { return game::wmo::WmoGroup(m_raw).GetSize(); }
        /// Sets that bound, after reshaping the buffer in place.
        void SetSize(uint32_t size) { game::wmo::WmoGroup(m_raw).SetSize(size); }

        bool operator==(const WmoGroup& o) const { return m_raw == o.m_raw; }
        bool operator!=(const WmoGroup& o) const { return m_raw != o.m_raw; }

    private:
        void* m_raw = nullptr;
    };

    /// One map-object root: the raw chunk buffer the client's walk reads, and its groups.
    class Wmo
    {
    public:
        Wmo() = default;
        /// Wraps a raw WMO root pointer (may be null).
        explicit Wmo(void* raw) : m_raw(raw) {}

        /// True when the handle holds an object.
        explicit operator bool() const { return m_raw != nullptr; }
        /// The raw engine pointer, for the bindings that take one.
        void* Raw() const { return m_raw; }

        /// The chunk buffer; null for a null handle.
        void* Buffer() const { return game::wmo::WmoRoot(m_raw).GetBuffer(); }
        /// The buffer's byte size, the bound the client's walk reads to.
        uint32_t Size() const { return game::wmo::WmoRoot(m_raw).GetSize(); }
        /// Sets that bound, after reshaping the buffer in place.
        void SetSize(uint32_t size) { game::wmo::WmoRoot(m_raw).SetSize(size); }

        /// Number of groups of the root; 0 when null.
        uint32_t GroupCount() const { return game::wmo::WmoRoot(m_raw).GetGroupCount(); }
        /// The group at `i`; null when out of range.
        WmoGroup GroupAt(uint32_t i) const { return WmoGroup(game::wmo::WmoRoot(m_raw).GetGroupAt(i)); }

        /// The resident state of a group; a nonzero `force` makes it resident.
        unsigned GroupResident(unsigned groupIndex, unsigned force) const
        { return game::wmo::WmoRoot(m_raw).GetGroupResident(groupIndex, force); }

        /// Resolves one material's texture-name offsets; the index is not bounds-checked.
        void ResolveMaterialTexture(int materialIndex)
        { game::wmo::WmoRoot(m_raw).ResolveMaterialTexture(materialIndex); }

        /// The path of the map object the camera is inside; null outdoors or while it builds.
        static const char* CurrentInteriorPath() { return game::wmo::GetCurrentInteriorPath(); }

        /// The outdoor-render gate read while the camera is indoors; negative skips the exterior pass.
        static float OutdoorGateValue() { return game::wmo::GetOutdoorGateValue(); }

        bool operator==(const Wmo& o) const { return m_raw == o.m_raw; }
        bool operator!=(const Wmo& o) const { return m_raw != o.m_raw; }

    private:
        void* m_raw = nullptr;
    };
}
