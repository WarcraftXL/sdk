-- ground-marks: shapes drawn in the world with wxl.draw. A spell's targeting ring under the target
-- and a cone in front of it, which follow it and turn with it (kept shapes, attached to the
-- target); and, each frame, a grid scored into the ground where the camera looks (immediate mode:
-- nothing to remove): soft gold lines over a light shadow, the cells darkened as the dirt is.

local draw = wxl.draw

if not draw.Supported() then
    print("ground marks are off: " .. draw.UnsupportedReason())
    return
end

-- --- the target: kept shapes, attached ---------------------------------------------------------

local marks = {}

local function clearMarks()
    for _, shape in ipairs(marks) do shape:Remove() end
    marks = {}
end

local function mark(target)
    clearMarks()
    if not target then return end
    -- Offsets from the target: the ring around its feet, the cone ahead of it. Flag 1
    -- (ATTACH_TURN) turns them with the target's facing.
    -- The ring is the game's own spell circle picture, laid on the ground and added as light.
    local ring = draw.Add({ kind = "ring", radius = 2, inner = 1.2 },
                          { alpha = 0, fill_alpha = 1, fill_color = 0xFFD040, texture = "SPELLS\\T_VFX_HERO_CIRCLE.BLP", blend = "add" })
    -- The cone fades out towards its border and from its tip outwards.
    local cone = draw.Add({ kind = "sector", radius = 8, angle = math.rad(70), inner = 1.6 },
                          { color = 0xFF3020, alpha = 0.9, fill_alpha = 0.3, width = 2, softness = 1, pulse = 1.2,
                            feather = 1.5, radial_fade = 0.6 })
    for _, shape in ipairs({ ring, cone }) do
        if shape and shape:Attach(target, 1) then marks[#marks + 1] = shape end
    end
end

Observe("ObjectScript", "OnTargetChanged", function(previous, current) mark(current) end)

-- --- a grid where the camera looks: immediate mode, every frame --------------------------------

local camera, terrain = wxl.camera, wxl.terrain

Observe("RenderScript", "OnFrameBegin", function()
    if not (camera and terrain) then return end
    local eye, forward = camera.Position(), camera.Forward()
    if not (eye and forward) then return end
    local far = { x = eye.x + forward.x * 60, y = eye.y + forward.y * 60, z = eye.z + forward.z * 60 }
    local hit = terrain.Trace(eye, far)
    if not hit then return end
    -- Snapped to whole yards, so that the grid stands still as the aim moves inside a cell.
    local at = { x = math.floor(hit.point.x + 0.5), y = math.floor(hit.point.y + 0.5), z = hit.point.z }
    -- Soft gold lines over a light black shadow; the inside multiplied into the ground (a dark
    -- brown at a quarter darkens the dirt like a score mark), its border soft.
    draw.Draw({ kind = "grid", center = at, length = 10, width = 10, spacing = 1 },
              { color = 0xE8C060, alpha = 0.85, width = 1.5, softness = 1, shadow_alpha = 0.45,
                fill_alpha = 0.25, fill_color = 0x605040, blend = "modulate", feather = 0.5 })
end)
