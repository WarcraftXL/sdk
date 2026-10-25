// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The relay service: one mod asks another for something and is answered later.
//
// A mod publishes a service under a name; another mod calls it with a method and arguments and
// gets a request number at once; the publisher answers that request when it can -- at once, a
// frame later, or from a thread of its own once a download finished -- and the caller is handed
// the answer. Both sides may be native or Lua mods. Every call into a mod -- a request, a
// cancellation, an answer -- comes on the client's main thread, at the runtime's next update
// after the event that causes it, never from inside the call that causes it.
//
// Either side may go -- disabled, unloaded, a Lua mod reloaded -- with requests in flight. The
// publisher going fails each request it had not answered (WXL_E_NOT_FOUND, "gone"); the caller
// going cancels its own (the publisher is told, and its answers to them are refused); nothing is
// ever handed to a mod that went.

#ifndef WXL_SERVICE_RELAY_H
#define WXL_SERVICE_RELAY_H

#include "wxl/abi.h"
#include "wxl/services/script.h"

#define WXL_RELAY_SERVICE         "wxl.relay"
#define WXL_RELAY_SERVICE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

/// A request's number, unique for the process; 0 is none.
typedef uint64_t wxl_request;

/**
 * @brief A request the published service receives, on the main thread.
 *
 * @p method and @p args (numbers, strings, booleans, nils) are valid for the call: what is kept
 * is copied. The service answers through answer or fail, now or later, once.
 */
typedef void(WXL_CALL* wxl_relay_request_fn)(void* user, wxl_request request, wxl_str method,
                                             const wxl_value* args, uint32_t count);

/// A request no answer is wanted for any more: its caller cancelled it or went. Main thread.
typedef void(WXL_CALL* wxl_relay_cancel_fn)(void* user, wxl_request request);

/**
 * @brief What a caller is told of its request, once, on the main thread.
 *
 * WXL_OK with the values answer gave; WXL_E_FAILED with one string, the reason fail gave;
 * WXL_E_NOT_FOUND with none when the service went (or was never there) before answering.
 */
typedef void(WXL_CALL* wxl_relay_reply_fn)(void* user, wxl_request request, wxl_result result,
                                           const wxl_value* values, uint32_t count);

/// What a mod publishes.
typedef struct wxl_relay_endpoint
{
    uint32_t             struct_size;
    void*                user;      ///< handed back as the first argument of both functions
    wxl_relay_request_fn request;
    wxl_relay_cancel_fn  cancelled; ///< optional, may be NULL
} wxl_relay_endpoint;

typedef struct wxl_relay_api
{
    uint32_t struct_size;

    /**
     * @brief Publishes a service under a name, for the calling mod.
     *
     * Names are the publisher's to choose, compared in any case; its mod id first is the
     * custom ("wxl_casc.fetch"). Copied: the struct may go once this returns. The service is
     * the mod's until unpublish or until the mod goes. WXL_E_INVALID for an empty name, one
     * longer than 127 bytes, a missing request function, or a name another mod publishes.
     */
    wxl_result(WXL_CALL* publish)(wxl_str name, const wxl_relay_endpoint* endpoint);

    /// Takes the calling mod's service back; its unanswered requests fail with WXL_E_NOT_FOUND.
    wxl_result(WXL_CALL* unpublish)(wxl_str name);

    /// Whether some mod publishes a service under the name.
    wxl_bool(WXL_CALL* published)(wxl_str name);

    /**
     * @brief Asks a service, for the calling mod: @p reply is told the answer later.
     *
     * Any thread. The arguments are copied. A name nobody publishes is still a request: it
     * fails at the next update with WXL_E_NOT_FOUND, the way every answer comes.
     *
     * @param out  receives the request's number.
     * @return WXL_E_INVALID for an empty name or a missing reply.
     */
    wxl_result(WXL_CALL* call)(wxl_str name, wxl_str method, const wxl_value* args, uint32_t count,
                               wxl_relay_reply_fn reply, void* user, wxl_request* out);

    /**
     * @brief Answers a request the calling mod's service received, with values.
     *
     * Any thread, once per request; the values are copied. WXL_E_NOT_FOUND for a request that
     * is not waiting for this mod's answer -- answered already, cancelled, its caller gone.
     */
    wxl_result(WXL_CALL* answer)(wxl_request request, const wxl_value* values, uint32_t count);

    /// Fails a request the calling mod's service received, with a reason: the caller gets
    /// WXL_E_FAILED and the reason. As answer otherwise.
    wxl_result(WXL_CALL* fail)(wxl_request request, wxl_str reason);

    /**
     * @brief Takes back a request the calling mod made: it is never answered, and the service
     *        is told. WXL_E_NOT_FOUND for one that is not the mod's or was answered already.
     */
    wxl_result(WXL_CALL* cancel)(wxl_request request);
} wxl_relay_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_RELAY_H
