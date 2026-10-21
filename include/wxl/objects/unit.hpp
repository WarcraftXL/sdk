// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// wxl::Unit: something alive, or recently so.

#ifndef WXL_OBJECTS_UNIT_HPP
#define WXL_OBJECTS_UNIT_HPP

#include "wxl/objects/object.hpp"

namespace wxl
{
    /// The powers a unit may run on, as the index unit_power takes.
    enum class Power : uint32_t
    {
        Mana       = 0,
        Rage       = 1,
        Focus      = 2,
        Energy     = 3,
        Happiness  = 4,
        Runes      = 5,
        RunicPower = 6
    };

    /// Something alive: a creature, an NPC, a player. The client's ID_UNIT and the base of Player.
    class Unit : public Object
    {
    public:
        static constexpr wxl_object_type kKinds[] = {WXL_OBJECT_UNIT, WXL_OBJECT_PLAYER};
        static constexpr bool            Accepts(wxl_object_type type)
        {
            return type == WXL_OBJECT_UNIT || type == WXL_OBJECT_PLAYER;
        }

        using Object::Object;

        /// A value of 0 is a dead unit; a missing value is a gone one.
        Expected<uint32_t> Health() const { return Read(&wxl_object_api::unit_health); }
        Expected<uint32_t> MaxHealth() const { return Read(&wxl_object_api::unit_max_health); }
        Expected<uint32_t> Level() const { return Read(&wxl_object_api::unit_level); }

        /// Health as a fraction in 0..1; 0 when the unit has no maximum to compare against.
        Expected<float> HealthFraction() const
        {
            const Expected<uint32_t> max = MaxHealth();
            if (!max) return Unexpected{max.Error()};
            const Expected<uint32_t> health = Health();
            if (!health) return Unexpected{health.Error()};
            return *max ? static_cast<float>(*health) / static_cast<float>(*max) : 0.0f;
        }

        /// What the unit targets. A null Unit as the value means it targets nothing in view.
        Expected<Unit> Target() const
        {
            Expected<wxl_handle> target = Read(&wxl_object_api::unit_target);
            if (!target) return Unexpected{target.Error()};
            return Unit(*target);
        }

        /// How this unit regards @p other: 0-1 hostile, 2-3 neutral, 4-7 friendly.
        Expected<uint32_t> ReactionTo(const Unit& other) const
        {
            const wxl_object_api* api = ObjectService::Raw();
            if (!detail::Carries(api, &wxl_object_api::unit_reaction)) return Unexpected{WXL_E_UNSUPPORTED};
            if (!HasHandle() || !other.HasHandle()) return Unexpected{WXL_E_NOT_FOUND};
            uint32_t         out    = 0;
            const wxl_result result = api->unit_reaction(Handle(), other.Handle(), &out);
            if (result != WXL_OK) return Unexpected{result};
            return out;
        }

        Expected<uint32_t> PowerValue(Power power) const
        {
            return ReadAt(&wxl_object_api::unit_power, static_cast<uint32_t>(power));
        }
        Expected<uint32_t> MaxPowerValue(Power power) const
        {
            return ReadAt(&wxl_object_api::unit_max_power, static_cast<uint32_t>(power));
        }

        /// The power the unit runs on.
        Expected<Power> PowerType() const
        {
            const Expected<uint32_t> type = Read(&wxl_object_api::unit_power_type);
            if (!type) return Unexpected{type.Error()};
            return static_cast<Power>(*type);
        }

        /// The unit's own power and its maximum, by PowerType().
        Expected<uint32_t> PowerValue() const
        {
            const Expected<Power> type = PowerType();
            if (!type) return Unexpected{type.Error()};
            return PowerValue(*type);
        }
        Expected<uint32_t> MaxPowerValue() const
        {
            const Expected<Power> type = PowerType();
            if (!type) return Unexpected{type.Error()};
            return MaxPowerValue(*type);
        }

        Expected<uint32_t> Race() const { return Read(&wxl_object_api::unit_race); }
        Expected<uint32_t> Class() const { return Read(&wxl_object_api::unit_class); }
        /// 0 male, 1 female, 2 none.
        Expected<uint32_t> Sex() const { return Read(&wxl_object_api::unit_sex); }

        Expected<uint32_t> DisplayId() const { return Read(&wxl_object_api::unit_display_id); }
        Expected<uint32_t> NativeDisplayId() const { return Read(&wxl_object_api::unit_native_display_id); }
        /// 0 while not mounted.
        Expected<uint32_t> MountDisplayId() const { return Read(&wxl_object_api::unit_mount_display_id); }

        Expected<uint32_t> FactionTemplate() const { return Read(&wxl_object_api::unit_faction_template); }
        Expected<uint32_t> Flags() const { return Read(&wxl_object_api::unit_flags); }
        Expected<uint32_t> Flags2() const { return Read(&wxl_object_api::unit_flags2); }
        Expected<uint32_t> DynamicFlags() const { return Read(&wxl_object_api::unit_dynamic_flags); }
        Expected<uint32_t> NpcFlags() const { return Read(&wxl_object_api::unit_npc_flags); }

        Expected<float> BoundingRadius() const { return Read(&wxl_object_api::unit_bounding_radius); }
        Expected<float> CombatReach() const { return Read(&wxl_object_api::unit_combat_reach); }

        /// Who it charms, summons, is charmed by, was summoned by, was created by; none as 0.
        Expected<wxl_guid> CharmGuid() const { return Read(&wxl_object_api::unit_charm); }
        Expected<wxl_guid> SummonGuid() const { return Read(&wxl_object_api::unit_summon); }
        Expected<wxl_guid> CharmedByGuid() const { return Read(&wxl_object_api::unit_charmed_by); }
        Expected<wxl_guid> SummonedByGuid() const { return Read(&wxl_object_api::unit_summoned_by); }
        Expected<wxl_guid> CreatedByGuid() const { return Read(&wxl_object_api::unit_created_by); }
    };
} // namespace wxl

namespace std
{
    template <>
    struct hash<wxl::Unit> : hash<wxl::Object>
    {
    };
} // namespace std

#endif // WXL_OBJECTS_UNIT_HPP
