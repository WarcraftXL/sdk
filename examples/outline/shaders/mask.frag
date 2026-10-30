#version 460
#extension GL_GOOGLE_include_directive : require
// wxl.outline's mask: a batch of the target drawn again behind its own vertex program, white where
// it is seen. A texel the game cuts out by its alpha (hair, leaves, a cloak's edge) is cut out here too.
#include "wxl/render/batch.glsl"

layout(location = 0) out vec4 outMask;

void main()
{
    if (WxlAlphaRejected(WxlStageSample(0, wxlTexcoord0.xy).a)) discard;
    outMask = vec4(1.0);
}
