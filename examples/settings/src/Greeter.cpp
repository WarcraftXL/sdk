// SPDX-License-Identifier: GPL-3.0-or-later
// Greets the player after entering the world, as its settings say: settings and a coroutine.

#include "GreeterSettings.settings.hpp"

#include <wxl/Game.hpp>
#include <wxl/Task.hpp>
#include <wxl/wxl.hpp>

#include <cstdio>

using namespace wxl;

class Greeter : public Extension
{
    static constexpr size_t kLineSize = 256;

    Settings<GreeterSettings> settings;
    Task                      greeting;
    int                       greeted = 0;

    void OnStart() override
    {
        settings.Start();
    }

    void OnWorldEnter(MapId) override
    {
        if (settings->Enabled && greeted < settings->Times) greeting = Greet();
    }

    Task Greet()
    {
        co_await Delay(settings->Delay);
        auto me = Game().Player();
        if (!me) co_return; // left the world meanwhile
        char line[kLineSize];
        std::snprintf(line, sizeof line, settings->Text.c_str(), me->Level().ValueOr(0));
        Log::Info("{}", line);
        ++greeted;
    }
};

Export<Greeter> extension;
