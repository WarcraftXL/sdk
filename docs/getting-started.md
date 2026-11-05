# Getting started

From nothing to an extension the client loads: install the SDK, build `examples/hello`, test it
without the client, then copy it into a client.

## 1. Install the SDK

You need CMake 3.20 or later and a C++20 compiler that targets 32-bit Windows: Visual Studio 2022,
clang-cl, or clang mingw-w64. The client is a 32-bit program, so everything builds for **Win32**.

```sh
cmake -S wxl-sdk -B build -A Win32
cmake --install build --prefix C:/wxl-sdk
```

From Linux, replace `-A Win32` with `-G Ninja -DCMAKE_TOOLCHAIN_FILE=<an x86 Windows toolchain file>`.

## 2. An extension's files

An extension is a folder with a `CMakeLists.txt`, a manifest `wxl.json` and its sources. This is
`examples/hello`.

`CMakeLists.txt`:

<!-- quote: examples/hello/CMakeLists.txt -->
```cmake
find_package(WarcraftXL 1.3 CONFIG REQUIRED)

wxl_add_extension(wxl-hello
    SOURCES Hello.cpp
    MANIFEST wxl.json
    LINK WarcraftXL::game)
wxl_add_load_test(wxl-hello ARGS --world 571 --expect-log "wxl-hello: wxl-hello 1.0.0 started"
                                     --expect-log "wxl-hello: map 571 after 3 ticks")
```

- `wxl_add_extension` builds `wxl-hello.dll` and writes its `wxl.json` beside it.
- `LINK WarcraftXL::game` brings the game bindings (`wxl/Game.hpp`, `wxl/objects/`).
- `wxl_add_load_test` adds a test that loads the DLL without a client (step 4).

`wxl.json`, the manifest the core reads before it loads the DLL:

<!-- quote: examples/hello/wxl.json -->
```json
{
  "manifest": 1,
  "extension": {
    "id": "wxl-hello",
    "version": "1.0.0",
    "abi": "1.3",
    "entry": "wxl-hello.dll"
  },
  "license": "GPL-3.0-or-later"
}
```

`id` and `entry` match the name given to `wxl_add_extension`. `abi` is rewritten at build time with
the SDK version you build against. Without `MANIFEST`, a minimal `wxl.json` is generated. An
extension that needs raw hooks or patches adds `"permissions": { "low_level": true }`
(see [add-a-hook.md](add-a-hook.md)).

`Hello.cpp`: one class derived from `Extension`, the events it overrides, and its `Export`.

<!-- quote: examples/hello/Hello.cpp -->
```cpp
#include <wxl/Game.hpp>
#include <wxl/wxl.hpp>

using namespace wxl;

class Hello : public Extension
{
    uint32_t ticks = 0;

    void OnStart() override
    {
        Log::Info("{} {} started", Name(), Version());

        On<TargetChanged>([](auto&)
        {
            if (auto me = Game().Player())
                Log::Info("level {} player changed target", me->Level().ValueOr(0));
        });
    }

    void OnUpdate(float, uint32_t) override
    {
        ++ticks;
    }

    void OnWorldEnter(MapId map) override
    {
        Log::Info("map {} after {} ticks", map, ticks);
    }
};

Export<Hello> extension;
```

- Only the events the class overrides are connected (here `OnUpdate` and `OnWorldEnter`).
- `OnStart` runs when the core loads the extension: connect handlers, hook, publish there.
  `On<TargetChanged>` connects one; it lasts until the extension stops.
- `Log::Info` takes `std::format` syntax and tags the line with the extension's name.
- `Export<Hello> extension;` writes the entry points, once per DLL. The name and version come from
  `wxl.json`: nothing in the code repeats them.

## 3. Build

```sh
cmake -S examples/hello -B build-hello -A Win32 -DCMAKE_PREFIX_PATH=C:/wxl-sdk
cmake --build build-hello --config Release
```

The result is `build-hello/Extensions/wxl-hello/`: `wxl-hello.dll` and `wxl.json`.

## 4. Test without the client

```sh
ctest --test-dir build-hello -C Release --output-on-failure
```

The test runs the SDK's fake host: it reads the manifest, loads the DLL, calls `WXL_Load`, raises
`OnExtensionsLoaded` and three `OnUpdate` (and `OnWorldEnter` with `--world`), unloads the DLL (which
stops the extension), then prints what the extension did:

```
extension: wxl-hello 1.0.0, load engine, low_level no
WXL_Load: 1
connects: OnUpdate
connects: OnWorldEnter
connects: OnTargetChanged
log: wxl-hello: wxl-hello 1.0.0 started
log: wxl-hello: map 571 after 3 ticks
0 failure(s)
```

`--expect-log "<line>"` fails the test when that log line is missing. A hook on an unknown point, a
raw hook or patch without `low_level`, or a connection still live once the DLL is unloaded, fails it
too. From Linux, set `CMAKE_CROSSCOMPILING_EMULATOR`
to a wine wrapper before configuring so the test runs under wine.

## 5. Put it in the client

Copy `build-hello/Extensions/wxl-hello/` into the client's `Extensions/` folder, next to `Wow.exe`.
Or configure with `-DCLIENT_PATH=<client folder>` and every build copies it there. The core logs
what it loaded, or why it refused it, in `Logs/wxl-core.log`.

## Next

- [listen-to-an-event.md](listen-to-an-event.md): overrides, `On` handlers and signals.
- [add-a-hook.md](add-a-hook.md): change what a client function does.
- [publish-a-service.md](publish-a-service.md): offer functions to other extensions.
- [add-settings.md](add-settings.md): settings the player changes.
- Every example: `examples/`; what changed since 1.2: [migrating-to-1.3.md](migrating-to-1.3.md).
