// SPDX-License-Identifier: GPL-3.0-or-later
// The smallest extension: an override counting frames, and a handler on the player's target.

#include <wxl/Game.hpp>
#include <wxl/wxl.hpp>

using namespace wxl;

class Hello : public Extension
{
    uint32_t ticks = 0;

    void OnStart() override
    {
        Log::Info("{} {} started", Name(), Version());

        On<TargetChanged>([](auto&)
        {
            if (auto me = Game().Player())
                Log::Info("level {} player changed target", me->Level().ValueOr(0));
        });
    }

    void OnUpdate(float, uint32_t) override
    {
        ++ticks;
    }

    void OnWorldEnter(MapId map) override
    {
        Log::Info("map {} after {} ticks", map, ticks);
    }
};

Export<Hello> extension;
