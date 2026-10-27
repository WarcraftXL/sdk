// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The rules of the boundary: what a type may be, how a version is negotiated, how a symbol is
// exported. Every other SDK header rests on this one.

#ifndef WXL_ABI_H
#define WXL_ABI_H

#include <stddef.h>
#include <stdint.h>

// --- Boundary rules ------------------------------------------------------------------------
//
// A mod and the runtime that loads it are separate binaries, built at different times and often
// with different toolchains. Only plain C crosses between them.
//
//   - No C++ type crosses: no std types, no classes, no vtables, no exceptions, no RTTI.
//     The C++ layer of this SDK compiles into the mod and resolves to the calls below.
//   - Every function pointer spells its calling convention: the default is a compiler flag, and a
//     mod built with another one would corrupt the stack on the first call.
//   - Strings are UTF-8. A `wxl_str` carries its length and is never assumed NUL-terminated;
//     a bare `const char*` is NUL-terminated and is used only for fixed identifiers.
//   - Whoever allocates frees. Nothing handed across is freed by the other side. A pointer given
//     to a callback is valid for that call only, unless its event says otherwise.
//   - Structs grow by appending fields and bumping `struct_size`. A reader checks `struct_size`
//     before touching a field that a smaller version would not carry.

// --- Versioning ----------------------------------------------------------------------------
//
// The contract version has three parts, major.minor.feature, written "0.1.5".
//
// Major changes when an existing field, signature or event id changes meaning. A runtime refuses
// a mod whose major differs: nothing can be salvaged from that.
//
// Minor is a stage of the platform: 0.1 is the first scope. A runtime refuses a mod of a later
// stage than its own. While the major is 0 the stages are not supersets of each other, so a
// different minor is refused either way.
//
// Feature changes when something is appended: a field at the end of a struct, an event, a
// service. Within one major.minor, a runtime accepts a mod that needs a feature lower than or
// equal to its own and refuses a higher one -- the mod would expect what the runtime does not
// have. A manifest's "abi" names the lowest version the mod needs; a mod built against newer
// headers may name an older one and read `wxl_host.abi_feature` to adapt rather than require.
//
// Major 0 means the contract is still moving: any minor may break any other.

#define WXL_ABI_VERSION_MAJOR   0
#define WXL_ABI_VERSION_MINOR   1
#define WXL_ABI_VERSION_FEATURE 43

// --- Platform ------------------------------------------------------------------------------
//
// `WXL_CALL` is cdecl where a choice exists. x86-64 has a single convention, so it expands to
// nothing there; spelling it everywhere keeps one signature for both targets.

#if defined(_WIN32) && !defined(_WIN64)
    #define WXL_CALL __cdecl
#else
    #define WXL_CALL
#endif

#if defined(_WIN32)
    #define WXL_EXPORT __declspec(dllexport)
#else
    #define WXL_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- Types ---------------------------------------------------------------------------------

/// Truth value. `int32_t`, not `bool`: `bool` has no guaranteed representation across compilers.
typedef int32_t wxl_bool;

#define WXL_FALSE 0
#define WXL_TRUE  1

/// How a call went. Anything other than `WXL_OK` leaves out-parameters untouched.
typedef enum wxl_result
{
    WXL_OK            = 0, ///< the call did what it says
    WXL_E_UNSUPPORTED = 1, ///< this runtime does not implement it; a legitimate answer, not a fault
    WXL_E_VERSION     = 2, ///< the versions cannot be reconciled
    WXL_E_INVALID     = 3, ///< an argument is out of range or malformed
    WXL_E_NOT_FOUND   = 4, ///< the name, handle or resource does not resolve
    WXL_E_FAILED      = 5, ///< it should have worked and did not
    /// Not available yet, try again later: what was asked for exists, but is still being made
    /// ready (downloaded, read, computed). Appended in 0.1.19.
    WXL_E_PENDING     = 6
} wxl_result;

/**
 * @brief A UTF-8 string slice.
 *
 * Carries its length, so it can hold text that is not NUL-terminated and can point into a buffer
 * the other side owns. Valid for the duration of the call that produced it unless documented
 * otherwise.
 */
typedef struct wxl_str
{
    const char* data; ///< UTF-8 bytes; may be NULL when size is 0
    uint32_t    size; ///< length in bytes, terminator excluded
} wxl_str;

/**
 * @brief A reference to something the runtime owns.
 *
 * Generational: the low bits index a slot, the high bits count how many times that slot has been
 * reused. A handle kept after its subject is gone resolves to invalid rather than to whatever took
 * its place, so holding one across frames is safe. Zero is never valid.
 */
typedef uint64_t wxl_handle;

#define WXL_HANDLE_NONE ((wxl_handle)0)

/// A world-space position, in the client's own axes and units.
typedef struct wxl_vec3
{
    float x, y, z;
} wxl_vec3;

/// A server object's identity, stable for as long as the object exists on the server.
typedef uint64_t wxl_guid;

#define WXL_GUID_NONE ((wxl_guid)0)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_ABI_H
