-- selftest: the in-game checks a person would otherwise type one by one. On entering the world it
-- runs, from the client's own Lua, everything below in order, without a keystroke:
--
--   1. calls every function the test mods register and checks each answer;
--   2. whispers the player, sends an addon message and changes a CVar, and checks the events;
--   3. hot reload: makes wxl.test-reload rewrite a file of its own and checks it reloaded; checks
--      that wxl.test-order-a still runs before wxl.test-order-b after it reloads; breaks
--      wxl.test-reload's mod.json (the mod unloads) and has wxl.test-manifest repair it (it loads);
--   4. provokes events a session rarely shows (PROVOKE): a cast refused, a cast cancelled, a
--      racial's cooldown, a hearthstone use refused, an item taken off and put back on;
--   5. reloads the interface and calls the functions again;
--   6. logs out and back in with the same character (RELOG) and calls them again;
--   7. checks the engine points; 8. subscribes every gameplay event, provokes a /wave and reads
--      the inventory and aura events phase 4 caused;
--   9. reads the player, its target and an item it wears through wxl.object and compares each
--      with what the client's Lua says, then has the player target itself and clears the target;
--  10. the asset pipeline: bytes it serves read back through the client, a file wxl.test-assets
--      provides and converts, the add-on it provides that the client loaded, and the engine's
--      asset events seen since the start;
--  11. models: the loader's events and the appearance changes phase 4 caused, then wxl.m2 hangs a
--      murloc on the player's head and takes it off again, and ModelBatchDrawn counts the batches
--      drawn meanwhile, for two seconds;
--  12. the model loader's points: wxl.test-points attaches to CM2Shared::FinishLoading and to
--      M2Init (inlined in the stock client into FinishLoading and FinishLoadingLowPrioritySequence,
--      de-inlined by its runtime from both), models the starting zones rarely show are asked for,
--      and both handlers must fire, M2Init once in each FinishLoading and once per sequence read
--      later, and read the models through the engine headers while the models still parse;
--  13. the asset bricks: FileMissing for a texture nobody has, a file served under a path of its
--      own, an override, content hashes (served bytes, an archive's file, asynchronously), textures
--      and the player's model read again, a service of its own through wxl.relay, then
--      wxl.test-assets' downloader: a pending texture fetched on its thread, served under a path the
--      selftest chose, put in place by an override, a request cancelled;
--  14. the camera: wxl.camera places it ten yards above the head for two seconds while wxl.input
--      holds the pointer, then gives both back; a trace meets the ground under the player,
--      IsOutdoors agrees with the client's IsIndoors, and wxl.action.Face turns the player and back;
--  15. the mouse look kept out: MouselookStart while wxl.input blocks it leaves the game out of
--      its mouse look, and works again once the block is given back; wxl.pose turns the player's
--      head for a second and gives it back; ImGuiFrame is raised by the runtime;
--  16. models of the world: wxl.m2 makes a murloc in the world's scene and places it on the
--      ground three yards in front of the player, plays its animation, fades it, reads where the
--      player's head is in the world, then takes it away; wxl.test-bones watches the bones of the
--      character models for a second; the casts seen since the start say where a ground spell
--      landed;
--  17. reticle targeting's bricks: the runtime raises WorldClickRequest; CanAttack agrees with the
--      client's UnitCanAttack for the nearest unit (targeted for the question, then the target is
--      given back); CanInteract and a game object's CanUse answer for everything in view.
--  18. a highlight: wxl.highlight outlines the player for two seconds where the runtime draws
--      highlights (either client on Vulkan, the stock client on D3D9), and the batches drawn again
--      are counted; elsewhere it must say why it draws none, and refuse as unsupported.
--  19. the game's selection circles: the nearest unit is targeted, its circle counted drawn, then
--      hidden by wxl.selectioncircle, drawn larger and blue for two seconds, and given back; the
--      target the player had comes back after.
--  20. a rolled camera and a pointer held off the centre: wxl.camera draws the client's own view
--      rolled 0.3 rad (the horizon leans) for two seconds and reads the roll back, wxl.input holds
--      the pointer a tenth of the screen above the centre and reads the point back, then both are
--      given back and the camera draws level again.
--  21. textures from SVG and PNG: wxl.texture makes a reticle of the selftest's own SVG at 64
--      pixels wide, red with a darker outline twice as wide, and a disc of its PNG; a frame in
--      the middle of the screen shows them for two seconds each through SetTexture, the
--      interface's WxlTexture answers the same path for the same request, another colour and an
--      outline taken off give new paths, and a missing file and a group on a PNG are refused.
--  22. console variables: wxl.cvar registers one saved in Config.wtf and ranged, sets it and the
--      interface's GetCVar reads it back; the interface's SetCVar changes it and OnCVarChanged
--      follows, a value outside the range is refused, a reset brings the default back; a read-only
--      one refuses SetCVar; the interface's WxlRegisterCVar registers one of its own. The value
--      the last launch saved (in Config.wtf, and the character's) must come back: a NOTE, since
--      the first launch with this phase has none, and a launch that did not end cleanly saved none.
--  23. the interface's signatures: every InterfaceSignature since the mod loaded; FrameXML.toc
--      must have been judged (with Bindings.xml), and its verdict is printed, as GlueXML.toc's and
--      each add-on's; a verdict short of signed with the world up means a mod took it as signed
--      (wxl.glue-unlock with a changed FrameXML), a NOTE. The clearing of loose folders was asked
--      once, at the login screens.
--  24. drawing into the frame: wxl.gx names the device; wxl.render draws on the Vulkan renderer,
--      on either client (on Direct3D 9 it must say why not): a screen image is made and read, a
--      full-screen pipeline of the selftest's own SPIR-V tints the frame after the world for two
--      seconds, the four points of the frame come, a model batch is kept from ModelBatchDrawn,
--      and a GPU zone is opened and closed around the tint.
--  25. compute and bone palettes: on the Vulkan renderer (either client) a compute dispatch fills
--      a buffer the frame then shows orange for two seconds, and a palette of 300 matrices shows
--      its last row green for two; on Direct3D 9 both are refused as unsupported.
--  26. the ground: wxl.terrain answers the height under the player, the chunks loaded since the
--      mod loaded were announced, then a brush of the vertices within three yards of a point six
--      yards north (VerticesIn) is lowered three yards for two seconds: wxl.terrain's height and
--      trace and the client's own ray test (wxl.world.Trace) meet the dip, the original height
--      stays readable, and a reset gives the ground back.
--
-- Each check is one line in the log: PASS, FAIL, SKIP (what it needs is not there: a class
-- without a cast-time spell, say) or NOTE (an event the server may not send). The chat box shows
-- the totals at the end.

-- What the run does besides calling functions. Off, the part is skipped with a SKIP line.
local PROVOKE = true -- casts, a hearthstone use, an item taken off and put back on
local RELOG   = true -- logs out and back in at the end: do not move during the logout

local MARKER = "wxl selftest"

-- --- what each call must answer ----------------------------------------------------------
-- A check takes the call's pcall result and returns true, or false and what was expected.

local function equals(want) return function(ok, a) return ok and a == want, string.format("%q", want) end end
local function contains(want)
    return function(ok, a) return ok and type(a) == "string" and a:find(want, 1, true) ~= nil, "text with " .. want end
end
local function number(ok, a) return ok and tonumber(a) ~= nil, "a number" end
local function fails(want)
    return function(ok, a) return not ok and tostring(a):find(want, 1, true) ~= nil, "an error with " .. want end
end
local function answers(ok) return ok, "an answer, not an error" end

-- name, the client Lua that calls it, its check.
local CALLS = {
    { "ScoutCount", "return ScoutCount()", number },
    { "ScoutEntry", "return ScoutEntry()", answers },
    { "ScoutNearby", "return ScoutNearby()", number },
    { "ScoutTraffic received", "return ScoutTraffic(0xA9)", number },
    { "ScoutTraffic sent", "return ScoutTraffic(0x95, 'sent')", number },
    { "ScoutEntry(0) refused", "return ScoutEntry(0)", fails("") },
    { "ScoutDrop on", "return ScoutDrop(0x105, true)", answers },
    { "ScoutDrop off", "return ScoutDrop(0x105, false)", answers },
    { "LuaServicesGreet", "return LuaServicesGreet('you')", equals("hello you, from Lua services") },
    { "LuaServicesGreet()", "return LuaServicesGreet()", equals("hello nil, from Lua services") },
    { "TestThrow contained", "return TestThrow()", fails("thrown on purpose from TestThrow") },
    { "TestFields", "return TestFields()", contains("Health=") },
    { "TestSearchRemove", "return TestSearchRemove()", contains("RemoveSearchPath: true") },
    { "TestSearchAdd", "return TestSearchAdd()", contains("AddSearchPath: true") },
    { "MemoryReport", "return MemoryReport()", answers },
    { "WxlProbeStep", "return WxlProbeStep()", answers },
}

-- After a /reload, and after logging back in: the functions are registered again in the new
-- interface.
local AGAIN = {
    { "LuaServicesGreet", "return LuaServicesGreet('you')", equals("hello you, from Lua services") },
    { "TestThrow", "return TestThrow()", fails("thrown on purpose from TestThrow") },
    { "ScoutCount", "return ScoutCount()", number },
    { "TestFieldsHealth", "return TestFieldsHealth()", answers },
    { "TestReloadPart", "return TestReloadPart()", answers },
    { "TestOrderMark", "return TestOrderMark()", answers },
}

-- The first rank of a spell with a cast time that a class has from level 1, cast on the player:
-- Holy Light, Lesser Heal, Healing Wave, Healing Touch, Summon Imp.
local CAST_SPELLS = { 635, 2050, 331, 5185, 688 }

-- Racials with a cooldown that only touch the player: Every Man for Himself, Stoneform, Escape
-- Artist, Blood Fury (three), Will of the Forsaken, Berserking, Gift of the Naaru (one per class).
-- War Stomp, Arcane Torrent and Shadowmeld are left out: they reach the units around, or hide
-- the player.
local RACIALS = { 59752, 20594, 20589, 20572, 33697, 33702, 7744, 26297,
                  28880, 59542, 59543, 59544, 59545, 59547, 59548 }

local HEARTHSTONE, HEARTHSTONE_SPELL = 6948, 8690

-- The equipment slots tried, in order: shirt, feet, wrists, waist, legs, chest.
local SLOTS = { 4, 8, 9, 6, 7, 5 }

-- --- results ---------------------------------------------------------------------------------

local passed, failed, skipped = 0, 0, 0

local function result(name, ok, detail)
    if ok then passed = passed + 1 else failed = failed + 1 end
    print(string.format("%s %s%s", ok and "PASS" or "FAIL", name, detail and (": " .. detail) or ""))
end

local function skip(name, why)
    skipped = skipped + 1
    print("SKIP " .. name .. ": " .. why)
end

-- An event the server may or may not send: seen is a PASS, unseen a NOTE, never a FAIL.
local function note(name, seen, why)
    if seen then
        result(name, true, "raised")
    else
        print("NOTE " .. name .. ": not raised; " .. why)
    end
end

-- --- the client's Lua ------------------------------------------------------------------------

local answered = {}

-- The client's Lua answers each call here: a name, whether pcall succeeded, and two values.
wxl.script.RegisterFunction("WxlSelfTestAnswer", function(name, ok, a, b)
    answered[name] = { ok = ok, a = a, b = b }
end, "game")

local function run(code) wxl.script.Run(code, "wxl.selftest") end

-- Runs `code` in the client's Lua and answers what it returned: true and its first two values,
-- or false and the error. In the world only: the login screens have no WxlSelfTestAnswer.
local function ask(code)
    answered.ask = nil
    run(string.format("WxlSelfTestAnswer('ask', pcall(loadstring(%q)))", code))
    local got = answered.ask
    if not got then return false, "the client never answered" end
    return got.ok, got.a, got.b
end

local function chat(text)
    run("DEFAULT_CHAT_FRAME:AddMessage(" .. string.format("%q", "|cff40ff40wxl.selftest|r " .. text) .. ")")
end

local function call(list, suffix)
    for _, entry in ipairs(list) do
        local ok, a, b = ask(entry[2])
        local good, expected = entry[3](ok, a, b)
        local got = (ok and "answered " or "failed with ") .. tostring(a) .. (b ~= nil and (", " .. tostring(b)) or "")
        result(entry[1] .. (suffix or ""), good, good and got or (got .. "; expected " .. expected))
    end
end

-- --- the events, as the runtime reports them ---------------------------------------------------

-- Event name -> the argument lists seen since `watch(name)`; an event not watched keeps nothing.
local seen = {}

local function watch(...)
    for i = 1, select("#", ...) do seen[(select(i, ...))] = {} end
end

-- Whether `name` came since it was watched, with arguments `test` accepts when given.
local function saw(name, test)
    for _, args in ipairs(seen[name] or {}) do
        if not test or test(args) then return true end
    end
    return false
end

local function keep(family, name)
    Observe(family, "On" .. name, function(...)
        local list = seen[name]
        if list then list[#list + 1] = { ... } end
    end)
end

for family, names in pairs({
    ChatScript   = { "ChatAddonMessage", "ChatSend", "ChatMessage" },
    WorldScript  = { "CVarChanged", "WorldEnter", "WorldLeave" },
    ObjectScript = { "EquipmentChanged", "SpellCastStart", "SpellCastGo", "SpellCastFailed",
                     "SpellCastInterrupted", "SpellCastCancel", "SpellCooldown", "ItemUseRequest",
                     "SpellCastRequest" },
    GlueScript   = { "GlueScreenChanged", "GlueCharacterList", "GlueEnterWorld" },
}) do
    for _, name in ipairs(names) do keep(family, name) end
end
-- The login's own entry into the world, which phase 5 checks.
watch("WorldEnter", "WorldLeave")

-- The gameplay events phase 8 checks, kept from the phases that provoke them.
for family, names in pairs({
    ObjectScript = { "UnitAuraApplied" },
    ItemScript   = { "InventorySlotChanged" },
    SocialScript = { "Emote", "TextEmote" },
}) do
    for _, name in ipairs(names) do keep(family, name) end
end

-- The engine's asset events phase 10 checks: counted from the start, with the first one's
-- arguments as text (a tile's bytes are megabytes: they are not kept).
local ASSET_EVENTS = { "TerrainTileLoad", "MapObjectGroupLoad", "SkinProfileLoad", "TextureDecode", "ShaderLoad",
                       "SoundFileLoad" }
local assetCount, assetFirst = {}, {}
for _, name in ipairs(ASSET_EVENTS) do
    assetCount[name] = 0
    Observe("AssetScript", "On" .. name, function(...)
        assetCount[name] = assetCount[name] + 1
        if assetFirst[name] then return end
        local shown = {}
        for i = 1, select("#", ...) do
            local v = select(i, ...)
            shown[#shown + 1] = type(v) == "string" and #v > 64 and ("<" .. #v .. " bytes>") or tostring(v)
        end
        assetFirst[name] = table.concat(shown, ", ")
    end)
end

-- The model events phase 11 checks: counted from the start (the loader's stages, the
-- appearances phase 4 changed), with the first appearance change as text.
local MODEL_EVENTS = { AppearanceChanged = "ObjectScript", ModelParsed = "AssetScript",
                       SkinProfileParsed = "AssetScript", ModelSequenceLoad = "AssetScript" }
local modelCount, firstAppearance = {}, nil
for name, family in pairs(MODEL_EVENTS) do
    modelCount[name] = 0
    Observe(family, "On" .. name, function(...)
        modelCount[name] = modelCount[name] + 1
        if name == "AppearanceChanged" and not firstAppearance then
            local change, _, _, race, _, slot, display = ...
            firstAppearance = string.format("%s slot %d display %d race %d", tostring(change), slot, display, race)
        end
    end)
end

-- Any argument equal to `text`.
local function carries(text)
    return function(args)
        for i = 1, table.maxn(args) do
            if tostring(args[i]) == text then return true end
        end
        return false
    end
end

-- The spell id at argument `index` is `id`.
local function spell(index, id) return function(args) return args[index] == id end end

-- What the run refuses while a check is armed: a spell id, and any item use.
local refuseSpell, refuseItems = nil, false

Override("ObjectScript", "OnSpellCastRequest", function(caster, spell_id)
    if refuseSpell and spell_id == refuseSpell then return true end
    return nil
end)

Override("ObjectScript", "OnItemUseRequest", function()
    if refuseItems then return true end
    return nil
end)

-- --- the run: a queue of steps, one per tick at most -----------------------------------------

local steps, cursor = {}, 1
local delay = 0
local condition, deadline, poll, onTimeout

local function step(fn) steps[#steps + 1] = fn end

-- The next step waits `seconds`.
local function wait(seconds) delay = seconds end

-- The next step waits until `test()` is true, asked every quarter second, or `seconds` have
-- passed; then `timeout()` runs, when given.
local function waitFor(test, seconds, timeout)
    condition, deadline, poll, onTimeout = test, seconds, 0, timeout
end

local entered, started = 0, false

Observe("LuaScript", "OnUiEvent", function(_, name)
    if name ~= "PLAYER_ENTERING_WORLD" then return end
    entered = entered + 1
    if not started then
        started = true
        wait(3)
    end
end)

registerForEvent("onUpdate", function(delta)
    if not started then return end
    if condition then
        deadline = deadline - delta
        poll = poll - delta
        if poll > 0 and deadline > 0 then return end
        poll = 0.25
        if condition() then
            condition = nil
        elseif deadline <= 0 then
            condition = nil
            if onTimeout then onTimeout() end
        else
            return
        end
    end
    if delay > 0 then
        delay = delay - delta
        if delay > 0 then return end
    end
    local fn = steps[cursor]
    if not fn then return end
    cursor = cursor + 1
    fn()
end)

-- 1. The functions -----------------------------------------------------------------------------

step(function()
    chat("running: about two minutes; the interface reloads once and the character logs out " ..
         "and back in by itself, do not move")
    print("phase 1: the functions the test mods register")
    call(CALLS)
end)

-- 2. Chat, addon message, CVar ----------------------------------------------------------------

step(function()
    print("phase 2: chat, an addon message and a CVar")
    watch("ChatAddonMessage", "ChatSend", "ChatMessage", "CVarChanged")
    run([[
local me = UnitName("player")
SendAddonMessage("WXL", "ping wxl selftest", "WHISPER", me)
SendChatMessage("wxl selftest", "WHISPER", nil, me)
local farclip = GetCVar("farclip")
SetCVar("farclip", farclip == "500" and "600" or "500")
SetCVar("farclip", farclip)
]])
    wait(5)
end)

step(function()
    result("event ChatAddonMessage", saw("ChatAddonMessage", carries("ping " .. MARKER)))
    result("event ChatSend", saw("ChatSend", carries(MARKER)))
    result("event ChatMessage", saw("ChatMessage", carries(MARKER)))
    result("event CVarChanged", saw("CVarChanged", carries("farclip")))
end)

-- 3. Hot reload -----------------------------------------------------------------------------------

local function part() return ask("return TestReloadPart()") end

local originalPart, newPart

step(function()
    print("phase 3: hot reload")
    local ok, current = part()
    if not ok then
        result("TestReloadPart", false, "failed with " .. tostring(current) .. "; is wxl.test-reload in Mods/?")
        return
    end
    originalPart, newPart = current, "part " .. os.time()
    local written, text = ask(string.format("return TestReloadWrite(%q)", newPart))
    result("TestReloadWrite", written and text == newPart, tostring(text))
    waitFor(function()
        local loaded, now = part()
        return loaded and now == newPart
    end, 5)
end)

step(function()
    if not newPart then return end
    local ok, now = part()
    result("reload after a file of its own changed", ok and now == newPart,
           "lib/part.lua now gives " .. tostring(now) .. ((ok and now == newPart) and "" or (", expected " .. newPart)))
end)

-- Order: wxl.test-order-a answers "wxl order <n>" before wxl.test-order-b sees it.
local function order(n)
    run(string.format("SendChatMessage(%q, 'WHISPER', nil, UnitName('player'))", "wxl order " .. n))
end

local markBefore, markAfter

step(function()
    order(1)
    local ok, first = ask("return TestOrderSeen(1)")
    result("handler order: wxl.test-order-a before wxl.test-order-b", ok and first == true,
           ok and ("b saw a's answer: " .. tostring(first)) or tostring(first))
    local known, touched
    known, markBefore = ask("return TestOrderMark()")
    touched, markAfter = ask(string.format("return TestOrderTouch(%q)", "a" .. os.time()))
    if not (known and touched) then
        result("TestOrderTouch", false, tostring(markAfter))
        markAfter = nil
        return
    end
    waitFor(function()
        local loaded, now = ask("return TestOrderMark()")
        return loaded and now == markAfter
    end, 5)
end)

step(function()
    if not markAfter then return end
    local ok, now = ask("return TestOrderMark()")
    result("wxl.test-order-a reloaded after mark.lua changed", ok and now == markAfter,
           tostring(markBefore) .. " -> " .. tostring(now))
    order(2)
    local seenOk, second = ask("return TestOrderSeen(2)")
    result("handler order kept after wxl.test-order-a reloaded", seenOk and second == true,
           seenOk and ("b saw a's answer: " .. tostring(second)) or tostring(second))
    -- Leaves mark.lua as it was before the run.
    if markBefore then ask(string.format("return TestOrderTouch(%q)", markBefore)) end
end)

-- A broken mod.json unloads the mod; repaired, it loads again.
local broken

local function unloaded()
    local running, failure = part()
    return not running and tostring(failure):find("not running", 1, true) ~= nil, running, failure
end

step(function()
    local ok, done = ask("return TestReloadBreak()")
    broken = ok and done == true
    result("TestReloadBreak", broken, tostring(done))
    if broken then waitFor(unloaded, 5) end
end)

step(function()
    if not broken then return end
    local gone, running, failure = unloaded()
    result("a broken mod.json unloads the mod", gone,
           running and ("still answers " .. tostring(failure)) or tostring(failure))
    -- Two more seconds: it stays unloaded while the file stays broken.
    wait(2)
end)

step(function()
    if not broken then return end
    result("it stays unloaded while mod.json is broken", (unloaded()))
    local ok, repaired, what = ask("return TestManifestRepair()")
    result("TestManifestRepair", ok and repaired == true,
           ok and tostring(what) or (tostring(repaired) .. "; is wxl.test-manifest in Mods/?"))
    waitFor(function() return (part()) end, 5)
end)

step(function()
    if not broken then return end
    local ok, now = part()
    result("the repaired mod.json loads the mod again", ok, ok and ("answers " .. tostring(now)) or tostring(now))
    -- Leaves lib/part.lua as it was before the run.
    if ok and originalPart and now ~= originalPart then
        ask(string.format("return TestReloadWrite(%q)", originalPart))
    end
end)

-- 4. Events a session rarely shows ----------------------------------------------------------------

local inCombat, castSpell, castName

-- The first spell of `list` the player knows: its id and name.
local function known(list)
    local ids = {}
    for i, id in ipairs(list) do ids[i] = tostring(id) end
    return ask("for _, id in ipairs({" .. table.concat(ids, ", ") .. "}) do " ..
               "if IsSpellKnown(id) then return id, (GetSpellInfo(id)) end end return nil")
end

-- The name of the spell the player is casting, or nil.
local function casting()
    local _, name = ask("return UnitCastingInfo('player')")
    return name
end

step(function()
    print("phase 4: events a session rarely shows")
    if not PROVOKE then return end
    local _, combat = ask("return (UnitAffectingCombat('player') or InCombatLockdown()) and true or false")
    inCombat = combat == true
    local _, id, name = known(CAST_SPELLS)
    castSpell, castName = id, name
    if not castSpell then return end
    -- A cast refused by a handler never starts.
    watch("SpellCastRequest")
    refuseSpell = castSpell
    run(string.format("CastSpellByID(%d, 'player')", castSpell))
    wait(1)
end)

step(function()
    if not PROVOKE then
        skip("events", "PROVOKE is off at the top of wxl.selftest/init.lua")
        return
    end
    if not castSpell then
        skip("casts", "this character knows none of the cast-time spells tried (a paladin's, priest's, " ..
                      "shaman's, druid's or warlock's first)")
        return
    end
    refuseSpell = nil
    result("event SpellCastRequest (" .. tostring(castName) .. ")", saw("SpellCastRequest", spell(2, castSpell)))
    local name = casting()
    result("a cast a handler refuses does not start", name == nil, name and ("casting " .. name) or "no cast")
    if name then run("SpellStopCasting()") end
    wait(2)
end)

local castStarted

step(function()
    if not (PROVOKE and castSpell) then return end
    -- A cast the player cancels: the client's own SpellCastCancel.
    watch("SpellCastStart", "SpellCastCancel", "SpellCastInterrupted", "SpellCastFailed")
    run(string.format("CastSpellByID(%d, 'player')", castSpell))
    waitFor(function() return casting() ~= nil end, 3)
end)

step(function()
    if not (PROVOKE and castSpell) then return end
    castStarted = casting() ~= nil
    run("SpellStopCasting()")
    waitFor(function() return saw("SpellCastCancel", spell(3, castSpell)) end, 2)
end)

step(function()
    if not (PROVOKE and castSpell) then return end
    if not castStarted then
        skip("cast cancelled", castName .. " did not start (out of mana, moving, or refused by the server)")
    else
        result("event SpellCastStart (" .. castName .. ")", saw("SpellCastStart", spell(6, castSpell)))
        result("event SpellCastCancel (" .. castName .. ")", saw("SpellCastCancel", spell(3, castSpell)))
        note("event SpellCastInterrupted (" .. castName .. ")", saw("SpellCastInterrupted", spell(3, castSpell)),
             "the server answers a cancel with SMSG_SPELL_FAILURE only when the cast reached it")
    end
    if casting() then run("SpellStopCasting()") end
    -- The global cooldown of the cast passes.
    wait(2)
end)

-- A racial's cooldown, and the client refusing the same cast while it runs.
local racial, racialName, racialWent

step(function()
    if not PROVOKE then return end
    local _, id, name = known(RACIALS)
    racial, racialName = id, name
    if not racial then
        skip("cooldown", "this character knows none of the racials tried (War Stomp, Arcane Torrent " ..
                         "and Shadowmeld are left out on purpose)")
        return
    end
    local _, start = ask(string.format("return GetSpellCooldown(%d)", racial))
    if (tonumber(start) or 0) > 0 then
        skip("cooldown", racialName .. " is still cooling down from an earlier run")
        racial = nil
        return
    end
    watch("SpellCastGo", "SpellCooldown", "SpellCastFailed", "UnitAuraApplied")
    run(string.format("CastSpellByID(%d, 'player')", racial))
    waitFor(function() return saw("SpellCastGo", spell(6, racial)) end, 3)
end)

step(function()
    if not (PROVOKE and racial) then return end
    racialWent = saw("SpellCastGo", spell(6, racial))
    result("event SpellCastGo (" .. racialName .. ")", racialWent)
    if not racialWent then return end
    local _, start, duration = ask(string.format("return GetSpellCooldown(%d)", racial))
    result("the client holds " .. racialName .. "'s cooldown",
           (tonumber(start) or 0) > 0 and (tonumber(duration) or 0) > 1.5,
           "start " .. tostring(start) .. ", duration " .. tostring(duration))
    run(string.format("CastSpellByID(%d, 'player')", racial))
    waitFor(function() return saw("SpellCastFailed", spell(3, racial)) end, 2)
end)

step(function()
    if not (PROVOKE and racial and racialWent) then return end
    result("event SpellCastFailed: a cast during its cooldown", saw("SpellCastFailed", spell(3, racial)))
    note("event SpellCooldown (" .. racialName .. ")", saw("SpellCooldown", spell(5, racial)),
         "the server sends SMSG_SPELL_COOLDOWN for some casts of a player only")
    wait(1)
end)

-- An item use a handler refuses: the hearthstone, which never starts its cast. Its spell is
-- refused too, and any cast is stopped, should the refusal not hold.
local hearthBag, hearthCast

step(function()
    if not PROVOKE then return end
    local _, bag, slot = ask(string.format(
        "for bag = 0, 4 do for slot = 1, GetContainerNumSlots(bag) do " ..
        "if GetContainerItemID(bag, slot) == %d then return bag, slot end end end return nil", HEARTHSTONE))
    hearthBag = bag
    if not hearthBag then
        skip("item use", "no hearthstone in the bags")
        return
    end
    watch("ItemUseRequest")
    refuseItems, refuseSpell = true, HEARTHSTONE_SPELL
    local _, started = ask(string.format(
        "UseContainerItem(%d, %d) if UnitCastingInfo('player') then SpellStopCasting() return true end return false",
        bag, slot))
    hearthCast = started == true
    wait(1)
end)

step(function()
    if not (PROVOKE and hearthBag) then return end
    local name = casting()
    if name then run("SpellStopCasting()") end
    refuseItems, refuseSpell = false, nil
    result("event ItemUseRequest (hearthstone)", saw("ItemUseRequest"))
    result("an item use a handler refuses does not cast", not hearthCast and name == nil,
           (hearthCast or name) and "a cast started and was stopped" or "no cast")
end)

-- An item taken off and put back on: EquipmentChanged twice. Out of combat only, where the
-- client lets armour change.
local wornSlot, wornLink, wornId, takenOff

local function emptied(args) return args[3] == 0 end
local function filled(args) return args[3] == wornId end

step(function()
    if not PROVOKE then return end
    if inCombat then
        skip("equipment", "in combat, where armour cannot change")
        return
    end
    local slots = {}
    for i, slot in ipairs(SLOTS) do slots[i] = tostring(slot) end
    local _, slot, link = ask("for _, slot in ipairs({" .. table.concat(slots, ", ") .. "}) do " ..
                              "local link = GetInventoryItemLink('player', slot) " ..
                              "if link then return slot, link end end return nil")
    if not slot then
        skip("equipment", "nothing worn in the shirt, feet, wrist, waist, legs or chest slot")
        return
    end
    wornSlot, wornLink, wornId = slot, link, tonumber(tostring(link):match("item:(%d+)"))
    watch("EquipmentChanged", "InventorySlotChanged")
    local _, what = ask(string.format(
        "PickupInventoryItem(%d) PutItemInBackpack() " ..
        "if CursorHasItem() then PickupInventoryItem(%d) return 'kept' end return 'off'", slot, slot))
    takenOff = what == "off"
    if not takenOff then
        skip("equipment", "the backpack has no room: the item stays on")
        return
    end
    waitFor(function() return saw("EquipmentChanged", emptied) end, 3)
end)

step(function()
    if not (PROVOKE and takenOff) then return end
    result("event EquipmentChanged: slot " .. wornSlot .. " emptied", saw("EquipmentChanged", emptied))
    run(string.format("EquipItemByName(%q)", wornLink))
    waitFor(function() return saw("EquipmentChanged", filled) end, 3)
end)

local function wornAgain()
    local _, back = ask(string.format("return GetInventoryItemLink('player', %d) == %q", wornSlot, wornLink))
    return back == true
end

step(function()
    if not (PROVOKE and takenOff) then return end
    result("event EquipmentChanged: slot " .. wornSlot .. " filled", saw("EquipmentChanged", filled))
    waitFor(wornAgain, 3)
end)

step(function()
    if not (PROVOKE and takenOff) then return end
    -- The chat box reads | as an escape; the log shows the link as the client wrote it.
    local back = wornAgain()
    result("the item is worn again", back, back and wornLink or ("put " .. wornLink .. " back on by hand"))
end)

-- 5. /reload ------------------------------------------------------------------------------------

local enteredBefore

step(function()
    print("phase 5: /reload")
    result("event WorldEnter at login", saw("WorldEnter"))
    watch("WorldEnter", "WorldLeave")
    enteredBefore = entered
    run("ReloadUI()")
    waitFor(function() return entered > enteredBefore end, 60,
            function() result("/reload", false, "the world was not entered again within 60 s") end)
end)

step(function() wait(3) end)

step(function()
    if entered > enteredBefore then
        result("a /reload raises neither WorldLeave nor WorldEnter", not saw("WorldLeave") and not saw("WorldEnter"))
        call(AGAIN, " after /reload")
    end
end)

-- 6. Log out and back in ---------------------------------------------------------------------------

local relogging

step(function()
    print("phase 6: log out and back in")
    if not RELOG then
        skip("log out and back in", "RELOG is off at the top of wxl.selftest/init.lua")
        return
    end
    relogging = true
    chat("logging out and back in by itself: do not move")
    watch("WorldLeave", "GlueScreenChanged", "GlueCharacterList", "GlueEnterWorld", "WorldEnter")
    run("Logout()")
    waitFor(function() return saw("GlueCharacterList") end, 45, function()
        relogging = false
        result("log out", false, "the character list did not come within 45 s: in combat, moved, or the " ..
                                 "logout was refused; log in again by hand if needed")
    end)
end)

step(function()
    if not relogging then return end
    result("event WorldLeave", saw("WorldLeave"))
    result("event GlueScreenChanged to charselect", saw("GlueScreenChanged", carries("charselect")))
    result("event GlueCharacterList", saw("GlueCharacterList"))
    wait(3)
end)

step(function()
    if not relogging then return end
    enteredBefore = entered
    -- The login screens' Lua: the character selected is the one that just left.
    run("EnterWorld()")
    waitFor(function() return entered > enteredBefore end, 90, function()
        relogging = false
        result("back in the world", false, "not in the world 90 s after EnterWorld(); enter by hand")
    end)
end)

step(function()
    if not relogging then return end
    result("event GlueEnterWorld", saw("GlueEnterWorld"))
    result("event WorldEnter", saw("WorldEnter"))
    wait(3)
end)

step(function()
    if relogging then call(AGAIN, " after logging back in") end
end)

-- 7. Engine points (wxl.point) ----------------------------------------------------------------------

local function flag(flags, bit) return math.floor(flags / bit) % 2 == 1 end

step(function()
    print("phase 7: the engine points")
    if not wxl.point then
        result("wxl.point", false, "the runtime publishes no wxl.point")
        return
    end
    local points, why = wxl.point.Points()
    if not points then
        result("wxl.point.Points", false, tostring(why))
        return
    end
    result("wxl.point lists the seven points", #points >= 7, #points .. " listed")
    local byName = {}
    for _, p in ipairs(points) do byName[p.name] = p end
    for _, name in ipairs({ "CGCamera::UpdateCallback", "CM2Cache::CreateShared", "CGWorldFrame::RenderWorld",
                            "vFrameScript_SignalEvent", "NetClient::ProcessMessage", "CM2Shared::FinishLoading",
                            "M2Init(uint8_t*,uint32_t,M2Data&)" }) do
        local p = byName[name]
        local id = p and wxl.point.Find(name)
        result("point " .. name .. " is here", p ~= nil and p.point ~= 0 and flag(p.flags, 4) and id == p.point,
               p and (p.absent ~= "" and p.absent or p.signature) or "not listed")
    end
end)

-- 8. Gameplay events ---------------------------------------------------------------------------

-- Each gameplay family subscribes, and what client Lua can provoke alone is provoked; the rest
-- needs another player, an NPC or a death, and is skipped with why.
local GAMEPLAY = {
    ObjectScript = { "UnitAuraApplied", "UnitAuraRemoved", "UnitAuraRefreshed", "UnitMountChanged" },
    LootScript   = { "LootOpened", "LootSlot", "LootSlotCleared", "LootClosed", "LootMoney", "LootRollStart",
                     "LootRoll", "LootRollWon", "LootAllPassed" },
    ItemScript   = { "ItemPushed", "InventorySlotChanged", "ItemDurabilityChanged", "ItemStackChanged",
                     "InventoryError" },
    QuestScript  = { "QuestAccepted", "QuestRemoved", "QuestObjectiveChanged", "QuestCompleted", "QuestFailed",
                     "QuestTurnedIn", "QuestGiverGreeting", "QuestGiverDetail", "QuestGiverProgress",
                     "QuestGiverReward" },
    GroupScript  = { "GroupInvite", "GroupUpdated", "GroupMember", "GroupLeft", "GroupLootMethodChanged",
                     "ReadyCheck", "ReadyCheckResponse", "ReadyCheckFinished" },
    SocialScript = { "DuelRequested", "DuelCountdown", "DuelWon", "DuelFinished", "TradeStatus", "Emote",
                     "TextEmote", "ResurrectRequest", "WindowOpened", "WindowClosed" },
    PlayerScript = { "PlayerDied", "PlayerReleased", "PlayerResurrected", "ReputationChanged",
                     "AchievementEarned" },
}

step(function()
    print("phase 8: gameplay events")
    local count, refused = 0, {}
    for family, names in pairs(GAMEPLAY) do
        for _, name in ipairs(names) do
            local ok, why = pcall(Observe, family, "On" .. name, function() end)
            if ok then count = count + 1 else refused[#refused + 1] = name .. ": " .. tostring(why) end
        end
    end
    result("every gameplay event subscribes", #refused == 0,
           #refused == 0 and (count .. " events") or table.concat(refused, "; "))
    watch("TextEmote", "Emote")
    run("DoEmote('WAVE')")
    waitFor(function() return saw("TextEmote") end, 3)
end)

step(function()
    result("event TextEmote: /wave", saw("TextEmote", function(args) return args[3] ~= 0 end))
    note("event Emote", saw("Emote"), "the server plays a text emote's animation through SMSG_EMOTE on some cores only")
    if not (PROVOKE and takenOff) then
        skip("event InventorySlotChanged", "phase 4 took no item off")
    else
        result("event InventorySlotChanged: the item taken off went to the bags", saw("InventorySlotChanged"))
    end
    if not (PROVOKE and racial and racialWent) then
        skip("event UnitAuraApplied", "phase 4 cast no racial")
    else
        note("event UnitAuraApplied (" .. racialName .. ")", saw("UnitAuraApplied", spell(4, racial)),
             "this racial puts no aura on its caster")
    end
    for _, what in ipairs({
        "loot (kill something and loot it: LootOpened, LootSlot, LootSlotCleared, LootClosed, LootMoney)",
        "rolls, group, ready checks, trades, duels, resurrection offers (another player)",
        "quests (a quest giver: QuestGiver*, QuestAccepted, QuestObjectiveChanged, QuestTurnedIn)",
        "windows (an NPC: WindowOpened, WindowClosed)",
        "death, release, resurrection, reputation, achievements (they change the character)",
    }) do
        skip("gameplay " .. what, "the probe lists them when they happen")
    end
end)

-- 9. Object methods ------------------------------------------------------------------------------

-- The reads of wxl.object against what the client's own Lua says of the same thing, on the
-- player, its target when it has one, and an item it wears; then two harmless actions: the player
-- targets itself, and the target is cleared.

-- Compares a read with the client's answer to `code`; a read that gives nothing is a FAIL with why.
local function same(name, value, why, code, convert)
    if value == nil then
        result(name, false, "the read gave nil, " .. tostring(why))
        return
    end
    local ok, want = ask(code)
    if not ok then
        result(name, false, "the client's Lua failed: " .. tostring(want))
        return
    end
    if convert then want = convert(want) end
    result(name, value == want, "read " .. tostring(value) .. ", the client says " .. tostring(want))
end

local function truthy(v) return v ~= nil and v ~= false and v ~= 0 end

step(function()
    print("phase 9: object methods")
    local me = wxl.object.LocalPlayer()
    if not me then
        result("wxl.object.LocalPlayer", false, "no player in the world")
        return
    end
    result("IsActivePlayer on the player", me:IsActivePlayer() == true)
    same("Guid", me:Guid(), nil, "return UnitGUID('player')")
    same("Level", me:Level(), nil, "return UnitLevel('player')")
    same("Health", me:Health(), nil, "return UnitHealth('player')")
    same("Money", me:Money(), nil, "return GetMoney()")
    same("Xp", me:Xp(), nil, "return UnitXP('player')")
    same("NextLevelXp", me:NextLevelXp(), nil, "return UnitXPMax('player')")
    same("IsDead", me:IsDead(), nil, "return UnitIsDead('player')", truthy)
    same("InCombat", me:InCombat(), nil, "return UnitAffectingCombat('player')", truthy)
    same("IsMounted", me:IsMounted(), nil, "return IsMounted()", truthy)
    same("IsSwimming", me:IsSwimming(), nil, "return IsSwimming()", truthy)
    same("IsResting", me:IsResting(), nil, "return IsResting()", truthy)
    same("IsAfk", me:IsAfk(), nil, "return UnitIsAFK('player')", truthy)
    same("PartySize", me:PartySize(), nil, "return GetNumPartyMembers()")
    same("RaidSize", me:RaidSize(), nil, "return GetNumRaidMembers()")
    result("Reaction to itself is friendly", me:Reaction() == "friendly", tostring(me:Reaction()))
    local distance = me:Distance(me)
    result("Distance to itself is 0", distance == 0, tostring(distance))
    local run = me:Speed("run")
    result("Speed('run') is a speed", type(run) == "number" and run > 0, tostring(run))
    local skills = me:Skills()
    result("Skills lists the skill lines", type(skills) == "table" and #skills > 0 and skills[1].id > 0,
           skills and (#skills .. " skills") or "nil")
    note("KnowsSpell(6603) Auto Attack", me:KnowsSpell(6603) == true, "this core may not teach Auto Attack as a spell")

    -- Auras: the first of the player's buffs, by spell id, as the client's UnitAura sees it.
    local ok, buff = ask("return select(11, UnitAura('player', 1))")
    if ok and buff then
        result("HasAura of the player's first buff (" .. buff .. ")", me:HasAura(buff) == true)
        local auras = me:Auras()
        local found
        for _, a in ipairs(auras or {}) do if a.spell_id == buff then found = a end end
        result("Auras lists it, with its caster", found ~= nil and type(found.caster) == "string",
               found and (found.caster .. ", " .. found.stacks .. " stacks") or "not listed")
    else
        skip("HasAura and Auras", "the player has no buff")
    end

    -- An item worn: its entry and its quality as the client's GetInventoryItemID and GetItemInfo.
    local worn
    for slot = 0, 18 do
        local item = me:InventoryItem(slot)
        if item then worn = { slot = slot, item = item } break end
    end
    if worn then
        same("InventoryItem(" .. worn.slot .. "):Entry", worn.item:Entry(), nil,
             "return GetInventoryItemID('player', " .. (worn.slot + 1) .. ")")
        local quality, why = worn.item:Quality()
        local qualities = { [0] = "poor", "common", "uncommon", "rare", "epic", "legendary", "artifact", "heirloom" }
        if quality then
            same("an item's Quality", quality, nil,
                 "return select(3, GetItemInfo(" .. worn.item:Entry() .. "))", function(q) return qualities[q] end)
        else
            note("an item's Quality", false, "the item cache has no record yet (" .. tostring(why) .. ")")
        end
        local durability = worn.item:Durability()
        result("an item's Durability reads", type(durability) == "number", tostring(durability))
    else
        skip("InventoryItem", "the player wears nothing")
    end

    -- The target, when there is one.
    local target = me:Target()
    if target then
        same("the target's Health", target:Health(), nil, "return UnitHealth('target')")
        local reaction = target:Reaction()
        local names = { "hated", "hostile", "unfriendly", "neutral", "friendly", "honored", "revered", "exalted" }
        same("the target's Reaction", reaction, nil, "return UnitReaction('target', 'player')",
             function(r) return r and names[r] end)
        result("the target's CreatureType is a name", type(target:CreatureType()) == "string",
               tostring(target:CreatureType()))
        local yards = me:Distance(target)
        result("Distance to the target", type(yards) == "number" and yards >= 0, tostring(yards))
    else
        skip("the target's reads", "nothing targeted: target a creature before the run to check them")
    end

    -- Two harmless actions.
    local done, why = me:Select()
    result("Select(): the player targets itself", done == true, tostring(why))
end)

step(function() wait(0.5) end)

step(function()
    same("the target is the player after Select()", true, nil, "return UnitIsUnit('target', 'player')", truthy)
    local done, why = wxl.action.ClearTarget()
    result("wxl.action.ClearTarget()", done == true, tostring(why))
end)

step(function() wait(0.5) end)

step(function()
    same("nothing is targeted after ClearTarget()", false, nil, "return UnitExists('target')", truthy)
end)

-- 10. The asset pipeline --------------------------------------------------------------------------

step(function()
    print("phase 10: the asset pipeline")
    local s = wxl.storage
    if not (s and s.Serve) then
        result("wxl.storage.Serve", false, "this runtime has no asset pipeline")
        return
    end
    local ok, why = s.Serve("Interface\\WxlSelfTest\\served.txt", "served by wxl.selftest", 1)
    result("Serve: a Lua mod serves bytes under a name", ok == true, tostring(why))
    local text, reason = s.Read("interface/wxlselftest/SERVED.txt")
    result("the client's file layer reads served bytes as one of its files", text == "served by wxl.selftest",
           tostring(text or reason))
    s.Unserve("Interface\\WxlSelfTest\\served.txt")
    result("Unserve: the name is gone", s.Exists("Interface\\WxlSelfTest\\served.txt") == false)

    if not s.Exists("Interface\\WxlTest\\lower.wxltest") then
        skip("a provider's file and add-on", "wxl.test-assets is not in Mods")
    else
        local upper, why2 = s.Read("Interface\\WxlTest\\lower.wxltest")
        result("a provider's file, converted by a transform as it is read", upper == "READ THROUGH A TRANSFORM",
               tostring(upper or why2))
        local asked, value, loaded = ask("return WXL_PROVIDED_ADDON, IsAddOnLoaded('WxlProvided')")
        result("the client listed, loaded and ran an add-on only a provider has, its Lua transformed",
               asked and value == "loaded, transformed" and loaded ~= nil,
               "WXL_PROVIDED_ADDON " .. tostring(value) .. ", IsAddOnLoaded " .. tostring(loaded))
    end
    run("PlaySoundFile('Sound\\\\Interface\\\\iQuestUpdate.wav')")
    wait(1)
end)

step(function()
    for _, name in ipairs({ "TerrainTileLoad", "TextureDecode", "SkinProfileLoad" }) do
        result("event " .. name .. " (" .. assetCount[name] .. " since the start)", assetCount[name] > 0,
               assetFirst[name] or "never raised")
    end
    -- The client loads no sound file while its sound is off: PlaySoundKit and PlaySoundFile return
    -- before the sound engine (SoundPlay is still raised, at the request).
    local asked, allSound = ask("return GetCVar('Sound_EnableAllSound')")
    if asked and allSound == "0" and assetCount.SoundFileLoad == 0 then
        skip("event SoundFileLoad", "the client's sound is off (Sound_EnableAllSound 0), so it loads no sound file")
    else
        result("event SoundFileLoad (" .. assetCount.SoundFileLoad .. " since the start)",
               assetCount.SoundFileLoad > 0, assetFirst.SoundFileLoad or "never raised")
    end
    note("event MapObjectGroupLoad (" .. assetCount.MapObjectGroupLoad .. ")", assetCount.MapObjectGroupLoad > 0,
         "only where a map object stands in view")
    note("event ShaderLoad (" .. assetCount.ShaderLoad .. ")", assetCount.ShaderLoad > 0,
         "shaders may load before the mods do")
end)

-- 11. Models --------------------------------------------------------------------------------------

local hat, batches, batchId = nil, 0, nil

step(function()
    print("phase 11: models")
    result("event ModelParsed (" .. modelCount.ModelParsed .. " since the start)", modelCount.ModelParsed > 0,
           "no model file was parsed while the mods were loaded")
    result("event SkinProfileParsed (" .. modelCount.SkinProfileParsed .. ")", modelCount.SkinProfileParsed > 0)
    note("event ModelSequenceLoad (" .. modelCount.ModelSequenceLoad .. ")", modelCount.ModelSequenceLoad > 0,
         "only for a model whose sequences live in .anim files")
    if modelCount.AppearanceChanged > 0 then
        result("event AppearanceChanged (" .. modelCount.AppearanceChanged .. ")", true, firstAppearance)
    else
        skip("event AppearanceChanged", "phase 4 took no item off")
    end

    local m2 = wxl.m2
    local body = m2.ObjectModel(wxl.object.LocalPlayer())
    result("wxl.m2.ObjectModel: the player's model", body ~= nil)
    if not body then return end
    local prefix = m2.RacePrefix(wxl.object.LocalPlayer():Race() or 1)
    note("wxl.m2.RacePrefix of the player's race", type(prefix) == "string", tostring(prefix))
    local point = m2.AttachmentPoint(body, 11)
    result("wxl.m2.AttachmentPoint: the head's point in the bind pose", type(point) == "table" and point.z ~= nil,
           point and string.format("%.2f %.2f %.2f", point.x, point.y, point.z) or "none")
    hat = m2.CreateModel(body, "Creature\\Murloc\\Murloc.m2")
    result("wxl.m2.CreateModel: a model of the mod's own", hat ~= nil)
    if not hat then return end
    result("Model:Attach on the head", hat:Attach(body, 11, 0) == true)
    result("Model:Parent is the player's model", hat:Parent() == body)
    -- Thousands of batches a frame: listened to for these two seconds only.
    if wxl.runtime.Raises("RenderScript", "OnModelBatchDrawn") then
        batchId = Observe("RenderScript", "OnModelBatchDrawn", function() batches = batches + 1 end)
    end
    wait(2)
end)

step(function()
    if batchId then
        Unobserve(batchId)
        batchId = nil
        result("event ModelBatchDrawn (" .. batches .. " batches in two seconds)", batches > 0)
    else
        result("event ModelBatchDrawn", false, "this runtime does not raise it")
    end
    if not hat then return end
    hat:Detach()
    result("Model:Detach takes it off", hat:Parent() == nil)
    hat:Release()
    hat = nil
end)

-- 12. A de-inlined point --------------------------------------------------------------------------

-- Models the starting zones rarely show, so that at least one is read while the points are armed.
local POINT_MODELS = { "Creature\\Ragnaros\\Ragnaros.m2", "Creature\\Illidan\\Illidan.m2",
                       "Creature\\KelThuzad\\KelThuzad.m2", "Creature\\Arthaslichking\\ArthasLichKing.m2" }
local pointModels, parsedBefore, sequencesBefore = {}, 0, 0

step(function()
    print("phase 12: the model loader's points, M2Init de-inlined on the stock client")
    -- wxl.test-points registers its functions in the client's Lua, where they are called.
    local asked, present = ask("return TestPointsArm ~= nil")
    if not (asked and present) then
        result("wxl.test-points is loaded", false, "TestPointsArm is not registered in the client's Lua")
        return
    end
    local ok, armed, why = ask("return TestPointsArm()")
    result("CM2Shared::FinishLoading and M2Init(uint8_t*,uint32_t,M2Data&) attached", ok and armed == true,
           ok and tostring(why) or tostring(armed))
    if not (ok and armed) then return end
    parsedBefore, sequencesBefore = modelCount.ModelParsed, modelCount.ModelSequenceLoad
    local body = wxl.m2.ObjectModel(wxl.object.LocalPlayer())
    for _, path in ipairs(POINT_MODELS) do
        local model = body and wxl.m2.CreateModel(body, path)
        if model then pointModels[#pointModels + 1] = model end
    end
    result("models of the mod's own asked for while the points are attached", #pointModels > 0,
           #pointModels .. " of " .. #POINT_MODELS)
    wait(3)
end)

step(function()
    local asked, counts = ask("local f, i, n, v, s, a, b = TestPointsCounts() " ..
                              "return string.format('%d|%d|%s|%d|%d|%d|%d', f or 0, i or 0, n or '', v or 0, " ..
                              "s or 0, a or 0, b or 0)")
    if not (asked and type(counts) == "string") then return end
    local f, i, n, v, z, a, b = counts:match("^(%d+)|(%d+)|(.-)|(%d+)|(%d+)|(%d+)|(%d+)$")
    local finishes, inits, name, version, size = tonumber(f), tonumber(i), n, tonumber(v), tonumber(z)
    local fromFinish, fromSequences = tonumber(a) or 0, tonumber(b) or 0
    local sequences = modelCount.ModelSequenceLoad - sequencesBefore
    result("a handler on CM2Shared::FinishLoading fired (" .. tostring(finishes) .. " model(s))",
           (finishes or 0) > 0, "no model finished loading in three seconds")
    -- M2Init has two parents on both clients: every FinishLoading checks its model's header once,
    -- and every sequence read later (FinishLoadingLowPrioritySequence) once more.
    result("a handler on M2Init fired (" .. tostring(inits) .. "): " .. fromFinish .. " in FinishLoading, " ..
           fromSequences .. " for sequences read later, de-inlined from both on the stock client",
           (inits or 0) > 0 and inits >= finishes and fromFinish == finishes,
           "FinishLoading " .. tostring(finishes))
    if sequences > 0 then
        result("M2Init fired once per sequence read later (ModelSequenceLoad " .. sequences .. ")",
               fromSequences == sequences, fromSequences .. " outside FinishLoading")
    else
        note("M2Init for sequences read later", false, "no sequence was read later while the points were attached")
    end
    result("the handlers read the model through the engine headers: " .. tostring(name),
           version == 264 and (size or 0) > 0 and type(name) == "string" and name ~= "",
           string.format("version %s, %s bytes", tostring(version), tostring(size)))
    result("models still parse with the recompiled FinishLoading (ModelParsed " ..
           (modelCount.ModelParsed - parsedBefore) .. ")", modelCount.ModelParsed > parsedBefore)
    run("TestPointsDisarm()")
    for _, model in ipairs(pointModels) do model:Release() end
    pointModels = {}
end)

-- 13. The asset bricks ------------------------------------------------------------------------------

-- The names the client looked for and nobody had, those of this run only (the client probes many
-- optional files of its own).
local missing = {}
Observe("AssetScript", "OnFileMissing", function(path, kind, open, pending, loader)
    local low = path:lower()
    if not (low:find("wxltest", 1, true) or low:find("wxlselftest", 1, true)) then return end
    missing[#missing + 1] = { path = path, kind = kind, open = open, pending = pending }
end)

local function missingText(list)
    local shown = {}
    for _, m in ipairs(list) do
        shown[#shown + 1] = string.format("%s (%s, %s%s)", m.path, tostring(m.kind), m.open and "open" or "exists",
                                          m.pending and ", pending" or "")
    end
    return #shown > 0 and table.concat(shown, "; ") or "none"
end

local function anyMissing(test)
    for _, m in ipairs(missing) do
        if test(m) then return m end
    end
    return nil
end

local BASE = "Interface\\FrameXML\\FrameXML.toc"
local QUESTION, BAG = "Interface\\Icons\\INV_Misc_QuestionMark.blp", "Interface\\Icons\\INV_Misc_Bag_08.blp"
-- A texture larger than the interface's atlas takes (square, 64 pixels or less), so read again in
-- place, and another of the same size to put in its place.
local FRAME, ELITE = "Interface\\TargetingFrame\\UI-TargetingFrame.blp", "Interface\\TargetingFrame\\UI-TargetingFrame-Elite.blp"
local LATER, FETCHED = "Interface\\WxlTest\\Later.blp", "Interface\\WxlSelfTest\\Fetched.blp"
local hashed, echoNow, echoLater, echoRequest, echoNobody, fetched, cancelledCount, cppMissing, neverAnswered
local neverRequest

step(function()
    print("phase 13: names nobody has, content hashes, overrides, reloads, the relay")
    local s = wxl.storage
    if not (s and s.HashFile) then
        result("wxl.storage.HashFile", false, "this runtime has no asset bricks")
        return
    end
    missing = {}
    run("WxlSelfTestNone = WxlSelfTestNone or UIParent:CreateTexture() " ..
        "WxlSelfTestNone:SetTexture('Interface\\\\WxlSelfTest\\\\NoSuchTexture')")
    local before = #missing
    s.Exists("Interface\\WxlSelfTest\\NoSuchFile.blp")
    result("a mod's own wxl.storage lookup raises no FileMissing", #missing == before)
    wait(0.5)
end)

step(function()
    local seen = anyMissing(function(m) return m.kind == "texture" and m.path:lower():find("nosuchtexture", 1, true) end)
    result("FileMissing: the texture the client asked for and no layer has", seen ~= nil, missingText(missing))

    local s        = wxl.storage
    local original = s.Read(BASE)
    if not original then
        skip("overrides and hashes", BASE .. " is not in this client's archives")
        return
    end
    local mine = "Interface\\WxlSelfTest\\FrameXML.toc"
    result("a path of the mod's own: nothing there yet", s.Exists(mine) == false)
    s.Serve(mine, "override text", 0)
    result("served at 0 under a new path, the copy lives beside the client's file",
           s.Read(mine) == "override text" and s.Read(BASE) == original)
    local h = s.HashFile(mine)
    result("HashFile of served bytes: the MD5 of what is served, layer served",
           h ~= nil and h.md5 == "b43a45b5805955f0f335c0e884f18fd7" and h.layer == "served",
           h and (h.md5 .. " " .. h.layer .. " " .. h.source) or "none")
    local hu = s.HashFile(BASE)
    result("HashFile of the client's file: an archive's, its size and its MPQ",
           hu ~= nil and hu.layer == "archive" and #hu.md5 == 32 and hu.size == #original,
           hu and (hu.md5 .. " " .. hu.size .. " " .. hu.source) or "none")
    result("OverrideFile: the client's file reads as the mod's copy",
           s.OverrideFile(BASE, mine) == true and s.Read(BASE) == "override text" and s.HashFile(BASE).layer == "served")
    local restored = s.RestoreFile(BASE)
    local again, why = s.RestoreFile(BASE)
    result("RestoreFile: the client's own file again; once", restored == true and s.Read(BASE) == original and
           again == nil and why == "not_found")
    s.Unserve(mine)

    hashed = nil
    local request = s.HashFileAsync(BASE, function(hash, reason) hashed = hash or reason end, true)
    result("HashFileAsync answers a request number at once", type(request) == "number")
    waitFor(function() return hashed ~= nil end, 5)
end)

step(function()
    local s = wxl.storage
    if s.HashFile and s.Read(BASE) then
        local hu = s.HashFile(BASE)
        result("and its answer comes later, on the main thread, the same MD5", type(hashed) == "table" and hu ~= nil and
               hashed.md5 == hu.md5, type(hashed) == "table" and hashed.md5 or tostring(hashed))
    end
    -- A texture on screen, read again from another file.
    run("WxlSelfTestIcon = WxlSelfTestIcon or UIParent:CreateTexture(nil, 'OVERLAY') WxlSelfTestIcon:SetWidth(64) " ..
        "WxlSelfTestIcon:SetHeight(64) WxlSelfTestIcon:SetPoint('CENTER', -80, 0) " ..
        "WxlSelfTestIcon:SetTexture('Interface\\\\Icons\\\\INV_Misc_QuestionMark') WxlSelfTestIcon:Show()")
    run("WxlSelfTestFrame = WxlSelfTestFrame or UIParent:CreateTexture(nil, 'OVERLAY') WxlSelfTestFrame:SetWidth(256) " ..
        "WxlSelfTestFrame:SetHeight(128) WxlSelfTestFrame:SetPoint('CENTER', -220, 120) " ..
        "WxlSelfTestFrame:SetTexture('Interface\\\\TargetingFrame\\\\UI-TargetingFrame') WxlSelfTestFrame:Show()")
    wait(1)
end)

step(function()
    local s = wxl.storage
    s.OverrideFile(FRAME, ELITE)
    local r, why = s.ReloadAsset(FRAME)
    result("ReloadAsset of a texture the client holds: read again in place (up left of the screen's centre, " ..
           "the target frame shows its elite dragon for two seconds)", r ~= nil and r.textures > 0,
           r and (r.textures .. " texture(s), " .. r.busy .. " still loading") or why)
    s.OverrideFile(QUESTION, BAG)
    local icon, iconWhy = s.ReloadAsset(QUESTION)
    result("ReloadAsset of an icon, held in the interface's atlas: unsupported", icon == nil and iconWhy == "unsupported",
           tostring(icon and icon.textures or iconWhy))
    local unsupported, reason = s.ReloadAsset("World\\wmo\\Azeroth\\Buildings\\Inn\\Inn.wmo")
    result("ReloadAsset of a map object: unsupported", unsupported == nil and reason == "unsupported", tostring(reason))
    wait(2)
end)

step(function()
    local s = wxl.storage
    s.RestoreFile(QUESTION)
    s.RestoreFile(FRAME)
    s.ReloadAsset(FRAME)
    run("WxlSelfTestIcon:Hide() WxlSelfTestFrame:Hide()")
    local asked, race, sex = ask("return select(2, UnitRace('player')), UnitSex('player')")
    if not asked or type(race) ~= "string" then
        skip("ReloadAsset of the player's model", "the client did not say the player's race")
        return
    end
    local gender = sex == 3 and "Female" or "Male"
    local path   = "Character\\" .. race .. "\\" .. gender .. "\\" .. race .. gender .. ".m2"
    local r, why = s.ReloadAsset(path)
    result("ReloadAsset of the player's model: dropped from the model cache and the player made again (it blinks)",
           r ~= nil and r.models > 0 and r.objects > 0,
           path .. ": " .. (r and (r.models .. " file(s), " .. r.objects .. " unit(s)") or tostring(why)))
end)

step(function()
    local relay = wxl.relay
    echoNow, echoLater, echoRequest = nil, nil, nil
    result("wxl.relay.Publish: a Lua mod publishes a service", relay.Publish("wxl.selftest.echo", function(request, method, a)
        if method == "now" then return "echo " .. tostring(a) end
        echoRequest = request
    end) == true and relay.Published("WXL.SELFTEST.ECHO") == true)
    relay.Call("wxl.selftest.echo", "now", { "hi" }, function(ok, v) echoNow = { ok, v } end)
    relay.Call("wxl.selftest.echo", "later", nil, function(ok, v) echoLater = { ok, v } end)
    waitFor(function() return echoNow ~= nil and echoRequest ~= nil end, 3)
end)

step(function()
    local relay = wxl.relay
    result("a service that returns values answers at once: true and them", echoNow ~= nil and echoNow[1] == true and
           echoNow[2] == "echo hi", echoNow and tostring(echoNow[2]) or "never answered")
    result("one that returns nothing answers later through Answer", echoRequest ~= nil and echoLater == nil and
           relay.Answer(echoRequest, "done") == true)
    waitFor(function() return echoLater ~= nil end, 3)
end)

step(function()
    local relay = wxl.relay
    result("and the answer reaches the caller at a later update", echoLater ~= nil and echoLater[1] == true and
           echoLater[2] == "done", echoLater and tostring(echoLater[2]) or "never answered")
    relay.Unpublish("wxl.selftest.echo")
    echoNobody = nil
    relay.Call("wxl.selftest.echo", "now", { "x" }, function(ok, why) echoNobody = { ok, why } end)
    waitFor(function() return echoNobody ~= nil end, 3)
end)

step(function()
    local nobody = echoNobody
    result("a service taken back answers nothing: the caller is told not_found", nobody ~= nil and nobody[1] == false and
           nobody[2] == "not_found", nobody and tostring(nobody[2]) or "never answered")
    if not wxl.relay.Published("wxl.test-assets.fetch") then
        skip("a C++ mod's service: a download under a path of the selftest's", "wxl.test-assets is not in Mods")
        return
    end
    -- The client asks for a texture the downloader has not yet: missing, and pending.
    missing = {}
    run("WxlSelfTestLater = WxlSelfTestLater or UIParent:CreateTexture(nil, 'OVERLAY') WxlSelfTestLater:SetWidth(64) " ..
        "WxlSelfTestLater:SetHeight(64) WxlSelfTestLater:SetPoint('CENTER', 80, 0) " ..
        "WxlSelfTestLater:SetTexture('Interface\\\\WxlTest\\\\Later') WxlSelfTestLater:Show()")
    fetched = nil
    wxl.relay.Call("wxl.test-assets.fetch", "fetch", { LATER, FETCHED }, function(ok, path, md5, size)
        fetched = { ok, path, md5, size }
    end)
    waitFor(function() return fetched ~= nil end, 5)
end)

step(function()
    if not wxl.relay.Published("wxl.test-assets.fetch") then return end
    local s    = wxl.storage
    local seen = anyMissing(function(m) return m.path:lower() == LATER:lower() and m.pending end)
    result("FileMissing for a name a provider has not yet: pending", seen ~= nil, missingText(missing))
    result("wxl.relay: a C++ service answers later from a thread of its own, the file served under the path asked",
           fetched ~= nil and fetched[1] == true and fetched[2] == FETCHED,
           fetched and (tostring(fetched[2]) .. " " .. tostring(fetched[4]) .. " bytes") or "never answered")
    if not (fetched and fetched[1]) then return end
    local h = s.HashFile(FETCHED)
    result("HashFile of a provider's file: the MD5 the downloader reported, layer provider",
           h ~= nil and h.md5 == fetched[3] and h.layer == "provider" and h.source == "wxl.test-assets",
           h and (h.md5 .. " " .. h.layer .. " " .. h.source) or "none")
    result("the name it was fetched for is not replaced: nobody has it", s.Exists(LATER) == false)
    run("WxlSelfTestFetched = WxlSelfTestFetched or UIParent:CreateTexture(nil, 'OVERLAY') " ..
        "WxlSelfTestFetched:SetWidth(64) WxlSelfTestFetched:SetHeight(64) WxlSelfTestFetched:SetPoint('CENTER', 160, 0) " ..
        "WxlSelfTestFetched:SetTexture('Interface\\\\WxlSelfTest\\\\Fetched') WxlSelfTestFetched:Show()")
    result("OverrideFile puts the fetched file in place of the missing one", s.OverrideFile(LATER, FETCHED) == true and
           s.Exists(LATER) == true)
    run("WxlSelfTestLater:SetTexture(nil) WxlSelfTestLater:SetTexture('Interface\\\\WxlTest\\\\Later')")
    print("NOTE look: right of the screen's centre, two bags for three seconds -- the fetched file by its own " ..
          "path, and by the name it overrides")
    neverAnswered, cancelledCount = false, nil
    -- Handed to the C++ service at the runtime's next update; cancelled after that, the service is told.
    neverRequest = wxl.relay.Call("wxl.test-assets.fetch", "never", nil, function() neverAnswered = true end)
    wait(0.5)
end)

step(function()
    if not neverRequest then return end
    result("Cancel: a request taken back by its caller", wxl.relay.Cancel(neverRequest) == true)
    neverRequest = nil
    wait(3)
end)

step(function()
    if not wxl.relay.Published("wxl.test-assets.fetch") then return end
    wxl.relay.Call("wxl.test-assets.fetch", "cancelled", nil, function(ok, n) cancelledCount = n end)
    wxl.relay.Call("wxl.test-assets.fetch", "missing", nil, function(ok, path, kind, open, pending, count)
        cppMissing = { path = path, kind = kind, open = open, pending = pending, count = count }
    end)
    waitFor(function() return cancelledCount ~= nil and cppMissing ~= nil end, 3)
end)

step(function()
    if not wxl.relay.Published("wxl.test-assets.fetch") then return end
    result("the C++ service is told of the cancel, and the caller never answered", (cancelledCount or 0) >= 1 and
           not neverAnswered, "cancelled " .. tostring(cancelledCount))
    result("a C++ mod's OnFileMissing saw the names nobody had", cppMissing ~= nil and (cppMissing.count or 0) > 0,
           cppMissing and string.format("%d, last %s kind %s", cppMissing.count, tostring(cppMissing.path),
                                        tostring(cppMissing.kind)) or "no answer")
    wxl.storage.RestoreFile(LATER)
    run("WxlSelfTestLater:Hide() WxlSelfTestFetched:Hide()")
end)

-- 14. The camera, the pointer and the world ------------------------------------------------------

local cameraFrames, placedSeen, mouseMoves = 0, nil, 0
local cameraId, motionId, placedAt, frameEndId = nil, nil, nil, nil
local faceFrom, faceTo = nil, nil

step(function()
    print("phase 14: the camera, the pointer and the world")
    local camera = wxl.camera
    if not (camera and camera.Position) then
        result("wxl.camera", false, "this runtime has no camera service")
        return
    end
    local position, fov, pivot = camera.Position(), camera.Fov(), camera.Pivot()
    result("wxl.camera.Position, Fov and Pivot read the world camera", position ~= nil and fov ~= nil and pivot ~= nil,
           string.format("%s %s %s", tostring(position and position.z), tostring(fov), tostring(pivot and pivot.z)))
    result("wxl.camera.Cinematic: none while playing", camera.Cinematic() == false)
    if not pivot then return end
    -- Ten yards above the head, looking down: set at every update, read back at the next.
    placedAt = { x = pivot.x, y = pivot.y, z = pivot.z + 10 }
    -- The view is written right after OnCameraUpdate, so it is read back at the frame's end, once the
    -- frame was drawn from it.
    cameraId = Observe("RenderScript", "OnCameraUpdate", function()
        cameraFrames = cameraFrames + 1
        camera.SetView(placedAt, { x = 0.01, y = 0, z = -1 }, 0)
    end)
    frameEndId = Observe("RenderScript", "OnFrameEnd", function()
        local now = camera.Position()
        if now and math.abs(now.z - placedAt.z) < 0.01 then placedSeen = true end
    end)
    motionId = Observe("WorldScript", "OnMouseMotion", function() mouseMoves = mouseMoves + 1 end)
    local held, why = wxl.input.CaptureCursor(true)
    result("wxl.input.CaptureCursor(true)", held == true and wxl.input.CursorCaptured() == true, tostring(why))
    note("wxl.input.PointerOverWorld at the centre", wxl.input.PointerOverWorld() == true,
         "a frame of the interface may stand at the centre of the screen")
    wait(2)
end)

step(function()
    if not cameraId then return end
    Unobserve(cameraId)
    Unobserve(frameEndId)
    cameraId, frameEndId = nil, nil
    local camera = wxl.camera
    result("event CameraUpdate (" .. cameraFrames .. " in two seconds)", cameraFrames > 10)
    result("wxl.camera.SetView: the camera is where the view put it", placedSeen == true)
    result("wxl.camera.ReleaseView", camera.ReleaseView() == true)
    wxl.input.CaptureCursor(false)
    result("wxl.input.CaptureCursor(false) lets the pointer go", wxl.input.CursorCaptured() == false)
    Unobserve(motionId)
    note("event MouseMotion (" .. mouseMoves .. " while held)", mouseMoves > 0, "nobody moved the mouse")

    local me = wxl.object.LocalPlayer()
    local feet = me and me:Position()
    if feet then
        local ground = wxl.world.Trace({ x = feet.x, y = feet.y, z = feet.z + 3 }, { x = feet.x, y = feet.y, z = feet.z - 10 })
        result("wxl.world.Trace meets the ground under the player", ground ~= nil and math.abs(ground.z - feet.z) < 2,
               ground and string.format("%.2f under %.2f", ground.z, feet.z) or "nothing met")
        local sky = wxl.world.Trace({ x = feet.x, y = feet.y, z = feet.z + 500 }, { x = feet.x, y = feet.y, z = feet.z + 501 })
        result("and nothing in the empty sky", sky == nil)
        local outdoors = me:IsOutdoors()
        local asked, indoors = ask("return IsIndoors()")
        result("Unit:IsOutdoors agrees with the client's IsIndoors", type(outdoors) == "boolean" and asked and
               (outdoors == (indoors == nil)), tostring(outdoors) .. " / IsIndoors " .. tostring(indoors))
        faceFrom = me:Facing()
        if faceFrom then
            faceTo = (faceFrom + 0.5) % (2 * math.pi)
            local done, why = wxl.action.Face(faceTo)
            result("wxl.action.Face answers true", done == true, tostring(why))
            wait(0.5)
        end
    end
end)

step(function()
    if not faceTo then return end
    local me  = wxl.object.LocalPlayer()
    local now = me and me:Facing()
    result("wxl.action.Face turned the player", now ~= nil and math.abs(now - faceTo) < 0.01,
           string.format("%s, asked %.3f", tostring(now), faceTo))
    wxl.action.Face(faceFrom)
end)

-- 15. The mouse look kept out, a head turned, Dear ImGui ----------------------------------------

step(function()
    print("phase 15: the mouse look kept out, a head turned, the runtime's Dear ImGui")
    local input = wxl.input
    if not (input and input.BlockMouseLook) then
        result("wxl.input.BlockMouseLook", false, "this runtime is older than abi 0.1.25")
        return
    end
    local ok, why = input.BlockMouseLook(true)
    result("wxl.input.BlockMouseLook(true)", ok == true and input.MouseLookBlocked() == true, tostring(why))
    run("MouselookStart()")
    local asked, looking = ask("return IsMouselooking()")
    result("MouselookStart while blocked leaves the game out of its mouse look", asked and not looking,
           "IsMouselooking " .. tostring(looking))
    run("MouselookStop()")
    input.BlockMouseLook(false)
    result("the block is given back", input.MouseLookBlocked() == false)
    run("MouselookStart()")
    asked, looking = ask("return IsMouselooking()")
    result("and MouselookStart works again", asked and looking ~= nil, "IsMouselooking " .. tostring(looking))
    run("MouselookStop()")

    local me = wxl.object.LocalPlayer()
    local turned, why2 = wxl.pose.Head(me, 0.6, 0.2, 0.1)
    result("wxl.pose.Head turns the player's head (look: left and up, for a second)", turned == true, tostring(why2))
    note("event ImGuiFrame is raised by this runtime", wxl.runtime.Raises("RenderScript", "OnImGuiFrame") == true,
         "an older runtime")
    wait(1)
end)

step(function()
    if not (wxl.pose and wxl.pose.ReleaseHead) then return end
    local released, why = wxl.pose.ReleaseHead(wxl.object.LocalPlayer())
    result("wxl.pose.ReleaseHead gives the head back", released == true, tostring(why))
end)

-- 16. Models of the world, bone watchers, the cast's ground point -------------------------------------

-- The casts that named a destination since the start (a ground spell, a blink): the point and the
-- transport it was measured on. The args after miss_reasons came with abi 0.1.21.
local destinations = 0
local lastDestination
local function onCast(...)
    local args = { ... }
    if args[17] == true and type(args[18]) == "table" then
        destinations = destinations + 1
        lastDestination = string.format("spell %d at %.1f %.1f %.1f", args[6] or 0, args[18].x, args[18].y, args[18].z)
    end
end
Observe("ObjectScript", "OnSpellCastStart", onCast)
Observe("ObjectScript", "OnSpellCastGo", onCast)

local placed

step(function()
    print("phase 16: models of the world, bone watchers, the cast's ground point")
    local m2 = wxl.m2
    if not (m2 and m2.CreateWorldModel) then
        result("wxl.m2.CreateWorldModel", false, "this runtime is older than abi 0.1.21")
        return
    end
    local me = wxl.object.LocalPlayer()
    local at, facing = me:Position(), me:Facing()
    if not (at and facing) then
        skip("models of the world", "the player has no position")
        return
    end
    local ground, why = m2.GroundHeight(at, 3, 10)
    result("wxl.m2.GroundHeight under the player", type(ground) == "number" and math.abs(ground - at.z) < 3,
           ground and string.format("%.2f, the player at %.2f", ground, at.z) or tostring(why))
    local body = m2.ObjectModel(me)
    local head = body and m2.AttachmentWorld(body, 11)
    result("wxl.m2.AttachmentWorld: the player's head in the world", type(head) == "table" and head.z > at.z,
           head and string.format("%.2f %.2f %.2f", head.x, head.y, head.z) or "none")

    placed = m2.CreateWorldModel("Creature\\Murloc\\Murloc.m2")
    result("wxl.m2.CreateWorldModel: a model of the world's scene", placed ~= nil)
    if not placed then return end
    local x, y = at.x + 3 * math.cos(facing), at.y + 3 * math.sin(facing)
    local z = m2.GroundHeight({ x = x, y = y, z = at.z }, 3, 10) or at.z
    local ok, why2 = placed:Place({ position = { x = x, y = y, z = z }, yaw = facing + math.pi })
    result("Model:Place three yards in front of the player, facing it (look: a murloc for two seconds)",
           ok == true, tostring(why2))
    result("Model:PlayAnimation (stand)", placed:PlayAnimation(0, 1) == true)
    wait(1)
end)

step(function()
    if not placed then return end
    local m2 = wxl.m2
    result("wxl.m2.ModelLoaded: the placed model has loaded", m2.ModelLoaded(placed) == true)
    local length = m2.AnimationLength(placed, 0)
    result("wxl.m2.AnimationLength of its stand", type(length) == "number" and length > 0, tostring(length))
    placed:SetAlpha(0.5)
    placed:SetEmitting(false)
    result("Model:SetAlpha and SetEmitting (look: it fades to half)", true)

    local asked, present = ask("return TestBonesArm ~= nil")
    if asked and present then
        local ok, armed = ask("return TestBonesArm()")
        result("wxl.test-bones watches the bones of character models", ok and armed == true, tostring(armed))
    else
        result("wxl.test-bones is loaded", false, "TestBonesArm is not registered in the client's Lua")
    end
    wait(1)
end)

step(function()
    local asked, counts = ask("return TestBonesCounts and TestBonesCounts()")
    if asked and type(counts) == "string" then
        local files, frames, bones, root, matrices = counts:match("^(%d+)|(%d+)|(%d+)|(-?%d+)|(%d)$")
        result("the bone watcher took the character files (" .. tostring(files) .. ") and was handed " ..
               tostring(frames) .. " frame(s)", (tonumber(files) or 0) > 0 and (tonumber(frames) or 0) > 0, counts)
        result("each frame had its matrices, and BoneInfo read the root bone (parent -1)",
               matrices == "1" and root == "-1" and (tonumber(bones) or 0) > 0, counts)
        run("TestBonesDisarm()")
    end
    if placed then
        placed:Release()
        placed = nil
        result("Model:Release takes the model out of the world", true)
    end
    note("a cast named its ground point (" .. destinations .. " since the start)", destinations > 0,
         "only a ground-targeted spell or a blink names one" .. (lastDestination and (": " .. lastDestination) or ""))
    if lastDestination then print("     " .. lastDestination) end
end)

-- 17. Reticle targeting's bricks: the click before the game acts, what the player can do -----------

local previousTarget

step(function()
    print("phase 17: the click before the game acts, what the player can do with what is there")
    local me = wxl.object.LocalPlayer()
    if not (me and me.CanAttack) then
        result("Unit:CanAttack", false, "this runtime is older than abi 0.1.26")
        return
    end
    result("the runtime raises WorldClickRequest", wxl.runtime.Raises("WorldScript", "OnWorldClickRequest") == true)

    -- The nearest other unit, whatever it is.
    local nearest, best
    for _, unit in ipairs(wxl.object.InView("Unit")) do
        local d = unit:Distance(me)
        if d and (not best or d < best) then nearest, best = unit, d end
    end
    local interacting, usable, units, objects = 0, 0, 0, 0
    for _, unit in ipairs(wxl.object.InView("Unit")) do
        local can = unit:CanInteract()
        if type(can) == "boolean" then units = units + 1 end
        if can then interacting = interacting + 1 end
    end
    for _, object in ipairs(wxl.object.InView("GameObject")) do
        local can = object:CanUse()
        if type(can) == "boolean" then objects = objects + 1 end
        if can then usable = usable + 1 end
    end
    result("Unit:CanInteract answers for every unit in view (" .. interacting .. " of " .. units .. " offer something)",
           units == #wxl.object.InView("Unit"))
    result("GameObject:CanUse answers for every game object in view (" .. usable .. " of " .. objects .. " usable)",
           objects == #wxl.object.InView("GameObject"))
    if not nearest then
        skip("Unit:CanAttack against UnitCanAttack", "no unit in view")
        return
    end
    local asked, guid = ask("return UnitGUID('target')")
    previousTarget = asked and guid or nil
    wxl.action.Target(nearest)
    local ok, client = ask("return UnitCanAttack('player', 'target') and true or false")
    local mine = nearest:CanAttack()
    result("Unit:CanAttack agrees with UnitCanAttack for the nearest unit (" .. tostring(nearest:Name()) .. ")",
           ok and mine == client, "CanAttack " .. tostring(mine) .. ", UnitCanAttack " .. tostring(client))
    wait(0.5)
end)

step(function()
    if previousTarget then
        local target = wxl.object.FromGuid(previousTarget)
        if target then wxl.action.Target(target) else wxl.action.ClearTarget() end
    else
        wxl.action.ClearTarget()
    end
end)

-- 18. A highlight -------------------------------------------------------------------------------

step(function()
    print("phase 18: an object highlighted, where it is seen")
    local hl = wxl.highlight
    if not (hl and hl.Supported) then
        result("wxl.highlight", false, "this runtime is older than abi 0.1.27")
        return
    end
    local me = wxl.object.LocalPlayer()
    if wxl.gx and wxl.gx.Api and wxl.gx.Api() == "vulkan" then
        result("on the Vulkan device wxl.highlight draws (both clients)", hl.Supported() == true, tostring(hl.UnsupportedReason()))
    end
    if not hl.Supported() then
        local why = hl.UnsupportedReason()
        local ok, err = hl.Set(me, 0xFF3030, 3, 12, 0.6)
        result("wxl.highlight draws none here, says why and refuses as unsupported", type(why) == "string" and #why > 0 and
               ok == nil and err == "unsupported", tostring(why))
        return
    end
    result("wxl.highlight.Set on the player (look: a red outline and a glow around the character for two seconds, " ..
           "hidden where a wall is in front)", hl.Set(me, 0xFF3030, 3, 12, 0.6) == true)
    wait(2)
end)

step(function()
    local hl = wxl.highlight
    if not (hl and hl.Supported and hl.Supported()) then return end
    local me    = wxl.object.LocalPlayer()
    local drawn = hl.Drawn()
    result("the player's batches were drawn again (" .. tostring(drawn) .. " in the last frame; 0 in first person)",
           (drawn or 0) > 0)
    result("Clear takes the highlight off", hl.Clear(me) == true)
    result("a second Clear finds none", select(2, hl.Clear(me)) == "not_found")
end)

-- 19. The game's selection circles ---------------------------------------------------------------

local circleTarget -- what the player had targeted before phase 19

step(function()
    print("phase 19: the game's selection circles hidden and restyled")
    local sc = wxl.selectioncircle
    if not (sc and sc.Hide) then
        result("wxl.selectioncircle", false, "this runtime is older than abi 0.1.28")
        return
    end
    local named, err = pcall(sc.Hide, "nope")
    result("a kind that does not exist is an error naming it", not named and tostring(err):find("nope", 1, true) ~= nil)
    local me = wxl.object.LocalPlayer()
    local nearest, best
    for _, unit in ipairs(wxl.object.InView("Unit")) do
        local d = unit:Distance(me)
        if d and (not best or d < best) then nearest, best = unit, d end
    end
    if not nearest then
        skip("the target's circle", "no unit in view")
        return
    end
    local asked, guid = ask("return UnitGUID('target')")
    circleTarget = asked and guid or false
    wxl.action.Target(nearest)
    wait(0.5)
end)

step(function()
    local sc = wxl.selectioncircle
    if not (sc and sc.Hide) or circleTarget == nil then return end
    local _, shown = ask("return GetCVar('ObjectSelectionCircle')")
    result("the target's circle is drawn (" .. tostring(sc.Drawn()) .. " in the last frame; ObjectSelectionCircle " ..
           tostring(shown) .. ")", sc.Drawn() > 0)
    result("Hide(\"target\") answers true", sc.Hide("target") == true)
    wait(0.3)
end)

step(function()
    local sc = wxl.selectioncircle
    if not (sc and sc.Hide) or circleTarget == nil then return end
    result("the hidden circle is not drawn (look: no circle under the target)", sc.Drawn() == 0 and sc.Hidden() > 0,
           "drawn " .. sc.Drawn() .. ", hidden " .. sc.Hidden())
    result("SetStyle draws it larger and blue (look: for two seconds)",
           sc.SetStyle("target", nil, { color = 0xFF3080FF, scale = 1.6 }) == true)
    wait(2)
end)

step(function()
    local sc = wxl.selectioncircle
    if not (sc and sc.Hide) or circleTarget == nil then return end
    result("the restyled circle is drawn", sc.Drawn() > 0 and sc.Hidden() == 0)
    sc.ResetAll()
    result("ResetObject finds nothing this mod changed", select(2, sc.ResetObject(wxl.object.LocalPlayer())) == "not_found")
    local target = circleTarget and wxl.object.FromGuid(circleTarget)
    if target then wxl.action.Target(target) else wxl.action.ClearTarget() end
end)

-- 20. A rolled camera, a pointer off the centre -------------------------------------------------

local rollId, rollEndId, rollSeen, rollFrames = nil, nil, nil, 0
local ROLL = 0.3

step(function()
    print("phase 20: a rolled camera and a pointer held off the centre")
    local camera, input = wxl.camera, wxl.input
    if not (camera and camera.SetViewRolled and input and input.CaptureCursorAt) then
        result("wxl.camera.SetViewRolled", false, "this runtime is older than abi 0.1.29")
        return
    end
    local bad = select(2, input.CaptureCursorAt(0.5, 1.5))
    result("CaptureCursorAt refuses a point off the window", bad == "invalid", tostring(bad))
    -- The client's own camera, rolled: set at every update from where the client put it, read at
    -- the frame's end once the frame was drawn from it.
    rollId = Observe("RenderScript", "OnCameraUpdate", function()
        local at, forward = camera.Position(), camera.Forward()
        if at and forward then camera.SetViewRolled(at, forward, 0, ROLL) end
    end)
    rollEndId = Observe("RenderScript", "OnFrameEnd", function()
        rollFrames = rollFrames + 1
        rollSeen = camera.Roll()
    end)
    local held, why = input.CaptureCursorAt(0.5, 0.4)
    local point = input.CapturePoint()
    result("CaptureCursorAt holds the pointer and CapturePoint reads it back", held == true and point ~= nil and
           math.abs(point.x - 0.5) < 1e-6 and math.abs(point.y - 0.4) < 1e-6,
           tostring(why) .. " " .. tostring(point and point.y))
    print("look: the horizon leans, its right end up, for two seconds; the mouseover is a tenth of the screen above the centre")
    wait(2)
end)

step(function()
    if not rollId then return end
    Unobserve(rollId)
    rollId = nil
    local camera, input = wxl.camera, wxl.input
    result("SetViewRolled: the camera draws with the roll set (" .. rollFrames .. " frames)",
           rollSeen ~= nil and math.abs(rollSeen - ROLL) < 1e-3, tostring(rollSeen))
    result("ReleaseView gives the rolled view back", camera.ReleaseView() == true)
    input.CaptureCursor(false)
    result("CaptureCursor(false) lets the pointer go: held nowhere", select(2, input.CapturePoint()) == "not_found")
    rollSeen = nil
    wait(0.3)
end)

step(function()
    if not rollEndId then return end
    Unobserve(rollEndId)
    rollEndId = nil
    note("the client's own camera draws level again", rollSeen ~= nil and math.abs(rollSeen) < 1e-3,
         "on a vehicle or a transport the client may roll its camera itself: " .. tostring(rollSeen))
end)

-- 21. Textures from SVG and PNG ---------------------------------------------------------------

local tex = {} -- what phase 21 made: paths and texture coordinates

-- Shows a texture in a frame of the given size in the middle of the screen: whether the client
-- found the file (SetTexture answers 1), and the region's size.
local function show(path, w, h, right, bottom)
    return ask(string.format([[
        local f = WxlSelfTestTexture or CreateFrame("Frame", "WxlSelfTestTexture", UIParent)
        f:SetFrameStrata("TOOLTIP")
        f:SetPoint("CENTER", 0, 120)
        f:SetSize(%d, %d)
        f.t = f.t or f:CreateTexture(nil, "OVERLAY")
        f.t:SetAllPoints(f)
        local found = f.t:SetTexture(%q)
        f.t:SetTexCoord(0, %.9g, 0, %.9g)
        f:Show()
        return found, f.t:GetWidth() .. "x" .. f.t:GetHeight()]], w, h, path, right, bottom))
end

step(function()
    print("phase 21: textures made from an SVG and a PNG")
    local t = wxl.texture
    if not (t and t.Make) then
        result("wxl.texture", false, "this runtime is older than abi 0.1.30")
        return
    end
    local path, right, bottom, tw, th = t.Make(".\\assets\\reticle.svg", 64, nil,
        { fill = "ff4040ff", outline = { color = "000000cc", width = 2 } })
    result("Make draws the reticle at 64 pixels wide, its height in proportion", path ~= nil and tw == 64 and th == 64 and
           right == 1 and math.abs(bottom - 40 / 64) < 1e-6, tostring(path) .. " " .. tostring(right) .. " " .. tostring(bottom))
    if not path then return end
    tex.red, tex.right, tex.bottom = path, right, bottom
    local ok, found, size = show(path, 64, 40, right, bottom)
    result("SetTexture finds it, the region is 64 x 40 (look: a red reticle with a dark outline above the centre, " ..
           "two seconds)", ok and found == 1 and size == "64x40", tostring(found) .. " " .. tostring(size))
    local again = t.Make(".\\assets\\reticle.svg", 64, nil, { outline = { width = 2, color = "000000cc" }, fill = "#ff4040" })
    result("the same request answers the same path", again == path, tostring(again))
    local asked, fromUi = ask("return WxlTexture('Mods\\\\wxl.selftest\\\\assets\\\\reticle.svg', 64, nil, " ..
                              "{ fill = 'ff4040ff', outline = { color = '000000cc', width = 2 } })")
    result("the interface's WxlTexture answers the same path for the same request", asked and fromUi == path, tostring(fromUi))
    local okMissing, none, why = ask("return WxlTexture('Interface\\\\AddOns\\\\Nobody\\\\none.svg', 16, 16)")
    result("WxlTexture answers nil and why for a file nobody has", okMissing and none == nil and
           tostring(why):find("no file", 1, true) ~= nil, tostring(why))
    wait(2)
end)

step(function()
    local t = wxl.texture
    if not (t and t.Make) or not tex.red then return end
    local blue = t.Make(".\\assets\\reticle.svg", 64, nil, { fill = 0xFF4080FF, outline = { width = 0 } })
    result("another colour and the outline taken off give another path", blue ~= nil and blue ~= tex.red, tostring(blue))
    if blue then
        local ok, found = show(blue, 64, 40, tex.right, tex.bottom)
        result("SetTexture finds it (look: the reticle blue, with no outline, two seconds)", ok and found == 1)
    end
    wait(2)
end)

step(function()
    local t = wxl.texture
    if not (t and t.Make) or not tex.red then return end
    local disc, right, bottom = t.Make(".\\assets\\disc.png", 48, 48, nil, "ffffffc0")
    result("a PNG resampled to 48 x 48 in a 64 x 64 texture", disc ~= nil and math.abs(right - 0.75) < 1e-6, tostring(disc))
    if disc then
        local ok, found = show(disc, 48, 48, right, bottom)
        result("SetTexture finds it (look: a disc shading red to white, slightly see-through, two seconds)", ok and found == 1)
    end
    local _, why = t.Make(".\\assets\\disc.png", 48, 48, { fill = "ff0000" })
    result("a group on a PNG is refused, and says why", tostring(why):find("PNG", 1, true) ~= nil, tostring(why))
    local _, none = t.Make(".\\assets\\none.svg", 16, 16)
    result("a missing file is refused, and named", tostring(none):find("none.svg", 1, true) ~= nil, tostring(none))
    wait(2)
end)

step(function()
    local t = wxl.texture
    if not (t and t.Make) or not tex.red then return end
    run("if WxlSelfTestTexture then WxlSelfTestTexture:Hide() end")
    local stats = t.Stats()
    result("Stats counts what was made", stats ~= nil and stats.textures >= 3 and stats.resident >= 1 and stats.reused >= 1,
           stats and (stats.textures .. " textures, " .. stats.bytes .. " bytes, " .. stats.reused .. " reused") or "nil")
end)

-- 22. Console variables -----------------------------------------------------------------------

-- A flag of wxl.cvar.Flags' mask (1 saved, 256 the client's, 512 the interface's).
local function hasFlag(mask, bit) return mask ~= nil and math.floor(mask / bit) % 2 == 1 end

-- Registered when the mod loads: before the client reads Config.wtf on a cold start, which is the
-- registration that waits and is made just before the file is read.
local CVAR_SAVED, CVAR_CHAR = "wxlSelfTest_saved", "wxlSelfTest_char"
if wxl.cvar and wxl.cvar.Register then
    wxl.cvar.Register(CVAR_SAVED, "0", "saved", "wxl.selftest: the token the last launch saved")
end

step(function()
    print("phase 22: console variables")
    local cvar = wxl.cvar
    if not (cvar and cvar.Register) then
        result("wxl.cvar", false, "this runtime is older than abi 0.1.31")
        return
    end
    -- What the last launch saved: its token, in Config.wtf and in the character's settings.
    local savedToken = cvar.Get(CVAR_SAVED)
    note("the value the last launch saved came back from Config.wtf", savedToken ~= nil and savedToken ~= "0",
         "none saved yet (the first launch with this phase, or the last did not end cleanly): " .. tostring(savedToken))
    result("Register a character's variable", cvar.Register(CVAR_CHAR, "0", "character") == true)
    local charToken = cvar.Get(CVAR_CHAR)
    note("the character's value the last session saved came back", charToken ~= nil and charToken ~= "0",
         "none saved yet for this character, or the server keeps no account data: " .. tostring(charToken))
    local token = tostring(os.time())
    result("both get a new token, written at the next clean exit (" .. token .. ")",
           cvar.Set(CVAR_SAVED, token) == true and cvar.Set(CVAR_CHAR, token) == true)
    print("next launch: the two NOTEs above must read PASS, with the token " .. token)

    -- A setting of the mod, ranged; the interface reads and sets it.
    local name = "wxlSelfTest_scale"
    result("Register a saved variable and its range", cvar.Register(name, 1, { "saved" }, "wxl.selftest: a scale") == true and
           cvar.SetRange(name, 0.5, 3) == true)
    result("SetNumber, and the interface's GetCVar reads it", cvar.SetNumber(name, 2) == true and
           select(2, ask("return GetCVar('" .. name .. "')")) == "2", tostring(select(2, ask("return GetCVar('" .. name .. "')"))))
    local seen
    local id = Observe("WorldScript", "OnCVarChanged", function(changed, _, value)
        if changed == name then seen = value end
    end)
    local okSet = ask("SetCVar('" .. name .. "', '2.5')")
    result("the interface's SetCVar changes it, and OnCVarChanged follows", okSet and cvar.GetNumber(name) == 2.5 and seen == "2.5",
           tostring(cvar.Get(name)) .. " " .. tostring(seen))
    ask("SetCVar('" .. name .. "', '9')")
    result("a value outside the range is refused, from the interface too", cvar.Get(name) == "2.5", tostring(cvar.Get(name)))
    local okDefault, fallback = ask("return GetCVarDefault('" .. name .. "')")
    result("the interface's GetCVarDefault reads the default", okDefault and fallback == "1", tostring(fallback))
    result("Reset gives the default back", cvar.Reset(name) == true and cvar.Get(name) == "1")
    Unobserve(id)

    -- Read-only: the interface is refused, the mod is not.
    local locked = "wxlSelfTest_locked"
    cvar.Register(locked, "on", "read_only")
    local okLocked, why = ask("SetCVar('" .. locked .. "', 'off')")
    result("the interface's SetCVar is refused a read-only variable", not okLocked and tostring(why):find("read%-only") ~= nil,
           tostring(why))
    result("its mod sets it, and GetBool reads it", cvar.SetBool(locked, false) == true and cvar.GetBool(locked) == false)

    -- The interface's own registration.
    local okUi, made = ask("return WxlRegisterCVar('wxlSelfTestUi_on', '1', 'saved', 'registered by the interface')")
    local flags = cvar.Flags("wxlSelfTestUi_on")
    result("the interface's WxlRegisterCVar registers one, saved and the interface's", okUi and made == true and
           hasFlag(flags, 1) and hasFlag(flags, 512), tostring(made) .. " " .. tostring(flags))
    local refused = select(2, cvar.Register("wxlSelfTestUi_on", "0"))
    result("a mod is refused the interface's name", refused == "invalid", tostring(refused))
    local client = select(2, cvar.Register("farclip", "1"))
    result("and the client's own", client == "invalid", tostring(client))
    result("Flags tells the client's own variables", hasFlag(cvar.Flags("farclip"), 256))
end)

-- 23. The interface's signatures --------------------------------------------------------------

-- The interface's signatures, observed from the start: the login screens' GlueXML.toc and its
-- clearing of loose folders come before the first phase, FrameXML.toc with every entry into the
-- world and every /reload.
local signatures, cleanups = {}, 0
if wxl.runtime.Raises and wxl.runtime.Raises("LuaScript", "OnInterfaceSignature") then
    Observe("LuaScript", "OnInterfaceSignature", function(file, extra, verdict)
        local name = file:match("([^\\/]+)$") or file
        local seen = signatures[name] or { count = 0 }
        seen.count, seen.verdict, seen.extra = seen.count + 1, verdict, extra
        signatures[name] = seen
    end)
    Observe("LuaScript", "OnInterfaceCleanupRequest", function() cleanups = cleanups + 1 end)
end

step(function()
    print("phase 23: the interface's signatures")
    if not (wxl.runtime.Raises and wxl.runtime.Raises("LuaScript", "OnInterfaceSignature")) then
        result("InterfaceSignature", false, "this runtime is older than abi 0.1.32")
        return
    end
    local frame = signatures["FrameXML.toc"]
    result("the gate answered for FrameXML.toc", frame ~= nil and frame.count > 0,
           "no check of FrameXML.toc was seen since the mod loaded")
    if frame then
        print("FrameXML.toc: the client's verdict is " .. tostring(frame.verdict) .. ", " .. frame.count ..
              " check(s), hashed with " .. tostring(frame.extra))
        result("FrameXML.toc is hashed with Bindings.xml", tostring(frame.extra):lower():find("bindings.xml", 1, true) ~= nil,
               tostring(frame.extra))
        -- The world is up, so the client took FrameXML: a verdict short of signed was answered.
        note("FrameXML.toc is the client's own (signed)", frame.verdict == "signed",
             "verdict " .. tostring(frame.verdict) .. ": a changed FrameXML, loaded because a mod took it as signed (wxl.glue-unlock)")
    end
    local glue = signatures["GlueXML.toc"]
    note("GlueXML.toc was judged at the login screens", glue ~= nil,
         "not seen: the mod loads after the login screens started, or the client skipped them")
    if glue then print("GlueXML.toc: the client's verdict is " .. tostring(glue.verdict)) end
    note("the clearing of loose interface folders was asked once", cleanups == 1, tostring(cleanups) .. " time(s)")
    local addOns = 0
    for name, seen in pairs(signatures) do
        if name ~= "FrameXML.toc" and name ~= "GlueXML.toc" then
            addOns = addOns + 1
            print("add-on " .. name .. ": " .. tostring(seen.verdict))
        end
    end
    print(addOns .. " add-on(s) judged")
end)

-- 24. Drawing into the frame ---------------------------------------------------------------------

-- A fragment program over the whole pass writing its push constants' colour (glslang -V
-- --target-env vulkan1.3 of: #include "wxl/render/render.glsl", layout(push_constant) uniform Push
-- { vec4 color; } pc; layout(location = 0) in vec2 uv; layout(location = 0) out vec4 outColor;
-- void main() { outColor = pc.color; }).
local TINT_SPIRV =
    "\3\2\35\7\0\6\1\0\11\0\8\0\29\0\0\0\0\0\0\0\17\0\2\0\1\0\0\0\17\0\2\0\182\20\0\0\11\0\6\0\1\0\0" ..
    "\0\71\76\83\76\46\115\116\100\46\52\53\48\0\0\0\0\14\0\3\0\0\0\0\0\1\0\0\0\15\0\10\0\4\0\0\0\4\0" ..
    "\0\0\109\97\105\110\0\0\0\0\9\0\0\0\12\0\0\0\21\0\0\0\25\0\0\0\28\0\0\0\16\0\3\0\4\0\0\0\7\0\0\0" ..
    "\3\0\3\0\2\0\0\0\204\1\0\0\4\0\8\0\71\76\95\69\88\84\95\110\111\110\117\110\105\102\111\114\109" ..
    "\95\113\117\97\108\105\102\105\101\114\0\4\0\11\0\71\76\95\69\88\84\95\115\97\109\112\108\101" ..
    "\114\108\101\115\115\95\116\101\120\116\117\114\101\95\102\117\110\99\116\105\111\110\115\0\0\0" ..
    "\0\4\0\10\0\71\76\95\71\79\79\71\76\69\95\99\112\112\95\115\116\121\108\101\95\108\105\110\101" ..
    "\95\100\105\114\101\99\116\105\118\101\0\0\4\0\8\0\71\76\95\71\79\79\71\76\69\95\105\110\99\108" ..
    "\117\100\101\95\100\105\114\101\99\116\105\118\101\0\5\0\4\0\4\0\0\0\109\97\105\110\0\0\0\0\5\0" ..
    "\5\0\9\0\0\0\111\117\116\67\111\108\111\114\0\0\0\0\5\0\4\0\10\0\0\0\80\117\115\104\0\0\0\0\6\0" ..
    "\5\0\10\0\0\0\0\0\0\0\99\111\108\111\114\0\0\0\5\0\3\0\12\0\0\0\112\99\0\0\5\0\4\0\21\0\0\0\117" ..
    "\73\109\97\103\101\115\0\5\0\5\0\25\0\0\0\117\83\97\109\112\108\101\114\115\0\0\0\5\0\3\0\28\0\0" ..
    "\0\117\118\0\0\71\0\4\0\9\0\0\0\30\0\0\0\0\0\0\0\71\0\3\0\10\0\0\0\2\0\0\0\72\0\5\0\10\0\0\0\0\0" ..
    "\0\0\35\0\0\0\0\0\0\0\71\0\4\0\21\0\0\0\33\0\0\0\0\0\0\0\71\0\4\0\21\0\0\0\34\0\0\0\0\0\0\0\71\0" ..
    "\4\0\25\0\0\0\33\0\0\0\1\0\0\0\71\0\4\0\25\0\0\0\34\0\0\0\0\0\0\0\71\0\4\0\28\0\0\0\30\0\0\0\0\0" ..
    "\0\0\19\0\2\0\2\0\0\0\33\0\3\0\3\0\0\0\2\0\0\0\22\0\3\0\6\0\0\0\32\0\0\0\23\0\4\0\7\0\0\0\6\0\0" ..
    "\0\4\0\0\0\32\0\4\0\8\0\0\0\3\0\0\0\7\0\0\0\59\0\4\0\8\0\0\0\9\0\0\0\3\0\0\0\30\0\3\0\10\0\0\0\7" ..
    "\0\0\0\32\0\4\0\11\0\0\0\9\0\0\0\10\0\0\0\59\0\4\0\11\0\0\0\12\0\0\0\9\0\0\0\21\0\4\0\13\0\0\0" ..
    "\32\0\0\0\1\0\0\0\43\0\4\0\13\0\0\0\14\0\0\0\0\0\0\0\32\0\4\0\15\0\0\0\9\0\0\0\7\0\0\0\25\0\9\0" ..
    "\18\0\0\0\6\0\0\0\1\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\1\0\0\0\0\0\0\0\29\0\3\0\19\0\0\0\18\0\0\0\32" ..
    "\0\4\0\20\0\0\0\0\0\0\0\19\0\0\0\59\0\4\0\20\0\0\0\21\0\0\0\0\0\0\0\26\0\2\0\22\0\0\0\29\0\3\0" ..
    "\23\0\0\0\22\0\0\0\32\0\4\0\24\0\0\0\0\0\0\0\23\0\0\0\59\0\4\0\24\0\0\0\25\0\0\0\0\0\0\0\23\0\4" ..
    "\0\26\0\0\0\6\0\0\0\2\0\0\0\32\0\4\0\27\0\0\0\1\0\0\0\26\0\0\0\59\0\4\0\27\0\0\0\28\0\0\0\1\0\0" ..
    "\0\54\0\5\0\2\0\0\0\4\0\0\0\0\0\0\0\3\0\0\0\248\0\2\0\5\0\0\0\65\0\5\0\15\0\0\0\16\0\0\0\12\0\0" ..
    "\0\14\0\0\0\61\0\4\0\7\0\0\0\17\0\0\0\16\0\0\0\62\0\3\0\9\0\0\0\17\0\0\0\253\0\1\0\56\0\1\0" .. ""

local render = { points = {}, kept = false, batches = 0, drawn = 0, refused = nil }

step(function()
    print("phase 24: drawing into the frame (wxl.render)")
    if not (wxl.gx and wxl.gx.Api and wxl.render and wxl.render.Supported) then
        result("wxl.render", false, "this runtime is older than abi 0.1.33")
        return
    end
    local api = wxl.gx.Api()
    result("wxl.gx.Api names the client's device (" .. tostring(api) .. ")", type(api) == "string" and api ~= "none", tostring(api))
    local rd = wxl.render
    -- Both clients draw through the Vulkan renderer under [gx] api = vulkan, the stock one too:
    -- there it must draw; on Direct3D 9 it must say why it does not.
    if api == "vulkan" then
        result("on the Vulkan device wxl.render draws (both clients)", rd.Supported() == true, tostring(rd.UnsupportedReason()))
    end
    if not rd.Supported() then
        local why = rd.UnsupportedReason()
        result("wxl.render draws nothing on " .. tostring(api) .. " and says why", api ~= "vulkan" and type(why) == "string" and #why > 0,
               tostring(why))
        local made, err = rd.CreateScreenImage(1, 1, 0)
        result("and makes no image", made == nil, tostring(err))
        return
    end
    result("wxl.render draws on the Vulkan renderer", api == "vulkan", tostring(api))
    local image = rd.CreateScreenImage(2, 1, 1)
    local w, h = image and image:ImageWidth(), image and image:ImageHeight()
    result("a screen image half the frame's size, with a slot", image ~= nil and (w or 0) > 0 and (h or 0) > 0 and
           image:ImageSlot() ~= 0xFFFFFFFF, tostring(w) .. "x" .. tostring(h))
    render.image = image
    local pipeline, why = rd.CreatePipeline({ kind = "fullscreen", name = "selftest tint", fragment = TINT_SPIRV, blend = "alpha" })
    result("a full-screen pipeline of the selftest's SPIR-V", pipeline ~= nil and pipeline:PipelineReady() == true, tostring(why))
    render.pipeline = pipeline
    render.points.ids = {
        Observe("RenderScript", "OnRenderPoint", function(point, frame, width, height, samples)
            render.points[point] = (render.points[point] or 0) + 1
            render.size = width .. "x" .. height .. ", " .. samples .. " sample(s)"
            if point ~= "after_world" or not render.pipeline or not render.tinting then return end
            wxl.profile.GpuZoneBegin("selftest tint")
            if rd.Begin({ colors = { "scene_color" } }) then
                local ok, err = rd.Fullscreen(render.pipeline, { 0.2, 0.4, 1.0, 0.15 })
                if ok then render.drawn = render.drawn + 1 else render.refused = err end
                rd.End()
            end
            wxl.profile.GpuZoneEnd()
        end),
        Observe("RenderScript", "OnModelBatchDrawn", function(...)
            render.batches = render.batches + 1
            if render.kept then return end
            local batch = select(16, ...)
            render.kept = batch ~= nil and rd.KeepBatch(batch) ~= nil
        end),
    }
    render.tinting = true
    print("look: the world tinted blue for two seconds, the interface not")
    wait(2)
end)

step(function()
    if not render.points.ids then return end
    render.tinting = false
    for _, id in ipairs(render.points.ids) do Unobserve(id) end
    for _, point in ipairs({ "after_opaque_world", "after_liquids", "after_world", "after_ui" }) do
        result("RenderPoint " .. point .. " comes each frame (" .. tostring(render.points[point] or 0) .. " in two seconds)",
               (render.points[point] or 0) > 0, render.size)
    end
    result("the tint was drawn at after_world (" .. render.drawn .. " frames)", render.drawn > 0, tostring(render.refused))
    result("a model batch was kept from ModelBatchDrawn (" .. render.batches .. " batches seen)", render.kept == true)
    result("the scene's colour is copied into a texture inside a point only",
           wxl.render.SceneColorSlot() == 0xFFFFFFFF)
    if render.pipeline then render.pipeline:DestroyPipeline() end
    if render.image then render.image:DestroyImage() end
    render.pipeline, render.image = nil, nil
end)

-- 25. Compute and bone palettes -----------------------------------------------------------------

-- A compute program filling `count` rows at an address with one colour (glslang -V --target-env
-- vulkan1.3 of: layout(local_size_x = 64) in; layout(buffer_reference, std430,
-- buffer_reference_align = 16) writeonly buffer Rows { vec4 rows[]; }; layout(push_constant)
-- uniform Push { uvec2 rows; uint count; uint rgba; } pc; void main() { if
-- (gl_GlobalInvocationID.x < pc.count) Rows(pc.rows).rows[gl_GlobalInvocationID.x] =
-- unpackUnorm4x8(pc.rgba); }), and a fragment program over the whole pass writing row `index` at
-- an address (#include "wxl/render/render.glsl", layout(push_constant) uniform Push { uvec2 rows;
-- uint index; } pc; void main() { outColor = WxlRows(pc.rows).rows[pc.index]; }).
local FILL_SPIRV =
    "\3\2\35\7\0\6\1\0\11\0\8\0\48\0\0\0\0\0\0\0\17\0\2\0\1\0\0\0\17\0\2\0\227\20\0\0\11\0\6\0\1\0\0" ..
    "\0\71\76\83\76\46\115\116\100\46\52\53\48\0\0\0\0\14\0\3\0\228\20\0\0\1\0\0\0\15\0\7\0\5\0\0\0\4" ..
    "\0\0\0\109\97\105\110\0\0\0\0\9\0\0\0\17\0\0\0\16\0\6\0\4\0\0\0\17\0\0\0\64\0\0\0\1\0\0\0\1\0\0" ..
    "\0\3\0\3\0\2\0\0\0\204\1\0\0\4\0\7\0\71\76\95\69\88\84\95\98\117\102\102\101\114\95\114\101\102" ..
    "\101\114\101\110\99\101\0\4\0\9\0\71\76\95\69\88\84\95\98\117\102\102\101\114\95\114\101\102\101" ..
    "\114\101\110\99\101\95\117\118\101\99\50\0\0\0\5\0\4\0\4\0\0\0\109\97\105\110\0\0\0\0\5\0\8\0\9" ..
    "\0\0\0\103\108\95\71\108\111\98\97\108\73\110\118\111\99\97\116\105\111\110\73\68\0\0\0\5\0\4\0" ..
    "\15\0\0\0\80\117\115\104\0\0\0\0\6\0\5\0\15\0\0\0\0\0\0\0\114\111\119\115\0\0\0\0\6\0\5\0\15\0\0" ..
    "\0\1\0\0\0\99\111\117\110\116\0\0\0\6\0\5\0\15\0\0\0\2\0\0\0\114\103\98\97\0\0\0\0\5\0\3\0\17\0" ..
    "\0\0\112\99\0\0\5\0\4\0\35\0\0\0\82\111\119\115\0\0\0\0\6\0\5\0\35\0\0\0\0\0\0\0\114\111\119\115" ..
    "\0\0\0\0\71\0\4\0\9\0\0\0\11\0\0\0\28\0\0\0\71\0\3\0\15\0\0\0\2\0\0\0\72\0\5\0\15\0\0\0\0\0\0\0" ..
    "\35\0\0\0\0\0\0\0\72\0\5\0\15\0\0\0\1\0\0\0\35\0\0\0\8\0\0\0\72\0\5\0\15\0\0\0\2\0\0\0\35\0\0\0" ..
    "\12\0\0\0\71\0\4\0\34\0\0\0\6\0\0\0\16\0\0\0\71\0\3\0\35\0\0\0\2\0\0\0\72\0\4\0\35\0\0\0\0\0\0\0" ..
    "\25\0\0\0\72\0\5\0\35\0\0\0\0\0\0\0\35\0\0\0\0\0\0\0\19\0\2\0\2\0\0\0\33\0\3\0\3\0\0\0\2\0\0\0" ..
    "\21\0\4\0\6\0\0\0\32\0\0\0\0\0\0\0\23\0\4\0\7\0\0\0\6\0\0\0\3\0\0\0\32\0\4\0\8\0\0\0\1\0\0\0\7\0" ..
    "\0\0\59\0\4\0\8\0\0\0\9\0\0\0\1\0\0\0\43\0\4\0\6\0\0\0\10\0\0\0\0\0\0\0\32\0\4\0\11\0\0\0\1\0\0" ..
    "\0\6\0\0\0\23\0\4\0\14\0\0\0\6\0\0\0\2\0\0\0\30\0\5\0\15\0\0\0\14\0\0\0\6\0\0\0\6\0\0\0\32\0\4\0" ..
    "\16\0\0\0\9\0\0\0\15\0\0\0\59\0\4\0\16\0\0\0\17\0\0\0\9\0\0\0\21\0\4\0\18\0\0\0\32\0\0\0\1\0\0\0" ..
    "\43\0\4\0\18\0\0\0\19\0\0\0\1\0\0\0\32\0\4\0\20\0\0\0\9\0\0\0\6\0\0\0\20\0\2\0\23\0\0\0\43\0\4\0" ..
    "\18\0\0\0\27\0\0\0\0\0\0\0\32\0\4\0\28\0\0\0\9\0\0\0\14\0\0\0\39\0\3\0\31\0\0\0\229\20\0\0\22\0" ..
    "\3\0\32\0\0\0\32\0\0\0\23\0\4\0\33\0\0\0\32\0\0\0\4\0\0\0\29\0\3\0\34\0\0\0\33\0\0\0\30\0\3\0\35" ..
    "\0\0\0\34\0\0\0\32\0\4\0\31\0\0\0\229\20\0\0\35\0\0\0\43\0\4\0\18\0\0\0\39\0\0\0\2\0\0\0\32\0\4" ..
    "\0\43\0\0\0\229\20\0\0\33\0\0\0\43\0\4\0\6\0\0\0\45\0\0\0\64\0\0\0\43\0\4\0\6\0\0\0\46\0\0\0\1\0" ..
    "\0\0\44\0\6\0\7\0\0\0\47\0\0\0\45\0\0\0\46\0\0\0\46\0\0\0\54\0\5\0\2\0\0\0\4\0\0\0\0\0\0\0\3\0\0" ..
    "\0\248\0\2\0\5\0\0\0\65\0\5\0\11\0\0\0\12\0\0\0\9\0\0\0\10\0\0\0\61\0\4\0\6\0\0\0\13\0\0\0\12\0" ..
    "\0\0\65\0\5\0\20\0\0\0\21\0\0\0\17\0\0\0\19\0\0\0\61\0\4\0\6\0\0\0\22\0\0\0\21\0\0\0\176\0\5\0" ..
    "\23\0\0\0\24\0\0\0\13\0\0\0\22\0\0\0\247\0\3\0\26\0\0\0\0\0\0\0\250\0\4\0\24\0\0\0\25\0\0\0\26\0" ..
    "\0\0\248\0\2\0\25\0\0\0\65\0\5\0\28\0\0\0\29\0\0\0\17\0\0\0\27\0\0\0\61\0\4\0\14\0\0\0\30\0\0\0" ..
    "\29\0\0\0\124\0\4\0\31\0\0\0\36\0\0\0\30\0\0\0\65\0\5\0\11\0\0\0\37\0\0\0\9\0\0\0\10\0\0\0\61\0" ..
    "\4\0\6\0\0\0\38\0\0\0\37\0\0\0\65\0\5\0\20\0\0\0\40\0\0\0\17\0\0\0\39\0\0\0\61\0\4\0\6\0\0\0\41" ..
    "\0\0\0\40\0\0\0\12\0\6\0\33\0\0\0\42\0\0\0\1\0\0\0\64\0\0\0\41\0\0\0\65\0\6\0\43\0\0\0\44\0\0\0" ..
    "\36\0\0\0\27\0\0\0\38\0\0\0\62\0\5\0\44\0\0\0\42\0\0\0\2\0\0\0\16\0\0\0\249\0\2\0\26\0\0\0\248\0" ..
    "\2\0\26\0\0\0\253\0\1\0\56\0\1\0"

local ROWS_SPIRV =
    "\3\2\35\7\0\6\1\0\11\0\8\0\42\0\0\0\0\0\0\0\17\0\2\0\1\0\0\0\17\0\2\0\182\20\0\0\17\0\2\0\227\20" ..
    "\0\0\11\0\6\0\1\0\0\0\71\76\83\76\46\115\116\100\46\52\53\48\0\0\0\0\14\0\3\0\228\20\0\0\1\0\0\0" ..
    "\15\0\10\0\4\0\0\0\4\0\0\0\109\97\105\110\0\0\0\0\9\0\0\0\14\0\0\0\34\0\0\0\38\0\0\0\41\0\0\0\16" ..
    "\0\3\0\4\0\0\0\7\0\0\0\3\0\3\0\2\0\0\0\204\1\0\0\4\0\7\0\71\76\95\69\88\84\95\98\117\102\102\101" ..
    "\114\95\114\101\102\101\114\101\110\99\101\0\4\0\9\0\71\76\95\69\88\84\95\98\117\102\102\101\114" ..
    "\95\114\101\102\101\114\101\110\99\101\95\117\118\101\99\50\0\0\0\4\0\8\0\71\76\95\69\88\84\95" ..
    "\110\111\110\117\110\105\102\111\114\109\95\113\117\97\108\105\102\105\101\114\0\4\0\11\0\71\76" ..
    "\95\69\88\84\95\115\97\109\112\108\101\114\108\101\115\115\95\116\101\120\116\117\114\101\95\102" ..
    "\117\110\99\116\105\111\110\115\0\0\0\0\4\0\10\0\71\76\95\71\79\79\71\76\69\95\99\112\112\95\115" ..
    "\116\121\108\101\95\108\105\110\101\95\100\105\114\101\99\116\105\118\101\0\0\4\0\8\0\71\76\95" ..
    "\71\79\79\71\76\69\95\105\110\99\108\117\100\101\95\100\105\114\101\99\116\105\118\101\0\5\0\4\0" ..
    "\4\0\0\0\109\97\105\110\0\0\0\0\5\0\5\0\9\0\0\0\111\117\116\67\111\108\111\114\0\0\0\0\5\0\4\0" ..
    "\12\0\0\0\80\117\115\104\0\0\0\0\6\0\5\0\12\0\0\0\0\0\0\0\114\111\119\115\0\0\0\0\6\0\5\0\12\0\0" ..
    "\0\1\0\0\0\105\110\100\101\120\0\0\0\5\0\3\0\14\0\0\0\112\99\0\0\5\0\4\0\22\0\0\0\87\120\108\82" ..
    "\111\119\115\0\6\0\5\0\22\0\0\0\0\0\0\0\114\111\119\115\0\0\0\0\5\0\4\0\34\0\0\0\117\73\109\97" ..
    "\103\101\115\0\5\0\5\0\38\0\0\0\117\83\97\109\112\108\101\114\115\0\0\0\5\0\3\0\41\0\0\0\117\118" ..
    "\0\0\71\0\4\0\9\0\0\0\30\0\0\0\0\0\0\0\71\0\3\0\12\0\0\0\2\0\0\0\72\0\5\0\12\0\0\0\0\0\0\0\35\0" ..
    "\0\0\0\0\0\0\72\0\5\0\12\0\0\0\1\0\0\0\35\0\0\0\8\0\0\0\71\0\4\0\21\0\0\0\6\0\0\0\16\0\0\0\71\0" ..
    "\3\0\22\0\0\0\2\0\0\0\72\0\4\0\22\0\0\0\0\0\0\0\24\0\0\0\72\0\5\0\22\0\0\0\0\0\0\0\35\0\0\0\0\0" ..
    "\0\0\71\0\4\0\34\0\0\0\33\0\0\0\0\0\0\0\71\0\4\0\34\0\0\0\34\0\0\0\0\0\0\0\71\0\4\0\38\0\0\0\33" ..
    "\0\0\0\1\0\0\0\71\0\4\0\38\0\0\0\34\0\0\0\0\0\0\0\71\0\4\0\41\0\0\0\30\0\0\0\0\0\0\0\19\0\2\0\2" ..
    "\0\0\0\33\0\3\0\3\0\0\0\2\0\0\0\22\0\3\0\6\0\0\0\32\0\0\0\23\0\4\0\7\0\0\0\6\0\0\0\4\0\0\0\32\0" ..
    "\4\0\8\0\0\0\3\0\0\0\7\0\0\0\59\0\4\0\8\0\0\0\9\0\0\0\3\0\0\0\21\0\4\0\10\0\0\0\32\0\0\0\0\0\0\0" ..
    "\23\0\4\0\11\0\0\0\10\0\0\0\2\0\0\0\30\0\4\0\12\0\0\0\11\0\0\0\10\0\0\0\32\0\4\0\13\0\0\0\9\0\0" ..
    "\0\12\0\0\0\59\0\4\0\13\0\0\0\14\0\0\0\9\0\0\0\21\0\4\0\15\0\0\0\32\0\0\0\1\0\0\0\43\0\4\0\15\0" ..
    "\0\0\16\0\0\0\0\0\0\0\32\0\4\0\17\0\0\0\9\0\0\0\11\0\0\0\39\0\3\0\20\0\0\0\229\20\0\0\29\0\3\0" ..
    "\21\0\0\0\7\0\0\0\30\0\3\0\22\0\0\0\21\0\0\0\32\0\4\0\20\0\0\0\229\20\0\0\22\0\0\0\43\0\4\0\15\0" ..
    "\0\0\24\0\0\0\1\0\0\0\32\0\4\0\25\0\0\0\9\0\0\0\10\0\0\0\32\0\4\0\28\0\0\0\229\20\0\0\7\0\0\0\25" ..
    "\0\9\0\31\0\0\0\6\0\0\0\1\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\1\0\0\0\0\0\0\0\29\0\3\0\32\0\0\0\31\0\0" ..
    "\0\32\0\4\0\33\0\0\0\0\0\0\0\32\0\0\0\59\0\4\0\33\0\0\0\34\0\0\0\0\0\0\0\26\0\2\0\35\0\0\0\29\0" ..
    "\3\0\36\0\0\0\35\0\0\0\32\0\4\0\37\0\0\0\0\0\0\0\36\0\0\0\59\0\4\0\37\0\0\0\38\0\0\0\0\0\0\0\23" ..
    "\0\4\0\39\0\0\0\6\0\0\0\2\0\0\0\32\0\4\0\40\0\0\0\1\0\0\0\39\0\0\0\59\0\4\0\40\0\0\0\41\0\0\0\1" ..
    "\0\0\0\54\0\5\0\2\0\0\0\4\0\0\0\0\0\0\0\3\0\0\0\248\0\2\0\5\0\0\0\65\0\5\0\17\0\0\0\18\0\0\0\14" ..
    "\0\0\0\16\0\0\0\61\0\4\0\11\0\0\0\19\0\0\0\18\0\0\0\124\0\4\0\20\0\0\0\23\0\0\0\19\0\0\0\65\0\5" ..
    "\0\25\0\0\0\26\0\0\0\14\0\0\0\24\0\0\0\61\0\4\0\10\0\0\0\27\0\0\0\26\0\0\0\65\0\6\0\28\0\0\0\29" ..
    "\0\0\0\23\0\0\0\16\0\0\0\27\0\0\0\61\0\6\0\7\0\0\0\30\0\0\0\29\0\0\0\2\0\0\0\16\0\0\0\62\0\3\0\9" ..
    "\0\0\0\30\0\0\0\253\0\1\0\56\0\1\0"

-- Push constants as bytes: 32-bit words, little end first; an address as two of them.
local function u32(n)
    n = n % 4294967296
    return string.char(n % 256, math.floor(n / 256) % 256, math.floor(n / 65536) % 256, math.floor(n / 16777216) % 256)
end
local function addressBytes(n) return u32(n % 4294967296) .. u32(math.floor(n / 4294967296)) end

local compute = { dispatched = 0, palettes = 0, mode = nil }

step(function()
    print("phase 25: compute and bone palettes (wxl.render)")
    local rd = wxl.render
    if not (rd and rd.Dispatch and rd.BonePalette) then
        result("wxl.render.Dispatch and BonePalette", false, "this runtime is older than abi 0.1.34")
        return
    end
    local api = wxl.gx and wxl.gx.Api and wxl.gx.Api()
    if api == "vulkan" then
        result("on the Vulkan device wxl.render dispatches and takes palettes (both clients)", rd.Supported() == true,
               tostring(rd.UnsupportedReason()))
    end
    if not rd.Supported() then
        result("wxl.render refuses compute only off Vulkan (" .. tostring(api) .. ")", api ~= "vulkan", tostring(rd.UnsupportedReason()))
        local ok, why = rd.BonePalette({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 })
        result("BonePalette answers unsupported where wxl.render draws nothing", ok == nil and why == "unsupported", tostring(why))
        local made, err = rd.CreatePipeline({ kind = "compute", compute = FILL_SPIRV })
        result("and no compute pipeline is made", made == nil, tostring(err))
        return
    end
    local fill, why = rd.CreatePipeline({ kind = "compute", name = "selftest fill", compute = FILL_SPIRV })
    result("a compute pipeline of the selftest's SPIR-V", fill ~= nil and fill:PipelineReady() == true, tostring(why))
    local rows, rowsWhy = rd.CreatePipeline({ kind = "fullscreen", name = "selftest rows", fragment = ROWS_SPIRV, blend = "alpha" })
    result("a full-screen pipeline reading rows by address", rows ~= nil, tostring(rowsWhy))
    local buffer = rd.CreateBuffer(64 * 16)
    result("a buffer a dispatch fills", buffer ~= nil)
    if not (fill and rows and buffer) then return end
    compute.fill, compute.rows, compute.buffer = fill, rows, buffer
    -- 300 matrices, past the client's 256 bones: the last row of the last one green, a fifth opaque.
    compute.matrices = {}
    for i = 1, 300 * 12 do compute.matrices[i] = 0 end
    compute.matrices[299 * 12 + 10], compute.matrices[299 * 12 + 12] = 1, 0.2
    -- Orange, a fifth opaque, as unpackUnorm4x8 reads it.
    local orange = 255 + 128 * 256 + 51 * 16777216
    compute.id = Observe("RenderScript", "OnRenderPoint", function(point)
        if point ~= "after_world" or not compute.mode then return end
        if compute.mode == "dispatch" then
            local at = compute.buffer:BufferAddress()
            if not at or at == 0 then return end
            local ok, err = rd.Dispatch(compute.fill, 1, 1, 1, addressBytes(at) .. u32(64) .. u32(orange))
            if ok then compute.dispatched = compute.dispatched + 1 else compute.refused = err end
            if rd.Begin({ colors = { "scene_color" } }) then
                if compute.inPass == nil then compute.inPass = select(2, rd.Dispatch(compute.fill, 1, 1, 1)) or "accepted" end
                rd.Fullscreen(compute.rows, addressBytes(at) .. u32(17))
                rd.End()
            end
        else
            local at, err = rd.BonePalette(compute.matrices)
            if not at then
                compute.refused = err
                return
            end
            compute.palettes = compute.palettes + 1
            if rd.Begin({ colors = { "scene_color" } }) then
                rd.Fullscreen(compute.rows, addressBytes(at) .. u32(299 * 3 + 2))
                rd.End()
            end
        end
    end)
    compute.mode = "dispatch"
    print("look: the world tinted orange for two seconds (a buffer a dispatch filled), then green for two (a palette of 300 matrices)")
    wait(2)
end)

step(function()
    if not compute.id then return end
    compute.mode = "palette"
    wait(2)
end)

step(function()
    if not compute.id then return end
    compute.mode = nil
    Unobserve(compute.id)
    result("a dispatch filled the buffer each frame (" .. compute.dispatched .. " in two seconds)", compute.dispatched > 0, tostring(compute.refused))
    result("a dispatch inside a pass is refused", compute.inPass == "invalid", tostring(compute.inPass))
    result("a palette of 300 matrices was written each frame (" .. compute.palettes .. " in two seconds)", compute.palettes > 0,
           tostring(compute.refused))
    local outside, why = wxl.render.Dispatch(compute.fill, 1, 1, 1)
    result("a dispatch outside a point is refused", outside == nil and why == "invalid", tostring(why))
    compute.fill:DestroyPipeline()
    compute.rows:DestroyPipeline()
    compute.buffer:DestroyBuffer()
    compute.fill, compute.rows, compute.buffer = nil, nil, nil
end)

-- Phase 26: the ground (wxl.terrain) ------------------------------------------------------------

-- The chunks announced since the mod loaded: the world loads around the player before phase 1.
local ground = { loaded = 0, unloaded = 0, edited = 0 }
if wxl.runtime.Raises and wxl.runtime.Raises("WorldScript", "OnTerrainChunkLoaded") then
    Observe("WorldScript", "OnTerrainChunkLoaded", function(map, x, y, edited)
        ground.loaded = ground.loaded + 1
        if edited then ground.edited = ground.edited + 1 end
    end)
    Observe("WorldScript", "OnTerrainChunkUnloaded", function() ground.unloaded = ground.unloaded + 1 end)
end

step(function()
    print("phase 26: the ground (wxl.terrain)")
    local tr = wxl.terrain
    if not (tr and tr.SetHeights) then
        result("wxl.terrain", false, "this runtime is older than abi 0.1.35")
        return
    end
    result("TerrainChunkLoaded announced the chunks around the player (" .. ground.loaded .. " loaded, " ..
           ground.unloaded .. " unloaded)", ground.loaded > 0)
    local me = wxl.object.LocalPlayer()
    local feet = me and me:Position()
    if not feet then
        skip("the ground", "no player in the world")
        return
    end
    local presence = tr.Presence(feet.x, feet.y)
    if presence ~= "ground" then
        skip("the ground", "there is no ground under the player here (" .. tostring(presence) ..
             "): a map without terrain, or a hole; stand outdoors on the ground")
        return
    end
    local h = tr.Height(feet.x, feet.y)
    if h and math.abs(h - feet.z) < 1 then
        result("the ground's height under the player is where the player stands", true, string.format("%.2f", h))
    else
        print(string.format("NOTE the ground's height under the player is %.2f, the player at %.2f: standing on something else",
                            h or -1, feet.z))
    end
    -- A brush in world space: every vertex within three yards of a point six yards north, each
    -- lowered three yards; one vertex on a chunk's edge or not, VerticesIn names each once.
    local cx, cy = feet.x + 6, feet.y
    local brush = tr.VerticesIn(cx, cy, 3)
    local edits, centre, seen, once = {}, nil, {}, true
    for _, v in ipairs(brush) do
        local key = string.format("%.2f %.2f", v.x, v.y)
        once = once and not seen[key]
        seen[key] = true
        if v.loaded then
            edits[#edits + 1] = { chunk_x = v.chunk_x, chunk_y = v.chunk_y, index = v.index, height = v.z - 3 }
            if not centre or (v.x - cx) ^ 2 + (v.y - cy) ^ 2 < (centre.x - cx) ^ 2 + (centre.y - cy) ^ 2 then centre = v end
        end
    end
    result("VerticesIn lists the brush's vertices, each once (" .. #brush .. ")", #brush >= 3 and once and centre ~= nil)
    if not centre then return end
    local v = centre
    ground.v = v
    local ok, why = tr.SetHeights(edits)
    result("SetHeights lowers the brush three yards", ok == true, tostring(why))
    local now, before = tr.Height(v.x, v.y), tr.Height(v.x, v.y, true)
    result("the height there follows the edit", now ~= nil and math.abs(now - (v.z - 3)) < 0.05,
           string.format("%.2f, expected %.2f", now or -1, v.z - 3))
    result("and the original height stays readable", before ~= nil and math.abs(before - v.original_z) < 0.05,
           string.format("%.2f", before or -1))
    local again = tr.VerticesIn(v.x, v.y, 0.01)[1]
    result("VerticesIn reads the edited height and the original", again ~= nil and math.abs(again.z - (v.z - 3)) < 0.05 and
           math.abs(again.original_z - v.original_z) < 0.05)
    local hit = tr.Trace({ x = v.x, y = v.y, z = v.z + 5 }, { x = v.x, y = v.y, z = v.z - 10 })
    result("wxl.terrain.Trace meets the dip", hit ~= nil and math.abs(hit.point.z - (v.z - 3)) < 0.05 and hit.normal.z > 0,
           hit and string.format("%.2f", hit.point.z) or "no hit")
    local client = wxl.world.Trace({ x = v.x, y = v.y, z = v.z + 5 }, { x = v.x, y = v.y, z = v.z - 10 })
    result("the client's own ray test (wxl.world.Trace) meets the dip", client ~= nil and math.abs(client.z - (v.z - 3)) < 0.1,
           client and string.format("%.2f", client.z) or "no hit")
    local stats = tr.Stats()
    if stats then
        print(string.format("NOTE the edit wrote %d vertices in %d chunks in %d us (longest so far %d us)", stats.last_vertices,
                            stats.last_chunks, stats.last_microseconds, stats.max_microseconds))
    end
    print("look: a dip three yards deep and six wide, six yards north of you, for two seconds, its light on the slopes")
    wait(2)
end)

step(function()
    local v = ground.v
    if not v then return end
    local tr = wxl.terrain
    result("ResetAll takes the brush off", tr.ResetAll() == true)
    local now = tr.Height(v.x, v.y)
    result("and the ground is back", now ~= nil and math.abs(now - v.z) < 0.05, string.format("%.2f", now or -1))
    result("nothing is left to reset", select(2, tr.ResetAll()) == "not_found")
    ground.v = nil
end)

-- Phase 27: shapes in the world (wxl.draw) -------------------------------------------------------

local shapes = { kept = {}, frame = nil, frames = 0 }

step(function()
    print("phase 27: shapes in the world (wxl.draw)")
    local dr = wxl.draw
    if not (dr and dr.Add) then
        result("wxl.draw", false, "this runtime is older than abi 0.1.36")
        return
    end
    local supported = dr.Supported()
    result("wxl.draw draws on this device", supported == true, dr.UnsupportedReason())
    if not supported then return end
    local me = wxl.object.LocalPlayer()
    local feet = me and me:Position()
    if not feet then
        skip("shapes in the world", "no player in the world")
        return
    end
    local f = me:Facing() or 0
    local function ahead(d, side)
        side = side or 0
        return { x = feet.x + d * math.cos(f) - side * math.sin(f), y = feet.y + d * math.sin(f) + side * math.cos(f), z = feet.z }
    end
    local seconds = 6
    local function add(name, shape, style)
        style.fade_out = 1
        local s, why = dr.Add(shape, style, seconds)
        result("Add keeps " .. name, s ~= nil, tostring(why))
        if s then shapes.kept[#shapes.kept + 1] = s end
        return s
    end
    -- On the ground, in a fan in front of the player: a filled circle, a ring, a cone, a rectangle,
    -- a concave polygon; a line along the ground; in the air, a box, a sphere and a cylinder.
    add("a circle", { kind = "circle", center = ahead(8, -8), radius = 3 }, { color = 0xFF4040, fill_alpha = 0.35, width = 2 })
    add("a ring", { kind = "ring", center = ahead(8, 0), radius = 3, inner = 2 }, { color = 0x40FF40, fill_alpha = 0.35, width = 2 })
    add("a cone", { kind = "sector", center = ahead(2, 0), radius = 12, facing = f, angle = 0.8 },
        { color = 0xFF8000, fill_alpha = 0.2, width = 3, pulse = 1 })
    add("a rectangle", { kind = "rectangle", center = ahead(8, 8), length = 6, width = 3, facing = f + 0.5 },
        { color = 0x4080FF, fill_alpha = 0.35, width = 2 })
    add("a polygon", { kind = "polygon", center = ahead(16, 0), points = { { 0, 0, 0 }, { 4, 0, 0 }, { 4, 4, 0 }, { 2, 1, 0 }, { 0, 4, 0 } } },
        { color = 0xFFFF40, fill_alpha = 0.35, width = 2 })
    add("a line on the ground", { kind = "line", points = { ahead(4, -12), ahead(14, -12), ahead(14, 12) }, on_ground = true },
        { color = 0xFFFFFF, width = 4 })
    local up = function(p, h) return { x = p.x, y = p.y, z = p.z + h } end
    add("a box", { kind = "box", center = up(ahead(16, -8), 2), size = { x = 2, y = 2, z = 2 }, facing = f, pitch = 0.3 },
        { color = 0x40FFFF, fill_alpha = 0.25, width = 2 })
    add("a sphere", { kind = "sphere", center = up(ahead(16, 8), 2), radius = 1.5 }, { color = 0xFF40FF, fill_alpha = 0.25, width = 1.5 })
    add("a cylinder, on top of everything", { kind = "cylinder", center = up(ahead(24, 0), 2), radius = 1.5, height = 4 },
        { color = 0xFFFFFF, fill_alpha = 0.15, width = 1.5, depth = "on_top" })
    local ring = add("a ring under the player", { kind = "ring", radius = 1.5, inner = 1.2 }, { color = 0x00FFFF, fill_alpha = 0.5, width = 1 })
    if ring then result("Attach makes it follow the player", ring:Attach(me, 1) == true) end
    -- Each frame, a grid on the terrain's own cells under the player (immediate mode).
    shapes.frames = 0
    shapes.frame = Observe("RenderScript", "OnFrameBegin", function()
        local at = me:Position()
        if not at then return end
        if dr.Draw({ kind = "grid", center = at, length = 16, width = 16, spacing = 4.1666665 }, { color = 0xC0C0C0, alpha = 0.6, width = 1 }) then
            shapes.frames = shapes.frames + 1
        end
    end)
    local bad, why = dr.Draw({ kind = "circle", center = feet, radius = 0 })
    result("a shape of no radius is refused", bad == nil and why == "invalid", tostring(why))
    print("look: in front of you for six seconds then fading: a red disc, a green ring, an orange cone beating, a blue rectangle," ..
          " a yellow concave polygon, a white line along the ground, a cyan box tilted, a magenta sphere, a white cylinder seen" ..
          " through walls; a cyan ring under you that follows you; a grey grid on the ground's cells under you; all lying on the" ..
          " slopes, hidden behind walls and units except the cylinder, the same under Direct3D 9 and Vulkan")
    wait(3)
end)

step(function()
    local dr = wxl.draw
    if not (dr and dr.Stats and shapes.frame) then return end
    local stats = dr.Stats()
    result("the frames draw the shapes", stats ~= nil and stats.drawn >= 10 and stats.vertices > 0,
           stats and string.format("%d shapes, %d vertices", stats.drawn, stats.vertices) or "no stats")
    result("the immediate grid is drawn every frame", shapes.frames > 10 and stats ~= nil and stats.immediate == 1,
           tostring(shapes.frames) .. " frames")
    if stats then
        print(string.format("NOTE wxl.draw's last frame: %d shapes, %d vertices, %.0f us on the CPU (the most %.0f us), %.0f us in Draw calls",
                            stats.drawn, stats.vertices, stats.build_microseconds, stats.max_build_microseconds, stats.immediate_microseconds))
    end
    wait(4.5)
end)

step(function()
    local dr = wxl.draw
    if not (dr and dr.Stats and shapes.frame) then return end
    Unobserve(shapes.frame)
    shapes.frame = nil
    local stats = dr.Stats()
    result("kept shapes go when their duration runs out", stats ~= nil and stats.shapes == 0,
           stats and tostring(stats.shapes) .. " left" or "no stats")
    dr.Clear()
    shapes.kept = {}
end)

-- Phase 28: animations on units (wxl.animation) ---------------------------------------------------

local anims = { ended = {}, observer = nil }

step(function()
    print("phase 28: an animation played on the player (wxl.animation)")
    local an = wxl.animation
    if not (an and an.Play) then
        result("wxl.animation", false, "this runtime is older than abi 0.1.37")
        return
    end
    local me = wxl.object.LocalPlayer()
    if not me then
        skip("animations on units", "no player in the world")
        return
    end
    anims.ended = {}
    anims.observer = Observe("ObjectScript", "OnUnitAnimationEnded", function(unit, anim_id, reason, looped)
        anims.ended[#anims.ended + 1] = { anim_id = anim_id, reason = reason, looped = looped }
    end)
    local bad, why = an.Play(me, 600)
    result("an id past AnimationData's is refused", bad == nil and why == "invalid", tostring(why))
    local ok, err = an.Play(me, 62)
    result("Play swings the player once (EmoteWork, the mining swing)", ok == true, tostring(err))
    result("Playing answers it", an.Playing(me) == 62, tostring(an.Playing(me)))
    local shown = an.Current(me)
    result("the model shows it (or the animation it falls back to)", shown ~= nil and shown ~= 0, tostring(shown))
    print("look: stand still: your character swings once as when mining, then stands again")
    wait(4)
end)

step(function()
    local an = wxl.animation
    if not (an and anims.observer) then return end
    local first = anims.ended[1]
    result("the one-shot ends by itself, finished", first ~= nil and first.anim_id == 62 and first.reason == "finished" and first.looped == false,
           first and (first.anim_id .. " " .. first.reason) or "no OnUnitAnimationEnded (did you move?)")
    result("and the player is handed back to the game", an.Playing(wxl.object.LocalPlayer()) == nil)
    anims.ended = {}
    local ok, err = an.Play(wxl.object.LocalPlayer(), 62, 1, 1.5)
    result("a loop at one and a half times the speed", ok == true, tostring(err))
    print("look: you swing again and again, faster, for three seconds; walk a step: your legs walk, the swing comes back when you stop")
    wait(3)
end)

step(function()
    local an = wxl.animation
    if not (an and anims.observer) then return end
    local me = wxl.object.LocalPlayer()
    result("the loop is still played after its length", #anims.ended == 0 and an.Playing(me) == 62,
           #anims.ended > 0 and anims.ended[1].reason or "")
    result("Stop ends it", an.Stop(me) == true)
    local again, why = an.Stop(me)
    result("and only once", again == nil and why == "not_found", tostring(why))
    wait(0.5)
end)

step(function()
    local an = wxl.animation
    if not (an and anims.observer) then return end
    local last = anims.ended[#anims.ended]
    result("OnUnitAnimationEnded says stopped, looped", last ~= nil and last.reason == "stopped" and last.looped == true,
           last and last.reason or "nothing")
    Unobserve(anims.observer)
    anims.observer = nil
end)

-- The end ----------------------------------------------------------------------------------------

step(function()
    local summary = string.format("done: %d passed, %d failed, %d skipped", passed, failed, skipped)
    print(summary)
    chat(summary .. (failed == 0 and "" or " (the log says which)"))
end)

print("ready: the checks start a few seconds after entering the world")
