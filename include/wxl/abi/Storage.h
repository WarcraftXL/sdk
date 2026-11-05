// SPDX-License-Identifier: GPL-3.0-or-later
// The wxl.storage service: serve, reshape or redirect the files the client opens.

#ifndef WXL_STORAGE_API_H
#define WXL_STORAGE_API_H

#include <stdint.h>

#include "wxl/abi/ByteSink.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WXL_STORAGE_API_VERSION 1

/// Supplies the whole file `name` (as the client spelled it) into `sink`; nonzero to claim it.
typedef int(__cdecl* WXL_ProvideFn)(const char* name, const WXL_ByteSink* sink);

/// Reshapes the bytes an archive read produced for `name` into `sink`; nonzero to claim them.
typedef int(__cdecl* WXL_TransformFn)(const char* name, const uint8_t* raw, uint32_t rawLen, const WXL_ByteSink* sink);

/// Answers `name` with another file name written to `out` (outCap bytes, terminator included); nonzero to claim.
typedef int(__cdecl* WXL_RedirectFn)(const char* name, char* out, uint32_t outCap);

typedef struct WXL_StorageApi
{
    uint32_t structSize;
    uint32_t apiVersion;

    /// Adds a provider, asked before any archive read; in registration order, the first claim wins.
    void(__cdecl* RegisterClientProvider)(WXL_ProvideFn fn);

    /// Adds a transform offered the bytes of every name ending in `suffix` (lowercase, e.g. ".blp";
    /// case-insensitive match), after the archive read.
    void(__cdecl* RegisterClientTransform)(const char* suffix, WXL_TransformFn fn);

    /// Adds a name redirect, asked before providers; in registration order, the first claim wins. It
    /// must decline the names it produces itself.
    void(__cdecl* RegisterClientRedirect)(WXL_RedirectFn fn);
} WXL_StorageApi;

#ifdef __cplusplus
}
#endif

#endif // WXL_STORAGE_API_H
