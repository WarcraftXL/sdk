// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The model-file service: an M2 file as the client reads it, at the points its loader passes
// through, for a mod that teaches the client a model format it was not written for.
//
// Not portable. It is published only by a runtime that sees the client's model loader from the
// inside -- the recompiled client -- and a mod that needs it requires it in its manifest. Bytes
// are raw: the model is the client's own M2Data, the skin profile its own M2SkinProfile, laid out
// as the file is (an array is a 32-bit count and a 32-bit offset). What the offsets mean depends
// on the stage, and is written beside each stage below.
//
// Calls are made on the client's main thread, from the client's file completion callbacks.

#ifndef WXL_SERVICE_M2FILE_H
#define WXL_SERVICE_M2FILE_H

#include "wxl/abi.h"

#define WXL_M2FILE_SERVICE         "wxl.m2file"
#define WXL_M2FILE_SERVICE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

/// Where in the loader a wxl_m2file was taken.
typedef enum wxl_m2file_stage
{
    /**
     * The model file has been read and nothing has looked at it. `data` holds `size` bytes,
     * offsets are from `data`. A watcher may rewrite the bytes in place and lower `size` (never
     * raise it: the allocation is the client's); the client then parses what is left as a
     * version 264 'MD20' model. Answering false refuses the file: the client reports it corrupt
     * and the model never loads.
     */
    WXL_M2FILE_READ = 1,

    /**
     * The model has been parsed: every array of the header now holds its offset relative to the
     * array itself (data = (uint8_t*)&array + (int32_t)array.offset), as the client's
     * M2Array::Data reads it. Nothing has been made from it yet: no skin profile chosen, no
     * texture created. An array pointed elsewhere must stay within reach of a 32-bit offset of
     * the array: alloc_near is memory that is.
     */
    WXL_M2FILE_PARSED = 2,

    /**
     * The skin profile has been read and parsed (its arrays self-relative, as above) and the
     * client is about to size its draw state from it: batches, submeshes and the model's lookups
     * may still be replaced. `skin` is the profile's header.
     */
    WXL_M2FILE_SKIN = 3,

    /**
     * An external sequence file (.anim) of the model has been read and nothing has looked at it.
     * `sequence` holds `sequence_size` bytes and is what the sequence's offsets are relative to;
     * a watcher may rewrite them in place. The same buffer is handed once for every sequence that
     * shares it.
     */
    WXL_M2FILE_SEQUENCE = 4,
} wxl_m2file_stage;

/// One model file at one stage of its load.
typedef struct wxl_m2file
{
    uint32_t    struct_size;
    uint32_t    stage;          ///< a wxl_m2file_stage
    void*       file;           ///< the client's CM2Shared (opaque); what wxl.m2's models are made from
    const char* name;           ///< the path the client opened it by, with its extension
    void*       data;           ///< the model's bytes: its header
    uint32_t    size;           ///< how many; WXL_M2FILE_READ may lower it
    void*       skin;           ///< WXL_M2FILE_SKIN: the skin profile's header, else null
    void*       sequence;       ///< WXL_M2FILE_SEQUENCE: the external sequence's bytes, else null
    uint32_t    sequence_size;
    uint32_t    sequence_index; ///< WXL_M2FILE_SEQUENCE: which of the model's sequences it is
} wxl_m2file;

/**
 * @brief Called for every model file at every stage, until unwatched. Answers whether the file
 *        may load: false is honoured at WXL_M2FILE_READ and ignored after it. A watcher returns
 *        true at once for a file it does not want.
 */
typedef wxl_bool(WXL_CALL* wxl_m2file_fn)(wxl_m2file* file, void* user);

typedef struct wxl_m2file_api
{
    uint32_t struct_size;

    /// Calls @p fn for every model file the client loads. Runs under the crash guard; a watcher
    /// that faults disables its mod, and the file it faulted on is refused.
    wxl_result(WXL_CALL* watch_files)(wxl_m2file_fn fn, void* user, uint32_t* out_token);
    void(WXL_CALL* unwatch_files)(uint32_t token);

    /**
     * @brief @p size zeroed bytes, 16-aligned, that an array lying at @p anchor can point at with
     *        its 32-bit self-relative offset. They live for the session: a model's own arrays are
     *        never freed one by one, and neither is this. Null when no memory is left in reach.
     */
    void*(WXL_CALL* alloc_near)(const void* anchor, uint32_t size);

    /**
     * @brief Has the client animate every bone of @p file's models when it draws their shadows.
     *
     * The client skips the bone pass of a model none of whose bones carries one of the flags it
     * counts at load (billboards, a transform), and draws the shadow from the model's transform
     * alone. A file whose bones are animated but carry none of those flags then casts a shadow
     * that does not move with it. From WXL_M2FILE_PARSED on.
     */
    void(WXL_CALL* animate_bones)(void* file);
} wxl_m2file_api;

#ifdef __cplusplus
}
#endif

#endif
