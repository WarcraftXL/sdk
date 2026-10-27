<div align="center">

# WarcraftXL SDK

**The contract a WarcraftXL mod is written against**

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![ABI](https://img.shields.io/badge/ABI-0.1.40-blue.svg)](include/wxl/abi.h)
[![C++](https://img.shields.io/badge/C%2B%2B-17%20and%20up-blue.svg)](#-getting-started)
[![Header only](https://img.shields.io/badge/header--only-yes-lightgrey.svg)](include/wxl)

</div>

---

> [!IMPORTANT]
> ABI major 0 means the contract is still moving: any minor may break any other. Ids and stage
> numbers are already append-only, but signatures are not frozen until 1.0.

## 🚀 Overview

Write a mod once, in C++, and run it on every WarcraftXL runtime: the stock `Wow.exe` 3.3.5a and
the recompiled client alike. This repository declares what a mod exposes, what a runtime offers it,
and the rules of the boundary between them. There is nothing to link and nothing to ship.

### Key features

- **One source, every target.** `wxl_add_mod()` builds a mod for each architecture in one command.
- **Object-oriented.** Derive from `wxl::Mod` and the script families; world objects are C++
  values you can compare, hash and iterate, not handles you dereference by hand, and a handler
  receives them as `wxl::Object`. A handle names one stay of an object in view; its guid is what
  lasts.
- **A read that can fail says so.** Every read answers `wxl::Expected<T>`: the value, or the
  `wxl_result` that stood in for it. A dead unit, a gone one and a runtime without the service
  are three different answers.
- **Resources release themselves.** A subscription, a hook, a patch, an open file, a claimed
  opcode, a panel: each comes back as a move-only object that releases in its destructor, so
  nothing the runtime calls into outlives the code it points at.
- **A plain-C boundary.** No std type, vtable or exception crosses it, so a mod and the runtime
  that loads it may be built with different toolchains.
- **A missing service is an answer.** A mod declares what it requires and is refused before it
  loads if a runtime cannot meet it -- never halfway in.
- **Contract tests included.** `tests/contract.cpp` checks every promise of the C++ layer against
  a fake host, and `tests/host` checks the mod host every runtime links.

## 📋 Table of contents

- [Overview](#-overview)
- [Getting started](#-getting-started)
- [A mod](#-a-mod)
- [What a runtime offers](#-what-a-runtime-offers)
- [Layout](#-layout)
- [Versioning](#-versioning)
- [Documentation](#-documentation)
- [Support](#-support)
- [Contributing](#-contributing)
- [License](#-license)

## ⚡ Getting started

### Prerequisites

- CMake 3.21 or newer
- A C++ compiler: the SDK headers take C++17 (MSVC 19.3+, GCC 11+, Clang 14+); a mod builds as
  C++20 by default, for `std::format` (MSVC 19.3+, GCC 13+, Clang 17+)
- The runtime you are targeting, to run what you build

### Build the template

```sh
cmake -S template -B build
cmake --build build --config Release
```

This produces a complete mod, ready to drop into a runtime's `Mods/` directory:

```
build/dist/wxl.example/
    mod.json
    bin/x86/example.dll
    bin/x64/example.dll
```

### Build the conformance tests

```sh
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

The same build assembles the in-game test suite, `build/ingame/Mods/`, to copy into a client:
see [`tests/ingame/README.md`](tests/ingame/README.md).

### Run the tests from Linux

The suites are Windows executables. From Linux they build with clang-cl and run under Wine, one
architecture per tree. Two environment variables set this up: a toolchain file that drives
clang-cl and lld-link against the MSVC CRT and the Windows SDK (which
[xwin](https://github.com/Jake-Shadle/xwin) downloads), and an emulator that ctest puts in front
of every test, as does the build in front of the tools it runs (CMake 3.28 or newer reads it from
the environment):

```sh
export CMAKE_TOOLCHAIN_FILE=<toolchain for x86_64-pc-windows-msvc>
export CMAKE_CROSSCOMPILING_EMULATOR=<path to the wrapper below>
cmake -S . -B build-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DWXL_LUAJIT_LIBRARY=<luajit>/src/lua51.lib -DWXL_LUAJIT_INCLUDE_DIR=<luajit>/src
cmake --build build-x64
ctest --test-dir build-x64 --output-on-failure
```

The wrapper runs its arguments under Wine; 32-bit Wine is needed for the x86 tree:

```sh
#!/bin/sh
export WINEDEBUG="${WINEDEBUG:--all}"
exec wine "$@"
```

`wxl.lua` links LuaJIT, which the tree otherwise fetches and builds with LuaJIT's
`msvcbuild.bat`. `WXL_LUAJIT_LIBRARY` and `WXL_LUAJIT_INCLUDE_DIR` take one built beforehand
instead: a static `lua51.lib` for the tree's architecture, built against the dynamic CRT (`/MD`),
and the directory of its headers. `-DWXL_SDK_LUA=OFF` leaves `wxl.lua` and its tests out
altogether.

To build that LuaJIT, do the steps of `msvcbuild.bat static` by hand in a checkout's `src/`. The
host tools are built with the Linux compiler, at the target's pointer size and told the target is
Windows; the library is compiled with clang-cl, `XWIN` being where xwin put the CRT and the SDK:

```sh
git show -s --format=%ct > luajit_relver.txt
gcc -O2 -o minilua host/minilua.c -lm
./minilua ../dynasm/dynasm.lua -LN -D WIN -D JIT -D FFI -D ENDIAN_LE -D FPU -D P64 \
    -o host/buildvm_arch.h vm_x64.dasc
./minilua host/genversion.lua
gcc -O2 -DLUAJIT_OS=LUAJIT_OS_WINDOWS -I. -I../dynasm -o buildvm host/buildvm*.c
LIBS="lib_base.c lib_math.c lib_bit.c lib_string.c lib_table.c lib_io.c lib_os.c
      lib_package.c lib_debug.c lib_jit.c lib_ffi.c lib_buffer.c"
./buildvm -m peobj -o lj_vm.obj
for m in bcdef ffdef libdef recdef; do ./buildvm -m $m -o lj_$m.h $LIBS; done
./buildvm -m vmdef -o jit/vmdef.lua $LIBS
./buildvm -m folddef -o lj_folddef.h lj_opt_fold.c
clang-cl --target=x86_64-pc-windows-msvc /vctoolsdir $XWIN/crt /winsdkdir $XWIN/sdk \
    /nologo /c /O2 /MD /D_CRT_SECURE_NO_DEPRECATE \
    "/D_CRT_STDIO_INLINE=__declspec(dllexport)__inline" lj_*.c lib_*.c
llvm-lib /nologo /OUT:lua51.lib lj_*.obj lib_*.obj
```

For x86: `gcc -m32` for both host tools (a multilib gcc), no `-D P64`, `vm_x86.dasc`, and
`--target=i686-pc-windows-msvc /arch:SSE2`.

## 🧩 A mod

```cpp
#include "wxl/objects.hpp"
#include "wxl/sdk.hpp"

class Greeter final : public wxl::WorldScript
{
    void OnWorldEnter(uint32_t map_id, wxl_vec3) override
    {
        const wxl::Player me = wxl::Player::Local();
        if (wxl::Expected<std::string> name = me.Name())
            wxl::Log(WXL_LOG_INFO, "greeter", "hello, {}, on map {}", *name, map_id);
    }
};

Greeter g_greeter;

class GreeterMod final : public wxl::Mod
{
public:
    wxl_result OnLoad() override
    {
        wxl::ScriptMgr::Add(&g_greeter);
        return WXL_OK;
    }

    void OnReady() override { wxl::Log(WXL_LOG_INFO, "greeter", "client is up"); }
};

WXL_MOD(GreeterMod, "wxl.greeter", "Greeter")
```

Or write it in Lua, with nothing to build: a folder with an `init.lua`, run by the `wxl.lua`
mod on both clients, with every event and object above reachable by the same names.
[docs/lua-mods.md](docs/lua-mods.md) and [`examples/lua-hello/`](examples/lua-hello/).

[`template/`](template/) is the same thing as a buildable project, with its manifest.
[`examples/scout/`](examples/scout/) is a mod that does real work -- it reads the world,
serves the client's own Lua and watches the server traffic -- and
[docs/your-first-mod.md](docs/your-first-mod.md) builds it from nothing, one file at a time.

## 🔌 What a runtime offers

Events are grouped into fifteen script families: `WorldScript`, `RenderScript`, `ObjectScript`,
`AssetScript`, `NetScript`, `LuaScript`, `CombatScript`, `PlayerScript`, `GlueScript`,
`ChatScript`, `LootScript`, `ItemScript`, `QuestScript`, `GroupScript` and `SocialScript`. Override the hooks you want; the rest cost nothing. A hook takes its event's
arguments as parameters, `OnMessageReceived(uint32_t opcode, wxl::Bytes data, wxl::Answer
suppressed)`, in C++ and in Lua alike.

Beyond events, a runtime publishes services, and a mod asks for the ones it needs:

| Service | What it reaches | Portable? |
|---|---|---|
| `wxl.object` | units, players, game objects | yes |
| `wxl.script` | the client's Lua | yes |
| `wxl.storage` | archives and files, folders, providers and bytes a mod adds to them, content hashes, overrides, reloads | yes |
| `wxl.relay` | services mods publish for each other, answered later on the main thread | yes |
| `wxl.data` | the client's databases, served from a mod's files | yes |
| `wxl.net` | messages to and from the server | yes; no runtime publishes it yet |
| `wxl.ui` | frames and the interface | yes; no runtime publishes it yet |
| `wxl.point` | functions of the engine: run code before, instead of or after them, those the stock client inlined included (`wxl/deinlined.def`) | yes; one that exists on one runtime only says so |
| `wxl.m2file` | a parsed model file's arrays, read and pointed the runtime's way, for a mod that teaches the client a model format | yes |
| `wxl.m2` | the models objects draw with, and models a mod makes and hangs on them | yes |
| `wxl.gx` | which device the client draws with, and its D3D9 device, to draw a model batch again (`ModelBatchDrawn`) | yes |
| `wxl.engine` | the layout hash of each engine structure of `engine/`, as the runtime lays it out | yes |
| `wxl.camera` | the world camera: where it is, and placing it instead of the client each frame | yes |
| `wxl.input` | the pointer held at the centre of the window, for a mod that steers with the mouse | yes |
| `wxl.world` | the world's geometry: the first point of it on a segment | yes |
| `wxl.pose` | a unit's head turned over its animation | yes |
| `wxl.imgui` | Dear ImGui and ImPlot drawn in the game's frame, for a native mod's windows | yes |
| `wxl.cvar` | console variables: a mod's settings registered where the game keeps its own, read and set by the console and the interface, saved in Config.wtf, per account or per character | yes |
| `wxl.terrain` | the ground of the map: its height, rays against it, its grid, and its vertices set to other heights, which the client draws, walks on and collides with | yes |
| `wxl.animation` | an animation of the game played on a unit (a mining swing, an attack, a cast), once or looped, through the client's own animation of units and seen on this client only | yes |
| `wxl.draw` | shapes drawn in the world: lines, circles, rings, sectors, rectangles, polygons and grids lying on the ground, boxes, spheres and cylinders; for a frame, kept, or following an object; the game's textures on them, soft and shadowed lines; particles (bursts and kept emitters, bouncing on the terrain, soft sprites) | yes, on Direct3D 9 and the Vulkan renderer alike |
| `wxl.render` | drawing into the game's frame on the Vulkan renderer: the frame's points, images, buffers, pipelines of SPIR-V, compute dispatches, bone palettes, the game's model batches drawn again, its shaders replaced | yes; a Direct3D 9 device says it does not draw |
| `wxl.vk` | the Vulkan device and the frame's command buffer, for a mod that records its own commands | no: the Vulkan renderer only |
| `wxl.hook` | hook points a runtime names and keeps working | yes |
| `wxl.hook.raw` | a detour at a raw address | no: runtimes that load into a shipped binary |
| `wxl.patch` | bytes written into the image | no: runtimes that load into a shipped binary |

Each service has a typed facade with its name and version sealed in -- `wxl::ObjectService`,
`wxl::ScriptService`, `wxl::StorageService`, `wxl::DataService`, `wxl::NetService`,
`wxl::UiService`, `wxl::PointService`, `wxl::M2FileService`, `wxl::M2Service`, `wxl::GxService`,
`wxl::EngineService`, `wxl::CVarService`, `wxl::TerrainService`, `wxl::AnimationService`, `wxl::DrawService`, `wxl::RenderService`, `wxl::VkService`,
`wxl::HookService`, `wxl::RawHookService`, `wxl::PatchService` -- taking and
returning C++ types. `wxl::Service<T>` is still there for the raw table. Lua mods reach the same
services as `wxl.storage`, `wxl.data`, `wxl.script`, `wxl.hook`, `wxl.patch` and `wxl.m2`, built from the
same tables.

**Portable means a runtime may serve it, not that yours does.** A facade tests false and every
call through it answers `WXL_E_UNSUPPORTED` while the service is absent; require it in your
manifest and be refused early instead. Which services a given runtime publishes today is in
that runtime's own README.

A mod that requires a service a runtime does not have is refused, with the reason, before it loads.

## 📂 Layout

```
include/wxl/
    abi.h          the rules of the boundary: types, versioning, export
    events.def     the event table -- the single source of ids, names and args
    events.h       the event enum, and the argument structs fields.def declares
    fields.def     every argument struct, member by member, with what each member carries
    enums.def      the enumerations those members hold, with the name a script sees for each
    update_fields.def
                   every descriptor field of every kind of object, by the client's names, with
                   its dword index, size and value; generated from the client fork
    reflect.hpp    the events and their members as data, for a runtime that is not C++
    host.h         what a runtime offers a mod
    mod.h          what a mod exposes to a runtime
    expected.hpp   Expected<T>: a value or the wxl_result that stood in for it
    base.hpp       what every C++ header rests on: the runtime table, strings, the log, Service
    sdk.hpp        the C++ layer: Mod, the script families and their parameters, Subscription,
                   WXL_MOD
    objects.hpp    world objects as C++ values: the umbrella over objects/
    objects/       one header per kind the client knows: Object, Item, Container, Unit, Player,
                   GameObject, DynamicObject, Corpse; plus the view, the ObjectService facade,
                   ObjectMap, data kept per object until it leaves, and fields.hpp, the update
                   fields as typed constants (wxl::field::Unit::Health)
    services.hpp   every typed facade at once
    services/      one C header per service, its table (<service>.def: every member with the
                   kinds it takes and answers) and one .hpp with its typed facade; rows.hpp
                   reads the tables, for wxl.lua's modules and the facades' plain methods
    points.def     the engine points of wxl.point, generated from core's points.txt
    deinlined.def  the points the stock client inlined and its runtime makes real again, each
                   with the functions it was inlined into (a row for each)
engine/            the engine headers (wxl::engine::CM2Shared, CGUnit_C, M2Data...): the client's
                   structures laid out for x86 and x64, each with its layout hash, generated from
                   the client fork; check.hpp, the load-time check of those hashes; under their
                   own NOTICE.md, not this license
host/              the mod host: discovery, manifests, load order, registry, event bus
runtime/           service scaffolding every runtime shares: handles, wxl.object over a table of
                   reads (object_reads.def) each runtime implements
cmake/             wxl_add_mod(), which builds a mod for every target at once
schema/mod.json    the manifest, as a JSON Schema
lua/               wxl.lua: the Lua mod runtime, a mod itself (LuaJIT, one state per Lua mod);
                   types/wxl.d.lua, the API for an editor, generated from the tables above
template/          a buildable mod to start from
examples/scout/    a worked example: the world, the client's Lua, the server traffic
examples/lua-hello/ the same in Lua: a folder, no build
examples/lua-services/ a Lua mod using the services: files, a search path, databases, the
                   client's own script
examples/memory-report/ a Lua mod that writes where the client's memory goes into a report
examples/model-swap/ a C++ mod changing the engine through a point: the model cache
examples/asset-provider/ a C++ mod serving an add-on from memory through an asset provider, and
                   converting it through a transform
examples/lua-assets/ a Lua mod serving a file and watching what the engine loads
examples/probe/    a Lua test tool: every event the runtime raises, and /mark, into the log
tests/             the contract, the mod host, the shared runtime and wxl.lua, each against fakes
tests/ingame/      the same against a real client: test mods and wxl.selftest, which drives them
                   from the game and logs PASS or FAIL per check
```

A mod links `wxl::sdk` and nothing else; one that reads the engine's structures links
`wxl::engine` too. A runtime links `wxl::host` and `wxl::runtime` too, and
supplies only the bindings that reach its own client.

## 🔢 Versioning

`major.minor.feature`, in [`abi.h`](include/wxl/abi.h): today `0.1.40`.

- **Major** changes when an existing field, signature or id changes meaning. A runtime refuses a
  mod built against a different major.
- **Minor** is a stage of the platform: `0.1` is the first scope. While the major is 0, a runtime
  refuses a mod of a different minor.
- **Feature** changes when something is appended: an event, a service, a field at the end of a
  struct. A runtime accepts a mod that needs a feature at or below its own and refuses a higher
  one.

A manifest's `"abi"` is the lowest version the mod needs. A mod built against newer headers may
ask for less and check `wxl_host.abi_feature` before it uses what came in between.

Event ids and stage numbers are appended, never renumbered, never removed.

## 📚 Documentation

- [Your first mod](docs/your-first-mod.md) -- a tutorial, from an empty folder to a mod that does real work
- [Building a mod](docs/building-a-mod.md) -- the manifest, the targets, the C++ layer
- [Lua mods](docs/lua-mods.md) -- a mod with nothing to build: the folder, the API, reload, the sandbox
- [Profiling](docs/profiling.md) -- where frame time goes, mod by mod, in the Tracy viewer, and zones of your own
- [Reading the log](docs/the-log.md) -- where the log is, what a launch says, the level setting, marking a test with `/mark`
- [`schema/mod.json`](schema/mod.json) -- the manifest schema, for your editor
- [`template/`](template/) -- a working mod to copy
- [`examples/scout/`](examples/scout/) -- the worked example the tutorial builds
- [`examples/model-swap/`](examples/model-swap/) -- the engine changed through a point ("Changing the engine: points" in Building a mod)
- [`examples/asset-provider/`](examples/asset-provider/) and [`examples/lua-assets/`](examples/lua-assets/) -- files served and converted ("Serving and converting assets" in both guides)

## 💬 Support

- Something does not behave as these headers say: open an issue, and say which runtime.
- A runtime refuses your mod: the reason is in its log, and it names the service and the version.

## 🤝 Contributing

1. Fork and branch.
2. Keep the boundary plain C: no std type, vtable or exception crosses it.
3. Append ids, never renumber them.
4. Add the conformance check with the feature -- a contract without a test is a suggestion.
5. Open a pull request.

**Contributors.** Tyrallis brought the model services (`wxl.m2file`, `wxl.m2`, `wxl.gx`), the
character appearance seams behind `AppearanceChanged`, and the recompiled client's skins over 65535
indices and textures over 4 MB (sdk, core and client pull requests #1), then `wxl.m2`'s models of
the world and bone watchers, the cast's ground point, and the recompiled client's additive blend,
retail animation ids, Ogg Vorbis sounds and later bodies' geosets (pull requests #4).

## 📄 License

MIT. See [LICENSE](LICENSE).

These headers compile into whoever includes them, so they are deliberately permissive: a mod built
against this SDK carries no obligation from it.

The engine headers under `engine/include/wxl/engine/` are the exception: they describe the 3.3.5a
client's own structures, generated from its decompilation, and no license is granted over them.
See [engine/NOTICE.md](engine/NOTICE.md). `deps/` holds unordered_dense (MIT) and xxHash (BSD-2,
used by the tests to check the layout hashes), each with its license file.

<div align="center">

[⬆ Back to top](#warcraftxl-sdk)

</div>
