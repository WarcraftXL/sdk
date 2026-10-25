# 🌙 Lua mods

This guide takes you from an empty folder to a Lua mod that reacts to the game, reads the world
and changes what the client does, and then covers each part in detail. It ends with a reference
you can come back to.

You need to know some Lua (tables, functions, `local`) and to have played World of Warcraft. You
do not need C++, a compiler for your own code, or any knowledge of how the client works inside.

A Lua mod is a folder of `.lua` files. Nothing is built: the same folder runs on the stock
`Wow.exe` 3.3.5a and on the recompiled client, without a change.

---

## 🤔 How it fits together

Three pieces are involved, and it helps to know their names before you start.

- **The runtime** is what loads mods into the client. There are two: one for the stock
  `Wow.exe`, one built into the recompiled `WowClient64.exe`. Both look for mods in a folder
  named `Mods` next to the client's executable, and both write a log, `Logs/WarcraftXL.log`,
  next to it too.
- **`wxl.lua`** is a mod like any other, written in C++. It carries a Lua engine (LuaJIT, which
  runs Lua 5.1 code) and runs every Lua mod it finds in `Mods/`. Without it, a Lua mod does
  nothing.
- **Your mod** is a folder in `Mods/` with an `init.lua` in it. `wxl.lua` gives it a Lua state of
  its own: your globals, your modules and your errors never touch another mod's.

What a mod does is react to **events**. An event is something the client is about to do or has
just done: a unit died, a file is being opened, a message arrived from the server. Every event
belongs to a **family**, and the families have the names the C++ side uses for them:
`WorldScript`, `RenderScript`, `ObjectScript`, `AssetScript`, `NetScript`, `LuaScript`,
`CombatScript`, `PlayerScript`, `GlueScript`, `ChatScript`, `LootScript`, `ItemScript`,
`QuestScript`, `GroupScript` and `SocialScript`. An event is named `On` followed by what happened: `OnUnitDeath` of
`ObjectScript`, `OnMessageReceived` of `NetScript`.

A function you give to an event is a **handler**. When the event happens, your handler is called
with what the event carries -- the unit that died, the opcode and bytes of the message -- as its
parameters.

---

## 📦 1. Check that `wxl.lua` is in place

`wxl.lua` ships with both runtimes: a build of either puts it in its `Mods/` folder, ready to run
every Lua mod you write. Look for this folder next to the client's executable:

```
Mods/wxl.lua/
    mod.json
    bin/x86/wxl-lua.dll       with the stock Wow.exe
    bin/x64/wxl-lua.dll       with WowClient64.exe
    LICENSE-LuaJIT.txt
```

Each runtime takes the DLL for its own architecture, so a folder holding both serves both
clients. If the folder is there, go on to step 2.

If it is missing, copy it from the runtime's build: core's `build/dist/Mods/wxl.lua/` for the stock
client, the client's `bin/Win/Mods/wxl.lua/` for the recompiled one.

### Building it yourself

You only need this to change `wxl.lua` itself, or to build it against another SDK. It takes the
same tools as a C++ mod: CMake 3.21 or newer and Visual Studio 2022 or newer with the *Desktop
development with C++* workload, x86 and x64 build tools both ([Building a mod](building-a-mod.md)
says which components). From the SDK's checkout:

```sh
cmake -S lua -B build-lua
cmake --build build-lua --config Release
```

The first configure downloads LuaJIT at a fixed commit and builds it for each architecture. The
result is one folder, `build-lua/dist/wxl.lua/`, laid out as above: copy it into `Mods/` in place
of the one there, keeping its name.

---

## ⚡ 2. A mod in five minutes

Create a folder `Mods/hello/` and, in it, a file `init.lua`:

```lua
print("hello from " .. wxl.mod.id)

function ObjectScript:OnUnitDeath(unit)
    print((unit and unit:Name() or "something") .. " died")
end
```

Start the client, enter the world, and open `Logs/WarcraftXL.log`. Near the top you will find
`wxl.lua` finding and loading your folder, then your own line:

```
[wxl.lua] found hello in folder 'hello' (no manifest)
[wxl.lua] loaded hello 0.0.0 (hello) from init.lua
[hello] hello from hello
```

Every line your mod prints is tagged with its id, here `hello`. Now kill something, or watch
something die, and look again:

```
[hello] Young Wolf died
```

Leave the client running and change the text in `init.lua`. Within a second the log shows
`reloading hello` and the new version is the one that runs. That is the loop you will work in:
edit, save, read the log.

Three things happened that the rest of this guide explains:

- `init.lua` ran from top to bottom when `wxl.lua` loaded the mod. The `print` at the top ran
  then; the function did not.
- Defining `OnUnitDeath` on the `ObjectScript` table made it a handler: after `init.lua` had run,
  `wxl.lua` subscribed it to that event.
- `unit` is an **object**: a value standing for a thing in the world, with methods such as
  `Name()` that read it.

---

## 🧭 3. A complete example, file by file

[`examples/lua-hello/`](../examples/lua-hello/) is a small mod that uses every mechanism a Lua
mod has. Copy it into `Mods/` beside `wxl.lua` and it runs as it is. This section goes through it
in the order the files are read.

```
lua-hello/
  mod.json        who the mod is and what it needs
  init.lua        the entry file
  lib/tally.lua   a module
```

### The manifest

A folder without a manifest is a mod already, as step 2 showed: its id comes from the folder's
name and its version is `0.0.0`. A manifest, `mod.json`, gives it a stable identity and lets it
state what it needs.

**`examples/lua-hello/mod.json`**

```json
{
  "$schema": "../../schema/mod.json",
  "id": "wxl.lua-hello",
  "name": "Lua hello",
  "version": "1.0.0",
  "abi": "0.1.15",
  "description": "A Lua mod: it counts deaths, reports targets, hides a message, swaps an icon and reloads when edited.",
  "lua": "init.lua",
  "optional": [
    { "service": "wxl.object", "version": 1 }
  ]
}
```

- `lua` names the entry file, and is what makes this a Lua mod. A C++ mod lists `binaries`
  instead; this manifest has none.
- `abi` is the lowest version of the contract the mod needs, `major.minor.feature`. The contract
  grows by features: each adds events or functions, and a mod asks for the feature that brought
  the newest one it uses. lua-hello swaps a file with `wxl.storage.Redirect`, which came with
  `0.1.15`, so it asks for `0.1.15` and runs on every runtime since. A runtime older than what a mod asks for, or of another major or minor,
  refuses it and says why, instead of loading it and failing at the first unknown event.
- `optional` lists **services** the mod uses when they are there. A service is a set of
  functions a runtime offers; `wxl.object` is the one that reads world objects. Listing a service
  under `requires` instead makes the runtime refuse the mod when the service is missing.

The `$schema` line lets an editor check the file as you type. The [reference](#the-manifest-1)
lists every field.

### Modules and state

**`examples/lua-hello/init.lua`**

```lua
local Tally = require("lib.tally")

-- State lives in the file's locals: it lasts from one event to the next, and starts over when
-- the mod reloads.
local deaths = Tally.new()
local icons = 0
```

`require("lib.tally")` loads `lib/tally.lua` from the mod's own folder and returns what that
file returns. Dots in the name are folders. `require` never looks outside the mod's folder, so two
mods can each have a `lib/tally.lua` without meeting.

**`examples/lua-hello/lib/tally.lua`**

```lua
local Tally = {}
Tally.__index = Tally

function Tally.new()
    return setmetatable({ counts = {}, total = 0 }, Tally)
end

-- Counts one more for a name, and returns how many that name has now.
function Tally:add(name)
    self.counts[name] = (self.counts[name] or 0) + 1
    self.total = self.total + 1
    return self.counts[name]
end
```

`deaths` and `icons` are locals of `init.lua`. Every handler defined in the file sees them, and
they keep their values from one event to the next for as long as the mod is loaded. That is how a
mod remembers anything. When the mod reloads, the file runs again in a new Lua state and both
start over.

### Callbacks: the mod's own life

**`examples/lua-hello/init.lua`**

```lua
-- onInit runs once the client is fully up, and right after a reload.
registerForEvent("onInit", function()
    print("hello from " .. wxl.mod.name .. " on " .. wxl.runtime.name .. ", abi " .. wxl.runtime.abi)
end)

-- onShutdown runs when the client closes, and before a reload drops this state.
registerForEvent("onShutdown", function()
    print("deaths " .. deaths:summary() .. ", " .. icons .. " icon(s) opened")
end)
```

`registerForEvent` attaches a function to a moment in the mod's own life rather than to a game
event. There are four: `onInit`, `onUpdate`, `onDraw` and `onShutdown` (the
[reference](#registerforeventname-fn) says when each runs).

`init.lua` runs very early, before the client has finished starting. Handlers work from then on,
but anything that needs the client up -- the player, the interface -- belongs in `onInit`.
`onShutdown` is the last chance to report or save; here it prints the tally:

```
[wxl.lua-hello] deaths 3 seen: Thorgrim x2, Boar x1, 41 icon(s) opened
```

`wxl` is a table every mod has: `wxl.mod` describes this mod (from its manifest), `wxl.runtime`
the runtime that is running it, including `wxl.runtime.abi`, the contract version it serves.

### A handler on a family table

**`examples/lua-hello/init.lua`**

```lua
function ObjectScript:OnUnitDeath(unit)
    if not unit then
        return
    end
    -- A read answers its value, or nil and the reason: "gone" once the unit has left.
    local name = unit:Name() or "something"
    local level = unit:Level() or "?"
    print(name .. " died at level " .. level .. " (" .. deaths:add(name) .. " so far)")
end
```

Each family is a global table. A function defined on it under an event's name is a handler for
that event, subscribed once the entry file has finished running. Because it is defined with `:`,
it is a method: its first parameter, `self`, is the family table, and the event's parameters come
after it. `OnUnitDeath` carries one, the unit.

The table is checked when it is subscribed: anything on `ObjectScript` that is not a function
named after one of its events stops the mod from loading, with a log line listing the events it
does have. A misspelt `OnUnitDaeth` is caught the moment you save, not after an evening of
wondering why nothing happens.

### Objects, and reads that can fail

`unit` is an object. Its methods read the thing it stands for: `Name()`, `Level()`, `Health()`,
`Position()` and dozens more. Every read can fail -- the unit may have left your view since
the event, or the method may not apply to this kind of object -- so every read answers either
the value, or `nil` and a short reason. `unit:Name() or "something"` is the usual way to say
"this, or a fallback". When you want to know why, take the second value:

**`examples/lua-hello/init.lua`**

```lua
Observe("ObjectScript", "OnTargetChanged", function(previous, current)
    if not current then
        print("target cleared")
        return
    end
    local health, why = current:Health()
    if health then
        print("target " .. (current:Name() or "?") .. ": " .. health .. "/" ..
              (current:MaxHealth() or "?") .. " health")
    else
        print("target " .. tostring(current) .. " has no health: " .. why)
    end
end)
```

Target a chest and the mod writes:

```
[wxl.lua-hello] target GameObject(0x000000000000002C) has no health: a GameObject has no Health
```

The reasons are `"gone"` (the object left), `"unsupported"` (this runtime cannot make that read)
and a sentence naming what the object is not, as above. An object parameter is `nil` when the
event has no object to give: `current` is `nil` when you clear your target.

### `Observe`, and taking a handler back

The handler above was given with `Observe(family, event, function)`, the second way to subscribe.
It does the same as a family method, with two differences: the function is not a method (no
`self`), and `Observe` can be called at any time, from anywhere -- inside another handler, inside
`onInit` -- while family methods are only collected once, after the entry file.

`Observe` returns an id, and `Unobserve(id)` removes that handler:

**`examples/lua-hello/init.lua`**

```lua
local firstWorld
firstWorld = Observe("WorldScript", "OnWorldEnter", function(map_id, position)
    print(string.format("first world: map %d at %.1f, %.1f, %.1f", map_id, position.x,
                        position.y, position.z))
    Unobserve(firstWorld)
end)
```

`firstWorld` is declared before the call so that the function can refer to it. The handler runs
for the first world you enter and then removes itself. `position` shows another kind of
parameter: a table with `x`, `y` and `z`.

### `Override`: changing what the client does

Some events can be **answered**: the handler decides something, and the client acts on it.
`OnMessageReceived` lets a handler hide a message from the client. For these, a handler's return
value is its answer, and `Override(family, event, function)` subscribes it:

**`examples/lua-hello/init.lua`**

```lua
local SMSG_MOTD = 0x33D
Override("NetScript", "OnMessageReceived", function(opcode, data, suppressed)
    if opcode == SMSG_MOTD and not suppressed then
        print("message of the day hidden (" .. #data .. " bytes)")
        return true
    end
end)
```

`0x33D` is the opcode of the server's message of the day. `data` is the message's body as a Lua
string, so `#data` is its length in bytes. `suppressed` is the member the event is answered
through, and arrives as a parameter like the others, holding what was decided before this
handler ran: `true` if another handler already hid the message. Returning `true` hides it;
returning nothing leaves it alone.

A family method on an event that can be answered is an override already, and answers the same
way.

### Swapping a file, on any thread

The last part of the example shows a coin wherever the client opens the question-mark icon. The
client opens most of its files on its loading threads, not on the main thread, and a Lua handler
only runs on the main thread: an event raised on another thread reaches your handler as a copy,
a moment later, when the client has already opened the file ([Events from other
threads](#events-from-other-threads)). So the swap is not an answer to an event but something you
tell the runtime once, which it applies to every open, on every thread:

**`examples/lua-hello/init.lua`**

```lua
local redirected, why = wxl.storage.Redirect("Interface\\Icons\\INV_Misc_QuestionMark.blp",
                                             "Interface\\Icons\\INV_Misc_Coin_01.blp")
if not redirected then
    print("the question mark icon is not redirected: " .. why)
end
```

From this line on, whatever opens the first name opens the second. The names are compared the
way the client compares them: in any case, with either slash. Doubled backslashes are how a Lua
string holds one backslash. The redirect lasts until your mod takes it back with
`wxl.storage.Unredirect(name)`, reloads or goes. Redirecting needs abi `0.1.15`, which is why the
example's `mod.json` asks for it.

The event is still there to watch. An `Observe` of `OnFileOpen` sees every file the client opens,
from whatever thread, as a copy delivered at the next update; here it counts the icons:

**`examples/lua-hello/init.lua`**

```lua
Observe("AssetScript", "OnFileOpen", function(path)
    if path:lower():find("^interface\\icons\\") then
        icons = icons + 1
    end
end)
```

`icons` is declared at the top of the file, next to `deaths`, so that `onShutdown` can print it.

That is the whole example. The sections below take each mechanism further.

---

## 🧩 4. Handlers in depth

### Three ways to subscribe

| Way | When it is subscribed | Called with | Answers |
|---|---|---|---|
| `function Family:OnEvent(...)` | once, after the entry file has run | `self`, then the event's parameters | on an event that can be answered |
| `Observe("Family", "OnEvent", fn)` | at the call | the event's parameters | never |
| `Override("Family", "OnEvent", fn)` | at the call | the event's parameters | always; refused on an event that cannot be answered |

`Observe` and `Override` return an id for `Unobserve`. A misspelt family or event is an error at
the call, naming what exists: the families, or the family's events. A function added to a family
table after the entry file has run is never subscribed; use `Observe` from then on.

A family table holds handlers and nothing else. Keep your state in locals, or in a table of your
own: `ObjectScript.count = 0` stops the mod from loading.

### Parameters

A handler receives what the event carries, one parameter per member, in a fixed order. The
order, names and types of every event's parameters are in the editor definitions,
[`lua/types/wxl.d.lua`](../lua/types/wxl.d.lua) (see [Your editor](#-8-your-editor)), generated
from the SDK's tables so that they always say what the runtime does. For example:

```lua
function ObjectScript:OnUnitAuraChanged(unit, spell_id, removed) end
function CombatScript:OnCombatDamage(source, source_guid, target, target_guid, spell_id, kind,
                                     environment, amount, overkill, school, absorbed, resisted,
                                     blocked, crit, hit_info) end
```

You may take fewer parameters than an event has (`function(source, source_guid, target)`): the
rest are dropped, as Lua always does. You cannot skip one in the middle; name it `_` if you do
not use it.

Each member arrives as a Lua value according to its kind. The kind is what the SDK's field table,
[`include/wxl/fields.def`](../include/wxl/fields.def), says the member is:

| Kind | In Lua |
|---|---|
| `u32`, `i32`, `u64`, `i64`, `f32` | a number. A 64-bit value above 2^53 loses its lowest digits |
| `bool` | `true` or `false` |
| `handle` | an object, or `nil` when there is none |
| `guid` | a string, `"0x"` and sixteen hex digits, such as `"0x0000000000000100"`: the server's id for an object, kept whole |
| `vec3` | a table `{ x = ..., y = ..., z = ... }` |
| `str` | a string |
| `bytes` | a string holding the bytes |
| `answer` | a boolean: what was decided so far; `nil` when the runtime gave nothing to answer through |
| `buffer` | a string: what was written so far; `nil` when the runtime gave nothing to write to |
| `ptr` | an opaque value (a light userdata) or `nil`; nothing in the Lua API uses it |
| an enumeration (`WXL_ARGS_ENUM`) | a string naming the value: a script context is `"glue"` or `"game"`, a mouse button `"left"`, `"right"` or `"middle"`, an object's type `"Unit"`, `"GameObject"`... as `Type()` names it. The names are in [`include/wxl/enums.def`](../include/wxl/enums.def); a value this wxl.lua does not know yet arrives as its number |

A member that only gives the length of another is not a parameter: the string carries its own
length. `OnMessageReceived` has no `size` parameter, `OnFileOpen` no `redirect_capacity`.

Every value is a copy. A handler may keep any of them in a local and read it later. An object
kept that way reads as `"gone"` once its subject has left.

### Answers

An event can be answered when its members include an `answer` or a `buffer`. An `Override` (or a
family method on such an event) answers with its return values, one per such member, in member
order. Most events that can be answered have one such member, so one return value; the three
requests of [gameplay](#gameplay-events) (`OnGroupInvite`, `OnDuelRequested`,
`OnResurrectRequest`) have two, `accept` then `decline`:

- **an `answer`** (`suppressed`, `handled`, `refused`): `true` answers. `false` and `nil` leave it
  as it is: an answer is a request, and a later handler cannot take it back.
- **a `buffer`** (`redirect`): a string is written into it. A string longer than the buffer holds
  is not written, and the log says what the limit is.

A return value of the wrong kind (a number where `true` was expected) or more values than the
event takes are not used, and the log says so, once per mod and event. An `Observe` handler sees
the answer parameter but cannot change it.

The events that can be answered, as the SDK stands:

| Event | Answers through | What answering does |
|---|---|---|
| `WorldScript:OnInput` | `handled` | the client never sees the input |
| `WorldScript:OnMouseWheel` | `handled` | the game never sees the turn of the wheel (abi `0.1.24`) |
| `WorldScript:OnSoundPlay` | `suppressed` | the sound does not play |
| `AssetScript:OnFileOpen` | `redirect` | the client opens the path you return instead, for an open on the main thread; on any thread, use `wxl.storage.Redirect` |
| `NetScript:OnMessageReceived` | `suppressed` | the client never sees the server's message |
| `NetScript:OnMessageSent` | `suppressed` | the message does not go to the server |
| `ObjectScript:OnSpellCastRequest` | `refused` | the player's cast does not go ahead |
| `ObjectScript:OnItemUseRequest` | `refused` | the item is not used |
| `ChatScript:OnChatSend` | `suppressed` | the chat or addon message is not sent (abi `0.1.6`) |
| `GroupScript:OnGroupInvite` | `accept`, `decline` | `return true` joins the group, `return nil, true` turns it down, as the dialog's buttons do; the dialog never shows (abi `0.1.17`) |
| `SocialScript:OnDuelRequested` | `accept`, `decline` | the same for a duel challenge (abi `0.1.17`) |
| `SocialScript:OnResurrectRequest` | `accept`, `decline` | the same for an offer to resurrect you (abi `0.1.17`) |

### Loading and updates

Seven events follow what the client loads from disk and how objects change. Each is raised only
while some mod listens to it: until then the client does not even read what the event would
carry, so a mod that does not subscribe costs nothing. The busy ones are worth knowing before you
subscribe: `OnTextureLoad` runs for every texture reference, and `OnFileOpened` for every file.

| Event | When | What it carries |
|---|---|---|
| `ObjectScript:OnObjectUpdate` | an update packet from the server changed an object's fields; once per object and packet, after the whole packet is applied | the object, its guid, its type |
| `ObjectScript:OnDoodadSpawn` | the map placed a doodad: a tree, a lamp, a crate of a tile or of a building | `doodad` (always `nil`: a doodad is not an object you can read), the model's path, its position |
| `AssetScript:OnModelLoad` | a model's file (`.m2`) has been read, before the client parses it | the path, the file's bytes; the last two parameters are for C++ mods (below) |
| `AssetScript:OnTextureLoad` | the client asks for a texture by name, cached or not | the path |
| `AssetScript:OnMapObjectLoad` | a building's root file (`.wmo`) has been read, before the client walks it | the path, the file's bytes |
| `AssetScript:OnFileOpened` | an open finished, found or not | the path that was opened (after any `OnFileOpen` redirect), whether it was found, its size |
| `AssetScript:OnClientDbLoaded` | the client has loaded its databases, once per table | the file's name and the table's size; `records` is for C++ mods |

An object update and a create are different events: an object that comes into view arrives in
`OnObjectCreate`, not in `OnObjectUpdate`. To follow one field, watch it with `wxl.object.Watch(type, name)` and
`OnObjectFieldChanged` (see [Descriptor fields](#descriptor-fields-everything-the-server-says-about-an-object)),
which tell you which field moved and what it was before.

The bytes of `OnModelLoad` and `OnMapObjectLoad` are a copy of the whole file, as a string. A
handler may look at them (`data:sub(1, 4)` is the file's magic, `"MD20"` for a model), but a large
model is a large string: subscribe only while you need them. Giving the client other bytes to
parse instead is a C++ mod's job: `replacement` and `replacement_size` arrive in Lua as opaque
values that nothing in the Lua API can write.

### The order handlers run in

For one event, handlers run mod by mod, in the order the mods were loaded, and within a mod in the
order it subscribed them:

1. Mods are loaded in an order the manifests decide: `loadAfter`, then folder names in
   alphabetical order. Every handler of a mod runs before any handler of the mods loaded after
   it.
2. Within a mod, the `Observe` and `Override` calls the entry file makes come first, in order,
   then its family methods, then whatever it subscribes later (from `onInit`, or from a handler),
   in the order it does.

A mod keeps its place when it reloads: its new handlers run where its old ones did, between the
same two mods, until the client closes.

Lua mods all run inside `wxl.lua`, so next to C++ mods they run where `wxl.lua` runs.

### Events from other threads

Every handler runs on the client's main thread, the one that runs the game. A few events can
happen elsewhere: [`events.def`](../include/wxl/events.def) says so on their row, and today they
are `OnFileOpen` and `OnFileOpened` (any thread that opens files), `OnMessageSent` (the
connection's thread), and
`OnGlueRealmList` and `OnGlueRealm` (the logon connection's thread).
When one of them happens on another thread, `wxl.lua` copies it and delivers the copy at the
start of the next `OnUpdate`. Your handler sees the same values, a moment later. It can no longer
answer: the client has already gone on. The log says so the first time an `Override` gets such a
copy, once per mod and event:

```
[wxl.lua] mod wxl.my-mod: OnFileOpen was raised off the main thread; its Override handler ran on a copy at the next Update and its answer is not taken
```

The rule that follows: **an `Override` of an event raised off the main thread cannot answer it.**
`Observe` such an event to know what happened; to change what happens, give the runtime the
answer as data beforehand, which it applies on whatever thread the event comes. For files that is
`wxl.storage.Redirect` ([Swapping a file, on any thread](#swapping-a-file-on-any-thread)). An
`Override` of `OnFileOpen` still answers the files the client opens on its main thread, and the
opens your own mod makes with `wxl.storage`.

---

## 🌍 5. Objects in depth

An object stands for something in the client's world: a unit (a creature or an NPC), a player,
an item, a container, a game object (a chest, a door, a mailbox), a dynamic object (a spell's
area on the ground), a corpse. You get one from an event's parameter or from `wxl.object`:

```lua
local me = wxl.object.LocalPlayer()               -- nil while out of the world
local units = wxl.object.InView("Unit")           -- every unit in view, as a list
local someone = wxl.object.FromGuid("0x0000000000000100")  -- nil when it is not in view
```

`InView` takes a type name (`"Item"`, `"Container"`, `"Unit"`, `"Player"`, `"GameObject"`,
`"DynamicObject"`, `"Corpse"`) or nothing, for every object. A list is a snapshot: an object in
it may have left by the time you read it, and then answers `"gone"`.

The methods:

- `Name()`, `Guid()`, `Type()` (the kind's name, `"Unit"`, `"GameObject"`...), `TypeId()` (the
  same as a number) and `Target()` (what a unit is targeting, an object) work on every kind.
- `Valid()` answers `true` while the object is still there, `false` once it has left.
- `Handle()` answers the object's handle as a string, for logs.
- The others come from the runtime's table of reads and are named after it: `Health()`,
  `MaxHealth()`, `Level()`, `Power(index)`, `Position()`, `Facing()`, `DisplayId()`,
  `StackCount()`, `Durability()`... The editor definitions list all of them, with the kinds each
  applies to. A method that does not apply to an object answers `nil` and a sentence such as
  `"a GameObject has no Health"`. A read whose name would be taken twice says its kind:
  `GameObjectType()` is a game object's own type (a chest, a door), `PlayerFlags()` a player's
  flags beside the `Flags()` every unit has.

### What a unit, a player, an item can tell you

Since abi `0.1.18` each kind has a method for what the client knows of it: whether a unit is dead,
in combat, casting, mounted or stealthed, how it stands towards you, what kind of creature it is,
its auras and speeds; your player's money, experience, skills, bags and group; an item's
durability, enchantments, and what the item cache says of it. The [reference](#the-reads-by-kind)
lists them all, kind by kind. A mod that uses them asks for `"abi": "0.1.18"`; one asking for less
gets an error naming the version instead, as for an event.

Here is a mod that warns you when you target a rare elite that would attack you:

```lua
Observe("ObjectScript", "OnTargetChanged", function(previous, current)
    if not current or current:Type() ~= "Unit" then return end
    if current:Classification() == "rareelite" and current:Reaction() == "hostile" then
        print("a rare elite, level " .. current:Level() .. ": " .. current:Name())
    end
end)
```

A choice among names arrives as its name: `Classification()` answers `"normal"`, `"elite"`,
`"rare"`, `"rareelite"` or `"worldboss"`, `Reaction()` one of `"hated"`, `"hostile"`,
`"unfriendly"`, `"neutral"`, `"friendly"`, `"honored"`, `"revered"`, `"exalted"` -- the colour the
client gives the name plate, worked out by the client's own function. The editor definitions
list each set of names, so your editor completes them.

Your own player has more, kept by the client for it alone:

```lua
local me = wxl.object.LocalPlayer()
print(me:Money() .. " copper, " .. me:Xp() .. " of " .. me:NextLevelXp() .. " experience")
local weapon = me:InventoryItem("main_hand")          -- a slot by name, or by number (15)
if weapon then
    print(weapon:Name() .. ", durability " .. weapon:Durability() .. "/" .. weapon:MaxDurability())
    local quality, why = weapon:Quality()             -- "epic", or nil and "failed" until cached
end
for _, aura in ipairs(me:Auras()) do                  -- spell_id, caster, stacks, duration, expiration
    if aura.spell_id == 1459 then print("Arcane Intellect is up") end
end
for _, skill in ipairs(me:Skills()) do print(skill.id, skill.value, skill.max) end
```

An empty slot answers `nil` alone; a read that could not answer says why in its second value. The
item cache's reads (`Quality()`, `Level()`, `Class()`, `SellPrice()`...) answer `nil, "failed"`
until the client has received the item's record from the server: ask again a moment later. Asked
of another player, your player's own reads answer `nil, "invalid"`.

### What your player can do with an object

The same objects carry what the player could do with them. Each is done through the function the
client runs for a click, a key or a slash command, with the client's own checks: a spell out of
range is refused as it would be for you. Each answers `true`, or `nil` and why (`"failed"` when the
client refused it):

```lua
unit:Select()          -- targets it, as a left click does
unit:Interact()        -- a right click: talk to it, loot it, open it, use it
unit:CastAt(133)       -- casts a spell you know at it
unit:Follow()          -- as /follow
unit:Assist()          -- targets what it targets, as /assist
item:Use()             -- uses an item, on what it picks itself; item:Use(unit) on a unit
```

`wxl.action` has the same and what needs no object: `wxl.action.ClearTarget()`,
`wxl.action.StopCasting()`, `wxl.action.CastSpell(id)` at your current target, and
`wxl.action.MoveTo({ x = ..., y = ..., z = ... })`, which walks your player there as a
click-to-move click does, and `wxl.action.Face(radians)` (abi `0.1.24`), which turns your player
the way steering with the mouse does and tells the server: 0 faces north, the angle grows towards
the west. An action runs on the thread that runs the game, where your handlers of world events
run.

### One object, one value

While an object stays in view, it is one Lua value: the object an event hands you, the one
`FromGuid` finds and the one in an `InView` list are the same value (abi `0.1.7`: a mod that
counts on it asks for `"0.1.7"`). You can compare objects with `==` and use them as table keys:

```lua
local hits = {}
Observe("CombatScript", "OnCombatDamage", function(source, source_guid, target)
    if target then hits[target] = (hits[target] or 0) + 1 end
end)
```

An object also keeps fields of your own. Set one as on a table, read it back at any later event:

```lua
Observe("ObjectScript", "OnObjectCreate", function(object, guid, type)
    object.noted = true
end)
```

A field cannot take the name of a method (`object.Health = 1` is an error that says so), and
stays with the object until it leaves. That is the difference with a table of your own keyed by
objects, like `hits` above: the table keeps an object that left until you remove it, while a
field goes with the object.

### When an object leaves

When the object leaves view, your `OnObjectDestroy` handlers run first, with its fields still
there. Then the object you kept is **gone**: `Valid()` answers `false`, every read answers `nil,
"gone"`, its fields are dropped and storing a new one is an error. `tostring` shows it as `gone
Unit(0x...)`.

If the same creature comes back into view, it is a new object: a new value, with no fields, and
`OnObjectCreate` announces it again. What it shares with the old one is its guid, so key a table
by guid for what must outlast leaving and coming back:

```lua
local seen = {}
Observe("ObjectScript", "OnObjectCreate", function(object, guid, type)
    seen[guid] = (seen[guid] or 0) + 1
end)
```

A guid is a string because a Lua number cannot hold every 64-bit value; as a string it compares
and keys tables exactly. `FromGuid` takes the string, `"0x"` and up to sixteen hex digits, or a
whole number for a small one; anything else is an error.

When no runtime offers the `wxl.object` service, every read answers `nil, "unsupported"`,
`Valid()` included, and `InView` an empty list. List the service under `requires` in your
manifest if your mod is no use without it.

### Descriptor fields: everything the server says about an object

The methods above read a few facts by name. Behind them, the client keeps every object's state in
a block of numbers the server fills and updates: its **descriptor fields**. A unit has 89 of
them -- health, each power, its stats, its resistances, its flags, the guid of what it targets --
and a player 215 more. [`include/wxl/update_fields.def`](../include/wxl/update_fields.def) lists
them all, with the names the client gives them (`UNIT_FIELD_HEALTH`, `PLAYER_XP`). These methods
need abi `0.1.9`, so a mod that uses them asks for `"abi": "0.1.9"` in its `mod.json`.

**Reading one.** `Field(name)` reads a field of an object:

```lua
local me = wxl.object.LocalPlayer()
print(me:Field("UNIT_FIELD_HEALTH"))    -- the client's name
print(me:Field("Unit.Health"))          -- the kind and a shorter name, the same field
print(me:Field("Health"))               -- the short name alone, when it is not ambiguous
```

The short name is the client's without its kind's prefix, each word capitalised and the words
joined: `PLAYER_FIELD_COINAGE` is `Coinage`, `UNIT_FIELD_MAXHEALTH` is `MaxHealth` (the client runs
`MAX` and `HEALTH` together; the short names split them, the way the getters are named, so the
field and `MaxHealth()` read alike). Alone it must name one field of the object:
a player has both `Unit.Flags` and `Player.Flags`, so `me:Field("Flags")` is an error that names
the two, and you write the one you mean. A name the object does not have is an error too: a typo
is a mistake to fix, not a value to test.

What comes back depends on what the field holds, as its row in `update_fields.def` says:

| The row's value | `Field` answers |
|---|---|
| `u32`, `i32`, `f32` | a number |
| `guid`, `u64` | a string, `"0x"` and sixteen hex digits, as `Guid()` gives |
| `bytes` | four numbers, one per byte from the low end: `local race, class, sex, power = me:Field("Bytes0")` |
| `u16x2`, `i16x2` | two numbers, the low half first |

A field with several elements, such as the seven of `UNIT_FIELD_RESISTANCES`, takes the element
as a second argument, counted from 0: `unit:Field("Resistances", 6)`. `Fields(name)` answers all
of them as a list. `FieldCount()` answers how many numbers the object's block holds.

A player other than yours has a shorter block: the client keeps the fields after
`PLAYER_FIELD_PAD_0` -- bags, money, experience, skills -- for the player it controls only. Asking
another player for `PLAYER_XP` answers `nil, "invalid"`, and the runtime's log says why.

**Many objects at once.** `wxl.object.Fields(list, name)` reads one field of every object of a
list in one call, and answers a list in the same order, with `false` for an object that has no
such field (a game object in a list of units):

```lua
local units = wxl.object.InView("Unit")
local health = wxl.object.Fields(units, "Health")
for i, unit in ipairs(units) do
    if health[i] and health[i] < 100 then print(unit:Name() .. " is nearly dead") end
end
```

**Writing one.** `SetField(name, value)` changes the field in your client, with the same kinds of
value (a table of four numbers for bytes, of two for halves):

```lua
target:SetField("Unit.BoundingRadius", 3)
```

Only your client sees it, and only until the server next updates that field, which puts its own
value back. Nothing else is told: the client does not redraw a model or refresh its interface
because of a write, so a value the client reads every frame shows at once and others show when
the client next looks at them. The guid and the type, the first three numbers, are the keys the
client finds the object by and cannot be written.

**Seeing one change.** `OnObjectFieldChanged` tells you when an update of the server changes a
field. Nothing is raised until a mod asks for a field with `wxl.object.Watch(type, name)`, since
watching every field of every object would cost on every packet:

```lua
wxl.object.Watch("Player", "Coinage")
Observe("ObjectScript", "OnObjectFieldChanged", function(object, type, field, count, old, new)
    if field == wxl.field.Player.Coinage then
        print("money " .. old .. " -> " .. new)
    end
end)
```

`type` is the kind of objects to watch (`"Unit"` covers players too; `nil` every object) and the
event fires for those only. `field` is the number of the field that changed -- `wxl.field.Unit.Health`
and the like give each field's number -- and `old` and `new` are its values, of the same kinds as
`Field` gives, a table for bytes and halves. For a field with several elements, each element that
changed raises its own event, `field` being the element's number.

The client finds a change by comparing a field with a copy it keeps from before the update, and
it keeps one for most fields but not all: `Watch` answers `nil` and the reason for one it does not,
such as `UNIT_FIELD_CREATEDBY`. The `watch` column of `update_fields.def` says which: `any` on every
object, `local` on your own player only (bags, money, experience), `none` not at all.

A watch is your mod's. Your `OnObjectFieldChanged` handlers receive the changes of the fields
your mod watches, and only those: another mod watching `UNIT_FIELD_HEALTH` does not fill your log
with every health change in view, and your watch does not fill its. Asking twice for the same
field changes nothing. When you no longer need it, give it back:

```lua
wxl.object.Unwatch("Player", "Coinage")
```

From then on your handlers receive no change of it, and once no mod watches a field the client
stops reporting it. You do not have to give your watches back before your mod goes: they end when
it is disabled, when it reloads -- the new code asks again for what it needs, as it runs from the
top -- and when the client closes. `Unwatch` needs abi `0.1.10`.

---

## 🗂️ 6. The client's services

Events tell your mod what the client does. **Services** are the other direction: things your mod
asks the client for. A runtime publishes them -- the files of the game, its databases, its own
Lua -- and `wxl.lua` gives each one a module in the `wxl` table:

| Module | What it reaches |
|---|---|
| `wxl.storage` | the game's files, in its archives and in folders your mod adds |
| `wxl.data` | the client's databases (`Spell.dbc` and the others), served from your own files |
| `wxl.script` | the client's own Lua, the one its interface and addons run in |
| `wxl.hook` | the points a runtime names for a native detour |
| `wxl.patch` | bytes written into the client's program; your `mod.json` has to ask for it |
| `wxl.footprint` | where the client's memory goes: its allocations by source file, its big consumers, its address space |
| `wxl.m2` | the models objects draw with, and models your mod makes and hangs on them; your `mod.json` has to ask for it |

Every function of these modules answers the way an object's reads do: its value, or `nil` and a
reason. The reason is a short word: `"unsupported"` when this runtime does not have the service or
that function, `"not_found"`, `"invalid"` or `"failed"` when the client said so, `"pending"` when
what was asked for is not ready yet (a file a provider is still fetching: try again later).

Leaving out an argument is a mistake in your code, not a question for the client, so it is an
error that names the function and the argument:

```
wxl.storage.AddSearchPath: 'priority' (argument 2) is missing; it takes a whole number
```

Passing `nil` counts as leaving it out. A few arguments may be left out; the editor definitions
mark them with a `?` (`after?`), and the table at the end of this guide says what each one is
then, such as `wxl.patch.Find`'s `after`, which starts the search from the beginning.

The example of this chapter is [`examples/lua-services/`](../examples/lua-services/). Copy it into
`Mods/` next to `lua-hello` and start the client; each line below is from its log.

### Files: `wxl.storage`

A file is named the way the client names it: with backslashes, in any case, relative to the
game's `Data` folder. Whether it sits in an archive (`common.MPQ`, `patch.MPQ`...) or somewhere
else does not change its name:

**`examples/lua-services/init.lua`**

```lua
local TOC = "Interface\\FrameXML\\FrameXML.toc"
```

In a Lua string a backslash is written twice, so this is `Interface\FrameXML\FrameXML.toc`: the
list of files the game's own interface is made of. `Read` gives you a whole file as a string, or
`nil` and why:

**`examples/lua-services/init.lua`**

```lua
    local toc, reason = wxl.storage.Read(TOC)
    if toc then
        local _, lines = toc:gsub("\n", "")
        print(TOC .. ": " .. #toc .. " bytes, " .. lines .. " lines")
    else
        print(TOC .. " was not read: " .. reason)
    end
```

A Lua string holds any bytes, so a texture or a model reads as well as text does. For a large
file you want a piece of, `Open` gives you a `File`, which reads at an offset as many bytes as you
ask for, or what is left:

**`examples/lua-services/init.lua`**

```lua
    local file, reason = wxl.storage.Open(TOC)
    if file then
        local start, failed = file:ReadAt(0, 12)
        if start then
            print("it starts with " .. string.format("%q", start))
        else
            print("its first bytes were not read: " .. failed)
        end
        file:Close()
    else
        print(TOC .. " was not opened: " .. reason)
    end
```

```
[wxl.lua-services] it starts with "## Interface"
```

`ReadAt` answers like every other function: the bytes, or `nil` and why. `file:Close()` gives the
file back to the client. A file you forget to close is closed for you
when nothing refers to it any more, and when your mod reloads, but closing it yourself frees it
at once. `Exists(name)` and `Size(name)` answer without reading.

`Find` lists the names that match a pattern, `*` standing for any run of characters. It reads the
list of every archive, which takes a moment: keep it out of `onUpdate`.

**`examples/lua-services/init.lua`**

```lua
    local tocs = wxl.storage.Find("Interface\\FrameXML\\*.toc")
    print(#tocs .. " table(s) of contents in Interface\\FrameXML")
```

### Files of your own: the search path

Your mod can ship files the client then finds as its own: a texture, a sound, a database. Put
them in a folder of your mod, under the names the client uses, and add the folder to the files
the client searches:

**`examples/lua-services/init.lua`**

```lua
local added, why = wxl.storage.AddSearchPath(wxl.mod.directory .. "\\assets", 1)
if not added then
    print("the assets folder is not searched: " .. why)
end
```

The example has `assets\Interface\lua-services\hello.txt`, so the client now has a file named
`Interface\lua-services\hello.txt`, which `Read` finds like any other:

**`examples/lua-services/init.lua`**

```lua
    local hello = wxl.storage.Read("Interface\\lua-services\\hello.txt")
    print("from the assets folder: " .. (hello and hello:gsub("\n", "") or "nothing"))
```

```
[wxl.lua-services] from the assets folder: hello from the assets folder
```

The second argument is the folder's **priority**. The game's own files are at 0. A folder above
0 wins a name both have: put `Interface\Icons\INV_Misc_Coin_01.blp` in your folder at priority 1
and every coin in the interface is your picture. A folder at 0 or below only adds names the game
does not have. The folder has to be inside the client's folder, which your mod's own folder in
`Mods/` is; this call is best made when the mod loads, at the top of `init.lua`, so the client
finds your files from its first look. `MountArchive(path, priority)` does the same for a whole
`.MPQ`.

The folder belongs to your mod. `wxl.storage.RemoveSearchPath(folder)` takes it out again: from
then on the client finds none of its files, and a file it already opened from there stays open.
Your mod's folders also leave the search on their own when the mod is disabled, when it reloads
(the reloaded `init.lua` adds them again at its top) and when the client closes, so a mod you
remove from `Mods/` leaves nothing behind in the next session. Another mod cannot remove your
folder: for a folder it did not add, `RemoveSearchPath` answers `nil, "not_found"`.
`RemoveSearchPath` needs abi `0.1.10`.

### One file in place of another: `Redirect`

A folder of your own adds files under the client's names. When the file you want the client to
open already has a name of its own, in the game's archives or in your folder, redirect instead:
`wxl.storage.Redirect(name, other)` makes every open of `name` open `other`, which is then found
the way any name is, in a folder of the search path first. [`lua-hello`](#swapping-a-file-on-any-thread)
swaps an icon this way. The runtime answers it on every thread the client opens files on, which
an `OnFileOpen` handler cannot do ([Events from other threads](#events-from-other-threads)).

The redirect is your mod's, like its folders: redirecting the same name again changes where it
leads, `wxl.storage.Unredirect(name)` takes it back (`nil, "not_found"` for a name your mod did not
redirect), and it goes on its own when the mod is disabled, reloads or the client closes. When
two mods redirect one name, the one that did it last wins, and when it takes its redirect back
the other's answers again. An `OnFileOpen` handler that writes `redirect` still has the last
word, and sees the redirect's answer in `redirect` before it does. `Redirect` and `Unredirect`
need abi `0.1.15`.

### Serving and converting assets

The search path and redirects work with files that exist. `wxl.storage.Serve` makes one from a
string: the client then reads it as one of its own files, on whichever of its threads opens it.

**`examples/lua-assets/init.lua`**

```lua
-- A file the client opens as one of its own, on any of its threads, until the mod goes. Priority
-- 1 puts it above the client's archives; this name is in none of them anyway.
local NOTE = "Interface\\lua-assets\\note.txt"
local ok, why = wxl.storage.Serve(NOTE, "This file lives in a Lua mod's memory.", 1)
print("served " .. NOTE .. ": " .. tostring(ok or why))

-- Read back through the client's own file layer, as any of its files would be.
local text = wxl.storage.Read(NOTE)
print("read back: " .. tostring(text))
```

The bytes are copied: the runtime holds them and answers every thread itself, which is why a Lua
mod can serve files the client opens off the main thread. The priority works as the search
path's: above 0 the name wins over the client's own data, 0 or below it only fills a name the
client lacks. Serving the name again replaces your bytes; `wxl.storage.Unserve(name)` takes them
back, and they go when your mod is disabled, reloads or unloads. When two mods serve one name, the
higher priority wins, then the later one.

Two more ways to stand among the client's files are for C++ mods only: a **provider**, an object
the client asks for files (a whole archive format, a download cache), and a **transform**, which
converts a kind of file's bytes between the read and the client's parse. Both are called on the
client's loading threads, where Lua cannot run; the C++ guide (`building-a-mod.md`, "Serving and
converting assets") shows them. Their effects reach your Lua mod anyway: a file a provider has
reads through `wxl.storage` like any other, and one a provider announced but has not fetched yet
answers `nil, "pending"`: try again later.

What the engine does with what it read comes as `AssetScript` events, on the main thread:

**`examples/lua-assets/init.lua`**

```lua
-- The tile's bytes arrive as one string; # gives their count.
function AssetScript:OnTerrainTileLoad(path, tile_x, tile_y, data)
    count("terrain tile", string.format("%s (%d, %d), %d bytes", path, tile_x, tile_y, #data))
end

function AssetScript:OnTextureDecode(path, width, height, encoding, alpha_bits)
    count("texture", string.format("%s, %dx%d, %s, %d-bit alpha", path, width, height, tostring(encoding), alpha_bits))
end

function AssetScript:OnSoundFileLoad(path, streaming)
    count("sound", path .. (streaming and ", streamed" or ""))
end
```

| Event | Parameters | Happens when |
|---|---|---|
| `OnTerrainTileLoad` | `path`, `tile_x`, `tile_y`, `data` | a map tile's `.adt` was read, before its chunks are walked |
| `OnMapObjectGroupLoad` | `path`, `group`, `data` | a building's group file was read: `path` is the building's root file |
| `OnSkinProfileLoad` | `model`, `data` | a model's `.skin` was read, before it is parsed |
| `OnTextureDecode` | `path`, `width`, `height`, `encoding`, `alpha_bits`, `alpha_kind`, `mip_levels` | a texture file is about to be decoded: what its header says (`encoding`: `"palette"`, `"dxt"` or `"raw"`) |
| `OnShaderLoad` | `stage`, `path`, `name`, `permutations` | the graphics device loads a shader program (`stage`: `"vertex"` or `"pixel"`) |
| `OnSoundFileLoad` | `path`, `streaming`, `priority` | the sound engine opens a sound file to play it (never while the game's sound is off) |

`data` is the file's bytes as one string, valid in the handler; `#data` is their count. They are
the bytes the client is about to parse, transforms applied. These events came with `0.1.19`: a mod
using them, or `Serve`, asks for `"abi": "0.1.19"`.

### Assets on demand: missing names, hashes, paths of your own, overrides, reloads

A mod that fetches assets while the game runs -- from a server, a cache, another mod -- needs five
things from the platform, and each is a small call.

**Knowing what the client did not find.** `AssetScript`'s `OnFileMissing` comes whenever the
client looks for a file and no layer has it: not its archives, not its folder, no search folder,
no provider, nothing served.

```lua
function AssetScript:OnFileMissing(path, kind, open, pending, loader)
    -- kind: "model", "skin", "texture", "terrain", "sound", "interface", ... (by the extension)
    -- open: the client wanted to read it; false: it only asked whether it exists
    -- pending: a provider said "not yet" for it
    print("missing " .. path .. " (" .. kind .. ")")
end
```

It may come on one of the client's loading threads; a Lua mod gets it at the next update, as
every event raised elsewhere. Your own `wxl.storage` calls never raise it. The client probes many
optional files (a texture's other format, an item's texture for each sex), so expect names it can
live without, and the same name more than once.

**Knowing what a file holds.** `wxl.storage.HashFile(name)` answers the MD5 of a file as the client
would read it, from whichever layer holds it, with its length:

```lua
local hash = wxl.storage.HashFile("Interface\\FrameXML\\FrameXML.toc")
print(hash.md5, hash.size, hash.layer, hash.source) -- 32 hex digits, bytes, "archive", "Data\\...MPQ"
```

`layer` is `"archive"`, `"disk"`, `"folder"`, `"provider"` or `"served"`; `source` names the
archive, the file on the disk or the mod that serves it. The MD5 is the one a content
distribution network keys a file by, so a mod can tell whether its copy differs from the client's.
The answer is kept, so asking twice is cheap; `HashFile(name, true)` reads the file again. Reading
a large file takes as long as reading it: `wxl.storage.HashFileAsync(name, fn)` hashes on a thread
of the runtime's and calls `fn(hash)` (or `fn(nil, reason)`) at a later update.

**A path of your own.** Many paths are the same in this client and in later ones, with other
contents. A file you bring in goes under the path you choose: a new path lives beside the client's
files, and only taking the same path as one of the client's replaces it -- which you do on purpose,
below.

```lua
local mine = "Creature\\Murloc\\MurlocModern.m2" -- a new path: beside the client's Murloc.m2
wxl.storage.Serve(mine, bytes, 0)
```

Hand that path to the client -- a model you make, a texture you set -- and it reads your copy.
The client keeps its models by file name alone, without the folder, so give a model of your own a
file name of its own.

**Replacing a file, when you mean it.** `wxl.storage.OverrideFile(name, other)` makes every open of
`name`, and every check that it exists, get `other` instead, on every thread, above every layer:

```lua
wxl.storage.OverrideFile("Creature\\Murloc\\Murloc.m2", mine)
-- ...
wxl.storage.RestoreFile("Creature\\Murloc\\Murloc.m2")
```

It is logged, it lasts until `RestoreFile` or until your mod is disabled, reloads or unloads, and
the latest of two mods' wins. One file at a time: that is the only way to replace a file of the
client with one you serve at priority 0.

**Making the client read again.** What the client loaded stays loaded. `wxl.storage.ReloadAsset(name)`
makes it read a model or a texture again:

```lua
local done = wxl.storage.ReloadAsset("Creature\\Murloc\\Murloc.m2")
print(done.textures, done.models, done.objects, done.busy)
```

A texture (`.blp`) is read again in place: everything drawing it shows the new pixels. A model
(`.m2`, `.mdx`, or one of its `.skin` files) is dropped from the client's model cache, so its next
use reads the file again, and the units and players in view drawn with it are made again at once.
Other kinds answer `nil, "unsupported"`: map tiles and buildings are read again when their area
loads again, sounds when they play. Two things are not painted again: a character's skin, baked
from the textures of its parts (it is, at its next change of appearance), and a texture of the
client's atlas -- a square interface texture of 64 pixels or less, an icon, most buttons --, which
answers `nil, "unsupported"`. `nil, "pending"` means the texture is still loading: ask again in a
moment.

Put together, a fetch on demand reads: `OnFileMissing` (or a request from another mod) says what is
needed; the file is fetched and served under the path you chose; `HashFile` compares it with the client's when
both exist; `OverrideFile` puts it in place if it should replace the client's; `ReloadAsset` shows
it. All of this came with abi `0.1.23`.

### Services between mods: `wxl.relay`

A mod can offer a service to the others, and answer when it is ready rather than at once.

```lua
-- The mod that serves.
wxl.relay.Publish("my.fetcher.fetch", function(request, method, path)
    if method == "has" then return wxl.storage.Exists(path) end -- answered at once
    pending[#pending + 1] = request                             -- answered later
end)
-- ... later, maybe many updates later:
wxl.relay.Answer(request, localPath, md5)   -- or wxl.relay.Fail(request, "no such file")
```

```lua
-- The mod that asks.
wxl.relay.Call("my.fetcher.fetch", "get", { "Creature\\Murloc\\Murloc.m2" }, function(ok, name, md5)
    if not ok then print("not fetched: " .. tostring(name)) return end
    print("ready as " .. name)
end)
```

Everything is handed over at an update, on the main thread, never inside the call that caused it:
a request reaches the service at the next update, an answer reaches the caller at the update after
it is given. Values are numbers, strings, booleans and `nil`. A C++ mod publishes and calls the same
services. Either side may go with requests in flight: a service that goes fails what it had not
answered (`false, "not_found"`), a caller that goes has its requests cancelled -- the service's
optional `cancelled(request)` function is told -- and nothing is handed to a mod that went.
`wxl.relay.Cancel(request)` takes one back by hand. The module came with abi `0.1.23`.

### Models: `wxl.m2`

What a character wears comes as an event, and a model of your own can be hung on any model the
client draws -- a character, a creature, a mount. This part of the platform is the work of
Tyrallis, who contributed it for his equipment and model mods.

`wxl.m2` hands your mod the client's own models, which nothing checks for you, so your `mod.json`
names it, in `"requires"` or `"optional"`:

```json
"requires": [ { "service": "wxl.m2", "version": 1 } ]
```

A mod that uses it without that line gets an error saying which line to add.

**What characters wear.** `ObjectScript:OnAppearanceChanged` comes when a character's appearance
puts an item display on a slot (`change` is `"worn"`), is about to take one off (`"removed"`), or
goes away (`"freed"`). An appearance is what the client dresses: every player, every NPC dressed
from its display, and the copies the interface makes of one (the character panel, portraits,
character select). `model` is its body, what items hang on, as a light userdata: an address your
mod hands back to `wxl.m2` and never reads itself.

```lua
function ObjectScript:OnAppearanceChanged(change, wearer, model, race, sex, slot, display, npc)
    if change == "worn" then
        print("slot " .. slot .. " puts on display " .. display)
    end
end
```

`slot` counts from 0: 0 the head, 1 the shoulders, 3 the chest, 4 the belt, 10 the cloak.

**A model of your own.** `wxl.m2.CreateModel(model, path)` makes a model of a file in the scene of
the model you name, and answers a `wxl.Model`, your mod's. Its methods hang it and change it:

```lua
local hat

function WorldScript:OnUpdate(delta)
    if hat and hat:Parent() then return end           -- still on the head
    local body = wxl.m2.ObjectModel(wxl.object.LocalPlayer())
    if not body then return end                        -- not in the world yet
    hat = hat or wxl.m2.CreateModel(body, "Creature\\Murloc\\Murloc.m2")
    print("hung: " .. tostring(hat:Attach(body, 11, 0)))  -- 11: the top of the head
end
```

`wxl.m2.ObjectModel(object)` answers the model an object draws with (a mounted player answers its
mount). `model:Attach(parent, point, flags)` hangs yours on another model at one of its attachment
points: 11 is the top of a character's head, 1 and 2 its hands, 32 its hip. `flags` is 0, or 1 to
hang it at the parent's origin where it has no such point, or 2 to have its bones follow the
parent's (a piece made for a body's skeleton), or 3 for both. The client lets your model go when
the model it hangs on goes -- the player changes shape or mounts -- and `model:Parent()` then
answers `nil, "not_found"`: hang it again, as above.

| Method | What it does |
|---|---|
| `model:Attach(parent, point, flags)` | hangs it on `parent`; `nil, "not_found"` for a point the parent lacks (flags 1 hangs it anyway) |
| `model:AttachToBone(parent, point, key_bone, { x, y, z })` | hangs it on one key bone of `parent`, moved by an offset: a point the parent's file does not have |
| `model:Detach()`, `model:Parent()` | takes it off; what it hangs on |
| `model:SetGeosets(first, last, visible)` | shows or hides its geosets numbered `first` to `last` |
| `model:ReplaceTexture(type, path)` | gives it a texture of its own (2 an item's skin, 11-13 a creature's) |
| `model:Release()` | takes it off and gives it back; your mod's models go when it unloads too |

`wxl.m2.AttachmentPoint(model, point)` answers where a point of a loaded model sits, as
`{ x, y, z }`; `wxl.m2.RacePrefix(race)` the race's prefix in model file names (`"Hu"`, `"Or"`).

The model loader's stages (`AssetScript:OnModelParsed`, `OnSkinProfileParsed`,
`OnModelSequenceLoad`) and the drawn batches (`RenderScript:OnModelBatchDrawn`) come to Lua too,
with their numbers, but reshaping a model file or drawing with the device is a C++ mod's work:
the C++ guide (`building-a-mod.md`, "Models") shows it. `OnModelBatchDrawn` comes thousands of
times a frame: listen to it only while you need it. All of this came with abi `0.1.20`.

**A model in the world.** `wxl.m2.CreateWorldModel(path)` makes a `wxl.Model` in the world's own
scene, the one units and spell effects are in. It shows nowhere until you place it:
`model:Place(placement)` puts it at a point of the world, and moves it there again each time you
call it. A placement is a table: `position`, a point, and if you want them `yaw`, `pitch` and
`roll` in radians (yaw as a unit's facing) and `scale` (1 when left out).

```lua
local fire

function ObjectScript:OnSpellCastGo(caster, caster_guid, target, target_guid, cast_item, spell_id,
                                    cast_count, cast_flags, target_flags, cast_time, hit_count,
                                    hits, misses, miss_reasons, has_source, source, has_dest, dest)
    if not has_dest then return end                  -- no ground point: not a ground spell
    local ground = wxl.m2.GroundHeight(dest, 5, 20) or dest.z
    if fire then fire:Release() end
    fire = wxl.m2.CreateWorldModel("Spells\\FireNova_Area.m2")
    fire:Place({ position = { x = dest.x, y = dest.y, z = ground } })
    fire:PlayAnimation(158, 1)                       -- its birth, then what the file chains
end
```

`has_dest` and `dest` say where a ground spell lands, `has_source` and `source` where it starts,
in world coordinates unless the `dest_transport` (or `source_transport`) after them names the ship
or elevator they were measured on.

| Function or method | What it does |
|---|---|
| `wxl.m2.CreateWorldModel(path)` | a `wxl.Model` of your mod's in the world's scene; it shows once placed |
| `model:Place({ position = {x, y, z}, yaw, pitch, roll, scale })` | puts it in the world there, or moves it there |
| `model:SetTransform(placement)` | how it sits on the point it hangs on, for one you `Attach` |
| `model:PlayAnimation(id, speed)` | plays an animation (`AnimationData.dbc` id) from its start, once loaded |
| `model:SetAlpha(alpha)`, `model:SetEmitting(on)` | a fade, 0 to 1; whether its particles start new ones |
| `wxl.m2.ModelLoaded(model)`, `wxl.m2.AnimationLength(model, id)` | whether a model has loaded; how long one play of an animation lasts, in milliseconds |
| `wxl.m2.AttachmentWorld(model, point)` | where a point of a loaded model is in the world now, as `{ x, y, z }` |
| `wxl.m2.GroundHeight(at, up, down)` | the height of the ground under a point, looked for from `up` yards above to `down` below |

Leaving the world (a logout, a teleport to another map) takes your placed models out of it, with
the client's own spell effects: place them again in `WorldScript:OnWorldEnter`. The bones the
client poses every frame can be watched and moved too (a cape, a chain), but only by a C++ mod:
it happens on the client's animate threads, where no script runs. These came with abi `0.1.21`.

### The client's databases: `wxl.data`

The client keeps its game data -- spells, items, maps -- in databases, files ending in `.dbc`,
which it reads once, early in its start, and never again. `wxl.data.ServeTable(table, path)`
makes it read one from your file instead, provided you ask before that moment: at the top of
`init.lua`, which runs as the client starts. The file must have the columns of the client's own;
adding rows is what this is for.

```lua
-- an illustration: Spell.dbc read from assets\DBFilesClient\Spell.dbc of your search path
wxl.data.ServeTable("Spell.dbc", "DBFilesClient\\Spell.dbc")
```

The path is a client name, so a database in your search path works. `IsLoaded(table)` tells you
whether the client has read it already, in which case serving it comes too late this session:

**`examples/lua-services/init.lua`**

```lua
    print("Spell.dbc read already: " .. tostring(wxl.data.IsLoaded("Spell.dbc")))
```

`ServedFrom(table)` says which file a table is served from, `""` for the client's own, and
`RestoreTable(table)` gives it back. One mod serves a table at a time: a second one is refused
with `"failed"`, and the log names the first.

### The client's own Lua: `wxl.script`

The client runs a Lua of its own, separate from your mod's: the one its interface, its addons and
the `/run` command use. Your mod can add a function to it:

**`examples/lua-services/init.lua`**

```lua
wxl.script.RegisterFunction("LuaServicesGreet", function(name)
    return "hello " .. tostring(name) .. ", from " .. wxl.mod.name
end)
```

Type `/run print(LuaServicesGreet("you"))` in the chat box and the client calls your function,
in your mod's state, with what its caller passed, and takes back what you return. Both sides
are numbers, strings, booleans and `nil`; a table does not cross. An error in your function
shows in the client as its error, and counts in your log as any error of your mod does.

The client throws its Lua away and starts a new one more than once: at the login screen, when
you enter the world, when the interface reloads. `wxl.lua` registers your function again in each
one, so you call `RegisterFunction` once, at the top of `init.lua`. A third argument,
`"glue"` (the login and character screens) or `"game"`, keeps it to one of them.

`RegisterEvent` adds an event the client's frames can listen to, kept the same way, and
`FireEvent` raises it with values:

**`examples/lua-services/init.lua`**

```lua
wxl.script.RegisterEvent("LUA_SERVICES_WORLD")

function WorldScript:OnWorldEnter(map_id)
    wxl.script.FireEvent("LUA_SERVICES_WORLD", map_id)
    -- Script text, run in the client's context: it writes a line in the chat frame.
    wxl.script.Run("DEFAULT_CHAT_FRAME:AddMessage('lua-services: entered map " .. map_id .. "')",
                   "lua-services")
end
```

An addon's frame that called `frame:RegisterEvent("LUA_SERVICES_WORLD")` now receives it each
time you enter the world. `Run(text, name)` runs Lua text in the client's state, as `/run` would;
`name` is what its errors are labelled with. `wxl.script.Context()` says which state is live,
`"glue"` or `"game"`, and `wxl.script.Live()` whether one is: the client has none while it starts
and for a moment between two.

### Hook points: `wxl.hook`

A runtime can name points of the client a native mod may detour. Lua lists them; attaching one is
for C++, since a detour is native code. Only the runtime for the original client has them: the
recompiled client calls the platform from its own code and detours nothing, so there `wxl.hook` is
absent and `Points()` answers `nil, "unsupported"`. Check the answer before you use it:

**`examples/lua-services/init.lua`**

```lua
    local points, why = wxl.hook.Points()
    if points then
        print(#points .. " hook point(s) named")
    else
        print("no hook points on this runtime: " .. why)
    end
```

Each point is a table with its `name` and `signature`; `HasPoint(name)` asks for one.

### Writing into the client: `wxl.patch`

`wxl.patch` writes bytes into the client's program: a jump turned around, a constant changed.
It exists on the stock `Wow.exe` only, where every address is known, and it is a sharp tool, so
a mod reaches it only when its `mod.json` declares it:

```json
"requires": [ { "service": "wxl.patch", "version": 1 } ]
```

(`"optional"` works too, for a mod that does without on the other client.) Without it, every use
of `wxl.patch` stops with an error saying what to add. Each write names the bytes it expects to
find, and is refused when they are not there, so a wrong address becomes a log line rather than
a broken client:

```lua
-- an illustration: two NOPs at an address that holds two INT3 now
local patch, why = wxl.patch.Apply("my change", 0x00401000, "\204\204", "\144\144")
```

Bytes are written as a Lua string, `"\204"` being the byte 204 (0xCC). `Apply` answers a `Patch`,
or `nil` and why; `patch:Restore()` puts the old bytes back. Your mod's patches are put back,
newest first, when it reloads or stops, so a reload starts from the client as it was. `Matches`
checks bytes without writing, `Find(bytes, after)` searches the program's constant data for them.

### The camera, the mouse and the world: `wxl.camera`, `wxl.input`, `wxl.world`

Three modules (abi `0.1.24`) let a mod take the camera and steer it with the mouse. The client
places its world camera once a frame, and `RenderScript`'s `OnCameraUpdate` comes right after,
before anything is drawn: a view set there is the one the frame is drawn from.

```lua
local yaw, pitch = 0, -0.3

Observe("WorldScript", "OnMouseMotion", function(dx, dy)
    yaw, pitch = yaw - dx * 0.004, pitch - dy * 0.004
end)

Observe("RenderScript", "OnCameraUpdate", function(index, delta)
    local head = wxl.camera.Pivot()                 -- the character, at the height it looks from
    if not head or wxl.camera.Cinematic() then return end
    local forward = { x = math.cos(pitch) * math.cos(yaw), y = math.cos(pitch) * math.sin(yaw), z = math.sin(pitch) }
    local eye = { x = head.x - forward.x * 4, y = head.y - forward.y * 4, z = head.z - forward.z * 4 }
    eye = wxl.world.Trace(head, eye) or eye         -- in front of a wall, never through it
    wxl.camera.SetView(eye, forward, 0)             -- 0 keeps the client's field of view
end)

wxl.input.CaptureCursor(true)                       -- hidden, held at the centre of the window
```

| Function | Answers |
|---|---|
| `wxl.camera.Position()`, `.Forward()`, `.Up()` | where the camera is this frame, as `{ x, y, z }`; where it looks; the top of the screen |
| `wxl.camera.Fov()`, `.Pivot()`, `.Cinematic()` | its field of view in radians; the point the client's camera turns around (the character at its eye height); whether a cinematic has the view |
| `wxl.camera.SetView(position, forward, fov)` | `true`: the camera is placed there from now on, until `ReleaseView()` or the mod goes; `fov` 0 keeps the client's. Of two mods' views the later is drawn, and the earlier comes back when it lets go |
| `wxl.camera.ReleaseView()` | `true`: the client has its camera again, as the player left it |
| `wxl.input.CaptureCursor(captured)` | `true`: the pointer is hidden and held at the centre (`true`), or the mod's hold is let go (`false`). The game still picks and clicks at the centre: what is there is the mouseover, a left click targets it, a right click talks to it |
| `wxl.input.CursorCaptured()`, `.PointerOverWorld()` | whether some mod holds the pointer; whether it is over the world rather than the interface |
| `wxl.world.Trace(start, finish, flags)` | the first point of the ground, a building or a placed object between the two, or `nil` when the way is clear; `flags` (left out: the camera's) is the client's hit-test mask, `0x100111` the line of sight of spells |

The mouse's motion arrives as `WorldScript`'s `OnMouseMotion(dx, dy)`, in the mouse's own counts
(right and towards you positive), whether the pointer is held or not; the wheel as
`OnMouseWheel(delta)`, in notches (away from you positive), which an `Override` answering `true`
keeps from the game. The pointer is let go while the game's window is not in front. A mod's view
and its hold on the pointer go when it reloads, is disabled or unloads. Whether a unit stands
indoors is `unit:IsOutdoors()`. Everything here runs on the game's thread, where your handlers run.

`wxl.input.BlockMouseLook(true)` (abi `0.1.25`) keeps the game out of its own mouse look: a held
mouse button turns no camera and no character, hides no pointer, and a click let go within 0.8 s
still selects (left) or talks, loots, attacks (right). `wxl.pose.Head(unit, yaw, pitch, smoothing)`
turns a unit's head from where its body faces (radians: yaw positive to its left, pitch up;
`smoothing` the seconds to cover half the way), held until `wxl.pose.ReleaseHead(unit)`; the
runtime holds the angles to 100 degrees of yaw and 80 of pitch. Both are let go when the mod
reloads, is disabled or unloads. Dear ImGui windows (`wxl.imgui`, `OnImGuiFrame`) are a C++ mod's:
see "Windows of your own" in [Building a mod](building-a-mod.md).

### Where the memory goes: `wxl.footprint`

The original client is a 32-bit program: it has 4 GB of addresses, and it fails when it cannot
find one free block large enough for what it asks, often long before the total runs out.
`wxl.footprint` measures where that memory goes, so that you can see what your mod, an addon or a
zone costs. The example of this section is
[`examples/memory-report/`](../examples/memory-report/): copy it into `Mods/`, play, and read the
report it writes into its own folder.

The client asks for nearly all its memory through one allocator, Storm's, and tells it which
source file is asking. Counting that costs a little on every allocation, so it is off until a mod
starts it. The example starts it as soon as it loads, before the client loads the world, so that
everything after is counted:

**`examples/memory-report/init.lua`**

```lua
local started, why = footprint.Start()
if not started then
    print("Storm's allocations are not counted: " .. why)
end
footprint.WatchPressure(THRESHOLD)
```

Only what is allocated after `Start` is followed: a block allocated before it is not counted, and
neither is its release. `Stop` ends the counting and keeps the last numbers.

`Snapshot()` takes a picture: the counts, what the big consumers outside Storm hold (the
graphics device's textures and buffers, the sound engine, the client's own Lua, the archives, the
databases), the heap of every Lua mod, and the address space. The lists read that picture, each
answering an array of tables, or `nil` and why when the runtime cannot say:

**`examples/memory-report/init.lua`**

```lua
local function list(name)
    local rows = footprint[name]()
    return rows or {}
end
```

| List | Each entry |
|---|---|
| `Categories()` | `name`, `owner`, `bytes`, `count`, `peak`: one consumer, largest first |
| `Sources()` | `file`, `bytes`, `count`, `peak`, `allocations`: Storm's allocations of one source file, largest first |
| `Archives()` | `path`, `priority`, `hash_entries`, `block_entries`, `bytes`: one mounted archive |
| `Tables()` | `name`, `records`, `record_size`, `bytes`: one loaded client database |
| `Regions()` | `state`, `type`, `bytes`, `regions`, `largest`: one kind of the address space |

Take the picture in a handler or a callback, on the client's thread, as every Lua mod runs. The
categories overlap: the client's Lua, its databases and its archives' tables are allocated
through Storm too, so they are also inside `storm`. The number to watch on the original client is
the `largest` of the `free` region: an allocation larger than it fails.

`Report(name, bytes, count)` adds a consumer of your own to the picture, shown as
`<your mod id>/<name>`; a report of 0 and 0 takes it out. `WatchPressure(bytes)` asks for
`OnMemoryPressure` when the largest free block falls under that many bytes:

**`examples/memory-report/init.lua`**

```lua
function WorldScript:OnMemoryPressure(largest_free, free, committed, threshold)
    print(string.format("address space low: the largest free block is %s, under %s; %s committed",
                        mb(largest_free), mb(threshold), mb(committed)))
    report("memory pressure")
end
```

The runtime looks about once a second, raises the event when the block falls under the
threshold, and again only once it has risen back above. `OnArchiveMounted` and
`OnArchiveUnmounted` (`AssetScript`) tell each archive the client mounts or closes, with the size
of its tables. These came with abi `0.1.11`, so the example's `mod.json` asks for it, and it
requires `wxl.footprint`, without which it has nothing to report.

The example writes into its own folder with `io.open`, which finds files there. A mod reloads only
when a file of its code changes, so its reports do not reload it.

---

## 🔁 7. Your mod's life

### Loading

When the client starts, the runtime loads `wxl.lua`, which reads `Mods/` and for each folder:

1. takes its `mod.json` if it has one and names a `lua` entry, or, with no manifest at all, an
   `init.lua`, naming the mod after the folder: lowercase, letters and digits, runs of anything
   else as one `-` (`My Mod_One` is `my-mod-one`);
2. refuses it, with the reason in the log, if its `abi` cannot be served or a service it
   `requires` is missing (a `mod.json` that names both `binaries` and `lua` is refused by the
   runtime before `wxl.lua` sees it);
3. orders the mods by `loadAfter` and folder name;
4. for each, creates a Lua state, runs the entry file, and subscribes its family methods.

The runtime's own log says, for every Lua mod, that it was skipped because `wxl.lua` serves it.
That is expected: the runtime loads C++ mods, `wxl.lua` the Lua ones.

```
[host] skipped wxl.lua-hello: script mod (lua: init.lua): wxl.lua serves it
[wxl.lua] loaded wxl.lua-hello 1.0.0 (Lua hello) from init.lua
```

The `abi` a Lua mod is checked against is the lower of the runtime's and the one `wxl.lua` was
built with: an event newer than your `wxl.lua` is not available, whatever the runtime offers.
`wxl.runtime.abi` says what your mod is served.

The `abi` your manifest asks for is also what your mod may use. Subscribing to an event that came
with a later feature is an error at the call, which names the event, the version that brought it,
and what to write instead:

```
Observe: OnChatMessage of ChatScript came with abi 0.1.6, and this mod's mod.json asks for 0.1.5; to use it, set "abi": "0.1.6" there
```

Asking for `0.1.6` then makes a `0.1.5` runtime refuse the mod at the start, saying why, rather
than run it without the event. A folder without a manifest may use everything its runtime serves.

The service modules follow the same rule (abi `0.1.14`). A module that came with a later feature
than your manifest's `abi`, such as `wxl.profile` (`0.1.12`), raises an error the first time your
code reaches into it, and so does a function added to a module after the module came, such as
`wxl.storage.RemoveSearchPath` (`0.1.10`) or `wxl.storage.Redirect` (`0.1.15`):

```
wxl.my-mod/init.lua:3: wxl.profile came with abi 0.1.12, and this mod's mod.json asks for 0.1.11; to use it, set "abi": "0.1.12" there
```

The modules `wxl.storage`, `wxl.data`, `wxl.script`, `wxl.hook` and `wxl.patch` were there from
`0.1.5` on: any `abi` reaches them.

### When each part runs

| What | When |
|---|---|
| the entry file | once, when `wxl.lua` loads, before the client has finished starting; again at each reload |
| family methods, `Observe`, `Override` handlers | every time their event happens, from the moment they are subscribed |
| `onInit` | once the client is fully up (`WXL_STAGE_READY`), and right after each reload |
| `onUpdate(delta)` | every game tick, after the `OnUpdate` handlers; `delta` is the time since the previous one, in seconds |
| `onDraw` | every frame, once the interface is drawn |
| `onShutdown` | when the client closes, and before each reload, in the state that is going away |

### Reloading

Once a second, `wxl.lua` looks at the files each Lua mod's code comes from: its `mod.json`, its
entry file, and every file it loaded with `require`, `dofile` or `loadfile`, including a module
`require` looked for and did not find. When one of them has changed, the mod reloads: the log
names the file, `onShutdown` runs, its handlers are removed, its Lua state is closed, then the
entry file runs in a new state and `onInit` runs. Other mods are not touched.

```
[wxl.lua] mod wxl.lua-hello: lib/tally.lua changed
[wxl.lua] reloading wxl.lua-hello
```

A changed `mod.json` is read again and checked as at the start. One that no longer reads, gives
another `id`, or asks for an `abi` this runtime cannot serve leaves the mod unloaded, with the
reason in the log, until you save a fix.

Nothing in Lua memory survives a reload, the fields you stored on objects included: in the new
state, an object still in view is a new value with none of them. A file the mod writes into its own
folder does (see the sandbox below), and writing one never reloads the mod, nor does changing a file
it reads with `io.open`: those are data, not code. Save settings whenever it suits you.

### Errors

An error in a handler or a callback is written to the log with the mod's id, what it was doing,
and a traceback (the chain of calls that led to it). The other handlers and the other mods carry
on:

```
[wxl.lua] mod hello errored in its Observe handler for OnWorldEnter of WorldScript (1/25): hello/init.lua:4: attempt to index local 'position' (a nil value)
stack traceback: ...
```

The count is there because a mod that keeps failing is **disabled** at the 25th error since it
was loaded: its handlers are removed and its callbacks stop. The log says so, and it stays off
until one of its files changes, which reloads it with the count back at zero.

An error while the entry file runs, a syntax error included, means the mod is not loaded. Fix the
file and save: the change reloads it.

`print` writes a line to the log at the `info` level, tagged with the mod's id, its arguments
separated by tabs. `wxl.log(level, ...)` picks the level: `"trace"`, `"debug"`, `"info"`,
`"warn"` or `"error"`. Which levels reach the file is set in `WarcraftXL.ini`, for every mod or
for yours alone; [Reading the log](the-log.md) shows how, and how to mark the steps of a test
with `/mark`.

### The sandbox

A mod runs with the standard Lua libraries, without what would let it reach outside the game or
outside its folder:

- **removed:** `os.execute`, `os.exit`, `os.getenv`, `os.remove`, `os.rename`, `os.setlocale`,
  `os.tmpname`, `io.popen`, `io.tmpfile`, `io.input`, `io.output`, `io.read`, `io.stdin`,
  `io.stdout`, `io.stderr`, `package.loadlib`, the `ffi` module, `jit.util`, `jit.profile`, and
  every function of `debug` but `debug.traceback` and `debug.getinfo`;
- **limited to the mod's folder:** `io.open`, `io.lines`, `loadfile`, `dofile` and `require` take
  paths relative to the folder, and refuse one that is absolute or contains `..`;
- **text only:** `load`, `loadstring` and every file loaded refuse precompiled Lua bytecode;
- **redirected:** `io.write` writes to the log, one line per call.

`require("a.b")` looks for `a/b.lua`, then `a/b/init.lua`, in the mod's folder. `package.preload`
works as usual. `os.time`, `os.clock`, `os.date`, `string`, `table`, `math` and `bit` are all
there.

---

## ✏️ 8. Your editor

[`lua/types/wxl.d.lua`](../lua/types/wxl.d.lua) describes the whole API for
[lua-language-server](https://github.com/LuaLS/lua-language-server), which the *Lua* extension
of VS Code and most other editors use: every family's events with their parameters named and
typed, what an `Override` returns, `Observe` and `Override` with the families and events to
complete, every object method and the kinds it applies to, and the `wxl` table. Pointed at it,
the editor completes `ObjectScript:On...`, shows a handler's parameters as you type them, and
underlines a misspelt event before you save.

In VS Code, add to the workspace's `.vscode/settings.json`:

```json
{
  "Lua.runtime.version": "LuaJIT",
  "Lua.workspace.library": [ "path/to/sdk/lua/types" ]
}
```

A copy of `wxl.d.lua` anywhere in the folder the editor has open works too. The file is
generated from the same tables `wxl.lua` is built from; take it from the SDK your `wxl.lua` was
built with, so it describes the events your `wxl.lua` knows.

---

## 📚 9. Reference

### The manifest

`mod.json`, beside the entry file. The schema is [`schema/mod.json`](../schema/mod.json).

| Field | Required | Meaning |
|---|---|---|
| `id` | yes | the mod's identity: lowercase letters and digits, separated by single `.` or `-`, 3 to 64 characters. It tags its log lines and is what `loadAfter` names |
| `name` | yes | the name shown in the log |
| `version` | yes | the mod's own version, `1.2.3` |
| `abi` | yes | the lowest contract version the mod needs, `major.minor.feature`, such as `"0.1.5"`: the feature that brought the newest event it uses. A runtime of another major, or another minor while the major is 0, refuses it; so does one whose feature is lower. The mod may subscribe only to the events this version has. A two-part version (`"0.5"`) is refused as the old numbering |
| `lua` | yes, for a Lua mod | the entry file, relative to the folder; it may not leave it. A Lua mod names no `binaries`: a manifest with both is refused |
| `requires` | no | services the mod cannot run without, `{ "service": "wxl.object", "version": 1 }`. A runtime without one refuses the mod |
| `optional` | no | services the mod uses when they are there; informational |
| `loadAfter` | no | ids of mods to load before this one when they are present; a missing one is ignored |
| `description`, `authors`, `homepage` | no | for people reading the folder |

A folder with an `init.lua` and no `mod.json` is a mod named after the folder, version `0.0.0`,
served the current contract, requiring nothing.

### Globals

#### `registerForEvent(name, fn)`

Adds `fn` to one of the mod's callbacks. `name` is `"onInit"`, `"onUpdate"` (`fn` receives
`delta`, in seconds), `"onDraw"` or `"onShutdown"`; [When each part runs](#when-each-part-runs)
says when. Several functions may share a name; they run in the order they were added. Another
name is an error listing these four.

#### `Observe(family, event, fn)`

Subscribes `fn` to an event and returns its id, a number. `family` is a family's name as a string
(`"WorldScript"`), `event` the event's (`"OnUpdate"`). `fn` receives the event's parameters. A
handler subscribed while the event is being delivered runs from the next time. An error for an
event newer than the manifest's `abi`.

#### `Override(family, event, fn)`

As `Observe`, and `fn`'s return values answer the event ([Answers](#answers)). An error on an
event that cannot be answered.

#### `Unobserve(id)`

Removes the handler with that id. Answers `true`, or `false` when the mod has no handler with that
id (it was already removed). Safe from inside the handler itself.

#### The family tables

`WorldScript`, `RenderScript`, `ObjectScript`, `AssetScript`, `NetScript`, `LuaScript`,
`CombatScript`, `PlayerScript`, `GlueScript`, `ChatScript`, `LootScript`, `ItemScript`,
`QuestScript`, `GroupScript`, `SocialScript`. A function defined on one under an event's name is subscribed after the entry file
has run, as an `Override` on an event that can be answered and as an `Observe` otherwise, and is
called with `self` first. Anything else on these tables, a method for an event newer than the
manifest's `abi`, or replacing one with something that is not a table, stops the mod from
loading.

#### `print(...)`

Writes its arguments to the log, through `tostring`, separated by tabs, at the `info` level.

### The `wxl` table

| Field | What it holds |
|---|---|
| `wxl.mod.id`, `.name`, `.version` | this mod, from its manifest or its folder |
| `wxl.mod.directory` | the full path of the mod's folder, for messages (file functions take paths relative to the folder) |
| `wxl.runtime.id`, `.name`, `.version` | the runtime that loaded `wxl.lua` |
| `wxl.runtime.abi` | the contract this mod is served, as text, such as `"0.1.6"` |
| `wxl.runtime.abi_major`, `.abi_minor`, `.abi_feature` | the same, as numbers |
| `wxl.runtime.Raises(family, event)` | whether the runtime raises that event at all: `wxl.runtime.Raises("WorldScript", "OnSoundPlay")`; a handler for one it does not raise never runs (abi `0.1.13`) |
| `wxl.object.LocalPlayer()` | the player you control, or `nil` |
| `wxl.object.FromGuid(guid)` | the object with that guid (`"0x"` and up to sixteen hex digits, or a whole number), or `nil` when it is not in view |
| `wxl.object.InView(type)` | a list of the objects of a type in view; every object without a type |
| `wxl.object.Fields(list, name, element)` | one descriptor field of every object of a list, in order, `false` where an object has none (abi `0.1.9`) |
| `wxl.object.Watch(type, name)` | asks for `OnObjectFieldChanged` when the field changes on objects of the type, `nil` for every object, for this mod's handlers; `true`, or `nil` and why (abi `0.1.9`) |
| `wxl.object.Unwatch(type, name)` | gives that watch back; `true`, also for a field the mod did not watch (abi `0.1.10`) |
| `wxl.field` | every descriptor field's number, by kind: `wxl.field.Unit.Health` |
| `wxl.events` | each family's events by name, `wxl.events.NetScript[1] == "OnMessageReceived"` |
| `wxl.log(level, ...)` | a log line at `"trace"`, `"debug"`, `"info"`, `"warn"` or `"error"` |

### Service modules

Each answers its value, or `nil` and the reason. Every argument must be given, except `after` of
`wxl.patch.Find`, which is 0 when left out; one left out is an error naming the function and the
argument. Every function, typed, is in [`wxl.d.lua`](../lua/types/wxl.d.lua), and every row behind
them in `include/wxl/services/<service>.def`.

| Function | Answers |
|---|---|
| `wxl.storage.Exists(name)`, `.Size(name)` | whether a file is there; its length in bytes |
| `wxl.storage.Read(name)` | the whole file, as a string |
| `wxl.storage.Open(name)` | a `File`: `file:ReadAt(offset, count)` reads, `file:Close()` gives it back |
| `wxl.storage.Find(pattern)` | a list of the names matching, `*` any run of characters |
| `wxl.storage.AddSearchPath(folder, priority)` | `true`: the folder's files are the client's, above 0 over its own, until the mod removes it or goes |
| `wxl.storage.RemoveSearchPath(folder)` | `true`: a folder the mod added leaves the search; `nil, "not_found"` for one it did not add (abi `0.1.10`) |
| `wxl.storage.MountArchive(path, priority)` | `true`: an archive joins the client's |
| `wxl.storage.Redirect(name, other)` | `true`: every open of `name`, on any thread, opens `other`, until the mod takes it back or goes; the latest of two mods' wins (abi `0.1.15`) |
| `wxl.storage.Unredirect(name)` | `true`: the mod's redirect of `name` is taken back; `nil, "not_found"` for one it did not make (abi `0.1.15`) |
| `wxl.storage.Serve(name, data, priority)` | `true`: the client reads `data` as the file `name`, on every thread, until the mod takes it back or goes; the higher priority of two mods' wins, then the later (abi `0.1.19`) |
| `wxl.storage.Unserve(name)` | `true`: what the mod serves under `name` is taken back; `nil, "not_found"` for a name it does not serve (abi `0.1.19`) |
| `wxl.storage.HashFile(name, fresh)`, `.HashFileAsync(name, fn, fresh)` | `{ md5, size, layer, source }` of a file as the client would read it; the same later, to `fn` (abi `0.1.23`) |
| `wxl.storage.OverrideFile(name, other)`, `.RestoreFile(name)` | `true`: `other` replaces the client's `name` for opens and existence checks, on every thread, until restored or the mod goes |
| `wxl.storage.ReloadAsset(name)` | `{ textures, models, objects, busy }`: a texture or a model the client holds, read again |
| `wxl.relay.Publish(name, fn, cancelled)`, `.Unpublish(name)`, `.Published(name)` | a service of the mod's, its requests to `fn(request, method, ...)`; taken back; whether one is published (abi `0.1.23`) |
| `wxl.relay.Call(name, method, args, fn)` | the request's number; `fn(true, ...)` or `fn(false, reason)` at a later update |
| `wxl.relay.Answer(request, ...)`, `.Fail(request, reason)`, `.Cancel(request)` | `true`: a request the mod's service received answered, or failed; one the mod made taken back |
| `wxl.data.ServeTable(table, path)`, `.RestoreTable(table)` | `true`: the client reads the table from `path`, or its own again |
| `wxl.data.ServedFrom(table)`, `.IsLoaded(table)` | where a table comes from, `""` for the client's own; whether the client read it already |
| `wxl.script.RegisterFunction(name, fn, context)` | `true`: `name` calls `fn` from the client's Lua, in every context or in `"glue"` or `"game"` |
| `wxl.script.RegisterEvent(name, context)`, `.FireEvent(name, ...)` | `true`: an event the client's frames can listen to; raised with values |
| `wxl.script.Run(text, name)`, `.Context()` | `true` once the text ran; `"glue"` or `"game"` |
| `wxl.script.Live()` | whether a context is live: `false` before the first and between two (abi `0.1.15`) |
| `wxl.hook.Points()`, `.HasPoint(name)` | the points as `{ name, signature }`; whether one is named |
| `wxl.patch.Apply(name, address, expected, bytes)` | a `Patch`, `patch:Restore()` to undo; the module needs `wxl.patch` in `mod.json` |
| `wxl.patch.Matches(address, expected)`, `.Find(bytes, after)` | whether the bytes are there; where they are, after `after` or, left out, from the start |
| `wxl.footprint.Start()`, `.Stop()`, `.Running()` | `true`: Storm's allocations are counted from now; counting ends; whether it runs (abi `0.1.11`) |
| `wxl.footprint.Snapshot()` | `true`: the picture the lists read, Lua mods' heaps included |
| `wxl.footprint.Categories()`, `.Sources()`, `.Archives()`, `.Tables()`, `.Regions()` | the lists of the last picture, as arrays of tables |
| `wxl.footprint.Report(name, bytes, count)` | `true`: a consumer of the mod's own joins the picture as `<id>/<name>`; 0 and 0 take it out |
| `wxl.footprint.WatchPressure(bytes)` | `true`: `OnMemoryPressure` when the largest free block falls under it; 0 stops |
| `wxl.profile.Begin(name)`, `.End()`, `.Text(text)` | a zone on the profiler's timeline inside the running handler, closed by `End` or when the handler returns; a line of text on it (abi `0.1.12`, see [Profiling](profiling.md)) |
| `wxl.profile.Plot(name, value)`, `.Message(text)`, `.FrameMark(name)`, `.Connected()` | a point on a graph; a line on the timeline; a frame of your own frame set; whether a viewer is connected |
| `wxl.action.Target(object)`, `.ClearTarget()`, `.Interact(object)` | `true`: targets an object, clears the target, right-clicks an object; `nil, "failed"` when the client refused (abi `0.1.18`) |
| `wxl.action.CastSpell(id, target)`, `.StopCasting()`, `.UseItem(item, target)` | casts a spell you know at a unit, or at the current target when `target` is left out; stops the cast in progress; uses an item, on a unit or what it picks |
| `wxl.action.Follow(unit)`, `.Assist(unit)`, `.MoveTo(point)` | follows a unit; targets what it targets; walks to a point (`{ x, y, z }`) as click-to-move does |
| `wxl.m2.ObjectModel(object)`, `.RacePrefix(race)`, `.AttachmentPoint(model, point)` | the model an object draws with; a race's prefix in file names; a point of a model as `{ x, y, z }` (abi `0.1.20`; the module needs `wxl.m2` in `mod.json`) |
| `wxl.m2.CreateModel(model, path)` | a `Model` of your mod's in that model's scene: `:Attach`, `:AttachToBone`, `:Detach`, `:Parent`, `:SetGeosets`, `:ReplaceTexture`, `:Release` (see [Models](#models-wxlm2)) |
| `wxl.m2.CreateWorldModel(path)`, `.ModelLoaded(model)`, `.AnimationLength(model, id)`, `.AttachmentWorld(model, point)`, `.GroundHeight(at, up, down)` | a `Model` in the world's scene: `:Place`, `:SetTransform`, `:PlayAnimation`, `:SetAlpha`, `:SetEmitting`; whether a model loaded; an animation's length; a point in the world; the ground under a point (abi `0.1.21`) |
| `wxl.point.Points()`, `.Find(name)` | the engine functions a C++ mod can run code before, instead of or after (name, signature, thread, flags, and `absent`: why this runtime lacks one); the id of one this runtime has (abi `0.1.16`). Attaching from Lua is to come; see "Changing the engine: points" in [Building a mod](building-a-mod.md) |
| `wxl.engine.Layout(name)` | the layout hash of an engine structure (`"CM2Shared"`) as this runtime lays it out, for a tool comparing it with the SDK's engine headers (abi `0.1.22`); `nil, "not_found"` for a type it does not describe |
| `wxl.camera.Position()`, `.Forward()`, `.Up()`, `.Fov()`, `.Pivot()`, `.Cinematic()` | where the world camera is and looks, its field of view, the point the client's camera turns around, whether a cinematic has the view (abi `0.1.24`) |
| `wxl.camera.SetView(position, forward, fov)`, `.ReleaseView()` | `true`: the camera is placed from now on, until the mod releases it or goes; see [The camera, the mouse and the world](#the-camera-the-mouse-and-the-world-wxlcamera-wxlinput-wxlworld) |
| `wxl.input.CaptureCursor(captured)`, `.CursorCaptured()`, `.PointerOverWorld()` | `true`: the pointer held at the centre, or let go; whether some mod holds it; whether it is over the world (abi `0.1.24`) |
| `wxl.world.Trace(start, finish, flags)` | the first point of the world's geometry between two points, or `nil` (abi `0.1.24`) |
| `wxl.action.Face(radians)` | `true`: your player turns that way and the server is told, as mouse look does (abi `0.1.24`) |
| `wxl.input.BlockMouseLook(blocked)`, `.MouseLookBlocked()` | `true`: the game kept out of its own mouse look, clicks still act; whether some mod keeps it out (abi `0.1.25`) |
| `wxl.pose.Head(unit, yaw, pitch, smoothing)`, `.ReleaseHead(unit)` | `true`: a unit's head turned from where its body faces and held, or given back (abi `0.1.25`) |

### Object methods

| Method | Answers |
|---|---|
| `Valid()` | whether the object is still there; `nil, "unsupported"` without the object service |
| `Guid()` | its guid, `"0x..."` |
| `Type()`, `TypeId()` | its kind, as a name or a number |
| `Name()` | its name |
| `Target()` | for a unit, the object it targets, or `nil` |
| `Handle()` | its handle as text, `"0x..."`, for logs |
| `Field(name, element)` | a descriptor field by name (`"UNIT_FIELD_HEALTH"`, `"Unit.Health"`, `"Health"`): a number, a guid, or the bytes or halves as several numbers (abi `0.1.9`) |
| `Fields(name)` | every element of a field, as a list |
| `SetField(name, value, element)` | writes a field in your client until the server's next update of it; `true`, or `nil` and why |
| `FieldCount()` | how many numbers the object's block holds |
| `Auras()`, `Skills()`, `PartyMembers()`, `RaidMembers()` | a unit's auras; your player's skills, party and raid members' guids, as lists (abi `0.1.18`) |
| `Select()`, `Interact()`, `CastAt(id)`, `Follow()`, `Assist()`, `Use(target)` | what your player can do with the object, through `wxl.action`: `true`, or `nil` and why |
| every other method | a read from the runtime's table: the value, or `nil` and why; the tables below list them by kind |

#### The reads, by kind

An object has the methods of its kind and of the kinds above it: a player has every unit
method, a container every item method. The tables are made from the runtime's table of reads.

<!-- object-members:lua (written by wxl-object-members) -->

**Every object**

| Method | Answers | What |
|---|---|---|
| `Position()` | `wxl.Vec3` | Where it stands in the world, in yards; an item has no place. |
| `Facing()` | `number` | The way it faces, in radians. |
| `Entry()` | `integer` | The server's template id: a creature, item or game-object entry; 0 for a player. |
| `Scale()` | `number` | Its size, 1 being the model's own. |
| `IsActivePlayer()` | `boolean` | Whether it is the unit the client controls. Came with abi 0.1.18. |
| `Distance(other)` | `number` | Yards from it to another object, centre to centre; invalid when either has no place in the world. Came with abi 0.1.18. |
| `DistanceTo(point)` | `number` | Yards from it to a point; invalid for an object with no place in the world. Came with abi 0.1.18. |

**Units, players included**

| Method | Answers | What |
|---|---|---|
| `Health()` | `integer` | Its health; 0 or less is a dead unit, as the client reads it. |
| `MaxHealth()` | `integer` | Its maximum health. |
| `Level()` | `integer` | Its level. |
| `Power(power)` | `integer` | Its power of a type (0 mana, 1 rage, 2 focus, 3 energy, 4 happiness, 5 runes, 6 runic power). |
| `MaxPower(power)` | `integer` | Its maximum power of a type. |
| `PowerType()` | `integer` | The type of power it runs on, as PowerValue takes it. |
| `Race()` | `integer` | Its race, as ChrRaces.dbc numbers it. |
| `Class()` | `integer` | Its class, as ChrClasses.dbc numbers it. |
| `Sex()` | `integer` | 0 male, 1 female, 2 none. |
| `DisplayId()` | `integer` | The model it shows now, a CreatureDisplayInfo.dbc id. |
| `NativeDisplayId()` | `integer` | The model it shows when nothing changes its shape. |
| `MountDisplayId()` | `integer` | The model of its mount; 0 while not mounted. |
| `FactionTemplate()` | `integer` | Its FactionTemplate.dbc id. |
| `Flags()` | `integer` | UNIT_FIELD_FLAGS as the server sends it. |
| `Flags2()` | `integer` | UNIT_FIELD_FLAGS_2 as the server sends it. |
| `DynamicFlags()` | `integer` | UNIT_DYNAMIC_FLAGS as the server sends it to this client. |
| `NpcFlags()` | `integer` | What it offers to talk about: vendor, trainer, quest giver, ... as UNIT_NPC_FLAGS. |
| `BoundingRadius()` | `number` | The radius of its body, in yards. |
| `CombatReach()` | `number` | How far it reaches in melee, in yards. |
| `Charm()` | `wxl.Guid` | The unit it charms, or 0. |
| `Summon()` | `wxl.Guid` | The unit it summoned, its pet among them, or 0. |
| `CharmedBy()` | `wxl.Guid` | Who charms it, or 0. |
| `SummonedBy()` | `wxl.Guid` | Who summoned it: a pet's owner, or 0. |
| `CreatedBy()` | `wxl.Guid` | Who created it: a totem's shaman, or 0. |
| `TargetGuid()` | `wxl.Guid` | What it targets, as a guid, whether that is in view or not; 0 for nothing. Came with abi 0.1.18. |
| `Reaction()` | `wxl.Reaction` | How it stands towards the active player, by the client's own reckoning: the colour of its name. Came with abi 0.1.18. |
| `ReactionTo(other)` | `wxl.Reaction` | How it stands towards another unit, by the client's own reckoning. Came with abi 0.1.18. |
| `IsDead()` | `boolean` | Whether it is dead: no health left, or marked dead by the server. Came with abi 0.1.18. |
| `InCombat()` | `boolean` | Whether it is in combat, as the client's UnitAffectingCombat reads it. Came with abi 0.1.18. |
| `IsPvp()` | `boolean` | Whether it is flagged for player-versus-player combat. Came with abi 0.1.18. |
| `IsTapped()` | `boolean` | Whether someone has tapped it: its kill and loot are theirs. Came with abi 0.1.18. |
| `IsTappedByPlayer()` | `boolean` | Whether the active player, or their group, has tapped it. Came with abi 0.1.18. |
| `IsLootable()` | `boolean` | Whether it has loot for the active player. Came with abi 0.1.18. |
| `IsSkinnable()` | `boolean` | Whether its corpse can be skinned. Came with abi 0.1.18. |
| `StandState()` | `wxl.StandState` | How it stands: standing, sitting, sleeping, kneeling, ... Came with abi 0.1.18. |
| `IsSitting()` | `boolean` | Whether it sits, on the ground or in a chair. Came with abi 0.1.18. |
| `IsStealthed()` | `boolean` | Whether it is stealthed, as the client's IsStealthed reads it. Came with abi 0.1.18. |
| `IsMounted()` | `boolean` | Whether it rides a mount, as the client's IsMounted reads it. Came with abi 0.1.18. |
| `CastingSpellId()` | `integer` | The spell it casts now; 0 for none. Came with abi 0.1.18. |
| `CastStart()` | `integer` | When the cast began, in milliseconds on the client's clock (the client's GetTime() * 1000). Came with abi 0.1.18. |
| `CastEnd()` | `integer` | When the cast ends, on the same clock. Came with abi 0.1.18. |
| `IsCasting()` | `boolean` | Whether it is casting, its cast not yet over, as the client's UnitCastingInfo reads it. Came with abi 0.1.18. |
| `ChannelSpellId()` | `integer` | The spell it channels now; 0 for none. Came with abi 0.1.18. |
| `ChannelStart()` | `integer` | When the channel began, on the client's clock. Came with abi 0.1.18. |
| `ChannelEnd()` | `integer` | When the channel ends, on the client's clock. Came with abi 0.1.18. |
| `IsChanneling()` | `boolean` | Whether it is channeling, its channel not yet over. Came with abi 0.1.18. |
| `AuraCount()` | `integer` | How many auras the client holds on it, buffs and debuffs alike. Came with abi 0.1.18. |
| `Aura(slot)` | `wxl.Aura` | The aura in a slot, 0 to AuraCount - 1: its spell, caster, stacks, duration, expiration and flags. Came with abi 0.1.18. |
| `HasAura(spell_id)` | `boolean` | Whether an aura of a spell is on it. Came with abi 0.1.18. |
| `CreatureType()` | `wxl.CreatureType` | What kind of creature it is, as the client works it out: beast, humanoid, undead, ... Came with abi 0.1.18. |
| `CreatureFamily()` | `integer` | Its beast family, a CreatureFamily.dbc id; 0 for none. Came with abi 0.1.18. |
| `Classification()` | `wxl.Classification` | Its rank: normal, elite, rare, rare elite, world boss. Came with abi 0.1.18. |
| `KnowsSpell(spell_id)` | `boolean` | Whether it knows a spell: the active player's spell book or its pet's; false for any other unit. Came with abi 0.1.18. |
| `MovementFlags()` | `integer` | The flags of its movement: forward 0x1, backward 0x2, ..., falling 0x1000, flying 0x2000000. Came with abi 0.1.18. |
| `Speed(type)` | `number` | One of its speeds, in yards a second: walk, run, swim, flight, ...; the turn and pitch rates in radians a second. Came with abi 0.1.18. |
| `IsSwimming()` | `boolean` | Whether it swims, as the client's IsSwimming reads it. Came with abi 0.1.18. |
| `IsFlying()` | `boolean` | Whether it flies, as the client's IsFlying reads it. Came with abi 0.1.18. |
| `IsFalling()` | `boolean` | Whether it is falling, as the client's IsFalling reads it. Came with abi 0.1.18. |
| `IsOutdoors()` | `boolean` | Whether it stands outdoors, as the client's IsOutdoors reads it: false in a building or a cave the world marks indoors, where a mount is refused. Came with abi 0.1.24. |

**Players**

| Method | Answers | What |
|---|---|---|
| `PlayerFlags()` | `integer` | PLAYER_FLAGS as the server sends it. |
| `GuildId()` | `integer` | Its guild's id; 0 while unguilded. |
| `GuildRank()` | `integer` | Its rank in the guild, 0 for the guild master. |
| `ChosenTitle()` | `integer` | The title it shows, a CharTitles.dbc bit index; 0 for none. |
| `VisibleItem(slot)` | `integer` | The item id an equipment slot shows (0 head .. 18 tabard); 0 when the slot is empty. |
| `IsGhost()` | `boolean` | Whether it is a ghost, released from its corpse. Came with abi 0.1.18. |
| `IsAfk()` | `boolean` | Whether it is away from the keyboard. Came with abi 0.1.18. |
| `IsDnd()` | `boolean` | Whether it asked not to be disturbed. Came with abi 0.1.18. |
| `IsResting()` | `boolean` | Whether it rests in an inn or a city, gaining rested experience. Came with abi 0.1.18. |
| `Money()` | `integer` | Its money, in copper. Only the active player has it: "invalid" for another. Came with abi 0.1.18. |
| `Xp()` | `integer` | Its experience in this level. Only the active player has it: "invalid" for another. Came with abi 0.1.18. |
| `NextLevelXp()` | `integer` | The experience this level takes. Only the active player has it: "invalid" for another. Came with abi 0.1.18. |
| `RestedXp()` | `integer` | Its rested experience: kills give twice as much until it is spent. Only the active player has it: "invalid" for another. Came with abi 0.1.18. |
| `HonorPoints()` | `integer` | Its honor points. Only the active player has it: "invalid" for another. Came with abi 0.1.18. |
| `ArenaPoints()` | `integer` | Its arena points. Only the active player has it: "invalid" for another. Came with abi 0.1.18. |
| `InventoryItem(slot)` | `wxl.Object` | The item in an equipment slot, or the bag in a bag slot (19 to 22); none for an empty slot. Only the active player has it: "invalid" for another. Came with abi 0.1.18. |
| `BackpackItem(slot)` | `wxl.Object` | The item in a slot of the backpack, 0 to 15; none for an empty slot. Only the active player has it: "invalid" for another. Came with abi 0.1.18. |
| `Skill(line)` | `wxl.Skill` | The skill on a line of the skill list, 0 to 127: its id, value and maximum; an id of 0 for an empty line. Only the active player has it: "invalid" for another. Came with abi 0.1.18. |
| `SkillOf(skill_id)` | `wxl.Skill` | A skill by its SkillLine.dbc id; invalid for a skill the player does not have. Only the active player has it: "invalid" for another. Came with abi 0.1.18. |
| `PartySize()` | `integer` | How many others are in its party, 0 to 4. Only the active player has it: "invalid" for another. Came with abi 0.1.18. |
| `PartyMemberGuid(slot)` | `wxl.Guid` | The other party member in a party slot, 0 to 3, whether in view or not; 0 for an empty slot. Only the active player has it: "invalid" for another. Came with abi 0.1.18. |
| `RaidSize()` | `integer` | How many are in its raid, itself included; 0 outside a raid. Only the active player has it: "invalid" for another. Came with abi 0.1.18. |
| `RaidMemberGuid(slot)` | `wxl.Guid` | The raid member in a raid slot, 0 to 39, whether in view or not; 0 for an empty slot. Only the active player has it: "invalid" for another. Came with abi 0.1.18. |

**Items, containers included**

| Method | Answers | What |
|---|---|---|
| `Owner()` | `wxl.Guid` | The player who owns it. |
| `ContainedIn()` | `wxl.Guid` | The bag holding it, or 0 in the backpack or when equipped. |
| `Creator()` | `wxl.Guid` | Who crafted it, or 0. |
| `StackCount()` | `integer` | How many the stack holds. |
| `Duration()` | `integer` | Seconds left before it expires; 0 when it does not. |
| `Flags()` | `integer` | ITEM_FIELD_FLAGS as the server sends it: soulbound 0x1, ... |
| `Durability()` | `integer` | Its durability now. |
| `MaxDurability()` | `integer` | Its durability when repaired; 0 for an item that does not wear. |
| `Enchant(slot)` | `integer` | The enchantment in a slot, 0 to 11 (0 permanent, 1 temporary, 2 to 4 sockets, ...), a SpellItemEnchantment.dbc id; 0 for none. Came with abi 0.1.18. |
| `Quality()` | `wxl.ItemQuality` | Its quality: poor, common, uncommon, rare, epic, legendary, artifact, heirloom. "failed" until the client has the item's record from the server. Came with abi 0.1.18. |
| `Level()` | `integer` | Its item level. "failed" until the client has the item's record from the server. Came with abi 0.1.18. |
| `RequiredLevel()` | `integer` | The level it takes to use. "failed" until the client has the item's record from the server. Came with abi 0.1.18. |
| `Class()` | `integer` | Its class, an ItemClass.dbc id: weapon 2, armor 4, ... "failed" until the client has the item's record from the server. Came with abi 0.1.18. |
| `Subclass()` | `integer` | Its subclass within the class: a sword, a cloth robe, ... "failed" until the client has the item's record from the server. Came with abi 0.1.18. |
| `InventoryType()` | `integer` | Where it is worn: head 1, neck 2, ...; 0 for an item that is not worn. "failed" until the client has the item's record from the server. Came with abi 0.1.18. |
| `MaxStack()` | `integer` | How many a stack of it holds at most. "failed" until the client has the item's record from the server. Came with abi 0.1.18. |
| `SellPrice()` | `integer` | What a vendor pays for one, in copper. "failed" until the client has the item's record from the server. Came with abi 0.1.18. |

**Containers**

| Method | Answers | What |
|---|---|---|
| `NumSlots()` | `integer` | How many slots the bag has. |
| `Slot(slot)` | `wxl.Guid` | The item in a slot, as a guid; 0 for an empty one, invalid past NumSlots. |
| `SlotItem(slot)` | `wxl.Object` | The item in a slot of the bag; none for an empty one, invalid past NumSlots. Came with abi 0.1.18. |

**Game objects**

| Method | Answers | What |
|---|---|---|
| `CreatedBy()` | `wxl.Guid` | Who summoned or spawned it, or 0 for one placed in the world. |
| `DisplayId()` | `integer` | Its model, a GameObjectDisplayInfo.dbc id. |
| `Flags()` | `integer` | GAMEOBJECT_FLAGS: in use 0x1, locked 0x2, ... |
| `DynamicFlags()` | `integer` | GAMEOBJECT_DYNAMIC's flags, as the server sends them to this client. |
| `Faction()` | `integer` | Its FactionTemplate.dbc id. |
| `Level()` | `integer` | Its level. |
| `State()` | `integer` | 0 active, 1 ready, 2 destroyed. |
| `GameObjectType()` | `integer` | The game-object type, in the server's numbering (door 0, button 1, chest 3, ...). |
| `ArtKit()` | `integer` | The art kit it shows. |

**Dynamic objects**

| Method | Answers | What |
|---|---|---|
| `Caster()` | `wxl.Guid` | The unit that cast it. |
| `SpellId()` | `integer` | The spell it is the area of. |
| `Radius()` | `number` | Its radius, in yards. |
| `CastTime()` | `integer` | The client time it was cast at, in milliseconds. |

**Corpses**

| Method | Answers | What |
|---|---|---|
| `Owner()` | `wxl.Guid` | The player it belongs to. |
| `Party()` | `wxl.Guid` | The party the owner was in when they died, or 0. |
| `DisplayId()` | `integer` | Its model. |
| `Guild()` | `integer` | The owner's guild id. |
| `Flags()` | `integer` | CORPSE_FIELD_FLAGS: bones 0x1, ... |
| `DynamicFlags()` | `integer` | CORPSE_FIELD_DYNAMIC_FLAGS: lootable 0x1. |

<!-- /object-members:lua -->

`tostring(object)` gives its kind and handle, `Unit(0x000000000000002B)`, `gone Unit(...)` once it
has left. An object is one value while it is in view: `==` and table keys work on it, and any
other name is a field of your own (`object.note = "rare"`), dropped when it leaves.

### Events

The families and their events are listed, with every parameter, in
[`wxl.d.lua`](../lua/types/wxl.d.lua), and described one line each in
[`include/wxl/events.def`](../include/wxl/events.def). At run time, `wxl.events` lists them.
A few to start from:

| Family | Event | Parameters | Happens when |
|---|---|---|---|
| `WorldScript` | `OnUpdate` | `delta` | every game tick |
| | `OnWorldEnter` | `map_id`, `position` | the world finished loading and you are in it |
| | `OnWorldClick` | `point`, `object`, `button` | a click landed in the world (`button`: `"left"`, `"right"` or `"middle"`) |
| `ObjectScript` | `OnObjectCreate` | `object`, `guid`, `type` | an object came into view |
| | `OnTargetChanged` | `previous`, `current` | your target changed |
| | `OnUnitDeath` | `unit` | a unit died |
| | `OnUnitAuraChanged` | `unit`, `spell_id`, `removed` | an aura was applied to a unit, or left it |
| | `OnSpellCastRequest` | `caster`, `spell_id`, `item`, `target_guid`, `confirmed`, `refused` | you ask to cast a spell; answer to refuse it |
| `CombatScript` | `OnCombatDamage` | `source`, `source_guid`, `target`, `target_guid`, `spell_id`, ... `amount`, ... | damage landed, as the server reports it |
| `NetScript` | `OnMessageReceived` | `opcode`, `data`, `suppressed` | a message arrived from the server; answer to hide it |
| `AssetScript` | `OnFileOpen` | `path`, `redirect` | a file is about to be opened, on any thread; answer with another path (an open on the main thread only) |
| `LuaScript` | `OnUiEvent` | `index`, `name`, `format`, `values`, `value_count` | the client fires an event to its interface (`PLAYER_TARGET_CHANGED`...) |
| `PlayerScript` | `OnPlayerMoneyChanged` | `player`, `previous`, `current` | your money changed, in copper (abi `0.1.6`) |
| `ChatScript` | `OnChatMessage` | `type`, `language`, `sender`, `sender_guid`, `sender_name`, ... `text`, ... | a chat message of the server became a line the chat shows (abi `0.1.6`; `line_id`, `shown_text` `0.1.7`) |
| | `OnChatAddonMessage` | `type`, `sender`, `sender_guid`, `sender_name`, `prefix`, `text`, `channel`, `gm` | an addon message reached the interface as `CHAT_MSG_ADDON` (abi `0.1.7`) |
| `GlueScript` | `OnGlueScreenChanged` | `screen`, `previous` | the login screens moved on: `"login"`, `"charselect"`... (abi `0.1.6`) |
| `ObjectScript` | `OnObjectFieldChanged` | `object`, `type`, `field`, `count`, `old_value`, `new_value` | a descriptor field `wxl.object.Watch` asked for changed (abi `0.1.9`) |
| `AssetScript` | `OnArchiveMounted`, `OnArchiveUnmounted` | `path`, `priority`, `hash_entries`, `block_entries`, `bytes` | the client mounted an archive, or is about to close one (abi `0.1.11`) |
| `WorldScript` | `OnMemoryPressure` | `largest_free`, `free`, `committed`, `threshold` | the largest free block of the address space fell under the threshold `wxl.footprint.WatchPressure` set (abi `0.1.11`) |
| `AssetScript` | `OnTerrainTileLoad`, `OnMapObjectGroupLoad`, `OnSkinProfileLoad`, `OnTextureDecode`, `OnShaderLoad`, `OnSoundFileLoad` | see [Serving and converting assets](#serving-and-converting-assets) | the engine read a tile, a building's group, a skin, a texture, a shader, a sound (abi `0.1.19`) |
| `ObjectScript` | `OnAppearanceChanged` | `change`, `wearer`, `model`, `race_id`, `sex_id`, `item_slot`, `display_id`, `npc` | a character's appearance put an item on, is taking one off (`"worn"`, `"removed"`), or goes away (`"freed"`) (abi `0.1.20`) |
| `AssetScript` | `OnModelParsed`, `OnSkinProfileParsed`, `OnModelSequenceLoad` | `path`, `file`, then the header, the skin or the sequence | the model loader's stages; see [Models](#models-wxlm2) (abi `0.1.20`) |
| `AssetScript` | `OnFileMissing` | `path`, `kind`, `open`, `pending`, `loader` | the client looked for a file no layer has; see [Assets on demand](#assets-on-demand-missing-names-hashes-paths-of-your-own-overrides-reloads) (abi `0.1.23`) |
| `RenderScript` | `OnModelBatchDrawn` | `model`, `root_model`, then the numbers of the draw | the device drew one batch of a model (abi `0.1.20`) |
| `WorldScript` | `OnMouseMotion`, `OnMouseWheel` | `dx`, `dy`; `delta`, `handled` | the mouse moved, in its own counts; the wheel turned, in notches, which an `Override` keeps from the game (abi `0.1.24`) |
| `RenderScript` | `OnCameraUpdate` | `index`, `delta` | the client placed the world camera for the frame: set a view with `wxl.camera.SetView` here (abi `0.1.24`) |
| `RenderScript` | `OnImGuiFrame` | `index`, `delta` | a frame of the runtime's Dear ImGui is open, for a C++ mod's windows (abi `0.1.25`) |

The families `PlayerScript`, `GlueScript` and `ChatScript`, and `WorldScript`'s `OnZoneChanged`,
`OnMouseoverChanged` and `OnCVarChanged`, came with `0.1.6`: a mod that uses one asks for
`"abi": "0.1.6"`, so that an older runtime refuses it with a reason rather than failing at the
unknown name. A mod asking for less cannot subscribe to them. `ChatScript`'s `OnChatAddonMessage`,
and `OnChatMessage`'s last two parameters, came with `0.1.7`: a mod that uses them asks for
`"abi": "0.1.7"`. `ObjectScript`'s `OnObjectFieldChanged` and the descriptor fields came with
`0.1.9`. `0.1.10` brought no event: its `wxl.object.Unwatch` and `wxl.storage.RemoveSearchPath`
answer `nil, "unsupported"` on an older runtime, as any function the runtime lacks does.
`AssetScript`'s `OnArchiveMounted` and `OnArchiveUnmounted`, `WorldScript`'s `OnMemoryPressure`
and the `wxl.footprint` module came with `0.1.11`; the `wxl.profile` module with `0.1.12`.
`0.1.13` brought no event: `wxl.runtime.Raises` says whether the runtime raises an event at all,
and answers `nil, "unsupported"` on an older runtime. `0.1.14` brought no event either: from it on,
a service module or function newer than the manifest's `abi` is refused as an event is, so a mod
using `wxl.footprint` asks for `0.1.11` and one using `wxl.profile` for `0.1.12`. `0.1.15` brought
no event: `wxl.storage.Redirect`, `wxl.storage.Unredirect` and `wxl.script.Live` came with it.
The [gameplay events](#gameplay-events), with the families `LootScript`, `ItemScript`,
`QuestScript`, `GroupScript` and `SocialScript`, came with `0.1.17`. The object reads and actions of
[What a unit, a player, an item can tell you](#what-a-unit-a-player-an-item-can-tell-you) and the
`wxl.action` module came with `0.1.18`. `AssetScript`'s `OnTerrainTileLoad`, `OnMapObjectGroupLoad`,
`OnSkinProfileLoad`, `OnTextureDecode`, `OnShaderLoad` and `OnSoundFileLoad`,
`wxl.storage.Serve` and `wxl.storage.Unserve`, and the reason `"pending"`, came with `0.1.19`.
`ObjectScript`'s `OnAppearanceChanged`, `AssetScript`'s `OnModelParsed`, `OnSkinProfileParsed` and
`OnModelSequenceLoad`, `RenderScript`'s `OnModelBatchDrawn` and the `wxl.m2` module came with
`0.1.20`. `wxl.m2`'s models of the world (`CreateWorldModel`, `Place` and the rest) and the cast's
points (`has_source`, `source`, `has_dest`, `dest` and their transports) came with `0.1.21`, and the
`wxl.engine` module with `0.1.22`.
`AssetScript`'s `OnFileMissing`, `wxl.storage`'s `HashFile`, `HashFileAsync`, `OverrideFile`,
`RestoreFile` and `ReloadAsset`, and the `wxl.relay` module came with `0.1.23`.
`WorldScript`'s `OnMouseMotion` and `OnMouseWheel`, `RenderScript`'s `OnCameraUpdate`,
the `wxl.camera`, `wxl.input` and `wxl.world` modules, `wxl.action.Face` and `IsOutdoors` came with
`0.1.24`. `wxl.input.BlockMouseLook` and `MouseLookBlocked`, the `wxl.pose` module and
`RenderScript`'s `OnImGuiFrame` came with `0.1.25`.

### Gameplay events

These tell what happens to your character and around it: auras, loot, items, quests, the group,
the people you deal with. Each is raised where the client itself takes the fact in: an aura when
it is put in one of the unit's aura slots, the loot window when the client fills it, an inventory
slot when the server's update is applied. You see what the player sees, at the moment the client
knows it. They need `"abi": "0.1.17"` in your `mod.json`.

A small mod that writes down what you loot and joins any group a friend of yours invites you to:

```lua
local friends = { Jaina = true, Thrall = true }

function LootScript:OnLootSlot(index, slot, item_id, quantity)
    print("in the loot: item " .. item_id .. " x" .. quantity)
end

function GroupScript:OnGroupInvite(inviter, can_accept)
    if friends[inviter] then return true end -- accept
    return nil                               -- leave it to the dialog
end
```

`OnGroupInvite` is a request: its first return value accepts, its second declines (`return nil,
true`), and returning nothing shows the client's dialog as usual. When you answer, the client does
not show the dialog at all, and does what its buttons would have done.

Many of these come in pairs or runs, the way the client gets them: `OnLootOpened` says how many
items the window holds, and one `OnLootSlot` per item follows; `OnGroupUpdated` gives the group,
and one `OnGroupMember` per other member follows. The full parameter lists are in
[`wxl.d.lua`](../lua/types/wxl.d.lua).

| Family | Event | Parameters | Happens when |
|---|---|---|---|
| `ObjectScript` | `OnUnitAuraApplied`, `OnUnitAuraRemoved` | `unit`, `unit_guid`, `slot`, `spell_id`, `caster`, `caster_guid`, `stacks`, `previous_stacks`, `level`, `flags`, `harmful`, `duration`, `remaining` | an aura was put in a unit's aura slot, or left it (with what it was) |
| | `OnUnitAuraRefreshed` | the same | an aura still on a unit gained or lost stacks (`previous_stacks` says how many it had), or its duration was renewed |
| | `OnUnitAuraChanged` | `unit`, `spell_id`, `removed` | the coarse one, older than these: something about this unit's auras changed, an aura put on (`removed` false) or taken off; use it when you only need to know that, the three above when you need what |
| | `OnUnitMountChanged` | `unit`, `unit_guid`, `previous_display_id`, `display_id` | a unit got on a mount (`display_id` not 0), off one (`0`) or changed mounts |
| `LootScript` | `OnLootOpened` | `source`, `source_guid`, `kind`, `money`, `count` | the loot window opened on a corpse, a chest, a fish... (`kind`: `"corpse"`, `"fishing"`, `"skinning"`...) |
| | `OnLootSlot` | `index`, `slot`, `item_id`, `quantity`, `display_id`, `random_suffix`, `random_property_id`, `slot_type` | one item of that window (`slot_type`: `"take"`, `"roll"`, `"master"`, `"locked"`, `"owner"`) |
| | `OnLootSlotCleared` | the same | an item left the window: someone took it |
| | `OnLootClosed` | like `OnLootOpened` | the window closed |
| | `OnLootMoney` | `amount`, `alone` | you received your share of the money, in copper |
| | `OnLootRollStart` | `source_guid`, `slot`, `item_id`, ..., `duration`, `vote_mask`, `roll_id` | a roll started on an item; `roll_id` is what the interface's `RollOnLoot` takes |
| | `OnLootRoll`, `OnLootRollWon`, `OnLootAllPassed` | `source_guid`, `slot`, `item_id`, ..., `player_guid`, `roll`, `roll_type`, `auto_pass` | someone chose (`roll_type`: `"need"`, `"greed"`...), someone won, or everyone passed |
| `ItemScript` | `OnItemPushed` | `player_guid`, `bag`, `slot`, `item_id`, ..., `count`, `total`, `received`, `created`, `announced` | an item was looted, given (`received`) or made (`created`) for you or a member of your group |
| | `OnInventorySlotChanged` | `container`, `container_guid`, `slot`, `previous_guid`, `item`, `item_guid` | a slot of your bags got another item, or none: `container` is you for your backpack and bag slots, or the bag |
| | `OnItemDurabilityChanged` | `item`, `item_guid`, `previous`, `current`, `maximum` | an item wore down or was repaired |
| | `OnItemStackChanged` | `item`, `item_guid`, `previous`, `current` | a stack grew or shrank |
| | `OnInventoryError` | `result`, `item_guid`, `other_guid`, `required_level` | the server refused an inventory change, with its reason code |
| `QuestScript` | `OnQuestAccepted`, `OnQuestRemoved` | `quest_id`, `log_slot` | a quest entered your quest log, or left it (abandoned or turned in) |
| | `OnQuestObjectiveChanged` | `quest_id`, `log_slot`, `objective`, `previous`, `current` | the count of a kill or object objective moved |
| | `OnQuestCompleted`, `OnQuestFailed` | `quest_id`, `log_slot` | every objective is done, or the quest failed |
| | `OnQuestTurnedIn` | `quest_id`, `xp`, `money`, `honor`, `talents`, `arena_points` | you turned a quest in, with its rewards |
| | `OnQuestGiverGreeting`, `OnQuestGiverDetail`, `OnQuestGiverProgress`, `OnQuestGiverReward` | `giver`, `giver_guid`, `quest_id`, `title`, `text`, `count`, `shared_by` | a quest giver shows its list, a quest's details, what a quest still asks for, or its reward |
| `GroupScript` | `OnGroupInvite` | `inviter`, `can_accept`, `accept`, `decline` | you are invited to a group; answer to accept or decline |
| | `OnGroupUpdated` | `group_guid`, `raid`, `battleground`, `count`, `subgroup`, `leader_guid`, `loot_method`, `looter_guid`, `loot_threshold`, ... | the server listed your group |
| | `OnGroupMember` | `index`, `count`, `member`, `member_guid`, `name`, `online`, `subgroup`, `flags`, `roles` | one other member of it |
| | `OnGroupLeft` | `reason` | you are no longer in a group (`"removed"` or `"disbanded"`) |
| | `OnGroupLootMethodChanged` | `loot_method`, `looter_guid`, `loot_threshold` | the loot rules changed (`loot_method`: `"group"`, `"need_before_greed"`, `"master"`...) |
| | `OnReadyCheck`, `OnReadyCheckResponse`, `OnReadyCheckFinished` | `initiator`, `initiator_guid`, `duration`; `member`, `member_guid`, `ready`; `initiator_guid` | a ready check started, someone answered, it is over |
| `SocialScript` | `OnDuelRequested` | `challenger`, `challenger_guid`, `arbiter_guid`, `accept`, `decline` | someone challenges you; answer to accept or decline |
| | `OnDuelCountdown`, `OnDuelWon`, `OnDuelFinished` | `duration`; `winner`, `loser`, `fled`; `completed` | the duel counts down, someone won, your duel is over |
| | `OnTradeStatus` | `status`, `partner_guid` | a trade moved a step (`"proposed"`, `"initiated"`, `"accepted"`, `"complete"`, `"cancelled"`...) |
| | `OnEmote`, `OnTextEmote` | `unit`, `unit_guid`, `emote_id`; `sender`, `sender_guid`, `text_emote`, `emote_number`, `target_name` | a unit played an emote's animation; a player's `/wave` or `/dance` reached you |
| | `OnResurrectRequest` | `offerer`, `offerer_guid`, `name`, `sickness`, `accept`, `decline` | someone offers to resurrect you; answer to accept or decline |
| | `OnWindowOpened`, `OnWindowClosed` | `kind`, `npc`, `npc_guid` | a window opened on someone you talk to, or closed (`kind`: `"merchant"`, `"trainer"`, `"bank"`, `"quest"`, `"mail"`, `"taxi"`, `"trade"`...) |
| `PlayerScript` | `OnPlayerDied`, `OnPlayerReleased`, `OnPlayerResurrected` | `unit` | you died, released your spirit, are alive again |
| | `OnReputationChanged` | `faction_id`, `previous`, `current`, `bonus` | your standing with a faction moved, counted as the reputation panel counts it |
| | `OnAchievementEarned` | `player`, `player_guid`, `achievement_id`, `date`, `silent` | you, or a player near you, earned an achievement |

A few things to know:

- **Who and what.** A handle (`unit`, `caster`, `item`, `npc`...) is `nil` when the client does not
  hold that object, out of view for example; the guid next to it is there all the same.
- **Auras a unit already has** when it comes into view are not announced one by one: read them
  with `UnitAura` from the interface, or wait for their next change.
- **Your equipped items** (head, chest, weapons...) are `ObjectScript:OnEquipmentChanged`'s;
  `OnInventorySlotChanged` covers your bags, your bank's bags and the bags' own slots.
- **Text emotes** come as the server sent them, before the client looks at your ignore list: a
  `/wave` from someone you ignore still reaches `OnTextEmote`, though the chat does not show it. A
  mod that cares checks the sender itself.
- **Windows.** `OnWindowOpened` comes once when a window opens, not again for each page it shows;
  the quest giver's pages have events of their own.

Not every runtime raises every event. Subscribing to one that yours does not raise logs a warning
naming it, and the handler never runs.

### The `wxl.lua` service

`wxl.lua` publishes a service, `wxl.lua` version 1
([`lua/include/wxl/lua.h`](../lua/include/wxl/lua.h)), for a runtime, a console or a C++ mod: how
many Lua mods there are, `poll()` to look for changed folders now, `reload(id)` to reload one mod.

---

## 🧰 When it does not work

Every one of these leaves a line in `Logs/WarcraftXL.log`, tagged `[wxl.lua]`. Search the log for
your mod's id first.

| The log says | What it means |
|---|---|
| nothing about your mod at all | `wxl.lua` is not in `Mods/` (step 1), or your folder has neither a `mod.json` with `lua` nor an `init.lua`. Look for `[wxl.lua] scanning` |
| `[host] refused <folder>: ... 'binaries' and 'lua' cannot both be given` | a manifest names one or the other: `lua` for a Lua mod |
| `mod <id> failed to load init.lua: ...` | the entry file has a syntax error, or raised one while running; the rest of the line is Lua's message, with the line number |
| `<Family>.<name> is not an event of <Family>; its events are ...` | a misspelt family method, or something other than a handler on a family table |
| `refused <id>: needs abi ...` | the manifest's `abi` is newer than your `wxl.lua` or the runtime, or of another minor. The line gives both versions |
| `On<Event> of <Family> came with abi ...` | the event is newer than your manifest's `abi`; the line says what to ask for |
| `wxl.<module> came with abi ...`, `wxl.<module>.<Function> came with abi ...` | the module, or that function of it, is newer than your manifest's `abi`; the line says what to ask for |
| `refused <id>: requires service ...` | a service under `requires` is missing from this runtime |
| `folder '<name>' holds an init.lua but its name makes no mod id` | rename the folder, or give it a `mod.json` with an `id` |
| `this runtime does not raise On<Event> of <Family>Script` | the handler is valid but will never run on this runtime |
| `mod <id> errored in its ... (n/25)` | an error in a handler; the traceback after it gives the line |
| `wxl.<module>.<Function>: '<argument>' (argument n) is missing; it takes ...` | a call left out an argument, or passed `nil` for it: give it, as the line says |
| `mod <id> is disabled: it errored 25 times` | fix the error and save a file of the mod: it reloads and runs again |
| `the Override of On<Event> returned ...` | a return value of the wrong kind, too many of them, or a string too long for the buffer |
| `On<Event> was raised off the main thread` | that event happened on another thread, and its answer could not be taken ([Events from other threads](#events-from-other-threads)) |
| `mod <id> stays unloaded: its mod.json ...` | after a reload, the changed manifest no longer reads, gives another id, or asks for what the runtime cannot serve; save a fix and it loads |

---

## 🧭 Where to go next

- [`examples/lua-hello/`](../examples/lua-hello/), the example of section 3, to copy and change.
- [`examples/lua-services/`](../examples/lua-services/), the example of section 6: files, a
  search path, the databases, the client's own Lua.
- [`examples/memory-report/`](../examples/memory-report/), a mod to keep: where the client's
  memory goes, written into a report every few minutes.
- [Profiling](profiling.md): where your mod's time goes, in the Tracy viewer.
- [Reading the log](the-log.md): the launch's header, the level setting, and `/mark` with
  [`examples/probe/`](../examples/probe/), a test tool that logs every event the runtime raises.
- [`lua/types/wxl.d.lua`](../lua/types/wxl.d.lua), for every event, object method and service
  function.
- [Your first mod](your-first-mod.md), the same platform from C++, for what only native code
  does: detours on the hook points, and the interface panels of `wxl.ui`.
