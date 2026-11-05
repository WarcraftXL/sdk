// SPDX-License-Identifier: GPL-3.0-or-later
// Overrides and handlers side by side: a priority, Once, a filtered signal and an answer to OnInput.

#include <wxl/wxl.hpp>

using namespace wxl;

namespace
{
    constexpr uint32_t  kKeyDown = 0x0100; // WM_KEYDOWN
    constexpr uintptr_t kF12     = 0x7B;   // VK_F12
}

class Events : public Extension
{
    void OnStart() override
    {
        // Before every other OnUpdate handler, and only once.
        Once<Update>([](auto& e)
        {
            Log::Info("first frame at {} ms", e.timeMs);
        }, priority::First);

        // OnInput asks a question: answering true keeps F12 from the game.
        auto f12 = SignalOf<Input>().Filter([](const InputArgs& e)
        {
            return e.message == kKeyDown && e.wparam == kF12;
        });
        On(f12, [](const InputArgs& e)
        {
            *e.handled = true;
        });
    }

    void OnWorldEnter(MapId map) override
    {
        Log::Info("entered map {}", map);
    }
};

Export<Events> extension;
