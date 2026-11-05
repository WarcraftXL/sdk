// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "wxl/offsets/free/I.hpp"

/// The engine initialisation routine: runs once on the main thread, before the file reader and textures.
namespace wxl::game::boot
{
    namespace off = wxl::offsets;

    constexpr uintptr_t kEngineInit = off::InitializeEngineCallback;

    using EngineInitFn = off::InitializeEngineCallbackFn;
}
