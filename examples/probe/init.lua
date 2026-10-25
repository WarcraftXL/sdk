-- probe: every event the runtime raises, written into the log as it happens, so that a test
-- session can be read back from Logs/WarcraftXL.log alone. The first few occurrences of each
-- event are logged with their arguments, then a line of counts every minute, and at exit the
-- events that never came. /mark <text> in the chat box writes a marker line between two steps
-- of a test; with a steps.txt next to it, /mark alone shows the next step of the plan.
--
-- A test tool, not a mod to keep installed: listening to everything makes the client do the work
-- of every event, which it skips while nobody listens (see README.md).

local FIRST    = 5    -- occurrences of each event logged with their arguments
local PERIOD   = 60   -- seconds between two lines of counts
local TEXT     = 80   -- characters of a string argument kept in the log
local BYTES    = 16   -- bytes of a binary argument shown, in hex
local PER_LINE = 900  -- characters of a list before it continues on the next line

-- Events left alone, by name: "OnModelLoad" copies every model file into a Lua string for the
-- handler, for example. Add a name here to take an event out of the probe.
local SKIP = {
    -- One per model batch drawn, thousands a frame: a Lua call for each would slow the whole session.
    OnModelBatchDrawn = true,
    -- Listening has the runtime open and draw a frame of Dear ImGui every frame, for nothing.
    OnImGuiFrame = true,
}

-- Events that reach only the mods that asked for them: their absence from a probe's log is
-- expected, and the "never seen" line says why.
local ON_REQUEST = {
    OnObjectFieldChanged = "reaches only a mod that watches a field (wxl.object.Watch); the probe watches none",
}

-- --- what an argument looks like in the log -----------------------------------------------

local function number(value)
    if value == math.floor(value) and math.abs(value) < 2 ^ 53 then
        return string.format("%d", value)
    end
    return string.format("%.6g", value)
end

-- Whether a string is UTF-8 text: well-formed sequences (shortest form, no surrogates, at most
-- U+10FFFF) and no control character other than a tab or a line break.
local function utf8(value)
    if value:find("[%z\1-\8\11\12\14-\31\127]") then return false end
    local i, n = 1, #value
    while i <= n do
        local lead = value:byte(i)
        local length, low, high = 1, 0x80, 0xBF
        if lead < 0x80 then length = 1
        elseif lead >= 0xC2 and lead <= 0xDF then length = 2
        elseif lead == 0xE0 then length, low = 3, 0xA0
        elseif lead == 0xED then length, high = 3, 0x9F
        elseif lead >= 0xE1 and lead <= 0xEF then length = 3
        elseif lead == 0xF0 then length, low = 4, 0x90
        elseif lead == 0xF4 then length, high = 4, 0x8F
        elseif lead >= 0xF1 and lead <= 0xF3 then length = 4
        else return false end
        if length > 1 then
            if i + length - 1 > n then return false end
            local second = value:byte(i + 1)
            if second < low or second > high then return false end
            for k = i + 2, i + length - 1 do
                local byte = value:byte(k)
                if byte < 0x80 or byte > 0xBF then return false end
            end
        end
        i = i + length
    end
    return true
end

local function text(value)
    -- Anything that is not UTF-8 text is bytes: shown in hex, so the log stays text.
    if not utf8(value) then
        local hex = {}
        for i = 1, math.min(BYTES, #value) do hex[i] = string.format("%02X", value:byte(i)) end
        return string.format("<%d bytes %s%s>", #value, table.concat(hex, " "), #value > BYTES and " ..." or "")
    end
    -- Cut at a character's start, never inside one.
    local cut = math.min(TEXT, #value)
    while cut < #value and cut > 0 and value:byte(cut + 1) >= 0x80 and value:byte(cut + 1) <= 0xBF do
        cut = cut - 1
    end
    local shown = value:sub(1, cut):gsub("\\", "\\\\"):gsub("\n", "\\n"):gsub("\r", "\\r"):gsub("\"", "\\\"")
    if cut < #value then return string.format("\"%s...\"(%d)", shown, #value) end
    return "\"" .. shown .. "\""
end

local function show(value)
    local kind = type(value)
    if kind == "number" then return number(value) end
    if kind == "string" then return text(value) end
    if kind == "table" then
        if value.x and value.y and value.z then
            return string.format("(%.2f, %.2f, %.2f)", value.x, value.y, value.z)
        end
        local parts = {}
        for k, v in pairs(value) do
            parts[#parts + 1] = tostring(k) .. "=" .. (type(v) == "table" and "{...}" or show(v))
            if #parts == 8 then parts[#parts + 1] = "..."; break end
        end
        return "{" .. table.concat(parts, ", ") .. "}"
    end
    if kind == "userdata" then
        -- An object: its kind and handle, and its name while it is in view.
        local shown = tostring(value)
        local ok, name = pcall(function() return value:Name() end)
        if ok and type(name) == "string" and name ~= "" then shown = shown .. " " .. text(name) end
        return shown
    end
    return tostring(value)
end

local function arguments(...)
    local count = select("#", ...)
    local parts = {}
    for i = 1, count do parts[i] = show((select(i, ...))) end
    return table.concat(parts, ", ")
end

-- Writes "<head>: a, b, c", continued over as many lines as it takes to keep each one under
-- PER_LINE characters: "<head> (2): d, e".
local function list(head, items)
    if #items == 0 then
        print(head .. ": none")
        return
    end
    local line, length, part = {}, 0, 1
    local function flush()
        print((part == 1 and head or string.format("%s (%d)", head, part)) .. ": " .. table.concat(line, ", "))
        line, length, part = {}, 0, part + 1
    end
    for _, item in ipairs(items) do
        if length + #item > PER_LINE and #line > 0 then flush() end
        line[#line + 1] = item
        length = length + #item + 2
    end
    flush()
end

-- --- the events ------------------------------------------------------------------------------

local probes = {}       -- one per event subscribed: { name, total, period, mark }
local unraised = {}     -- events this runtime never raises
local refused = {}      -- events the probe could not subscribe to, with why

-- Every family's events, from wxl.lua's own tables: what this wxl.lua knows, whatever its
-- version, and not a list written here.
local families = {}
for family in pairs(wxl.events) do families[#families + 1] = family end
table.sort(families)

for _, family in ipairs(families) do
    for _, name in ipairs(wxl.events[family]) do
        local raises = wxl.runtime.Raises(family, name)
        if SKIP[name] then
            refused[#refused + 1] = name .. " (skipped)"
        elseif raises == false then
            unraised[#unraised + 1] = name
        else
            -- raises is true, or nil on a runtime that cannot say: subscribe and see.
            local probe = { name = name, total = 0, period = 0, mark = 0 }
            local ok, problem = pcall(Observe, family, name, function(...)
                local total = probe.total + 1
                probe.total = total
                probe.period = probe.period + 1
                probe.mark = probe.mark + 1
                if total <= FIRST then
                    print(string.format("%s #%d%s: %s", name, total, total == FIRST and " (last shown)" or "",
                                        arguments(...)))
                end
            end)
            if ok then
                probes[#probes + 1] = probe
            else
                refused[#refused + 1] = name .. " (" .. tostring(problem):gsub("^.-: ", "") .. ")"
            end
        end
    end
end

print(string.format("listening to %d events, the first %d of each with their arguments, counts every %d s",
                    #probes, FIRST, PERIOD))
list("not raised by this runtime", unraised)
if #refused > 0 then list("not listened to", refused) end

-- --- counts ------------------------------------------------------------------------------------

-- "OnUpdate 3600, OnFrameEnd 3600, ..." for every probe whose `field` is not 0, largest first;
-- the field is reset when `reset` is set.
local function counts(field, reset)
    local seen = {}
    for _, probe in ipairs(probes) do
        if probe[field] > 0 then seen[#seen + 1] = probe end
    end
    table.sort(seen, function(a, b)
        if a[field] ~= b[field] then return a[field] > b[field] end
        return a.name < b.name
    end)
    local items, raises = {}, 0
    for i, probe in ipairs(seen) do
        items[i] = probe.name .. " " .. probe[field]
        raises = raises + probe[field]
        if reset then probe[field] = 0 end
    end
    return items, raises
end

local minute, elapsed, clock = 0, 0, 0

registerForEvent("onUpdate", function(delta)
    clock = clock + delta
    elapsed = elapsed + delta
    if elapsed < PERIOD then return end
    elapsed = elapsed - PERIOD
    minute = minute + 1
    local items, raises = counts("period", true)
    list(string.format("minute %d: %d events, %d raises", minute, #items, raises), items)
end)

registerForEvent("onShutdown", function()
    local items, raises = counts("total", false)
    list(string.format("summary: %d of %d events seen, %d raises", #items, #probes, raises), items)
    local never = {}
    for _, probe in ipairs(probes) do
        if probe.total == 0 then
            local why = ON_REQUEST[probe.name]
            never[#never + 1] = why and (probe.name .. " (" .. why .. ")") or probe.name
        end
    end
    list("never seen", never)
end)

-- --- marks ------------------------------------------------------------------------------------

local marks, markedAt = 0, 0
local footprint = wxl.footprint

local function mb(bytes) return string.format("%.1f MB", (bytes or 0) / 1048576) end

-- One line on the memory: the address space and the largest consumers, from a new snapshot.
local function memory()
    if not footprint then return "no wxl.footprint" end
    local taken, why = footprint.Snapshot()
    if not taken then return "no snapshot: " .. tostring(why) end
    local parts = {}
    for _, region in ipairs(footprint.Regions() or {}) do
        if region.state == "free" then
            parts[#parts + 1] = "free " .. mb(region.bytes) .. ", largest block " .. mb(region.largest)
        elseif region.state == "committed" then
            parts[#parts + 1] = "committed " .. region.type .. " " .. mb(region.bytes)
        end
    end
    local categories = footprint.Categories() or {}
    for i = 1, math.min(5, #categories) do
        parts[#parts + 1] = categories[i].name .. " " .. mb(categories[i].bytes)
    end
    if not footprint.Running() then parts[#parts + 1] = "Storm not counted (memory-report starts it)" end
    return table.concat(parts, "; ")
end

-- A test plan, when the probe's folder has one: steps.txt, one step per line, blank lines and
-- lines starting with # left out. /mark with no text then moves to the next step and shows it in
-- the chat box, so the person testing reads the plan in the game and the log says which step
-- each mark began.
local steps, step = {}, 0
do
    local file = io.open("steps.txt", "r")
    if file then
        for line in file:lines() do
            line = line:gsub("^%s+", ""):gsub("%s+$", "")
            if line ~= "" and line:sub(1, 1) ~= "#" then steps[#steps + 1] = line end
        end
        file:close()
        print(string.format("%d step(s) read from steps.txt", #steps))
    end
end

-- "step 3/19: <what to do>" for step n, or what comes after the last one.
local function describe(n)
    if #steps == 0 then return "no steps.txt: /mark <text> writes a mark" end
    if n == 0 then return string.format("%d steps: /mark shows the first", #steps) end
    if n > #steps then return string.format("after the last of %d steps", #steps) end
    return string.format("step %d/%d: %s", n, #steps, steps[n])
end

-- Writes the mark and answers its number and the line the chat box shows.
local function mark(note)
    marks = marks + 1
    note = tostring(note or ""):gsub("^%s+", ""):gsub("%s+$", "")
    if note == "" and #steps > 0 then
        step = step + 1
        note = describe(step)
    end
    print(string.format("==== MARK %d: %s ====", marks, note))
    -- The same mark on the profiler's timeline, when a Tracy viewer is connected.
    if wxl.profile then wxl.profile.Message(string.format("MARK %d: %s", marks, note)) end
    local items, raises = counts("mark", true)
    local since = marks == 1 and "since the probe loaded" or "since mark " .. (marks - 1)
    list(string.format("%s (%.1f s): %d events, %d raises", since, clock - markedAt, #items, raises), items)
    print("memory at mark " .. marks .. ": " .. memory())
    markedAt = clock
    return marks, note
end

-- From the client's own Lua: /mark [text], /step (the current step again), or
-- /run WxlProbeMark("text").
if wxl.script then
    local registered, why = wxl.script.RegisterFunction("WxlProbeMark", mark, "game")
    if not registered then print("no /mark: " .. tostring(why)) end
    wxl.script.RegisterFunction("WxlProbeStep", function() return describe(step) end, "game")
end

-- The chat box treats | as the start of an escape: a step's text shows it doubled.
local SLASH = [[
local function show(text)
    DEFAULT_CHAT_FRAME:AddMessage("|cffffd200wxl.probe|r " .. tostring(text):gsub("|", "||"))
end
SLASH_WXLPROBEMARK1 = "/mark"
SlashCmdList["WXLPROBEMARK"] = function(text)
    local n, shown = WxlProbeMark(text)
    show("mark " .. tostring(n) .. ": " .. tostring(shown))
end
SLASH_WXLPROBESTEP1 = "/step"
SlashCmdList["WXLPROBESTEP"] = function() show(WxlProbeStep()) end
if WXL_PROBE_GREETED == nil and WxlProbeStep():find("^%d+ steps") then
    WXL_PROBE_GREETED = true
    show(WxlProbeStep())
end
]]

-- The chat's slash commands exist once the interface is loaded, and again after each /reload.
Observe("LuaScript", "OnUiEvent", function(_, name)
    if name == "PLAYER_ENTERING_WORLD" and wxl.script then wxl.script.Run(SLASH, "wxl.probe") end
end)
