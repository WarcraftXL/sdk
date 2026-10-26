# Outline

Outlines the player's target, where it is seen and never through a wall, on the recompiled
client's Vulkan renderer (`[gx] api = vulkan` in `WarcraftXL.ini`). It is built on `wxl.render`
alone: nothing of it is the runtime's own outline (`wxl.highlight`).

- `ModelBatchDrawn`: every batch the game draws of the target's model (its `root_model`) is kept
  for the frame with `KeepBatch`; blended parts are left out.
- `RenderPoint` after the world: the kept batches are drawn again with a batch program
  (`shaders/mask.frag`) into a mask the frame's size and samples, tested against the frame's depth
  (`WXL_RENDER_SCENE_DEPTH`); then a full-screen program (`shaders/edge.frag`) colours the pixels
  around the mask, onto the frame.

The programs are GLSL, compiled to SPIR-V with the Vulkan SDK's `glslangValidator` when CMake finds
it, or taken from `generated/`, made the same way:

    glslangValidator -V --target-env vulkan1.3 -I../../include --vn kOutlineMaskFrag -o generated/mask_frag.h shaders/mask.frag
    glslangValidator -V --target-env vulkan1.3 -I../../include --vn kOutlineEdgeFrag -o generated/edge_frag.h shaders/edge.frag

On a client where `wxl.render` does not draw (the stock client, or Direct3D 9), it logs why once and
does nothing.
