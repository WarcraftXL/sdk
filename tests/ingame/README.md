# The in-game test suite

The tests under `tests/` check the contract against a fake client. This folder checks it against
the real one: a set of test mods, and `wxl.selftest`, a Lua mod that drives them from inside the
game and writes one line per check into `Logs/WarcraftXL.log`. Install it in a client, enter the
world with a character, wait about two minutes without touching anything, and read the log.

## What is here

| Folder | Kind | What it checks |
|---|---|---|
| `wxl.selftest/` | Lua | runs every check below in order, from the client's own Lua, and reports PASS, FAIL, SKIP or NOTE for each |
| `wxl.test-abi-old/` | Lua | a manifest asking for abi 0.1.5 is refused the events of later features, with the version to ask for |
| `wxl.test-both/` | Lua | a manifest naming both a binary and a Lua file is refused, and nothing of it runs |
| `wxl.test-events/` | Lua | logs the events of waves 2 and 3, the exact chat events and the identity of the current target |
| `wxl.test-fields/` | Lua | reads, watches and unwatches descriptor fields; `TestFields()`, `TestFieldsHealth()` |
| `wxl.test-order-a/`, `wxl.test-order-b/` | Lua | handlers run in load order, and a mod that reloads keeps its place; `TestOrderMark()`, `TestOrderTouch()`, `TestOrderSeen()` |
| `wxl.test-reload/` | Lua | a change to a file of its own reloads the mod; a broken `mod.json` unloads it, a repaired one loads it again; `TestReloadPart()`, `TestReloadWrite()`, `TestReloadBreak()` |
| `wxl.test-search/` | Lua | a mod's own search folder: added, read from, removed; `TestSearchRemove()`, `TestSearchAdd()` |
| `wxl.test-watch-other/` | Lua | a field watch is per mod: this one never receives the fields `wxl.test-fields` watches |
| `wxl.test-throw/` | C++ | an exception thrown by a handler or a script function costs that call only; `TestThrow()` |
| `wxl.test-throw-ready/` | C++ | an exception thrown by `OnReady` disables the mod, and none of its code runs again |
| `wxl.test-manifest/` | C++ | puts back the `mod.json` `wxl.test-reload` broke, which that mod cannot do once it is unloaded; `TestManifestRepair()` |
| `wxl.test-points/` | C++ | attaches to `CM2Shared::FinishLoading` and the de-inlined `M2Init` on request and counts them, reading each model through the engine headers (phase 12) |
| `wxl.test-bones/` | C++ | watches the bones of character models on request, on the client's animate threads, counts the files taken and the frames handed over and reads a file's root bone through `BoneInfo` (phase 16) |
| `wxl.test-assets/` | C++ | serves the add-on `WxlProvided` and `Interface\WxlTest\lower.wxltest` through an asset provider; one transform rewrites the add-on's Lua, another upper-cases `.wxltest` files (phase 10); plays a downloader through its `wxl.relay` service `wxl.test-assets.fetch`, and keeps the last name the client could not find (phase 13) |
| `steps.txt` | text | the plan the probe shows with `/mark`: the few steps a person still plays |

`wxl.selftest` also calls into four of the SDK's examples: `scout` (`ScoutCount()` and the others),
`lua-services` (`LuaServicesGreet()`), `memory-report` (`MemoryReport()`) and `probe`
(`WxlProbeStep()`). The build puts them in the suite's folder too, with `model-swap`, the points
example: its `[model-swap]` lines in the log name every model loaded, and murlocs are drawn as gnolls.
Phase 7 of `wxl.selftest` checks the list of engine points (`wxl.point`, abi 0.1.16) on the runtime
it runs on. Phase 8 subscribes every gameplay event (abi 0.1.17), waves (`TextEmote`, `Emote` as a
NOTE), and reads what phase 4 caused: the item taken off reaching the bags
(`InventorySlotChanged`) and the racial's aura (`UnitAuraApplied`, a NOTE for a racial that puts
none on its caster). Loot, quests, groups, trades, duels, windows, death and reputation need a
kill, an NPC or another player: they are SKIP lines, and the probe lists them when they happen.
Phase 9 reads the player, its target and an item it wears through the object methods (abi
0.1.18) and compares each with the client's own Lua (`UnitLevel`, `GetMoney`, `UnitAura`,
`GetInventoryItemID`, `GetItemInfo`, `UnitReaction`...), then targets the player with `Select()`
and clears the target with `wxl.action.ClearTarget()`. Target a creature before the run to have
the target's reads checked; without one they are a SKIP.
Phase 10 checks the asset pipeline (abi 0.1.19): bytes `wxl.storage.Serve` serves are read back
through the client's file layer and taken back; the C++ mod `wxl.test-assets` provides an add-on,
`WxlProvided`, that is on no disk -- the client must list it, load it and run its Lua, which a
transform of `.lua` files rewrites on the way (`WXL_PROVIDED_ADDON` reads `"loaded, transformed"`)
-- and a `.wxltest` file a second transform upper-cases; then the asset events seen since the
start: terrain tiles, textures, skins and a sound it plays are PASS lines, map object groups and
shaders NOTE lines (they depend on where you stand and on when the mods load). Without
`wxl.test-assets` in `Mods/`, the provider's checks are a SKIP. The `.lua` transform sees every
Lua file the interface loads (it changes only its own add-on's), so this pass also reads the whole
interface through the runtime's file layer.
Phase 11 checks the models (abi 0.1.20, the work of Tyrallis): the loader's events seen since
the start (`ModelParsed`, `SkinProfileParsed`; `ModelSequenceLoad` a NOTE), the appearance changes
phase 4 caused (`AppearanceChanged`, a SKIP when phase 4 took nothing off), then `wxl.m2`: the
player's model, its head's attachment point, a murloc made, hung on the head (`Model:Attach`) and
taken off again. While it hangs, `ModelBatchDrawn` counts the batches the device draws for two
seconds, then the selftest stops listening (the event comes thousands of times a frame): a PASS on
both clients, a FAIL when none came or the runtime does not raise it. With `model-swap` in
`Mods/`, the murloc is drawn as a gnoll.
Phase 12 checks the model loader's points (abi 0.1.22): the C++ mod `wxl.test-points` attaches to
`CM2Shared::FinishLoading` and to `M2Init(uint8_t*,uint32_t,M2Data&)` -- inlined into FinishLoading
in the stock client, which its runtime then runs recompiled with M2Init a call of its own -- and
four boss models are made, which the starting zones do not show. Both handlers must fire, read the
model through the engine headers (its file name, version 264) and the models must still parse
(`ModelParsed`). Without `wxl.test-points` in `Mods/` the phase is a FAIL.
Phase 13 checks the bricks a mod fetching assets on demand is built from (abi 0.1.23): a texture
nobody has raises `FileMissing` (a mod's own `wxl.storage` lookup does not); bytes the selftest
serves at 0 under a path of its own (`Interface\WxlSelfTest\FrameXML.toc`) live beside the
client's `Interface\FrameXML\FrameXML.toc`; `OverrideFile` puts them in place of the client's and
`RestoreFile` takes them back; `HashFile` hashes served bytes (a known MD5) and the archive's file,
`HashFileAsync` answers the same a moment later; `ReloadAsset` reads the target frame's texture
again from the elite one an override puts in its place (up left of the screen's centre, the elite
dragon for two seconds), answers `unsupported` for an icon (the interface keeps small square
textures in its shared atlas, which is not read again) and makes the player's model again (it
blinks); a map object is `unsupported`. Then
`wxl.relay`: a Lua service answering at once and later, taken back (`not_found`); and, with
`wxl.test-assets`, a texture its provider has not yet (`FileMissing`, pending), fetched by its
service on a thread of its own and served under the path the selftest chose
(`Interface\WxlSelfTest\Fetched.blp`, hashed as the downloader reported), shown by that path and,
through an override, by the name it lacked (right of the centre: two bags for three seconds, a NOTE
to look at), and a request cancelled once the C++ service has it (the service is told; a request cancelled
before it is handed over is dropped without a word to anyone). Without `wxl.test-assets`, the
downloader's checks are a SKIP.

Phase 14 checks the camera and the pointer (abi 0.1.24): `wxl.camera` reads the world camera,
then places it ten yards above the player's head at every `CameraUpdate` for two seconds (the
view is seen while it lasts), while `wxl.input` holds the pointer at the centre; both are given
back. `MouseMotion` is a NOTE (move the mouse during those two seconds to see it). Then
`wxl.world.Trace` meets the ground under the player and nothing in the sky, `IsOutdoors` agrees
with the client's `IsIndoors`, and `wxl.action.Face` turns the player half a radian and back.

Phase 15 checks the game kept out of its mouse look and a head turned (abi 0.1.25): with
`wxl.input.BlockMouseLook(true)`, the game's `MouselookStart()` leaves `IsMouselooking()` false;
once the block is given back it works again. `wxl.pose.Head` turns the player's head left and up
for a second (watch it), then `ReleaseHead` gives it back. `ImGuiFrame` raised by the runtime is a
NOTE.

Phase 16 checks models of the world (abi 0.1.21): `wxl.m2.GroundHeight` finds the ground under the
player, `AttachmentWorld` the player's head above it; a murloc made with `CreateWorldModel` stands
on the ground three yards in front of the player, facing it, for two seconds (watch it: it fades to
half its alpha after one), then `Release` takes it away. The C++ mod `wxl.test-bones` watches the
bones of the character models for a second: the files its filter took, the frames handed over, the
matrices there and the root bone read by `BoneInfo` (parent -1). A cast that named a ground point
since the start (a ground-targeted spell, a blink) is a NOTE with the point; cast one before the
run to see it. Without `wxl.test-bones` in `Mods/` its checks are a FAIL.

## Building it

The suite is built with the SDK's tests (`WXL_SDK_TESTS`, on in the SDK's own tree), by the
target `wxl-ingame`, part of the default build. It assembles a folder ready to copy:

```
<build>/ingame/Mods/
  wxl.selftest/  wxl.test-*/               the suite
  wxl.test-throw/bin/<arch>/test-throw.dll the C++ mods, for this tree's architecture
  wxl.scout/  wxl.lua-services/  wxl.memory-report/  wxl.probe/  (with steps.txt)
```

A build tree is one architecture. To get the C++ mods for both, give the x64 and the x86 tree the
same folder with `-DWXL_INGAME_DIR=<folder>` and build both; each fills its own `bin/x64/` or
`bin/x86/`. From Linux, with the toolchain of each architecture as the SDK's README shows:

```sh
cmake -S sdk -B out/sdk-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DWXL_INGAME_DIR=$PWD/out/ingame ...
cmake --build out/sdk-x64 --target wxl-ingame
cmake -S sdk -B out/sdk-x86 -G Ninja -DCMAKE_BUILD_TYPE=Release -DWXL_INGAME_DIR=$PWD/out/ingame ...
cmake --build out/sdk-x86 --target wxl-ingame
```

## Installing it

1. Close the client.
2. Copy every folder of the suite's folder into the client's `Mods/`, next to the `wxl.lua` the
   runtime ships. A mod of the same name already there is replaced by this build's.
3. Put a `WarcraftXL.ini` next to the executable with `level = debug` under `[log]`, so the log
   keeps every line.
4. Start the client and enter the world.

Take the suite out of `Mods/` after the test: the probe listens to every event, which slows the
client, and `wxl.selftest` runs again at every entry into the world.

## What wxl.selftest does

A few seconds after the character enters the world, without a keystroke:

1. **The functions.** Calls, from the client's Lua, every function the test mods and examples
   register, and checks each answer.
2. **Chat, addon message, CVar.** Whispers the player, sends an addon message, changes `farclip`
   and puts it back, and checks that `OnChatSend`, `OnChatMessage`, `OnChatAddonMessage` and
   `OnCVarChanged` came.
3. **Hot reload.** Has `wxl.test-reload` rewrite its `lib/part.lua` and checks that the mod
   reloaded with the new text. Sends a chat message `wxl.test-order-a` answers and checks that
   `wxl.test-order-b` saw the answer (a runs first), has `wxl.test-order-a` rewrite its `mark.lua`,
   and checks again after its reload. Has `wxl.test-reload` break its `mod.json` (the mod is
   unloaded and stays so), then `wxl.test-manifest` repair it (the mod loads again). The files
   rewritten are put back as they were.
4. **Events a session rarely shows** (`PROVOKE` at the top of `init.lua`): a cast a handler
   refuses (it must not start), the same cast started and cancelled (`OnSpellCastStart`,
   `OnSpellCastCancel`), a racial with a cooldown (`OnSpellCastGo`, the cooldown, and
   `OnSpellCastFailed` when cast again during it), a hearthstone use a handler refuses
   (`OnItemUseRequest`, and no cast), an item taken off and put back on (`OnEquipmentChanged`
   twice).
5. **/reload.** Checks `OnWorldEnter` came at login, reloads the interface, checks it raised
   neither `OnWorldLeave` nor `OnWorldEnter` (the player never left), and calls the functions again.
6. **Log out and back in** (`RELOG`): logs out, waits for the character list, enters the world
   with the same character (`OnWorldLeave`, `OnGlueScreenChanged`, `OnGlueCharacterList`,
   `OnGlueEnterWorld`, `OnWorldEnter`), and calls the functions again.

The chat box then shows `wxl.selftest done: N passed, N failed, N skipped`.

**What it does to the character.** It casts a spell with a cast time on itself (a heal, or a
warlock's Imp) and cancels it, uses a racial (whose cooldown then runs), starts a hearthstone that
is refused before it casts, takes off a piece of armour and puts it back on, and logs out. Outside
combat and standing still, in an inn if possible (the logout is then immediate instead of 20
seconds): moving cancels the logout, and armour cannot change in combat.

**When something is missing,** the check is skipped with a reason rather than failed: a class
without a cast-time spell at its level (warrior, rogue, hunter, mage, death knight), a race whose
racials are left out (War Stomp, Arcane Torrent and Shadowmeld reach the units around, or hide the
player), no hearthstone, nothing worn in the slots tried, a full backpack, the game's sound off
(`OnSoundFileLoad`: the client loads no sound file then). If the logout does not
happen (in combat, moved), the run says so and ends; log in again by hand. Set `PROVOKE` or
`RELOG` to `false` at the top of `wxl.selftest/init.lua` to leave that part out.

## Reading the results

Every line of the run is tagged `[wxl.selftest]` in `Logs/WarcraftXL.log`:

```
[INFO ] [wxl.selftest] PASS reload after a file of its own changed: lib/part.lua now gives part 1793520000
[INFO ] [wxl.selftest] FAIL handler order kept after wxl.test-order-a reloaded: b saw a's answer: false
[INFO ] [wxl.selftest] SKIP casts: this character knows none of the cast-time spells tried (...)
[INFO ] [wxl.selftest] NOTE event SpellCooldown (Stoneform): not raised; the server sends ...
[INFO ] [wxl.selftest] done: 52 passed, 1 failed, 1 skipped
```

- **PASS**: the check held; the text after the colon is what the client answered.
- **FAIL**: it did not; the text says what came and what was expected. A function that answers
  `attempt to call global 'ScoutCount' (a nil value)` belongs to a mod that is not installed or
  did not load: the mods at the top of the log say why.
- **SKIP**: the check needs something this character or this run does not have.
- **NOTE**: an event the server may or may not send for that action; its absence is not a fault
  of the runtime.

`core/tools/read_logs.py <client folder>` summarises the launch around them: the mods loaded, the
warnings and errors, the probe's marks and the events seen and never seen. The test mods' own
lines (`[wxl.test-throw]`, `[wxl.test-fields]`, ...) show the rest: the errors `wxl.test-throw`
and `wxl.test-throw-ready` cause are on purpose, each one says so in its text.

## Files the run writes

All inside the suite's own folders: `wxl.test-reload/lib/part.lua` and `wxl.test-order-a/mark.lua`
are rewritten and put back; `wxl.test-reload/mod.json` is broken and repaired, with
`mod.json.good` beside it in between (`wxl.test-manifest` repairs it at the next launch if the
client closed before); `wxl.test-events/notes.txt`; `wxl.memory-report`'s reports.
