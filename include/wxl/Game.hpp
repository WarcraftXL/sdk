// SPDX-License-Identifier: GPL-3.0-or-later
// wxl::Game(): the player, the selection, the world, and the game-wide signals.

#pragma once

#include "wxl/Signal.hpp"
#include "wxl/game/World.hpp"
#include "wxl/objects/GameObject.hpp"
#include "wxl/objects/Player.hpp"

namespace wxl
{
    /// The world the player is in.
    class WorldFacade
    {
    public:
        /// The map id; NotInWorld at the login or loading screen.
        Result<int> MapId() const
        {
            if (!InWorld()) return Error::NotInWorld;
            return game::world::CurrentMapId();
        }

        bool InWorld() const { return !wxl::Player::Active().IsNull(); }

        /// The world finished loading.
        Signal<events::WorldEnterArgs> Entered() const { return SignalOf<::wxl::WorldEnter>(); }
        /// The world is being left; the map is still loaded.
        Signal<events::WorldLeaveArgs> Left() const { return SignalOf<::wxl::WorldLeave>(); }
        /// A click resolved to a point or an object of the world.
        Signal<events::WorldClickArgs> Clicked() const { return SignalOf<::wxl::WorldClick>(); }

        /// Calls fn(object) for each resident object of the kinds in `typeMask`; fn returns false to stop.
        template <class Fn>
        void ForEachObject(uint32_t typeMask, Fn fn) const
        {
            if (!InWorld()) return;
            game::world::ForEachObject(typeMask, [&](unsigned long long guid, void*) {
                const Object object = Object::FromGuid(guid);
                return object.IsNull() ? true : static_cast<bool>(fn(object));
            });
        }
    };

    /// What wxl::Game() answers.
    class GameFacade
    {
    public:
        /// The active player; NotInWorld outside the world. The same as Player::Active().
        Result<wxl::Player> Player() const
        {
            wxl::Player player = wxl::Player::Active();
            if (player.IsNull()) return Error::NotInWorld;
            return player;
        }

        /// The current target; a null Object when there is none.
        Result<Object> Target() const
        {
            if (Player().HasValue()) return wxl::Player::Target();
            return Error::NotInWorld;
        }

        /// The object under the cursor; a null Object when there is none.
        Result<Object> Mouseover() const
        {
            if (Player().HasValue()) return wxl::Player::Mouseover();
            return Error::NotInWorld;
        }

        WorldFacade World() const { return {}; }

        /// Once per frame, the logic tick.
        Signal<events::UpdateArgs> Tick() const { return SignalOf<::wxl::Update>(); }
        /// A window message; set `*handled` to swallow it.
        Signal<events::InputArgs> Input() const { return SignalOf<::wxl::Input>(); }
        /// The player's target changed.
        Signal<events::TargetChangedArgs> TargetChanged() const { return SignalOf<::wxl::TargetChanged>(); }
    };

    /// The way in:
    ///     if (auto me = Game().Player()) Log::Info("level {}", me->Level().ValueOr(0));
    inline GameFacade Game() { return {}; }
}
