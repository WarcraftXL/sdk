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

layout(set = 0, binding = 0) uniform texture2D uImages[];
layout(set = 0, binding = 1) uniform sampler uSamplers[];

/// The image of bindless index @p image (image_slot, scene_color_slot) through sampler @p sampler_index.
vec4 WxlSample(uint image, uint sampler_index, vec2 uv)
{
    return texture(sampler2D(uImages[nonuniformEXT(image)], uSamplers[nonuniformEXT(sampler_index)]), uv);
}

/// The texel at @p texel of level 0 of the image @p image.
vec4 WxlFetch(uint image, ivec2 texel)
{
    return texelFetch(uImages[nonuniformEXT(image)], texel, 0);
}

#endif
