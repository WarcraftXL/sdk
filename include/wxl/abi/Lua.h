// SPDX-License-Identifier: GPL-3.0-or-later
// The wxl.lua service: global script functions and settings sets an extension adds to the client's Lua.

#ifndef WXL_LUA_API_H
#define WXL_LUA_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WXL_LUA_API_NAME "wxl.lua"
#define WXL_LUA_API_VERSION 1

/// A script function: reads its arguments (one-based), pushes its results, answers how many.
/// Runs on the main thread, inside a script call.
typedef int(__cdecl* WXL_LuaFunction)(void* state);

/// The type of a WXL_SettingValue.
enum
{
    WXL_SETTING_NIL     = 0,
    WXL_SETTING_BOOLEAN = 1,
    WXL_SETTING_NUMBER  = 2,
    WXL_SETTING_STRING  = 3,
};

typedef struct WXL_SettingValue
{
    int32_t     type;    ///< WXL_SETTING_*
    int32_t     boolean;
    double      number;
    const char* string;  ///< from Get: valid until the next call into the set
} WXL_SettingValue;

/// A named set of player settings that Lua reaches through WXL_Settings_List/Get/Set/GetAll/ApplyPreset/Save/
/// IsAvailable(set, ...), which never raise. Static storage; every callback gets `user` back.
typedef struct WXL_SettingsSet
{
    uint32_t           structSize;
    const char*        name; ///< what Lua names the set by, e.g. "foliage"
    void*              user;
    const char* const* keys; ///< the keys GetAll walks, in order
    uint32_t           keyCount;

    /// Fills `out`; nonzero for a known key.
    int(__cdecl* Get)(void* user, const char* key, WXL_SettingValue* out);
    /// Applies a value live; nonzero when taken.
    int(__cdecl* Set)(void* user, const char* key, const WXL_SettingValue* value);
    /// Applies a named preset; nonzero when known. May be null.
    int(__cdecl* ApplyPreset)(void* user, const char* preset);
    /// Persists the set; nonzero when written. May be null.
    int(__cdecl* Save)(void* user);
    /// Nonzero when the set can work in this session. May be null (always available).
    int(__cdecl* IsAvailable)(void* user);
} WXL_SettingsSet;

typedef struct WXL_LuaApi
{
    uint32_t structSize;
    uint32_t apiVersion;

    /// Adds a global function (name copied, 63 characters at most, prefix it) to every script context,
    /// before the interface loads; 0 for a null argument or a full registry (256). Again: replaces it.
    int(__cdecl* RegisterFunction)(const char* name, WXL_LuaFunction fn);

    /// Adds a settings set the WXL_Settings_* functions reach by name; 0 for a bad set or a full table (32).
    int(__cdecl* RegisterSettings)(const WXL_SettingsSet* set);

    int(__cdecl* ArgCount)(void* state);
    /// The Lua type of an argument: -1 none, 0 nil, 1 boolean, 3 number, 4 string, 5 table, 6 function.
    int(__cdecl* Type)(void* state, int index);
    double(__cdecl* ToNumber)(void* state, int index);
    /// Valid until the call returns; null when not convertible.
    const char*(__cdecl* ToString)(void* state, int index);
    /// Lua truth: false for nil and false only.
    int(__cdecl* ToBoolean)(void* state, int index);

    void(__cdecl* PushNil)(void* state);
    void(__cdecl* PushNumber)(void* state, double value);
    void(__cdecl* PushString)(void* state, const char* value);
    void(__cdecl* PushBoolean)(void* state, int value);
    void(__cdecl* NewTable)(void* state);
    /// t[key] = value on the table at the top of the stack, which stays there.
    void(__cdecl* SetFieldNumber)(void* state, const char* key, double value);
    void(__cdecl* SetFieldString)(void* state, const char* key, const char* value);
    void(__cdecl* SetFieldBoolean)(void* state, const char* key, int value);

    /// Raises a Lua error; it does not return, so nothing with a destructor may live in the caller.
    void(__cdecl* Error)(void* state, const char* message);
} WXL_LuaApi;

#ifdef __cplusplus
}
#endif

#endif /* WXL_LUA_API_H */
