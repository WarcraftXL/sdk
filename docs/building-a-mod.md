# Building a mod

One source, one command, two binaries. A mod is written once against the SDK headers and built
for both runtimes in a single step: a 64-bit DLL for the recompiled client and a 32-bit DLL for
the stock `Wow.exe`. Architecture lives in the folder, never in the file name.

A mod written in Lua has nothing to build: [Lua mods](lua-mods.md) covers it.

## What you need

- **CMake 3.21 or newer.**
- **Visual Studio 2022 or newer** with the *Desktop development with C++* workload. The component
  *MSVC v14x - C++ x64/x86 build tools* carries both compilers; the Build Tools edition is enough.
- **The SDK**: a checkout of `WarcraftXL/sdk`, or an install of it (`cmake --install`), reached
  with `find_package(wxl-sdk)`.

Nothing else. The SDK is header-only and fetches nothing.

## The mod's `CMakeLists.txt`

Start from [`template/`](../template/). The whole file is:

```cmake
cmake_minimum_required(VERSION 3.21)
project(my-mod VERSION 1.0.0 LANGUAGES NONE)

set(WXL_SDK_DIR "path/to/sdk" CACHE PATH "Path to the WarcraftXL SDK")
include("${WXL_SDK_DIR}/cmake/wxl_add_mod.cmake")

wxl_add_mod(my-mod
    SOURCES src/mod.cpp)
```

`wxl_add_mod` takes:

| Argument | Meaning | Default |
|---|---|---|
| `SOURCES` | the mod's translation units | required |
| `MANIFEST` | the manifest to validate and ship | `mod.json` beside the `CMakeLists.txt` |
| `TARGETS` | `win-x64`, `win-x86`, or both | both |
| `OUTPUT_NAME` | the DLL's base name | the target name |
| `OUTPUT_DIR` | where the mod folder is assembled | `build/dist` |
| `CXX_STANDARD` | the C++ standard the mod is compiled as: `17`, `20` or `23` | `20` |

`LANGUAGES NONE` is deliberate: with Visual Studio the top level drives the per-architecture builds
and never needs a compiler; each child enables C++ itself. With another generator the tree builds
one architecture, and `wxl_add_mod` enables C++ there.

## The one command

From the mod's directory:

```sh
cmake -S . -B build
cmake --build build --config Release
```

Without `--config`, Visual Studio builds Debug.

## What comes out

```
build/dist/<id>/            <id> is the manifest's "id"
  mod.json
  bin/x64/<name>.dll        win-x64: the recompiled client
  bin/x86/<name>.dll        win-x86: the stock Wow.exe
```

That folder is the mod. The build also leaves one tree per architecture, `build/win-x64/` and
`build/win-x86/`, where the PDBs and intermediate files live.

The manifest's `binaries` table is checked at configure time against what the build produces:

```json
"binaries": {
  "win-x64": "bin/x64/<name>.dll",
  "win-x86": "bin/x86/<name>.dll"
}
```

Every architecture built must be listed at that path, and every architecture listed must be one
of the mod's `TARGETS`. A manifest that promises a binary the mod is never built for is a
packaging bug; the build says so instead of letting a runtime discover it. An architecture the
mod is built for, but not by this tree (below), is only noted at configure time: the folder lacks
that DLL until a build of that architecture writes it.

## Installing into a client

Copy `build/dist/<id>/` into the runtime's mod directory, `Mods/` beside the client executable,
keeping the folder name. Each runtime reads `mod.json`, picks the entry for its own architecture
and ignores the other. A mod with no entry for a runtime is skipped, not failed.

A loaded DLL is locked by the game: close the client before rebuilding into a folder it loaded
from.

## Building one architecture

**With Visual Studio**, narrow the targets from the command line:

```sh
cmake -S . -B build -DWXL_MOD_TARGETS=win-x64
```

The configure output names what it skipped, and the manifest can go on listing both:

```
-- wxl_add_mod: win-x86 is not built by this tree: the mod folder lacks bin/x86/my-mod.dll until a build for win-x86 writes it there
-- my-mod: win-x64  -> .../build/dist/my.mod/bin/x64/my-mod.dll
-- my-mod: win-x86  skipped (not in the targets: win-x64)
```

**With Ninja or Makefiles**, a tree builds one architecture: the one its compiler targets. On
Windows that is the architecture of the developer prompt (*x64 Native Tools Command Prompt*, or
*x86*); from Linux, that of the toolchain file. The DLL lands in the same mod folder layout:

```sh
cmake -S . -B build-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-x64                          # build-x64/dist/<id>/bin/x64/<name>.dll
```

For the folder to hold both, configure a second tree with the other compiler and install it into
the first one's folder:

```sh
cmake -S . -B build-x86 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-x86
cmake --install build-x86 --prefix build-x64/dist/<id>
```

A mod whose `TARGETS` leave out the compiler's architecture builds nothing in that tree, and the
configure says so.

**For a quick loop** on one architecture, configure a child tree by hand and skip the packaging
entirely:

```sh
cmake -S . -B build-x86 -A Win32 -DWXL_MOD_ARCH=win-x86
cmake --build build-x86 --config Release        # DLL in build-x86/Release/
```

`-A x64` with `win-x64` for the other one. The tree checks that the platform and the architecture
agree.

## Adding to the target

At the top level of the superbuild there is no library target, only the per-architecture drivers.
Anything that touches the target belongs inside `if(TARGET ...)`, which is true in each child:

```cmake
wxl_add_mod(my-mod SOURCES src/mod.cpp src/other.cpp)

if(TARGET my-mod)
    target_include_directories(my-mod PRIVATE third_party/include)
    target_compile_definitions(my-mod PRIVATE MY_MOD_VERBOSE=1)
endif()
```

`WXL_MOD_ARCH` holds `win-x64` or `win-x86` in a child, and is empty at the top level, for
anything that differs per architecture.

The mod is compiled as C++20 (`CXX_STANDARD` above) with `/W4 /permissive-` (MSVC) or `-Wall
-Wextra`. The SDK headers compile clean at that level, as C++17 and as C++20.

## The C++ layer

Everything under `include/wxl/*.hpp` compiles into the mod and resolves to the C tables in
`host.h` and `services/*.h`. Nothing of it crosses the boundary.

### The mod

Derive from `wxl::Mod`, declare it once with `WXL_MOD(Class, "id", "Name")`: the id is the
manifest's, and the version is the manifest's alone. Each boot stage has a named virtual:
`OnEarly`, `OnStorageReady`, `OnGraphicsReady`, `OnScriptReady`, `OnReady`, `OnShutdown`.
`OnStage(wxl_stage)` still runs after each of them, for every stage, including one appended to
the contract after this SDK was built. `OnLoad` runs before any stage and may refuse to load;
`OnUnload` runs last, and `wxl::ScriptMgr` is cleared right after it so no handler outlives the
code it points into.

`wxl::Log(level, tag, text)` writes a line; with arguments after the text it formats them with
`std::format`, checked at compile time: `wxl::Log(WXL_LOG_INFO, "my-mod", "map {} at {:.1f}", id,
x)`. `wxl::ResultName(result)` names a `wxl_result` for such a line, and `wxl::ModId()` is the id
`WXL_MOD` declared.

### What a throw costs

No C++ exception crosses into the runtime. The SDK catches one where it called the mod's code,
logs it as an error under the mod's id with its `what()`, and stops what it interrupted:

| Thrown from | Costs |
|---|---|
| the mod's constructor, `OnLoad` | the load: the mod is not loaded |
| a stage's virtual, `OnStage`, `OnUnload` | the mod: the runtime disables it as it disables a mod that faults (`wxl_host.disable`), and none of its code runs again |
| a script's handler, a `wxl::Subscribe` handler | that call: the next script, the next handler and the next event still run |
| a function registered with `wxl.script` | that call: it fails on the script side with `what()` as its error |
| a panel registered with `wxl.ui` | that frame's drawing of it |

A runtime older than abi `0.1.8` has no `disable`: there the mod stops itself, dropping its
scripts and running none of its remaining stages or its unload, and says so in the log. A
destructor that throws still ends the process: C++ makes destructors `noexcept`.

### Events

Derive from a script family (`WorldScript`, `RenderScript`, `ObjectScript`, `AssetScript`,
`NetScript`, `LuaScript`, `CombatScript`, `PlayerScript`, `GlueScript`, `ChatScript`,
`LootScript`, `ItemScript`, `QuestScript`, `GroupScript`, `SocialScript`), override
the events you want,
`wxl::ScriptMgr::Add(&script)`. `Add` subscribes only the events the script overrides, decided at
compile time, so an event you leave alone costs nothing at all. That needs the script's own type:
pass `&script`, not a pointer to its family, which hides what it overrides and makes `Add`
subscribe the whole family to be safe.

A handler takes the event's args as parameters, one per member of its struct in `fields.def`
order:

```cpp
void OnMessageReceived(uint32_t opcode, wxl::Bytes data, wxl::Answer suppressed) override
{
    if (opcode == 0x1234 && !data.empty()) suppressed.Set(); // the client never sees it
}
```

| Kind in `fields.def` | Parameter |
|---|---|
| `u32`, `i32`, `u64`, `i64`, `f32` | `uint32_t`, `int32_t`, `uint64_t`, `int64_t`, `float` |
| `bool` | `bool` |
| `handle` | `wxl::Object`, the object as a value (below); `object.As<wxl::Unit>()` reads it as its kind |
| `guid`, `vec3` | `wxl_guid`, `wxl_vec3` |
| `str` | `std::string_view`, valid for the call |
| `bytes` | `wxl::Bytes`: `data()`, `size()`, a range of `uint8_t`, `View()` as characters; valid for the call |
| `answer` | `wxl::Answer`: `Value()` is what earlier handlers decided, `Set()` answers |
| `buffer` | `wxl::Buffer`: `Value()` is what it holds, `Write(text)` answers when the text and its terminator fit in `Capacity()` |
| `ptr` | the member's C type |
| `enum` (a `WXL_ARGS_ENUM` row) | the C enum it names: `wxl_object_type`, `wxl_script_context`, ... ([`enums.def`](../include/wxl/enums.def)) |

A member that only gives the length of a bytes member or the capacity of a buffer is not a
parameter: the view carries it. `wxl::HandlerOf<WXL_EV_...>` is the whole list as a function type.
A member function named like a handler but taking other parameters would hide the family's virtual
instead of overriding it; `Add` refuses it at compile time.

For one event outside a family, `wxl::Subscribe<WXL_EV_TargetChanged>(&handler)` calls the
handler with the same parameters and returns a `wxl::Subscription` that unsubscribes in its
destructor. `wxl::EventArgsOf<WXL_EV_...>` names the args struct of any event: what a C handler
receives, and what `wxl::Emit` raises.
`wxl::Raises(WXL_EV_...)` (abi `0.1.13`) says whether the runtime raises an event at all, for a
mod that offers what the runtime has rather than asking for one event; it is false on an older
runtime, which cannot say.

### Loading and updates

Seven events follow what the client loads and how objects change. Each is raised only while a
mod listens to it -- until then the client does not read what it would carry -- so they cost
nothing to the mods that leave them alone. Two are busy: `OnTextureLoad` runs for every texture
reference, `OnFileOpened` for every open (on whichever thread opens the file).

| Event | When | Parameters |
|---|---|---|
| `ObjectScript::OnObjectUpdate` | a server update changed an object's fields; once per object and packet, once the packet is applied | `wxl::Object`, its guid, its type |
| `ObjectScript::OnDoodadSpawn` | the map placed a doodad (of a tile, of a building, or one the game puts down) | the doodad (`WXL_HANDLE_NONE`: it is not an object of `wxl.object`), the model's path, its position |
| `AssetScript::OnModelLoad` | a model file has been read, before the client parses it | the path, the bytes, and two pointers to answer through |
| `AssetScript::OnTextureLoad` | a texture is asked for by name, cached or not | the path |
| `AssetScript::OnMapObjectLoad` | a building's root file has been read, before the client walks it | the path, the bytes |
| `AssetScript::OnFileOpened` | an open finished, found or not | the path opened (after any `OnFileOpen` redirect), whether it was found, its size |
| `AssetScript::OnClientDbLoaded` | the client has loaded its databases, once per table | the file's name, the records, their count and size |

`OnModelLoad` can be answered with other bytes. Point `*replacement` at them and set
`*replacement_size`: once every handler has run, the runtime copies them into a buffer of the
client's own and the client parses that instead of the file. Your buffer only has to outlive the
event. The last handler to write wins, so read `*replacement` first if another mod may answer
too:

```cpp
void OnModelLoad(std::string_view path, wxl::Bytes data, const void** replacement,
                 uint32_t* replacement_size) override
{
    if (path != "Creature\\Boar\\Boar.m2" || *replacement) return;
    bigger_ = LoadMyBoar();                       // a std::vector<uint8_t> member of the script
    *replacement      = bigger_.data();
    *replacement_size = static_cast<uint32_t>(bigger_.size());
}
```

The bytes have to be a model the client can parse: a file of the same version, as the client's
own tools write it. If the runtime cannot make the copy, the log says so and the client parses
the file as it read it.

### Gameplay events

The families `LootScript`, `ItemScript`, `QuestScript`, `GroupScript` and `SocialScript`, and the
aura, mount, death, reputation and achievement events of `ObjectScript` and `PlayerScript`, came
with abi `0.1.17`: build against headers of that version or later. They tell what happens to the
player's character and around it, raised where the client takes the fact in (the aura slot it
fills, the loot window it opens, the inventory slot it updates), so a handler sees what the
player sees, when the client knows it.

A script that keeps what the player loots and joins any group a friend invites it to:

```cpp
class Companion final : public wxl::GroupScript
{
public:
    void OnGroupInvite(std::string_view inviter, bool can_accept, wxl::Answer accept,
                       wxl::Answer decline) override
    {
        if (can_accept && (inviter == "Jaina" || inviter == "Thrall")) accept.Set();
        (void)decline; // left alone: the dialog shows as usual for anyone else
    }
};

class Looter final : public wxl::LootScript
{
public:
    void OnLootSlot(uint32_t index, uint32_t slot, uint32_t item_id, uint32_t quantity,
                    uint32_t display_id, uint32_t random_suffix, int32_t random_property_id,
                    wxl_loot_slot_type slot_type) override
    {
        if (slot_type == WXL_LOOT_SLOT_TAKE) wxl::Log("loot: item {} x{}", item_id, quantity);
    }
};
```

Three events are requests, with two answers each, `accept` and `decline`:
`GroupScript::OnGroupInvite`, `SocialScript::OnDuelRequested` and
`SocialScript::OnResurrectRequest`. Setting one makes the runtime do what the dialog's button would
have done, and the client never shows the dialog; setting neither leaves the dialog to the player.
When two handlers disagree, decline wins.

| Family | Events | What they tell |
|---|---|---|
| `ObjectScript` | `OnUnitAuraApplied`, `OnUnitAuraRemoved`, `OnUnitAuraRefreshed` | an aura in a unit's aura slot: the slot, the spell, the caster (`wxl::Object` and guid), stacks and the count before, level, flags, `harmful`, the duration and what is left, in milliseconds |
| | `OnUnitAuraChanged` | the coarse one, older than these: something about this unit's auras changed (an aura on, or off with `removed`); the three above say what |
| | `OnUnitMountChanged` | a unit's mount display id before and after: on a mount, off it, another one |
| `LootScript` | `OnLootOpened`, `OnLootSlot`, `OnLootSlotCleared`, `OnLootClosed` | the loot window: its source, its `wxl_loot_kind`, its money and item count, then one `OnLootSlot` per item; an item taken; the window closed |
| | `OnLootMoney` | the player's share of looted money |
| | `OnLootRollStart`, `OnLootRoll`, `OnLootRollWon`, `OnLootAllPassed` | a roll on a group's loot: started (with the `roll_id` the interface's `RollOnLoot` takes), someone's choice as `wxl_roll_type`, the winner, nobody |
| `ItemScript` | `OnItemPushed` | an item looted, given or made, with its bag, slot, count and total |
| | `OnInventorySlotChanged` | a slot of the player's bags or bank bags, or of one of its bags, got another item or none |
| | `OnItemDurabilityChanged`, `OnItemStackChanged` | an item's durability (with its maximum) or stack count, before and after |
| | `OnInventoryError` | the server refused an inventory change, with its result code |
| `QuestScript` | `OnQuestAccepted`, `OnQuestRemoved`, `OnQuestObjectiveChanged`, `OnQuestCompleted`, `OnQuestFailed` | the quest log as the server writes it: a quest in or out of a slot, an objective's count, its state |
| | `OnQuestTurnedIn` | a quest turned in, with xp, money, honor, talents and arena points |
| | `OnQuestGiverGreeting`, `OnQuestGiverDetail`, `OnQuestGiverProgress`, `OnQuestGiverReward` | a quest giver's pages: the giver, the quest, its title and text |
| `GroupScript` | `OnGroupInvite` | an invitation, answerable |
| | `OnGroupUpdated`, `OnGroupMember`, `OnGroupLeft`, `OnGroupLootMethodChanged` | the group as the server lists it, one `OnGroupMember` per other member, leaving it, its loot rules as `wxl_loot_method` |
| | `OnReadyCheck`, `OnReadyCheckResponse`, `OnReadyCheckFinished` | a ready check and its answers |
| `SocialScript` | `OnDuelRequested`, `OnDuelCountdown`, `OnDuelWon`, `OnDuelFinished` | a duel challenge (answerable), its countdown, a winner, its end |
| | `OnTradeStatus` | a trade's step as `wxl_trade_status`, with the proposer for a proposal |
| | `OnEmote`, `OnTextEmote` | an emote's animation; a `/wave` or `/dance` |
| | `OnResurrectRequest` | an offer to resurrect, answerable |
| | `OnWindowOpened`, `OnWindowClosed` | a window on someone the player talks to, as `wxl_window_kind`: merchant, trainer, bank, quest, mail, taxi, trade, ... |
| `PlayerScript` | `OnPlayerDied`, `OnPlayerReleased`, `OnPlayerResurrected` | the player's life: dead, a ghost, alive again |
| | `OnReputationChanged` | a faction's standing before and after, counted as the reputation panel counts it |
| | `OnAchievementEarned` | an achievement earned by the player or one near it |

Each parameter list is the struct of [`fields.def`](../include/wxl/fields.def) the event names in
[`events.def`](../include/wxl/events.def); `wxl::HandlerOf<WXL_EV_LootSlot>` spells it as a
function type. A handle parameter is `WXL_HANDLE_NONE` when the client does not hold the object
(out of view); its guid is there all the same. A unit's auras when it comes into view, and the
equipped items (`ObjectScript::OnEquipmentChanged`'s), are not part of these events. `SocialScript::OnTextEmote` is
raised as the server sent it, before the client consults the player's ignore list: a mod that
should not react to an ignored player checks the sender itself.

### Reads that can fail

Every read through a service answers `wxl::Expected<T>` (`include/wxl/expected.hpp`): the value,
or the `wxl_result` that stood in for it.

```cpp
wxl::Expected<int32_t> hp = unit.Health();
if (hp)                        // a value; 0 or less means the unit is dead
else if (hp.IsNotFound())      // the handle is stale: the unit is gone
else if (hp.IsUnsupported())   // this runtime has no object service
```

`*hp`, `hp->`, `hp.Value()` need `hp` to hold a value; `hp.ValueOr(0)` never does.
`hp.ToOptional()` drops the reason for code that does not care.

### Objects

`wxl::Object` and its kinds -- `Item`, `Container`, `Unit`, `Player`, `GameObject`, `DynamicObject`, `Corpse`, one per `wxl_object_type`, one header each under `wxl/objects/` -- are values over a handle: copy them,
compare them, order them, hash them, keep them across frames. `if (unit)` asks whether the unit
is still there (one call); `unit.HasHandle()` asks whether there is a handle at all (free).
`object.As<wxl::Unit>()` is a checked downcast returning `std::optional`.

Finders answer with an object that may be null -- `wxl::Player::Local()`,
`wxl::ObjectService().FromGuid(guid)` -- and reads answer with an `Expected`.

```cpp
wxl::ObjectService objects;
for (wxl::Unit unit : objects.InView<wxl::Unit>())        // a snapshot of what is in view
    ...
auto view  = objects.InView<wxl::Unit>();
auto alive = std::count_if(view.begin(), view.end(),
                           [](const wxl::Unit& u) { return u.Health().ValueOr(0) > 0; });
```

`ObjectView<T>` holds handles and builds a `T` on access, so a unit that left between the
snapshot and the visit reads as gone. `objects.Positions(view)` reads every position in one call.

A handle names one visibility episode: from the `ObjectCreate` that announces the object to the
`ObjectDestroy` that sees it leave. An object that leaves view and comes back is announced again,
as another `wxl::Object` with the same `Guid()`. Key by `Guid()` what must outlast that; key by
the object what belongs to one episode, with `wxl::ObjectMap<T>` (`wxl/objects/map.hpp`), which
erases an entry when its object leaves -- after the mod's own `OnObjectDestroy` has run:

```cpp
wxl::ObjectMap<uint32_t> hits;                                // a member of the script
void OnUnitDeath(wxl::Object unit) override { ++hits[unit]; } // gone with the unit
```

#### Reading what an object is and does

Each kind's class has a method for what the client knows of that kind (abi `0.1.18`): a unit's
level, health and power, whether it is dead, in combat, casting or mounted, its auras, how it
stands towards you; a player's money, experience, skills, bags and group; an item's durability,
enchantments and what the item cache says of it. Each answers an `Expected` of the type that fits:
a number, a `bool`, an enumeration of `wxl/enums.def` (`wxl_reaction`, `wxl_creature_type`,
`wxl_item_quality`...), another object (`wxl::Item`), or a struct (`wxl_aura`, `wxl_skill`).

Say your mod should warn when the target is a rare elite that hates you. Start from the event that
hands you the new target:

```cpp
void OnTargetChanged(wxl::Object, wxl::Object current) override
{
    const wxl::Unit unit(current);                 // the event says a target is a unit or nothing
    if (!unit) return;                             // targets nothing in view

    const wxl::Expected<wxl_classification> rank     = unit.Classification();
    const wxl::Expected<wxl_reaction>       reaction = unit.Reaction();
    if (rank && *rank == WXL_CLASSIFICATION_RARE_ELITE && reaction && *reaction <= WXL_REACTION_HOSTILE)
        wxl::Log(WXL_LOG_INFO, "alert", "a rare elite, level {}", unit.Level().ValueOr(0));
}
```

`Reaction()` asks the client's own function, the one that colours the name plate: you get the
same answer the player sees. A read that has nothing to answer says why in its `Error()`:
`WXL_E_NOT_FOUND` once the object has left, `WXL_E_INVALID` for a read its kind does not have,
`WXL_E_UNSUPPORTED` on a runtime older than the read.

Your own player has more: the client keeps money, experience, the inventory and the skills for the
player it controls only, and those reads answer `WXL_E_INVALID` for any other player.

```cpp
const wxl::Player me = wxl::Player::Local();
const uint32_t    gold = me.Money().ValueOr(0) / 10000;
if (wxl::Expected<wxl::Item> weapon = me.InventoryItem(WXL_INVENTORY_MAIN_HAND); weapon && *weapon)
{
    const uint32_t durability = weapon->Durability().ValueOr(0);
    const auto     quality    = weapon->Quality();    // WXL_E_FAILED until the item cache has it
}
for (const wxl_aura& aura : me.Auras().ValueOr(std::vector<wxl_aura>{}))
    if (aura.spell_id == 1459) ...                     // Arcane Intellect is up
```

An empty slot answers a null `wxl::Item` (`*weapon` tests false), not an error. A read of the item
cache -- `Quality()`, `Level()`, `Class()`, `SellPrice()` -- answers `WXL_E_FAILED` while the
client has not received the item's record from the server; ask again later.

#### What the player can do: `wxl.action`

The same objects carry what the player could do with them, done through the function the client
runs for a click, a key or a slash command, with the client's own checks: a spell out of range
is refused as it would be for the player.

```cpp
unit.Select();                              // targets it, as a left click does
unit.Interact();                            // a right click: talk, loot, open, use
unit.CastAt(133);                           // casts a spell the player knows at it
weapon->Use();                              // uses an item, on what it picks itself
unit.Follow();                              // as /follow
unit.Assist();                              // targets what it targets, as /assist
wxl::ActionService::ClearTarget();          // as Escape
wxl::ActionService::StopCasting();
wxl::ActionService::MoveTo(wxl_vec3{-8913.0f, 554.0f, 93.0f}); // click-to-move to a point
wxl::ActionService::Face(1.57);             // turns the player to face west, as mouse look does
```

`Face` (abi `0.1.23`) takes radians as the client counts them: 0 faces north (+x), and the angle
grows towards the west (+y). The character turns at once and the server is told, as when the
player steers with the right mouse button; it is refused (`WXL_E_FAILED`) while the client would
not let the player turn: dead, stunned, on a flight path.

Each answers a `wxl_result`: `WXL_OK` when the client took the action, `WXL_E_FAILED` when it
refused it, `WXL_E_UNSUPPORTED` out of the world. An action runs on the thread that runs the game,
where your handlers of world events run; from any other thread it is refused with `WXL_E_INVALID`
and the log says why.

#### Every member

Each class reads what its kind holds, every method answering an `Expected`; a class has its
parents' members too (a `Player` is a `Unit` is an `Object`). The tables are made from the same
rows as the methods, by `wxl-object-members`.

<!-- object-members:cxx (written by wxl-object-members) -->

**`wxl::Object`**: Every object

| Method | Answers | What |
|---|---|---|
| `Position()` | `wxl_vec3` | Where it stands in the world, in yards; an item has no place. |
| `Facing()` | `float` | The way it faces, in radians. |
| `Entry()` | `uint32_t` | The server's template id: a creature, item or game-object entry; 0 for a player. |
| `Scale()` | `float` | Its size, 1 being the model's own. |
| `IsActivePlayer()` | `bool` | Whether it is the unit the client controls. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `Distance(const Object& other)` | `float` | Yards from it to another object, centre to centre; invalid when either has no place in the world. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `DistanceTo(wxl_vec3 point)` | `float` | Yards from it to a point; invalid for an object with no place in the world. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |

**`wxl::Unit`**: Units, players included

| Method | Answers | What |
|---|---|---|
| `Health()` | `int32_t` | Its health; 0 or less is a dead unit, as the client reads it. |
| `MaxHealth()` | `uint32_t` | Its maximum health. |
| `Level()` | `uint32_t` | Its level. |
| `PowerValue(Power power)` | `uint32_t` | Its power of a type (0 mana, 1 rage, 2 focus, 3 energy, 4 happiness, 5 runes, 6 runic power). |
| `MaxPowerValue(Power power)` | `uint32_t` | Its maximum power of a type. |
| `PowerType()` | `Power` | The type of power it runs on, as PowerValue takes it. |
| `Race()` | `uint32_t` | Its race, as ChrRaces.dbc numbers it. |
| `Class()` | `uint32_t` | Its class, as ChrClasses.dbc numbers it. |
| `Sex()` | `uint32_t` | 0 male, 1 female, 2 none. |
| `DisplayId()` | `uint32_t` | The model it shows now, a CreatureDisplayInfo.dbc id. |
| `NativeDisplayId()` | `uint32_t` | The model it shows when nothing changes its shape. |
| `MountDisplayId()` | `uint32_t` | The model of its mount; 0 while not mounted. |
| `FactionTemplate()` | `uint32_t` | Its FactionTemplate.dbc id. |
| `Flags()` | `uint32_t` | UNIT_FIELD_FLAGS as the server sends it. |
| `Flags2()` | `uint32_t` | UNIT_FIELD_FLAGS_2 as the server sends it. |
| `DynamicFlags()` | `uint32_t` | UNIT_DYNAMIC_FLAGS as the server sends it to this client. |
| `NpcFlags()` | `uint32_t` | What it offers to talk about: vendor, trainer, quest giver, ... as UNIT_NPC_FLAGS. |
| `BoundingRadius()` | `float` | The radius of its body, in yards. |
| `CombatReach()` | `float` | How far it reaches in melee, in yards. |
| `CharmGuid()` | `wxl_guid` | The unit it charms, or 0. |
| `SummonGuid()` | `wxl_guid` | The unit it summoned, its pet among them, or 0. |
| `CharmedByGuid()` | `wxl_guid` | Who charms it, or 0. |
| `SummonedByGuid()` | `wxl_guid` | Who summoned it: a pet's owner, or 0. |
| `CreatedByGuid()` | `wxl_guid` | Who created it: a totem's shaman, or 0. |
| `TargetGuid()` | `wxl_guid` | What it targets, as a guid, whether that is in view or not; 0 for nothing. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `Reaction()` | `wxl_reaction` | How it stands towards the active player, by the client's own reckoning: the colour of its name. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `ReactionTo(const Object& other)` | `wxl_reaction` | How it stands towards another unit, by the client's own reckoning. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsDead()` | `bool` | Whether it is dead: no health left, or marked dead by the server. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `InCombat()` | `bool` | Whether it is in combat, as the client's UnitAffectingCombat reads it. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsPvp()` | `bool` | Whether it is flagged for player-versus-player combat. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsTapped()` | `bool` | Whether someone has tapped it: its kill and loot are theirs. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsTappedByPlayer()` | `bool` | Whether the active player, or their group, has tapped it. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsLootable()` | `bool` | Whether it has loot for the active player. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsSkinnable()` | `bool` | Whether its corpse can be skinned. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `StandState()` | `wxl_stand_state` | How it stands: standing, sitting, sleeping, kneeling, ... Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsSitting()` | `bool` | Whether it sits, on the ground or in a chair. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsStealthed()` | `bool` | Whether it is stealthed, as the client's IsStealthed reads it. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsMounted()` | `bool` | Whether it rides a mount, as the client's IsMounted reads it. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `CastingSpellId()` | `uint32_t` | The spell it casts now; 0 for none. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `CastStart()` | `uint32_t` | When the cast began, in milliseconds on the client's clock (the client's GetTime() * 1000). Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `CastEnd()` | `uint32_t` | When the cast ends, on the same clock. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsCasting()` | `bool` | Whether it is casting, its cast not yet over, as the client's UnitCastingInfo reads it. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `ChannelSpellId()` | `uint32_t` | The spell it channels now; 0 for none. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `ChannelStart()` | `uint32_t` | When the channel began, on the client's clock. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `ChannelEnd()` | `uint32_t` | When the channel ends, on the client's clock. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsChanneling()` | `bool` | Whether it is channeling, its channel not yet over. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `AuraCount()` | `uint32_t` | How many auras the client holds on it, buffs and debuffs alike. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `Aura(uint32_t slot)` | `wxl_aura` | The aura in a slot, 0 to AuraCount - 1: its spell, caster, stacks, duration, expiration and flags. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `HasAura(uint32_t spell_id)` | `bool` | Whether an aura of a spell is on it. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `CreatureType()` | `wxl_creature_type` | What kind of creature it is, as the client works it out: beast, humanoid, undead, ... Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `CreatureFamily()` | `uint32_t` | Its beast family, a CreatureFamily.dbc id; 0 for none. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `Classification()` | `wxl_classification` | Its rank: normal, elite, rare, rare elite, world boss. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `KnowsSpell(uint32_t spell_id)` | `bool` | Whether it knows a spell: the active player's spell book or its pet's; false for any other unit. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `MovementFlags()` | `uint32_t` | The flags of its movement: forward 0x1, backward 0x2, ..., falling 0x1000, flying 0x2000000. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `Speed(wxl_move_type type)` | `float` | One of its speeds, in yards a second: walk, run, swim, flight, ...; the turn and pitch rates in radians a second. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsSwimming()` | `bool` | Whether it swims, as the client's IsSwimming reads it. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsFlying()` | `bool` | Whether it flies, as the client's IsFlying reads it. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsFalling()` | `bool` | Whether it is falling, as the client's IsFalling reads it. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsOutdoors()` | `bool` | Whether it stands outdoors, as the client's IsOutdoors reads it: false in a building or a cave the world marks indoors, where a mount is refused. Came with abi 0.1.23: WXL_E_UNSUPPORTED on an older runtime. |

**`wxl::Player`**: Players

| Method | Answers | What |
|---|---|---|
| `Flags()` | `uint32_t` | PLAYER_FLAGS as the server sends it. |
| `GuildId()` | `uint32_t` | Its guild's id; 0 while unguilded. |
| `GuildRank()` | `uint32_t` | Its rank in the guild, 0 for the guild master. |
| `ChosenTitle()` | `uint32_t` | The title it shows, a CharTitles.dbc bit index; 0 for none. |
| `VisibleItem(EquipSlot slot)` | `uint32_t` | The item id an equipment slot shows (0 head .. 18 tabard); 0 when the slot is empty. |
| `IsGhost()` | `bool` | Whether it is a ghost, released from its corpse. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsAfk()` | `bool` | Whether it is away from the keyboard. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsDnd()` | `bool` | Whether it asked not to be disturbed. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `IsResting()` | `bool` | Whether it rests in an inn or a city, gaining rested experience. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `Money()` | `uint32_t` | Its money, in copper. The active player only: WXL_E_INVALID for another player. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `Xp()` | `uint32_t` | Its experience in this level. The active player only: WXL_E_INVALID for another player. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `NextLevelXp()` | `uint32_t` | The experience this level takes. The active player only: WXL_E_INVALID for another player. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `RestedXp()` | `uint32_t` | Its rested experience: kills give twice as much until it is spent. The active player only: WXL_E_INVALID for another player. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `HonorPoints()` | `uint32_t` | Its honor points. The active player only: WXL_E_INVALID for another player. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `ArenaPoints()` | `uint32_t` | Its arena points. The active player only: WXL_E_INVALID for another player. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `InventoryItem(wxl_inventory_slot slot)` | `Item` | The item in an equipment slot, or the bag in a bag slot (19 to 22); none for an empty slot. The active player only: WXL_E_INVALID for another player. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `BackpackItem(uint32_t slot)` | `Item` | The item in a slot of the backpack, 0 to 15; none for an empty slot. The active player only: WXL_E_INVALID for another player. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `Skill(uint32_t line)` | `wxl_skill` | The skill on a line of the skill list, 0 to 127: its id, value and maximum; an id of 0 for an empty line. The active player only: WXL_E_INVALID for another player. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `SkillOf(uint32_t skill_id)` | `wxl_skill` | A skill by its SkillLine.dbc id; invalid for a skill the player does not have. The active player only: WXL_E_INVALID for another player. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `PartySize()` | `uint32_t` | How many others are in its party, 0 to 4. The active player only: WXL_E_INVALID for another player. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `PartyMemberGuid(uint32_t slot)` | `wxl_guid` | The other party member in a party slot, 0 to 3, whether in view or not; 0 for an empty slot. The active player only: WXL_E_INVALID for another player. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `RaidSize()` | `uint32_t` | How many are in its raid, itself included; 0 outside a raid. The active player only: WXL_E_INVALID for another player. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `RaidMemberGuid(uint32_t slot)` | `wxl_guid` | The raid member in a raid slot, 0 to 39, whether in view or not; 0 for an empty slot. The active player only: WXL_E_INVALID for another player. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |

**`wxl::Item`**: Items, containers included

| Method | Answers | What |
|---|---|---|
| `OwnerGuid()` | `wxl_guid` | The player who owns it. |
| `ContainedInGuid()` | `wxl_guid` | The bag holding it, or 0 in the backpack or when equipped. |
| `CreatorGuid()` | `wxl_guid` | Who crafted it, or 0. |
| `StackCount()` | `int32_t` | How many the stack holds. |
| `Duration()` | `uint32_t` | Seconds left before it expires; 0 when it does not. |
| `Flags()` | `uint32_t` | ITEM_FIELD_FLAGS as the server sends it: soulbound 0x1, ... |
| `Durability()` | `uint32_t` | Its durability now. |
| `MaxDurability()` | `uint32_t` | Its durability when repaired; 0 for an item that does not wear. |
| `Enchant(uint32_t slot)` | `uint32_t` | The enchantment in a slot, 0 to 11 (0 permanent, 1 temporary, 2 to 4 sockets, ...), a SpellItemEnchantment.dbc id; 0 for none. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `Quality()` | `wxl_item_quality` | Its quality: poor, common, uncommon, rare, epic, legendary, artifact, heirloom. WXL_E_FAILED until the client has the item's record from the server. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `Level()` | `uint32_t` | Its item level. WXL_E_FAILED until the client has the item's record from the server. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `RequiredLevel()` | `uint32_t` | The level it takes to use. WXL_E_FAILED until the client has the item's record from the server. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `Class()` | `uint32_t` | Its class, an ItemClass.dbc id: weapon 2, armor 4, ... WXL_E_FAILED until the client has the item's record from the server. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `Subclass()` | `uint32_t` | Its subclass within the class: a sword, a cloth robe, ... WXL_E_FAILED until the client has the item's record from the server. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `InventoryType()` | `uint32_t` | Where it is worn: head 1, neck 2, ...; 0 for an item that is not worn. WXL_E_FAILED until the client has the item's record from the server. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `MaxStack()` | `uint32_t` | How many a stack of it holds at most. WXL_E_FAILED until the client has the item's record from the server. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |
| `SellPrice()` | `uint32_t` | What a vendor pays for one, in copper. WXL_E_FAILED until the client has the item's record from the server. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |

**`wxl::Container`**: Containers

| Method | Answers | What |
|---|---|---|
| `NumSlots()` | `uint32_t` | How many slots the bag has. |
| `SlotGuid(uint32_t slot)` | `wxl_guid` | The item in a slot, as a guid; 0 for an empty one, invalid past NumSlots. |
| `SlotItem(uint32_t slot)` | `Item` | The item in a slot of the bag; none for an empty one, invalid past NumSlots. Came with abi 0.1.18: WXL_E_UNSUPPORTED on an older runtime. |

**`wxl::GameObject`**: Game objects

| Method | Answers | What |
|---|---|---|
| `CreatedByGuid()` | `wxl_guid` | Who summoned or spawned it, or 0 for one placed in the world. |
| `DisplayId()` | `uint32_t` | Its model, a GameObjectDisplayInfo.dbc id. |
| `Flags()` | `uint32_t` | GAMEOBJECT_FLAGS: in use 0x1, locked 0x2, ... |
| `DynamicFlags()` | `uint32_t` | GAMEOBJECT_DYNAMIC's flags, as the server sends them to this client. |
| `Faction()` | `uint32_t` | Its FactionTemplate.dbc id. |
| `Level()` | `uint32_t` | Its level. |
| `State()` | `uint32_t` | 0 active, 1 ready, 2 destroyed. |
| `GoType()` | `uint32_t` | The game-object type, in the server's numbering (door 0, button 1, chest 3, ...). |
| `ArtKit()` | `uint32_t` | The art kit it shows. |

**`wxl::DynamicObject`**: Dynamic objects

| Method | Answers | What |
|---|---|---|
| `CasterGuid()` | `wxl_guid` | The unit that cast it. |
| `SpellId()` | `uint32_t` | The spell it is the area of. |
| `Radius()` | `float` | Its radius, in yards. |
| `CastTime()` | `uint32_t` | The client time it was cast at, in milliseconds. |

**`wxl::Corpse`**: Corpses

| Method | Answers | What |
|---|---|---|
| `OwnerGuid()` | `wxl_guid` | The player it belongs to. |
| `PartyGuid()` | `wxl_guid` | The party the owner was in when they died, or 0. |
| `DisplayId()` | `uint32_t` | Its model. |
| `Guild()` | `uint32_t` | The owner's guild id. |
| `Flags()` | `uint32_t` | CORPSE_FIELD_FLAGS: bones 0x1, ... |
| `DynamicFlags()` | `uint32_t` | CORPSE_FIELD_DYNAMIC_FLAGS: lootable 0x1. |

<!-- /object-members:cxx -->

### Descriptor fields

Every object carries a block of dwords the server fills: its descriptor fields, all of them in
[`include/wxl/update_fields.def`](../include/wxl/update_fields.def) with the client's names, dword
indices, sizes and what they hold. `wxl/objects/fields.hpp` turns each row into a typed constant,
`wxl::field::<Kind>::<Name>` -- `UNIT_FIELD_HEALTH` is `wxl::field::Unit::Health`, `PLAYER_XP`
`wxl::field::Player::Xp` -- and `Field` reads it as the type its row says (abi `0.1.9`; on an older
runtime every call answers `WXL_E_UNSUPPORTED`):

```cpp
wxl::Expected<int32_t> health = unit.Field(wxl::field::Unit::Health);
wxl::Expected<float>   arcane = unit.Field(wxl::field::Unit::Resistances, 6); // element 6 of 7
wxl::Expected<wxl_guid> aim   = unit.Field(wxl::field::Unit::Target);
auto bytes = unit.Field(wxl::field::Unit::Bytes0);  // std::array<uint8_t, 4>: race, class, sex, power
```

A field of a kind the object is not -- a unit's field asked of a game object -- answers
`WXL_E_INVALID`, as does a player other than the active one asked for a field past
`PLAYER_FIELD_PAD_0`: the client keeps those for the player it controls only. `Fields(field)`
reads every element, `RawFields(first, count)` the dwords by index, and
`wxl::ObjectService().Fields(view, field)` one field of a whole view in one call.

`SetField(field, value)` writes the client's copy. The server's next update of the field puts its
own value back, and nothing else learns of the write: no handler of the client runs and no model
is rebuilt. The guid and the type are refused.

To hear of a change, ask for the field once, then override `OnObjectFieldChanged` in an
`ObjectScript`:

```cpp
wxl::ObjectService().Watch(wxl::field::Unit::Health);   // in the mod's OnLoad, say

void OnObjectFieldChanged(wxl::Object object, wxl_object_type, uint32_t field, uint32_t,
                          wxl::FieldValue was, wxl::FieldValue now) override
{
    if (field == wxl::field::Unit::Health.index && now.As(wxl::field::Unit::Health) == 0)
        ... // a unit's health reached 0
}
```

`Watch` takes the field's own kind -- units and players, here -- or the type given
(`Watch(wxl::field::Unit::Health, WXL_OBJECT_PLAYER)`), and nothing is raised for a field nobody
asked for. The client sees a change by comparing a field with a copy it keeps from before the
update, which it keeps for most fields and not all: the `watch` column of `update_fields.def` says
which, and `Watch` answers `WXL_E_UNSUPPORTED` for the others, naming them in the log.

The watch is your mod's: your handlers receive the changes of the fields you watch, and another
mod's watches do not reach them. Each `Watch` call is one watch, given back by one
`Unwatch(wxl::field::Unit::Health)` (abi `0.1.10`), and every watch you still hold is given back
when your mod is disabled or unloaded. The simplest way to hold one is a `wxl::FieldWatch`: it
watches from its construction and gives the watch back in its destructor, so as a member of your
script it lasts exactly as long as the script:

```cpp
class Health final : public wxl::ObjectScript
{
    wxl::FieldWatch watch_{wxl::field::Unit::Health};   // watched while this script lives

    void OnObjectFieldChanged(wxl::Object, wxl_object_type, uint32_t field, uint32_t,
                              wxl::FieldValue, wxl::FieldValue now) override { ... }
};
```

`if (!watch_)` tells you the watch was refused, and `watch_.Result()` why.

The short names of the constants are the client's names with the kind's prefix dropped and each
word capitalised. Where the client runs words together, the name splits them the way the getters
do: `UNIT_FIELD_MAXHEALTH` is `wxl::field::Unit::MaxHealth`, as `unit.MaxHealth()` reads it, and
`UNIT_FIELD_BOUNDINGRADIUS` is `BoundingRadius`. A field and a getter of one name always read the
same value, in the same type: health is an `int32_t` both ways, as the client holds it (it takes
0 or less for dead).

### Services

Each service has a typed facade with its name and version sealed in: `wxl::ObjectService`,
`wxl::ScriptService`, `wxl::StorageService`, `wxl::DataService`, `wxl::NetService`,
`wxl::UiService`, `wxl::HookService`, `wxl::RawHookService`, `wxl::PatchService`, `wxl::PointService`,
`wxl::ActionService`, `wxl::M2Service`, `wxl::M2FileService`, `wxl::GxService`,
`wxl::CameraService`, `wxl::InputService`, `wxl::WorldService`, `wxl::PoseService`,
`wxl::ImGuiService` (`include/wxl/services/imgui.hpp`, which compiles Dear ImGui in: not in
`services.hpp`)
(`include/wxl/services/*.hpp`, or all of them through `include/wxl/services.hpp`). A facade is stateless -- construct one
wherever it is needed -- and resolves its table on first use. It tests false while the runtime
does not publish the service, and every call through it then answers `WXL_E_UNSUPPORTED` or the
empty object.

```cpp
wxl::ScriptService script;
script.RegisterFunction<&Greet>("Greet");       // void Greet(wxl::ScriptCall&)
script.RegisterFunction<&Api::Count>("Count", this); // this->Count(wxl::ScriptCall&)
script.RegisterFunction("Add", &adder);          // (*adder)(wxl::ScriptCall&)
script.FireEvent("MY_EVENT", {wxl::Value::Number(1), wxl::Value::String("x")});

wxl::StorageService storage;
if (wxl::Expected<std::string> text = storage.ReadText("Interface\\x.lua")) ...
```

The plain methods of a facade -- a string or a number in, a result, a bool, a string or an
`Expected` out -- are not written in its header: each is a row of the service's table, beside its
C header (`include/wxl/services/storage.def` for `wxl.storage`), and the facade takes it from
there. The row names the C member and what it takes and answers, so the method calls the member
when the runtime's table reaches it and answers as a missing service does when it does not:

```cpp
// storage.def:
//   WXL_API(storage, add_search_path, AddSearchPath, result, (str directory, i32 priority), ...)
wxl::StorageService storage;
if (storage.AddSearchPath("Mods\\mymod\\assets", 1) != WXL_OK) ...   // a wxl_result
wxl::Expected<uint64_t> size = storage.Size("Interface\\x.blp");       // out<u64>: an Expected

wxl::DataService data;
if (!data.IsLoaded("Spell.dbc")) data.ServeTable("Spell.dbc", "Mods\\mymod\\Spell.dbc");
```

A folder added to the search is your mod's. `storage.RemoveSearchPath("Mods\\mymod\\assets")`
(abi `0.1.10`) takes it back out, and the runtime takes out every folder your mod still has in the
search when the mod is disabled or unloaded: a mod that goes leaves no folder behind for the
others to trip on. A folder another mod added is its own; removing it answers `WXL_E_NOT_FOUND`.

To have the client open one file in place of another, redirect the name rather than answer
`OnFileOpen`: the client opens most files on its loading threads, and a redirect is answered by the
runtime on every thread, before any handler runs. `storage.Redirect(name, other)` (abi `0.1.15`)
makes every open of `name` open `other`, found as any name is; `storage.Unredirect(name)` takes it
back, and the runtime takes back what your mod still holds when it is disabled or unloaded. Names
are compared as the client compares them, in any case and with either slash; when two mods
redirect one name, the later wins and taking it back uncovers the earlier. An `OnFileOpen` handler
sees the redirect's answer in `redirect` and may still write its own.

What C++ does better than the raw table stays written by hand in the header: `Read` into a
vector, `Open` as a `wxl::File` that closes itself, `Find` as a vector of strings, the templates
of `RegisterFunction`. The same rows build the Lua modules of `wxl.lua`, so a member reaches C++
and Lua alike from its one row.

`wxl::Service<T>(name, version)` still gives the raw table, for a service these do not cover.

### Serving and converting assets

The client reads every file it uses -- models, textures, terrain, sounds, the interface, its
databases -- by name, from its archives. Your mod can stand among those archives in three ways,
from the simplest to the most capable:

| Way | What you give | Good for |
|---|---|---|
| a search folder (`AddSearchPath`) | a folder of files inside the client's folder | files you ship with the mod |
| served bytes (`Serve`) | a name and its bytes, copied | a few files you make at run time |
| a provider (`AddProvider`) | an object the client asks for files | files kept elsewhere: an archive format the client does not read, a cache, a generator |

And one way to change what the client reads, whichever of those it comes from: a **transform**
(`AddTransform`) receives a kind of file's bytes once they are read and before the client parses
them, and may answer other bytes.

Two rules hold for all of them. **Priority**: above 0, your file wins a name over the client's own
data; 0 or below, it only fills a name the client does not have. **Threads**: the client opens
most files on its loading threads, several at once, so a provider and a transform are called on
any thread, concurrently, and must not touch anything the main thread changes without a lock.
That is why they are C++ only; a Lua mod uses `Serve`, whose bytes the runtime holds.

The example `examples/asset-provider` serves an add-on, HelloProvider, that exists in the mod's
memory and nowhere on the disk.

**`examples/asset-provider/src/AssetProvider.cpp`**

```cpp
    /// The add-on's files, by name.
    class HelloAddOn final : public wxl::AssetProvider
    {
    public:
        HelloAddOn()
        {
            files_[Key("Interface\\AddOns\\HelloProvider\\HelloProvider.toc")] =
                "## Interface: 30300\n## Title: HelloProvider\nHelloProvider.lua\n";
            files_[Key("Interface\\AddOns\\HelloProvider\\HelloProvider.lua")] =
                "DEFAULT_CHAT_FRAME:AddMessage(\"Hello from a provider, version $VERSION\")\n";
        }

        // Asked at every open and existence check: a lookup, nothing more.
        wxl::AssetState Lookup(std::string_view path) override
        {
            return files_.count(Key(path)) ? wxl::AssetState::Present : wxl::AssetState::Missing;
        }

        // The work: here, a copy of the text. A provider over an archive would read it here.
        std::unique_ptr<wxl::AssetFile> Open(std::string_view path) override
        {
            const auto found = files_.find(Key(path));
            if (found == files_.end()) return nullptr;
            const std::string& text = found->second;
            return std::make_unique<wxl::MemoryAssetFile>(std::vector<uint8_t>(text.begin(), text.end()));
        }

        // The client lists Interface\AddOns from the disk; what is told here joins that list.
        void List(std::string_view folder, const std::function<void(std::string_view, bool)>& entry) override
        {
            if (Key(folder) == "interface\\addons") entry("HelloProvider", true);
        }

    private:
        std::map<std::string, std::string> files_;
    };
```

`Lookup` is asked at every open and every existence check the provider could win, often from
several threads at once: keep it a lookup. It answers `wxl::AssetState::Present`, `Missing`, or
`Pending` for a file the provider will have but has not yet -- one still being downloaded, say.
A pending name is looked for further down meanwhile (the next provider, the client's own files);
the client sees no such file if nobody has it, and a mod reading it through `wxl.storage` gets
`WXL_E_PENDING`, not `WXL_E_NOT_FOUND`, so it can try again later.

`Open` does the work and answers a `wxl::AssetFile`: its size, and `ReadAt(offset, buffer, size)`.
`wxl::MemoryAssetFile` is one over a vector; a provider over a large archive writes its own, which
reads a piece at a time: the client may read a sound or a movie a little at a time. The client
closes the file when it is done, which destroys the object.

`List` is optional. The client lists a few of its folders from the disk itself -- the add-ons, the
macro icons -- and what `List` tells joins those listings: that is how the client finds
HelloProvider among its add-ons, then reads its `.toc` and its `.lua` through the provider. What
`List` tells is also there when the client asks about one entry, a folder or a file, as the
add-on list does of each name it is told before it keeps it.
`StorageService::Find` walks the providers' folders too.

A transform is a function, or an object deriving from `wxl::AssetTransform`:

**`examples/asset-provider/src/AssetProvider.cpp`**

```cpp
    /// Replaces $VERSION in the add-on's Lua; every other Lua file goes by untouched.
    bool FillVersion(std::string_view path, const uint8_t* data, size_t size, const wxl::AssetOutput& out)
    {
        if (Key(path) != "interface\\addons\\helloprovider\\helloprovider.lua") return false;
        std::string text(reinterpret_cast<const char*>(data), size);
        const size_t at = text.find("$VERSION");
        if (at == std::string::npos) return false;
        text.replace(at, 8, kVersion);
        return out.Replace(text.data(), text.size());
    }
```

It is registered for a kind of file, the extension without its dot, and optionally the file's
first four bytes (`wxl::Magic("MD20")`, 0 for any). Answering `true` after `out.Replace` gives the
client other bytes; answering `false` leaves them as they came. Several transforms of one kind run
in priority order, highest first, each given what the one before left, so their sizes may change
freely. A transform sees every file of its kind the client reads, from any source: check the name
first and leave the others alone, as `FillVersion` does.

**`examples/asset-provider/src/AssetProvider.cpp`**

```cpp
        wxl_result OnLoad() override
        {
            const wxl::StorageService storage;
            // Priority 1: above the client's own files, which are at 0.
            auto provider = storage.AddProvider(addOn_, 1);
            if (!provider)
            {
                wxl::Log(WXL_LOG_WARN, kTag, "no provider: {}", wxl::ResultName(provider.Error()));
                return WXL_OK;
            }
            provider_ = std::move(*provider);

            // Every .lua file the client reads passes through here, so the check is cheap.
            auto transform = storage.AddTransform("lua", 0, 0, &FillVersion);
            if (transform) transform_ = std::move(*transform);
            wxl::Log(WXL_LOG_INFO, kTag, "HelloProvider served; look for it in the add-on list");
            return WXL_OK;
        }
```

`AddProvider` and `AddTransform` answer a `wxl::AssetRegistration`, which takes the registration
back when it goes. The runtime takes back whatever your mod still holds when it is disabled or
unloaded: it waits for the calls in progress, and the provider closes the files the client still
has open from it (their next reads fail). The provider object must outlive its registration; here
both are members of the mod.

`StorageService().Serve(name, text, priority)` serves bytes without a provider: the runtime keeps a
copy and answers every thread from it until `Unserve(name)` or the mod goes. When two mods serve
one name, the higher priority wins, then the later one.

What the client then does with what it read can be watched through `AssetScript`'s events:
`OnTerrainTileLoad` (a map tile's file, with its place on the grid), `OnMapObjectGroupLoad` (a
building's group file), `OnSkinProfileLoad` (a model's `.skin`), `OnTextureDecode` (a texture's
size, encoding and alpha, from its header), `OnShaderLoad` and `OnSoundFileLoad` (abi `0.1.19`).
They come on the main thread, and only while someone listens. The bytes they show are the ones
transforms left; to change bytes, write a transform. `OnModelLoad` still lets a handler answer a
model's bytes on the main thread, after the transforms of `m2` ran.

### Assets on demand: missing names, hashes, paths of your own, overrides, reloads, services

A mod that brings assets in while the game runs -- downloads them, builds them, gets them from
another mod -- rests on six pieces of the platform. Here they are in the order such a mod uses them.

**1. What the client did not find.** `AssetScript::OnFileMissing` comes whenever the client looks
for a name and no layer has it (archives, disk, search folders, providers, served bytes):

```cpp
class Wants final : public wxl::AssetScript
{
public:
    void OnFileMissing(std::string_view path, wxl_asset_kind kind, bool open, bool pending,
                       bool loader) override
    {
        // Often a loader thread (loader): hand the name to your own thread, do not fetch here.
        if (open && kind == WXL_ASSET_MODEL) queue_.Push(std::string(path));
    }
};
```

`kind` is what the file is for, by its extension; `open` says the client wanted to read it (false:
it only asked whether it exists); `pending` says a provider of yours answered "not yet". The event
is raised for the client's asks only, never for your own `StorageService` calls, and once per ask:
the client probes optional files, so the same name may come again and many never matter.

**2. "Not yet."** A provider whose `Lookup` answers `wxl::AssetState::Pending` lets the client go on
without the file (it finds nothing, or the next layer's) and makes `OnFileMissing` carry `pending`.
When the bytes arrive, the provider answers `Present` from then on.

**3. A path of your own.** A file you bring in goes under the path you choose. Many paths are the
same in this client and in later ones, with other contents: a new path lives beside the client's
file, and the same path replaces it only when you say so (5, below). Serve it, or have your provider
answer it:

```cpp
const wxl::StorageService storage;
storage.Serve("Creature\\Murloc\\MurlocModern.m2", bytes.data(), size, 0); // beside the client's Murloc.m2
```

The client keeps its models by file name alone, without the folder: give a model of your own a file
name of its own.

**4. What a file holds.** `HashFile` answers the MD5 of a file as the client would read it, from the
layer that has it, with its length -- the key a content network files it under:

```cpp
const auto hash = storage.HashFile("Creature\\Murloc\\Murloc.m2");
if (hash && hash->Hex() != expected) { /* the client's copy differs: fetch ours */ }
```

`hash->layer` (`WXL_FILE_LAYER_ARCHIVE`, `_DISK`, `_FOLDER`, `_PROVIDER`, `_SERVED`) and
`hash->source` (the archive, the disk path, or the mod that serves it) say where the bytes came
from. The digest is computed by Windows' own MD5 (CNG, `BCrypt`) and kept by name and layer;
`HashFile(name, true)` reads again. `HashFileAsync(name, [](wxl::Expected<wxl::FileHash> hash) {...})`
hashes on the runtime's thread and calls you back on the main thread.

**5. Replacing, explicitly, and showing it.** `OverrideFile(name, other)` puts `other` in place of
`name` for every open and every existence check, on every thread, above every layer, until
`RestoreFile(name)` or your mod goes. Then `ReloadAsset(name)` makes the client read what it holds
again:

```cpp
storage.OverrideFile("Creature\\Murloc\\Murloc.m2", "Creature\\Murloc\\MurlocModern.m2");
const auto done = storage.ReloadAsset("Creature\\Murloc\\Murloc.m2");
// done->models: dropped from the model cache; done->objects: units made again with the new file
```

A texture (`.blp`) is read again in place; a model (`.m2`, `.mdx`, a `.skin`) is dropped from the
client's model cache -- every cached model of that file name, since the cache knows models by file
name -- and the units and players in view drawn with it are made again. Map tiles, buildings and
sounds are read again when the client next loads them (`WXL_E_UNSUPPORTED` here), and so is a
character's baked skin; `WXL_E_PENDING` means the texture is still loading (ask again in a
moment). A small square interface texture (64 pixels or less: icons, most buttons) is kept in a
block of the client's shared atlas, which is not read again in place: `WXL_E_UNSUPPORTED`. Main
thread only.

**6. Services between mods.** A fetching mod publishes a service; the mods that want assets call it
and are answered later, on the main thread:

```cpp
// The fetcher.
auto published = wxl::RelayService().Publish("my.fetcher.fetch",
    [](const wxl::RelayRequest& request, std::string_view method, wxl::RelayValues args) {
        // Keep the request (it is a value: copy it to your thread) and answer when ready:
        // request.Answer({wxl::Value::String(localPath), wxl::Value::String(md5)});
        // or request.Fail("no such file");
    });

// A consumer.
wxl::RelayService().Call("my.fetcher.fetch", "get", {wxl::Value::String("Creature\\Murloc\\Murloc.m2")},
    [](wxl_result result, wxl::RelayValues values) {
        if (result == WXL_OK) wxl::Log(WXL_LOG_INFO, "consumer", "ready as {}", values.String(0));
    });
```

`Answer` and `Fail` may be called from any thread; requests, answers and cancellations are handed
over at the next update, never from inside the call that made them. Values are numbers, strings,
booleans and nil, copied. The `RelayPublication` takes the service back when it goes. Either side
may go with requests in flight: a service that goes fails what it had not answered
(`WXL_E_NOT_FOUND`), a caller that goes has its requests cancelled (the service's `cancelled`
function is told), and nothing is ever handed to a mod that went. All of this came with abi
`0.1.22`.

### Models: what characters wear, models of your own, model files

Three things about models reach a mod, from the simplest to the most technical. This part of the
platform is the work of Tyrallis, who contributed it for his own mods: an equipment mod, an outline
mod and a mod that teaches the client a later expansion's model format.

| Want to | Use | Where |
|---|---|---|
| know what a character puts on and takes off | `ObjectScript::OnAppearanceChanged` | every runtime |
| hang a model of your own on a character, a creature, a mount | `wxl::M2Service`, `wxl::M2Model` | every runtime |
| reshape a model file as the client loads it | `AssetScript::OnModelParsed` and the stages after it, `wxl::M2FileService` | every runtime |
| draw a batch of a model again, with your own state | `RenderScript::OnModelBatchDrawn`, `wxl::GxService` | every runtime |

The three services are **raw**: they hand you the client's own models and memory, which no
check stands between. A native mod names each one it uses in its manifest, as any service.

**What characters wear.** A character's appearance is what the client dresses: every player, every
NPC dressed from its display, and every copy the interface makes of one (the character panel,
portraits, character select). `OnAppearanceChanged` comes when one puts an item display on a slot
(`WXL_APPEARANCE_WORN`, once the client's own models for it hang on the body), is about to take
one off (`WXL_APPEARANCE_REMOVED`), and when the appearance goes away (`WXL_APPEARANCE_FREED`: drop
whatever you kept for it). `wearer` names the appearance until then; `model` is its body, what
items hang on. `wxl::M2Service::ItemDisplay(id)` reads the display's row of `ItemDisplayInfo.dbc`.

**`examples/murloc-hat/src/MurlocHat.cpp`**

```cpp
    /// Logs the first items characters put on, with the model file the item display names.
    class Wardrobe final : public wxl::ObjectScript
    {
    public:
        void OnAppearanceChanged(wxl_appearance_change change, void*, void*, uint32_t, uint32_t, uint32_t slot,
                                 uint32_t display, bool) override
        {
            if (change != WXL_APPEARANCE_WORN || told_ >= 10) return;
            ++told_;
            const wxl::Expected<wxl_m2_item_display> row   = wxl::M2Service::ItemDisplay(display);
            const char*                              model = row && *row->model[0] ? row->model[0] : "no model";
            wxl::Log(WXL_LOG_INFO, kTag, "slot {} puts on display {} ({})", slot, display, model);
        }

    private:
        int told_ = 0;
    };
```

**A model of your own.** `wxl::M2Model(scene, path)` makes a model of a file in the scene of a
model you name, and gives it back when the object goes. `Attach(parent, point, flags)` hangs it on
another model at one of its attachment points: 11 is the top of a character's head, 1 and 2 its
hands, 32 its hip. The example hangs a murloc on the player's head:

**`examples/murloc-hat/src/MurlocHat.cpp`**

```cpp
        void OnUpdate(float delta) override
        {
            if (hat_ && hat_.Parent()) return; // still on the head
            wait_ -= delta;
            if (wait_ > 0.0f) return;
            wait_ = 1.0f; // one try a second while it is not

            // The model the player draws with: its mount's, while it rides one.
            void* body = wxl::M2Service::ObjectModel(wxl::Player::Local());
            if (!body) return; // not in the world yet

            if (!hat_) hat_ = wxl::M2Model(body, "Creature\\Murloc\\Murloc.m2");
            const wxl_result hung = hat_.Attach(body, kHead);
            if (hung == WXL_OK)
                wxl::Log(WXL_LOG_INFO, kTag, "a murloc sits on the player's head");
            else
                wxl::Log(WXL_LOG_WARN, kTag, "the murloc could not be hung: {}", wxl::ResultName(hung));
        }
```

Why the loop: the client lets your model go whenever the model it hangs on goes -- the player
changes shape, mounts, leaves the world. `Parent()` then answers null, and the mod hangs it again
on the new body. Your model stays yours until the `M2Model` goes; the runtime takes it off and
gives it back when your mod is disabled or unloads.

Two flags change how it hangs. `WXL_M2_ATTACH_ROOT` hangs it even where the parent has no such
point, at the parent's origin: what a piece skinned to the whole body wants. `WXL_M2_ATTACH_FOLLOW_BONES`
poses its bones every frame as the parent's bones of the same key bone or name, so a piece made for
a body's skeleton (a later expansion's collection piece) moves with the body. `AttachToBone(parent,
point, key_bone, offset)` makes a point the parent's file does not have: every bone of your model
takes the pose of one key bone of the parent, moved by `offset`. `SetGeosets` shows or hides parts
of your model, `ReplaceTexture` gives it a texture of your own. The client strips the attachments
of the copies its interface draws (the character panel, portraits); the runtime hangs yours back on
them.

**Model files.** A mod that teaches the client a model format reshapes each file as it loads, at
five stages, all on the main thread:

| Event | When |
|---|---|
| `AssetScript::OnModelLoad` | the file is read, nothing parsed: answer other bytes (or convert them with an `m2` transform, above) |
| `AssetScript::OnModelParsed` | the header is parsed and nothing made from it: point its arrays elsewhere |
| `AssetScript::OnSkinProfileLoad` | the `.skin` is read, before it is parsed |
| `AssetScript::OnSkinProfileParsed` | the skin is parsed, before the client sizes its draw state from it |
| `AssetScript::OnModelSequenceLoad` | an external sequence (`.anim`) is read, before it is parsed against the model |

The parsed header is the file's own layout, and an array in it is two 32-bit words, a count and
where its elements are -- an address on the stock client, an offset from the array on the
recompiled one, whose addresses are 64-bit. `wxl::M2FileService` reads and writes an array the same
way on both:

```cpp
void OnModelParsed(std::string_view, void* file, void* header, uint32_t) override
{
    uint8_t* textures = static_cast<uint8_t*>(header) + 0x50;   // the header's texture array
    auto     current  = wxl::M2FileService::Elements<uint8_t>(textures);
    void*    more     = wxl::M2FileService::AllocNear(textures, (current.size() + 1) * 16);
    // ... copy the 16-byte entries over, add one ...
    wxl::M2FileService::SetArray(textures, more, current.size() + 1);
    wxl::M2FileService::AnimateBones(file); // its shadow moves with its bones
}
```

`AllocNear(anchor, size)` gives zeroed memory an array at `anchor` can point at: on the recompiled
client an offset reaches 2 GB at most, which the client's allocator does not promise. It lives for
the session, as the client's arrays do. `AnimateBones(file)` has the client animate the file's bones
when it draws their shadows: it skips that for a file whose bones carry none of the flags it counts.

**Drawing a batch again.** `RenderScript::OnModelBatchDrawn` comes after every batch the device
drew, with the numbers of its `DrawIndexedPrimitive` and everything it drew
with still bound: an outline mod sets a render target and a pixel shader of its own on
`wxl::GxService::D3d9Device()`, draws the same numbers again, puts the target back and calls
`wxl::GxService::InvalidateState()` so that the client sends its own state again. It comes for
every batch of every model, thousands a frame: return at once for the ones you do not want, and
subscribe only while you draw. Both clients raise it with the same numbers: the recompiled client
records them as the device draws, and on the stock one the runtime rebuilds them from what the batch
was drawn from.

These came with abi `0.1.20`.

### Changing the engine: points

Events tell your mod what happened. A **point** lets it change what happens: it is a function of
the client's engine -- the camera's update, the model cache, the world's render pass, the network's
packet handler -- that your mod runs code in front of, instead of, or after. The runtime names the
points it keeps working on every client, in `include/wxl/points.def`, and each one is a C++ class
in `wxl::points` once you include `wxl/services/point.hpp`.

A handler goes on one of three sides:

| Side | Runs | Can change |
|---|---|---|
| `Before` | when the function is entered | its arguments: each is handed by reference |
| `Replace` | instead of the function | everything: it answers in the function's place, and calls `original(...)` to run the function, with the arguments it chooses |
| `After` | once the function has returned | its answer, handed by reference first |

The example `examples/model-swap` works on `CM2Cache::CreateShared`, the function every model of
the client goes through by file name before the cache is searched. Its row in `points.def` gives
the signature your handlers take: `(void* self, void* name, uint32_t flags)`, answering a `void*`.
Every pointer is a `void*` for now; `name` is the model's path as a C string.

The Replace hands the function another file name for one model. It receives `original` first,
then the arguments; whatever it returns is what the client gets:

**`examples/model-swap/src/ModelSwap.cpp`**

```cpp
            swap_ = CreateShared::Replace([](auto original, void* cache, void* name, uint32_t flags) {
                if (name && IsMurloc(static_cast<const char*>(name)))
                    return original(cache, const_cast<char*>("Creature\\Gnoll\\Gnoll.m2"), flags);
                return original(cache, name, flags);
            });
```

Calling `original` is a choice, not a duty. A Replace that never calls it takes the call away from
the function -- and from every other mod's Replace attached below yours; the log says so once, by
name, so that two mods fighting over a point are easy to tell apart.

The After sees the answer. Here it logs each model the first time it loads; `model` is a reference,
so writing it would change what the client receives:

**`examples/model-swap/src/ModelSwap.cpp`**

```cpp
            log_ = CreateShared::After([this](void*& model, void*, void* name, uint32_t) {
                if (!name || !seen_.insert(static_cast<const char*>(name)).second) return;
                char line[320];
                std::snprintf(line, sizeof line, "model %s%s", static_cast<const char*>(name),
                              model ? "" : " (not found)");
                wxl::Log(WXL_LOG_INFO, kTag, line);
            });
```

Each call answers a `wxl::PointAttachment`, which detaches the handler when it is destroyed: keep it
as a member of your mod, as the example does, and the handler lives as long as the mod. The
runtime detaches whatever your mod still holds when it is unloaded or disabled.

Build the example as any mod, put `build/dist/wxl.model-swap/` in the client's `Mods/`, and walk to a
place with murlocs: they are gnolls, and `Logs\WarcraftXL.log` holds a `[model-swap] model ...`
line per model. The mod's manifest asks for `"abi": "0.1.16"`, which brought `wxl.point`, and
requires the service.

What to know before writing your own:

- **Order.** Handlers of a lower priority (the second argument of `Before`, `Replace`, `After`,
  0 by default) run first among Befores, outermost among Replaces and last among Afters, so that
  your mod's Before and After nest around the others'. Equal priorities follow the mods' load order.
- **Faults.** A handler that crashes is stopped there, its mod disabled, and the call goes on: a
  Replace that crashed before calling the original is followed by the original. A C++ exception
  is caught at the handler and logged, and the call goes on the same way.
- **Threads.** The points of `thread main` run on the client's main thread, as events do. A point
  of `thread any` may run on several threads at once: its handler must hold no state (a lambda
  without captures, or a function), which the facade checks when it compiles.
- **Inlined functions.** The 2008 compiler copied some functions into their callers, so the stock
  client has no address for them. Some are points all the same, **de-inlined**: the runtime
  recompiles the function they were copied into from the client's source, in which they are calls
  again, and runs that body in place of the stock one while the point has handlers (below).
  `include/wxl/deinlined.def` lists them, each with the function it lives in. A point the runtime
  cannot reach at all is listed with why: `Here()` tells whether this runtime has a point and
  `Absent()` why not; attaching where it is absent attaches nothing and logs why.
- **Overloads.** A function the client has several of under one name is named with its parameter
  types, as the client's source spells them: `M2Init(uint8_t*,uint32_t,M2Data&)`, whose class is
  `wxl::points::M2Init_uint8_t_uint32_t_M2Data`. A parameter taken by reference is handed to your
  handler as its address, a `void*`, like a pointer.
- **Signatures.** Each attach carries the point's layout hash from your headers. If a later SDK
  changes a point's signature, a mod built against the old one is refused with a line asking to
  rebuild it, instead of being called with the wrong arguments. The points of `stable` never change
  signature; a `provisional` one may, with a new version of the contract.
- **Cost.** A point nobody is attached to costs nothing: on the stock client its function is not
  hooked at all, and on the recompiled client it is the test of one byte. Attached, a call costs
  about what an event's dispatch costs.

For a point your mod only knows by name at run time, `wxl::PointService` lists them all
(`Points()`), finds one (`Find(name)`), and attaches a handler of the generic shape, which is
handed the call: `call->args[i]` is the address of argument `i` and `call->result` the address of
the answer.

### Reading the engine: the engine headers

A point hands your handler `void*`s: the client's own objects. The **engine headers** say what is
in them. They are generated from the client's source for both clients and live in the SDK's
`engine/` folder; link `wxl::engine` beside `wxl::sdk` and include the group you need:

| Header | Types |
|---|---|
| `wxl/engine/math.hpp` | `C2Vector`, `C3Vector`, `C4Vector`, `C4Quaternion`, `CAaBox`, `CAaSphere`, `C44Matrix`, `CImVector`, `CiRange`, `C2iVector`, `C4Plane`, `CRect`, `CiRect` |
| `wxl/engine/m2.hpp` | a model file once parsed (`M2Data` and its records: `M2Sequence`, `M2CompBone`, `M2Vertex`, `M2Texture`, `M2Material`, `M2Attachment`, `M2Particle`...), the skin (`M2SkinProfile`, `M2SkinSection`, `M2Batch`), `CM2Shared` (a model as every instance shares it), `CM2Model` (an instance), `CM2SceneRender` |
| `wxl/engine/gx.hpp` | the graphics device `CGxDevice`, its `CGxCaps` and `CGxFormat`, `CGxBuf`, `CGxPool`, `CGxTex`, `CGxMatrixStack` |
| `wxl/engine/texture.hpp` | `CTexture` |
| `wxl/engine/object.hpp` | `WOWGUID`, `CMovement_C`, `CGObject_C`, `CGUnit_C`, `CGPlayer_C` and their descriptor blocks `CGObjectData`, `CGUnitData`, `CGPlayerData` |
| `wxl/engine/engine.hpp` | all of them |

Each is a struct of `wxl::engine` named as the client names it, its members where the client has
them on the client your mod is built for: one layout for the stock client (x86), one for the
recompiled client (x64), each held to its offsets by `static_assert`s. A member whose type the
headers do not describe is bytes of its size, its type in a comment; a pointer to such a type is a
`void*`. A model file's arrays (`M2Array<T>`) answer `Data()` and `[]` as each client holds them,
which differ once the client has parsed the file.

```cpp
#include "wxl/engine/m2.hpp"

finish_ = wxl::points::CM2Shared_FinishLoading::Before([](void*& self) {
    const auto* shared = static_cast<const wxl::engine::CM2Shared*>(self);
    wxl::Log(WXL_LOG_INFO, "mymod", "{} ({} bytes)", shared->m_fileName, shared->m_fileSize);
});
```

**The layout check.** A client built from a later source may lay a type out otherwise, and a mod
built against the old headers would read the wrong bytes. Every type carries its **layout hash**
(`kLayoutHash`, XXH64 of its members' offsets, kinds and sizes, an embedded type by its own hash),
and the runtime answers its own through `wxl::EngineService::Layout(name)` (abi `0.1.21`). Check
the types your mod reads once, in `OnLoad`:

```cpp
wxl_result OnLoad() override
{
    if (!wxl::engine::RequireLayouts<wxl::engine::CM2Shared, wxl::engine::M2Data>()) return WXL_OK;
    ...
}
```

`RequireLayouts` (in `wxl/engine/check.hpp`) stops your mod with a line naming each type that
differs and asking to rebuild against this runtime's SDK; `LayoutMismatch<...>()` answers that line
without stopping it. The stock client never changes, so its hashes never do; the recompiled
client's build fails when its own types drift from the headers, before a mod can see the drift.

The engine headers are not under the SDK's license: they describe the client's own structures
(`engine/NOTICE.md`).

### Taking a function apart: extraction

To change part of an engine function on the stock client -- one test, one call inside it -- you
need the function as source. `core/tools/extract/extract.py` writes it out from the client's
source as a starting template you compile for x86:

```sh
python core/tools/extract/extract.py CM2Shared::FinishLoading --client <client checkout> \
    --binary <Wow.exe 12340> --namespace mine --out FinishLoading.cpp
```

It takes a qualified name, an overload with its types, or an x86 address (`0x0083CF00`), and
writes a `.cpp` against the engine headers in which:

- every function the original calls is called in Wow.exe at its own address, with its own
  convention (namespace `stock`); a member function becomes `__fastcall (Class* self, void* edx,
  ...)`, which is how a mod writes the client's `__thiscall`;
- every global it reads is Wow.exe's own, by reference;
- every function the 2008 compiler had copied into it is extracted too, as a function of its own;
- memory comes from Storm, as in the client: its allocator is called like any other function;
- what it uses that the engine headers lack is listed at the top of the file.

`--binary` reads the stock Wow.exe to tell apart the instantiations of one template that the
client's source tags with a list of addresses: each call takes the next of its candidates in the
order Wow.exe's own function makes them.

Change what you need, build it as part of your mod for `win-x86`, and put it in place of the stock
function through a point:

```cpp
#include "FinishLoading.cpp"

finish_ = wxl::points::CM2Shared_FinishLoading::Replace([](auto original, void* self) {
    // the recompiled FinishLoading, changed, runs instead of Wow.exe's
    return mine::CM2Shared_FinishLoading(static_cast<wxl::engine::CM2Shared*>(self), nullptr);
});
```

A Replace that never calls `original` takes the call for good; it is your mod's from then on, and
the log says so once. On the recompiled client the function already is source: change it in the
client, or attach to the point there as usual.

The runtime does the same for the points the stock client inlined. `M2Init(uint8_t*,uint32_t,
M2Data&)`, which checks a model file's header and finds its arrays, is copied into
`CM2Shared::FinishLoading` in Wow.exe; the runtime carries FinishLoading recompiled by this tool,
in which M2Init is a call, and puts it in place of the stock one while M2Init has handlers. A mod
that teaches the stock client another model version attaches a Replace there:

```cpp
init_ = wxl::points::M2Init_uint8_t_uint32_t_M2Data::Replace(
    [](auto original, void* base, uint32_t size, void* data) {
        auto* header = static_cast<wxl::engine::M2Data*>(data);
        if (header->m_version == 0x110) Downgrade(*header); // a file of a later client, made 0x108
        return original(base, size, data);
    });
```

The runtime's recompiled body is checked against Wow.exe's own code: run on the same model files,
both answer the same and leave the same bytes (core's `stock` test).

### The camera, the mouse and the world's geometry

Three services (abi `0.1.23`) let a mod take the camera and steer it with the mouse: a camera
that follows the character from over its shoulder, a free camera, a top-down one.

**Placing the camera.** The client places its world camera once a frame, and
`RenderScript::OnCameraUpdate` comes right after, before anything is drawn from it. A view set
there with `wxl::CameraService::SetView` is the one the frame is drawn from: where the camera is,
where it looks, and a field of view in radians (0 keeps the client's). It stays until
`ReleaseView()`, so set it again each frame to move with the frame.

```cpp
class Overhead : public wxl::RenderScript
{
    void OnCameraUpdate(uint64_t, float) override
    {
        // The point the client's camera turns around: the character, at the height it looks from.
        const wxl::Expected<wxl_vec3> pivot = wxl::CameraService::Pivot();
        if (!pivot || wxl::CameraService::Cinematic()) return;
        const wxl_vec3 eye{pivot->x - 6.0f, pivot->y, pivot->z + 8.0f};
        const wxl_vec3 look{pivot->x - eye.x, pivot->y - eye.y, pivot->z - eye.z};
        wxl::CameraService::SetView(eye, look, 0.0);
    }
};
```

`Position()`, `Forward()`, `Up()` and `Fov()` read where the camera is this frame, `Pivot()` the
point the client's own camera turns around (higher on a tauren than on a gnome, higher on a
mount), `Cinematic()` whether a cinematic has the view, which a view set by a mod does not
override. Everything the client does from the camera follows the view you set: the world drawn,
the sound heard, and what is under the pointer. The client keeps its own camera going underneath,
so `ReleaseView()` puts it back as the player left it. When two mods set a view, the later is
drawn; when it lets go, the earlier comes back. A mod's view goes when the mod is disabled or
unloaded. Coordinates are the client's world coordinates, in yards: x north, y west, z up.

**Steering with the mouse.** `wxl::InputService::CaptureCursor(true)` hides the pointer and holds it
at the centre of the game's window. The game still sees it there: what is at the centre of the
screen is the mouseover, a left click targets it and a right click talks to it or loots it, and
nothing tells the game a button is held. The motion arrives as `WorldScript::OnMouseMotion(dx,
dy)`, in the mouse's own counts (right and towards you are positive), whether the pointer is held
or not; the wheel as `WorldScript::OnMouseWheel(delta)`, in notches (away from you is positive),
which a handler keeps from the game by answering `handled`:

```cpp
class Steer : public wxl::WorldScript
{
    float yaw_ = 0.0f, pitch_ = 0.0f;

    void OnMouseMotion(int32_t dx, int32_t dy) override
    {
        if (!wxl::InputService::CursorCaptured()) return;
        yaw_ -= dx * 0.004f;   // right turns clockwise, as facings shrink
        pitch_ -= dy * 0.004f; // towards you looks down
    }

    void OnMouseWheel(float delta, wxl::Answer handled) override
    {
        zoom_ += delta;
        handled.Set(); // the game's own zoom never sees it
    }

    float zoom_ = 0.0f;
};
```

`CursorCaptured()` says whether some mod holds the pointer, `PointerOverWorld()` whether it is over
the world rather than a frame of the interface. The pointer is let go while the game's window is
not in front, and held again when it comes back; a mod's hold goes when it is disabled or unloaded.
With the character's facing (`wxl::ActionService::Face`) and the camera above, that is all a
third-person camera needs.

**The world's geometry.** `wxl::WorldService::Trace(start, end[, flags])` answers the first point of
the world between two points, the way the client's own camera and the spells' line of sight test
it: the ground, buildings and the collision of placed objects, never a unit or a player. It answers
nothing when the way is clear. `flags` is the client's hit-test mask: `WXL_TRACE_CAMERA` (the
default: what the client's camera stops at), `WXL_TRACE_LINE_OF_SIGHT`, `WXL_TRACE_LIQUID`.

```cpp
if (const std::optional<wxl_vec3> wall = wxl::WorldService::Trace(head, eye))
    eye = *wall;  // pull the camera in front of what is between it and the head
```

Whether the character stands indoors is `wxl::Unit::IsOutdoors()`, as the client's `IsOutdoors`
answers it: false in a building or a cave the world marks indoors, where a mount is refused.

The three services and `Face` run on the main thread, where your handlers of world events run;
from another thread they are refused with `WXL_E_INVALID`.

**Keeping the game out of its mouse look.** With the left button held the game turns its own
camera, with the right one it turns the character, and either way it hides the pointer and
forgets what is under it. A mod that steers the camera itself keeps the game out of that with
`wxl::InputService::BlockMouseLook(true)` (abi `0.1.24`): a held button then turns nothing, hides
nothing and starts no turn, and a click still does what a click does -- the left one selects, the
right one talks, loots or attacks -- when it is let go within the game's click time (0.8 s), on
what was under the pointer when it was pressed. A longer hold does nothing, as in the game, and
the game's `MouselookStart` and its move with both buttons are kept out too. `BlockMouseLook(false)`
gives it back; it holds while any mod blocks it, and goes with the mod.

**Turning a head.** `wxl::PoseService::Head(unit, yaw, pitch, smoothing)` (abi `0.1.24`) turns a
unit's head from where its body faces and holds it there, over whatever its animation does:
radians, a positive yaw to the unit's left, a positive pitch up, `smoothing` the seconds to cover
half the way (0 at once). The runtime holds the angles to what a neck allows (100 degrees of yaw,
80 of pitch); keep your own narrower limits and turn the body (`Face`) past them.
`ReleaseHead(unit)` gives the head back to the animation as smoothly. A head is the last mod's to
turn it; a mod's heads go back when it goes, and a unit's when it leaves view.

```cpp
// The player looks where the camera aims, within 70 degrees either way.
const float yaw = AngleBetween(playerFacing, cameraYaw);
wxl::PoseService::Head(wxl::Player::Local(), std::clamp(yaw, -1.22f, 1.22f), pitch, 0.08);
```

### Windows of your own: Dear ImGui

The runtime carries [Dear ImGui](https://github.com/ocornut/imgui) and
[ImPlot](https://github.com/epezent/implot) (abi `0.1.24`) and draws them over the game's frame,
after its interface, with the game's own Direct3D 9 device, on both clients. A mod adds its
windows from `RenderScript::OnImGuiFrame`, which comes once a frame while some mod listens:

```cpp
#include "wxl/services/imgui.hpp"   // and target_link_libraries(<mod> PRIVATE wxl::imgui)

class Panel : public wxl::RenderScript
{
    void OnImGuiFrame(uint64_t, float) override
    {
        if (!shown_ || !wxl::ImGuiService::Bind()) return;
        ImGui::Begin("My mod", &shown_);
        ImGui::SliderFloat("Speed", &speed_, 0.0f, 10.0f);
        ImGui::End();
    }
    bool  shown_ = true;
    float speed_ = 1.0f;
};
```

Your mod compiles its own copy of Dear ImGui and ImPlot from the SDK (`wxl::imgui`), and
`Bind()` points that copy at the runtime's contexts and allocator, which is how every mod's
windows end up in one frame. It answers false, and logs once why, on a runtime without
`wxl.imgui` or with another version of Dear ImGui: build against that runtime's SDK. Call it
first in each `OnImGuiFrame`. Nothing is drawn, and nothing is set up, while no mod listens to
the event: listen to it only while your windows are open, or return at once.

The game window's mouse and keys reach Dear ImGui first: what it wants -- the pointer over one of
its windows, a text field being typed in -- the game and the other mods do not see. Show the
pointer while your windows are open (`wxl::InputService::CaptureCursor(false)` if your mod holds
it). Text and spacing are sized for the window: the monitor's DPI scale, or a quarter step per
1080 lines of height, from 1 to 3. The windows' places are not saved. The device going away and
coming back (a resize, Alt+Tab out of full screen) is handled by the runtime. An `ImGui::Begin`
without its `End` is mended at the end of the frame rather than ending the game.

### Profiling memory

`wxl::FootprintService` (abi `0.1.11`) measures where the client's memory goes: Storm's
allocations by the source file that asked, the big consumers outside Storm, and the address space,
whose largest free block is what the 32-bit client runs out of first. Counting Storm's allocations
costs about a tenth of a microsecond on each, so it waits for a mod to start it; start it early,
in `OnLoad`, to follow everything after:

```cpp
const wxl::FootprintService footprint;
footprint.Start();                                   // Storm's allocations from now on
footprint.WatchPressure(256ull * 1024 * 1024);       // OnMemoryPressure under 256 MB free in one block

// later, on the main thread
footprint.Snapshot();
for (const wxl::footprint::Source& source : footprint.Sources())        // largest first
    wxl::Log(WXL_LOG_INFO, "mymod", "%s: %llu bytes in %llu blocks", source.file.c_str(),
             (unsigned long long)source.bytes, (unsigned long long)source.count);
for (const wxl::footprint::Region& region : footprint.Regions())
    if (region.state == "free")
        wxl::Log(WXL_LOG_INFO, "mymod", "largest free block: %llu", (unsigned long long)region.largest);
```

`Categories()`, `Archives()` and `Tables()` read the same picture. To show what your own mod
holds next to the client's numbers, report it: `footprint.Report("cache", bytes, count)` appears
among the categories under your mod's id until you report it again or the mod goes. The
categories overlap (the client's Lua, its databases and its archives' tables are Storm
allocations too); `include/wxl/services/footprint.h` says what each one counts. The Lua example
`examples/memory-report/` turns all of it into a report you can read.

### Profiling time

The runtime carries the Tracy profiler: connect its viewer and every event, every mod's handler
and every frame shows on a timeline. `wxl/services/profile.hpp` (abi `0.1.12`) adds your own
zones, `WXL_PROFILE_ZONE("Journal::Flush");` for the rest of a block, and graphs, messages and
frame sets through `wxl::ProfileService`. [Profiling](profiling.md) walks through it.

### Resources

Whatever the C boundary hands back as a token comes back as a move-only object that releases in
its destructor: `wxl::Subscription`, `wxl::Hook`, `wxl::Patch`, `wxl::File`, `wxl::OpcodeClaim`,
`wxl::Panel`, and `wxl::FieldWatch`, which holds a watch rather than a token. Each has `Reset()` to release early (safe to repeat) and `Release()` to disown the
token for code that manages it by hand. Keep them as members of the mod or of a script, and they
go when it goes -- before the DLL does.

`wxl::PatchSet` holds several patches and restores them last-applied-first; `Apply` answers
false when a write is refused, so a mod can stop and let what went before undo itself.

## When it fails

**`the 32-bit (x86) MSVC toolchain is not installed`** -- Visual Studio has no x86 compiler. Visual
Studio Installer > Modify > Individual components > *MSVC v14x - C++ x64/x86 build tools*. Or
build `win-x64` alone, as above.

**`mod.json promises "win-x86" ... but the mod is not built for win-x86`** -- the `TARGETS` of
`wxl_add_mod` leave out an architecture the manifest lists. Add it to `TARGETS`, or remove the
entry.

**`mod.json promises no binary for win-x86, which this build produces`** -- the opposite: add the
entry, at the path the message gives.

**`mod.json promises win-x64 at "..." but the build writes it to "bin/x64/<name>.dll"`** -- the
path in the manifest is not where the DLL lands. `OUTPUT_NAME` sets the file name; the folder is
always `bin/<arch>/`.

**`"abi" is "x.y.z" but the SDK headers are a.b.c`** -- the manifest asks for a contract version
the headers it was built against do not carry: another major or minor, or a later feature. Set it
to what the message says.

**`"abi" is "x.y", the two-part numbering the SDK used before 0.1.5`** -- the manifest predates
the three-part version. Set it to what the message says.

**`this tree's compiler targets win-x86, and 'Ninja' builds that one architecture only, but
WXL_MOD_TARGETS asks for win-x64`** -- with Ninja or Makefiles the compiler decides the
architecture. Configure the tree from a prompt, or with a toolchain file, of the architecture you
want, and drop `WXL_MOD_TARGETS`; or use `-G "Visual Studio 17 2022"` to build both from one tree.

**`wxl::Log with arguments formats them with std::format, which this standard library does not
have`** -- the mod is compiled as C++17, or with a standard library older than `std::format`. Use
`CXX_STANDARD 20` (the default), or log a text you build yourself.

**`this tree is WXL_MOD_ARCH=win-x64 (64-bit) but the compiler targets 32-bit`** -- a hand-made
child tree with the wrong `-A`. Match them.

**`cannot find the SDK next to ...`** -- `WXL_SDK_DIR` does not point at a checkout, or the
installed package is incomplete. Pass `-DWXL_SDK_DIR=<path to WarcraftXL/sdk>`.

**`No CMAKE_CXX_COMPILER could be found`** or **`MSB8020: The build tools for v14x cannot be
found`** -- the C++ workload is not installed in the Visual Studio CMake picked. Install it, or
delete `build/` and configure again after installing.

**`LNK1104: cannot open file '....dll'`** -- the DLL is loaded by a running client. Close it.

**The mod folder is stale after renaming the mod or its id** -- the old folder is not removed.
Delete `build/dist/` and build again.
