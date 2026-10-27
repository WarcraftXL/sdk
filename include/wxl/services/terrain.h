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
} wxl_terrain_layer;

/// A layer provider's answer, as the runtime hands it (the previous provider's answer, or the
/// client's own look).
typedef struct wxl_terrain_layer_look
{
    uint32_t struct_size;
    float    texture_scale; ///< how many times larger the texture is drawn than the client's own tiling: 1 as the client, 4 a picture over four times the yards; 1/64 to 1024
} wxl_terrain_layer_look;

/// Called on the main thread for each layer the client makes, while a provider is set.
typedef void(WXL_CALL* wxl_terrain_layer_fn)(void* user, const wxl_terrain_layer* layer, wxl_terrain_layer_look* look);

/// What set_layer_provider takes.
typedef struct wxl_terrain_layer_provider
{
    uint32_t             struct_size;
    wxl_terrain_layer_fn fn;
    void*                user;
} wxl_terrain_layer_provider;

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
} wxl_terrain_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_TERRAIN_H
