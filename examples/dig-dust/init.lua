-- dig-dust: particles of a dig, with wxl.draw. Every two seconds, where the camera looks: clods of
-- earth thrown up that bounce once and settle (a burst, forgotten), a soft dust puff that spreads
-- and fades in half a second, then grains drawn back into the hole as it is filled (a kept
-- emitter with an attractor, stopped a second later).

local draw, camera, terrain = wxl.draw, wxl.camera, wxl.terrain

if not draw.Supported() then
    print("dig dust is off: " .. draw.UnsupportedReason())
    return
end

-- Their looks: earth-coloured pictures of the game's cracks, only their shape taken (mask); a soft
-- blot of dust; small bright grains.
local clodLook = { texture = "SPELLS\\CrackedGround.blp", mask = true, softness = 0.15,
                   keys = { { t = 0, size = 1, color = 0x6B5235, alpha = 1 }, { t = 0.8, size = 1, color = 0x5A4430, alpha = 1 },
                            { t = 1, size = 0.6, color = 0x5A4430, alpha = 0 } } }
local dustLook = { softness = 0.4,
                   keys = { { t = 0, size = 0.6, color = 0x9A8A70, alpha = 0.45 }, { t = 1, size = 2.6, color = 0x8A7A60, alpha = 0 } } }
local grainLook = { keys = { { t = 0, size = 1, color = 0xC8B48C, alpha = 0 }, { t = 0.2, size = 1, color = 0xC8B48C, alpha = 0.9 },
                             { t = 1, size = 0.5, color = 0xC8B48C, alpha = 0.6 } } }

local function dig(at)
    -- Clods: thrown up in a cone, falling at the game's gravity, one bounce, then at rest.
    draw.Emit({ center = { x = at.x, y = at.y, z = at.z + 0.05 }, burst = 14, spread = 0.7, speed = { 3, 6 },
                life = { 1.0, 1.6 }, size = { 0.12, 0.28 }, spin = { -6, 6 }, gravity = 19.29, drag = 0.2,
                ground = "bounce", restitution = 0.35, friction = 0.6 }, clodLook)
    -- Dust: a soft puff spreading out and fading over half a second.
    draw.Emit({ spawn = "disc", center = at, extent = { x = 0.4, y = 0, z = 0 }, burst = 10, spread = 1.2, speed = { 0.4, 1.2 },
                life = { 0.4, 0.6 }, size = { 0.5, 0.9 }, drag = 2.5 }, dustLook)
end

local function fill(at)
    -- Grains: thrown on a disc around the hole, pulled into it, going as they reach it.
    local grains = draw.AddEmitter({ spawn = "disc", center = { x = at.x, y = at.y, z = at.z + 0.1 }, extent = { x = 1.5, y = 0, z = 0 },
                                     rate = 80, speed = 0, life = 2, size = { 0.04, 0.08 }, drag = 1.5,
                                     attractor = at, attraction = 9, attract_radius = 0.15 }, grainLook)
    return grains
end

local next, filling, stopAt = 0, nil, 0

Observe("RenderScript", "OnFrameBegin", function()
    if not (camera and terrain) then return end
    local now = os.clock()
    if filling and now >= stopAt then
        filling:Stop()
        filling = nil
    end
    if now < next then return end
    next = now + 2
    local eye, forward = camera.Position(), camera.Forward()
    if not (eye and forward) then return end
    local hit = terrain.Trace(eye, { x = eye.x + forward.x * 40, y = eye.y + forward.y * 40, z = eye.z + forward.z * 40 })
    if not hit then return end
    dig(hit.point)
    filling = fill(hit.point)
    stopAt  = now + 1
end)
