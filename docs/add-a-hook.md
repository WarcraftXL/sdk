# Add a hook

Change what a client function does. Use an **interceptor** first: typed arguments, any number of
extensions on the same function, and a fault in your handler disables your extension instead of
ending the game. Use a **raw hook** only when an interceptor cannot do it; it needs the `low_level`
permission and runs unguarded.

Functions are named by their entry in `wxl/offsets/` (link `WarcraftXL::offsets`, or
`WarcraftXL::game` which brings it): an address, and a `...Fn` alias of its signature.

## Interceptors

Example: `examples/intercept` caps the master volume and counts interface sounds.

<!-- quote: examples/intercept/src/Quiet.cpp -->
```cpp
        cap = Intercept<off::SE2::SetMasterVolumeFn, off::SE2::SetMasterVolume>().Before([](auto& call)
        {
            float& volume = call.template Arg<0>();
            if (volume > kMaxVolume) volume = kMaxVolume;
        });
        count = Intercept<off::Script_PlaySoundFn, off::Script_PlaySound>().After([this](auto&)
        {
            ++sounds;
        });
        if (!cap || !count) Log::Warn("the sound functions could not be intercepted");
```

`Intercept<Fn, Address>()` is the interceptor of one function. Add handlers to it:

| Handler | Runs | May |
|---|---|---|
| `Before(h)` | before the function, in priority order | change `call.Arg<I>()`; `call.Return(value)` skips the function |
| `Replace(h)` | instead of the function (the first by priority only) | call `call.Original()`; return the result |
| `After(h)` | after the function, in priority order | read and change `call.Result()` |

Each returns an `Interception`: keep it (a member of your extension), the handler lives as long as it
does. Give a priority as the second argument (`priority::First`, `Default`, `Last`). In a generic lambda, write
`call.template Arg<0>()`.

A handler that faults is undone (the arguments and the result go back to what it found) and the
extension is disabled; the call goes on. A call of the same function from inside your handler goes
straight to the client.

## Raw hooks (low level)

Example: `examples/hook` records the largest file size the client asks for. Its `wxl.json` grants
the permission:

<!-- quote: examples/hook/wxl.json -->
```json
  "permissions": { "low_level": true },
```

The detour has the function's exact signature and convention; `Hook<Fn>` is the next link of the
chain, which ends at the client function:

<!-- quote: examples/hook/src/FileSizes.cpp -->
```cpp
    Hook<offsets::SFileGetFileSizeFn> g_fileSize;
    uint32_t                          g_largest = 0;

    // The detour: same signature and convention as the client function. Unguarded: keep it simple.
    uint32_t __stdcall FileSize(void* file, uint32_t* high)
    {
        const uint32_t size = g_fileSize(file, high); // the next link of the chain, then the client
        if (size > g_largest) g_largest = size;
        return size;
    }
```

Attach it to a hook point by name (the list is `wxl/HookPoints.def`), or to an address with
`Attach(label, address, detour)`:

<!-- quote: examples/hook/src/FileSizes.cpp -->
```cpp
        if (!g_fileSize.Attach("SFileGetFileSize", &FileSize))
```

- Without `low_level`, the core refuses the hook and logs the line to add to `wxl.json`; so does the
  fake host of `wxl_add_load_test`.
- Not calling the next link skips the extensions behind yours and the client function.
- A thiscall method's `Fn` is `__fastcall` with a dummy `edx`: `(self, edx, ...)`, pass `nullptr`.
- Bytes of the client are changed with `Patch` (`wxl/Patch.hpp`), also `low_level`: the core
  checks the expected bytes first and puts them back when the extension is disabled.

## Resident hooks (low level)

When an extension faults or declines to start, the core disables it: its handlers stop, and its hooks
and patches are taken back. For most code that is the safe outcome. For code that hands out memory or
handles that others keep, it is not: an allocator's `free` taken out while the game still holds its
blocks would hand them to a heap that never made them. Attach such a hook as **resident**:

```cpp
    Hook<offsets::freeFn> g_free;

    if (!g_free.AttachResident("free", offsets::free, &Free))
        Log::Warn("free: no resident hooks in this core");
```

- A resident hook stays in its chain when its extension is disabled or faults; the core's log says how
  many it kept (`kept: 5 resident hooks`). Its extension's DLL stays loaded for the rest of the process.
- Its detour must not depend on the rest of the extension: its other hooks, handlers and services may be
  gone. A fault inside the detour itself still ends the game, as a fault in any raw hook does.
- `AttachResident` answers false on a core older than resident hooks: decide then whether the extension
  can do without (an allocator should not start).
- From C: `HookAttachEx(name, target, detour, &original, priority, WXL_HOOK_RESIDENT)`; check that the
  core's table reaches `HookAttachEx` (its `structSize`) before calling it.
