// SPDX-License-Identifier: GPL-3.0-or-later
// wxl::Player: a player character, and the active player's selection (target, mouseover).

#pragma once

#include <cstdint>

#include "wxl/game/World.hpp"
#include "wxl/objects/Object.hpp"
#include "wxl/objects/Unit.hpp"

namespace wxl
{
    /// A player character, the active one included.
    class Player : public Unit
    {
    public:
        static constexpr uint32_t kTypeMask = game::world::kTypeMaskPlayer;

        Player() = default;
        explicit Player(WXL_Handle handle) : Unit(handle) {}

        /// The resident player with that GUID; null when not resident or not a player.
        static Player FromGuid(uint64_t guid) { return Object::FromGuid(guid, kTypeMask).AsPlayer(); }

        /// The active player; null outside the world.
        static Player Active() { return FromGuid(game::world::ActivePlayerGuid()); }

        /// The current target; null when nothing is targeted or the target is not resident.
        static Object Target() { return Object::FromGuid(game::world::TargetGuid()); }

        /// The object under the cursor; null when none.
        static Object Mouseover() { return Object::FromGuid(game::world::MouseoverGuid()); }
    };

    inline Player Object::AsPlayer() const { return IsPlayer() ? Player(m_handle) : Player(); }
}
