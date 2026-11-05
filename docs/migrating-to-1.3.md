# Migrating an extension to 1.3

1.3 is a break: the core refuses an extension built for 1.2 (`WXL_API_VERSION` is 2). Rebuild it
against the 1.3 SDK after the steps below. The new ways of doing things have a how-to each, listed
in [getting-started.md](getting-started.md).

## What breaks

| 1.2 | 1.3 |
|---|---|
| the extension builds inside `wxl-core/extensions/` | its own repository, against the installed SDK (`find_package(WarcraftXL 1.3)`) |
| `#include "game/..."`, `"offsets/..."`, `"engine/events/Event.hpp"` | `"wxl/game/..."`, `"wxl/offsets/<Class>.hpp"`, `"wxl/Events.hpp"` |
| `"wxl/PluginApi.h"`, `"wxl/Common.h"`, `"wxl/Version.h"`, `"wxl/<Service>Api.h"` | `"wxl/abi/Plugin.h"`, `"wxl/abi/Common.h"`, `"wxl/abi/Version.h"`, `"wxl/abi/<Service>.h"` |
| `wxl::structure::m2` | `wxl::formats::m2` |
| no `wxl.json`: loaded with a warning | refused; `wxl_add_extension` writes it |
| `WXL_Api::Subscribe(event, fn, user)` | `Connect(event, subject, priority, fn, user, release, &out)`, `Disconnect`, `Connected` |
| `WXL_DECLARE_EXTENSION(name, version)`, `AddScripts()` | a class derived from `Extension`, its `OnStart`, and `Export<MyExtension> extension;`; the name and version come from `wxl.json` |
| `ScriptMgr`, `WorldScript`, `RenderScript`, `ObjectScript`, `AssetScript` | the `Extension` class: override the events you need (`wxl/Overrides.def`); only those are connected |
| `ScriptMgr::Bind`, `Api`, `Name` | `Api()`, `ExtensionName()` (`wxl/Core.hpp`), `Name()` and `Version()` in the extension |
| `ScriptMgr::Log`, `Log(level, "%s", ...)` | `Log::Info("{}", ...)`, `Warn`, `Error`, `Debug`: `std::format` syntax (`wxl/Log.hpp`) |
| `ScriptMgr::GetInterface<T>(name, v)` | `Service<T>`: the name and version from `ServiceTraits<T>` in the service's header; `OnReady` once it is published |
| `GetInterface(name, v)`: exactly `v` | the highest published version at or above `v` |
| `wxl::ext::EventScript`, `Subscribe` | `On<Tag>(handler)`, owned by the extension (`wxl/Listener.hpp`) |
| `wxl::Object(void*)`, `Unit(void*)`, `Player(void*)`, `GameObject(void*)` | `Object::FromPointer(p)`, `FromGuid(guid)` |
| `obj.Raw()` | `obj.Resolve()`: `Result<void*>` |
| `Guid()`, `Position()`, `Facing()`, `TypeMask()`, `Model()`, `Reaction()` return values | they return `wxl::Result<T>` |
| `if (obj)`: the pointer is not null | the object is still resident |
| `HookAttach`, `HookAttachByName`, `Hook` for anyone | `"permissions": { "low_level": true }` in `wxl.json`, or `Intercept` |
| writing client memory yourself | `Patch` (`low_level`): checked first, given back on disable |
| hand-written offsets (`wxl::offsets::engine::camera::kViewSet`) | generated per class: `wxl::offsets::CGCamera::SetView`, `SetViewFn` |
| hook point `"Camera.ViewSet"` | the function's name in `wxl/offsets`: `"CGCamera::SetView"` |
| `wxl/Config.hpp`, `wxl/CfgParse.hpp` (environment, `.cfg`) | annotated settings, one ini per extension (`wxl/Settings.hpp`) |
| `WXL_Api::Ui*` (13 members), `wxl/OverlayApi.h` | the `wxl.overlay` service of wxl-overlay |
| `wxl/FdidApi.h`, `LoadPoolApi.h`, `LodApi.h`, `InterfaceApi.h`, `JsonApi.h`, `ModernBlpApi.h`, `M2ArenaApi.h` | in the extension that publishes the service |
| `world::SetTerrainPhase`, `ClearTerrainPhase` | the `wxl.phasing` service of wxl-phasing |
| `world::MapId()` | `world::CurrentMapId()` |
| `wxl/game/Gfx.hpp` debug shapes, `gx::Device9`, `gx::CompilePixelShader`, `EnsureBackbufferTarget`, `DrawFullscreenQuad`, `gx::ScopedXform` | gone: draw with your own D3D9 code from `OnWorldSceneEnd` |
| `world::RequestTerrainReload`, `world::GroundZ`, `xml::AnimGroupPlayReversed` | gone: build them from the bindings they used |
| `wmo::GetBatchMaterialIndex` and the u16 MOBA material | in the extension that reads newer WMOs |
| `kMagicMD21`, `kMagicAFM2`, `M2Texture`, `M2Batch`, `M2CompBone`, `M2Camera`, `M2Ribbon` | `wxl/offsets/M2*.hpp` (the client's layouts); newer containers are an extension's |
| the model, skin, batch, ribbon and chunk-build events, `OnAdtSplitTileLoad`, `OnGrassWind`, `OnAdtHeightBlend`, `OnM2NativeLoad`, `ModelScript` | removed: nothing raises them in 1.3; the other events are renumbered (docs/events.md) |
| the core fixes client bugs, unlocks the glue screen, widens the mip scratch, shows an overlay | each is an extension (step 10) |

## Steps

1. **Build on your own.** Install the SDK and write a `CMakeLists.txt` with `find_package` and
   `wxl_add_extension` ([getting-started.md](getting-started.md)). Link `WarcraftXL::game` for
   `wxl/game/` and `wxl/objects/`, `WarcraftXL::offsets` for `wxl/offsets/` alone. Copy into your
   repository whatever a `shared.cmake` pulled from the core's sources.
2. **Fix the includes** with the table above:

   ```sh
   grep -rl '#include "\(game\|offsets\)/\|engine/events/Event.hpp' src | xargs sed -i -E \
     's@#include "(game|offsets)/@#include "wxl/\1/@; s@"engine/events/Event.hpp"@"wxl/Events.hpp"@'
   ```

3. **Give it a `wxl.json`** (`MANIFEST` in `wxl_add_extension`, or none to have one generated). The
   build sets `extension.abi`, and the extension reads its name and version from it. Add
   `"permissions": { "low_level": true }` if it uses `Hook`, `HookAttach` or `Patch`;
   `"load": "before-archives"` if it must load before the client mounts its archives.
4. **One Extension class**: move what `AddScripts()` did into `OnStart`, the script hooks into
   overrides of the same name, and end the file with `Export<MyExtension> extension;`. A hand-written
   `WXL_Load` that called `Subscribe` uses an override or `On<Tag>` instead.
5. **Log and services**: `Log::Info("{}", x)` for the printf-style lines, `Service<T>` with the
   service's traits for the lookups ([publish-a-service.md](publish-a-service.md)).
6. **Objects**: build them with `FromPointer`/`FromGuid`; read through `Result`
   (`if (auto pos = unit.Position()) Use(*pos);` or `.ValueOr(fallback)`). A read on an object that
   left view answers `Error::Expired` for good.
7. **Offsets and hook points**: use the generated names; a thiscall `Fn` takes `(self, edx, ...)`,
   pass `nullptr` for `edx`.
8. **Settings**: move each `.cfg`/environment value into a `WXL_SETTINGS` struct and delete the old
   file ([add-settings.md](add-settings.md)).
9. **Services of other extensions**: copy their headers into your sources (for example `src/api/`)
   and use them from `OnReady` ([publish-a-service.md](publish-a-service.md)).
10. **Behaviour the core dropped**: a client that relied on it installs the extension that has it:

    | What | Extension |
    |---|---|
    | the overlay, its Settings and Profiles panels | wxl-overlay |
    | the large-M2 arena, wide index starts, added blend states | wxl-modern-m2 |
    | the wider mip scratch, BLP and DDS transcoding | wxl-modern-blp |
    | terrain phases | wxl-phasing |
    | particle, async-read, liquid-row and texture-upload fixes, the archive-mount guard, the glue-screen unlock, `Patch-<name>.MPQ` | wxl-engine-fixes |

11. **Test without the client**: `wxl_add_load_test(<name>)`, then `ctest`.

## What is new and needs nothing

Interceptors (`wxl/Intercept.hpp`), handlers with priorities and answers (`wxl/Listener.hpp`),
signals (`wxl/Signal.hpp`), coroutines (`wxl/Task.hpp`), observable properties
(`unit.HealthChanged()`), the `Game()` facade, and fault isolation: a fault in a guarded handler
disables the extension, not the game (raw `Hook` detours stay unguarded).
