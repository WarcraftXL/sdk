// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The terrain service: the ground of the map the player is on, read and reshaped while the game
// runs.
//
// --- The grid ----------------------------------------------------------------------------------
//
// The ground is the client's own: a grid of chunks, each 145 vertices (81 outer, 64 inner), a cell
// of about 4.17 yards between two outer vertices. A chunk is named by its column and row on the
// map, `chunk_x` and `chunk_y` (0 to 1023), a vertex by its index in the chunk (0 to 144). How
// indices, rows, columns and world positions relate is written out in wxl/terrain/grid.hpp, which
// C++ mods can call directly; `locate` and `vertices` answer the same at run time.
//
// --- Reading ----------------------------------------------------------------------------------
//
//   height     the ground's height under a point, on the triangle the client walks and collides
//              with; the original height, before any mod's edit, on request
//   presence   whether there is ground there: ground, a hole, a chunk not loaded, or none
//   trace      the first point where a segment meets the ground, with the triangle's normal and
//              the chunk
//   locate     the chunk, the cell and the nearest vertex of a point
//   vertices   the world positions of a chunk's 145 vertices, edited or original
//   vertices_in  every vertex within a radius of a point, each once whichever chunks it is on,
//              with its chunk and index for set_heights: a brush applied in world space
//
// The client's own ray tests (wxl.world's trace, its camera, its clicks) and its walking see the
// edited ground too: they read the same heights.
//
// --- Layers (abi 0.1.42) --------------------------------------------------------------------------
//
//   set_layer_provider  a mod is told of each texture layer as the client makes it, with the bytes
//                       of the ADT it comes from, and answers the layer's look: how many times
//                       larger its texture is drawn than the client's own tiling (8 repeats a
//                       chunk, one every 4.17 yards). What a mod reading newer ADT data (MTXF,
//                       MTXP: wxl.modern-adt) uses for retail's per-texture scale.
//
// The runtime draws the layer at that scale on every path the client draws terrain with (its
// vertex programs, single pass and a layer a pass, and its fixed-function texture matrices),
// through the client's own constants and matrices, so the same under Direct3D 9 and the Vulkan
// renderer; an animated layer keeps its speed in yards. A layer no provider answers, or answered
// 1, is drawn exactly as the client draws it. The answer is taken when the layer is made (when its
// chunk is loaded): a provider set later sees the chunks loaded after it.
//
// Since abi 0.1.43 a look also carries an extra texture and four numbers per layer, for a mod that
// draws the ground with pixel programs of its own (wxl.gx's set_shader_provider; wxl.modern-adt's
// height blending). In the client's single-pass pixel programs (the paths that draw every layer of
// a chunk in one draw, at ps_3_0 and under the Vulkan renderer alike), layer k's extra texture is
// bound on sampler WXL_TERRAIN_LAYER_SAMPLER + k, read at the layer's own texture coordinates
// (so at its texture scale too), wrapped as the layer's texture is; its numbers are pixel constant
// WXL_TERRAIN_LAYER_PARAM_ROW + k. A provider set with WXL_TERRAIN_LAYER_PARAMS_EVERY_DRAW has
// those rows written at every single-pass draw of every chunk, zeros for a layer without numbers,
// so its programs never read another chunk's. The client's own programs read neither: a stock
// chunk is drawn as before.
//
// Since abi 0.1.44 a provider may also answer a chunk's layers past the client's four (its
// chunk_fn): the client holds four layers a chunk and draws the first four of a chunk naming more.
// A chunk with layers past its four is drawn, on the vertex-program single-pass path, in two draws
// of the same pixel program:
//
//   first   as the client draws it: its four layers on samplers 0 to 3, its alpha maps on 4
//   second  alpha-blended over the first (source alpha, inverse source alpha; depth tested, not
//           written): layers 4 to 7 on samplers 0 to 3, at texture coordinates 0 to 3 (their own
//           scales; an extra layer is drawn without its animation)
//
// In both draws the program sees every layer: the alpha maps of layers 4 to 7 on sampler
// WXL_TERRAIN_EXTRA_ALPHA_SAMPLER (red to alpha), layer k's numbers in c(32 + k) for k to 7, the
// extra textures of layers 4 and 5 on WXL_TERRAIN_EXTRA_SAMPLER and the next, and each layer's
// texture coordinates as a function of the alpha maps' own (WXL_TERRAIN_LAYER_COORD_ROW + k: u =
// alpha u * x + z, v = alpha v * y + w), so it can weigh all eight in either draw. The pass row
// (WXL_TERRAIN_LAYER_PASS_ROW) says which draw it is (x: 1 in the second) and whether the chunk has
// extra layers (y). The second draw's colour is blended by the alpha the program writes. Paths
// without that draw (the fixed-function single pass, a layer a pass) draw the client's four; so
// does a chunk whose layer 0 is a reflection. A chunk the client draws with its neighbour (maps
// with big alpha) has both halves' layers put together by texture, their alphas added, a half
// without a layer reading 0 for it; past four together, the four covering the most are drawn.
//
// A layer and a chunk also carry the map's WDT (abi 0.1.44), read when the map loads: its MPHD
// flags say what the map's ADTs hold (retail: 0x80 adt_has_height_texturing, the height blend). The
// client of 3.3.5a reads uncompressed alpha maps of 4096 bytes only under 0x4 (adt_has_big_alpha),
// where retail reads them so under 0x4 or 0x80: a map loaded with 0x80 and without 0x4 is given 0x4
// as it loads, so its alpha maps are read as retail reads them.
//
// Since abi 0.1.45 a mod may also supply a tile's other files (set_tile_provider). Cataclysm and
// later write a tile as several files: the root (<map>_<x>_<y>.adt: MVER, MHDR, 256 MCNKs of
// heights, normals, vertex colours, sounds; MH2O and MFBO) and, beside it, _tex0 (MTEX, MTXF or
// MTXP, MAMP, and per chunk MCLY, MCAL, MCSH), _obj0 (MMDX, MMID, MWMO, MWID, MDDF, MODF, and per
// chunk the doodad and map object references MCRD and MCRW) and _obj1 (the same placements for
// the far view). The client of 3.3.5a reads one file with an index of its chunks (MCIN). When it
// has read a root without MCIN, the runtime asks the tile providers for the set, keeps the
// files a provider answered until the client frees the tile, and loads the tile natively: the
// client's own structures point into these files as they would into one file (no file is made
// again): its chunks by an index of the root's MCNKs, its textures by _tex0's MTEX, its
// placements by _obj0's, each chunk's layers, alpha maps and shadow by _tex0's MCNK of the same
// place, its references by _obj0's. The client holds 4 layers a chunk and 16 holes (a 4 x 4
// grid): a chunk's first 4 layers are kept (layers 5 to 8 through a provider's chunk_fn), and
// retail's 64 holes (8 x 8) are folded into the 16, a hole wherever one of the 4 it covers is.
// A layer naming a texture the tile does not have, a reference to a placement that does not exist
// or names its model by FileDataID, are left out. A root without MCIN no provider answers loads
// with its ground, liquids and sounds, untextured and without placements. Tiles with MCIN load as
// the client loads them.
//
// --- Editing ----------------------------------------------------------------------------------
//
//   set_heights  sets vertices to absolute heights, many at once
//   reset_chunk  gives a chunk back its original heights, as far as the caller's edits go
//   reset_all    gives back every edit of the caller
//
// An edit is an absolute world height for a vertex. The client then sees it everywhere: the
// ground drawn (its normals, recomputed across the neighbouring chunks' edges, its vertex
// buffers, its bounds and culling, under Direct3D 9 and the Vulkan renderer alike), the player's
// walking and falling, the camera's collision and the client's ray tests. A vertex on a chunk's
// edge is the same ground as its neighbour's: setting it through either chunk sets both, so the
// ground never cracks. A player standing still on ground that was lowered starts to fall, as the
// client does when a transport leaves. Ground raised over the feet of a player on it (standing or
// walking, under any part of them) puts them on top: a rise up to a step's at once, as the
// client's own step up does, a higher one at 8 yards a second; one standing still then lands
// through the client's fall, so the server gets the movement messages the client sends for a
// landing (MSG_MOVE_HEARTBEAT falling, MSG_MOVE_FALL_LAND), never a teleport. Nobody is lifted
// through a roof, a floor of a building or anything else of the world above them. Liquids keep
// their own surface, and map doodads (trees, rocks) stay where the map placed them.
//
// Edits belong to the mod that made them, in a layer of its choosing (`layer`, any number; a
// native mod may keep its committed edits in one and a preview in another, and reset either
// alone; wxl.lua gives each Lua mod its own). Where two mods or layers set one vertex, the latest
// edit shows, and the one before comes back when it is reset. A mod's edits come off when it is
// disabled or unloaded.
//
// The client streams terrain in and out as the player moves. The runtime keeps the edits of a
// chunk that leaves and puts them back when it loads again; TerrainChunkLoaded and
// TerrainChunkUnloaded say when (a chunk's edits are back before TerrainChunkLoaded). A chunk not
// loaded may be edited: the edit waits for it. When the map is unloaded (a teleport to another
// map, a logout) every edit is dropped and TerrainEditsCleared is raised; a teleport within the
// map keeps them.
//
// Main thread only: a call off it answers false, nothing or WXL_E_INVALID. Out of the world, and
// on maps made only of map objects (instances without terrain), there is no ground: reads answer
// nothing and edits WXL_E_UNSUPPORTED. Every refusal is logged once per mod under the tag
// `terrain`, naming the call and what to change.
//
// The resolution is the client's grid: a vertex every 4.17 yards (2.08 counting the inner ones).
// Holes, textures and the server's view of the ground are not this service's: the server keeps
// its own heights, so a mod that digs where a server checks movement must tell its server.

#ifndef WXL_SERVICE_TERRAIN_H
#define WXL_SERVICE_TERRAIN_H

#include "wxl/abi.h"
#include "wxl/events.h"

#define WXL_TERRAIN_SERVICE         "wxl.terrain"
#define WXL_TERRAIN_SERVICE_VERSION 1

/// Vertices of a chunk: 9 x 9 outer and 8 x 8 inner.
#define WXL_TERRAIN_CHUNK_VERTICES 145u
/// Chunks on a side of a map.
#define WXL_TERRAIN_CHUNKS_PER_SIDE 1024
/// The highest an edit may set a vertex, and minus the lowest, in yards.
#define WXL_TERRAIN_HEIGHT_LIMIT 50000.0f
/// The largest radius vertices_in takes, in yards.
#define WXL_TERRAIN_RADIUS_LIMIT 128.0f

#ifdef __cplusplus
extern "C" {
#endif

/// Where a segment met the ground.
typedef struct wxl_terrain_hit
{
    uint32_t struct_size; ///< set by the caller: sizeof(wxl_terrain_hit)
    wxl_vec3 point;       ///< the point, in world coordinates
    wxl_vec3 normal;      ///< the unit normal of the triangle met, facing up
    int32_t  chunk_x;     ///< the chunk of the triangle
    int32_t  chunk_y;
    float    fraction;    ///< how far along the segment, 0 at its start, 1 at its end
} wxl_terrain_hit;

/// Where a point is on the grid.
typedef struct wxl_terrain_place
{
    uint32_t struct_size; ///< set by the caller: sizeof(wxl_terrain_place)
    int32_t  chunk_x;     ///< the chunk's column on the map, 0-1023
    int32_t  chunk_y;     ///< its row
    int32_t  cell_row;    ///< the cell's row in the chunk, 0-7: along world x, falling
    int32_t  cell_col;    ///< its column, 0-7: along world y, falling
    uint32_t nearest;     ///< the chunk's vertex nearest to the point (0-144)
    wxl_bool loaded;      ///< whether the chunk is loaded now
} wxl_terrain_place;

/// One vertex set to a height: an entry of an edit.
typedef struct wxl_terrain_height
{
    int32_t  chunk_x; ///< the chunk's column on the map, 0-1023
    int32_t  chunk_y; ///< its row
    uint32_t index;   ///< the vertex, 0-144 (wxl/terrain/grid.hpp)
    float    height;  ///< the absolute world height, within +-WXL_TERRAIN_HEIGHT_LIMIT
} wxl_terrain_height;

/// A vertex of the ground in a region (vertices_in).
typedef struct wxl_terrain_vertex
{
    int32_t  chunk_x;    ///< the chunk it is named in, for set_heights: on a chunk's edge, the
    int32_t  chunk_y;    ///< chunk whose cells it starts (set_heights writes every copy)
    uint32_t index;      ///< the vertex in that chunk, 0-144
    float    x;          ///< its place in the world
    float    y;
    float    z;          ///< its height now, edits included; 0 when its chunk is not loaded
    float    original_z; ///< its height before any mod's edit; 0 when its chunk is not loaded
    wxl_bool inner;      ///< an inner vertex (V8, in the middle of a cell), else an outer one (V9)
    wxl_bool loaded;     ///< whether its chunk is loaded now (the heights mean nothing otherwise)
} wxl_terrain_vertex;

/// What the service has done since the client started, and what its last edit cost.
typedef struct wxl_terrain_stats
{
    uint32_t struct_size;      ///< set by the caller: sizeof(wxl_terrain_stats)
    uint32_t edited_chunks;    ///< chunks some mod has an edit on now, loaded or not
    uint32_t edited_vertices;  ///< vertices some mod has an edit on now, an edge vertex counted once per chunk
    uint64_t edits;            ///< set_heights calls that changed something
    uint64_t reapplied;        ///< chunks whose edits were put back as they loaded again
    uint32_t last_vertices;    ///< vertices the last edit wrote, the copies on edges included
    uint32_t last_chunks;      ///< chunks the last edit reshaped
    uint32_t last_microseconds; ///< how long the last edit took the runtime, the client's rebuilds in it
    uint32_t max_microseconds; ///< the longest edit so far
} wxl_terrain_stats;

/// A texture layer of the ground as the client makes it (CMapRenderChunk::CreateLayer), what a layer
/// provider is told. Valid during the call only.
typedef struct wxl_terrain_layer
{
    uint32_t    struct_size;
    int32_t     tile_x;    ///< the ADT's tile: its column on the map (0 to 63)
    int32_t     tile_y;    ///< and its row
    int32_t     chunk_x;   ///< the chunk the layer is drawn on, as wxl.terrain names it (0 to 1023); -1 unknown
    int32_t     chunk_y;
    uint32_t    layer;     ///< its place among the chunk's layers, 0 to 3
    uint32_t    texture;   ///< its texture: an index into the ADT's MTEX
    uint32_t    flags;     ///< its MCLY flags
    const void* file;      ///< the ADT's bytes as the client read them (a whole file: MVER, MHDR, ...)
    uint32_t    file_size;
    uint32_t    reserved;  ///< 0
    // --- abi 0.1.44 (read when struct_size reaches past them)
    const void* map_file;      ///< the map's WDT as the runtime read it when the map loaded (MVER, MPHD, MAIN, ...); null for none
    uint32_t    map_file_size;
    uint32_t    reserved2;     ///< 0
} wxl_terrain_layer;

/// A layer provider's answer, as the runtime hands it (the previous provider's answer, or the
/// client's own look).
typedef struct wxl_terrain_layer_look
{
    uint32_t struct_size;
    float    texture_scale; ///< how many times larger the texture is drawn than the client's own tiling: 1 as the client, 4 a picture over four times the yards; 1/64 to 1024
    // --- abi 0.1.43 (read when struct_size reaches past them)
    const char* extra_texture; ///< a texture file (a BLP path in the client's archives) bound with the layer on sampler WXL_TERRAIN_LAYER_SAMPLER + layer of the single-pass pixel programs; null for none. Copied: valid during the call only
    float       params[4];     ///< the layer's four numbers, pixel constant WXL_TERRAIN_LAYER_PARAM_ROW + layer of those programs; zeros for none
} wxl_terrain_layer_look;

/// The sampler of layer 0's extra texture in the single-pass pixel programs; layer k's is this + k.
#define WXL_TERRAIN_LAYER_SAMPLER 12u
/// The pixel constant of layer 0's numbers in those programs; layer k's is this + k.
#define WXL_TERRAIN_LAYER_PARAM_ROW 32u

/// The sampler of the alpha maps of layers 4 to 7 (abi 0.1.44), red to alpha.
#define WXL_TERRAIN_EXTRA_ALPHA_SAMPLER 9u
/// The sampler of layer 4's extra texture; layer 5's is the next. Layers 6 and 7 have none: the
/// single-pass programs' sixteen samplers hold no more.
#define WXL_TERRAIN_EXTRA_SAMPLER 10u
/// The pixel constant of layer 0's texture coordinates as a function of the alpha maps'; layer k's
/// is this + k, for k to 7.
#define WXL_TERRAIN_LAYER_COORD_ROW 41u
/// The pixel constant saying which draw it is (x: 0 first, 1 second) and whether the chunk has
/// layers past its four (y: 1).
#define WXL_TERRAIN_LAYER_PASS_ROW 40u
/// Extra layers a chunk: layers 4 to 7.
#define WXL_TERRAIN_EXTRA_LAYERS 4u
/// The side of a chunk's alpha map, in texels.
#define WXL_TERRAIN_ALPHA_SIZE 64u

/// wxl_terrain_layer_provider.flags (abi 0.1.43): the layers' numbers are written at every
/// single-pass draw of every chunk, zeros for a layer that has none.
#define WXL_TERRAIN_LAYER_PARAMS_EVERY_DRAW 0x1u

/// A map chunk whose client layers are made, what a provider's chunk_fn is told (abi 0.1.44).
/// Valid during the call only.
typedef struct wxl_terrain_chunk
{
    uint32_t    struct_size;
    int32_t     tile_x;      ///< the ADT's tile
    int32_t     tile_y;
    int32_t     chunk_x;     ///< the map chunk, as wxl.terrain names it
    int32_t     chunk_y;
    uint32_t    layers;      ///< the client's layers on it: 4 (a chunk with fewer has no extra layers)
    uint32_t    texture[4];  ///< their MTEX textures, in order
    const void* file;        ///< the root ADT's bytes
    uint32_t    file_size;
    const void* map_file;    ///< the map's WDT, as wxl_terrain_layer's
    uint32_t    map_file_size;
    uint32_t    reserved;    ///< 0
} wxl_terrain_chunk;

/// One layer past the client's four.
typedef struct wxl_terrain_extra_layer
{
    const char* texture;        ///< its texture file (a BLP path); copied
    const char* extra_texture;  ///< as a look's; null for none (layers 6 and 7: never bound)
    float       texture_scale;  ///< as a look's
    float       params[4];      ///< as a look's
    uint32_t    flags;          ///< its MCLY flags
} wxl_terrain_extra_layer;

/// A chunk_fn's answer: the chunk's layers past its four, and their alpha maps.
typedef struct wxl_terrain_extra_layers
{
    uint32_t                struct_size;
    uint32_t                count; ///< 0 to WXL_TERRAIN_EXTRA_LAYERS: layers 4 to 3 + count
    wxl_terrain_extra_layer layer[WXL_TERRAIN_EXTRA_LAYERS];
    /// Their alpha maps, WXL_TERRAIN_ALPHA_SIZE square, row by row from the chunk's north-west
    /// corner (as MCAL), four bytes a texel: layer 4's, 5's, 6's, 7's, read as the map's own: on a
    /// map with big alpha (MPHD 0x4, and so every map flagged 0x80) the eight layers' alphas add up
    /// and layer 0 has what they leave, as retail; on any other each lies over the layers below it
    /// (lerp(below, layer, alpha)).
    uint8_t alpha[WXL_TERRAIN_ALPHA_SIZE * WXL_TERRAIN_ALPHA_SIZE * 4];
} wxl_terrain_extra_layers;

/// Called on the main thread when a chunk's four client layers are made, while a provider with a
/// chunk_fn is set; it fills @p out (count 0 leaves the chunk as the client draws it).
typedef void(WXL_CALL* wxl_terrain_chunk_fn)(void* user, const wxl_terrain_chunk* chunk, wxl_terrain_extra_layers* out);

/// Called on the main thread for each layer the client makes, while a provider is set.
typedef void(WXL_CALL* wxl_terrain_layer_fn)(void* user, const wxl_terrain_layer* layer, wxl_terrain_layer_look* look);

/// What set_layer_provider takes.
typedef struct wxl_terrain_layer_provider
{
    uint32_t             struct_size;
    wxl_terrain_layer_fn fn;
    void*                user;
    // --- abi 0.1.43 (read when struct_size reaches past it)
    uint32_t             flags; ///< WXL_TERRAIN_LAYER_PARAMS_EVERY_DRAW, or 0
    // --- abi 0.1.44
    wxl_terrain_chunk_fn chunk_fn; ///< a chunk's layers past the client's four; null for none
} wxl_terrain_layer_provider;

/// A tile's file a tile provider supplies (abi 0.1.45): an index into wxl_terrain_tile_files.
#define WXL_TERRAIN_TILE_TEX0 0u  ///< <tile>_tex0.adt: textures, layers, alpha maps, shadows
#define WXL_TERRAIN_TILE_OBJ0 1u  ///< <tile>_obj0.adt: models, map objects, each chunk's references
#define WXL_TERRAIN_TILE_OBJ1 2u  ///< <tile>_obj1.adt: the far view's placements (kept for providers; this client draws obj0's)
/// Files a tile provider answers.
#define WXL_TERRAIN_TILE_FILES 3u

/// A root ADT without MCIN the client has read, what a tile provider is told (abi 0.1.45). Valid
/// during the call only.
typedef struct wxl_terrain_tile
{
    uint32_t    struct_size;
    int32_t     tile_x;        ///< its column on the map (0 to 63)
    int32_t     tile_y;        ///< and its row
    wxl_str     path;          ///< the root's name as the client opened it ("World\\Maps\\X\\X_32_48.adt")
    const void* file;          ///< the root's bytes (MVER, MHDR, MCNK...)
    uint32_t    file_size;
    const void* map_file;      ///< the map's WDT, as wxl_terrain_layer's
    uint32_t    map_file_size;
    uint32_t    reserved;      ///< 0
} wxl_terrain_tile;

/// A tile provider's answer: the files of the tile's set it has, by WXL_TERRAIN_TILE_*; null (or a
/// size of 0) for one it has not. The runtime copies them before the call returns.
typedef struct wxl_terrain_tile_files
{
    uint32_t    struct_size;
    const void* data[WXL_TERRAIN_TILE_FILES];
    uint32_t    size[WXL_TERRAIN_TILE_FILES];
} wxl_terrain_tile_files;

/// Called on the main thread when the client has read a root ADT without MCIN, while a tile
/// provider is set; it fills @p out. The first provider that answers a file gives it.
typedef void(WXL_CALL* wxl_terrain_tile_fn)(void* user, const wxl_terrain_tile* tile, wxl_terrain_tile_files* out);

/// What set_tile_provider takes.
typedef struct wxl_terrain_tile_provider
{
    uint32_t            struct_size;
    wxl_terrain_tile_fn fn;
    void*               user;
} wxl_terrain_tile_provider;

typedef struct wxl_terrain_api
{
    uint32_t struct_size;

    /**
     * @brief The ground's height under a point.
     * @param original  WXL_TRUE for the height before any mod's edit
     * @param out       the absolute world height, written only when there is ground
     * @return WXL_TRUE when there is ground under the point: loaded, and no hole
     */
    wxl_bool(WXL_CALL* height)(double x, double y, wxl_bool original, double* out);

    /**
     * @brief Whether there is ground under a point.
     * @return a wxl_terrain_presence: WXL_TERRAIN_GROUND, WXL_TERRAIN_HOLE, WXL_TERRAIN_NOT_LOADED,
     *         or WXL_TERRAIN_NONE (off the map, out of the world, a map without terrain)
     */
    uint32_t(WXL_CALL* presence)(double x, double y);

    /**
     * @brief The first point where a segment meets the ground: loaded chunks only, holes let
     * through, both faces of a triangle counted.
     * @param out  set its struct_size; written only when the segment meets the ground
     * @return WXL_TRUE when it does
     */
    wxl_bool(WXL_CALL* trace)(wxl_vec3 start, wxl_vec3 finish, wxl_terrain_hit* out);

    /**
     * @brief Where a point is on the grid.
     * @param out  set its struct_size
     * @return WXL_TRUE when the point is on the map; it need not be loaded
     */
    wxl_bool(WXL_CALL* locate)(double x, double y, wxl_terrain_place* out);

    /**
     * @brief The world positions of a chunk's vertices, in index order.
     * @param original  WXL_TRUE for the heights before any mod's edit
     * @param out       room for `capacity` positions; may be null to count
     * @return 145 when the chunk is loaded (the positions written, as many as fit), 0 when it is
     *         not or the chunk is off the map
     */
    uint32_t(WXL_CALL* vertices)(int32_t chunk_x, int32_t chunk_y, wxl_bool original, wxl_vec3* out,
                                 uint32_t capacity);

    /**
     * @brief Sets vertices to absolute heights, in one go: the client's ground, drawing and
     * collision follow before the call returns (the drawing at the end of the frame when called
     * while the world is being drawn).
     * @param heights  `count` entries; a vertex named twice takes the last
     * @param layer    the caller's layer the edits go in
     * @return WXL_OK; WXL_E_INVALID for an entry off the grid or a height not finite or beyond
     *         WXL_TERRAIN_HEIGHT_LIMIT (nothing is set then), or off the main thread;
     *         WXL_E_UNSUPPORTED out of the world or on a map without terrain
     */
    wxl_result(WXL_CALL* set_heights)(const wxl_terrain_height* heights, uint32_t count, uint32_t layer);

    /**
     * @brief Takes the caller's edits of one layer off a chunk.
     * @return WXL_OK; WXL_E_NOT_FOUND when the layer has none there; WXL_E_INVALID off the grid
     */
    wxl_result(WXL_CALL* reset_chunk)(int32_t chunk_x, int32_t chunk_y, uint32_t layer);

    /**
     * @brief Takes every edit of the caller's layer off, everywhere.
     * @return WXL_OK; WXL_E_NOT_FOUND when the layer has none
     */
    wxl_result(WXL_CALL* reset_all)(uint32_t layer);

    /**
     * @brief What the service has done, and what its last edit cost.
     * @param out  set its struct_size
     */
    wxl_result(WXL_CALL* stats)(wxl_terrain_stats* out);

    /**
     * @brief Every vertex within `radius` yards of (x, y) in the plane, each once: a vertex on a
     * chunk's edge is reported in one chunk, and set_heights writes it into every chunk holding
     * it. Ordered along world x, then y. Vertices of chunks not loaded are reported with `loaded`
     * false.
     * @param radius  0 to WXL_TERRAIN_RADIUS_LIMIT
     * @param out     room for `capacity` vertices; may be null to count
     * @return how many vertices the region holds (as many written as fit); 0 for a region off the
     *         map, a radius out of range, off the main thread, or with no ground loaded
     */
    uint32_t(WXL_CALL* vertices_in)(double x, double y, double radius, wxl_terrain_vertex* out, uint32_t capacity);

    /**
     * @brief The calling mod's layer provider (abi 0.1.42): told of each texture layer as the
     *        client makes it, it answers its look (wxl_terrain_layer_look). One per mod; a new one
     *        replaces it; null takes it off, and it goes when the mod is disabled or unloads.
     *        Several mods' providers are called in the order they were set, each seeing the look
     *        the one before left. WXL_E_INVALID off the main thread or for a provider without fn.
     */
    wxl_result(WXL_CALL* set_layer_provider)(const wxl_terrain_layer_provider* provider);

    /**
     * @brief The calling mod's tile provider (abi 0.1.45): asked for a tile's split files (_tex0,
     *        _obj0, _obj1) when the client has read a root ADT without MCIN, so the tile loads
     *        natively. One per mod; a new one replaces it; null takes it off, and it goes when
     *        the mod is disabled or unloads. Several mods' providers are asked in the order they
     *        were set, each file taken from the first that answers it. WXL_E_INVALID off the main
     *        thread or for a provider without fn.
     */
    wxl_result(WXL_CALL* set_tile_provider)(const wxl_terrain_tile_provider* provider);
} wxl_terrain_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_TERRAIN_H
