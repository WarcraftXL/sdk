// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The interface of a wxl.render BATCH program: a fragment program drawn behind the vertex program
// of one of the client's model batches (draw_batch). Include it from the mod's GLSL, compiled to
// SPIR-V 1.5 or later for Vulkan 1.3 (glslangValidator -V --target-env vulkan1.3):
//
//     #version 460
//     #extension GL_GOOGLE_include_directive : require
//     #include "wxl/render/batch.glsl"
//     layout(location = 0) out vec4 outColor;
//     void main()
//     {
//         vec4 tint = WxlConstant(0);                       // the mod's first vec4 of constants
//         vec4 diffuse = WxlStageSample(0, wxlTexcoord0.xy); // the batch's first texture
//         if (WxlAlphaRejected(diffuse.a)) discard;          // the batch's own alpha test
//         outColor = vec4(tint.rgb, 1.0);
//     }
//
// What the batch's vertex program hands on (every one writes all of them; (0, 0, 0, 1) where it
// has nothing to say, the colour 0 white): its two colours, its eight texture coordinates, its fog
// factor (x, 1 unfogged), its normal, tangent and binormal (where it computes them).
//
// The push constants are the batch's own (128 bytes): the mod reads its textures, its alpha test,
// and its own constants, the bytes `constants` of draw_batch (rows of vec4), at pc.pixelRegs. Its
// other members are the client's and stay as the batch drew.
//
// The bindless table is wxl.render's: uImages[] and uSamplers[]; a mod's image_slot and sampler
// index them, and so do the batch's stage words (pc.stages[i]: image in the low 16 bits, sampler in
// the high 16).

#ifndef WXL_RENDER_BATCH_GLSL
#define WXL_RENDER_BATCH_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D uImages[];
layout(set = 0, binding = 1) uniform sampler uSamplers[];

layout(push_constant) uniform WxlBatchPush
{
    uvec2 data;       ///< the client's
    uvec2 vertexRegs; ///< the batch's vertex registers (vec4 rows)
    uvec2 pixelRegs;  ///< the mod's constants (vec4 rows)
    float alphaRef;   ///< the batch's alpha test: 1 to 255, 0 for none
    uint  flags;      ///< the client's
    vec4  clipPlane;  ///< the client's
    uint  fogColor;   ///< the client's
    float pointSize;  ///< the client's
    vec2  pointRange; ///< the client's
    uint  stages[16]; ///< the batch's textures: image slot (low 16 bits), sampler (high 16)
} pc;

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer WxlRows
{
    vec4 rows[];
};

layout(location = 0) in vec4 wxlColor0;
layout(location = 1) in vec4 wxlColor1;
layout(location = 2) in vec4 wxlTexcoord0;
layout(location = 3) in vec4 wxlTexcoord1;
layout(location = 4) in vec4 wxlTexcoord2;
layout(location = 5) in vec4 wxlTexcoord3;
layout(location = 6) in vec4 wxlTexcoord4;
layout(location = 7) in vec4 wxlTexcoord5;
layout(location = 8) in vec4 wxlTexcoord6;
layout(location = 9) in vec4 wxlTexcoord7;
layout(location = 10) in vec4 wxlFog;
layout(location = 11) in vec4 wxlNormal;
layout(location = 12) in vec4 wxlTangent;
layout(location = 13) in vec4 wxlBinormal;

/// Row @p i of the mod's constants (draw_batch's `constants`).
vec4 WxlConstant(uint i)
{
    return WxlRows(pc.pixelRegs).rows[i];
}

/// The batch's texture of stage @p stage, sampled with the batch's own sampler.
vec4 WxlStageSample(uint stage, vec2 uv)
{
    const uint word = pc.stages[stage];
    return texture(sampler2D(uImages[nonuniformEXT(word & 0xFFFFu)], uSamplers[nonuniformEXT(word >> 16)]), uv);
}

/// The batch's texture of stage @p stage, sampled with the sampler of index @p sampler_index.
vec4 WxlStageSampleWith(uint stage, uint sampler_index, vec2 uv)
{
    return texture(sampler2D(uImages[nonuniformEXT(pc.stages[stage] & 0xFFFFu)], uSamplers[nonuniformEXT(sampler_index)]), uv);
}

/// The batch's own alpha test (its reference against the 8-bit alpha, as the client tests it):
/// true where the client discarded the fragment.
bool WxlAlphaRejected(float alpha)
{
    return pc.alphaRef > 0.0 && floor(clamp(alpha, 0.0, 1.0) * 255.0 + 0.5) < pc.alphaRef;
}

#endif
