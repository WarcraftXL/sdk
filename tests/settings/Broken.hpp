// SPDX-License-Identifier: GPL-3.0-or-later
// A settings header the settings tool must refuse: no WXL_SETTING, no doc, a ranged bool, no setting type.

#pragma once

#include <wxl/Annotate.hpp>

struct WXL_SETTINGS("Broken") BrokenSettings
{
    int Plain = 0;
    WXL_SETTING() int Undocumented = 0;
    /// A flag.
    WXL_SETTING(range(0, 1)) bool Ranged = false;
    /// A pointer.
    WXL_SETTING() const char* Name = nullptr;
};
