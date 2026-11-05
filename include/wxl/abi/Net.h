// SPDX-License-Identifier: GPL-3.0-or-later
// The wxl.net service: server messages past the client's handler table, and client messages of any opcode.

#ifndef WXL_NET_API_H
#define WXL_NET_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WXL_NET_API_NAME    "wxl.net"
#define WXL_NET_API_VERSION 1

/// The highest opcode a server message header carries (two bytes).
#define WXL_NET_MAX_SERVER_OPCODE 0xFFFF

/// A server message: `data` is its body after the opcode, `size` bytes, valid during the call. Runs on the
/// main thread, inside the client's message dispatch; `timeReceived` is the client's receive time stamp.
typedef void(__cdecl* WXL_NetHandler)(void* user, uint32_t opcode, uint32_t timeReceived, const uint8_t* data,
                                      uint32_t size);

typedef struct WXL_NetApi
{
    uint32_t structSize;
    uint32_t apiVersion;

    /// The first server opcode past the client's handler table: the bound its dispatch checks (0x51F in
    /// 12340). The stock client drops every message from there on, unread.
    uint32_t(__cdecl* FirstOpcode)(void);

    /// Gives the calling extension the server messages of `opcode`, FirstOpcode() to
    /// WXL_NET_MAX_SERVER_OPCODE; taken back when it is disabled. WXL_OK, WXL_E_INVALID_ARGUMENT (no
    /// handler, or an opcode out of that range), WXL_E_DENIED (another extension holds it). Again: replaces
    /// the caller's own.
    int(__cdecl* SetHandler)(uint32_t opcode, WXL_NetHandler handler, void* user);

    /// Gives `opcode` back. WXL_OK, WXL_E_NOT_FOUND when the caller holds no handler for it.
    int(__cdecl* ClearHandler)(uint32_t opcode);

    /// Nonzero while the client is connected to a world server (from the character list on).
    int(__cdecl* Connected)(void);

    /// Sends a client message: `opcode` (four bytes) then `size` bytes of `data`, through the client's own
    /// send. Main thread only. WXL_OK, WXL_E_INVALID_ARGUMENT (data null with a size), WXL_E_NOT_FOUND (no
    /// world server connection).
    int(__cdecl* Send)(uint32_t opcode, const void* data, uint32_t size);
} WXL_NetApi;

#ifdef __cplusplus
}
#endif

#endif /* WXL_NET_API_H */
