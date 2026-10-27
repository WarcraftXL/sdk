# Dig dust

A Lua mod drawing the particles of a dig with `wxl.draw`, on the stock client and the recompiled
one, under Direct3D 9 and Vulkan alike. Every two seconds, where the camera's ray meets the ground:

- **Clods** (`wxl.draw.Emit`, a burst forgotten once it has lived): thrown up in a cone, falling at
  the game's gravity, bouncing once on the ground (`ground = "bounce"`, `restitution`, `friction`)
  and settling. Their picture is the game's cracks, of which only the shape is taken (`mask`), in the
  colour of earth; they fade where they meet the ground (`softness`).
- **Dust**: a soft round puff (no texture) spreading and fading in half a second.
- **Grains** (`wxl.draw.AddEmitter`, kept for a second then `Stop`ped): thrown on a disc around the
  hole and pulled into it (`attractor`, `attraction`), going as they reach it.

Copy the folder into the game's `Mods` folder.
