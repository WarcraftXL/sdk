// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The script service: adding to the client's own scripting surface.
//
// The VM's state never crosses. A registered function receives a call handle and reads its
// arguments through it, so the same mod registers a function on a runtime that embeds Lua 5.1 and
// on one that embeds something else. The native state is still reachable, through the
// wxl_script_vm_args of the ScriptVmReady event, for a mod that accepts being tied to one VM.
//
// Registrations do not survive a context: both the glue and the game context are created and
// destroyed more than once in a session, so a mod registers again on every ScriptVmReady (one per
// interface that comes up; a context the client throws away before any interface is never
// announced, and takes nothing). The
// state is closed with the context, which takes every global and every listener with it; the
// runtime forgets what it was told at ScriptVmClosing, and until the next ScriptVmReady every
// call below that needs a context answers WXL_E_FAILED. Nothing kept from one context -- a
// wxl_script_call, a string an argument pointed into, an index -- means anything in the next.

#ifndef WXL_SERVICE_SCRIPT_H
#define WXL_SERVICE_SCRIPT_H

#include "wxl/abi.h"
#include "wxl/events.h"

#define WXL_SCRIPT_SERVICE         "wxl.script"
#define WXL_SCRIPT_SERVICE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

/// What a script value can be. Anything else arrives as WXL_VALUE_NIL.
typedef enum wxl_value_type
{
    WXL_VALUE_NIL    = 0,
    WXL_VALUE_NUMBER = 1,
    WXL_VALUE_STRING = 2,
    WXL_VALUE_BOOL   = 3
} wxl_value_type;

/// A value going to the script side.
typedef struct wxl_value
{
    uint32_t type; ///< a wxl_value_type value
    union
    {
        double   number;
        wxl_str  string;
        wxl_bool boolean;
    } as;
} wxl_value;

/// One invocation of a registered function. Valid for that call only.
typedef struct wxl_script_call wxl_script_call;

/**
 * @brief A function the script side can call.
 * @param call  reads the arguments and receives the return values.
 * @param user  the pointer given to register_function.
 */
typedef void(WXL_CALL* wxl_script_fn)(wxl_script_call* call, void* user);

typedef struct wxl_script_api
{
    uint32_t struct_size;

    /// Which context is live: a wxl_script_context value.
    uint32_t(WXL_CALL* context)(void);

    // --- registering ------------------------------------------------------------------------

    /**
     * @brief Makes a function callable from script, under a global name.
     *
     * Only valid while a context exists, so call it from ScriptVmReady; WXL_E_FAILED otherwise.
     * A name already taken is refused with WXL_E_INVALID rather than replaced: shadowing a
     * client function silently is never what was meant. The function is called on the client's
     * main thread, from inside the script engine; a fault in it is caught, reported to the
     * script as an error, and disables the mod.
     */
    wxl_result(WXL_CALL* register_function)(const char* name, wxl_script_fn fn, void* user);

    /**
     * @brief Declares an event name the script side can register frames for.
     *
     * Declaring is separate from raising so that a frame can listen before anything raises it.
     * Only valid while a context exists (WXL_E_FAILED otherwise); declaring a name twice is fine.
     */
    wxl_result(WXL_CALL* register_event)(const char* name);

    /// Raises a declared event, with its payload: WXL_E_NOT_FOUND for a name nobody declared on
    /// this context, WXL_E_FAILED while no context exists or when the client faults inside a
    /// frame's handler (see run).
    wxl_result(WXL_CALL* fire_event)(const char* name, const wxl_value* args, uint32_t count);

    /**
     * @brief Runs a chunk of script text in the live context.
     *
     * A Lua error in the chunk goes to the interface's error handler, as the client's own scripts'
     * do, and the call still answers WXL_OK. A hardware fault inside it (a function of the client
     * called before what it reads exists, such as GetBindingKey before the key bindings are loaded
     * after a /reload) abandons the chunk the way a Lua error would: the script state is put back
     * as it was before the call, the runtime logs the fault, and the call answers WXL_E_FAILED.
     * @param name  what to call it in an error message.
     */
    wxl_result(WXL_CALL* run)(wxl_str chunk, const char* name);

    // --- inside a call ------------------------------------------------------------------------
    //
    // Every accessor below is valid only inside a wxl_script_fn, and answers WXL_FALSE anywhere
    // else. An argument of the wrong type is a miss, not a conversion: a mod that wants leniency
    // asks for what it will accept.

    uint32_t(WXL_CALL* arg_count)(wxl_script_call* call);
    wxl_bool(WXL_CALL* arg_number)(wxl_script_call* call, uint32_t index, double* out);
    wxl_bool(WXL_CALL* arg_string)(wxl_script_call* call, uint32_t index, wxl_str* out);
    wxl_bool(WXL_CALL* arg_bool)(wxl_script_call* call, uint32_t index, wxl_bool* out);

    /// The type of an argument, for a function that branches on it.
    uint32_t(WXL_CALL* arg_type)(wxl_script_call* call, uint32_t index);

    /// Return values, in order. A function that pushes nothing returns nothing.
    void(WXL_CALL* push_number)(wxl_script_call* call, double value);
    void(WXL_CALL* push_string)(wxl_script_call* call, wxl_str value);
    void(WXL_CALL* push_bool)(wxl_script_call* call, wxl_bool value);
    void(WXL_CALL* push_nil)(wxl_script_call* call);

    /**
     * @brief Fails the call with a message, as the script side's own error would.
     *
     * The function returns normally afterwards; the error is raised on the script side once the
     * call unwinds, so no exception crosses the boundary.
     */
    void(WXL_CALL* fail)(wxl_script_call* call, wxl_str message);

    // --- appended in 0.1.15 -----------------------------------------------------------------

    /**
     * @brief Whether a context is live, the one ScriptVmReady announced last.
     *
     * WXL_FALSE before the first ScriptVmReady and from ScriptVmClosing to the next, when
     * every call above that needs a context answers WXL_E_FAILED. context() cannot tell: it
     * keeps answering the last context's value.
     */
    wxl_bool(WXL_CALL* live)(void);
} wxl_script_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_SCRIPT_H
