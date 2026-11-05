// SPDX-License-Identifier: GPL-3.0-or-later
// The wxl.profiles service: named snapshots of every settings set, in WTF\WarcraftXL\profiles\<name>.cfg.

#ifndef WXL_PROFILES_API_H
#define WXL_PROFILES_API_H

#include <stdint.h>

#include "wxl/abi/Plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WXL_PROFILES_API_NAME    "wxl.profiles"
#define WXL_PROFILES_API_VERSION 1

/// Main thread only. WXL_PROFILE=<name> in WarcraftXL.cfg loads a profile at start.
typedef void(__cdecl* WXL_ProfileVisitFn)(void* user, const char* name);

typedef struct WXL_ProfilesApi
{
    uint32_t structSize;
    uint32_t apiVersion;

    /// WXL_OK for 1 to 48 letters, digits, spaces, '-', '_' or '.' not starting with '.'; else WXL_E_INVALID_ARGUMENT.
    int(__cdecl* ValidName)(const char* name);
    /// Writes every settings set under `name`, replacing it; WXL_E_INVALID_ARGUMENT for a bad name or write.
    int(__cdecl* Save)(const char* name);
    /// Applies a profile set by set, skipping unknown sets, keys and refused values; WXL_E_NOT_FOUND.
    int(__cdecl* Load)(const char* name);
    /// WXL_E_NOT_FOUND when there is no such profile.
    int(__cdecl* Delete)(const char* name);
    /// Calls fn for every saved profile, sorted by name.
    int(__cdecl* List)(WXL_ProfileVisitFn fn, void* user);
    /// The profile last loaded or saved this session, "" for none.
    const char*(__cdecl* Current)(void);
} WXL_ProfilesApi;

#ifdef __cplusplus
}
#endif

#endif
