// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The draw service: simple shapes drawn in the 3D world by the runtime, on every device.
//
//   supported           whether this runtime draws shapes on the device it runs now
//   unsupported_reason  why not, in a sentence; empty when it does
//   draw                a shape for this frame only (immediate mode)
//   add                 a shape kept until removed or until its duration runs out: a handle
//   update, move        the kept shape changed: all of it, or only where it stands
//   set_style           its look changed
//   set_duration        how long it stays from now (0: until removed)
//   attach, detach      it follows a world object, and goes when the object leaves view
//   remove              it goes now
//   clear               every shape of the calling mod goes, kept and immediate
//   stats               what the last frame drew and what it cost
//
// --- Shapes ----------------------------------------------------------------------------------
//
// A shape is a description (wxl_draw_shape) in world coordinates, yards, and a look
// (wxl_draw_style). Its kind (wxl_draw_kind, enums.def) says which fields it reads:
//
//   line       points (2 or more): a polyline through them; WXL_DRAW_CLOSED joins the last to
//              the first, WXL_DRAW_ON_GROUND lays it on the ground
//   circle     center, radius
//   ring       center, radius, inner (the hole's radius, below radius)
//   sector     center, radius, facing (the middle of the arc), angle (its width, up to 2 pi),
//              inner (0 for a cone, more for a band of a ring)
//   rectangle  center, size.x (the length, along facing), size.y (the width), facing
//   polygon    points (3 or more, any shape that does not cross itself: concave is fine)
//   grid       center, size.x (along facing) and size.y, spacing between its lines, facing
//   box        center, size (x along facing, y across, z up), facing, pitch, roll
//   sphere     center, radius
//   cylinder   center (its middle), radius, size.z (its height), facing, pitch, roll
//   beam       points (2: where it starts, a, and where it ends, b), size.x its width at a and
//              size.y at b (yards; 0: as at a), point_alphas its opacity at a and b (none: 1 to 0);
//              abi 0.1.40
//
// The points of a line or a polygon are offsets from center: leave center at 0 to give them in
// world coordinates, or set it to move the whole shape with `move`.
//
// Circle, ring, sector, rectangle, polygon and grid are ground shapes: they lie on the map's
// ground, following its slopes and every edit wxl.terrain makes, read through the same triangles
// the client walks on. Where there is no ground (a hole, a chunk not loaded, a map without terrain
// such as an instance made of map objects, a bridge or a floor of a building), they lie flat at
// center.z; WXL_DRAW_FLAT keeps them flat at center.z everywhere (a floor whose height the mod
// knows, from wxl.world's trace). The z of a polygon's points is not read. Box, sphere and
// cylinder are volumes, drawn where they are.
//
// --- The look --------------------------------------------------------------------------------
//
// Colour 0xRRGGBB (the top byte is reserved: pass 0). `alpha` is the opacity of the lines (a
// line, the outline of a ground shape, a grid's lines, a volume's wireframe), 0 for none;
// `fill_alpha` the opacity of the inside (ground shapes and volumes), 0 for none. Lines are
// `width` pixels wide at any distance, in pixels of a picture 1080 lines high (drawn in
// proportion at other heights), 0.5 to WXL_DRAW_MAX_WIDTH. `depth` WXL_DRAW_DEPTH_TESTED hides a
// shape behind what stands in front of it (walls, units, the ground); WXL_DRAW_DEPTH_ON_TOP draws
// it over everything of the world. `pulse` (seconds a beat, 0 for none) makes the opacity beat
// between full and a fifth; `fade_in` and `fade_out` (seconds) bring a kept shape in after `add`
// and take it out over the end of its duration. A null style is white lines 2 pixels wide, no
// fill, tested against depth.
//
// From abi 0.1.38 a style goes on past `fade_out` (an older runtime reads the first part only, and
// a struct_size that stops at `fade_out` is the look above). Every field after it left at 0
// changes nothing.
//
// Lines. `softness` (pixels at 1080 lines) fades a line's edges over that many pixels, its width
// measured where it is half seen. Under WXL_DRAW_STYLE_LINE_ALPHA, a line's opacity goes from
// `alpha_start` at its first point to `alpha_end` at its last, along its length; a shape's
// `point_alphas` set it point by point (both multiply `alpha`). `shadow_alpha` > 0 draws a shadow
// under the shape's lines, in `shadow_color`, moved `shadow_x` pixels to the right and `shadow_y`
// pixels down (1.5 and 1.5 sit like the game's text shadows; -15 to 15), as a stroke
// `shadow_softness` pixels wider than the line whose edges fade over as many pixels: the shape's
// lines drawn once more, under every line of the frame.
//
// The inside of a ground shape (circle, ring, sector, rectangle, polygon, grid). `texture` names a
// texture the client loads as it loads its own: a BLP of the game's archives, a file a mod serves,
// or the path wxl.texture answers, with its `right` and `bottom` as `texture_right` and
// `texture_bottom` (the part of the texture its picture fills; 0 for 1). The inside is drawn once
// the texture has loaded and left out until then; a texture no shape uses is let go of a few
// seconds later. The picture is multiplied by `fill_color` under WXL_DRAW_STYLE_FILL_COLOR (by
// `color` otherwise) and by `fill_alpha`. `uv` (wxl_draw_uv) lays it on the ground: FIT once over
// the shape's bounds, TILE repeated in the same frame, both turned with the shape's facing (the
// picture's top towards it); WORLD in the world's own frame, its top to the north whatever the
// shape, so that shapes side by side join. `uv_size_x` and `uv_size_y` are one picture's size: a
// part of the bounds for FIT and TILE (0.5: twice across), yards for WORLD (0: 1). `uv_rotation`
// turns the picture (radians, counter-clockwise seen from above) and `uv_offset_x`, `uv_offset_y`
// move it, in pictures. `blend` (wxl_draw_blend) puts the inside on the ground: ALPHA over it, ADD
// as light, MODULATE multiplied into it (a stain, a scorch, a mark scored into the dirt: white
// leaves the ground as it is, the picture's colour darkens it, and the opacity says how much).
// `feather` (yards) fades the inside out towards its border, `radial_fade` (0 to 1) from the centre
// outwards over that part of the radius (1: from the centre itself). Any of these on a shape with
// no texture draws its plain inside so.
//
// From abi 0.1.39 a box's faces and a cylinder's side and ends take the same look, each face in
// its own frame: the picture's top towards the box's top (the top and bottom faces': towards its
// facing; a cylinder's side: the picture once around it, its seam behind its facing). FIT and TILE
// lay one picture over each face; WORLD lays `uv_size` yards a picture along each face's axes from
// the world's origin, so that faces of boxes side by side join. `feather` fades each face out
// towards its edges (a cylinder's side towards its ends), `radial_fade` from each face's middle. A
// sphere's inside stays plain. Changing only the look of a kept shape (set_style: its texture's
// frame, `uv_offset` each frame to scroll it, its colour) builds nothing again.
//
// A beam (abi 0.1.40) is a shaft of light, a spell's ray, a laser: a quad from a to b turned around
// its own axis to face the camera each frame, so that it reads as a volume from any side. It is
// drawn with the inside's look: `fill_alpha` and `fill_color`, its opacity going from a's to b's
// along it; `feather` is the part of its half width over which it fades to its sides (1: soft all
// across); a texture lies along it (FIT once from a to b, its top towards b; TILE repeated, uv_size
// a part of its length; WORLD uv_size yards a picture), `uv_offset` scrolling it; mask, blend and
// occluded_alpha as an inside's. It fades out where it nears the camera and as it is seen end-on.
//
// From abi 0.1.40, under WXL_DRAW_STYLE_MASK a texture gives the opacity alone and the colour is the
// style's (fill_color, or color): a dark picture of cracks glows in any colour. `texture_blur`
// blurs the picture, each step reading it at half its detail (its mip levels), alike on every device;
// `feather` and `radial_fade` soften a textured inside's border as a plain one's.
//
// `occluded_alpha` (abi 0.1.39, 0 to 1) shows what the scene hides of a shape tested against the
// depth (WXL_DRAW_DEPTH_TESTED): the shape is drawn twice, first where the scene stands in front of
// it at this part of its opacity (a selection box going under the ground, a line behind a wall),
// then where it is seen as usual. 0 (the default) draws only what is seen. Lines, ground shapes and
// volumes alike.
//
// --- Lifetimes ---------------------------------------------------------------------------------
//
// `draw` is immediate mode: the shape is drawn in the next frame of the world and then forgotten
// (one drawn after the world of a frame is drawn in the next). A mod calls it every frame for what
// follows the frame (a grid under the cursor); nothing is kept between frames, and nothing to
// remove. `add` keeps a shape and answers its handle: update, move, set_style and set_duration
// change it, remove takes it off, and a duration > 0 takes it off by itself (after its fade-out).
// `attach` makes a kept shape follow a world object: its center (and a line's or a polygon's
// points) are then offsets from the object's position, turned with the object's facing under
// WXL_DRAW_ATTACH_TURN; the shape goes when the object leaves view.
//
// Every shape belongs to the mod that made it: it goes when the mod is disabled or unloaded
// (wxl.lua's own, when that Lua mod goes). A handle of another mod's shape is not found.
//
// --- Budgets -----------------------------------------------------------------------------------
//
// A mod keeps up to WXL_DRAW_MAX_SHAPES shapes and draws up to WXL_DRAW_MAX_IMMEDIATE in a frame;
// a line or polygon takes up to WXL_DRAW_MAX_POINTS points; a ground shape or a volume reaches up
// to WXL_DRAW_MAX_EXTENT yards from its center, a grid draws up to WXL_DRAW_MAX_GRID_LINES lines;
// a shape builds up to WXL_DRAW_MAX_SHAPE_VERTICES vertices. Past any of these the call is
// refused with WXL_E_INVALID, and the reason logged once per mod and call under the tag `draw`.
// All mods together draw up to WXL_DRAW_MAX_FRAME_VERTICES vertices a frame: shapes past it are
// left out of that frame (immediate ones first, then the newest kept ones), counted in
// stats().left_out and logged once a second while it lasts.
//
// --- When it is drawn ----------------------------------------------------------------------------
//
// After the world's opaque geometry and its liquids, and before the world's translucent models,
// its particles and spell effects, its floating text and the interface: shapes are tested against
// the scene's depth, so walls, units and the ground hide them, and a shape on a lake's bed shows
// through the water. Nothing is written into the depth: the translucent world drawn after blends
// over them. Under Direct3D 9 (the stock client, and the recompiled one by default) the runtime
// draws through the game's device, and puts its state back; under the Vulkan renderer through
// wxl.render's passes. The same vertices, the same programs and the same numbers on both: a mod
// sees the same picture.
//
// --- Particles (abi 0.1.40) --------------------------------------------------------------------
//
// An emitter (wxl_draw_emitter) throws particles from a spawn shape (a point, a disc, a box, a
// segment) along a cone (direction, spread), at speeds, lives, sizes and spins drawn between their
// min and max; then gravity pulls them, drag slows them, the ground stops them (bounce: once,
// with restitution and friction, then they settle; settle: they rest where they land and slide to a
// stop), and an attractor (attraction > 0, yards/s^2 towards a point; < 0 pushes away) pulls them in,
// taking them out within attract_radius. `emit` throws one burst and forgets it (dust, debris);
// `add_emitter` keeps an emitter: a burst when it is added, and `rate` particles a second for
// `duration` seconds (0: until stopped); `burst`, `place`, `follow`, `update_emitter`,
// `set_emitter_style` change it, `stop` takes it off (its particles live out their lives). The
// runtime simulates them on the CPU once a frame of the world.
//
// Their look (wxl_draw_particles): soft sprites facing the camera, lying flat on the ground, or
// stretched along their velocity; a texture the client loads (wxl.texture's or the game's; a sprite
// sheet of frames_x by frames_y frames played at frame_rate, or once over a life) or, with none, a
// soft round blot; blended ALPHA (drawn far to near), ADD or MODULATE; over its life a particle's
// size, colour and opacity follow up to WXL_DRAW_MAX_PARTICLE_KEYS keys (none: white, its own size,
// fading out). `softness` fades a particle as it nears the ground, by its height above the terrain,
// the same on every device. `occluded_alpha` as a shape's.
//
// Budgets: WXL_DRAW_MAX_PARTICLES alive in all, WXL_DRAW_MAX_MOD_PARTICLES a mod,
// WXL_DRAW_MAX_EMITTERS emitters a mod (kept and thrown); what is past them is not thrown, and is
// counted in stats().particles_culled.
//
// Main thread only: a call off it answers WXL_E_INVALID.

#ifndef WXL_SERVICE_DRAW_H
#define WXL_SERVICE_DRAW_H

#include "wxl/abi.h"
#include "wxl/events.h"

#define WXL_DRAW_SERVICE         "wxl.draw"
#define WXL_DRAW_SERVICE_VERSION 1

/// Kept shapes a mod may hold at once.
#define WXL_DRAW_MAX_SHAPES 4096u
/// Immediate shapes a mod may draw in a frame.
#define WXL_DRAW_MAX_IMMEDIATE 4096u
/// Points of a line or a polygon.
#define WXL_DRAW_MAX_POINTS 4096u
/// How far a ground shape or a volume reaches from its center, in yards (a radius, half a size).
#define WXL_DRAW_MAX_EXTENT 250.0f
/// Lines of a grid, both ways together.
#define WXL_DRAW_MAX_GRID_LINES 1024u
/// Vertices one shape may build.
#define WXL_DRAW_MAX_SHAPE_VERTICES 131072u
/// Vertices drawn in a frame, all mods together.
#define WXL_DRAW_MAX_FRAME_VERTICES 524288u
/// The widest line, in pixels at 1080 lines.
#define WXL_DRAW_MAX_WIDTH 31.0f
/// The softest line edge and shadow, in pixels at 1080 lines.
#define WXL_DRAW_MAX_SOFTNESS 15.0f
/// The farthest a shadow is moved, in pixels at 1080 lines, either way.
#define WXL_DRAW_MAX_SHADOW_OFFSET 15.0f
/// The longest texture path, its terminating zero included.
#define WXL_DRAW_MAX_TEXTURE_PATH 260u
/// Particles alive at once, all mods together.
#define WXL_DRAW_MAX_PARTICLES 65536u
/// Particles alive at once of one mod.
#define WXL_DRAW_MAX_MOD_PARTICLES 16384u
/// Emitters a mod holds, kept and thrown.
#define WXL_DRAW_MAX_EMITTERS 256u
/// Particles a burst throws.
#define WXL_DRAW_MAX_BURST 4096u
/// Particles an emitter throws a second.
#define WXL_DRAW_MAX_RATE 4096.0f
/// The longest particle life, in seconds.
#define WXL_DRAW_MAX_PARTICLE_LIFE 60.0f
/// The largest particle, in yards.
#define WXL_DRAW_MAX_PARTICLE_SIZE 50.0f
/// Keys of a particle's life.
#define WXL_DRAW_MAX_PARTICLE_KEYS 4u

/// wxl_draw_shape.flags.
#define WXL_DRAW_FLAT      0x1u ///< a ground shape lies flat at center.z, wherever the ground is
#define WXL_DRAW_ON_GROUND 0x2u ///< a line lies on the ground (its points' z where there is none)
#define WXL_DRAW_CLOSED    0x4u ///< a line joins its last point to its first

/// wxl_draw_style.flags.
#define WXL_DRAW_STYLE_FILL_COLOR 0x1u ///< the inside is drawn in fill_color, not color
#define WXL_DRAW_STYLE_LINE_ALPHA 0x2u ///< a line's opacity goes from alpha_start to alpha_end along it
#define WXL_DRAW_STYLE_MASK       0x4u ///< abi 0.1.40: the texture gives the opacity alone, the colour comes from the style

/// wxl_draw_particles.flags (abi 0.1.40).
#define WXL_DRAW_PARTICLES_MASK 0x1u ///< the texture gives the opacity alone, the colour comes from the keys

/// attach's flags.
#define WXL_DRAW_ATTACH_TURN 0x1u ///< offsets turn with the object's facing (and a shape's facing adds to it)

#ifdef __cplusplus
extern "C" {
#endif

/// What a shape is and where: which fields a kind reads is listed above.
typedef struct wxl_draw_shape
{
    uint32_t        struct_size;
    uint32_t        kind;        ///< wxl_draw_kind
    uint32_t        flags;       ///< WXL_DRAW_FLAT, WXL_DRAW_ON_GROUND, WXL_DRAW_CLOSED
    wxl_vec3        center;
    float           radius;
    float           inner;       ///< ring, sector: the inner radius
    float           angle;       ///< sector: the arc's width in radians
    float           facing;      ///< radians around z, 0 along +x (north), as an object's facing
    float           pitch;       ///< box, cylinder: radians, nose up
    float           roll;        ///< box, cylinder: radians, around the facing
    wxl_vec3        size;        ///< rectangle, grid: x length, y width; box: x, y, z; cylinder: z height
    float           spacing;     ///< grid: yards between two lines
    const wxl_vec3* points;      ///< line, polygon: offsets from center
    uint32_t        point_count;
    // abi 0.1.38
    const float*    point_alphas; ///< line: an opacity per point (0 to 1) multiplying the style's; null for none
} wxl_draw_shape;

/// The size of a wxl_draw_shape before abi 0.1.38, which a runtime still takes.
#define WXL_DRAW_SHAPE_SIZE_0136 ((uint32_t)offsetof(wxl_draw_shape, point_alphas))

/// How a shape looks.
typedef struct wxl_draw_style
{
    uint32_t struct_size;
    uint32_t color;      ///< 0xRRGGBB
    float    alpha;      ///< the lines' opacity, 0 to 1; 0 for none
    float    fill_alpha; ///< the inside's opacity, 0 to 1; 0 for none
    float    width;      ///< the lines' width in pixels at 1080 lines, 0.5 to WXL_DRAW_MAX_WIDTH
    uint32_t depth;      ///< wxl_draw_depth
    float    pulse;      ///< seconds a beat of the opacity; 0 for none
    float    fade_in;    ///< seconds, after add
    float    fade_out;   ///< seconds, at the end of a duration
    // abi 0.1.38: every field left at 0 changes nothing
    uint32_t    flags;           ///< WXL_DRAW_STYLE_FILL_COLOR, WXL_DRAW_STYLE_LINE_ALPHA
    uint32_t    fill_color;      ///< 0xRRGGBB of the inside, under WXL_DRAW_STYLE_FILL_COLOR
    float       softness;        ///< pixels at 1080 lines over which the lines' edges fade, 0 to WXL_DRAW_MAX_SOFTNESS
    float       alpha_start;     ///< a line's opacity at its first point, under WXL_DRAW_STYLE_LINE_ALPHA
    float       alpha_end;       ///< and at its last
    uint32_t    shadow_color;    ///< 0xRRGGBB
    float       shadow_alpha;    ///< the shadow's opacity, 0 to 1; 0 for none
    float       shadow_x;        ///< pixels at 1080 lines to the right, -15 to 15
    float       shadow_y;        ///< pixels at 1080 lines down, -15 to 15
    float       shadow_softness; ///< pixels: the shadow's stroke this much wider, its edges fading over as many
    const char* texture;         ///< the inside's texture, a path the client loads; null or "" for none
    float       texture_right;   ///< the part of the texture its picture fills across, 0 to 1 (0: 1)
    float       texture_bottom;  ///< and down
    uint32_t    uv;              ///< wxl_draw_uv
    float       uv_size_x;       ///< one picture's width: of the bounds (FIT, TILE) or yards (WORLD); 0: 1
    float       uv_size_y;       ///< and its height
    float       uv_rotation;     ///< radians, counter-clockwise seen from above
    float       uv_offset_x;     ///< in pictures
    float       uv_offset_y;
    uint32_t    blend;           ///< wxl_draw_blend: how the inside goes onto the ground
    float       feather;         ///< yards over which the inside fades out towards its border; 0 for none
    float       radial_fade;     ///< 0 to 1: the part of the radius over which the inside fades out; 0 for none
    // abi 0.1.39
    float       occluded_alpha;  ///< 0 to 1: what the scene hides of the shape drawn at this part of its opacity; 0 for none
    uint32_t    reserved0;       ///< 0; keeps this size apart from abi 0.1.38's on 64-bit, whose padding ends where this field does
    // abi 0.1.40
    float       texture_blur;    ///< the picture blurred: each step reads it at half the detail (0 to 8; 0 for none)
    uint32_t    reserved1;       ///< 0; keeps this size apart from abi 0.1.39's on 64-bit
} wxl_draw_style;

/// The size of a wxl_draw_style before abi 0.1.38, which a runtime still takes.
#define WXL_DRAW_STYLE_SIZE_0136 ((uint32_t)offsetof(wxl_draw_style, flags))
/// The size of a wxl_draw_style of abi 0.1.38, which a runtime still takes.
#define WXL_DRAW_STYLE_SIZE_0138 ((uint32_t)offsetof(wxl_draw_style, occluded_alpha))
/// The size of a wxl_draw_style of abi 0.1.39, which a runtime still takes.
#define WXL_DRAW_STYLE_SIZE_0139 ((uint32_t)offsetof(wxl_draw_style, texture_blur))

/// A point of a particle's life: at `t` (0 to 1 of it), its size multiplied by `size`, its colour
/// and opacity; between two keys they go in a line.
typedef struct wxl_draw_particle_key
{
    float    t;
    float    size;  ///< multiplies the particle's own size
    uint32_t color; ///< 0xRRGGBB
    float    alpha; ///< 0 to 1
} wxl_draw_particle_key;

/// Where particles come from and how they move (abi 0.1.40).
typedef struct wxl_draw_emitter
{
    uint32_t struct_size;
    uint32_t spawn;          ///< wxl_draw_spawn
    wxl_vec3 center;         ///< world coordinates; offsets from an object it follows
    wxl_vec3 extent;         ///< disc: x its radius; box: its sides; segment: the offset to its other end
    float    facing;         ///< turns the disc, the box and the direction around z
    wxl_vec3 direction;      ///< the cone's axis; (0, 0, 0) is up
    float    spread;         ///< the cone's half angle in radians: 0 along the axis, pi every way
    uint32_t burst;          ///< particles thrown at once (when added, or by emit)
    float    rate;           ///< a kept emitter's particles a second
    float    duration;       ///< a kept emitter's seconds of rate; 0: until stopped
    float    speed_min, speed_max;   ///< yards a second
    float    life_min, life_max;     ///< seconds
    float    size_min, size_max;     ///< yards across
    float    spin_min, spin_max;     ///< radians a second
    float    gravity;        ///< yards/s^2 downwards
    float    drag;           ///< the part of its speed a particle loses a second
    uint32_t ground;         ///< wxl_draw_ground
    float    restitution;    ///< the part of its fall a bounce gives back, 0 to 1
    float    friction;       ///< the part of its speed along the ground lost at the bounce, and a second while it slides
    wxl_vec3 attractor;      ///< a point particles are pulled towards (world coordinates)
    float    attraction;     ///< yards/s^2 towards it; < 0 away; 0 for none
    float    attract_radius; ///< particles nearer than this to it go
    uint32_t seed;           ///< 0: a new one each time
} wxl_draw_emitter;

/// How particles look (abi 0.1.40).
typedef struct wxl_draw_particles
{
    uint32_t              struct_size;
    uint32_t              mode;           ///< wxl_draw_billboard
    uint32_t              flags;          ///< WXL_DRAW_PARTICLES_MASK
    float                 texture_blur;   ///< as a style's
    const char*           texture;        ///< null or "": a soft round blot
    float                 texture_right;  ///< the part of the texture its picture fills (0: 1)
    float                 texture_bottom;
    uint32_t              frames_x;       ///< a sprite sheet's columns (0 or 1: one picture)
    uint32_t              frames_y;       ///< and rows
    float                 frame_rate;     ///< frames a second; 0: the frames once over a life
    uint32_t              blend;          ///< wxl_draw_blend
    uint32_t              depth;          ///< wxl_draw_depth
    float                 occluded_alpha; ///< as a shape's
    float                 stretch;        ///< stretched: drawn this many seconds of its velocity long
    float                 softness;       ///< yards above the ground over which a particle fades in; 0 for none
    uint32_t              key_count;      ///< 0 to WXL_DRAW_MAX_PARTICLE_KEYS; 0: white, its size, fading out
    wxl_draw_particle_key keys[WXL_DRAW_MAX_PARTICLE_KEYS];
} wxl_draw_particles;

/// What the runtime drew in the last frame of the world, and what it cost.
typedef struct wxl_draw_stats
{
    uint32_t struct_size;
    uint32_t shapes;           ///< kept, all mods
    uint32_t immediate;        ///< immediate shapes the last frame drew
    uint32_t drawn;            ///< shapes the last frame drew, kept and immediate
    uint32_t vertices;         ///< vertices the last frame drew
    uint32_t left_out;         ///< shapes the last frame left out, past WXL_DRAW_MAX_FRAME_VERTICES
    uint32_t rebuilt;          ///< kept shapes built again for the last frame (moved, followed, the ground changed)
    float    build_microseconds;     ///< the CPU's part of the last frame: shapes built, vertices written, draws sent
    float    max_build_microseconds; ///< the most a frame took since the client started
    float    immediate_microseconds; ///< of the last frame's mods, the time their draw calls took
    uint32_t frames;           ///< frames of the world drawn with shapes
    // abi 0.1.38
    uint32_t textured;         ///< insides the last frame drew with a texture, a feather, a fade or a blend
    uint32_t textures;         ///< textures held for shapes, loaded or not
    uint32_t textures_loading; ///< of those, still loading
    uint32_t shadows;          ///< shapes whose lines the last frame drew a shadow under
    // abi 0.1.39
    uint32_t occluded;         ///< shapes the last frame drew where the scene hides them too (occluded_alpha)
    uint32_t reused;           ///< immediate shapes the last frame drew without building them again (as the frame before)
    // abi 0.1.40
    uint32_t particles;          ///< alive after the last frame
    uint32_t particles_emitted;  ///< thrown in the last frame
    uint32_t particles_culled;   ///< not thrown, past a budget, since the frame before
    uint32_t particle_draws;     ///< draw calls of particles in the last frame
    uint32_t emitters;           ///< emitters alive, kept and thrown
    float    particle_microseconds; ///< the CPU's part of the last frame for particles: simulated, sorted, written
} wxl_draw_stats;

typedef struct wxl_draw_api
{
    uint32_t struct_size;

    /// Whether this runtime draws shapes on the device it runs now.
    wxl_bool(WXL_CALL* supported)(void);

    /// Why shapes are not drawn, in a sentence for a log; empty when they are.
    wxl_str(WXL_CALL* unsupported_reason)(void);

    /**
     * @brief Draws a shape in the next frame of the world only.
     * @param style  null: white lines 2 pixels wide, no fill
     *
     * WXL_E_INVALID for a shape or a style the service refuses (logged under `draw`, saying
     * why), or past WXL_DRAW_MAX_IMMEDIATE this frame; WXL_E_UNSUPPORTED where shapes are not
     * drawn.
     */
    wxl_result(WXL_CALL* draw)(const wxl_draw_shape* shape, const wxl_draw_style* style);

    /**
     * @brief Keeps a shape until removed, or for @p duration seconds when it is > 0.
     * @return its handle; WXL_HANDLE_NONE when refused (logged under `draw`)
     */
    wxl_handle(WXL_CALL* add)(const wxl_draw_shape* shape, const wxl_draw_style* style, float duration);

    /// The kept shape becomes another (its kind may change); it stays attached when it is.
    wxl_result(WXL_CALL* update)(wxl_handle shape, const wxl_draw_shape* desc);

    /// The kept shape's center (a line's or a polygon's points are offsets from it) and facing.
    wxl_result(WXL_CALL* move)(wxl_handle shape, wxl_vec3 center, float facing);

    /// The kept shape's look.
    wxl_result(WXL_CALL* set_style)(wxl_handle shape, const wxl_draw_style* style);

    /// The kept shape goes in @p seconds from now (after its fade-out); 0: when removed.
    wxl_result(WXL_CALL* set_duration)(wxl_handle shape, float seconds);

    /**
     * @brief The kept shape follows a world object from now on: its center and points are
     *        offsets from the object's position (turned with its facing under
     *        WXL_DRAW_ATTACH_TURN). It goes when the object leaves view.
     */
    wxl_result(WXL_CALL* attach)(wxl_handle shape, wxl_handle target, uint32_t flags);

    /// The kept shape stays where it is now, in world coordinates.
    wxl_result(WXL_CALL* detach)(wxl_handle shape);

    /// The kept shape goes now.
    void(WXL_CALL* remove)(wxl_handle shape);

    /// Every shape of the calling mod goes, kept and immediate; from abi 0.1.40 its emitters and
    /// their particles too.
    void(WXL_CALL* clear)(void);

    /// What the last frame drew and what it cost; set out->struct_size.
    wxl_result(WXL_CALL* stats)(wxl_draw_stats* out);

    // abi 0.1.40: particles

    /// One burst of `emitter->burst` particles, forgotten once they have lived.
    wxl_result(WXL_CALL* emit)(const wxl_draw_emitter* emitter, const wxl_draw_particles* look);

    /// A kept emitter: its burst now, then its rate; its handle, WXL_HANDLE_NONE when refused.
    wxl_handle(WXL_CALL* add_emitter)(const wxl_draw_emitter* emitter, const wxl_draw_particles* look);

    /// The emitter becomes another; the particles it threw keep how they move.
    wxl_result(WXL_CALL* update_emitter)(wxl_handle emitter, const wxl_draw_emitter* desc);

    /// Its particles' look, those alive included.
    wxl_result(WXL_CALL* set_emitter_style)(wxl_handle emitter, const wxl_draw_particles* look);

    /// Throws @p count more particles now.
    wxl_result(WXL_CALL* burst)(wxl_handle emitter, uint32_t count);

    /// Where it throws from, and its facing; it stops following an object.
    wxl_result(WXL_CALL* place)(wxl_handle emitter, wxl_vec3 center, float facing);

    /// It throws from a world object from now on (center an offset, turned under
    /// WXL_DRAW_ATTACH_TURN); it stops when the object leaves view.
    wxl_result(WXL_CALL* follow)(wxl_handle emitter, wxl_handle target, uint32_t flags);

    /// It stops and goes; its particles live out their lives.
    void(WXL_CALL* stop)(wxl_handle emitter);
} wxl_draw_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_DRAW_H
