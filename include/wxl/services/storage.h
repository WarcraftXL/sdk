// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The storage service: reading the client's files, whatever they are packed in.
//
// Paths are the client's own: backslash-separated, case-insensitive, relative to the data root --
// "World\\Maps\\Azeroth\\Azeroth_32_48.adt". Whether that resolves inside an archive, a patch or a
// loose folder is the runtime's business.

#ifndef WXL_SERVICE_STORAGE_H
#define WXL_SERVICE_STORAGE_H

#include "wxl/abi.h"

#define WXL_STORAGE_SERVICE         "wxl.storage"
#define WXL_STORAGE_SERVICE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

// --- what a mod hands the storage to serve and convert files (0.1.19) -------------------------
//
// The client resolves every file it opens by name -- models, textures, maps, sounds, the
// interface, the client databases -- on its main thread and on its loader threads alike. A
// provider is a source of files the client reads as it reads its archives; a transform rewrites
// a file's bytes between the read and the client's parse. Both are called on whichever thread is
// opening the file, often several at once, so every function below must be safe to call from
// any thread at any time while it is registered. Neither may block for long: the client waits.

/// One entry of a folder a provider lists: a file, or a folder of its own.
typedef void(WXL_CALL* wxl_asset_entry_fn)(void* context, wxl_str name, wxl_bool is_folder);

/**
 * @brief A source of files, asked by name before or after the client's own archives.
 *
 * Names are the client's: backslash-separated, any case ("World\\Maps\\Azeroth\\Azeroth.wdt").
 * The storage hands a provider names with backslashes; comparing them in any case is the
 * provider's part.
 */
typedef struct wxl_asset_provider
{
    uint32_t struct_size;
    void*    user; ///< handed back as the first argument of every function

    /**
     * @brief Whether the provider has a file by this name.
     *
     * WXL_OK: it has it. WXL_E_NOT_FOUND: it does not. WXL_E_PENDING: it will, but not yet (a
     * file still being fetched or made); the name is then looked for further down -- the next
     * providers, the client's own files --, the client sees no such file if none has it, and a
     * mod reading it through wxl.storage is told WXL_E_PENDING. Asked at every open the provider
     * may win and at every existence check, so it must be cheap: a lookup, not a read.
     */
    wxl_result(WXL_CALL* lookup)(void* user, wxl_str path);

    /**
     * @brief Opens a file the provider has, answering its length through @p out_size.
     *
     * Answers a pointer of the provider's own, which comes back in read_at and close, or NULL
     * when it cannot open the file after all (the client is then told there is no such file).
     */
    void*(WXL_CALL* open)(void* user, wxl_str path, uint64_t* out_size);

    /// Reads up to @p size bytes at @p offset into @p buffer, how many through @p out_read:
    /// fewer only at the end of the file. Calls on one file come one at a time.
    wxl_result(WXL_CALL* read_at)(void* user, void* file, uint64_t offset, void* buffer, uint32_t size,
                                  uint32_t* out_read);

    /// The client is done with a file open answered. Called once per file, the last call on it.
    void(WXL_CALL* close)(void* user, void* file);

    /**
     * @brief Optional, may be NULL: tells @p fn each entry of a folder ("Interface\\AddOns",
     *        "" for the top), its name alone ("MyAddOn", "MyAddOn.toc").
     *
     * The client lists some of its folders from the disk -- the add-ons, the macro icons --
     * and the entries listed here join them, so a provider can serve a whole add-on; find
     * walks it too. An entry listed here also exists for the client where the disk has none:
     * asked its information or whether it exists, it answers a folder or a file, as the add-on
     * list asks of every entry it is told.
     */
    void(WXL_CALL* list)(void* user, wxl_str folder, wxl_asset_entry_fn fn, void* context);
} wxl_asset_provider;

/// Where a transform puts the bytes it answers with.
typedef struct wxl_asset_output
{
    uint32_t struct_size;
    void*    context;

    /**
     * @brief A buffer of @p size bytes that replaces the file's bytes once apply returns WXL_OK.
     *
     * Fill it before returning. Asking again replaces the earlier buffer. NULL when that much
     * memory cannot be had, and the file is then left as it was.
     */
    void*(WXL_CALL* replace)(void* context, uint64_t size);
} wxl_asset_output;

/**
 * @brief A conversion of one kind of file, given the bytes as read and before the client parses
 *        them.
 */
typedef struct wxl_asset_transform
{
    uint32_t struct_size;
    void*    user;

    /// The kind: the files whose name ends in this extension, without its dot, in any case
    /// ("m2", "skin", "blp", "wmo", "adt", "dbc").
    wxl_str extension;

    /// Only files whose first four bytes are these, read as a little-endian number ("MD20" is
    /// 0x3032444D); 0 for every file of the kind.
    uint32_t magic;

    /**
     * @brief Looks at a file's bytes and may answer with others through @p out.
     *
     * @p data stays valid until apply returns. Answering nothing (or anything but WXL_OK)
     * leaves the bytes as they came; the next transform of the kind gets what this one left.
     */
    wxl_result(WXL_CALL* apply)(void* user, wxl_str path, const void* data, uint64_t size,
                                const wxl_asset_output* out);
} wxl_asset_transform;

// --- what a mod learns of a file and of what the client holds (0.1.23) --------------------

/// The layer of the client's files a name was found in.
typedef enum wxl_file_layer
{
    WXL_FILE_LAYER_NONE     = 0, ///< found nowhere
    WXL_FILE_LAYER_ARCHIVE  = 1, ///< an archive: an .MPQ file, or a folder mounted as one
    WXL_FILE_LAYER_DISK     = 2, ///< a loose file under the client's folder
    WXL_FILE_LAYER_FOLDER   = 3, ///< a folder of the search path (add_search_path)
    WXL_FILE_LAYER_PROVIDER = 4, ///< a mod's provider (add_provider)
    WXL_FILE_LAYER_SERVED   = 5  ///< bytes a mod serves (serve)
} wxl_file_layer;

/// hash_file: read the file again rather than answer from what an earlier ask kept.
#define WXL_HASH_FRESH 0x1u

/// The content hash of a file, as hash_file answers it.
typedef struct wxl_file_hash
{
    uint32_t struct_size;
    uint32_t layer;   ///< a wxl_file_layer: where the bytes hashed came from
    uint64_t size;    ///< bytes
    uint8_t  md5[16]; ///< the MD5 digest of the bytes, the form a CDN content key takes

    /// Which file of the layer: the archive's path, the file's path on the disk, the id of the
    /// mod whose provider or served bytes answered. Valid until the next hash on the same
    /// thread; for an answer handed to a callback, for that call.
    wxl_str source;
} wxl_file_hash;

/**
 * @brief Where an asynchronous hash_file_async answers, on the client's main thread.
 *
 * @p hash is null unless @p result is WXL_OK; valid for the call.
 */
typedef void(WXL_CALL* wxl_file_hash_fn)(void* user, uint64_t request, wxl_result result, const wxl_file_hash* hash);

/// What reload_asset did.
typedef struct wxl_asset_reload
{
    uint32_t struct_size;
    uint32_t textures; ///< textures the client holds that were read again, in place
    uint32_t models;   ///< model files dropped from the client's model cache, read again at their next use
    uint32_t objects;  ///< units and players whose model was made again from the new file
    uint32_t busy;     ///< textures still loading, left alone: ask again in a moment
} wxl_asset_reload;

typedef struct wxl_storage_api
{
    uint32_t struct_size;

    // --- whole files --------------------------------------------------------------------------

    wxl_bool(WXL_CALL* exists)(wxl_str path);

    /// The file's length, without reading it.
    wxl_result(WXL_CALL* size)(wxl_str path, uint64_t* out);

    /**
     * @brief Reads a whole file into the caller's buffer.
     *
     * The caller allocates and frees: nothing is handed back to free. A buffer too small is
     * WXL_E_INVALID with @p out_read set to what would have been needed, so the usual shape is
     * size() then read().
     */
    wxl_result(WXL_CALL* read)(wxl_str path, void* buffer, uint64_t capacity, uint64_t* out_read);

    // --- streaming ----------------------------------------------------------------------------
    //
    // For a file too big to want in memory at once. A handle must be closed; a runtime closes
    // whatever is left open at shutdown and logs it.

    /// Opens a file, or returns WXL_HANDLE_NONE.
    wxl_handle(WXL_CALL* open)(wxl_str path);

    wxl_result(WXL_CALL* read_at)(wxl_handle file, uint64_t offset, void* buffer, uint64_t size,
                                  uint64_t* out_read);

    void(WXL_CALL* close)(wxl_handle file);

    // --- what gets searched -------------------------------------------------------------------
    //
    // A mod that ships assets makes them findable here rather than by writing into the client's
    // own data folder. Higher priority wins a name collision; the client's own data is 0.

    /**
     * @brief Adds a directory to the search path, for the calling mod.
     *
     * The folder is the mod's: adding it again changes its priority, and it leaves the search
     * when the mod removes it (remove_search_path) or goes -- disabled or unloaded. Another mod
     * may add the same folder; each holds its own.
     */
    wxl_result(WXL_CALL* add_search_path)(wxl_str directory, int32_t priority);

    /// Adds an archive to the search path.
    wxl_result(WXL_CALL* mount_archive)(wxl_str path, int32_t priority);

    /**
     * @brief Lists files matching a pattern.
     *
     * The pattern is a client path with `*` standing for any run of characters. Each match is
     * reported once, from whichever source won it.
     * @param out  receives up to @p capacity paths, each valid until the next storage call; may
     *             be NULL to count only.
     * @return how many matched, which may exceed @p capacity.
     */
    uint32_t(WXL_CALL* find)(wxl_str pattern, wxl_str* out, uint32_t capacity);

    // --- appended in 0.1.10 -----------------------------------------------------------------

    /**
     * @brief Takes a folder the calling mod added back out of the search path.
     *
     * Named as add_search_path was given it, or by any spelling of the same folder. From here on
     * no name is answered from it; a file opened from it already stays open. WXL_E_NOT_FOUND
     * when the mod holds no such folder -- another mod's stays.
     */
    wxl_result(WXL_CALL* remove_search_path)(wxl_str directory);

    // --- appended in 0.1.15 -----------------------------------------------------------------

    /**
     * @brief Opens one name in place of another, for the calling mod, on every thread.
     *
     * From here on, whatever opens @p from -- the client on any of its threads, or a mod --
     * opens @p to instead, found as any name is: in a folder of the search path or in an
     * archive. Names are compared as the client compares them, in any case and with either
     * slash. This is the way to answer an open that happens off the main thread, where a
     * FileOpen handler that runs later cannot answer it.
     *
     * The redirect is the mod's: redirecting the same name again changes where it leads, and
     * it lasts until the mod takes it back (unredirect) or goes -- disabled or unloaded. When
     * two mods redirect one name, the latest wins, and taking it back uncovers the earlier
     * one. A FileOpen handler that answers still has the last word. WXL_E_INVALID for an empty
     * name or one longer than 511 bytes.
     */
    wxl_result(WXL_CALL* redirect)(wxl_str from, wxl_str to);

    /// Takes back the calling mod's redirect of a name: WXL_E_NOT_FOUND when it holds none.
    wxl_result(WXL_CALL* unredirect)(wxl_str from);

    // --- appended in 0.1.19 -----------------------------------------------------------------

    /**
     * @brief Adds a source of files, for the calling mod, at a priority.
     *
     * Every name the client opens or looks for is asked of the providers in priority order.
     * One above 0 is asked before the client's own archives and wins the names it has; one of
     * 0 or below only fills names the client does not have. A folder of the search path at the
     * same priority is asked first. The provider is copied: the struct may go once this
     * returns, what its pointers lead to may not. It is the mod's until remove_provider, or
     * until the mod goes -- disabled or unloaded --, which waits for the calls in progress and
     * closes the files the client still holds open from it.
     *
     * @param out  receives the provider's handle
     * @return WXL_E_INVALID for a missing function or a struct too short.
     */
    wxl_result(WXL_CALL* add_provider)(const wxl_asset_provider* provider, int32_t priority, uint64_t* out);

    /// Takes a provider back, as a mod going does. WXL_E_NOT_FOUND for a handle that is not the
    /// calling mod's; WXL_E_INVALID from inside one of the provider's own functions.
    wxl_result(WXL_CALL* remove_provider)(uint64_t provider);

    /**
     * @brief Adds a conversion of one kind of file, for the calling mod, at a priority.
     *
     * Transforms of a kind run in priority order, highest first, each given what the one before
     * left; ModelLoad then sees the bytes they made. Copied as add_provider copies; the mod's
     * until remove_transform or until it goes.
     *
     * @return WXL_E_INVALID for a missing apply, an empty extension or one longer than 15 bytes.
     */
    wxl_result(WXL_CALL* add_transform)(const wxl_asset_transform* transform, int32_t priority, uint64_t* out);

    /// Takes a transform back. WXL_E_NOT_FOUND for a handle that is not the calling mod's.
    wxl_result(WXL_CALL* remove_transform)(uint64_t transform);

    /**
     * @brief Serves bytes under a name, for the calling mod, on every thread: a file that needs
     *        no provider.
     *
     * The bytes are copied. Priority as add_provider's. Serving a name again replaces the
     * mod's bytes; when two mods serve one name, the higher priority wins, then the latest.
     * WXL_E_INVALID for an empty name or one longer than 240 bytes.
     */
    wxl_result(WXL_CALL* serve)(wxl_str path, const void* data, uint32_t size, int32_t priority);

    /// Takes back what the calling mod serves under a name: WXL_E_NOT_FOUND when it serves none.
    wxl_result(WXL_CALL* unserve)(wxl_str path);

    // --- appended in 0.1.23 -----------------------------------------------------------------

    /**
     * @brief The content hash of a file as the client would read it: its MD5 and its length,
     *        and the layer that holds it.
     *
     * The name is resolved as the client resolves it -- a mod's override and redirect, the folders
     * of the search path, the providers, the bytes mods serve, the archives, the disk -- and the bytes of the layer that wins are hashed, before any transform: the bytes
     * a CDN's content key is the MD5 of. The digest is computed by the system's own MD5
     * (Windows CNG, BCrypt), and kept by name and layer, so a second ask of a file that did not
     * change answers without reading it; WXL_HASH_FRESH in @p flags reads it again. Any thread;
     * reading a large file takes as long as reading it, so hash_file_async is the call for the
     * main thread. WXL_E_NOT_FOUND when no layer has the name, WXL_E_PENDING when a provider
     * answered "not yet".
     */
    wxl_result(WXL_CALL* hash_file)(wxl_str path, uint32_t flags, wxl_file_hash* out);

    /**
     * @brief hash_file on a thread of the runtime's: the answer comes later, to @p fn, on the
     *        client's main thread.
     *
     * @param out  receives the request's number, which the answer carries. A request of a mod
     *             that goes -- disabled or unloaded -- is dropped and never answered.
     */
    wxl_result(WXL_CALL* hash_file_async)(wxl_str path, uint32_t flags, wxl_file_hash_fn fn, void* user,
                                          uint64_t* out);

    /**
     * @brief Replaces a file of the client with another, for the calling mod: the explicit
     *        override.
     *
     * From here on, whatever opens @p path or asks whether it exists -- the client on any of its
     * threads, or a mod -- gets @p with instead, above every layer: a name of the mod's own
     * that a provider or served bytes answer, typically, or any other name. A source of 0
     * priority and below only fills names the client lacks, so this is the way to put one file
     * of the mod's in place of one the client has without taking every name the source holds.
     * Logged. The override is the mod's until restore_file or until the mod goes --
     * disabled or unloaded --; when two mods override one name the latest wins and taking it back
     * uncovers the earlier. What the client already loaded stays as it is: reload_asset makes it
     * read the file again. Applied before redirect; a FileOpen handler still has the last word.
     * WXL_E_INVALID for an empty name or one longer than 259 bytes.
     */
    wxl_result(WXL_CALL* override_file)(wxl_str path, wxl_str with);

    /// Takes the calling mod's override of a name back: WXL_E_NOT_FOUND when it holds none.
    wxl_result(WXL_CALL* restore_file)(wxl_str path);

    /**
     * @brief Makes the client read again a model or a texture it already holds, so that a file
     *        that changed (a download that arrived, an override set) shows without a restart.
     *
     * A texture (.blp) is read again in place: every model, map and frame drawing it shows the
     * new pixels. A model (.m2, .mdx, its .skin) is dropped from the client's model cache, so its
     * next use reads the new file, and every unit and player in view drawn with it is made again
     * at once. The client keeps models by file name without the folder, so every cached model of
     * that file name is dropped. Other files are read again when the client next loads them --
     * map tiles and buildings as the area loads again, sounds as they play -- and answer
     * WXL_E_UNSUPPORTED here; nor is a character's baked skin painted again from the texture of
     * a part (it is, at its next change of appearance), nor a texture of the client's atlas.
     * Main thread only (WXL_E_INVALID elsewhere). WXL_E_NOT_FOUND when the client holds nothing
     * of that name; WXL_E_PENDING when all it holds is still loading (ask again in a moment);
     * WXL_E_UNSUPPORTED too when all it holds of a texture are blocks of the interface's atlas,
     * where the client keeps the square interface textures of 64 pixels or less it shares.
     */
    wxl_result(WXL_CALL* reload_asset)(wxl_str path, wxl_asset_reload* out);
} wxl_storage_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_STORAGE_H
