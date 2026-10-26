// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The texture service: textures made from an SVG or a PNG, which the client loads as its own files.
//
// The client's interface and its engine read textures as BLP (or TGA) only. This service takes an
// SVG -- drawn at the size asked, so it is sharp at any size -- or a PNG, makes a texture of it and
// gives it a name of its own, a path under "Interface\\WXL\\". Whatever loads a texture by name then
// loads it: Texture:SetTexture(path) in an add-on, a model's texture, a mod's. The name says what
// the texture was made from: the same source file, size, colours and widths give the same name,
// and the texture is made once; a change to any of them, the source file's bytes included, gives a
// new one. The game's own interface reaches the service by the function WxlTexture, in both its
// script contexts (the login screens' and the game's).
//
// --- What a texture is made from (`source`) -----------------------------------------------------
//
//   "Mods\\<folder>\\<path>"   a file of a mod's folder: the folder's name under the client's
//                              Mods folder, then the file's path inside it
//   ".\\<path>"                a file of the calling mod's own folder (a mod's call, not an add-on's)
//   any other name             a file the client has under that name, found as the client finds
//                              its files: "Interface\\AddOns\\<add-on>\\<path>" for a file of an
//                              add-on, or one in an archive, or one a mod serves
//
// Either slash; any case outside a mod's folder. A name may not go up a folder ("..") or be a path
// of the disk ("C:..."). The kind is read from the file's bytes, not its name.
//
// --- How it is drawn ---------------------------------------------------------------------------
//
// An SVG is drawn at `width` x `height` pixels (0 for either: the size the SVG declares), anti-
// aliased, its proportions kept: fitted and centred, as an SVG's default preserveAspectRatio. A
// PNG is resampled to the size asked (0: its own) with a filter that averages what it shrinks.
//
// The texture the client makes has power-of-two edges of at least 8 pixels: its device makes no
// other. The picture sits at the top-left corner of a texture large enough for it, the rest
// transparent, so that it is drawn pixel for pixel. Show only the picture with its texture
// coordinates: `right` and `bottom` of the answer, in an add-on
//
//     texture:SetTexture(path)
//     texture:SetTexCoord(0, right, 0, bottom)
//
// and give the region the picture's size in pixels. The texture carries every mip, for a picture
// drawn smaller than it was made. Its colours are written straight (not premultiplied), as the
// client's blend takes them; transparent pixels carry their neighbours' colour, so no dark fringe
// shows where the client filters.
//
// --- Groups: colours, outline widths, visibility (SVG) ---------------------------------------
//
// A group restyles the shapes of an SVG by id: those whose `id` is the group's, or whose nearest
// enclosing element with an id has it -- `<g id="outline">` reaches every shape inside it. Ids are
// case-sensitive. What a group sets, each on its own:
//   colour    0xAARRGGBB: every paint of its shapes, fill and stroke, gradients too, takes this
//             colour, and its alpha takes the place of their opacities (fill-opacity,
//             stroke-opacity, opacity)
//   width     its shapes' strokes as a multiple of the width the SVG gives each: 1 as drawn, 2
//             twice as wide, 0.5 half, 0 takes them off. Each shape keeps its proportion to the
//             others (an outline ring drawn wider than the outlines of the marks stays wider);
//             a shape with no stroke gets none, a dash pattern scales with its stroke
//   visible   false leaves its shapes out
// A reticle in two groups, `<g id="outline">` (the shapes filled and stroked dark) under
// `<g id="fill">` (the shapes filled light), is coloured and outlined by two groups:
// {fill: 0xFFFF4040}, {outline: 0xCC000000, width 2 (twice the outline the SVG draws)}.
//
// --- The tint -----------------------------------------------------------------------------------
//
// `tint` (0xAARRGGBB, SVG and PNG) multiplies every pixel once the picture is drawn: white leaves
// it, 0 means no tint.
//
// --- What is kept ------------------------------------------------------------------------------
//
// The textures are kept in memory, up to a budget (stats says it): past it, the least used are let
// go, and made again from their source if the client asks for them again. The name of a texture
// stays valid while its source file is unchanged.

#ifndef WXL_SERVICE_TEXTURE_H
#define WXL_SERVICE_TEXTURE_H

#include "wxl/abi.h"

#define WXL_TEXTURE_SERVICE         "wxl.texture"
#define WXL_TEXTURE_SERVICE_VERSION 1

/// The largest edge of a picture, in pixels: the client loads no larger texture.
#define WXL_TEXTURE_MAX_EDGE 2048

/// The longest a texture's path is, its terminating zero included.
#define WXL_TEXTURE_PATH_MAX 260

/// The most groups a request may carry.
#define WXL_TEXTURE_MAX_GROUPS 32

/// What a wxl_texture_group sets: any of these, or-ed.
#define WXL_TEXTURE_SET_COLOR   0x1u
#define WXL_TEXTURE_SET_WIDTH   0x2u
#define WXL_TEXTURE_SET_VISIBLE 0x4u

#ifdef __cplusplus
extern "C" {
#endif

/// A restyle of the shapes of an SVG with an id.
typedef struct wxl_texture_group
{
    uint32_t struct_size;
    wxl_str  id;      ///< the element's id, 1 to 63 bytes
    uint32_t set;     ///< WXL_TEXTURE_SET_*: which of the three below apply
    uint32_t color;   ///< 0xAARRGGBB, for every paint, its alpha their opacity
    float    width;   ///< the strokes' width times the SVG's own: 1 as drawn, 0 takes them off
    wxl_bool visible; ///< WXL_FALSE leaves the shapes out
} wxl_texture_group;

/// What a texture is made from.
typedef struct wxl_texture_request
{
    uint32_t                 struct_size;
    wxl_str                  source;      ///< see "What a texture is made from" above
    uint32_t                 width;       ///< pixels, up to WXL_TEXTURE_MAX_EDGE; 0 for the source's own
    uint32_t                 height;      ///< pixels, up to WXL_TEXTURE_MAX_EDGE; 0 for the source's own
    const wxl_texture_group* groups;      ///< SVG only; may be null when group_count is 0
    uint32_t                 group_count; ///< up to WXL_TEXTURE_MAX_GROUPS
    uint32_t                 tint;        ///< 0xAARRGGBB multiplied in; 0 for none
} wxl_texture_request;

/// A texture made.
typedef struct wxl_texture
{
    uint32_t struct_size;
    char     path[WXL_TEXTURE_PATH_MAX]; ///< the name to load it by, zero-terminated
    uint32_t width;          ///< the picture, in pixels
    uint32_t height;
    uint32_t texture_width;  ///< the texture, a power of two, at least 8
    uint32_t texture_height;
    float    right;          ///< width / texture_width: the picture's right texture coordinate
    float    bottom;         ///< height / texture_height: its bottom one
    wxl_bool reused;         ///< WXL_TRUE when it had been made already
    char     reason[256];    ///< on a failure, why, zero-terminated; empty on success
} wxl_texture;

/// What the service holds.
typedef struct wxl_texture_stats
{
    uint32_t struct_size;
    uint32_t textures;       ///< names known: each is answered while its source is unchanged
    uint32_t resident;       ///< of them, those whose bytes are in memory
    uint32_t texture_limit;  ///< names kept at most: past it the least used is forgotten
    uint64_t bytes;          ///< the bytes in memory
    uint64_t byte_limit;     ///< the budget: past it the least used textures' bytes are let go
    uint64_t made;           ///< textures drawn since the start, made again after being let go included
    uint64_t reused;         ///< requests answered with a texture already made
    uint64_t released;       ///< textures whose bytes were let go for the budget
} wxl_texture_stats;

typedef struct wxl_texture_api
{
    uint32_t struct_size;

    /**
     * @brief Makes a texture of an SVG or a PNG, or answers the one already made of the same.
     *
     * Any thread. Reads the source each time, to know whether it changed, and draws the picture
     * only the first time: a reticle asked again at each change of the screen's size costs a read.
     * @p out receives the texture's path and sizes; on a failure, its reason. WXL_E_INVALID: a
     * request that cannot be (a size over 2048, a group with no id, too many groups, a group on a
     * PNG, a source that names nothing a mod can read); WXL_E_NOT_FOUND: no such file;
     * WXL_E_UNSUPPORTED: a file neither SVG nor PNG; WXL_E_FAILED: one that could not be read.
     */
    wxl_result(WXL_CALL* make)(const wxl_texture_request* request, wxl_texture* out);

    /// What the service holds, written no longer than @p out's struct_size.
    wxl_result(WXL_CALL* stats)(wxl_texture_stats* out);
} wxl_texture_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_TEXTURE_H
