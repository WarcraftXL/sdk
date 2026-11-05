# Add settings

Settings are values the player changes without rebuilding: in the extension's ini file, with the
`/wxl` chat command, in a settings screen, or by loading a profile. You declare them once, as an
annotated struct; the core keeps them.

Example: `examples/settings` greets the player some seconds after they enter the world.

## 1. Declare them

A header that includes only `wxl/Annotate.hpp`. `WXL_SETTINGS("Section")` names the ini section; each
setting is a field with `WXL_SETTING(...)`, a `///` line saying what it does, and its default:

<!-- quote: examples/settings/src/GreeterSettings.hpp -->
```cpp
struct WXL_SETTINGS("Greeter") GreeterSettings
{
    /// What the greeting says; %u is the player's level.
    WXL_SETTING() std::string Text = "Welcome back, level %u.";
    /// Seconds after entering the world before the greeting.
    WXL_SETTING(range(0, 30), step = 0.5) float Delay = 2.0f;
    /// How many times a session it greets.
    WXL_SETTING(range(1, 10)) int Times = 1;
    /// Whether it greets at all.
    WXL_SETTING() bool Enabled = true;
};
```

Types: `bool`, integers, `float`/`double`, `std::string`. Options, each optional: `range(min, max)`,
`step = n`, `ui = slider|checkbox|input` (how a settings screen shows it).

## 2. Generate the traits

Name the header in `wxl_add_extension`:

<!-- quote: examples/settings/CMakeLists.txt -->
```cmake
wxl_add_extension(wxl-greeter
    SOURCES src/Greeter.cpp
    SETTINGS src/GreeterSettings.hpp
    LINK WarcraftXL::game)
```

The settings tool (`tools/settings/settings.lua`, run with LuaJIT and libclang 19) writes
`GreeterSettings.settings.hpp` beside the header whenever the header changes. **Commit that file**:
where the tool is missing, the build uses it as it is. A header the tool cannot read fails the build
with the file, the line and what to fix (a field without `WXL_SETTING`, without a `///` doc, a range
on a `bool`...).

## 3. Use them

Include the generated header, keep a `Settings<S>` in your extension, and `Start()` it in `OnStart`:

<!-- quote: examples/settings/src/Greeter.cpp -->
```cpp
    Settings<GreeterSettings> settings;
    Task                      greeting;
    int                       greeted = 0;

    void OnStart() override
    {
        settings.Start();
    }
```

`Start()` declares every setting with the struct's initializers as defaults, reads the current
values, and follows the player's changes. When the core cannot take them it logs why, and the
defaults apply (`TryStart()` answers the reason instead). Read them through `->`:

<!-- quote: examples/settings/src/Greeter.cpp -->
```cpp
        if (settings->Enabled && greeted < settings->Times) greeting = Greet();
```

- `settings.Set(&GreeterSettings::Times, 3)` changes one from code (saved; a value out of range is
  refused and logged, `TrySet` answers why).
- `On(settings.Changed(), handler)` runs once the struct holds a new value.

## Where the player changes them

- The ini: `Extensions/wxl-greeter/wxl-greeter.ini`, written the first time with the defaults, each
  `///` line becoming a `;` comment; edit it while the game is closed. A value that does not read, or
  is out of range, falls back to the default with a warning in the log.

  ```ini
  [Greeter]
  ; Seconds after entering the world before the greeting. Range: from 0 to 30. Default: 2.
  Delay = 2
  ```

- In game: `/wxl get` lists the extensions with settings, `/wxl get wxl-greeter` their values,
  `/wxl set wxl-greeter.Delay 5` changes one.
- A settings screen drawn by an extension, and saved profiles (`wxl.profiles`), go
  through the same service.
