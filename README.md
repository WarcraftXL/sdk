<div align="center">

# WarcraftXL SDK

**The contract a WarcraftXL mod is written against**

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![ABI](https://img.shields.io/badge/ABI-0.5-blue.svg)](include/wxl/abi.h)
[![C++](https://img.shields.io/badge/C%2B%2B-17%20and%20up-blue.svg)](#-getting-started)
[![Header only](https://img.shields.io/badge/header--only-yes-lightgrey.svg)](include/wxl)

</div>

---

> [!IMPORTANT]
> ABI major 0 means the contract is still moving: any release may break any other. Ids and stage
> numbers are already append-only, but signatures are not frozen until 1.0.

## 🚀 Overview

Write a mod once, in C++, and run it on every WarcraftXL runtime: the stock `Wow.exe` 3.3.5a and
the recompiled client alike. This repository declares what a mod exposes, what a runtime offers it,
and the rules of the boundary between them. There is nothing to link and nothing to ship.

### Key features

- **One source, every target.** `wxl_add_mod()` builds a mod for each architecture in one command.
- **Object-oriented.** Derive from `wxl::Mod` and the script families; world objects are C++
  values you can compare, hash and iterate, not handles you dereference by hand.
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
- A C++17 compiler (MSVC 19.3+, GCC 11+, Clang 14+)
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

## 🧩 A mod

```cpp
#include "wxl/objects.hpp"
#include "wxl/sdk.hpp"

class Greeter final : public wxl::WorldScript
{
    void OnWorldEnter(const wxl_world_args&) override
    {
        const wxl::Player me = wxl::Player::Local();
        if (wxl::Expected<std::string> name = me.Name())
            wxl::Log(WXL_LOG_INFO, "greeter", "hello, " + *name);
    }
};

Greeter g_greeter;

class GreeterMod final : public wxl::Mod
{
    wxl_result OnLoad() override
    {
        wxl::ScriptMgr::Add(&g_greeter);
        return WXL_OK;
    }

    void OnReady() override { wxl::Log(WXL_LOG_INFO, "greeter", "client is up"); }
};

WXL_MOD(GreeterMod, "wxl.greeter", "Greeter", 1)
```

Or write it in Lua, with nothing to build: a folder with an `init.lua`, run by the `wxl.lua`
mod on both clients, with every event and object above reachable by the same names.
[docs/lua-mods.md](docs/lua-mods.md) and [`examples/lua-hello/`](examples/lua-hello/).

[`template/`](template/) is the same thing as a buildable project, with its manifest.
[`examples/scout/`](examples/scout/) is a mod that does real work -- it reads the world,
serves the client's own Lua and watches the server traffic -- and
[docs/your-first-mod.md](docs/your-first-mod.md) builds it from nothing, one file at a time.

## 🔌 What a runtime offers

Events are grouped into seven script families: `WorldScript`, `RenderScript`, `ObjectScript`,
`AssetScript`, `NetScript`, `LuaScript` and `CombatScript`. Override the hooks you want; the rest
cost nothing.

Beyond events, a runtime publishes services, and a mod asks for the ones it needs:

| Service | What it reaches | Portable? |
|---|---|---|
| `wxl.object` | units, players, game objects | yes |
| `wxl.script` | the client's Lua | yes |
| `wxl.storage` | archives and files | yes |
| `wxl.net` | messages to and from the server | yes; no runtime publishes it yet |
| `wxl.ui` | frames and the interface | yes; no runtime publishes it yet |
| `wxl.gx` | the client's D3D9 device and the model batches it draws | no: the x64 runtime, on a D3D9 device |
| `wxl.m2file` | model files as the client's loader reads them, for a mod that teaches it a format | no: the x64 runtime |
| `wxl.hook` | hook points a runtime names and keeps working | yes |
| `wxl.hook.raw` | a detour at a raw address | no: runtimes that load into a shipped binary |
| `wxl.patch` | bytes written into the image | no: runtimes that load into a shipped binary |

Each service has a typed facade with its name and version sealed in -- `wxl::ObjectService`,
`wxl::ScriptService`, `wxl::StorageService`, `wxl::NetService`, `wxl::UiService`,
`wxl::HookService`, `wxl::RawHookService`, `wxl::PatchService` -- taking and returning C++ types.
`wxl::Service<T>` is still there for the raw table.

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
    reflect.hpp    the events and their members as data, for a runtime that is not C++
    host.h         what a runtime offers a mod
    mod.h          what a mod exposes to a runtime
    expected.hpp   Expected<T>: a value or the wxl_result that stood in for it
    sdk.hpp        the C++ layer: Mod, the script families, Subscription, Service, WXL_MOD
    objects.hpp    world objects as C++ values: the umbrella over objects/
    objects/       one header per kind the client knows: Object, Item, Container, Unit, Player,
                   GameObject, DynamicObject, Corpse; plus the view and the ObjectService facade
    services.hpp   every typed facade at once
    services/      one C header per service, and one .hpp with its typed facade
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
tests/             the contract, the mod host, the shared runtime and wxl.lua, each against fakes
```

A mod links `wxl::sdk` and nothing else. A runtime links `wxl::host` and `wxl::runtime` too, and
supplies only the bindings that reach its own client.

## 🔢 Versioning

`major.minor`, in [`abi.h`](include/wxl/abi.h).

- **Major** changes when an existing field, signature or id changes meaning. A runtime refuses a
  mod built against a different major.
- **Minor** changes when something is appended. A runtime accepts a minor at or below its own and
  refuses a higher one.

Event ids and stage numbers are appended, never renumbered, never removed.

## 📚 Documentation

- [Your first mod](docs/your-first-mod.md) -- a tutorial, from an empty folder to a mod that does real work
- [Building a mod](docs/building-a-mod.md) -- the manifest, the targets, the C++ layer
- [Lua mods](docs/lua-mods.md) -- a mod with nothing to build: the folder, the API, reload, the sandbox
- [`schema/mod.json`](schema/mod.json) -- the manifest schema, for your editor
- [`template/`](template/) -- a working mod to copy
- [`examples/scout/`](examples/scout/) -- the worked example the tutorial builds

## 💬 Support

- Something does not behave as these headers say: open an issue, and say which runtime.
- A runtime refuses your mod: the reason is in its log, and it names the service and the version.

## 🤝 Contributing

1. Fork and branch.
2. Keep the boundary plain C: no std type, vtable or exception crosses it.
3. Append ids, never renumber them.
4. Add the conformance check with the feature -- a contract without a test is a suggestion.
5. Open a pull request.

## 📄 License

MIT. See [LICENSE](LICENSE).

These headers compile into whoever includes them, so they are deliberately permissive: a mod built
against this SDK carries no obligation from it.

<div align="center">

[⬆ Back to top](#warcraftxl-sdk)

</div>
