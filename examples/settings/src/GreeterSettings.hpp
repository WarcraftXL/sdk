// SPDX-License-Identifier: GPL-3.0-or-later
// The greeter's settings: what it says, and when.

#pragma once

#include <string>

#include <wxl/Annotate.hpp>

struct WXL_SETTINGS("Greeter") GreeterSettings
{
    /// What the greeting says; %u is the player's level.
    WXL_SETTING() std::string Text = "Welcome back, level %u.";
    /// Seconds after entering the world before the greeting.
    WXL_SETTING(range(0, 30), step = 0.5) float Delay = 2.0f;
    /// How many times a session it greets.
    WXL_SETTING(range(1, 10)) int Times = 1;
    /// Whether it greets at all.
    WXL_SETTING() bool Enabled = true;
};
