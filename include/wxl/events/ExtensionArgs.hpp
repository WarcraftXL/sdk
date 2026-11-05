// SPDX-License-Identifier: GPL-3.0-or-later
// The args of the core's own events: settings and the end of the extension loads.

#pragma once

#include <cstdint>

namespace wxl::events
{
    /// OnSettingChanged: the extension, the key and the new value as text, valid during the call.
    struct SettingChangedArgs { const char* extension; const char* key; const char* value; };

    /// OnExtensionsLoaded: how many extensions loaded.
    struct ExtensionsLoadedArgs { uint32_t loaded; };
}
