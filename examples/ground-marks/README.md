# Ground marks

A Lua mod drawing shapes in the world with `wxl.draw`, on the stock client and the recompiled one,
under Direct3D 9 and Vulkan alike.

- **The target:** a ring under it and a cone in front of it, kept shapes attached to the target
  (`shape:Attach(target, 1)`): they follow it and turn with it, and go when it leaves view. A new
  target takes them off (`shape:Remove()`) and draws them under the new one. The ring is the
  game's own spell circle (`SPELLS\T_VFX_HERO_CIRCLE.BLP`) laid on the ground in gold and added
  as light (`blend = "add"`); the cone's inside fades out towards its border (`feather`) and from
  its tip outwards (`radial_fade`), its outline soft (`softness`).
- **Where the camera looks:** each frame (`OnFrameBegin`), a 10 x 10 yard grid of one-yard cells
  on the ground where the camera's ray meets it, snapped to whole yards: soft gold lines over a
  light black shadow (`shadow_alpha`), its cells multiplied into the dirt (`blend = "modulate"`)
  so that they darken it like a score mark. Drawn with `wxl.draw.Draw`, immediate mode: it is
  drawn by the next frame only, so there is nothing to remove.

Ground shapes (the ring, the cone, the grid) lie on the ground's slopes and follow every edit of
`wxl.terrain`; walls and units hide them. Copy the folder into the game's `Mods` folder.
