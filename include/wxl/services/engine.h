// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The engine service: the client's own structures as this runtime lays them out.
//
// The SDK publishes the engine's structures as C++ headers (sdk/engine, wxl/engine/*.hpp), one
// layout for the stock client (x86) and one for the recompiled client (x64), each type with a
// layout hash: XXH64 of its members' offsets, kinds and sizes. A mod built against those headers
// asks the runtime it runs on for the hash of each type it reads; a different answer means the
// runtime lays the type out differently (a client built from a later fork) and the mod's reads
// would land on the wrong bytes. wxl/engine/check.hpp does the asking and stops the mod with a
// sentence naming the types.

#ifndef WXL_SERVICE_ENGINE_H
#define WXL_SERVICE_ENGINE_H

#include "wxl/abi.h"

#define WXL_ENGINE_SERVICE         "wxl.engine"
#define WXL_ENGINE_SERVICE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wxl_engine_api
{
    uint32_t struct_size;

    /**
     * @brief The layout hash of an engine type as this runtime lays it out.
     *
     * @param name      the type as the fork spells it (`CM2Shared`, `CGxDevice::TextureTarget`),
     *                  its `kName`; the engine headers' C++ name (`CGxDevice_TextureTarget`) is
     *                  accepted too.
     * @param out_hash  receives the hash.
     * @return WXL_OK, WXL_E_NOT_FOUND for a type this runtime does not describe, WXL_E_INVALID
     *         for a null argument.
     */
    wxl_result(WXL_CALL* layout)(const char* name, uint64_t* out_hash);
} wxl_engine_api;

#ifdef __cplusplus
}
#endif

#endif
