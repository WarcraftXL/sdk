// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The render service: a mod draws into the client's frame on the runtime's Vulkan renderer, with
// images, buffers and pipelines of its own, its programs in SPIR-V.
//
//   supported, unsupported_reason   whether the client draws through a device this service drives
//   create_image, create_screen_image, destroy_image, image_slot, image_width, image_height
//                                   images: render targets a pass draws into, textures a program samples
//   create_buffer, destroy_buffer, write_buffer, buffer_address
//                                   buffers a program reads by their address (or a draw's indices)
//   sampler                         a sampler's index in the bindless table
//   create_pipeline, destroy_pipeline, pipeline_ready
//                                   the mod's programs with the state they draw with
//   begin, end                      a pass: what draws go into, inside a RenderPoint
//   draw, draw_indexed, fullscreen  the mod's own draws
//   draw_batch, keep_batch          a model batch of the client's drawn again with a program of the mod
//   scene_color_slot, scene_depth_slot
//                                   the frame drawn so far, as textures to sample
//   replace_shader, restore_shader  a client shader's program replaced by the mod's
//
// --- When a mod draws -------------------------------------------------------------------------
//
// Inside the event RenderPoint (Render family), raised at each point of the frame
// (wxl_render_point) while a mod listens: after the opaque world, after the liquids, after the
// whole world, after the interface. There a mod opens a pass (begin), draws, and closes it (end);
// several passes per point if it likes. The frame's own drawing stops for the pass and goes on
// after it, with nothing of the mod's state left behind: the runtime keeps the frame's order, so a
// mod draws over what the frame drew before the point and under what it draws after. Nothing is
// drawn outside a point: a call there answers WXL_E_INVALID. Resources are made and given back at
// any time on the main thread (the mod's load, a handler).
//
// --- Images ----------------------------------------------------------------------------------
//
// An image is a render target and a texture. `create_image` makes one of a fixed size;
// `create_screen_image` one the frame's size divided by `divisor` (1 the full size, 2 half, ...,
// rounded up), made again, its contents lost, when the frame's size changes. `samples` is 1 for
// one sample per pixel, or 0 for the frame's own count (its multisampling): an image a pass draws
// into together with the frame's depth must have the frame's count. A multisampled image is
// resolved (each pixel the mean of its samples) when a pass that drew into it ends; what a program
// samples is the resolved image. A depth image (WXL_RENDER_FORMAT_DEPTH32F) is sampled as one
// channel. Every image starts transparent black (depth 1).
//
// Programs reach images through the bindless table: `image_slot` gives the index of an image in
// `uImages[]`, `sampler` the index of a sampler in `uSamplers[]` (see "Programs" below). The frame
// itself is no image of the mod's: WXL_RENDER_SCENE_COLOR and WXL_RENDER_SCENE_DEPTH name its
// colour and its depth as a pass's attachments (its colour drawn over, its depth tested against),
// and `scene_color_slot` / `scene_depth_slot` copy them, as drawn so far, into textures a program
// samples (made only when a mod asks, once per point).
//
// --- Buffers -----------------------------------------------------------------------------------
//
// A buffer is memory a program reads through its address (`buffer_address`, GLSL's
// GL_EXT_buffer_reference), or the indices of a `draw_indexed` (32-bit). `write_buffer` copies
// bytes into it; the copy reaches the GPU before the frame's first draw, so a buffer a mod changes
// every frame holds the new bytes for every draw of that frame, and the frames before it, still
// on the GPU, read the bytes they were drawn with (the copy waits for them).
//
// --- Programs ----------------------------------------------------------------------------------
//
// A pipeline is the mod's SPIR-V (compiled from GLSL when the mod is built, or shipped as .spv
// files) with the state it draws with: what it draws into (the colour formats, up to 4, the depth
// format, the samples), how it blends, tests depth and culls. Three kinds:
//
//   WXL_RENDER_PIPELINE_GRAPHICS    the mod's vertex and fragment programs. No vertex input: a
//                                   vertex program reads its vertices from buffers, by address
//   WXL_RENDER_PIPELINE_FULLSCREEN  the mod's fragment program over one triangle covering the
//                                   pass; it reads `layout(location = 0) in vec2 uv` (0,0 the
//                                   top-left corner, 1,1 the bottom-right)
//   WXL_RENDER_PIPELINE_BATCH       the mod's fragment program behind the vertex program of a
//                                   client batch (draw_batch): the batch's vertices, skinned,
//                                   placed as the client placed them
//
// Every program sees the same descriptor set: set 0, binding 0 `texture2D uImages[]`, binding 1
// `sampler uSamplers[]`, indexed with nonuniformEXT where the index varies. GRAPHICS and
// FULLSCREEN programs get 128 bytes of push constants, the mod's own (what `push` of a draw
// holds). A BATCH program shares the client's interface instead (the SDK's
// `wxl/render/batch.glsl` declares it): its inputs are the batch's varyings at fixed locations
// (colours 0 and 1, texture coordinates 0 to 7 at locations 2 to 9, ...), its push constants the
// batch's own, among them the batch's textures (`pc.stages[i]`: the bindless image in the low 16
// bits, the sampler in the high 16), its alpha test (`pc.alphaRef`, 0 for none) and the address of
// the mod's constants (`pc.pixelRegs`, the bytes `constants` of draw_batch, as rows of vec4).
// Pipelines are compiled when they are made: `create_pipeline` refuses one the device does not
// take, with a log line saying why. A BATCH pipeline is completed for each vertex program of the
// client it meets, the first time it meets it (a short stall, once).
//
// --- Model batches ------------------------------------------------------------------------------
//
// ModelBatchDrawn hands each model batch the client draws, with `batch`: the draw, valid during
// the event. `draw_batch` draws it again with a BATCH pipeline, into the open pass, where the
// client drew it on the screen (scaled to a pass of another size). `keep_batch` copies it for
// later in the same frame: draw at a later point the batches seen during the world (an outline,
// a silhouette, a glow). A kept batch is valid until the frame ends.
//
// --- Shaders of the client -----------------------------------------------------------------------
//
// `replace_shader` gives the client's shaders whose name starts with `name` (as the client names
// them: "Diffuse_T1", "Ocean", ...) the mod's SPIR-V for `stage` (0 vertex, 1 pixel), written
// against the same interface as a BATCH program (the client's registers at `pc.vertexRegs` and
// `pc.pixelRegs`). The client's own program comes back with `restore_shader`, or when the mod goes.
//
// --- Threads, devices -----------------------------------------------------------------------------
//
// Main thread only. On a device this service does not drive (Direct3D 9: the stock client, or the
// recompiled one without `[gx] api = vulkan`) `supported` answers false, and every call
// WXL_E_UNSUPPORTED (or nothing): the points are still raised there, and a mod draws with wxl.gx's
// device. A mod's resources are given back when it goes, once the GPU is done with them.

#ifndef WXL_SERVICE_RENDER_H
#define WXL_SERVICE_RENDER_H

#include "wxl/abi.h"

#define WXL_RENDER_SERVICE         "wxl.render"
#define WXL_RENDER_SERVICE_VERSION 1

/// The frame's colour and depth, as a pass's attachments (never sampled: see scene_color_slot).
#define WXL_RENDER_SCENE_COLOR ((wxl_handle)1)
#define WXL_RENDER_SCENE_DEPTH ((wxl_handle)2)

/// What image_slot answers for no image.
#define WXL_RENDER_NO_SLOT 0xFFFFFFFFu

/// The most colour attachments of a pass, and bytes of a draw's push constants or a batch's constants.
#define WXL_RENDER_MAX_COLORS    4
#define WXL_RENDER_PUSH_BYTES    128
#define WXL_RENDER_BATCH_BYTES   4096

/// Image formats.
#define WXL_RENDER_FORMAT_RGBA8    1 ///< 8 bits a channel, unsigned normalised
#define WXL_RENDER_FORMAT_BGRA8    2 ///< the frame's own format
#define WXL_RENDER_FORMAT_R8       3
#define WXL_RENDER_FORMAT_RG8      4
#define WXL_RENDER_FORMAT_R16F     5 ///< 16-bit float channels
#define WXL_RENDER_FORMAT_RG16F    6
#define WXL_RENDER_FORMAT_RGBA16F  7
#define WXL_RENDER_FORMAT_R32F     8 ///< 32-bit float channels
#define WXL_RENDER_FORMAT_RGBA32F  9
#define WXL_RENDER_FORMAT_DEPTH32F 10 ///< a depth target, sampled as one float channel

/// Pipeline kinds (see "Programs").
#define WXL_RENDER_PIPELINE_GRAPHICS   0
#define WXL_RENDER_PIPELINE_FULLSCREEN 1
#define WXL_RENDER_PIPELINE_BATCH      2

/// How a colour attachment blends what a program writes (source) with what is there (destination).
#define WXL_RENDER_BLEND_NONE          0 ///< written over
#define WXL_RENDER_BLEND_ALPHA         1 ///< source * a + destination * (1 - a)
#define WXL_RENDER_BLEND_ADD           2 ///< source + destination
#define WXL_RENDER_BLEND_PREMULTIPLIED 3 ///< source + destination * (1 - a)
#define WXL_RENDER_BLEND_MULTIPLY      4 ///< source * destination

/// The depth test: a fragment is kept when its depth compares so with the attachment's.
#define WXL_RENDER_DEPTH_OFF           0 ///< no test
#define WXL_RENDER_DEPTH_LESS          1
#define WXL_RENDER_DEPTH_LESS_EQUAL    2
#define WXL_RENDER_DEPTH_EQUAL         3
#define WXL_RENDER_DEPTH_GREATER       4
#define WXL_RENDER_DEPTH_GREATER_EQUAL 5
#define WXL_RENDER_DEPTH_ALWAYS        6 ///< tested and kept (written, when depth_write)

/// Which faces are not drawn. Faces are front when their corners turn clockwise on the screen,
/// as the client's are.
#define WXL_RENDER_CULL_NONE  0
#define WXL_RENDER_CULL_BACK  1
#define WXL_RENDER_CULL_FRONT 2
#define WXL_RENDER_CULL_BATCH 3 ///< as the client drew the batch (BATCH pipelines)

/// What a GRAPHICS pipeline's vertices make.
#define WXL_RENDER_TOPOLOGY_TRIANGLES      0
#define WXL_RENDER_TOPOLOGY_TRIANGLE_STRIP 1
#define WXL_RENDER_TOPOLOGY_LINES          2
#define WXL_RENDER_TOPOLOGY_POINTS         3

/// sampler's flags: linear filtering (else nearest), wrapping (else clamped to the edge), and
/// linear filtering between mip levels.
#define WXL_RENDER_SAMPLER_LINEAR 1u
#define WXL_RENDER_SAMPLER_WRAP   2u
#define WXL_RENDER_SAMPLER_MIPS   4u

/// begin's clear: bit i clears colour attachment i to clear_colors[i]; this one the depth.
#define WXL_RENDER_CLEAR_DEPTH 0x10u

#ifdef __cplusplus
extern "C" {
#endif

/// A pipeline: the mod's programs and the state they draw with.
typedef struct wxl_render_pipeline_desc
{
    uint32_t    struct_size;
    uint32_t    kind;          ///< WXL_RENDER_PIPELINE_*
    const char* name;          ///< for the log; may be null
    const void* vertex;        ///< SPIR-V of the vertex program, GRAPHICS only (null otherwise)
    uint32_t    vertex_size;   ///< its bytes
    const void* fragment;      ///< SPIR-V of the fragment program
    uint32_t    fragment_size;
    uint32_t    color_count;   ///< colour attachments, 0 to WXL_RENDER_MAX_COLORS
    uint32_t    color_formats[WXL_RENDER_MAX_COLORS]; ///< WXL_RENDER_FORMAT_*; BGRA8 to draw onto the frame
    uint32_t    depth_format;  ///< WXL_RENDER_FORMAT_DEPTH32F, or 0 for none; a pass onto the frame's depth takes any
    uint32_t    samples;       ///< samples per pixel of the attachments: 0 the frame's own, else 1, 2, 4 or 8
    uint32_t    blend[WXL_RENDER_MAX_COLORS]; ///< WXL_RENDER_BLEND_* of each colour attachment
    uint32_t    depth_test;    ///< WXL_RENDER_DEPTH_*
    wxl_bool    depth_write;
    uint32_t    cull;          ///< WXL_RENDER_CULL_*
    uint32_t    topology;      ///< WXL_RENDER_TOPOLOGY_*, GRAPHICS only (a batch keeps its own)
} wxl_render_pipeline_desc;

/// A pass: what the draws until end go into. Every attachment has the same size; the pass covers it.
typedef struct wxl_render_pass
{
    uint32_t   struct_size;
    uint32_t   color_count;                       ///< 0 to WXL_RENDER_MAX_COLORS
    wxl_handle colors[WXL_RENDER_MAX_COLORS];     ///< images, or WXL_RENDER_SCENE_COLOR
    wxl_handle depth;                             ///< an image, WXL_RENDER_SCENE_DEPTH, or 0 for none
    uint32_t   clear;                             ///< bit i: colour i cleared first; WXL_RENDER_CLEAR_DEPTH: the depth
    float      clear_colors[WXL_RENDER_MAX_COLORS][4];
    float      clear_depth;
} wxl_render_pass;

typedef struct wxl_render_api
{
    uint32_t struct_size;

    /// Whether the client draws through a device this service drives: the Vulkan renderer.
    wxl_bool(WXL_CALL* supported)(void);
    /// Why not, in a sentence for a log; empty when it does.
    wxl_str(WXL_CALL* unsupported_reason)(void);

    // --- Images --------------------------------------------------------------------------------

    /// An image of @p width x @p height, @p format, @p samples (0: the frame's). 0 when it cannot be made.
    wxl_handle(WXL_CALL* create_image)(uint32_t width, uint32_t height, uint32_t format, uint32_t samples);
    /// An image of the frame's size divided by @p divisor (at least 1), made again when it changes.
    wxl_handle(WXL_CALL* create_screen_image)(uint32_t divisor, uint32_t format, uint32_t samples);
    /// Gives an image back; it goes once the GPU is done with it.
    void(WXL_CALL* destroy_image)(wxl_handle image);
    /// The image's index in the bindless table (its resolved image when multisampled); WXL_RENDER_NO_SLOT for none.
    uint32_t(WXL_CALL* image_slot)(wxl_handle image);
    uint32_t(WXL_CALL* image_width)(wxl_handle image);
    uint32_t(WXL_CALL* image_height)(wxl_handle image);

    // --- Buffers -------------------------------------------------------------------------------

    /// A buffer of @p size bytes (zeros), read by address or as indices. 0 when it cannot be made.
    wxl_handle(WXL_CALL* create_buffer)(uint32_t size);
    void(WXL_CALL* destroy_buffer)(wxl_handle buffer);
    /// Copies @p size bytes to @p offset of the buffer, before the frame's first draw. WXL_E_INVALID past its end.
    wxl_result(WXL_CALL* write_buffer)(wxl_handle buffer, uint32_t offset, const void* data, uint32_t size);
    /// The buffer's address on the GPU, for a program's buffer reference; 0 for none.
    wxl_result(WXL_CALL* buffer_address)(wxl_handle buffer, uint64_t* address);

    /// The index in the bindless table of a sampler of these WXL_RENDER_SAMPLER_* flags.
    uint32_t(WXL_CALL* sampler)(uint32_t flags);

    // --- Pipelines -----------------------------------------------------------------------------

    /// A pipeline of @p desc, compiled now. 0 when the device refuses it (the log says why).
    wxl_handle(WXL_CALL* create_pipeline)(const wxl_render_pipeline_desc* desc);
    void(WXL_CALL* destroy_pipeline)(wxl_handle pipeline);
    /// Whether the pipeline draws now.
    wxl_bool(WXL_CALL* pipeline_ready)(wxl_handle pipeline);

    // --- Drawing, inside RenderPoint ----------------------------------------------------------------

    /// Opens a pass. WXL_E_INVALID outside a point, with a pass open, or with attachments of different sizes.
    wxl_result(WXL_CALL* begin)(const wxl_render_pass* pass);
    /// Closes the pass; the frame goes on.
    void(WXL_CALL* end)(void);
    /// @p vertex_count vertices, @p instance_count times, with @p push (up to 128 bytes) as push constants.
    wxl_result(WXL_CALL* draw)(wxl_handle pipeline, uint32_t vertex_count, uint32_t instance_count, const void* push, uint32_t push_size);
    /// @p index_count 32-bit indices of @p indices from @p first_index.
    wxl_result(WXL_CALL* draw_indexed)(wxl_handle pipeline, wxl_handle indices, uint32_t first_index, uint32_t index_count,
                                       uint32_t instance_count, const void* push, uint32_t push_size);
    /// A FULLSCREEN pipeline over the pass.
    wxl_result(WXL_CALL* fullscreen)(wxl_handle pipeline, const void* push, uint32_t push_size);
    /**
     * @brief A client batch (ModelBatchDrawn's `batch`, or keep_batch's copy) drawn again with a
     *        BATCH pipeline, @p constants (up to WXL_RENDER_BATCH_BYTES, rows of vec4) at
     *        `pc.pixelRegs`. WXL_E_NOT_FOUND for a batch of another frame.
     */
    wxl_result(WXL_CALL* draw_batch)(const void* batch, wxl_handle pipeline, const void* constants, uint32_t constants_size);
    /// A copy of the batch, valid until the frame ends; null when none can be kept (4096 a frame).
    void*(WXL_CALL* keep_batch)(const void* batch);

    /// The frame's colour as drawn so far (resolved), copied into a texture: its bindless index,
    /// WXL_RENDER_NO_SLOT outside a point.
    uint32_t(WXL_CALL* scene_color_slot)(void);
    /// The frame's depth as drawn so far (its first sample when multisampled), as one float channel.
    uint32_t(WXL_CALL* scene_depth_slot)(void);

    // --- The client's shaders -----------------------------------------------------------------

    /// The client's shaders whose name starts with @p name draw @p stage (0 vertex, 1 pixel) with
    /// @p spirv until restored. WXL_E_INVALID for SPIR-V the device refuses.
    wxl_result(WXL_CALL* replace_shader)(wxl_str name, uint32_t stage, const void* spirv, uint32_t size);
    /// Takes the mod's replacement for @p name and @p stage back. WXL_E_NOT_FOUND for none.
    wxl_result(WXL_CALL* restore_shader)(wxl_str name, uint32_t stage);
} wxl_render_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_RENDER_H
