// SPDX-License-Identifier: GPL-3.0-or-later
// The traits of the core's own service tables, so Service<WXL_LuaApi> and the wrappers find them.

#pragma once

#include "wxl/Service.hpp"
#include "wxl/abi/GraphicsDevice.h"
#include "wxl/abi/Lua.h"
#include "wxl/abi/Objects.h"
#include "wxl/abi/Profiles.h"
#include "wxl/abi/Settings.h"
#include "wxl/abi/Storage.h"

namespace wxl
{
#define WXL_DETAIL_CORE_SERVICE(T, name, version)                                                    \
    template <>                                                                                    \
    struct ServiceTraits<T>                                                                        \
    {                                                                                              \
        using Table                         = T;                                                   \
        static constexpr const char* kName  = name;                                                \
        static constexpr uint32_t    kVersion = version;                                           \
    };
    WXL_DETAIL_CORE_SERVICE(WXL_StorageApi, "wxl.storage", WXL_STORAGE_API_VERSION)
    WXL_DETAIL_CORE_SERVICE(WXL_ObjectsApi, WXL_OBJECTS_API_NAME, WXL_OBJECTS_API_VERSION)
    WXL_DETAIL_CORE_SERVICE(WXL_SettingsApi, WXL_SETTINGS_API_NAME, WXL_SETTINGS_API_VERSION)
    WXL_DETAIL_CORE_SERVICE(WXL_ProfilesApi, WXL_PROFILES_API_NAME, WXL_PROFILES_API_VERSION)
    WXL_DETAIL_CORE_SERVICE(WXL_LuaApi, WXL_LUA_API_NAME, WXL_LUA_API_VERSION)
    WXL_DETAIL_CORE_SERVICE(WXL_GraphicsDeviceApi, WXL_GRAPHICS_DEVICE_API_NAME, WXL_GRAPHICS_DEVICE_API_VERSION)
#undef WXL_DETAIL_CORE_SERVICE
}
