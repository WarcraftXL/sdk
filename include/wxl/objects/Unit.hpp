// SPDX-License-Identifier: GPL-3.0-or-later
// wxl::Unit: a creature or a player, with a body model and a reaction toward other units.

#pragma once

#include <cstdint>

#include "wxl/game/Unit.hpp"
#include "wxl/game/World.hpp"
#include "wxl/objects/Object.hpp"
#include "wxl/objects/Observable.hpp"
#include "wxl/offsets/CGObject_C.hpp"
#include "wxl/offsets/tables/update_fields.hpp"

namespace wxl
{
    namespace detail::unit_fields
    {
        // Byte offsets in a unit's descriptor block.
        inline constexpr uint32_t kSlotBytes = game::unit::kFieldSlotBytes;
        inline constexpr uint32_t kTarget    = offsets::fields::UNIT_FIELD_TARGET * kSlotBytes; // uint64
        inline constexpr uint32_t kHealth    = offsets::fields::UNIT_FIELD_HEALTH * kSlotBytes;
        inline constexpr uint32_t kMaxHealth = offsets::fields::UNIT_FIELD_MAXHEALTH * kSlotBytes;
        inline constexpr uint32_t kLevel     = offsets::fields::UNIT_FIELD_LEVEL * kSlotBytes;
    }

    /// A unit: a creature or a player.
    class Unit : public Object
    {
    public:
        static constexpr uint32_t kTypeMask = game::world::kTypeMaskUnit;

        Unit() = default;
        explicit Unit(WXL_Handle handle) : Object(handle) {}

        /// The resident unit with that GUID; null when not resident or not a unit.
        static Unit FromGuid(uint64_t guid) { return Object::FromGuid(guid, kTypeMask).AsUnit(); }

        /// The body model node game::unit::ModelParent walks up (not the M2 object wxl::Model wraps).
        Result<void*> Model() const
        {
            auto unit = Resolve(kTypeMask);
            if (!unit) return unit.Code();
            return game::unit::Model(*unit);
        }

        /// The CharacterComponent the equip and attach pipeline works on; null for a non-humanoid.
        Result<void*> CharacterComponent() const
        {
            auto unit = Resolve(kTypeMask);
            if (!unit) return unit.Code();
            return game::unit::CharacterComponent(*unit);
        }

        /// This unit's reaction toward another: 0..1 hostile, 2..3 neutral, 4 and above friendly.
        Result<int> Reaction(const Unit& other) const
        {
            auto unit = Resolve(kTypeMask);
            if (!unit) return unit.Code();
            auto them = other.Resolve(kTypeMask);
            if (!them) return them.Code();
            return game::unit::Reaction(*unit, *them);
        }

        /// The name the client shows ("Unknown" until the server answers); valid until control returns
        /// to the client. NameChanged() says when it changes.
        Result<const char*> Name() const
        {
            auto unit = Resolve(kTypeMask);
            if (!unit) return unit.Code();
            return game::unit::Name(*unit);
        }

        Result<uint32_t> Health() const { return Field<uint32_t>(detail::unit_fields::kHealth); }
        Result<uint32_t> MaxHealth() const { return Field<uint32_t>(detail::unit_fields::kMaxHealth); }
        Result<uint32_t> Level() const { return Field<uint32_t>(detail::unit_fields::kLevel); }

        /// What it targets; a null Object when nothing, or when the target is out of view.
        Result<Object> Target() const
        {
            auto guid = Field<uint64_t>(detail::unit_fields::kTarget);
            if (!guid) return guid.Code();
            return *guid ? Object::FromGuid(*guid) : Object();
        }

        Signal<Change<uint32_t>> HealthChanged() const { return Changed<uint32_t>("HealthChanged", &Unit::Health); }
        Signal<Change<uint32_t>> MaxHealthChanged() const { return Changed<uint32_t>("MaxHealthChanged", &Unit::MaxHealth); }
        Signal<Change<uint32_t>> LevelChanged() const { return Changed<uint32_t>("LevelChanged", &Unit::Level); }
        Signal<Change<Object>> TargetChanged() const { return Changed<Object>("TargetChanged", &Unit::Target); }

        /// Each damage this unit takes, while it is resident.
        Signal<events::UnitDamageArgs> Damaged() const { return SignalOf<::wxl::UnitDamage>(m_handle); }
        /// Its death, while it is resident.
        Signal<events::UnitDeathArgs> Died() const { return SignalOf<::wxl::UnitDeath>(m_handle); }
        /// Each change of the name it shows, while it is resident.
        Signal<events::UnitNameArgs> NameChanged() const { return SignalOf<::wxl::UnitNameChanged>(m_handle); }

        /// True when both are resident and the reaction is hostile.
        bool IsHostileTo(const Unit& other) const { return Reaction(other).ValueOr(kNeutral) <= kHostileMax; }
        /// True when both are resident and the reaction is friendly.
        bool IsFriendlyTo(const Unit& other) const { return Reaction(other).ValueOr(kNeutral) >= kFriendlyMin; }

    private:
        static constexpr int kHostileMax  = 1;
        static constexpr int kNeutral     = 2;
        static constexpr int kFriendlyMin = 4;

        /// A field of the descriptor block the object header points at.
        template <class T>
        Result<T> Field(uint32_t offset) const
        {
            auto unit = Resolve(kTypeMask);
            if (!unit) return unit.Code();
            const auto* fields = game::At<const uint8_t*>(*unit, offsets::CGObject_C::fields::m_obj);
            return *reinterpret_cast<const T*>(fields + offset);
        }

        template <class T>
        Signal<Change<T>> Changed(const char* name, Result<T> (Unit::*read)() const) const
        {
            const Unit self = *this;
            return detail::Watch<T>(name, [self, read] { return (self.*read)(); });
        }
    };

    inline Unit Object::AsUnit() const { return IsUnit() ? Unit(m_handle) : Unit(); }
}
