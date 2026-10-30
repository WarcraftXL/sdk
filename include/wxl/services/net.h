// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The net service: messages of the mod's own on the world connection.
//
// A mod claims opcode numbers the client does not use and talks to a server module through them,
// one opcode per action, as the game does: it is handed the messages of its numbers that the
// server sends, and it sends its own. Nothing is wrapped: what a packet log shows is what happens.
//
//   claim_opcode(op)              the number is the mod's until it gives it back or goes
//   set_handler(op, fn, user)     a message of that number from the server arrives at fn, on the
//                                 main thread, and goes no further
//   set_compressed(op, zop)       a message of zop is a u32 size and a zlib stream, as the
//                                 client's own compressed opcodes are: the runtime inflates it and
//                                 hands the result to op's handler as an op message
//   send(op, data, size)          a message to the server, framed and encrypted by the client
//
// Numbers. The client itself uses everything up to 0x51E, and servers of its family 0x51F and
// 0x520 (a redirect): reserved_opcode_limit() answers 0x520, and a mod's own numbers start at
// 0x521. A number up to the limit is the client's; claiming one needs `"net": { "stock": true }`
// in the mod's mod.json, for tools. A number another mod holds or declares is refused, and the
// log names that mod. A mod lists its numbers in mod.json (`"net": { "opcodes": ["0x521-0x52A"]
// }`): a mod that declares numbers claims only those, and of two mods declaring the same number
// the second does not load.
//
// The send gate. A world server closes the connection on a number it has no handler for, so a
// mod may send its own numbers only once the server has shown it knows them: send is refused
// until a message of one of the numbers the mod claimed has arrived from the server on this
// connection. The server's module announces itself with its first message. The gate closes again
// when the connection goes, and stays closed on the next one until the server speaks again.
//
// On the wire. Server to client: a header of 2 or 3 bytes of size (big-endian, the high bit of
// the first byte marking 3) and the opcode as a u16, little-endian; the client decrypts the size
// and the opcode, never the body. Client to server: 2 bytes of size (big-endian) and the opcode
// as a u32, little-endian, encrypted the same way; the server takes at most 10239 bytes after the
// size, so the body stops at max_send(). The body is the mod's, byte for byte: numbers in it are
// little-endian by convention, as the game's are.
//
// Receiving does not replace observing: Net/MessageReceived still sees every message first,
// claimed or not, and a handler there that suppresses a message stops it before the mod's
// handler.

#ifndef WXL_SERVICE_NET_H
#define WXL_SERVICE_NET_H

#include "wxl/abi.h"

#define WXL_NET_SERVICE         "wxl.net"
#define WXL_NET_SERVICE_VERSION 2

/// The highest opcode the client and its servers use; a mod's own numbers start above it.
#define WXL_NET_STOCK_LIMIT 0x520u

/// The highest opcode a message carries: the server writes it in 16 bits.
#define WXL_NET_OPCODE_MAX 0xFFFFu

/// The largest body send takes: under the server's 10235, with room for its header.
#define WXL_NET_MAX_SEND 10000u

/// The largest body a compressed message inflates to.
#define WXL_NET_MAX_INFLATED (8u * 1024u * 1024u)

/// How many messages of one number a mod may send in a second; send answers WXL_E_BUSY past it.
#define WXL_NET_SEND_RATE 64u

#ifdef __cplusplus
extern "C" {
#endif

/// Where the client is in its conversation with the world server.
typedef enum wxl_net_state
{
    WXL_NET_OFFLINE       = 0, ///< nothing open
    WXL_NET_CONNECTING    = 1,
    WXL_NET_CONNECTED     = 2, ///< the socket is up, the session not yet accepted
    WXL_NET_AUTHENTICATED = 3  ///< the server accepted the session: messages flow
} wxl_net_state;

/**
 * @brief A message of a claimed number, from the server, on the main thread.
 *
 * @p data and @p size are the body, without the opcode: valid for the call. @p opcode is the
 * number the handler was set for, also when the message came compressed under its other number.
 */
typedef void(WXL_CALL* wxl_net_handler_fn)(void* user, uint32_t opcode, const void* data, uint32_t size);

typedef struct wxl_net_api
{
    uint32_t struct_size;

    /// A wxl_net_state value.
    uint32_t(WXL_CALL* state)(void);

    /**
     * @brief Sends a message of a number the calling mod claimed to the server.
     *
     * @p data is the body, without the opcode; the runtime frames it. Main thread.
     *
     * @return WXL_E_INVALID for a number the mod has not claimed, above WXL_NET_OPCODE_MAX, or a
     *         body over max_send(); WXL_E_FAILED while not authenticated, or while the server
     *         has sent none of the mod's numbers on this connection (the send gate); WXL_E_BUSY
     *         past WXL_NET_SEND_RATE messages of the number in a second; WXL_E_UNSUPPORTED off
     *         the main thread.
     */
    wxl_result(WXL_CALL* send)(uint32_t opcode, const void* data, uint32_t size);

    /**
     * @brief Claims a number for the calling mod, for the server's messages and its own.
     *
     * A claimed number reaches Net/MessageReceived and then stops: the client never sees it. The
     * mod's handler, once set, is handed it. WXL_E_INVALID for a number above
     * WXL_NET_OPCODE_MAX; one up to reserved_opcode_limit() without `"stock": true` in the mod's
     * mod.json; one another mod claimed or declared (the log names it); one outside the numbers
     * the mod's own mod.json declares, when it declares some. Claiming its own number again is
     * WXL_OK.
     */
    wxl_result(WXL_CALL* claim_opcode)(uint32_t opcode);

    /// Gives back a claimed number, its handler and its compressed number with it.
    /// WXL_E_NOT_FOUND for one the calling mod does not hold. Everything goes when the mod goes.
    wxl_result(WXL_CALL* release_opcode)(uint32_t opcode);

    /// The highest number the client and its servers use, WXL_NET_STOCK_LIMIT.
    uint32_t(WXL_CALL* reserved_opcode_limit)(void);

    // --- abi 0.1.41 ---------------------------------------------------------------------------

    /**
     * @brief Sets the function a claimed number's messages are handed to; NULL takes it away
     *        (the messages still stop).
     *
     * WXL_E_INVALID for a number the calling mod has not claimed.
     */
    wxl_result(WXL_CALL* set_handler)(uint32_t opcode, wxl_net_handler_fn handler, void* user);

    /**
     * @brief Makes @p compressed the compressed form of @p opcode: a message of it is a u32 size
     *        (little-endian) and a zlib stream of that many bytes, inflated by the runtime and
     *        handed to @p opcode's handler.
     *
     * Both numbers must be the calling mod's. 0 for @p compressed takes the pairing back. A
     * stream that does not inflate to its size, or a size over WXL_NET_MAX_INFLATED, is dropped
     * with one log line per number. WXL_E_INVALID otherwise.
     */
    wxl_result(WXL_CALL* set_compressed)(uint32_t opcode, uint32_t compressed);

    /// The largest body send takes, WXL_NET_MAX_SEND.
    uint32_t(WXL_CALL* max_send)(void);

    /// The world connection, counted: 0 while there is none, a new number each time one comes up.
    uint32_t(WXL_CALL* connection)(void);

    /// Whether the calling mod may send on this connection: the server has sent it one of its
    /// numbers since it came up (the send gate).
    wxl_bool(WXL_CALL* heard)(void);

    /**
     * @brief Compresses bytes into a zlib stream.
     *
     * @param out_size  receives the stream's size; with a buffer too small, WXL_E_INVALID and the
     *                  size a buffer must have.
     */
    wxl_result(WXL_CALL* deflate)(const void* data, uint32_t size, void* buffer, uint64_t capacity,
                                  uint64_t* out_size);

    /**
     * @brief Inflates a zlib stream.
     *
     * @param out_size  receives the inflated size; with a buffer too small, WXL_E_INVALID and the
     *                  size a buffer must have. WXL_E_INVALID with 0 for a stream that is not
     *                  zlib, is cut short or inflates past WXL_NET_MAX_INFLATED.
     */
    wxl_result(WXL_CALL* inflate)(const void* data, uint32_t size, void* buffer, uint64_t capacity,
                                  uint64_t* out_size);
} wxl_net_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_NET_H
