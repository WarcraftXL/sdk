// SPDX-License-Identifier: GPL-3.0-or-later
// Follows the player's target's health: an observable property, object handles and the Game facade.

#include <wxl/Game.hpp>
#include <wxl/wxl.hpp>

using namespace wxl;

class Watch : public Extension
{
    Listener health;

    void OnTargetChanged(void*) override
    {
        health.Stop();
        auto target = Game().Target();
        if (!target || target->IsNull()) return;
        const Unit unit = target->AsUnit();
        if (unit.IsNull()) return;

        // Ends by itself once the unit has left view.
        health = On(unit.HealthChanged(), [](const Change<uint32_t>& c)
        {
            Log::Info("target health {} -> {}", c.previous, c.value);
        });
    }
};

Export<Watch> extension;
