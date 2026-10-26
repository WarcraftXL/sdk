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

-- The end ----------------------------------------------------------------------------------------

step(function()
    local summary = string.format("done: %d passed, %d failed, %d skipped", passed, failed, skipped)
    print(summary)
    chat(summary .. (failed == 0 and "" or " (the log says which)"))
end)

print("ready: the checks start a few seconds after entering the world")
