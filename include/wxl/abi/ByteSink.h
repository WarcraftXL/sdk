// SPDX-License-Identifier: GPL-3.0-or-later
// WXL_ByteSink: how a service hands variable-length bytes across the C ABI.

#ifndef WXL_BYTE_SINK_H
#define WXL_BYTE_SINK_H

#ifdef __cplusplus
extern "C" {
#endif

/// A growable sink the callee writes its output bytes into, in order.
typedef struct WXL_ByteSink
{
    void* ctx;
    void(__cdecl* Write)(void* ctx, const void* data, unsigned int len);
} WXL_ByteSink;

#ifdef __cplusplus
}
#endif

#endif // WXL_BYTE_SINK_H
