// SPDX-License-Identifier: GPL-3.0-or-later
// The wxl.settings service: each extension's settings in Extensions\<name>\<name>.ini (C++: wxl/Settings.hpp).

#ifndef WXL_SETTINGS_API_H
#define WXL_SETTINGS_API_H

#include <stdint.h>

#include "wxl/abi/Plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WXL_SETTINGS_API_NAME    "wxl.settings"
#define WXL_SETTINGS_API_VERSION 1

#define WXL_SETTING_KIND_BOOL    0
#define WXL_SETTING_KIND_INTEGER 1
#define WXL_SETTING_KIND_NUMBER  2
#define WXL_SETTING_KIND_TEXT    3

/// One setting as declared. max < min: no range. ui: "slider", "checkbox" or "input".
typedef struct WXL_SettingDecl
{
    uint32_t    structSize;
    const char* section;
    const char* key;
    uint32_t    kind;  ///< WXL_SETTING_KIND_*
    const char* value; ///< the default, as text
    const char* doc;
    double      min;
    double      max;
    double      step;
    const char* ui;
} WXL_SettingDecl;

/// One setting of one extension, with its current value; both valid for the call only (a Set of
/// that setting from the visitor changes the value).
typedef void(__cdecl* WXL_SettingVisitFn)(void* user, const char* extension, const WXL_SettingDecl* setting);

/// Main thread only; every change raises OnSettingChanged.
typedef struct WXL_SettingsApi
{
    uint32_t structSize;
    uint32_t apiVersion;

    /// Declares the caller's settings, then reads its ini (written with defaults and docs the first
    /// time); a bad value keeps the default, with a warning.
    int(__cdecl* Declare)(const WXL_SettingDecl* settings, uint32_t count);
    /// A setting's value as text; WXL_E_INVALID_ARGUMENT when the buffer is too small (length says).
    int(__cdecl* Get)(const char* key, char* buffer, uint32_t capacity, uint32_t* length);
    /// Changes a setting of the caller: checked, then saved.
    int(__cdecl* Set)(const char* key, const char* value);

    /// Calls fn for every setting of every extension, by extension then in declaration order, without
    /// allocating; a Declare from fn ends the walk.
    int(__cdecl* Visit)(WXL_SettingVisitFn fn, void* user);
    /// Changes a setting of any extension, as /wxl set does; WXL_E_NOT_FOUND, WXL_E_INVALID_ARGUMENT.
    int(__cdecl* SetOf)(const char* extension, const char* key, const char* value);
} WXL_SettingsApi;

#ifdef __cplusplus
}
#endif

#endif
