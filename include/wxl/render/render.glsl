// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The interface of a wxl.render GRAPHICS or FULLSCREEN program: the bindless table. The push
// constants (up to 128 bytes) are the mod's own: declare them as the program needs.
//
//     #version 460
//     #extension GL_GOOGLE_include_directive : require
//     #include "wxl/render/render.glsl"
//     layout(push_constant) uniform Push { vec4 tint; uint image; uint sampler_index; } pc;
//     layout(location = 0) in vec2 uv;        // a FULLSCREEN program's input
//     layout(location = 0) out vec4 outColor;
//     void main() { outColor = WxlSample(pc.image, pc.sampler_index, uv) * pc.tint; }

#ifndef WXL_RENDER_RENDER_GLSL
#define WXL_RENDER_RENDER_GLSL

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require

layout(set = 0, binding = 0) uniform texture2D uImages[];
layout(set = 0, binding = 1) uniform sampler uSamplers[];

/// The image of bindless index @p image (image_slot, scene_color_slot) through sampler @p sampler_index.
vec4 WxlSample(uint image, uint sampler_index, vec2 uv)
{
    return texture(sampler2D(uImages[nonuniformEXT(image)], uSamplers[nonuniformEXT(sampler_index)]), uv);
}

#ifndef WXL_RENDER_ROWS
#define WXL_RENDER_ROWS
/// vec4 rows at an address: a buffer's (buffer_address) or a bone palette's (bone_palette).
layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer WxlRows
{
    vec4 rows[];
};
#endif

/// Row @p row (0, 1 or 2) of matrix @p matrix of a bone palette at @p palette (bone_palette's
/// address, as two 32-bit words, low first).
vec4 WxlPaletteRow(uvec2 palette, uint matrix, uint row)
{
    return WxlRows(palette).rows[3u * matrix + row];
}

/// The texel at @p texel of level 0 of the image @p image.
vec4 WxlFetch(uint image, ivec2 texel)
{
    return texelFetch(uImages[nonuniformEXT(image)], texel, 0);
}

#endif
