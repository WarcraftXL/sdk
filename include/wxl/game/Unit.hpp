// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/CGObject_C.hpp"
#include "wxl/offsets/CGUnit_C.hpp"
#include "wxl/offsets/CM2Model.hpp"
#include "wxl/offsets/tables/update_fields.hpp"

/// Unit models, reaction and size fields. Every accessor returns null or 0 for a null unit.
namespace wxl::game::unit
{
    namespace off = wxl::offsets;

    /// Bytes per descriptor field slot.
    constexpr uint32_t kFieldSlotBytes = 4;

    /// The unit's body model.
    inline void* Model(void* unit)
    { return unit ? At<void*>(unit, off::CGObject_C::fields::m_worldModel) : nullptr; }

    /// The model's parent in the attachment chain, null at the root.
    inline void* ModelParent(void* model)
    { return model ? At<void*>(model, off::CM2Model::fields::m_attachParent) : nullptr; }

    /// The CharacterComponent that equips and attaches; null for non-humanoids or before it is built.
    inline void* CharacterComponent(void* unit)
    { return unit ? At<void*>(unit, off::CGUnit_C::fields::m_characterComponent) : nullptr; }

    /// Reaction of self toward other: 0..1 hostile, 2..3 neutral, 4+ friendly.
    inline int Reaction(void* self, void* other)
    { return Native<off::CGUnit_C::UnitReaction_7251C0Fn>(off::CGUnit_C::UnitReaction_7251C0)(self, nullptr, other); }

    /// Bounding radius in yards, as set by the server.
    inline float BoundingRadius(void* unit)
    {
        const auto* header = unit ? At<const uint8_t*>(unit, off::CGObject_C::fields::m_obj) : nullptr;
        return header ? *reinterpret_cast<const float*>(header + kFieldSlotBytes * off::fields::UNIT_FIELD_BOUNDINGRADIUS) : 0.0f;
    }

    /// Combat reach in yards.
    inline float CombatReach(void* unit)
    {
        const auto* header = unit ? At<const uint8_t*>(unit, off::CGObject_C::fields::m_obj) : nullptr;
        return header ? *reinterpret_cast<const float*>(header + kFieldSlotBytes * off::fields::UNIT_FIELD_COMBATREACH) : 0.0f;
    }

    /// The name the client shows, its own "Unknown" text until the server has answered the name query.
    /// Valid until control returns to the client.
    inline const char* Name(void* unit)
    {
        constexpr int32_t kFollowImageAuras = 1; // a mirror image shows its caster's name
        return unit ? Native<off::CGUnit_C::GetUnitNameFn>(off::CGUnit_C::GetUnitName)(unit, nullptr, nullptr, kFollowImageAuras)
                    : nullptr;
    }

    /// The mount's display id, 0 when not mounted.
    inline uint32_t MountDisplayId(void* unit)
    {
        const auto* header = unit ? At<const uint8_t*>(unit, off::CGObject_C::fields::m_obj) : nullptr;
        return header ? *reinterpret_cast<const uint32_t*>(header + kFieldSlotBytes * off::fields::UNIT_FIELD_MOUNTDISPLAYID) : 0u;
    }
}
