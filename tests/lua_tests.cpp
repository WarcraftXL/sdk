// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// wxl.lua against the mod host. Run with the wxl-lua binary, the lua-hello example's folder and
// a scratch directory:
//
//     wxl-lua-tests <wxl-lua.dll> <examples/lua-hello> <scratch>
//
// A mods tree is written into the scratch directory: wxl.lua itself, the example, and Lua mods
// written here that exercise what the example does not. The host loads wxl.lua through the OS
// loader, the test raises events and reads the log.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "fake_services.hpp"
#include "wxl/objects/fields.hpp"
#include "wxl/host/event_bus.hpp"
#include "wxl/host/log.hpp"
#include "wxl/host/manifest.hpp"
#include "wxl/host/mod_host.hpp"
#include "wxl/lua.h"
#include "wxl/reflect.hpp"

#include "../lua/src/ObjectNames.hpp"

namespace fs = std::filesystem;
using namespace wxl::host;

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* what)
    {
        if (condition) return;
        std::printf("  FAIL  %s\n", what);
        ++g_failures;
    }

    void Section(const char* title) { std::printf("%s\n", title); }

    bool Contains(const std::string& text, const char* needle)
    {
        return text.find(needle) != std::string::npos;
    }

    class CaptureSink final : public LogSink
    {
    public:
        void Write(wxl_log_level level, std::string_view tag, std::string_view message) override
        {
            lines.push_back(FormatLine(level, tag, message));
        }

        bool Has(const char* needle) const { return Count(needle) != 0; }

        size_t Count(const char* needle) const
        {
            size_t count = 0;
            for (const std::string& line : lines)
                if (Contains(line, needle)) ++count;
            return count;
        }

        /// Prints the lines, for a failing run.
        void Dump() const
        {
            for (const std::string& line : lines) std::printf("        %s\n", line.c_str());
        }

        std::vector<std::string> lines;
    };

    void WriteFile(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream << text;
    }

    std::string ReadFile(const fs::path& path)
    {
        std::ifstream      stream(path, std::ios::binary);
        std::ostringstream text;
        text << stream.rdbuf();
        return text.str();
    }

#define WXL_TEST_STR_(x) #x
#define WXL_TEST_STR(x)  WXL_TEST_STR_(x)

    constexpr const char* kAbi = WXL_TEST_STR(WXL_ABI_VERSION_MAJOR) "." WXL_TEST_STR(WXL_ABI_VERSION_MINOR) "."
        WXL_TEST_STR(WXL_ABI_VERSION_FEATURE);

    /// A script mod's manifest: no binaries, a lua entry.
    std::string LuaManifest(const char* id, const std::string& extra = std::string())
    {
        std::string text = "{\n  \"id\": \"";
        text += id;
        text += "\",\n  \"name\": \"";
        text += id;
        text += "\",\n  \"version\": \"1.0.0\",\n  \"abi\": \"";
        text += kAbi;
        text += "\",\n  \"lua\": \"init.lua\"";
        if (!extra.empty()) text += ",\n  " + extra;
        text += "\n}\n";
        return text;
    }

    /// wxl.lua's own manifest for this architecture.
    std::string RuntimeManifest()
    {
        std::string text = "{\n  \"id\": \"wxl.lua\",\n  \"name\": \"Lua\",\n  \"version\": \"0.1.0\",\n"
                           "  \"abi\": \"";
        text += kAbi;
        text += "\",\n  \"binaries\": { \"";
        text += CurrentTarget();
        text += "\": \"bin/wxl-lua.dll\" }\n}\n";
        return text;
    }

    /// The mods tree: wxl.lua, the example, and whatever else the section adds.
    fs::path MakeTree(const fs::path& scratch, const char* name, const fs::path& binary,
                      const fs::path& example)
    {
        const fs::path mods = scratch / name;
        fs::remove_all(mods);
        fs::create_directories(mods / "wxl.lua" / "bin");
        fs::copy_file(binary, mods / "wxl.lua" / "bin" / "wxl-lua.dll");
        WriteFile(mods / "wxl.lua" / "mod.json", RuntimeManifest());
        if (!example.empty())
            fs::copy(example, mods / "lua-hello", fs::copy_options::recursive);
        return mods;
    }

    /// A Lua mod that calls every generated object method from 0.1.18 on, on an object of a
    /// kind it applies to, and prints "row <name> <Lua type of the answer>" or why not.
    std::string EveryRowMod()
    {
        namespace names = wxl::lua::names;
        std::string lua = "local objects = {}\n"
                          "Observe('ObjectScript', 'OnObjectCreate', function(object, guid, type) objects[type] = object end)\n"
                          "local function show(name, ok, value, why)\n"
                          "    if not ok then print('row ' .. name .. ' error ' .. tostring(value))\n"
                          "    elseif value == nil then print('row ' .. name .. ' nil ' .. tostring(why))\n"
                          "    else print('row ' .. name .. ' ' .. type(value) .. ' ' .. tostring(type(value) == 'string' and value or '')) end\n"
                          "end\n"
                          "registerForEvent('onUpdate', function()\n"
                          "    if not objects.Item or done then return end\n"
                          "    done = true\n";
        const std::vector<names::RowInfo>& rows  = names::Rows();
        const std::vector<std::string>&    named = names::RowNames();
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const names::RowInfo& row = rows[i];
            if (row.since[2] < 18) continue;
            const std::string kinds = row.kinds_name;
            std::string on = kinds == "Players" || kinds == "Active" ? "objects.Player"
                           : kinds == "Items" || kinds == "Containers" ? "objects.Item"
                           : kinds == "GameObjects" ? "objects.GameObject" : "objects.Unit";
            std::string arg;
            if (*row.arg_c)
            {
                const wxl::reflect::Enum* e = wxl::reflect::FindEnum(row.arg);
                arg = std::strcmp(row.arg_c, "wxl_handle") == 0 ? "objects.Player"
                    : std::strcmp(row.arg_c, "wxl_vec3") == 0   ? "{ x = 1, y = 2, z = 3 }"
                    : e ? std::string("'") + e->values[1].name + "'" : "1";
            }
            lua += "    show('" + std::string(row.member) + "', pcall(function() return " + on + ":" + named[i] + "(" + arg + ") end))\n";
        }
        lua += "    show('Auras', pcall(function() return objects.Unit:Auras() end))\n"
               "    show('Skills', pcall(function() return objects.Player:Skills() end))\n"
               "    show('PartyMembers', pcall(function() return objects.Player:PartyMembers() end))\n"
               "    show('Select', pcall(function() return objects.Unit:Select() end))\n"
               "    show('CastAt', pcall(function() return objects.Unit:CastAt(116) end))\n"
               "    show('Use', pcall(function() return objects.Item:Use(objects.Unit) end))\n"
               "    show('UseOnUnit', pcall(function() return objects.Unit:Use() end))\n"
               "    show('CastSpell', pcall(function() return wxl.action.CastSpell(133, objects.Unit) end))\n"
               "    show('CastSpellBare', pcall(function() return wxl.action.CastSpell(133) end))\n"
               "    show('MoveTo', pcall(function() return wxl.action.MoveTo({ x = 1, y = 2, z = 3 }) end))\n"
               "    show('BadName', pcall(function() return objects.Unit:Speed('sprint') end))\n"
               "end)\n";
        return lua;
    }

    /// The Lua type a row's answer should arrive as.
    std::string LuaTypeOf(const wxl::lua::names::RowInfo& row)
    {
        if (wxl::reflect::FindEnum(row.cxx)) return "string";
        if (std::strcmp(row.type, "wxl_bool") == 0) return "boolean";
        if (std::strcmp(row.type, "wxl_handle") == 0) return "userdata";
        if (std::strcmp(row.type, "wxl_guid") == 0) return "string";
        if (std::strcmp(row.type, "wxl_vec3") == 0 || std::strcmp(row.type, "wxl_aura") == 0 ||
            std::strcmp(row.type, "wxl_skill") == 0)
            return "table";
        return "number";
    }

    HostConfig Config(const fs::path& mods, CaptureSink& sink)
    {
        HostConfig config;
        config.mods_directory = mods;
        config.runtime_id     = "test";
        config.runtime_name   = "Test runtime";
        config.log_sink       = &sink;
        config.events         = AllEvents();
        config.services.push_back(PublishedService{WXL_OBJECT_SERVICE, WXL_OBJECT_SERVICE_VERSION,
                                                   wxltest::FakeObjects::Table()});
        config.services.push_back(PublishedService{WXL_ACTION_SERVICE, WXL_ACTION_SERVICE_VERSION,
                                                   wxltest::FakeActions::Table()});
        return config;
    }

    const RefusedMod* FindRefused(const ModHost& host, const char* id)
    {
        for (const RefusedMod& mod : host.Refused())
            if (mod.id == id) return &mod;
        return nullptr;
    }

    // --- the probe mod: what the example leaves out ------------------------------------------

    constexpr const char* kProbeV1 = R"lua(
-- the sandbox
local problems = {}
if os.execute ~= nil then problems[#problems + 1] = "os.execute" end
if io.popen ~= nil then problems[#problems + 1] = "io.popen" end
if package.loadlib ~= nil then problems[#problems + 1] = "package.loadlib" end
if pcall(require, "ffi") then problems[#problems + 1] = "ffi" end
if io.open("../lua-hello/init.lua") ~= nil then problems[#problems + 1] = "io.open outside" end
if io.open("sub/data.txt") == nil then problems[#problems + 1] = "io.open inside" end
if pcall(require, "..lua-hello.init") then problems[#problems + 1] = "require outside" end
if require("sub.lib") ~= 42 then problems[#problems + 1] = "require inside" end
if loadstring("\27garbage") ~= nil then problems[#problems + 1] = "bytecode" end
if debug.getupvalue or debug.getregistry or debug.setupvalue then problems[#problems + 1] = "debug" end
if pcall(require, "jit.util") then problems[#problems + 1] = "jit.util" end
if type(debug.traceback()) ~= "string" then problems[#problems + 1] = "debug.traceback" end
if #problems == 0 then print("sandbox ok") else print("sandbox FAIL " .. table.concat(problems, ", ")) end

-- names are checked
local ok, err = pcall(Observe, "World", "OnUpdate", function() end)
if not ok and err:find("WorldScript") and err:find("NetScript") then print("family check ok") end
ok, err = pcall(Observe, "WorldScript", "OnTick", function() end)
if not ok and err:find("OnUpdate") and err:find("OnSoundPlay") then print("method check ok") end
ok, err = pcall(Override, "WorldScript", "OnUpdate", function() end)
if not ok and err:find("cannot be overridden") then print("override check ok") end
ok, err = pcall(registerForEvent, "onFrame", function() end)
if not ok and err:find("onDraw") then print("callback check ok") end
if #wxl.events.WorldScript >= 6 and wxl.events.NetScript[1] == "OnMessageReceived" then print("events table ok") end

registerForEvent("onInit", function() print("probe init v1") end)
registerForEvent("onUpdate", function(delta) print("probe update " .. delta) end)
registerForEvent("onDraw", function() print("probe draw") end)
registerForEvent("onShutdown", function() print("probe shutdown") end)

Observe("WorldScript", "OnUpdate", function(delta) print("probe observed update " .. delta) end)

-- a handler that errors, and one after it that must still run
Observe("WorldScript", "OnWorldEnter", function(map_id)
    error("boom " .. map_id)
end)
Observe("WorldScript", "OnWorldEnter", function(map_id, position)
    print("after-error " .. position.x .. "," .. position.y .. "," .. position.z)
end)

-- what a handler keeps is a copy, readable after the call
local kept
Observe("WorldScript", "OnWorldLeave", function(map_id, position) kept = position end)
Observe("WorldScript", "OnSoundPlay", function(sound_id, path, suppressed)
    if kept and kept.x == 1 and kept.z == 3 then print("kept values stay") end
    print("sound " .. sound_id .. " " .. path .. " suppressed=" .. tostring(suppressed))
end)

-- an Override answers with its return value; an Observe after it sees the answer
Override("WorldScript", "OnInput", function(message) return message == 7 end)
Observe("WorldScript", "OnInput", function(message, wparam, lparam, handled)
    print("input " .. message .. " " .. wparam .. " " .. lparam .. " handled=" .. tostring(handled))
end)

-- return values of the wrong kind, and too many of them
Override("WorldScript", "OnSoundPlay", function() return "loud", 1 end)

-- the off-thread events, and a string too long for the buffer
Override("AssetScript", "OnFileOpen", function(path, redirect)
    if path == "long" then return string.rep("x", 100) end
    if redirect == "" and path:lower() == "interface\\icons\\probe.blp" then return "Interface\\Icons\\Answer.blp" end
end)
Observe("AssetScript", "OnFileOpen", function(path, redirect) print("file open " .. path .. " redirect " .. redirect) end)
Override("NetScript", "OnMessageSent", function(opcode, data)
    print("message sent " .. opcode .. " " .. #data)
    return true
end)

-- the members that only measure another are not parameters
Observe("ObjectScript", "OnSpellCastGo", function(...)
    local caster, caster_guid, target, target_guid, cast_item, spell_id, cast_count, cast_flags,
          target_flags, cast_time, hit_count, hits, misses, miss_reasons = ...
    print("cast go " .. select("#", ...) .. " parameters, spell " .. spell_id .. ", " .. hit_count ..
          " hit in " .. #hits .. " bytes, " .. #miss_reasons .. " miss, caster " .. caster_guid)
end)

-- objects
Observe("ObjectScript", "OnTargetChanged", function(previous, current)
    if not current or current:Type() ~= "Player" then return end
    print("target " .. tostring(previous) .. " -> " .. current:Name() .. " " .. current:Type() ..
          " hp " .. current:Health() .. "/" .. current:MaxHealth() .. " target " .. tostring(current:Target()))
    local hp, why = current:Target():Health()
    print("boar target " .. tostring(hp) .. " " .. tostring(why))
    local entry, why2 = current:Entry()
    print("entry " .. tostring(entry) .. " " .. tostring(why2))
    local objects = wxl.object.InView("Unit")
    print("units in view " .. #objects .. " local " .. wxl.object.LocalPlayer():Name() ..
          " guid " .. wxl.object.LocalPlayer():Guid() .. " same " .. tostring(wxl.object.FromGuid("0x0000000000000100") == current))
end)
Observe("ObjectScript", "OnObjectCreate", function(object, guid, kind)
    print("created " .. tostring(object) .. " guid " .. guid .. " type " .. kind)
    local hp, why = object:Health()
    print("chest health " .. tostring(hp) .. " " .. tostring(why))
end)
Observe("LuaScript", "OnScriptVmReady", function(context, native_state)
    print("vm " .. context .. " " .. type(native_state))
end)

-- handlers defined on the family tables are subscribed after this file has run
function AssetScript:OnTextureLoad(path) print("implicit texture " .. path) end
function WorldScript:OnSoundPlay(sound_id) return sound_id == 9 end
function WorldScript:OnWorldClick(point, object, button) print("implicit click " .. button .. " at " .. point.z) end

local id = Observe("RenderScript", "OnFrameBegin", function(index) print("frame " .. index) end)
Observe("RenderScript", "OnFrameEnd", function() print("unobserve " .. tostring(Unobserve(id)) .. " " .. tostring(Unobserve(id))) end)
)lua";

    constexpr const char* kProbeV2 = R"lua(
registerForEvent("onInit", function() print("probe init v2") end)
Observe("WorldScript", "OnUpdate", function() print("probe v2 update") end)
)lua";

    constexpr const char* kBroken = R"lua(
Observe("WorldScript", "OnUpdate", function() error("always") end)
registerForEvent("onUpdate", function() print("broken still updates") end)
)lua";

    constexpr const char* kWatcher = R"lua(
local part = require("lib.part")
local later = pcall(require, "lib.later")
local data = io.open("data.txt"):read("*a")
registerForEvent("onInit", function()
    local notes = io.open("notes.txt", "w")
    notes:write("written at init")
    notes:close()
    print("watcher init " .. part .. " later=" .. tostring(later) .. " data=" .. data .. " " .. wxl.mod.version)
end)
)lua";

    constexpr const char* kOrderFirst = R"lua(
local added = false
Observe("WorldScript", "OnUpdate", function()
    print("order a1")
    if not added then
        added = true
        Observe("WorldScript", "OnUpdate", function() print("order a2") end)
    end
end)
)lua";

    constexpr const char* kOrderSecond = R"lua(
Observe("WorldScript", "OnUpdate", function() print("order b") end)
)lua";

    constexpr const char* kOlder = R"lua(
local ok, err = pcall(Observe, "ChatScript", "OnChatMessage", function() end)
print("observe " .. tostring(ok) .. ": " .. err)
ok, err = pcall(Override, "ChatScript", "OnChatSend", function() end)
print("override " .. tostring(ok) .. ": " .. err)
ok = pcall(Observe, "CombatScript", "OnCombatMiss", function() end)
print("older event " .. tostring(ok))
)lua";

    constexpr const char* kCacheFirst = R"lua(
Observe("ObjectScript", "OnObjectCreate", function(object) object.tag = "first state" end)
Observe("ObjectScript", "OnTargetChanged", function(previous, current)
    if not current then return end
    print("order a target tag " .. tostring(current.tag))
    current.tag = "reloaded state"
end)
Observe("ObjectScript", "OnObjectDestroy", function(object)
    print("order a destroy tag " .. tostring(object.tag) .. " health " .. tostring(object:Health()))
end)
)lua";

    constexpr const char* kCacheSecond = R"lua(
local held
Observe("ObjectScript", "OnTargetChanged", function(previous, current) held = current end)
Observe("ObjectScript", "OnObjectDestroy", function(object)
    print("order b destroy same " .. tostring(rawequal(object, held)) .. " valid " .. tostring(object:Valid()))
end)
registerForEvent("onUpdate", function()
    if held and not held:Valid() then
        print("order b " .. tostring(select(2, held:Health())))
        held = nil
    end
end)
)lua";

    /// The "order " lines logged from @p from on, as one string: "a1 b a1 a2 b".
    std::string OrderSince(const std::vector<std::string>& lines, size_t from)
    {
        std::string seen;
        for (size_t i = from; i < lines.size(); ++i)
        {
            const size_t at = lines[i].find("] order ");
            if (at == std::string::npos) continue;
            if (!seen.empty()) seen += ' ';
            seen += lines[i].substr(at + 8);
        }
        return seen;
    }

    constexpr const char* kBench = R"lua(
local sum = 0
Observe("WorldScript", "OnUpdate", function(delta) sum = sum + delta end)
registerForEvent("onShutdown", function() print("bench sum " .. sum) end)
)lua";

    constexpr const char* kBenchWide = R"lua(
local sum = 0
Observe("CombatScript", "OnCombatDamage", function(source, source_guid, target, target_guid, spell_id, kind,
                                                    environment, amount)
    sum = sum + amount
end)
)lua";
    constexpr const char* kIdentity = R"lua(
local kept, keptGuid, reported, collected
local byObject = {}
local weak = setmetatable({}, { __mode = "k" })

Observe("ObjectScript", "OnObjectCreate", function(object, guid)
    if guid == "0x0000000000000600" and not kept then
        kept, keptGuid = object, guid
        object.tag = "first"
        byObject[object] = 1
        print("same at lookup " .. tostring(rawequal(wxl.object.FromGuid(guid), object)))
        local ok, err = pcall(function() object.Health = 1 end)
        print("method name refused " .. tostring(not ok and err:find("is a method of objects") ~= nil))
    elseif guid == "0x0000000000000610" then
        object.note = "kept while in view"
        weak[object] = true
    elseif guid == "0x0000000000000600" then
        print("revived new value " .. tostring(object ~= kept) .. " key " .. tostring(byObject[object]) ..
              " tag " .. tostring(object.tag) .. " same guid " .. tostring(object:Guid() == keptGuid) ..
              " lookup " .. tostring(rawequal(wxl.object.FromGuid(guid), object)) ..
              " old " .. tostring(kept:Valid()))
    end
end)

Observe("ObjectScript", "OnTargetChanged", function(previous, current)
    if not current or current:Guid() ~= "0x0000000000000600" then return end
    print("same at event " .. tostring(rawequal(current, kept)) .. " key " .. tostring(byObject[current]) ..
          " tag " .. tostring(current.tag) .. " unset " .. tostring(current.nothing))
end)

Observe("ObjectScript", "OnObjectDestroy", function(object, guid)
    if guid ~= "0x0000000000000600" or not rawequal(object, kept) then return end
    print("destroy sees tag " .. tostring(object.tag) .. " health " .. tostring(object:Health()))
end)

registerForEvent("onUpdate", function()
    if kept and not kept:Valid() and not reported then
        reported = true
        local hp, why = kept:Health()
        local ok, err = pcall(function() kept.tag = "again" end)
        print("after destroy " .. tostring(hp) .. " " .. why .. " tag " .. tostring(kept.tag) ..
              " valid " .. tostring(kept:Valid()) .. " type " .. tostring(select(2, kept:Type())) ..
              " store " .. tostring(not ok and err:find("gone") ~= nil) .. " " .. tostring(kept))
    end
    if not collected then
        collectgarbage("collect")
        if next(weak) == nil then
            collected = true
            local again = wxl.object.FromGuid("0x0000000000000610")
            print("collected, data kept " .. tostring(again and again.note))
        end
    end
end)

Observe("ObjectScript", "OnTargetChanged", function(previous, current)
    if not current or current:Guid() ~= "0x0000000000000100" then return end
    local chest = previous
    print("chest kind " .. chest:Type() .. " game object type " .. tostring(chest:GameObjectType()))
    local forms = {}
    for _, guid in ipairs({ "0x200", "0X0000000000000200", 512 }) do
        forms[#forms + 1] = tostring(wxl.object.FromGuid(guid) ~= nil)
    end
    for _, guid in ipairs({ "0512", "512", "0x", "0x00000000000000200", "0x20g", 1.5, -1 }) do
        forms[#forms + 1] = tostring(pcall(wxl.object.FromGuid, guid))
    end
    print("guid forms " .. table.concat(forms, " "))
end)
)lua";

    // --- the service modules -------------------------------------------------------------------

    constexpr const char* kServices = R"lua(
local s, d, h, sc, p = wxl.storage, wxl.data, wxl.hook, wxl.script, wxl.patch
local function show(...)
    local t = {}
    for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end
    return table.concat(t, " ")
end

print("storage " .. show(s.Exists("Interface\\hello.txt"), s.Exists("nope"), s.Size("Interface\\hello.txt"), s.Size("nope")))
print("read " .. show(s.Read("Interface\\hello.txt"), #s.Read("Interface\\big.bin"), s.Read("nope")))
local f = s.Open("Interface\\hello.txt")
print("file " .. show(f:ReadAt(1, 3), f:ReadAt(4, 10), f:ReadAt(9, 2) == "", tostring(f):match("^File") ~= nil))
f:Close()
f:Close()
print("closed " .. show(pcall(f.ReadAt, f, 0, 1)))
print("open missing " .. show(s.Open("nope")))
print("find " .. show(#s.Find("Interface\\*"), #s.Find("nothing*"), s.Find("Interface\\h*")[1]))
print("search " .. show(s.AddSearchPath(wxl.mod.directory .. "\\assets", 1), s.Read("Interface\\probe.txt"),
                        s.Read("Interface\\hello.txt"), s.AddSearchPath(wxl.mod.directory .. "\\none", 1)))
print("mount " .. show(s.MountArchive("Data\\x.MPQ", 1)))
print("no path " .. show(pcall(s.Exists)))
print("no priority " .. show(pcall(s.MountArchive, "Data\\x.MPQ")))
print("no folder priority " .. show(pcall(s.AddSearchPath, wxl.mod.directory .. "\\assets")))
print("bad priority " .. show(pcall(s.AddSearchPath, "x", 1.5)))
local g = s.Open("Interface\\hello.txt")
print("no offset " .. show(pcall(g.ReadAt, g)))
print("no count " .. show(pcall(g.ReadAt, g, 0)))
g:Close()
print("remove " .. show(s.RemoveSearchPath(wxl.mod.directory .. "\\assets"), s.Read("Interface\\probe.txt"),
                        s.RemoveSearchPath(wxl.mod.directory .. "\\assets")))
print("remove other " .. show(s.RemoveSearchPath(wxl.mod.directory .. "\\..\\lua-services\\assets")))
s.AddSearchPath(wxl.mod.directory .. "/assets/", 1)
print("serve " .. show(s.Serve("Interface\\Served.txt", "served\0bytes", 1), s.Read("interface/served.TXT") == "served\0bytes",
                       s.Unserve("Interface\\Served.txt"), s.Read("Interface\\Served.txt"), s.Unserve("Interface\\Served.txt")))
print("no data " .. show(pcall(s.Serve, "Interface\\x.txt")))
print("no serve priority " .. show(pcall(s.Serve, "Interface\\x.txt", "x")))
s.Serve("Interface\\Kept.txt", "kept by services", 2)

print("data " .. show(d.IsLoaded("Spell.dbc"), d.ServeTable("Item.dbc", "Interface\\hello.txt"), d.ServedFrom("Item.dbc"),
                      select(2, d.ServeTable("Item.dbc", "Interface\\hello.txt")), d.RestoreTable("Item.dbc"),
                      d.ServedFrom("Item.dbc") == ""))

local points = h.Points()
print("hook " .. show(#points, points[1].name, points[1].signature, h.HasPoint("Thing"), h.HasPoint("Other"), h.Attach))

print("script " .. show(sc.Context(), sc.RegisterFunction("ProbeAdd", function(a, b) return a + b, "sum" end),
                        sc.RegisterEvent("PROBE_EVENT"), sc.FireEvent("PROBE_EVENT", 1, "x", true), select(2, sc.FireEvent("NOBODY")),
                        sc.Run("print(1)", "probe"), sc.ArgCount))
sc.RegisterFunction("ProbeFail", function() error("probe broke") end)
sc.RegisterFunction("ProbeTable", function() return {} end)
sc.RegisterFunction("ProbeGlue", function() return "glue only" end, "glue")
print("taken " .. show(sc.RegisterFunction("Taken", function() end)))
print("bad context " .. show(pcall(sc.RegisterFunction, "X", function() end, "nowhere")))
print("bad value " .. show(pcall(sc.FireEvent, "PROBE_EVENT", {})))

local patch = p.Apply("probe nop", 0x1000, "\144\144", "\204\204")
print("patch " .. show(tostring(patch):match("^Patch") ~= nil, p.Matches(0x1000, "\204\204"), p.Find("\204"), p.Find("\204", 0x1000),
                       p.Apply("again", 0x1000, "\144\144", "\1\1")))
print("patch sizes " .. show(pcall(p.Apply, "x", 0x1000, "\204", "\1\1")))
print("no address " .. show(pcall(p.Matches, nil, "\204")))
p.Apply("second", 0x1004, "\144", "\195")
)lua";

    constexpr const char* kServicesUndeclared = R"lua(
local ok, why = pcall(function() return wxl.patch.Apply end)
print("undeclared " .. tostring(ok) .. " " .. tostring(why))
print("modules " .. type(wxl.storage) .. " " .. type(wxl.data) .. " " .. type(wxl.script) .. " " .. type(wxl.hook) ..
      " " .. tostring(wxl.hook_raw))
)lua";

    constexpr const char* kServicesAbsent = R"lua(
local a, b = wxl.storage.Exists("x")
local c, e = wxl.script.RegisterFunction("Nope", function() end)
print("absent " .. tostring(a) .. " " .. tostring(b) .. " " .. tostring(c) .. " " .. tostring(e))
)lua";

    /// A call the client's script makes, with its arguments.
    wxl_script_call ScriptCallOf(std::initializer_list<wxl_value> args)
    {
        wxl_script_call call;
        call.args.assign(args.begin(), args.end());
        return call;
    }

    wxl_value Number(double number)
    {
        wxl_value value{};
        value.type      = WXL_VALUE_NUMBER;
        value.as.number = number;
        return value;
    }

    void WXL_CALL FakeScriptTaken(wxl_script_call*, void*) {}

    constexpr const char* kNoService = R"lua(
Observe("ObjectScript", "OnTargetChanged", function(previous, current)
    local valid, why = current:Valid()
    local hp, why2 = current:Health()
    print("no service " .. tostring(valid) .. " " .. tostring(why) .. " " .. tostring(hp) .. " " .. tostring(why2))
end)
)lua";

    constexpr const char* kFields = R"lua(
registerForEvent("onInit", function()
    local me, boar = wxl.object.LocalPlayer(), wxl.object.FromGuid("0x0000000000000200")
    local chest = wxl.object.InView("GameObject")[1]
    print("by name " .. boar:Field("Health") .. " " .. boar:Field("UNIT_FIELD_HEALTH") .. " " ..
          boar:Field("Unit.Health") .. " at " .. wxl.field.Unit.Health)
    print("bytes " .. table.concat({me:Field("Bytes0")}, ","))
    print("float " .. boar:Field("BoundingRadius") .. " guid " .. me:Field("Target"))
    print("element " .. boar:Field("Resistances", 6) .. " of " .. #boar:Fields("Resistances"))
    print("count " .. me:FieldCount() .. " " .. boar:FieldCount() .. " " .. chest:FieldCount())
    print("chest " .. chest:Field("GameObject.Flags") .. " " .. chest:Field("OBJECT_FIELD_ENTRY"))
    local ok, err = pcall(me.Field, me, "Flags")
    print("ambiguous " .. tostring(ok) .. " " .. err)
    ok, err = pcall(boar.Field, boar, "Xp")
    print("unknown " .. err)
    ok, err = pcall(boar.Field, boar, "Resistances", 7)
    print("past " .. err)
    print("set " .. tostring(boar:SetField("Health", 77)) .. " " .. boar:Field("Health"))
    print("set bytes " .. tostring(me:SetField("Bytes0", {4, 5, 6, 7})) .. " " .. table.concat({me:Field("Bytes0")}, ","))
    ok, err = pcall(me.SetField, me, "Bytes0", {4, 5, 6, 300})
    print("set refused " .. err)
    local many = wxl.object.Fields({me, boar, chest}, "Health")
    print("many " .. tostring(many[1]) .. " " .. tostring(many[2]) .. " " .. tostring(many[3]))
    print("watch " .. tostring(wxl.object.Watch("Unit", "Health")) .. " " .. tostring(wxl.object.Watch(nil, "Entry")))
    print("watch refused " .. tostring(select(2, wxl.object.Watch("Unit", "UNIT_FIELD_CREATEDBY"))))
    wxl.object.Watch("Unit", "HoverHeight")
    wxl.object.Watch("Player", "Target")
    wxl.object.Watch("Player", "Bytes0")
    print("watch again " .. tostring(wxl.object.Watch("Unit", "Health")))
end)

Observe("ObjectScript", "OnObjectFieldChanged", function(object, kind, field, count, old, new)
    local shown = function(v) return type(v) == "table" and table.concat(v, ",") or tostring(v) end
    print("changed " .. object:Name() .. " " .. kind .. " " .. field .. " " .. count .. " " ..
          shown(old) .. " -> " .. shown(new))
    if field == wxl.field.Unit.Health then
        print("unwatch " .. tostring(wxl.object.Unwatch("Unit", "Health")) .. " " ..
              tostring(wxl.object.Unwatch("Unit", "Health")))
    end
end)
)lua";

    // A second mod, which watches what the first does not.
    constexpr const char* kOtherFields = R"lua(
registerForEvent("onInit", function()
    wxl.object.Watch("Unit", "MaxHealth")
end)

Observe("ObjectScript", "OnObjectFieldChanged", function(object, kind, field)
    print("other changed " .. field)
end)
)lua";
} // namespace

int main(int argc, char** argv)
{
    // Unbuffered, so the last line printed before a crash is never held back in a buffer.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc < 4)
    {
        std::printf("usage: wxl-lua-tests <wxl-lua.dll> <examples/lua-hello> <scratch>\n");
        return 2;
    }
    const fs::path binary  = argv[1];
    const fs::path example = argv[2];
    const fs::path scratch = argv[3];

    Section("loading");
    {
        const fs::path mods = MakeTree(scratch, "main", binary, example);
        WriteFile(mods / "probe" / "mod.json", LuaManifest("wxl.probe", "\"loadAfter\": [ \"wxl.lua-hello\" ]"));
        WriteFile(mods / "probe" / "init.lua", kProbeV1);
        WriteFile(mods / "probe" / "sub" / "data.txt", "data");
        WriteFile(mods / "probe" / "sub" / "lib.lua", "return 42\n");
        WriteFile(mods / "broken" / "mod.json", LuaManifest("wxl.broken"));
        WriteFile(mods / "broken" / "init.lua", kBroken);
        WriteFile(mods / "syntax" / "mod.json", LuaManifest("wxl.syntax"));
        WriteFile(mods / "syntax" / "init.lua", "this is not lua\n");
        WriteFile(mods / "needy" / "mod.json",
                  LuaManifest("wxl.needy", "\"requires\": [ { \"service\": \"wxl.nothing\", \"version\": 1 } ]"));
        WriteFile(mods / "needy" / "init.lua", "print('never')\n");
        std::string future = LuaManifest("wxl.future");
        const std::string futureAbi = std::to_string(WXL_ABI_VERSION_MAJOR) + "." +
                                      std::to_string(WXL_ABI_VERSION_MINOR) + "." +
                                      std::to_string(WXL_ABI_VERSION_FEATURE + 1);
        future.replace(future.find(kAbi), std::strlen(kAbi), futureAbi);
        WriteFile(mods / "future" / "mod.json", future);
        WriteFile(mods / "future" / "init.lua", "print('never')\n");
        WriteFile(mods / "abi" / "mod.json", LuaManifest("wxl.abi"));
        WriteFile(mods / "abi" / "init.lua",
                  "print('abi ' .. wxl.runtime.abi .. ' ' .. wxl.runtime.abi_major .. ' ' .. "
                  "wxl.runtime.abi_minor .. ' ' .. wxl.runtime.abi_feature)\n");
        WriteFile(mods / "badname" / "mod.json", LuaManifest("wxl.badname"));
        WriteFile(mods / "badname" / "init.lua", "function WorldScript:OnTick() end\n");
        WriteFile(mods / "Bare Folder_One" / "init.lua",
                  "registerForEvent('onInit', function() print('bare init ' .. wxl.mod.id .. ' ' .. wxl.mod.version) end)\n");
        WriteFile(mods / "!!" / "init.lua", "print('never')\n");

        CaptureSink sink;
        HostConfig  config = Config(mods, sink);
        config.services.push_back(
            PublishedService{WXL_STORAGE_SERVICE, WXL_STORAGE_SERVICE_VERSION, wxltest::FakeStorage::Table()});
        ModHost host(std::move(config));
        host.LoadAll();
        Check(wxltest::FakeStorage::Redirects()["interface\\icons\\inv_misc_questionmark.blp"] ==
                  "Interface\\Icons\\INV_Misc_Coin_01.blp",
              "the example redirects its icon through wxl.storage, which the runtime answers on any thread");

        Check(host.Loaded().size() == 1 && host.Loaded()[0].id == "wxl.lua", "wxl.lua loads natively");
        const RefusedMod* hello = FindRefused(host, "wxl.lua-hello");
        Check(hello && hello->verdict == Verdict::Skip && Contains(hello->reason, "wxl.lua serves it"),
              "a script mod is skipped by the host with a reason naming wxl.lua");
        Check(sink.Has("[INFO ] [host] skipped wxl.lua-hello: script mod (lua: init.lua)"),
              "and that is an INFO line");
        Check(sink.Has("[wxl.lua] loaded wxl.lua-hello 1.0.0 (Lua hello) from init.lua") &&
                  sink.Has("[wxl.lua] loaded wxl.probe"),
              "wxl.lua loads the script mods");
        Check(sink.Has("mod wxl.syntax failed to load init.lua") && sink.Has("[wxl.lua] 5 script mod(s) loaded, 2 not"),
              "a mod whose entry does not compile is reported and the others load");
        Check(sink.Has("[ERROR] [wxl.lua] mod wxl.badname failed to load init.lua: WorldScript.OnTick is not an event of WorldScript; its events are OnUpdate, "),
              "a family-table member that is no event is a load error naming the events");
        Check(sink.Has("[INFO ] [wxl.lua] found bare-folder-one in folder 'Bare Folder_One' (no manifest)") &&
                  sink.Has("[wxl.lua] loaded bare-folder-one 0.0.0 (Bare Folder_One) from init.lua"),
              "a folder with init.lua and no manifest is a mod named after the folder");
        Check(sink.Has("[WARN ] [wxl.lua] folder '!!' holds an init.lua but its name makes no mod id"),
              "a folder whose name makes no id is refused with a reason");
        Check(sink.Count("[host] refused") == 0, "the host reports no problem with a manifest-less folder");
        Check(sink.Has("[wxl.lua] refused wxl.needy: requires service wxl.nothing"),
              "a script mod requiring a missing service is refused");
        Check(sink.Has(("[wxl.lua] refused wxl.future: needs abi " + futureAbi + ", newer than this runtime's " +
                        kAbi + ": it uses what was appended after " + kAbi)
                           .c_str()),
              "a script mod needing a later abi feature is refused, naming both versions");
        Check(sink.Has(("[wxl.abi] abi " + std::string(kAbi) + " " + std::to_string(WXL_ABI_VERSION_MAJOR) + " " +
                        std::to_string(WXL_ABI_VERSION_MINOR) + " " + std::to_string(WXL_ABI_VERSION_FEATURE))
                           .c_str()),
              "wxl.runtime carries the abi, as text and in parts");
        Check(sink.Has("[wxl.probe] sandbox ok"), "the sandbox holds");
        Check(sink.Has("[wxl.probe] family check ok") && sink.Has("[wxl.probe] method check ok") &&
                  sink.Has("[wxl.probe] override check ok") && sink.Has("[wxl.probe] callback check ok"),
              "unknown names are errors naming the valid ones");
        Check(sink.Has("[wxl.probe] events table ok"), "wxl.events lists the contract");

        const wxl_lua_api* api = static_cast<const wxl_lua_api*>(host.Table()->get_service(WXL_LUA_SERVICE, WXL_LUA_SERVICE_VERSION));
        Check(api && api->struct_size == sizeof(wxl_lua_api) && api->count() == 7,
              "the wxl.lua service is published and counts the script mods");

        host.Stage(WXL_STAGE_EARLY);
        Check(!sink.Has("probe init v1"), "onInit waits for the ready stage");
        host.Stage(WXL_STAGE_READY);
        Check(sink.Has(("[wxl.lua-hello] hello from Lua hello on Test runtime, abi " + std::string(kAbi)).c_str()),
              "the example's onInit ran and read wxl.mod and wxl.runtime");
        Check(sink.Has("[wxl.probe] probe init v1"), "onInit runs at WXL_STAGE_READY");
        Check(sink.Has("[bare-folder-one] bare init bare-folder-one 0.0.0"), "the manifest-less mod runs too");

        // Update: the Observe handler and the onUpdate callback, in that order.
        const wxl_update_args tick{0.5f};
        host.Raise(WXL_EV_Update, &tick);
        Check(sink.Has("[wxl.probe] probe observed update 0.5") && sink.Has("[wxl.probe] probe update 0.5"),
              "Observe(WorldScript, OnUpdate) and onUpdate both see the delta");
        const wxl_frame_args frame{3, 0.016f};
        host.Raise(WXL_EV_UiRenderEnd, &frame);
        Check(sink.Has("[wxl.probe] probe draw"), "onDraw runs at UiRenderEnd");

        // A handler that errors: logged with the id and a traceback, the next handler still runs.
        const wxl_world_args enter{530, {1.0f, 2.0f, 3.0f}};
        host.Raise(WXL_EV_WorldEnter, &enter);
        Check(sink.Has("[ERROR] [wxl.lua] mod wxl.probe errored in its Observe handler for OnWorldEnter of WorldScript (1/25): ") &&
                  sink.Has("boom 530") && sink.Has("stack traceback:"),
              "an error names the mod, the event and carries a traceback");
        Check(sink.Has("[wxl.probe] after-error 1,2,3"), "the handler after it still runs, and reads a vec3");
        Check(sink.Has("[wxl.lua-hello] first world: map 530 at 1.0, 2.0, 3.0"),
              "the example's Observe of OnWorldEnter reads a number and a vec3");
        host.Raise(WXL_EV_WorldEnter, &enter);
        Check(sink.Count("[wxl.lua-hello] first world") == 1,
              "and Unobserve from inside that handler stops it after the first world");

        // Values outlive the call: they are copies.
        host.Raise(WXL_EV_WorldLeave, &enter);
        wxl_bool       suppressed = WXL_FALSE;
        wxl_sound_args sound{9, wxl_str{"Sound\\x.wav", 11}, &suppressed};
        host.Raise(WXL_EV_SoundPlay, &sound);
        Check(sink.Has("[wxl.probe] kept values stay"), "a value kept past the handler is still readable");
        Check(sink.Has("[wxl.probe] sound 9 Sound\\x.wav suppressed=false"), "a str and an answer arrive as parameters");
        Check(suppressed == WXL_TRUE, "a family-table handler on an answerable event answers with its return value");
        Check(sink.Has("[WARN ] [wxl.lua] mod wxl.probe: the Override of OnSoundPlay returned 2 values; it answers through 'suppressed', one value each") &&
                  sink.Has("[WARN ] [wxl.lua] mod wxl.probe: the Override of OnSoundPlay returned a string for 'suppressed'; it takes true to answer"),
              "a return value of the wrong kind, and one too many, are warned about");
        const char*           texture = "World\\x.blp";
        wxl_texture_load_args load{wxl_str{texture, static_cast<uint32_t>(std::strlen(texture))}};
        host.Raise(WXL_EV_TextureLoad, &load);
        Check(sink.Has("[wxl.probe] implicit texture World\\x.blp"), "a family-table handler is subscribed without a declaration");
        const wxl_world_click_args click{{1.0f, 2.0f, 3.5f}, WXL_HANDLE_NONE, 2};
        host.Raise(WXL_EV_WorldClick, &click);
        Check(sink.Has("[wxl.probe] implicit click middle at 3.5"), "and reads its args, an enumeration by name");

        // An answer returned, and seen by the handler after it.
        wxl_bool       handled = WXL_FALSE;
        wxl_input_args input{7, 1, -1, &handled};
        host.Raise(WXL_EV_Input, &input);
        Check(handled == WXL_TRUE, "returning true from an Override writes the flag");
        Check(sink.Has("[wxl.probe] input 7 1 -1 handled=true"), "an Observe handler after it sees the answer");
        handled = WXL_FALSE;
        input.message = 8;
        host.Raise(WXL_EV_Input, &input);
        Check(handled == WXL_FALSE && sink.Has("[wxl.probe] input 8 1 -1 handled=false"),
              "and returning false writes nothing");

        // The example's Override returning true.
        wxl_bool         kept = WXL_FALSE;
        const char       body[] = "abcd";
        wxl_message_args message{0x33D, body, 4, &kept};
        host.Raise(WXL_EV_MessageReceived, &message);
        Check(kept == WXL_TRUE && sink.Has("[wxl.lua-hello] message of the day hidden (4 bytes)"),
              "returning true from an Override writes WXL_TRUE; bytes arrive as a string");
        host.Raise(WXL_EV_MessageReceived, &message);
        Check(sink.Count("[wxl.lua-hello] message of the day hidden") == 1,
              "the answer parameter carries what was decided before the handler");
        kept = WXL_FALSE;
        message.opcode = 0x1;
        host.Raise(WXL_EV_MessageReceived, &message);
        Check(kept == WXL_FALSE, "returning nothing leaves the flag alone");

        // An Override returning a string.
        char               redirect[64] = {};
        const char*        asked        = "Interface\\Icons\\Probe.blp";
        wxl_file_open_args open{wxl_str{asked, static_cast<uint32_t>(std::strlen(asked))}, redirect, sizeof redirect};
        host.Raise(WXL_EV_FileOpen, &open);
        Check(std::strcmp(redirect, "Interface\\Icons\\Answer.blp") == 0,
              "returning a string from an Override fills the buffer");
        Check(sink.Has("[wxl.probe] file open Interface\\Icons\\Probe.blp redirect Interface\\Icons\\Answer.blp"),
              "an Observe of the same event sees the buffer an earlier Override filled");
        const char*        lower = "interface\\icons\\probe.blp";
        wxl_file_open_args openLower{wxl_str{lower, static_cast<uint32_t>(std::strlen(lower))}, redirect, sizeof redirect};
        redirect[0] = '\0';
        host.Raise(WXL_EV_FileOpen, &openLower);
        Check(std::strcmp(redirect, "Interface\\Icons\\Answer.blp") == 0,
              "the handler compares the path without regard to case");
        std::strcpy(redirect, "Interface\\Icons\\Other.blp");
        host.Raise(WXL_EV_FileOpen, &open);
        Check(std::strcmp(redirect, "Interface\\Icons\\Other.blp") == 0,
              "and leaves a path an earlier handler wrote");
        const char*        longPath = "long";
        wxl_file_open_args tooLong{wxl_str{longPath, 4}, redirect, sizeof redirect};
        redirect[0] = '\0';
        host.Raise(WXL_EV_FileOpen, &tooLong);
        Check(redirect[0] == '\0' &&
                  sink.Has("[WARN ] [wxl.lua] mod wxl.probe: the Override of OnFileOpen returned 100 bytes for 'redirect', which takes at most 63; it is not taken"),
              "a string longer than the buffer is not taken, and the warning names the limit");

        // Off the main thread: queued, delivered at the next Update, the answer not taken.
        redirect[0] = '\0';
        const size_t before = sink.Count("[wxl.probe] file open");
        std::thread([&host, &open] { host.Raise(WXL_EV_FileOpen, &open); }).join();
        Check(redirect[0] == '\0' && sink.Count("[wxl.probe] file open") == before,
              "an event raised off the main thread is not delivered there");
        host.Raise(WXL_EV_Update, &tick);
        Check(sink.Count("[wxl.probe] file open") == before + 1, "it is delivered at the next Update");
        Check(redirect[0] == '\0', "and the Override's string is not taken");
        Check(sink.Count("[WARN ] [wxl.lua] mod wxl.probe: OnFileOpen was raised off the main thread") == 1,
              "which is warned about");
        Check(sink.Count("OnFileOpen was raised off the main thread") == 1,
              "only to a mod with an Override: the example's Observe takes its copy without a word");
        std::thread([&host, &open] { host.Raise(WXL_EV_FileOpen, &open); }).join();
        host.Raise(WXL_EV_Update, &tick);
        Check(sink.Count("[WARN ] [wxl.lua] mod wxl.probe: OnFileOpen was raised off the main thread") == 1,
              "once per mod and event");

        wxl_bool         sentKept = WXL_FALSE;
        wxl_message_args sent{0x77, body, 4, &sentKept};
        std::thread([&host, &sent] { host.Raise(WXL_EV_MessageSent, &sent); }).join();
        host.Raise(WXL_EV_Update, &tick);
        Check(sink.Has("[wxl.probe] message sent 119 4") && sentKept == WXL_FALSE,
              "a copied message carries its bytes; the Override's true goes nowhere");

        // Objects through handles.
        const wxl_target_args target{WXL_HANDLE_NONE, wxltest::FakeObjects::kPlayer};
        host.Raise(WXL_EV_TargetChanged, &target);
        Check(sink.Has("[wxl.probe] target nil -> Thorgrim Player hp 0/5000 target Unit(0x000000000000002B)"),
              "handle members are objects with the generated getters and the hand-written ones");
        Check(sink.Has("[wxl.probe] boar target 90 nil"), "a read through Target() reads the boar");
        Check(sink.Has("[wxl.lua-hello] target Thorgrim: 0/5000 health"), "the example reads its target's health");
        const wxl_target_args toChest{wxltest::FakeObjects::kPlayer, wxltest::FakeObjects::kChest};
        host.Raise(WXL_EV_TargetChanged, &toChest);
        Check(sink.Has("[wxl.lua-hello] target GameObject(0x000000000000002C) has no health: a GameObject has no Health"),
              "and says why a read gave nothing");
        const wxl_target_args cleared{wxltest::FakeObjects::kChest, WXL_HANDLE_NONE};
        host.Raise(WXL_EV_TargetChanged, &cleared);
        Check(sink.Has("[wxl.lua-hello] target cleared"), "a handle member with no object is nil");
        Check(sink.Has("[wxl.probe] entry nil unsupported"), "a getter the runtime's table does not carry says so");
        Check(sink.Has("[wxl.probe] units in view 1 local Thorgrim guid 0x0000000000000100 same true"),
              "wxl.object lists, finds the local player, resolves a guid, and compares by handle");
        const wxl_object_args created{wxltest::FakeObjects::kChest, 0x300, WXL_OBJECT_GAMEOBJECT};
        host.Raise(WXL_EV_ObjectCreate, &created);
        Check(sink.Has("[wxl.probe] created GameObject(0x000000000000002C) guid 0x0000000000000300 type GameObject"),
              "a handle, a guid and a number arrive as parameters");
        Check(sink.Has("[wxl.probe] chest health nil a GameObject has no Health"),
              "a unit getter on a game object answers nil and why");
        const wxl_unit_args death{wxltest::FakeObjects::kPlayer};
        host.Raise(WXL_EV_UnitDeath, &death);
        Check(sink.Has("[wxl.lua-hello] Thorgrim died at level 80 (1 so far)"), "the example reads a unit from a handle");
        const wxl_unit_args boarDeath{wxltest::FakeObjects::kBoar};
        host.Raise(WXL_EV_UnitDeath, &boarDeath);
        host.Raise(WXL_EV_UnitDeath, &death);
        Check(sink.Has("[wxl.lua-hello] Boar died at level 3 (1 so far)") &&
                  sink.Has("[wxl.lua-hello] Thorgrim died at level 80 (2 so far)"),
              "and keeps its tally, from a module, across events");
        int                vm = 0;
        wxl_script_vm_args ready{WXL_SCRIPT_CONTEXT_GAME, &vm};
        host.Raise(WXL_EV_ScriptVmReady, &ready);
        Check(sink.Has("[wxl.probe] vm game userdata"), "a ptr member is a lightuserdata, a context its name");

        const uint8_t             hits[16]   = {};
        const uint8_t             misses[8]  = {};
        const uint8_t             reasons[1] = {2};
        const wxl_spell_cast_args go{WXL_HANDLE_NONE, 0x10, WXL_HANDLE_NONE, 0, 0, 133, 1, 0, 0, 0,
                                     2, 1, hits, sizeof hits, misses, sizeof misses, reasons};
        host.Raise(WXL_EV_SpellCastGo, &go);
        Check(sink.Has("[wxl.probe] cast go 14 parameters, spell 133, 2 hit in 16 bytes, 1 miss, caster 0x0000000000000010"),
              "a member that only measures another is not a parameter: its bytes carry it");

        // Unobserve.
        host.Raise(WXL_EV_FrameBegin, &frame);
        host.Raise(WXL_EV_FrameEnd, &frame);
        host.Raise(WXL_EV_FrameBegin, &frame);
        Check(sink.Count("[wxl.probe] frame 3") == 1 && sink.Has("[wxl.probe] unobserve true false"),
              "Unobserve drops a handler by id, once");

        // A mod that keeps failing is disabled; the others go on.
        for (int i = 0; i < 30; ++i) host.Raise(WXL_EV_Update, &tick);
        Check(sink.Has("[ERROR] [wxl.lua] mod wxl.broken is disabled: it errored 25 times"),
              "a mod reaching the error limit is disabled");
        Check(sink.Count("mod wxl.broken errored") == 25, "and errors no more");
        Check(sink.Count("[wxl.broken] broken still updates") == 24, "its callbacks stop with it");
        Check(sink.Count("[wxl.probe] probe observed update") >= 30, "the other mods keep running");

        // Reload: the file changes, the mod's state is replaced, onInit runs again.
        WriteFile(mods / "probe" / "init.lua", kProbeV2);
        Check(api && api->poll() == 1, "a poll sees the changed folder and reloads one mod");
        Check(sink.Has("[wxl.probe] probe shutdown") && sink.Has("[wxl.lua] reloading wxl.probe") &&
                  sink.Has("[wxl.probe] probe init v2"),
              "the old state's onShutdown, then the new state's onInit");
        const size_t v1 = sink.Count("[wxl.probe] probe observed update");
        host.Raise(WXL_EV_Update, &tick);
        Check(sink.Count("[wxl.probe] probe observed update") == v1 && sink.Has("[wxl.probe] probe v2 update"),
              "the old handlers are gone and the new ones run");
        Check(api && api->poll() == 0, "nothing changed: nothing reloads");
        Check(api && api->reload("wxl.nothing") == WXL_E_NOT_FOUND && api->reload("wxl.broken") == WXL_OK,
              "reload by id; an unknown id is not found");
        host.Raise(WXL_EV_Update, &tick);
        Check(sink.Count("[wxl.broken] broken still updates") == 25, "a disabled mod reloaded runs again");

        host.Shutdown();
        Check(sink.Has("[wxl.lua-hello] deaths 3 seen: Thorgrim x2, Boar x1, 5 icon(s) opened"),
              "onShutdown runs at WXL_STAGE_SHUTDOWN, with the state the handlers built");
        Check(sink.Has("[host] unloaded wxl.lua") && host.Loaded().empty(), "and wxl.lua unloads");

        if (g_failures) sink.Dump();
    }

    Section("object method names");
    {
        namespace names = wxl::lua::names;
        std::vector<names::Member> rows = {
#define WXL_OBJECT_READ(kinds, member, Method, type, source, Cxx, cxx, doc) names::Member{#member, names::k##kinds},
#define WXL_OBJECT_READ_AT(kinds, member, Method, type, source, Cxx, cxx, Arg, arg, doc) \
    names::Member{#member, names::k##kinds},
#include "wxl/runtime/object_reads.def"
#undef WXL_OBJECT_READ_AT
#undef WXL_OBJECT_READ
        };
        const std::vector<std::string> real = names::LuaNames(rows);
        Check(names::Clash(rows, real).empty(), "the real rows leave no name ambiguous");

        // A method and a field of one name on one object read the same value: boar:MaxHealth()
        // and boar:Field("MaxHealth"). getter_fields.def says which field each one reads.
        struct Pair
        {
            const char* method; ///< a row's member, or a hand-written method's name
            const char* field;  ///< the client's name
        };
        const Pair pairs[] = {
#define WXL_GETTER_FIELD(member, Kind, Name) {#member, wxl::field::Kind::Name.name},
#define WXL_METHOD_FIELD(Method, Kind, Name) {#Method, wxl::field::Kind::Name.name},
#include "getter_fields.def"
#undef WXL_METHOD_FIELD
#undef WXL_GETTER_FIELD
        };
        const auto paired = [&](const char* method, const wxl::field::Row& field) {
            for (const Pair& pair : pairs)
                if (std::strcmp(pair.method, method) == 0 && std::strcmp(pair.field, field.name) == 0)
                    return true;
            return false;
        };
        std::string mismatched;
        for (uint32_t type = WXL_OBJECT_ITEM; type <= WXL_OBJECT_CORPSE; ++type)
        {
            const std::string kind = names::TypeName(type);
            for (size_t i = 0; i < rows.size(); ++i)
            {
                if (!(rows[i].kinds & names::KindBit(static_cast<wxl_object_type>(type)))) continue;
                const wxl::field::Row* field = wxl::field::Find(type, real[i]);
                if (field && !paired(rows[i].member, *field))
                    mismatched += kind + ":" + real[i] + "() reads " + rows[i].member + ", Field(\"" +
                                  real[i] + "\") " + field->name + "; ";
            }
            for (const char* own : names::kHandWritten)
            {
                const wxl::field::Row* field = wxl::field::Find(type, own);
                if (field && !paired(own, *field))
                    mismatched += kind + ":" + own + "() is hand-written, Field(\"" + own + "\") " +
                                  field->name + "; ";
            }
        }
        Check(mismatched.empty(), "a method and a field of one name read the same value");
        if (!mismatched.empty()) std::printf("        %s\n", mismatched.c_str());

        const std::vector<names::Member> made = {
            {"gameobject_type", names::kGameObjects}, {"unit_flags", names::kUnits},
            {"player_flags", names::kPlayers},        {"gameobject_flags", names::kGameObjects},
            {"unit_name", names::kUnits},             {"item_owner", names::kItems},
            {"corpse_owner", names::kCorpses}};
        const std::vector<std::string> named = names::LuaNames(made);
        Check(named[0] == "GameObjectType" && named[4] == "UnitName",
              "a row named like a hand-written method says its kind instead");
        Check(named[1] == "Flags" && named[2] == "PlayerFlags" && named[3] == "Flags",
              "of two rows sharing a name on one object the narrower says its kind; disjoint kinds share it");
        Check(named[5] == "Owner" && named[6] == "Owner", "kinds no object has both of share a name");

        const std::vector<names::Member> tied = {{"unit_level", names::kUnits}, {"unit_level", names::kUnits}};
        const std::vector<std::string>   both = names::LuaNames(tied);
        Check(both[0] == "UnitLevel" && both[1] == "UnitLevel" && !names::Clash(tied, both).empty(),
              "a clash no qualifier resolves is reported rather than shadowed");
    }

    Section("every object member in Lua");
    {
        using wxltest::FakeObjects;
        namespace names = wxl::lua::names;
        const fs::path mods = MakeTree(scratch, "rows", binary, fs::path());
        WriteFile(mods / "rows" / "mod.json", LuaManifest("wxl.rows"));
        WriteFile(mods / "rows" / "init.lua", EveryRowMod());
        WriteFile(mods / "older" / "mod.json",
                  "{\n  \"id\": \"wxl.older\",\n  \"name\": \"older\",\n  \"version\": \"1.0.0\",\n"
                  "  \"abi\": \"0.1.16\",\n  \"lua\": \"init.lua\"\n}\n");
        WriteFile(mods / "older" / "init.lua",
                  "Observe('ObjectScript', 'OnObjectCreate', function(object, guid, type)\n"
                  "    if type ~= 'Unit' then return end\n"
                  "    local ok, err = pcall(function() return object:IsDead() end)\n"
                  "    print('older ' .. tostring(ok) .. ' ' .. tostring(err))\n"
                  "    print('older health ' .. tostring(object:Health()))\n"
                  "end)\n");

        CaptureSink sink;
        ModHost     host(Config(mods, sink));
        host.LoadAll();
        host.Stage(WXL_STAGE_READY);
        wxltest::FakeActions::Calls().clear();

        const wxl_object_args created[] = {{FakeObjects::kPlayer, 0x100, WXL_OBJECT_PLAYER},
                                           {FakeObjects::kBoar, 0x200, WXL_OBJECT_UNIT},
                                           {FakeObjects::kChest, 0, WXL_OBJECT_GAMEOBJECT},
                                           {FakeObjects::kItem, 0, WXL_OBJECT_ITEM}};
        for (const wxl_object_args& args : created) host.Raise(WXL_EV_ObjectCreate, &args);
        const wxl_update_args tick{0.1f};
        host.Raise(WXL_EV_Update, &tick);

        const std::vector<names::RowInfo>& rows = names::Rows();
        std::string                        wrong;
        int                                checked = 0;
        for (const names::RowInfo& row : rows)
        {
            if (row.since[2] < 18) continue;
            ++checked;
            const bool held = std::strcmp(row.kinds_name, "Containers") != 0;
            const std::string line = std::string("[wxl.rows] row ") + row.member + " " +
                                     (held ? LuaTypeOf(row) : std::string("nil a Item has no"));
            if (!sink.Has(line.c_str())) wrong += line + "; ";
        }
        Check(checked > 60 && wrong.empty(), "every 0.1.18 row answers in Lua as the type its row says");
        if (!wrong.empty()) std::printf("        %s\n", wrong.c_str());
        Check(sink.Has("[wxl.rows] row unit_reaction string hostile") &&
                  sink.Has("[wxl.rows] row unit_stand_state string sitting") &&
                  sink.Has("[wxl.rows] row item_quality string common"),
              "an enumeration arrives as its name");
        Check(sink.Has("[wxl.rows] row Auras table") && sink.Has("[wxl.rows] row Skills table") &&
                  sink.Has("[wxl.rows] row PartyMembers table"),
              "the lists are tables");
        Check(sink.Has("[wxl.rows] row Select boolean") && sink.Has("[wxl.rows] row CastAt boolean") &&
                  sink.Has("[wxl.rows] row Use boolean") && sink.Has("[wxl.rows] row CastSpell boolean") &&
                  sink.Has("[wxl.rows] row CastSpellBare boolean") && sink.Has("[wxl.rows] row MoveTo boolean"),
              "the actions answer true when the client takes them");
        Check(sink.Has("[wxl.rows] row UseOnUnit nil a Unit has no Use"), "an action of another kind says why");
        Check(sink.Has("[wxl.rows] row BadName error") && sink.Has("'sprint' is not one of walk, run"),
              "an enumeration argument takes a name, and an unknown one is an error naming them");
        const auto& calls = wxltest::FakeActions::Calls();
        bool castAt = false, bare = false;
        for (const auto& call : calls)
        {
            if (call.name == "cast_spell" && call.spell == 116 && call.target == FakeObjects::kBoar) castAt = true;
            if (call.name == "cast_spell" && call.spell == 133 && call.target == WXL_HANDLE_NONE) bare = true;
        }
        Check(castAt && bare, "the calls reach wxl.action with their objects, none when left out");
        Check(sink.Has("[wxl.older] older false") && sink.Has("came with abi 0.1.18") &&
                  sink.Has("[wxl.older] older health 90"),
              "a mod asking for an older abi cannot use a newer method, and keeps the older ones");
        if (g_failures) sink.Dump();
    }

    Section("object identity");
    {
        using wxltest::FakeObjects;
        const fs::path mods = MakeTree(scratch, "identity", binary, fs::path());
        WriteFile(mods / "identity" / "mod.json", LuaManifest("wxl.identity"));
        WriteFile(mods / "identity" / "init.lua", kIdentity);

        CaptureSink sink;
        ModHost     host(Config(mods, sink));
        host.LoadAll();
        host.Stage(WXL_STAGE_READY);

        FakeObjects::Spawn(60, 0x600);
        FakeObjects::Spawn(61, 0x610);
        const wxl_object_args wolf{60, 0x600, WXL_OBJECT_UNIT};
        const wxl_object_args cub{61, 0x610, WXL_OBJECT_UNIT};
        host.Raise(WXL_EV_ObjectCreate, &wolf);
        host.Raise(WXL_EV_ObjectCreate, &cub);
        Check(sink.Has("[wxl.identity] same at lookup true"),
              "an object is the same Lua value at its event and at a lookup by guid");
        Check(sink.Has("[wxl.identity] method name refused true"),
              "a field named like a method is refused");

        const wxl_target_args toWolf{WXL_HANDLE_NONE, 60};
        host.Raise(WXL_EV_TargetChanged, &toWolf);
        Check(sink.Has("[wxl.identity] same at event true key 1 tag first unset nil"),
              "the same value at a later event: a table key, with the fields the mod stored");

        const wxl_update_args tick{0.1f};
        host.Raise(WXL_EV_Update, &tick);
        Check(sink.Has("[wxl.identity] collected, data kept kept while in view"),
              "an object nothing holds is collected, and its fields wait for the next push");

        host.Raise(WXL_EV_ObjectDestroy, &wolf);
        FakeObjects::Despawn(60);
        Check(sink.Has("[wxl.identity] destroy sees tag first health 50"),
              "a mod's ObjectDestroy handler still reads the object and its fields");
        host.Raise(WXL_EV_Update, &tick);
        Check(sink.Has("[wxl.identity] after destroy nil gone tag nil valid false type gone store true "
                       "gone Unit(0x000000000000003C)"),
              "after ObjectDestroy the value is dead: reads answer gone, its fields are dropped, none "
              "can be stored");

        // The client brings the same object back: another visibility episode, another handle.
        FakeObjects::Spawn(70, 0x600);
        const wxl_object_args revived{70, 0x600, WXL_OBJECT_UNIT};
        host.Raise(WXL_EV_ObjectCreate, &revived);
        Check(sink.Has("[wxl.identity] revived new value true key nil tag nil same guid true lookup true old false"),
              "a revived object is a new value with no fields; the guid is what it shares with the old one");

        const wxl_target_args chest{FakeObjects::kChest, FakeObjects::kPlayer};
        host.Raise(WXL_EV_TargetChanged, &chest);
        Check(sink.Has("[wxl.identity] chest kind GameObject game object type 3"),
              "Type() is the kind's name and a game object's own type is GameObjectType()");
        Check(sink.Has("[wxl.identity] guid forms true true true false false false false false false false"),
              "FromGuid takes \"0x\" and up to sixteen hex digits or a whole number, nothing else");

        FakeObjects::Despawn(61);
        FakeObjects::Despawn(70);
        host.Stage(WXL_STAGE_SHUTDOWN);
        if (g_failures) sink.Dump();
    }

    Section("objects without the service");
    {
        const fs::path mods = MakeTree(scratch, "noservice", binary, fs::path());
        WriteFile(mods / "noservice" / "mod.json", LuaManifest("wxl.noservice"));
        WriteFile(mods / "noservice" / "init.lua", kNoService);

        CaptureSink sink;
        HostConfig  config = Config(mods, sink);
        config.services.clear();
        ModHost host(std::move(config));
        host.LoadAll();
        const wxl_target_args target{WXL_HANDLE_NONE, wxltest::FakeObjects::kBoar};
        host.Raise(WXL_EV_TargetChanged, &target);
        Check(sink.Has("[wxl.noservice] no service nil unsupported nil unsupported"),
              "without the object service Valid() answers like every read: nil, \"unsupported\"");
    }

    Section("the service modules");
    {
        using wxltest::FakePatch;
        using wxltest::FakeScript;
        using wxltest::FakeStorage;

        const fs::path mods = MakeTree(scratch, "services", binary, fs::path());
        fs::copy(fs::path(example).parent_path() / "lua-services", mods / "lua-services", fs::copy_options::recursive);
        fs::copy(fs::path(example).parent_path() / "lua-assets", mods / "lua-assets", fs::copy_options::recursive);
        WriteFile(mods / "services" / "mod.json",
                  LuaManifest("wxl.services", "\"requires\": [ { \"service\": \"wxl.patch\", \"version\": 1 } ]"));
        WriteFile(mods / "services" / "init.lua", kServices);
        WriteFile(mods / "services" / "assets" / "Interface" / "probe.txt", "from disk");
        WriteFile(mods / "services" / "assets" / "Interface" / "hello.txt", "hello, overridden");
        WriteFile(mods / "undeclared" / "mod.json", LuaManifest("wxl.undeclared"));
        WriteFile(mods / "served" / "mod.json", LuaManifest("wxl.served"));
        WriteFile(mods / "served" / "init.lua", "wxl.storage.Serve(\"interface/KEPT.txt\", \"lower\", 1)\n");
        FakeStorage::Served().clear();
        WriteFile(mods / "undeclared" / "init.lua", kServicesUndeclared);

        const std::string toc = "## Interface: 30300\nGlobalStrings.lua\nFrameXML.xml\n";
        FakeStorage::Files()["Interface\\FrameXML\\FrameXML.toc"] = toc;
        FakeScript::Live() = WXL_SCRIPT_CONTEXT_GAME;
        FakeScript::Functions().clear();
        FakeScript::Functions()["Taken"] = FakeScript::Registered{&FakeScriptTaken, nullptr};

        CaptureSink sink;
        HostConfig  config = Config(mods, sink);
        config.services.push_back(PublishedService{WXL_SCRIPT_SERVICE, WXL_SCRIPT_SERVICE_VERSION, FakeScript::Table()});
        config.services.push_back(PublishedService{WXL_STORAGE_SERVICE, WXL_STORAGE_SERVICE_VERSION, FakeStorage::Table()});
        config.services.push_back(PublishedService{WXL_DATA_SERVICE, WXL_DATA_SERVICE_VERSION, wxltest::FakeData::Table()});
        config.services.push_back(PublishedService{WXL_HOOK_SERVICE, WXL_HOOK_SERVICE_VERSION, wxltest::FakeHooks::Table()});
        config.services.push_back(PublishedService{WXL_PATCH_SERVICE, WXL_PATCH_SERVICE_VERSION, FakePatch::Table()});
        ModHost host(std::move(config));
        host.LoadAll();
        host.Stage(WXL_STAGE_READY);
        const auto raise = [&](wxl_event event, const void* args) { host.Raise(event, args); };
        const wxl_lua_api* api =
            static_cast<const wxl_lua_api*>(host.Table()->get_service(WXL_LUA_SERVICE, WXL_LUA_SERVICE_VERSION));

        Check(sink.Has("[wxl.services] storage true false 5 nil not_found"),
              "storage: a bool, an out-parameter as its value, a failure as nil and the result's name");
        Check(sink.Has("[wxl.services] read hello 1000 nil not_found"), "a whole file reads into a string");
        Check(sink.Has("[wxl.services] file ell o true true"),
              "a File reads at an offset, as many bytes as asked or what is left, and says what it is");
        Check(sink.Has("[wxl.services] closed false ") && sink.Has("was given back already") &&
                  FakeStorage::Closes() == FakeStorage::Opens(),
              "a File given back is closed once, and a read through it afterwards is an error");
        Check(sink.Has("[wxl.services] open missing nil not_found"), "a file that is not there opens as nil, not_found");
        Check(sink.Has("[wxl.services] find 3 0 Interface\\hello.txt"), "find answers an array of names");
        Check(sink.Has("[wxl.services] search true from disk hello, overridden nil not_found"),
              "a folder added to the search path serves its files, ahead of the client's at a priority above 0");
        Check(sink.Has("[wxl.services] mount true"), "an archive mounts");
        Check(sink.Has("[wxl.services] no path false ") &&
                  sink.Has("wxl.storage.Exists: 'path' (argument 1) is missing; it takes a string"),
              "a missing argument is an error naming the function and the parameter");
        Check(sink.Has("[wxl.services] no priority false ") &&
                  sink.Has("wxl.storage.MountArchive: 'priority' (argument 2) is missing; it takes a whole number"),
              "a whole number left out is an error, not 0");
        Check(sink.Has("[wxl.services] no folder priority false ") &&
                  sink.Has("wxl.storage.AddSearchPath: 'priority' (argument 2) is missing; it takes a whole number"),
              "AddSearchPath's priority too");
        Check(sink.Has("[wxl.services] no offset false ") &&
                  sink.Has("File:ReadAt: 'offset' (argument 2) is missing; it takes a whole number") &&
                  sink.Has("[wxl.services] no count false ") &&
                  sink.Has("File:ReadAt: 'count' (argument 3) is missing; it takes how many bytes to read, a whole number"),
              "a method names itself on its resource");
        Check(sink.Has("[wxl.services] no address false ") &&
                  sink.Has("wxl.patch.Matches: 'address' (argument 1) is missing; it takes a whole number"),
              "nil is a missing argument too");
        Check(sink.Has("[wxl.services] remove true nil nil not_found"),
              "a folder removed serves nothing, and removing it again finds nothing");
        Check(sink.Has("[wxl.services] serve true true true nil nil not_found"),
              "bytes a Lua mod serves read back as a file, in any case and with either slash, until it takes them back");
        Check(sink.Has("[wxl.services] no data false ") &&
                  sink.Has("wxl.storage.Serve: 'data' (argument 2) is missing; it takes a string") &&
                  sink.Has("[wxl.services] no serve priority false ") &&
                  sink.Has("wxl.storage.Serve: 'priority' (argument 3) is missing; it takes a whole number"),
              "Serve names what is missing");
        Check(FakeStorage::Served()["interface\\kept.txt"] == "kept by services",
              "when two Lua mods serve one name, the runtime is handed the higher priority's bytes");
        Check(sink.Has("[wxl.services] remove other nil not_found"),
              "a mod cannot remove a folder another mod added");
        Check(sink.Has("[wxl.services] bad priority false ") && sink.Has("a whole number from -2147483648 to 2147483647"),
              "a number that is not whole, or out of range, is an error that says what is wanted");
        Check(sink.Has("[wxl.services] data true true Interface\\hello.txt failed true true"),
              "data: every row of the table, its answers as wxl.data gives them");
        Check(sink.Has("[wxl.services] hook 1 Thing int(int) true false nil"),
              "hook: the points as tables of their fields; attaching stays native");
        Check(sink.Has("[wxl.services] script game true true true not_found true nil"),
              "script: the context by name, a function and an event registered, an event fired, a chunk run, "
              "and no accessor of a call");
        Check(FakeScript::Events().size() >= 2 && sink.Has("[wxl.services] taken nil invalid") &&
                  sink.Has("RegisterFunction 'Taken': the name is taken in the client's script"),
              "a name the client already holds is refused and logged");
        Check(sink.Has("[wxl.services] bad context false ") && sink.Has("\"glue\" or \"game\""),
              "a context is named, or left out for both");
        Check(sink.Has("[wxl.services] bad value false ") && sink.Has("nil, a number, a string or a boolean"),
              "an event carries what the client's script can take");
        Check(sink.Has("[wxl.services] patch true true 4096 4097 nil not_found"),
              "patch: applied, matched, found, refused over bytes that are not the expected ones");
        Check(sink.Has("[wxl.services] patch sizes false ") && sink.Has("as many bytes as the string before it"),
              "the expected and the replacement bytes are one length, which the call carries");
        Check(sink.Has("[wxl.undeclared] undeclared false ") &&
                  sink.Has("wxl.patch reaches the client's memory, so a mod uses it only when its mod.json declares it: "
                           "add { \"service\": \"wxl.patch\", \"version\": 1 } to \"requires\""),
              "a raw module the manifest does not declare refuses every use, saying how to declare it");
        Check(sink.Has("[wxl.undeclared] modules table table table table nil"),
              "the open modules are there for every mod; a native-only table has no module");

        // The example: what the guide quotes.
        Check(sink.Has(("[wxl.lua-services] Interface\\FrameXML\\FrameXML.toc: " + std::to_string(toc.size()) +
                        " bytes, 3 lines").c_str()),
              "the example reads a whole file");
        Check(sink.Has("[wxl.lua-services] from the assets folder: hello from the assets folder"),
              "and one of its own, through the search path");
        Check(sink.Has("[wxl.lua-services] it starts with \"## Interface\""), "and the first bytes of a file it opens");
        Check(sink.Has("[wxl.lua-assets] served Interface\\lua-assets\\note.txt: true") &&
                  sink.Has("[wxl.lua-assets] read back: This file lives in a Lua mod's memory."),
              "lua-assets serves a file and reads it back through the storage");
        {
            const char*           tile = "World\\Maps\\Azeroth\\Azeroth_32_48.adt";
            wxl_terrain_tile_args args{};
            args.path   = wxl_str{tile, static_cast<uint32_t>(std::strlen(tile))};
            args.tile_x = 32;
            args.tile_y = 48;
            args.data   = "MVER";
            args.size   = 4;
            raise(WXL_EV_TerrainTileLoad, &args);
            Check(sink.Has("[wxl.lua-assets] first terrain tile: World\\Maps\\Azeroth\\Azeroth_32_48.adt (32, 48), 4 bytes"),
                  "and its AssetScript handler hears a terrain tile load, with its place and size");
        }
        Check(sink.Has("[wxl.lua-services] 1 table(s) of contents in Interface\\FrameXML") &&
                  sink.Has("[wxl.lua-services] Spell.dbc read already: true") &&
                  sink.Has("[wxl.lua-services] 1 hook point(s) named"),
              "and finds names, asks about a table, counts the hook points");

        // The client's script calls what the mods registered.
        wxl_script_call add = ScriptCallOf({Number(2), Number(3)});
        Check(FakeScript::Invoke("ProbeAdd", add) && add.results.size() == 2 &&
                  add.results[0].type == WXL_VALUE_NUMBER && add.results[0].as.number == 5 &&
                  add.results[1].type == WXL_VALUE_STRING &&
                  std::string(add.results[1].as.string.data, add.results[1].as.string.size) == "sum",
              "a registered Lua function gets the call's arguments and answers with its returns");
        wxl_script_call broken = ScriptCallOf({});
        Check(FakeScript::Invoke("ProbeFail", broken) && Contains(broken.error, "ProbeFail: ") &&
                  Contains(broken.error, "probe broke") && !Contains(broken.error, "stack traceback") &&
                  sink.Has("mod wxl.services errored in its script function ProbeFail (1/25)"),
              "a Lua error fails the call with its message, and counts against the mod with its traceback");
        wxl_script_call table = ScriptCallOf({});
        Check(FakeScript::Invoke("ProbeTable", table) && table.results.size() == 1 &&
                  table.results[0].type == WXL_VALUE_NIL &&
                  sink.Has("script function ProbeTable returned a table, which the client's script cannot take"),
              "a value the client's script cannot take crosses as nil, and is logged once");
        Check(!FakeScript::Functions().count("ProbeGlue"), "a function for another context waits for it");
        wxl_script_call greet = ScriptCallOf({});
        Check(FakeScript::Invoke("LuaServicesGreet", greet) && greet.results.size() == 1 &&
                  std::string(greet.results[0].as.string.data, greet.results[0].as.string.size) ==
                      "hello nil, from Lua services",
              "the example's function answers the client's script");
        Check(std::find(FakeScript::Events().begin(), FakeScript::Events().end(), "PROBE_EVENT n s b") !=
                  FakeScript::Events().end() &&
                  std::find(FakeScript::Chunks().begin(), FakeScript::Chunks().end(), "probe: print(1)") !=
                      FakeScript::Chunks().end(),
              "an event fired carries its values, a chunk runs under its name");

        const wxl_world_args world{530, {1.0f, 2.0f, 3.0f}};
        host.Raise(WXL_EV_WorldEnter, &world);
        Check(std::find(FakeScript::Events().begin(), FakeScript::Events().end(), "LUA_SERVICES_WORLD n") !=
                      FakeScript::Events().end() &&
                  std::find(FakeScript::Chunks().begin(), FakeScript::Chunks().end(),
                            "lua-services: DEFAULT_CHAT_FRAME:AddMessage('lua-services: entered map 530')") !=
                      FakeScript::Chunks().end(),
              "the example fires its event and runs its line when the world is entered");

        // A new context: what was registered is registered again, before any handler of it runs.
        FakeScript::Close(raise);
        FakeScript::Open(WXL_SCRIPT_CONTEXT_GLUE, raise);
        Check(FakeScript::Functions().count("ProbeAdd") && FakeScript::Functions().count("ProbeGlue") &&
                  FakeScript::Functions().count("LuaServicesGreet") && FakeScript::Declared().count("PROBE_EVENT") &&
                  FakeScript::Declared().count("LUA_SERVICES_WORLD"),
              "every function and event is registered again in each new context it belongs to");
        FakeScript::Close(raise);
        FakeScript::Open(WXL_SCRIPT_CONTEXT_GAME, raise);
        Check(!FakeScript::Functions().count("ProbeGlue") && FakeScript::Functions().count("ProbeAdd"),
              "and only in those");

        const auto searched = [](const fs::path& folder) {
            std::error_code ignored;
            for (const auto& known : FakeStorage::SearchPaths())
                if (fs::equivalent(fs::path(known.first), folder, ignored)) return true;
            return false;
        };
        Check(searched(mods / "services" / "assets") && searched(mods / "lua-services" / "assets"),
              "each mod's folder is searched, however it was spelled");

        // A reload: the patches go back, newest first; the functions stay bound and reach the new state.
        const size_t restored = FakePatch::RestoreOrder().size();
        Check(api && api->reload("wxl.services") == WXL_OK, "the mod reloads");
        Check(FakePatch::RestoreOrder().size() == restored + 2 &&
                  FakePatch::RestoreOrder()[restored] == 2 && FakePatch::RestoreOrder()[restored + 1] == 1,
              "a reload restores the mod's patches, newest first");
        Check(sink.Count("[wxl.services] patch true true 4096 4097 nil not_found") == 2,
              "the reloaded mod patches again over the bytes it expects");
        wxl_script_call again = ScriptCallOf({Number(4), Number(5)});
        Check(FakeScript::Invoke("ProbeAdd", again) && again.results.size() == 2 && again.results[0].as.number == 9 &&
                  !sink.Has("RegisterFunction 'ProbeAdd'"),
              "a function registered again by its reloaded mod keeps its binding and runs the new code");

        // A reload that fails leaves the name bound to nobody.
        WriteFile(mods / "services" / "init.lua", "this is not lua\n");
        Check(api && api->reload("wxl.services") == WXL_OK && FakePatch::Live().empty() &&
                  FakePatch::Image() == std::vector<uint8_t>(16, 0x90),
              "a reload that fails to load still gives the patches back, and the image is as it was");
        Check(!searched(mods / "services" / "assets") && searched(mods / "lua-services" / "assets"),
              "and its folder leaves the search; the other mod's stays");
        wxl_script_call after = ScriptCallOf({});
        Check(FakeScript::Invoke("ProbeAdd", after) && Contains(after.error, "the Lua mod that registered it is not running"),
              "a function whose mod is not loaded fails the call, saying so");

        host.Shutdown();
        Check(FakeStorage::SearchPaths().empty(), "the mods' folders leave the search as they close");
        // What the context holds points into wxl.lua, which is unloaded now.
        FakeScript::Functions().clear();
        FakeStorage::Files().erase("Interface\\FrameXML\\FrameXML.toc");
        FakeStorage::SearchPaths().clear();
        if (g_failures) sink.Dump();
    }

    Section("registering before any script context, and redirects");
    {
        using wxltest::FakeScript;
        using wxltest::FakeStorage;

        const fs::path mods = MakeTree(scratch, "early", binary, fs::path());
        WriteFile(mods / "early" / "mod.json", LuaManifest("wxl.early"));
        WriteFile(mods / "early" / "init.lua", R"lua(
print("early " .. tostring(wxl.script.Live()) .. " " .. tostring(wxl.script.RegisterFunction("EarlyGreet", function() return "hi" end)) ..
      " " .. tostring(wxl.script.RegisterEvent("EARLY_EVENT")))
local s = wxl.storage
print("redirect " .. tostring(s.Redirect("Interface\\Nope.txt", "Interface\\hello.txt")) .. " " .. tostring(s.Read("interface/NOPE.txt")))
print("redirect missing " .. tostring(select(2, pcall(s.Redirect, "x"))))
s.Redirect("Interface\\Icon.blp", "Interface\\A.blp")
)lua");
        WriteFile(mods / "later" / "mod.json", LuaManifest("wxl.later", "\"loadAfter\": [ \"wxl.early\" ]"));
        const char* kLater = R"lua(
local s = wxl.storage
s.Redirect("interface/icon.BLP", "Interface\\B.blp")
print("later " .. tostring(s.Unredirect("Interface\\Nope.txt")) .. " " .. tostring(select(2, s.Unredirect("Interface\\Nope.txt"))))
)lua";
        WriteFile(mods / "later" / "init.lua", kLater);

        // No context yet, as at a real load: the mods load before the client makes one.
        FakeScript::Live() = FakeScript::kNone;
        FakeScript::Functions().clear();
        FakeScript::Declared().clear();
        FakeScript::Refused() = 0;
        FakeStorage::Redirects().clear();

        CaptureSink sink;
        HostConfig  config = Config(mods, sink);
        config.services.push_back(PublishedService{WXL_SCRIPT_SERVICE, WXL_SCRIPT_SERVICE_VERSION, FakeScript::Table()});
        config.services.push_back(PublishedService{WXL_STORAGE_SERVICE, WXL_STORAGE_SERVICE_VERSION, FakeStorage::Table()});
        ModHost host(std::move(config));
        host.LoadAll();
        const auto raise = [&](wxl_event event, const void* args) { host.Raise(event, args); };
        const wxl_lua_api* api =
            static_cast<const wxl_lua_api*>(host.Table()->get_service(WXL_LUA_SERVICE, WXL_LUA_SERVICE_VERSION));

        Check(sink.Has("[wxl.early] early false true true") && FakeScript::Refused() == 0 &&
                  FakeScript::Functions().empty(),
              "a function and an event registered before any context are kept, and the service is not called");
        FakeScript::Open(WXL_SCRIPT_CONTEXT_GLUE, raise);
        Check(FakeScript::Functions().count("EarlyGreet") && FakeScript::Declared().count("EARLY_EVENT") &&
                  FakeScript::Refused() == 0,
              "they are registered when the first context comes");

        Check(sink.Has("[wxl.early] redirect true hello"),
              "Redirect opens one name in place of another, in any case and with either slash");
        Check(sink.Has("wxl.storage.Redirect: 'to' (argument 2) is missing; it takes a string"),
              "a name left out is an error naming it");
        Check(sink.Has("[wxl.later] later nil not_found"),
              "a mod takes back only its own redirects");
        Check(FakeStorage::Redirects()["interface\\icon.blp"] == "Interface\\B.blp",
              "two mods redirect one name: the runtime holds the later one");
        WriteFile(mods / "later" / "init.lua", "-- redirects nothing now\n");
        Check(api && api->reload("wxl.later") == WXL_OK &&
                  FakeStorage::Redirects()["interface\\icon.blp"] == "Interface\\A.blp",
              "the later mod's redirect goes with its state, and the earlier one answers again");

        host.Shutdown();
        Check(FakeStorage::Redirects().empty(), "every redirect goes when its mod does");
        FakeScript::Functions().clear();
        FakeScript::Live() = WXL_SCRIPT_CONTEXT_GAME;
        if (g_failures) sink.Dump();
    }

    Section("the service modules without the services");
    {
        const fs::path mods = MakeTree(scratch, "services-absent", binary, fs::path());
        WriteFile(mods / "absent" / "mod.json", LuaManifest("wxl.absent"));
        WriteFile(mods / "absent" / "init.lua", kServicesAbsent);
        CaptureSink sink;
        ModHost     host(Config(mods, sink));
        host.LoadAll();
        Check(sink.Has("[wxl.absent] absent nil unsupported nil unsupported"),
              "a module whose service is not published answers nil, unsupported, shims included");
        host.Shutdown();
    }

    Section("what a reload watches");
    {
        const fs::path mods = MakeTree(scratch, "watch", binary, fs::path());
        const fs::path watcher = mods / "watcher";
        WriteFile(watcher / "mod.json", LuaManifest("wxl.watcher"));
        WriteFile(watcher / "init.lua", kWatcher);
        WriteFile(watcher / "lib" / "part.lua", "return 'one'\n");
        WriteFile(watcher / "data.txt", "first");

        CaptureSink sink;
        ModHost     host(Config(mods, sink));
        host.LoadAll();
        host.Stage(WXL_STAGE_READY);
        const wxl_lua_api* api = static_cast<const wxl_lua_api*>(host.Table()->get_service(WXL_LUA_SERVICE, WXL_LUA_SERVICE_VERSION));
        Check(sink.Has("[wxl.watcher] watcher init one later=false data=first 1.0.0"), "the watcher loads");
        Check(api && fs::exists(watcher / "notes.txt") && api->poll() == 0,
              "a file the mod writes into its own folder does not reload it");
        WriteFile(watcher / "data.txt", "second, longer");
        WriteFile(watcher / "notes.txt", "written by someone else");
        WriteFile(watcher / "other.lua", "print('never loaded')\n");
        Check(api && api->poll() == 0, "nor does a data file it read, or a file it never loaded");

        WriteFile(watcher / "lib" / "part.lua", "return 'version two'\n");
        Check(api && api->poll() == 1 && sink.Has("[INFO ] [wxl.lua] mod wxl.watcher: lib/part.lua changed") &&
                  sink.Has("[wxl.watcher] watcher init version two later=false data=second, longer"),
              "a module it required reloads it when it changes, and the log names the file");
        WriteFile(watcher / "lib" / "later.lua", "return true\n");
        Check(api && api->poll() == 1 && sink.Has("[wxl.watcher] watcher init version two later=true"),
              "and so does a module it required and did not find, once it appears");
        Check(api && api->poll() == 0, "and then nothing has changed");

        std::string manifest = LuaManifest("wxl.watcher");
        manifest.replace(manifest.find("1.0.0"), 5, "1.0.1");
        WriteFile(watcher / "mod.json", manifest);
        Check(api && api->poll() == 1 && sink.Has("[wxl.lua] loaded wxl.watcher 1.0.1") &&
                  sink.Has("[wxl.watcher] watcher init version two later=true data=second, longer 1.0.1"),
              "a changed mod.json is read again at the reload");

        std::string both = manifest;
        both.replace(both.find("\"lua\""), 0, "\"binaries\": { \"win-x64\": \"bin/w.dll\" },\n  ");
        WriteFile(watcher / "mod.json", both);
        Check(api && api->poll() == 1 &&
                  sink.Has("[ERROR] [wxl.lua] mod wxl.watcher stays unloaded: its mod.json no longer reads: ") &&
                  sink.Has("'binaries' and 'lua' cannot both be given") &&
                  sink.Has("; it loads again when its files change"),
              "a mod.json that no longer reads leaves the mod unloaded, saying why");
        Check(api && api->poll() == 0, "without trying again until it changes");
        std::string renamed = manifest;
        renamed.replace(renamed.find("wxl.watcher"), 11, "wxl.renamed");
        WriteFile(watcher / "mod.json", renamed);
        Check(api && api->poll() == 1 &&
                  sink.Has("mod wxl.watcher stays unloaded: its mod.json now gives the id 'wxl.renamed', and a mod's id cannot change while the client runs"),
              "an id cannot change under a running mod");
        WriteFile(watcher / "mod.json", manifest + " ");
        const size_t inits = sink.Count("[wxl.watcher] watcher init");
        Check(api && api->poll() == 1 && sink.Count("[wxl.watcher] watcher init") == inits + 1,
              "a mod.json put right loads it again");

        host.Shutdown();
        if (g_failures) sink.Dump();
    }

    Section("the memory-report example");
    {
        using wxltest::FakeFootprint;
        using wxltest::FakeScript;

        const fs::path mods   = MakeTree(scratch, "memory", binary, fs::path());
        const fs::path folder = mods / "memory-report";
        fs::copy(fs::path(example).parent_path() / "memory-report", folder, fs::copy_options::recursive);
        FakeScript::Live() = WXL_SCRIPT_CONTEXT_GAME;
        FakeScript::Functions().clear();
        FakeFootprint::Running()   = false;
        FakeFootprint::Snapshots() = 0;
        FakeFootprint::Reports().clear();

        CaptureSink sink;
        HostConfig  config = Config(mods, sink);
        config.services.push_back(PublishedService{WXL_SCRIPT_SERVICE, WXL_SCRIPT_SERVICE_VERSION, FakeScript::Table()});
        config.services.push_back(
            PublishedService{WXL_FOOTPRINT_SERVICE, WXL_FOOTPRINT_SERVICE_VERSION, FakeFootprint::Table()});
        ModHost host(std::move(config));
        host.LoadAll();
        host.Stage(WXL_STAGE_READY);
        const wxl_lua_api* api =
            static_cast<const wxl_lua_api*>(host.Table()->get_service(WXL_LUA_SERVICE, WXL_LUA_SERVICE_VERSION));

        Check(FakeFootprint::Running() && FakeFootprint::Threshold() == 256ull * 1024 * 1024,
              "the example starts accounting at load and watches the address space");

        wxl_script_call asked = ScriptCallOf({});
        Check(FakeScript::Invoke("MemoryReport", asked) && asked.results.size() == 1 &&
                  std::string(asked.results[0].as.string.data, asked.results[0].as.string.size) == "memory-report.txt",
              "MemoryReport() writes a report and answers its name");
        const std::string text = ReadFile(folder / "memory-report.txt");
        Check(FakeFootprint::Snapshots() == 1 &&
                  text.find("700.0 MB   in 3000 blocks; the largest is      90.0 MB") != std::string::npos &&
                  text.find("Storm by source file: 2 files") != std::string::npos &&
                  text.find("Data\\patch-3.MPQ") != std::string::npos &&
                  text.find("DBFilesClient\\Spell.dbc") != std::string::npos,
              "the report holds the address space, Storm by source file, the archives and the databases");
        Check(text.find("wxl.memory-report/heap") != std::string::npos &&
                  FakeFootprint::Reports().count("wxl.memory-report/heap"),
              "and the mod's own Lua heap, which Snapshot reports for every Lua mod");
        Check(ReadFile(folder / "memory-sources.csv").find("new,524288000,1000000,545259520,9000000") !=
                  std::string::npos,
              "memory-sources.csv lists every source file");
        Check(api && api->poll() == 0, "writing its reports into its own folder does not reload the mod");

        const wxl_memory_pressure_args pressure{80ull << 20, 600ull << 20, 2700ull << 20, 256ull << 20};
        host.Raise(WXL_EV_MemoryPressure, &pressure);
        Check(sink.Has("[wxl.memory-report] address space low: the largest free block is      80.0 MB") &&
                  FakeFootprint::Snapshots() == 2,
              "MemoryPressure is logged and brings a report of its own");

        host.Stage(WXL_STAGE_SHUTDOWN);
        const std::string history = ReadFile(folder / "memory-history.csv");
        size_t            rows    = 0;
        for (char c : history) rows += c == '\n';
        Check(rows == 4 && history.find("the client is closing") != std::string::npos && !FakeFootprint::Running(),
              "the last report is written at exit, one line of history per report, and accounting stops");
        if (const char* keep = std::getenv("WXL_MEMORY_REPORT_OUT"))
            for (const char* name : {"memory-report.txt", "memory-sources.csv", "memory-history.csv"})
                fs::copy_file(folder / name, fs::path(keep) / name, fs::copy_options::overwrite_existing);
    }

    Section("Lua mods on the profiler's timeline");
    {
        using wxltest::FakeProfile;

        const fs::path mods = MakeTree(scratch, "profile", binary, fs::path());
        WriteFile(mods / "zones" / "mod.json", LuaManifest("wxl.zones"));
        WriteFile(mods / "zones" / "init.lua",
                  "Observe(\"WorldScript\", \"OnUpdate\", function(delta)\n"
                  "  wxl.profile.Begin(\"work\")\n"
                  "  wxl.profile.Text(\"one\")\n"
                  "  wxl.profile.End()\n"
                  "  wxl.profile.Begin(\"left open\")\n"
                  "  wxl.profile.Plot(\"delta\", delta * 1000)\n"
                  "  wxl.profile.Message(\"tick\")\n"
                  "  wxl.profile.FrameMark(\"zones\")\n"
                  "end)\n"
                  "Observe(\"WorldScript\", \"OnWorldEnter\", function()\n"
                  "  wxl.profile.Begin(\"fails\")\n"
                  "  error(\"on purpose\")\n"
                  "end)\n");
        FakeProfile::Connected() = false;
        FakeProfile::Trace().clear();

        CaptureSink sink;
        HostConfig  config = Config(mods, sink);
        config.services.push_back(PublishedService{WXL_PROFILE_SERVICE, WXL_PROFILE_SERVICE_VERSION, FakeProfile::Table()});
        ModHost host(std::move(config));
        host.LoadAll();
        host.Stage(WXL_STAGE_READY);

        const wxl_update_args tick{0.016f};
        host.Raise(WXL_EV_Update, &tick);
        Check(FakeProfile::Trace().empty(), "nothing reaches the profiler while no viewer is connected");

        FakeProfile::Connected() = true;
        host.Raise(WXL_EV_Update, &tick);
        const std::vector<std::string> update = {"+wxl.zones: OnUpdate", "+work",       "text:one",     "-work",
                                                 "+left open",           "plot:delta=16", "message:tick", "frame:zones",
                                                 "-left open",           "-wxl.zones: OnUpdate"};
        Check(FakeProfile::Trace() == update,
              "a handler is a zone named after its mod; Begin, End and Text nest inside it, and what it left open "
              "is closed when it returns");

        FakeProfile::Trace().clear();
        const wxl_world_args enter{};
        host.Raise(WXL_EV_WorldEnter, &enter);
        const std::vector<std::string> failed = {"+wxl.zones: OnWorldEnter", "+fails", "-fails",
                                                 "-wxl.zones: OnWorldEnter"};
        Check(FakeProfile::Trace() == failed, "and when it errors");
        FakeProfile::Connected() = false;
        host.Shutdown();
        if (g_failures) sink.Dump();
    }


    Section("the probe example");
    {
        using wxltest::FakeScript;

        const fs::path mods = MakeTree(scratch, "probe-example", binary, fs::path());
        fs::copy(fs::path(example).parent_path() / "probe", mods / "probe", fs::copy_options::recursive);
        {
            std::ofstream plan(mods / "probe" / "steps.txt");
            plan << "# a test plan\n\nwalk to the bank\n  open it  \n";
        }
        FakeScript::Live() = WXL_SCRIPT_CONTEXT_GAME;
        FakeScript::Functions().clear();
        FakeScript::Chunks().clear();

        CaptureSink sink;
        HostConfig  config = Config(mods, sink);
        // A runtime that never raises sounds, so the probe has something to say it lacks.
        config.events.erase(std::remove(config.events.begin(), config.events.end(), WXL_EV_SoundPlay),
                            config.events.end());
        config.services.push_back(PublishedService{WXL_SCRIPT_SERVICE, WXL_SCRIPT_SERVICE_VERSION, FakeScript::Table()});
        ModHost host(std::move(config));
        host.LoadAll();
        host.Stage(WXL_STAGE_READY);

        // Every event but the one this runtime lacks and the two the probe skips.
        const size_t offered = static_cast<size_t>(WXL_EV_COUNT) - 3;
        Check(sink.Has(("[wxl.probe] listening to " + std::to_string(offered) + " events").c_str()) &&
                  sink.Has("[wxl.probe] not raised by this runtime: OnSoundPlay"),
              "the probe listens to every event the runtime raises, and names the one it does not");
        Check(host.Listening(WXL_EV_TextureLoad) && !host.Listening(WXL_EV_SoundPlay),
              "which turns every raised event on, as its README warns");
        Check(!host.Listening(WXL_EV_ModelBatchDrawn) && sink.Has("[wxl.probe] not listened to: OnModelBatchDrawn (skipped)"),
              "but the model batches, thousands a frame, which it skips and says so");

        const wxl_world_args world{571, {5000.25f, -12.5f, 31.0f}};
        host.Raise(WXL_EV_WorldEnter, &world);
        Check(sink.Has("[wxl.probe] OnWorldEnter #1: 571, (5000.25, -12.50, 31.00)"),
              "an event's first occurrence is logged with its arguments, a position as three numbers");

        const unsigned char bytes[] = {0x01, 0x00, 0xFF, 0x10};
        wxl_message_args    message{};
        message.opcode = 0x1F6;
        message.data   = bytes;
        message.size   = sizeof bytes;
        host.Raise(WXL_EV_MessageReceived, &message);
        Check(sink.Has("[wxl.probe] OnMessageReceived #1: 502, <4 bytes 01 00 FF 10>, nil"),
              "bytes are shown in hex, an answer the runtime gave nothing to answer through as nil");

        const wxl_cvar_args cvar{wxl_str{"wxlProbe", 8}, wxl_str{"\x82", 1}, wxl_str{"caf\xC3\xA9", 5}};
        host.Raise(WXL_EV_CVarChanged, &cvar);
        Check(sink.Has("[wxl.probe] OnCVarChanged #1: \"wxlProbe\", <1 bytes 82>, \"caf\xC3\xA9\""),
              "a string that is not UTF-8 is shown as its bytes, so the log stays text; UTF-8 is kept as text");

        wxl_ui_event_args ui{};
        ui.name = wxl_str{"PLAYER_ENTERING_WORLD", 21};
        for (int i = 0; i < 7; ++i) host.Raise(WXL_EV_UiEvent, &ui);
        Check(sink.Has("[wxl.probe] OnUiEvent #5 (last shown): 0, \"PLAYER_ENTERING_WORLD\", \"\"") &&
                  !sink.Has("OnUiEvent #6"),
              "the first five occurrences are logged, then only counted");
        bool slash = false;
        for (const std::string& chunk : FakeScript::Chunks())
            if (Contains(chunk, "wxl.probe: ") && Contains(chunk, "SLASH_WXLPROBEMARK1 = \"/mark\"")) slash = true;
        Check(slash, "PLAYER_ENTERING_WORLD gives the client's chat a /mark command");

        const wxl_update_args tick{40.0f};
        host.Raise(WXL_EV_Update, &tick);
        host.Raise(WXL_EV_Update, &tick);
        Check(sink.Has("[wxl.probe] minute 1: 5 events, 12 raises: OnUiEvent 7, OnUpdate 2, OnCVarChanged 1, "
                       "OnMessageReceived 1, OnWorldEnter 1"),
              "after a minute, a line of what came in it, busiest first");

        wxl_value text{};
        text.type      = WXL_VALUE_STRING;
        text.as.string = wxl_str{" opened the bank ", 17};
        wxl_script_call marked = ScriptCallOf({text});
        Check(FakeScript::Invoke("WxlProbeMark", marked) && marked.results.size() == 2 &&
                  marked.results[0].as.number == 1.0,
              "WxlProbeMark answers the mark's number, and the text it marked");
        Check(sink.Has("[wxl.probe] ==== MARK 1: opened the bank ====") &&
                  sink.Has("[wxl.probe] since the probe loaded (80.0 s): 5 events, 12 raises: OnUiEvent 7, OnUpdate 2"),
              "a mark is a line of its own, followed by what came since the previous one");
        Check(sink.Has("[wxl.probe] memory at mark 1: no snapshot: unsupported"),
              "and the memory, or why there is none");

        Check(sink.Has("[wxl.probe] 2 step(s) read from steps.txt"), "a steps.txt is read, comments and blank lines left out");
        wxl_value empty{};
        empty.type      = WXL_VALUE_STRING;
        empty.as.string = wxl_str{"", 0};
        wxl_script_call stepped = ScriptCallOf({empty});
        Check(FakeScript::Invoke("WxlProbeMark", stepped) && stepped.results.size() == 2 &&
                  stepped.results[0].as.number == 2.0 &&
                  sink.Has("[wxl.probe] ==== MARK 2: step 1/2: walk to the bank ===="),
              "/mark with no text moves to the next step and marks it");
        wxl_script_call current = ScriptCallOf({});
        Check(FakeScript::Invoke("WxlProbeStep", current) && current.results.size() == 1,
              "/step answers the current step");
        wxl_script_call last  = ScriptCallOf({empty});
        wxl_script_call after = ScriptCallOf({empty});
        Check(FakeScript::Invoke("WxlProbeMark", last) && sink.Has("MARK 3: step 2/2: open it ====") &&
                  FakeScript::Invoke("WxlProbeMark", after) && sink.Has("MARK 4: after the last of 2 steps"),
              "the steps' text is trimmed, and a mark past the last says so");

        host.Stage(WXL_STAGE_SHUTDOWN);
        Check(sink.Has("[wxl.probe] summary: 5 of ") && sink.Has("[wxl.probe] never seen: ") &&
                  sink.Has("OnTextureLoad"),
              "at exit, the totals and the events that never came");
        Check(sink.Has("OnObjectFieldChanged (reaches only a mod that watches a field (wxl.object.Watch); the probe "
                       "watches none)"),
              "the never-seen line says why OnObjectFieldChanged cannot reach the probe");
        if (g_failures) sink.Dump();
    }

    Section("handler order across reloads");
    {
        const fs::path mods = MakeTree(scratch, "order", binary, fs::path());
        WriteFile(mods / "order-a" / "mod.json", LuaManifest("wxl.order-a"));
        WriteFile(mods / "order-a" / "init.lua", kOrderFirst);
        WriteFile(mods / "order-b" / "mod.json", LuaManifest("wxl.order-b"));
        WriteFile(mods / "order-b" / "init.lua", kOrderSecond);
        WriteFile(mods / "both" / "mod.json",
                  "{ \"id\": \"wxl.both\", \"name\": \"Both\", \"version\": \"1.0.0\", \"abi\": \"" +
                      std::string(kAbi) + "\",\n  \"binaries\": { \"" + CurrentTarget() +
                      "\": \"bin/both.dll\" },\n  \"lua\": \"init.lua\" }\n");
        WriteFile(mods / "both" / "init.lua", "print('both ran')\n");

        CaptureSink sink;
        ModHost     host(Config(mods, sink));
        host.LoadAll();
        host.Stage(WXL_STAGE_READY);
        const wxl_lua_api* api = static_cast<const wxl_lua_api*>(host.Table()->get_service(WXL_LUA_SERVICE, WXL_LUA_SERVICE_VERSION));

        Check(sink.Has("[WARN ] [host] refused both: ") && sink.Has("line 2: 'binaries' and 'lua' cannot both be given") &&
                  !sink.Has("both ran") && api && api->count() == 2,
              "a manifest with both binaries and lua is refused, and neither side runs it");

        const wxl_update_args tick{0.1f};
        size_t from = sink.lines.size();
        host.Raise(WXL_EV_Update, &tick);
        host.Raise(WXL_EV_Update, &tick);
        Check(OrderSince(sink.lines, from) == "a1 b a1 a2 b",
              "mods run in load order; a handler a mod adds runs after its own and before the next mod's");

        Check(api && api->reload("wxl.order-a") == WXL_OK, "the first mod reloads");
        from = sink.lines.size();
        host.Raise(WXL_EV_Update, &tick);
        host.Raise(WXL_EV_Update, &tick);
        const std::string after = OrderSince(sink.lines, from);
        Check(after == "a1 b a1 a2 b", ("a reloaded mod keeps its place: " + after).c_str());

        host.Shutdown();
        if (g_failures) sink.Dump();
    }

    Section("the in-game suite's reload and order mods");
    {
        using wxltest::FakeScript;

        // tests/ingame, next to the examples folder lua-hello sits in.
        const fs::path ingame = fs::path(example).parent_path().parent_path() / "tests" / "ingame";
        const fs::path mods   = MakeTree(scratch, "ingame", binary, fs::path());
        for (const char* mod : {"wxl.selftest", "wxl.test-abi-old", "wxl.test-events", "wxl.test-fields",
                                "wxl.test-order-a", "wxl.test-order-b", "wxl.test-reload", "wxl.test-search",
                                "wxl.test-watch-other"})
            fs::copy(ingame / mod, mods / mod, fs::copy_options::recursive);
        FakeScript::Live() = WXL_SCRIPT_CONTEXT_GAME;
        FakeScript::Functions().clear();

        CaptureSink sink;
        HostConfig  config = Config(mods, sink);
        config.services.push_back(PublishedService{WXL_SCRIPT_SERVICE, WXL_SCRIPT_SERVICE_VERSION, FakeScript::Table()});
        ModHost host(std::move(config));
        host.LoadAll();
        host.Stage(WXL_STAGE_READY);
        const wxl_lua_api* api = static_cast<const wxl_lua_api*>(host.Table()->get_service(WXL_LUA_SERVICE, WXL_LUA_SERVICE_VERSION));

        // Calls a registered function with one string argument, or none; answers its first
        // value as text, or "error: <message>".
        const auto ask = [](const char* name, const char* argument = nullptr) -> std::string {
            wxl_value text{};
            text.type      = WXL_VALUE_STRING;
            text.as.string = wxl_str{argument, argument ? static_cast<uint32_t>(std::strlen(argument)) : 0u};
            wxl_script_call call = argument ? ScriptCallOf({text}) : ScriptCallOf({});
            if (!FakeScript::Invoke(name, call)) return "error: not registered";
            if (!call.error.empty()) return "error: " + call.error;
            if (call.results.empty()) return "nothing";
            const wxl_value& first = call.results[0];
            if (first.type == WXL_VALUE_STRING) return std::string(first.as.string.data, first.as.string.size);
            if (first.type == WXL_VALUE_BOOL) return first.as.boolean ? "true" : "false";
            return "other";
        };
        // The chat message wxl.selftest sends for the order check.
        const auto send = [&](const char* text) {
            wxl_bool                suppressed = WXL_FALSE;
            const wxl_chat_send_args chat{wxl_str{text, static_cast<uint32_t>(std::strlen(text))}, wxl_str{"WHISPER", 7},
                                          wxl_str{"", 0}, wxl_str{"Me", 2}, wxl_str{"", 0}, WXL_FALSE, &suppressed};
            host.Raise(WXL_EV_ChatSend, &chat);
            return suppressed == WXL_TRUE;
        };
        const auto seen = [&](const char* n) {
            wxl_script_call call = ScriptCallOf({Number(std::atof(n))});
            return FakeScript::Invoke("TestOrderSeen", call) && call.results.size() == 1 &&
                   call.results[0].type == WXL_VALUE_BOOL && call.results[0].as.boolean;
        };

        Check(api && api->count() == 9 && !sink.Has("[ERROR]") && sink.Has("[wxl.selftest] ready: "),
              "every Lua mod of the suite loads without an error");
        Check(ask("TestReloadPart") == "part one", "wxl.test-reload answers the part it shipped with");
        Check(send("wxl order 1") && seen("1"),
              "wxl.test-order-a answers the order message, and wxl.test-order-b, after it, sees that answer");

        Check(ask("TestReloadWrite", "part number two") == "part number two" && api && api->poll() == 1 &&
                  ask("TestReloadPart") == "part number two",
              "TestReloadWrite rewrites lib/part.lua; the mod reloads with the new part");

        Check(ask("TestOrderTouch", "a-second") == "a-second" && api && api->poll() == 1 &&
                  ask("TestOrderMark") == "a-second",
              "TestOrderTouch rewrites mark.lua and wxl.test-order-a reloads");
        Check(send("wxl order 2") && seen("2"), "wxl.test-order-a still runs first after its reload");

        Check(ask("TestReloadBreak") == "true" && api && api->poll() == 1 &&
                  ask("TestReloadPart").find("not running") != std::string::npos &&
                  sink.Has("mod wxl.test-reload stays unloaded"),
              "TestReloadBreak breaks mod.json: the mod unloads and stays so");
        std::error_code ec;
        fs::copy_file(mods / "wxl.test-reload" / "mod.json.good", mods / "wxl.test-reload" / "mod.json",
                      fs::copy_options::overwrite_existing, ec);
        Check(!ec && api && api->poll() == 1 && ask("TestReloadPart") == "part number two",
              "mod.json.good put back, as wxl.test-manifest does, loads the mod again");

        host.Shutdown();
        if (g_failures) sink.Dump();
    }

    Section("objects across a reload");
    {
        using wxltest::FakeObjects;
        const fs::path mods = MakeTree(scratch, "cache", binary, fs::path());
        WriteFile(mods / "cache-a" / "mod.json", LuaManifest("wxl.cache-a"));
        WriteFile(mods / "cache-a" / "init.lua", kCacheFirst);
        WriteFile(mods / "cache-b" / "mod.json", LuaManifest("wxl.cache-b"));
        WriteFile(mods / "cache-b" / "init.lua", kCacheSecond);

        CaptureSink sink;
        ModHost     host(Config(mods, sink));
        host.LoadAll();
        host.Stage(WXL_STAGE_READY);
        const wxl_lua_api* api = static_cast<const wxl_lua_api*>(host.Table()->get_service(WXL_LUA_SERVICE, WXL_LUA_SERVICE_VERSION));

        FakeObjects::Spawn(80, 0x800);
        const wxl_object_args wolf{80, 0x800, WXL_OBJECT_UNIT};
        const wxl_target_args target{WXL_HANDLE_NONE, 80};
        host.Raise(WXL_EV_ObjectCreate, &wolf);
        size_t from = sink.lines.size();
        host.Raise(WXL_EV_TargetChanged, &target);
        Check(OrderSince(sink.lines, from) == "a target tag first state", "the first mod's field is on the object");

        Check(api && api->reload("wxl.cache-a") == WXL_OK, "the first mod reloads");
        from = sink.lines.size();
        host.Raise(WXL_EV_TargetChanged, &target);
        const std::string fresh = OrderSince(sink.lines, from);
        Check(fresh == "a target tag nil", ("a reloaded mod's new state holds none of the old fields: " + fresh).c_str());

        const wxl_update_args tick{0.1f};
        from = sink.lines.size();
        host.Raise(WXL_EV_ObjectDestroy, &wolf);
        FakeObjects::Despawn(80);
        host.Raise(WXL_EV_Update, &tick);
        const std::string gone = OrderSince(sink.lines, from);
        Check(gone == "a destroy tag reloaded state health 50 b destroy same true valid true b gone",
              ("after a reload, ObjectDestroy still reaches the reloaded mod first, then the next, and only then "
               "is the object gone in each state: " + gone).c_str());

        host.Shutdown();
        if (g_failures) sink.Dump();
    }

    Section("descriptor fields");
    {
        using wxltest::FakeObjects;
        const fs::path mods = MakeTree(scratch, "fields", binary, fs::path());
        WriteFile(mods / "fields" / "mod.json", LuaManifest("wxl.fields"));
        WriteFile(mods / "fields" / "init.lua", kFields);
        WriteFile(mods / "other" / "mod.json", LuaManifest("wxl.other"));
        WriteFile(mods / "other" / "init.lua", kOtherFields);

        CaptureSink sink;
        ModHost     host(Config(mods, sink));
        host.LoadAll();
        FakeObjects::Watches().clear();
        host.Stage(WXL_STAGE_READY);

        Check(sink.Has("[wxl.fields] by name 90 90 90 at 24"),
              "a field reads by its client name, its kind's and its own, and wxl.field gives its index");
        Check(sink.Has("[wxl.fields] bytes 1,2,0,1"), "a field of bytes reads as four numbers, low first");
        Check(sink.Has("[wxl.fields] float 1.5 guid 0x0000000000000200"), "a float as a number, a guid as its string");
        Check(sink.Has("[wxl.fields] element 0 of 7"), "an element of an array, and the whole array");
        Check(sink.Has("[wxl.fields] count 1326 148 18"), "FieldCount is the block's size");
        Check(sink.Has("[wxl.fields] chest 32 0"), "a game object reads its own fields and the object's");
        Check(sink.Has("[wxl.fields] ambiguous false 'Flags' is both Unit.Flags and Player.Flags on a Player; name one of them"),
              "a name two kinds of the object have is refused, naming both");
        Check(sink.Has("[wxl.fields] unknown a Unit has no field 'Xp'"), "a field the kind lacks is an error");
        Check(sink.Has("[wxl.fields] past UNIT_FIELD_RESISTANCES has 7 elements (0 to 6); 7 is not one of them"),
              "an element past the last is an error");
        Check(sink.Has("[wxl.fields] set true 77"), "SetField writes and Field reads it back");
        Check(sink.Has("[wxl.fields] set bytes true 4,5,6,7"), "a field of bytes is written from a table");
        Check(sink.Has("[wxl.fields] set refused SetField: UNIT_FIELD_BYTES_0 takes whole numbers from 0 to 255"),
              "and a byte out of range is refused");
        Check(sink.Has("[wxl.fields] many 0 77 false"),
              "wxl.object.Fields reads a list in one call, false where an object has no such field");
        Check(sink.Has("[wxl.fields] watch true true"), "Watch asks the runtime");
        Check(FakeObjects::Watches().size() == 6 && FakeObjects::Watches()[0].type == WXL_OBJECT_UNIT &&
                  FakeObjects::Watches()[0].first == 0x18 && FakeObjects::Watches()[0].count == 1 &&
                  FakeObjects::Watches()[1].type == WXL_OBJECT_UNKNOWN && FakeObjects::Watches()[1].first == 3,
              "with the type and the field's dwords; nil is every object");
        Check(sink.Has("[wxl.fields] watch again true"),
              "asking again for a field the mod watches answers true and asks nothing more");
        Check(sink.Has("[wxl.fields] watch refused the client does not see UNIT_FIELD_CREATEDBY change: it keeps no copy of it from before an update"),
              "a field the client does not see change is refused, saying why");

        const float               before = 1.5f, after = 2.0f;
        uint32_t                  oldBits = 0, newBits = 0;
        std::memcpy(&oldBits, &before, 4);
        std::memcpy(&newBits, &after, 4);
        const wxl_object_field_args health{FakeObjects::kBoar, WXL_OBJECT_UNIT, 0x18, 1, 90, 77};
        const wxl_object_field_args radius{FakeObjects::kBoar, WXL_OBJECT_UNIT, 0x92, 1, oldBits, newBits};
        const wxl_object_field_args target{FakeObjects::kPlayer, WXL_OBJECT_PLAYER, 0x12, 2, 0, 0xF130000000001234ull};
        const wxl_object_field_args bytes{FakeObjects::kPlayer, WXL_OBJECT_PLAYER, 0x17, 1, 0x01000201u, 0x04000201u};
        host.Raise(WXL_EV_ObjectFieldChanged, &health);
        host.Raise(WXL_EV_ObjectFieldChanged, &radius);
        host.Raise(WXL_EV_ObjectFieldChanged, &target);
        host.Raise(WXL_EV_ObjectFieldChanged, &bytes);
        Check(sink.Has("[wxl.fields] changed Boar Unit 24 1 90 -> 77"), "OnObjectFieldChanged hands an integer as a number");
        Check(sink.Has("[wxl.fields] changed Boar Unit 146 1 1.5 -> 2"), "a float as a number");
        Check(sink.Has("[wxl.fields] changed Thorgrim Player 18 2 0x0000000000000000 -> 0xF130000000001234"),
              "a guid as its string, nothing lost");
        Check(sink.Has("[wxl.fields] changed Thorgrim Player 23 1 1,2,0,1 -> 1,2,0,4"), "bytes as a table");
        Check(!sink.Has("[wxl.other] other changed"), "a mod receives none of the changes another mod watches");

        Check(sink.Has("[wxl.fields] unwatch true true") && FakeObjects::Watches().size() == 5,
              "Unwatch gives the field back, once, and again is no mistake");
        const wxl_object_field_args maxHealth{FakeObjects::kBoar, WXL_OBJECT_UNIT, 0x20, 1, 120, 100};
        host.Raise(WXL_EV_ObjectFieldChanged, &health);
        host.Raise(WXL_EV_ObjectFieldChanged, &maxHealth);
        Check(sink.Count("[wxl.fields] changed Boar Unit 24") == 1, "a field given back no longer reaches the mod");
        Check(sink.Has("[wxl.other] other changed 32") && !sink.Has("[wxl.fields] changed Boar Unit 32"),
              "and each mod receives what it watches");

        host.Shutdown();
        Check(FakeObjects::Watches().empty(), "the mods' watches are given back when they close");
        if (g_failures) sink.Dump();
    }

    Section("the gameplay events");
    {
        const fs::path mods = MakeTree(scratch, "gameplay", binary, fs::path());
        WriteFile(mods / "play" / "mod.json", LuaManifest("wxl.play"));
        WriteFile(mods / "play" / "init.lua", R"(
Observe("ObjectScript", "OnUnitAuraApplied", function(unit, guid, slot, spell, caster, caster_guid, stacks,
                                                      previous, level, flags, harmful, duration, remaining)
    print(string.format("aura %d slot %d stacks %d harmful %s left %d of %d caster %s", spell, slot, stacks,
                        tostring(harmful), remaining, duration, tostring(caster)))
end)
Observe("LootScript", "OnLootOpened", function(source, guid, kind, money, count)
    print("loot " .. kind .. " " .. money .. " " .. count)
end)
Observe("LootScript", "OnLootSlot", function(index, slot, item, quantity, display, suffix, property, slot_type)
    print("slot " .. index .. " " .. item .. " x" .. quantity .. " " .. slot_type .. " " .. property)
end)
Observe("ItemScript", "OnInventorySlotChanged", function(container, container_guid, slot, previous, item, item_guid)
    print("inventory slot " .. slot .. " item " .. tostring(item))
end)
Observe("QuestScript", "OnQuestGiverDetail", function(giver, giver_guid, quest, title, text, count, shared_by)
    print("detail " .. quest .. " " .. title .. " / " .. text)
end)
Observe("QuestScript", "OnQuestObjectiveChanged", function(quest, slot, objective, previous, current)
    print("objective " .. quest .. " #" .. objective .. " " .. previous .. "->" .. current)
end)
Observe("GroupScript", "OnGroupUpdated", function(group, raid, battleground, count, subgroup, leader, method)
    print("group " .. count .. " " .. tostring(raid) .. " " .. method)
end)
Observe("SocialScript", "OnWindowOpened", function(kind, npc, npc_guid)
    print("window " .. kind)
end)
Observe("PlayerScript", "OnReputationChanged", function(faction, previous, current, bonus)
    print("reputation " .. faction .. " " .. previous .. "->" .. current)
end)
Override("GroupScript", "OnGroupInvite", function(inviter, can_accept)
    print("invited by " .. inviter)
    return true
end)
Override("SocialScript", "OnDuelRequested", function(challenger, challenger_guid, arbiter)
    return nil, true
end)
function SocialScript:OnTradeStatus(status, partner)
    print("trade " .. status)
end
)");

        CaptureSink sink;
        ModHost     host(Config(mods, sink));
        host.LoadAll();
        host.Stage(WXL_STAGE_READY);

        wxl_aura_args aura{};
        aura.unit = WXL_HANDLE_NONE, aura.slot = 2, aura.spell_id = 8050, aura.stacks = 1, aura.harmful = WXL_TRUE,
        aura.duration = 18000, aura.remaining = 12000;
        host.Raise(WXL_EV_UnitAuraApplied, &aura);
        Check(sink.Has("[wxl.play] aura 8050 slot 2 stacks 1 harmful true left 12000 of 18000 caster nil"),
              "UnitAuraApplied reaches Lua with every member, a caster out of view as nil");

        wxl_loot_args loot{};
        loot.kind = WXL_LOOT_FISHING, loot.money = 12, loot.count = 1;
        host.Raise(WXL_EV_LootOpened, &loot);
        wxl_loot_slot_args slot{};
        slot.item_id = 6303, slot.quantity = 1, slot.slot_type = WXL_LOOT_SLOT_TAKE, slot.random_property_id = -3;
        host.Raise(WXL_EV_LootSlot, &slot);
        Check(sink.Has("[wxl.play] loot fishing 12 1") && sink.Has("[wxl.play] slot 0 6303 x1 take -3"),
              "LootOpened and LootSlot name their enums and keep a signed property");

        wxl_inventory_slot_args inventory{};
        inventory.slot = 23, inventory.item = WXL_HANDLE_NONE;
        host.Raise(WXL_EV_InventorySlotChanged, &inventory);
        Check(sink.Has("[wxl.play] inventory slot 23 item nil"), "InventorySlotChanged, an emptied slot's item nil");

        wxl_quest_giver_args detail{};
        detail.quest_id = 783, detail.title = wxl_str{"A Threat Within", 15}, detail.text = wxl_str{"Hail.", 5};
        host.Raise(WXL_EV_QuestGiverDetail, &detail);
        const wxl_quest_objective_args objective{783, 0, 1, 2, 3};
        host.Raise(WXL_EV_QuestObjectiveChanged, &objective);
        Check(sink.Has("[wxl.play] detail 783 A Threat Within / Hail.") && sink.Has("[wxl.play] objective 783 #1 2->3"),
              "the quest events carry their strings and counts");

        wxl_group_args group{};
        group.count = 4, group.raid = WXL_FALSE, group.loot_method = WXL_LOOT_NEED_BEFORE_GREED;
        host.Raise(WXL_EV_GroupUpdated, &group);
        Check(sink.Has("[wxl.play] group 4 false need_before_greed"), "GroupUpdated with its loot method by name");

        wxl_window_args window{};
        window.kind = WXL_WINDOW_AUCTION;
        host.Raise(WXL_EV_WindowOpened, &window);
        const wxl_reputation_args reputation{72, 2900, 3000, 0.0f};
        host.Raise(WXL_EV_ReputationChanged, &reputation);
        Check(sink.Has("[wxl.play] window auction") && sink.Has("[wxl.play] reputation 72 2900->3000"),
              "WindowOpened and ReputationChanged");

        wxl_bool              accept = WXL_FALSE, decline = WXL_FALSE;
        wxl_group_invite_args invite{};
        invite.inviter = wxl_str{"Jaina", 5}, invite.can_accept = WXL_TRUE, invite.accept = &accept,
        invite.decline = &decline;
        host.Raise(WXL_EV_GroupInvite, &invite);
        Check(sink.Has("[wxl.play] invited by Jaina") && accept == WXL_TRUE && decline == WXL_FALSE,
              "an Override of GroupInvite accepts by returning true");
        accept = decline = WXL_FALSE;
        wxl_duel_request_args duel{};
        duel.accept = &accept, duel.decline = &decline;
        host.Raise(WXL_EV_DuelRequested, &duel);
        Check(accept == WXL_FALSE && decline == WXL_TRUE, "and one of DuelRequested declines with nil, true");

        wxl_trade_args trade{};
        trade.status = WXL_TRADE_COMPLETE;
        host.Raise(WXL_EV_TradeStatus, &trade);
        Check(sink.Has("[wxl.play] trade complete"), "a family method of a new family is subscribed: SocialScript");

        host.Shutdown();
        if (g_failures) sink.Dump();
    }

    Section("the model events and wxl.m2 in Lua");
    {
        const fs::path mods = MakeTree(scratch, "models", binary, fs::path());
        WriteFile(mods / "tailor" / "mod.json",
                  LuaManifest("wxl.tailor", "\"requires\": [ { \"service\": \"wxl.m2\", \"version\": 1 } ]"));
        WriteFile(mods / "tailor" / "init.lua", R"(
local kept = {}
Observe("ObjectScript", "OnAppearanceChanged", function(change, wearer, model, race, sex, slot, display, npc)
    print("appearance " .. change .. " slot " .. slot .. " display " .. display .. " " .. type(model) .. " npc " .. tostring(npc))
    if change ~= "worn" then return end
    local buckle = wxl.m2.CreateModel(model, "Item\\Buckle.m2")
    print("made " .. tostring(buckle ~= nil) .. " " .. tostring(buckle):sub(1, 9))
    print("attach " .. tostring(buckle:Attach(model, 53, 1)))
    print("parent " .. tostring(buckle:Parent() == model))
    print("missing " .. tostring(select(2, buckle:Attach(model, 99, 0))))
    local point = wxl.m2.AttachmentPoint(model, 32)
    print("point " .. tostring(point))
    print("onbone " .. tostring(buckle:AttachToBone(model, 53, 5, { x = 0.5, y = 0, z = 1 })))
    print("prefix " .. wxl.m2.RacePrefix(race))
    kept[#kept + 1] = buckle
    local spare = wxl.m2.CreateModel(model, "Item\\Spare.m2")
    spare:Release()
    print("released " .. tostring(pcall(function() return spare:Parent() end)))
end)
Observe("AssetScript", "OnModelParsed", function(path, file, header, size)
    print("parsed " .. path .. " " .. type(file) .. " " .. size)
end)
Observe("AssetScript", "OnModelSequenceLoad", function(path, file, index, data)
    print("sequence " .. path .. " #" .. index .. " " .. data)
end)
Observe("RenderScript", "OnModelBatchDrawn", function(model, root, kind, base, min, count, start, primitives)
    print("batch " .. primitives .. " base " .. base)
end)
)");
        WriteFile(mods / "plain" / "mod.json", LuaManifest("wxl.plain"));
        WriteFile(mods / "plain" / "init.lua",
                  "print('undeclared ' .. tostring(select(2, pcall(function() return wxl.m2.RacePrefix(1) end))))\n");

        CaptureSink sink;
        HostConfig  config = Config(mods, sink);
        config.services.push_back(PublishedService{WXL_M2_SERVICE, WXL_M2_SERVICE_VERSION, wxltest::FakeModels::Table()});
        const int released = wxltest::FakeModels::Released();
        ModHost   host(std::move(config));
        host.LoadAll();
        host.Stage(WXL_STAGE_READY);

        int                       wearer = 0;
        const wxl_appearance_args worn{WXL_APPEARANCE_WORN, &wearer, wxltest::FakeModels::UnitModel(), 1, 0, 4, 416036,
                                       WXL_FALSE};
        host.Raise(WXL_EV_AppearanceChanged, &worn);
        Check(sink.Has("[wxl.tailor] appearance worn slot 4 display 416036 userdata npc false"),
              "AppearanceChanged reaches Lua: the change by name, the model as an address");
        Check(sink.Has("[wxl.tailor] made true Model(0x") && sink.Has("[wxl.tailor] attach true") &&
                  sink.Has("[wxl.tailor] parent true") && sink.Has("[wxl.tailor] missing not_found"),
              "a wxl.Model is made on the event's model and hangs on it; a missing point is not_found");
        Check(sink.Has("[wxl.tailor] point table") && sink.Has("[wxl.tailor] onbone true") &&
                  sink.Has("[wxl.tailor] prefix Hu"),
              "AttachmentPoint answers {x, y, z}, AttachToBone takes one, RacePrefix a string");
        Check(sink.Has("[wxl.tailor] released false") && wxltest::FakeModels::Released() == released + 1,
              "a model given back is given back once, and refuses its methods after");
        Check(sink.Has("[wxl.plain] undeclared") && sink.Has("reaches the client's memory"),
              "a mod that does not declare wxl.m2 is told how to");

        int                         file = 0;
        const wxl_model_parsed_args parsed{wxl::Str("Boar.m2"), &file, &file, 2048};
        host.Raise(WXL_EV_ModelParsed, &parsed);
        const wxl_model_sequence_args sequence{wxl::Str("Boar.m2"), &file, 3, "keys", 4};
        host.Raise(WXL_EV_ModelSequenceLoad, &sequence);
        wxl_model_batch_args batch{};
        batch.primitive_count = 12, batch.base_vertex = -4;
        host.Raise(WXL_EV_ModelBatchDrawn, &batch);
        Check(sink.Has("[wxl.tailor] parsed Boar.m2 userdata 2048") && sink.Has("[wxl.tailor] sequence Boar.m2 #3 keys") &&
                  sink.Has("[wxl.tailor] batch 12 base -4"),
              "the loader's stages and a drawn batch reach Lua");

        host.Shutdown();
        Check(wxltest::FakeModels::Released() == released + 2, "the mod's models are given back when it unloads");
        if (g_failures) sink.Dump();
    }

    Section("hashes, overrides, reloads and the relay in Lua");
    {
        using wxltest::FakeRelay;
        using wxltest::FakeStorage;
        const fs::path mods = MakeTree(scratch, "bricks", binary, fs::path());
        WriteFile(mods / "fetcher" / "mod.json", LuaManifest("wxl.fetcher"));
        WriteFile(mods / "fetcher" / "init.lua", R"(
local s = wxl.storage
local h = s.HashFile("Interface\\hello.txt")
print("hash " .. h.md5 .. " " .. h.size .. " " .. h.layer .. " " .. h.source)
print("nohash " .. tostring(select(2, s.HashFile("Interface\\none.txt"))))
print("override " .. tostring(s.OverrideFile("Interface\\big.bin", "Interface\\hello.txt")) .. " " ..
      tostring(s.HashFile("Interface\\big.bin").size) .. " " .. tostring(s.RestoreFile("Interface\\big.bin")) .. " " ..
      tostring(select(2, s.RestoreFile("Interface\\big.bin"))))
s.OverrideFile("Interface\\kept.bin", "Interface\\hello.txt")
local r = s.ReloadAsset("Textures\\a.blp")
print("reload " .. r.textures .. " " .. r.models .. " " .. r.objects .. " " .. r.busy .. " " ..
      tostring(select(2, s.ReloadAsset("World\\a.wmo"))))
print("async " .. tostring(type(s.HashFileAsync("Interface\\hello.txt", function(hash, why)
    print("hashed " .. tostring(hash and hash.size) .. " " .. tostring(why))
end))))
wxl.relay.Publish("wxl.fetcher.fetch", function(request, method, path)
    if method == "now" then return "Fetched\\" .. path, 7 end
    if method == "bad" then error("on purpose") end
    pending = request
end, function(request) print("told cancelled " .. tostring(request == cancelme)) end)
wxl.script.RegisterFunction("BrickAnswer", function() print("answered " .. tostring(wxl.relay.Answer(pending, "later", true))) end)
wxl.script.RegisterFunction("BrickFail", function() print("failed " .. tostring(wxl.relay.Fail(pending, "no such file"))) end)
wxl.script.RegisterFunction("BrickMark", function() cancelme = pending end)
)");
        WriteFile(mods / "asker" / "mod.json", LuaManifest("wxl.asker", "\"loadAfter\": [ \"wxl.fetcher\" ]"));
        WriteFile(mods / "asker" / "init.lua", R"(
local relay = wxl.relay
print("published " .. tostring(relay.Published("WXL.fetcher.fetch")) .. " " .. tostring(relay.Published("nobody")))
relay.Call("wxl.fetcher.fetch", "now", { "Creature\\Foo\\Foo.m2" }, function(ok, name, n)
    print("now " .. tostring(ok) .. " " .. tostring(name) .. " " .. tostring(n))
end)
relay.Call("nobody", "x", nil, function(ok, why) print("nobody " .. tostring(ok) .. " " .. tostring(why)) end)
relay.Call("wxl.fetcher.fetch", "bad", nil, function(ok, why) print("bad " .. tostring(ok) .. " " .. tostring(why)) end)
function Ask(method)
    return relay.Call("wxl.fetcher.fetch", method, { "x" }, function(ok, a, b)
        print("later " .. tostring(ok) .. " " .. tostring(a) .. " " .. tostring(b))
    end)
end
print("missing fn " .. tostring(pcall(relay.Call, "wxl.fetcher.fetch", "now", nil)))
wxl.script.RegisterFunction("BrickAsk", function(method) Ask(method) end)
)");

        wxltest::FakeScript::Live() = WXL_SCRIPT_CONTEXT_GAME;
        wxltest::FakeScript::Functions().clear();
        CaptureSink sink;
        HostConfig  config = Config(mods, sink);
        config.services.push_back(PublishedService{WXL_SCRIPT_SERVICE, WXL_SCRIPT_SERVICE_VERSION, wxltest::FakeScript::Table()});
        config.services.push_back(PublishedService{WXL_STORAGE_SERVICE, WXL_STORAGE_SERVICE_VERSION, FakeStorage::Table()});
        config.services.push_back(PublishedService{WXL_RELAY_SERVICE, WXL_RELAY_SERVICE_VERSION, FakeRelay::Table()});
        ModHost host(std::move(config));
        host.LoadAll();
        host.Stage(WXL_STAGE_READY);
        const wxl_lua_api* api =
            static_cast<const wxl_lua_api*>(host.Table()->get_service(WXL_LUA_SERVICE, WXL_LUA_SERVICE_VERSION));

        Check(sink.Has("[wxl.fetcher] hash 050102030405060708090a0b0c0d0e0f 5 archive Data\\fake.MPQ"),
              "HashFile answers the digest as hex, the size, the layer by name and the source");
        Check(sink.Has("[wxl.fetcher] nohash not_found"), "a name nobody has is not_found");
        Check(sink.Has("[wxl.fetcher] override true 5 true not_found"),
              "OverrideFile puts one name in place of another until RestoreFile");
        Check(sink.Has("[wxl.fetcher] reload 3 0 0 1 unsupported"), "ReloadAsset answers what it did as a table");
        Check(sink.Has("[wxl.fetcher] async number") && !sink.Has("hashed"), "HashFileAsync answers a request number at once");
        FakeStorage::PumpHashes();
        Check(sink.Has("[wxl.fetcher] hashed 5 nil"), "and the hash comes to the function later");
        Check(sink.Has("[wxl.asker] published true false") && sink.Has("[wxl.asker] missing fn false"),
              "Published finds a service in any case; Call without a function is an error");

        FakeRelay::Pump();
        FakeRelay::Pump();
        Check(sink.Has("[wxl.asker] now true Fetched\\Creature\\Foo\\Foo.m2 7"),
              "a service that returns values answers at once: the caller gets true and them");
        Check(sink.Has("[wxl.asker] nobody false not_found"), "a name nobody publishes fails with not_found");
        Check(sink.Has("[wxl.asker] bad false the service's Lua function errored") &&
                  sink.Has("errored in its wxl.relay service wxl.fetcher.fetch"),
              "a service whose function errors fails the request, and the error counts against it");

        // The client's script calls what the two mods registered.
        const auto run = [&](const char* function, const char* argument) {
            wxl_value text{};
            text.type            = WXL_VALUE_STRING;
            text.as.string       = argument ? wxl::Str(argument) : wxl_str{nullptr, 0};
            wxl_script_call call = argument ? ScriptCallOf({text}) : ScriptCallOf({});
            wxltest::FakeScript::Invoke(function, call);
        };
        run("BrickAsk", "later");
        FakeRelay::Pump();
        run("BrickAnswer", nullptr);
        FakeRelay::Pump();
        Check(sink.Has("[wxl.fetcher] answered true") && sink.Has("[wxl.asker] later true later true"),
              "a service that returns nothing answers later through Answer");
        run("BrickAsk", "later");
        FakeRelay::Pump();
        run("BrickFail", nullptr);
        FakeRelay::Pump();
        Check(sink.Has("[wxl.asker] later false no such file nil"), "or fails through Fail, with its reason");

        run("BrickAsk", "later");
        FakeRelay::Pump();
        run("BrickMark", nullptr);
        const size_t waiting = FakeRelay::Requests().size();
        Check(api && api->reload("wxl.asker") == WXL_OK, "the caller reloads with a request in flight");
        FakeRelay::Pump();
        Check(FakeRelay::Requests().size() + 1 == waiting && sink.Has("[wxl.fetcher] told cancelled true") &&
                  !sink.Has("[wxl.asker] later true x"),
              "its request is cancelled, the service told, the gone caller never answered");

        Check(FakeStorage::Overrides().count("interface\\kept.bin") == 1, "an override is held while its mod runs");
        Check(api && api->reload("wxl.fetcher") == WXL_OK && FakeRelay::Endpoints().count("wxl.fetcher.fetch") == 1,
              "the service is published again by the reloaded mod");
        host.Shutdown();
        Check(FakeRelay::Endpoints().empty() && FakeStorage::Overrides().count("interface\\kept.bin") == 0,
              "a mod's services and overrides go when it unloads");
        if (g_failures) sink.Dump();
    }

    Section("the camera, the pointer and the world in Lua");
    {
        using wxltest::FakeView;
        const fs::path mods = MakeTree(scratch, "camera", binary, fs::path());
        WriteFile(mods / "aim-a" / "mod.json", LuaManifest("wxl.aim-a"));
        WriteFile(mods / "aim-a" / "init.lua", R"(
local p = wxl.camera.Position()
print("position " .. p.x .. " " .. p.z .. " fov " .. wxl.camera.Fov() .. " pivot " .. wxl.camera.Pivot().z)
print("cinematic " .. tostring(wxl.camera.Cinematic()))
print("set " .. tostring(wxl.camera.SetView({ x = 1, y = 2, z = 3 }, { x = 1, y = 0, z = 0 }, 0)))
print("capture " .. tostring(wxl.input.CaptureCursor(true)) .. " " .. tostring(wxl.input.CursorCaptured()))
print("hit " .. wxl.world.Trace({ x = 0, y = 4, z = 0 }, { x = 20, y = 4, z = 0 }).x)
print("clear " .. tostring(wxl.world.Trace({ x = 0, y = 0, z = 0 }, { x = 5, y = 0, z = 0 }, 0x100111)))
print("face " .. tostring(wxl.action.Face(2.5)))
print("block " .. tostring(wxl.input.BlockMouseLook(true)) .. " " .. tostring(wxl.input.MouseLookBlocked()))
print("head " .. tostring(wxl.pose.Head(wxl.object.LocalPlayer(), 0.5, 0.1, 0.1)) .. " " ..
      tostring(wxl.pose.ReleaseHead(wxl.object.LocalPlayer())) .. " " ..
      tostring(wxl.pose.Head(wxl.object.LocalPlayer(), 0.25, 0, 0)))
print("missing " .. tostring(select(2, pcall(function() return wxl.camera.SetView({ x = 1, y = 2, z = 3 }) end))))
Observe("WorldScript", "OnMouseMotion", function(dx, dy) print("motion " .. dx .. " " .. dy) end)
Override("WorldScript", "OnMouseWheel", function(delta) print("wheel " .. delta) return true end)
Observe("RenderScript", "OnCameraUpdate", function(index, delta) print("camera " .. index) end)
)");
        WriteFile(mods / "aim-b" / "mod.json", LuaManifest("wxl.aim-b", "\"loadAfter\": [ \"wxl.aim-a\" ]"));
        WriteFile(mods / "aim-b" / "init.lua", R"(
print("b set " .. tostring(wxl.camera.SetView({ x = 9, y = 9, z = 9 }, { x = 0, y = 1, z = 0 }, 1.2)))
print("b capture " .. tostring(wxl.input.CaptureCursor(true)))
Observe("WorldScript", "OnMouseMotion", function(dx)
    if dx ~= 99 then return end
    print("b release " .. tostring(wxl.camera.ReleaseView()) .. " " .. tostring(wxl.input.CaptureCursor(false)))
    print("b again " .. tostring(select(2, wxl.camera.ReleaseView())))
end)
)");

        CaptureSink sink;
        HostConfig  config = Config(mods, sink);
        config.services.push_back(PublishedService{WXL_CAMERA_SERVICE, WXL_CAMERA_SERVICE_VERSION, FakeView::CameraTable()});
        config.services.push_back(PublishedService{WXL_INPUT_SERVICE, WXL_INPUT_SERVICE_VERSION, FakeView::InputTable()});
        config.services.push_back(PublishedService{WXL_WORLD_SERVICE, WXL_WORLD_SERVICE_VERSION, FakeView::WorldTable()});
        config.services.push_back(PublishedService{WXL_POSE_SERVICE, WXL_POSE_SERVICE_VERSION, FakeView::PoseTable()});
        FakeView::Calls().clear();
        wxltest::FakeActions::Calls().clear();
        {
            ModHost host(std::move(config));
            host.LoadAll();
            host.Stage(WXL_STAGE_READY);
            Check(sink.Has("[wxl.aim-a] position 1 3 fov 1.5 pivot 1.75") && sink.Has("[wxl.aim-a] cinematic false"),
                  "the camera's reads answer numbers and points");
            Check(sink.Has("[wxl.aim-a] set true") && sink.Has("[wxl.aim-a] capture true true") &&
                      sink.Has("[wxl.aim-b] b set true") && sink.Has("[wxl.aim-b] b capture true"),
                  "two mods set a view and capture the pointer");
            Check(sink.Has("[wxl.aim-a] hit 10") && sink.Has("[wxl.aim-a] clear nil") && sink.Has("[wxl.aim-a] face true"),
                  "a trace answers the point met or nil, Face answers true");
            Check(sink.Has("[wxl.aim-a] block true true") && sink.Has("[wxl.aim-a] head true true true"),
                  "BlockMouseLook and Head answer true; releasing a head turned again answers");
            Check(sink.Has("[wxl.aim-a] missing") && sink.Has("'forward' (argument 2) is missing"),
                  "a view without its direction is an error naming it");
            const auto& actions = wxltest::FakeActions::Calls();
            Check(!actions.empty() && actions.back().name == "face" && actions.back().point.x == 2.5f,
                  "Face reaches wxl.action");

            const wxl_mouse_motion_args motion{3, -4};
            host.Raise(WXL_EV_MouseMotion, &motion);
            wxl_bool                   kept = WXL_FALSE;
            const wxl_mouse_wheel_args wheel{-1.0f, &kept};
            host.Raise(WXL_EV_MouseWheel, &wheel);
            const wxl_frame_args frame{7, 0.016f};
            host.Raise(WXL_EV_CameraUpdate, &frame);
            Check(sink.Has("[wxl.aim-a] motion 3 -4") && sink.Has("[wxl.aim-a] wheel -1") && kept == WXL_TRUE &&
                      sink.Has("[wxl.aim-a] camera 7"),
                  "the mouse's events and the camera's update reach Lua; an Override keeps the wheel");

            // b, the later, holds the view applied; giving it back brings a's back.
            FakeView::Calls().clear();
            const wxl_mouse_motion_args release{99, 0};
            host.Raise(WXL_EV_MouseMotion, &release);
            const auto& calls = FakeView::Calls();
            Check(sink.Has("[wxl.aim-b] b release true true") && sink.Has("[wxl.aim-b] b again not_found") &&
                      calls.size() == 1 && calls[0].name == "set_view" && calls[0].position.x == 1.0f,
                  "a mod letting its view go brings back the one set before; the pointer stays held for the other");
            Check(FakeView::Captures() == 1, "the pointer is held while one Lua mod holds it");
            FakeView::Calls().clear();
            host.Shutdown();
        }
        const auto& calls = FakeView::Calls();
        bool released = false, freed = false, unblocked = false, headReleased = false;
        for (const auto& call : calls)
        {
            if (call.name == "release_view") released = true;
            if (call.name == "free") freed = true;
            if (call.name == "unblock") unblocked = true;
            if (call.name == "release_head") headReleased = true;
        }
        Check(released && freed && FakeView::Captures() == 0,
              "the last mod's view and capture go when it unloads");
        Check(unblocked && headReleased && FakeView::Blocks() == 0, "and so do its block of the mouse look and its heads");
        if (g_failures) sink.Dump();
    }

    Section("what a mod's abi lets it use");
    {
        const wxl::reflect::Version since = wxl::reflect::SinceOf(WXL_EV_ChatMessage);
        const std::string           newer = std::to_string(since.major) + "." + std::to_string(since.minor) + "." +
                                  std::to_string(since.feature);
        const std::string older = std::to_string(since.major) + "." + std::to_string(since.minor) + "." +
                                  std::to_string(since.feature - 1);

        const fs::path mods = MakeTree(scratch, "abi", binary, fs::path());
        std::string    manifest = LuaManifest("wxl.older");
        manifest.replace(manifest.find(kAbi), std::strlen(kAbi), older);
        WriteFile(mods / "older" / "mod.json", manifest);
        WriteFile(mods / "older" / "init.lua", kOlder);
        std::string family = LuaManifest("wxl.older-family");
        family.replace(family.find(kAbi), std::strlen(kAbi), older);
        WriteFile(mods / "older-family" / "mod.json", family);
        WriteFile(mods / "older-family" / "init.lua", "function ChatScript:OnChatMessage() end\n");
        WriteFile(mods / "current" / "mod.json", LuaManifest("wxl.current"));
        WriteFile(mods / "current" / "init.lua",
                  "print('current ' .. tostring(pcall(Observe, 'ChatScript', 'OnChatMessage', function() end)))\n");

        CaptureSink sink;
        ModHost     host(Config(mods, sink));
        host.LoadAll();

        const std::string observe = "[wxl.older] observe false: Observe: OnChatMessage of ChatScript came with abi " + newer +
                                    ", and this mod's mod.json asks for " + older + "; to use it, set \"abi\": \"" +
                                    newer + "\" there";
        Check(sink.Has(observe.c_str()), "an Observe of an event newer than the mod's abi is refused, naming both");
        Check(sink.Has(("[wxl.older] override false: Override: OnChatSend of ChatScript came with abi " + newer).c_str()),
              "and so is an Override");
        Check(sink.Has("[wxl.older] older event true"), "an event its abi has is served");
        Check(sink.Has(("mod wxl.older-family failed to load init.lua: ChatScript.OnChatMessage came with abi " + newer +
                        ", and this mod's mod.json asks for " + older)
                           .c_str()),
              "a family method of a newer event stops the mod from loading");
        Check(sink.Has("[wxl.current] current true"), "a mod asking for that abi gets the event");

        host.Shutdown();
        if (g_failures) sink.Dump();
    }

    Section("what a mod's abi lets it use of the services");
    {
        // wxl.profile came with 0.1.12 and wxl.storage.RemoveSearchPath with 0.1.10 (their
        // .def rows); wxl.storage itself is there from 0.1.5 on.
        const fs::path mods = MakeTree(scratch, "service-abi", binary, fs::path());
        const auto     withAbi = [](const char* id, const char* abi) {
            std::string manifest = LuaManifest(id);
            manifest.replace(manifest.find(kAbi), std::strlen(kAbi), abi);
            return manifest;
        };
        const char* const probe = R"lua(
local function try(what, reach)
    local ok, answer = pcall(reach)
    print(what .. ' ' .. tostring(ok) .. ' ' .. (ok and type(answer) or tostring(answer)))
end
try('profile', function() return wxl.profile.Connected end)
try('profile-call', function() return wxl.profile.Connected() end)
try('footprint', function() return wxl.footprint.Running end)
try('read', function() return wxl.storage.Read end)
try('remove', function() return wxl.storage.RemoveSearchPath end)
try('unknown', function() return wxl.storage.NoSuchFunction end)
)lua";
        WriteFile(mods / "eleven" / "mod.json", withAbi("wxl.eleven", "0.1.11"));
        WriteFile(mods / "eleven" / "init.lua", probe);
        WriteFile(mods / "nine" / "mod.json", withAbi("wxl.nine", "0.1.9"));
        WriteFile(mods / "nine" / "init.lua", probe);
        WriteFile(mods / "now" / "mod.json", LuaManifest("wxl.now"));
        WriteFile(mods / "now" / "init.lua", probe);

        CaptureSink sink;
        ModHost     host(Config(mods, sink));
        host.LoadAll();

        Check(sink.Has("[wxl.eleven] profile false wxl.eleven/init.lua:6: wxl.profile came with abi 0.1.12, and this mod's mod.json asks "
                       "for 0.1.11; to use it, set \"abi\": \"0.1.12\" there"),
              "a module newer than the mod's abi is refused at its first use, naming both and what to ask for");
        Check(sink.Has("[wxl.eleven] profile-call false wxl.eleven/init.lua:7: wxl.profile came with abi 0.1.12"),
              "and a call into it is refused the same way");
        Check(sink.Has("[wxl.eleven] footprint true function") && sink.Has("[wxl.eleven] remove true function"),
              "a module, and a function, of the mod's abi are served");
        Check(sink.Has("[wxl.nine] footprint false wxl.nine/init.lua:8: wxl.footprint came with abi 0.1.11, and this mod's mod.json asks "
                       "for 0.1.9"),
              "wxl.footprint came with 0.1.11");
        Check(sink.Has("[wxl.nine] remove false wxl.nine/init.lua:10: wxl.storage.RemoveSearchPath came with abi 0.1.10, and this mod's "
                       "mod.json asks for 0.1.9; to use it, set \"abi\": \"0.1.10\" there"),
              "a function added to a module after it came is refused on its own");
        Check(sink.Has("[wxl.nine] read true function") && sink.Has("[wxl.nine] unknown true nil"),
              "the module's older functions are served, and a name it lacks is still nil");
        Check(sink.Has("[wxl.now] profile true function") && sink.Has("[wxl.now] remove true function"),
              "a mod asking for the current abi gets every module");

        host.Shutdown();
        if (g_failures) sink.Dump();
    }

    Section("cost of one dispatch");
    {
        const fs::path mods = MakeTree(scratch, "bench", binary, fs::path());
        WriteFile(mods / "bench" / "mod.json", LuaManifest("wxl.bench"));
        WriteFile(mods / "bench" / "init.lua", kBench);

        CaptureSink sink;
        ModHost     host(Config(mods, sink));
        host.LoadAll();
        host.Stage(WXL_STAGE_READY);

        const wxl_update_args tick{0.001f};
        const wxl_combat_damage_args damage{WXL_HANDLE_NONE, 0x10, WXL_HANDLE_NONE, 0x20, 133, 1, 0, 250,
                                            0, 4, 0, 0, 0, WXL_TRUE, 0};
        constexpr int kRaises = 200000;
        const auto    measure = [&](wxl_event event, const void* args) {
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < kRaises; ++i) host.Raise(event, args);
            const auto end = std::chrono::steady_clock::now();
            return std::chrono::duration<double, std::nano>(end - start).count() / kRaises;
        };

        measure(WXL_EV_Update, &tick); // warm up
        const double one = measure(WXL_EV_Update, &tick);
        std::printf("  %.0f ns per Update with one Observe handler taking delta\n", one);

        WriteFile(mods / "bench" / "init.lua", kBenchWide);
        const wxl_lua_api* api = static_cast<const wxl_lua_api*>(host.Table()->get_service(WXL_LUA_SERVICE, WXL_LUA_SERVICE_VERSION));
        Check(api && api->poll() == 1, "the bench mod reloads with the wide variant");
        measure(WXL_EV_CombatDamage, &damage);
        const double wide = measure(WXL_EV_CombatDamage, &damage);
        std::printf("  %.0f ns per CombatDamage with one handler taking its 15 parameters\n", wide);

        api->reload("wxl.bench");
        WriteFile(mods / "bench" / "init.lua", "-- nothing observed\n");
        api->poll();
        measure(WXL_EV_Update, &tick);
        const double idle = measure(WXL_EV_Update, &tick);
        std::printf("  %.0f ns per Update with no Lua handler (the queue and poll checks)\n", idle);

        Check(sink.Has("[wxl.bench] bench sum"), "the handler ran");
        host.Shutdown();
    }

    if (g_failures == 0)
    {
        std::printf("\nall checks passed\n");
        return 0;
    }
    std::printf("\n%d check(s) failed\n", g_failures);
    return 1;
}
