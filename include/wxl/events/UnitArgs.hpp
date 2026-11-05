// SPDX-License-Identifier: GPL-3.0-or-later
// The args of the unit events: damage, death and the shown name.

#pragma once

#include "wxl/abi/Plugin.h"

#include <cstdint>

namespace wxl::events
{
    /// Where an OnUnitDamage entry comes from.
    enum class DamageKind : uint32_t
    {
        Melee       = 0, ///< a melee or ranged auto-attack round
        Spell       = 1,
        Periodic    = 2, ///< a damage-over-time tick
        Split       = 3, ///< a share of damage split from another unit
        Shield      = 4, ///< a damage shield struck back at the attacker
        Environment = 5, ///< falling, drowning, lava, fire...; `environment` says which
    };

    /// OnUnitDamage, read-only: the amounts as the server sent them; handles empty for units out of view.
    struct UnitDamageArgs
    {
        WXL_Handle source;      ///< empty for the environment
        WXL_Handle target;
        uint64_t   sourceGuid;  ///< 0 for the environment
        uint64_t   targetGuid;
        DamageKind kind;
        int32_t    spellId;     ///< 0 for melee and the environment
        int32_t    amount;      ///< the damage taken
        int32_t    overkill;    ///< past zero health, when the server says
        uint32_t   schoolMask;  ///< 1 physical, 2 holy, 4 fire, 8 nature, 16 frost, 32 shadow, 64 arcane
        int32_t    absorbed;
        int32_t    resisted;
        int32_t    blocked;
        uint32_t   environment; ///< kind Environment: 0 exhausted, 1 drowning, 2 fall, 3 lava, 4 slime, 5 fire, 6 void
        bool       critical;
    };

    /// OnUnitDeath: the unit, still resident.
    struct UnitDeathArgs { WXL_Handle unit; uint64_t guid; };

    /// OnUnitNameChanged: the name the client now shows, valid during the call.
    struct UnitNameArgs { WXL_Handle unit; uint64_t guid; const char* name; };
}
