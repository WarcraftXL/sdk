// SPDX-License-Identifier: GPL-3.0-or-later
// The C ABI between the core and an extension: the info block, the core's table, the entry points.

#ifndef WXL_PLUGIN_API_H
#define WXL_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#include "wxl/abi/Common.h"
#include "wxl/abi/Version.h"

#ifdef __cplusplus
extern "C" {
#endif

/// The layout of WXL_Api; the core refuses an extension built for another.
#define WXL_API_VERSION 2

/// The client build every address in the SDK is for.
#define WXL_CLIENT_BUILD 12340

#define WXL_LOG_TRACE 0
#define WXL_LOG_DEBUG 1
#define WXL_LOG_INFO  2
#define WXL_LOG_WARN  3
#define WXL_LOG_ERROR 4

/// Hook chain position: lower runs first, closest to the caller.
#define WXL_HOOK_DEFAULT_PRIORITY 0

/// HookAttachEx flags. A resident hook stays attached when its extension is disabled or faults, and keeps
/// the extension's DLL loaded: for code whose removal is worse than the fault, such as an allocator.
#define WXL_HOOK_RESIDENT 1

/// Event handler order: lower runs first; equal ones in connection order.
#define WXL_PRIORITY_FIRST   (-1000)
#define WXL_PRIORITY_DEFAULT 0
#define WXL_PRIORITY_LAST    1000

/// What a call answers; the numbers are wxl::Error's.
#define WXL_OK                 0
#define WXL_E_EXPIRED          1
#define WXL_E_NULL_HANDLE      2
#define WXL_E_UNSUPPORTED      3
#define WXL_E_WRONG_KIND       4
#define WXL_E_NOT_IN_WORLD     5
#define WXL_E_NOT_FOUND        6
#define WXL_E_INVALID_ARGUMENT 7
#define WXL_E_TIMEOUT          8
#define WXL_E_MISMATCH         9
#define WXL_E_DENIED          10

/// One resident object, or one connection: an index and a generation; generation 0 is empty.
typedef struct WXL_Handle
{
    uint32_t index;
    uint32_t generation;
} WXL_Handle;

typedef WXL_Handle WXL_Connection;

/// What an extension says about itself before any of its code runs.
typedef struct WXL_PluginInfo
{
    uint32_t    structSize;
    uint32_t    apiVersion;    ///< WXL_API_VERSION
    const char* name;          ///< in log lines
    uint32_t    pluginVersion; ///< the extension's own, opaque to the core
    uint32_t    clientBuild;   ///< WXL_CLIENT_BUILD
    uint32_t    sdkVersion;    ///< WXL_SDK_VERSION
} WXL_PluginInfo;

/// An event handler: `args` is the event's args struct (wxl/Events.hpp).
typedef void(__cdecl* WXL_EventFn)(void* user, const void* args);

/// Called once when a connection ends, never while its handler runs.
typedef void(__cdecl* WXL_ReleaseFn)(void* user);

/// The core's table, handed to WXL_Load, valid for the process lifetime. Main thread unless said.
typedef struct WXL_Api
{
    uint32_t structSize;
    uint32_t apiVersion;

    /// One line in the core's log. Any thread.
    void(__cdecl* Log)(int level, const char* tag, const char* fmt, ...);

    /// Calls handler(user, args) on each `event` in priority order; a non-empty `subject` limits it to
    /// that object and ends with it. `release`, if any, runs once the connection has ended.
    int(__cdecl* Connect)(uint32_t event, WXL_Handle subject, int32_t priority, WXL_EventFn handler, void* user,
                          WXL_ReleaseFn release, WXL_Connection* out);
    /// Ends a connection. Inside its own handler: that call finishes, no other comes.
    int(__cdecl* Disconnect)(WXL_Connection connection);
    /// WXL_OK while the connection is live, WXL_E_EXPIRED once it has ended.
    int(__cdecl* Connected)(WXL_Connection connection);
    /// Raises an event to every handler, core or extension.
    void(__cdecl* Emit)(uint32_t event, const void* args);

    /// Adds a raw detour; `original` receives the next link of the chain. Needs "low_level" in
    /// wxl.json. Non-zero when registered.
    int(__cdecl* HookAttach)(const char* name, uintptr_t target, void* detour, void** original, int priority);
    /// The same on a hook point the core knows by name.
    int(__cdecl* HookAttachByName)(const char* pointName, void* detour, void** original, int priority);
    /// The detour of an interceptor (wxl/Intercept.hpp), whose handlers run through Run. No permission.
    int(__cdecl* InterceptAttach)(const char* name, uintptr_t target, void* detour, void** original);

    /// The id of the extension whose code runs on this thread (in a service function: the caller).
    uint32_t(__cdecl* Self)(void);
    /// Runs fn(context) as `self`'s code under the core's guard; WXL_E_EXPIRED when that extension is
    /// disabled (fn not called) or faulted in it (now disabled). Any thread.
    int(__cdecl* Run)(uint32_t self, void(__cdecl* fn)(void* context), void* context);

    /// Offers a service under a name and version.
    void(__cdecl* PublishInterface)(const char* name, uint32_t version, void* iface);
    /// The service of that name with the highest published version >= `version`, or NULL.
    void*(__cdecl* GetInterface)(const char* name, uint32_t version);

    /// Writes `length` bytes at `address` if the bytes there equal `expected` (NULL: no check), given
    /// back on disable. Needs "low_level". WXL_OK, WXL_E_MISMATCH, WXL_E_DENIED or WXL_E_INVALID_ARGUMENT.
    int(__cdecl* Patch)(const char* name, uintptr_t address, const void* expected, const void* bytes,
                        uint32_t length);

    /// HookAttach with WXL_HOOK_* flags. Needs "low_level". Absent from a core older than this member.
    int(__cdecl* HookAttachEx)(const char* name, uintptr_t target, void* detour, void** original, int priority,
                               uint32_t flags);
} WXL_Api;

typedef const WXL_PluginInfo*(__cdecl* WXL_QueryFn)(void);
typedef int(__cdecl* WXL_LoadFn)(const WXL_Api* api);

/// The entry points, exported by the extension being compiled only.
#ifdef WXL_EXTENSION
WXL_EXPORT const WXL_PluginInfo* __cdecl WXL_Query(void);
/// Main thread, during engine initialisation: no device, no world yet. Zero declines.
WXL_EXPORT int __cdecl WXL_Load(const WXL_Api* api);
#endif

#ifdef __cplusplus
}
#endif

#endif /* WXL_PLUGIN_API_H */
