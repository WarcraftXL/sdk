#version 460
#extension GL_GOOGLE_include_directive : require
// wxl.outline's edge: over the whole frame, a pixel outside the mask within `width` pixels of it
// takes the outline's colour, blended over the frame by its alpha.
#include "wxl/render/render.glsl"

layout(push_constant) uniform Push
{
    vec4  color;   ///< the outline's colour, its alpha how much it covers
    vec2  pixel;   ///< one pixel in uv
    float width;   ///< pixels
    uint  mask;    ///< the mask's bindless slot
    uint  nearest; ///< a sampler: nearest, clamped
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

float Covered(vec2 at) { return WxlSample(pc.mask, pc.nearest, at).r; }

void main()
{
    const float inside = Covered(uv);
    float near = 0.0;
    // Sixteen directions on two rings: the outline's width and half of it.
    for (int i = 0; i < 16; ++i)
    {
        const float angle = float(i) * 0.39269908;
        const vec2  dir   = vec2(cos(angle), sin(angle)) * pc.pixel;
        near = max(near, Covered(uv + dir * pc.width));
        near = max(near, Covered(uv + dir * pc.width * 0.5));
    }
    const float edge = near * (1.0 - inside);
    outColor = vec4(pc.color.rgb, pc.color.a * edge);
}
