# Profiling a mod with Tracy

A frame of the client lasts a few milliseconds. When it lasts longer, the game stutters, and the
first question is where those milliseconds went: the client's own work, the events the runtime
raised, or the handlers of one mod. The runtime carries a profiler that answers it, and this
guide shows you how to read it and how to add your own measurements to it.

The profiler is [Tracy](https://github.com/wolfpld/tracy). It has two halves: a small client
inside the game, which WarcraftXL.dll already contains, and a viewer, a program you run on the
game's computer or on another one. The viewer connects to the game, and from then on draws every
frame on a timeline, with what ran inside it.

The client is off until you turn it on (section 2): a player who never asks for it has no port
open. Once on, it records nothing while no viewer is connected, and every measurement point costs
a single check, a few nanoseconds, so your own measurements can stay in the mod you ship.

## 1. Get the viewer

The viewer must be the same release as the client in the runtime: Tracy changes the way they
talk to each other between releases, and a viewer of another version refuses the connection,
saying the protocol is incompatible. This runtime carries **Tracy 0.14.1**.

1. Open [the release page of Tracy 0.14.1](https://github.com/wolfpld/tracy/releases/tag/v0.14.1).
2. Download `windows-0.14.1.zip` for Windows, `linux-0.14.1.zip` for Linux or
   `macos-0.14.1.zip` for a Mac. `tracy.pdf` on the same page is Tracy's own manual.
3. Unzip it anywhere and run the viewer inside, `tracy-profiler.exe` on Windows. Nothing needs
   installing.

## 2. Connect it to the game

### Turn the profiler on

Create a file named `WarcraftXL.ini` next to the game's executable (beside `Wow.exe` or
`WowClient64.exe`), or open it if it is there already, and add:

```ini
[profile]
enabled = 1
```

Start the game, x86 or x64. The header at the top of `Logs/WarcraftXL.log` says what the profiler
does, and a moment later it is ready:

```
[INFO ] [session] profiler: on ([profile] enabled in WarcraftXL.ini), Tracy 0.14.1 on demand, port 8086, listen localhost: 127.0.0.1 only
[INFO ] [profile] profiler up (Tracy 0.14.1, on demand): a viewer connects to 127.0.0.1 port 8086
```

Without the line, or with `enabled = 0`, the header says `profiler: off` and nothing listens.

The section takes two more lines, both optional:

| Line | What it does |
|---|---|
| `port = 8086` | the TCP port the viewer connects to, from 1 to 65535; 8086 when the line is missing |
| `listen = localhost` | who can connect: `localhost`, only programs on the game's own computer (the address `127.0.0.1`), or `any`, other computers too. `localhost` when the line is missing |

The environment variable `WXL_PROFILE` overrides `enabled` when it is set: `WXL_PROFILE=1` turns
the profiler on for one launch without touching the file, `WXL_PROFILE=0` keeps it off whatever
the file says. In Steam's launch options that is `WXL_PROFILE=1 %command%`.

### The viewer on the same computer

This is the case `listen = localhost` is made for. In the viewer's window, type `127.0.0.1` in
the address field and press **Connect** (`127.0.0.1:8090` if you set another port).

Under Proton or Wine the game runs on Linux, and it listens on the Linux machine itself: a viewer
started on that Linux machine connects to `127.0.0.1` the same way.

### The viewer on another computer

When the game runs on one computer and you watch it from another (a game streamed to your
desktop, say), the viewer runs where you are, and the game must accept a connection from the
network:

1. Set `listen = any` under `[profile]`, and start the game again. The header now says
   `listen any: every interface, other computers can connect`.
2. Let the port in through the firewall of the game's computer:
   - **On Windows,** the first time the game listens, Windows may ask whether to allow it on the
     network: allow it on private networks. If it does not ask and the viewer cannot connect,
     add an inbound rule for TCP port 8086 in *Windows Defender Firewall*.
   - **Under Proton or Wine,** the game listens on the Linux machine's network, and Windows'
     firewall plays no part: open TCP port 8086 in the Linux machine's firewall, if it has one.
3. In the viewer, type the address of the game's computer, `192.168.1.20` say, and press
   **Connect**.

Anyone who can reach that port can watch what the game does while it runs, so keep `any` for
your own network and set `localhost` back when you are done.

The game does not announce itself on the network, so it never appears in the list under the
address field: type the address.

### Once connected

The timeline starts filling at once. Press **Pause** in the viewer to stop scrolling and look at
a frame; disconnecting leaves the game as it was.

- **The port is taken:** two games started on the same computer cannot both listen on 8086. The
  second one's log says `port 8086 is taken`; give it another `port`.
- **A build without the profiler:** a runtime built with the CMake option `WXL_PROFILE` set to
  `OFF` carries no profiler. Its header says `profiler: not in this build`, it ignores
  `[profile]`, and the calls of section 4 do nothing.

## 3. Read the timeline

Each frame of the client ends when it presents its image, and the viewer draws one bar per frame
at the top: its height is how long it took. Click a tall one to see what filled it. Below, each
thread has its own row of zones, nested as the calls were. The ones you will see on the main
thread:

| Zone | What it is |
|---|---|
| `OnWorldUpdate`, `OnUpdate`, `OnUnitDeath`... | the runtime raising an event: everything every mod did with it is inside |
| `scout: OnUpdate` | one mod's handler for that event, named after the mod's id |
| `wxl.lua: OnUpdate` | the Lua mod runtime's handler: the Lua mods' own zones are inside |
| `lua-hello: OnUpdate`, `lua-hello: onUpdate` | one Lua mod's handler, or its callback registered with `registerForEvent` |
| `World.Update`, `World.Render`, `WorldScene.Render` | the client's world, updated and drawn (the x64 client only) |
| `M2.AdvanceTime`, `M2.Animate`, `M2.Draw` | the client's models: animated, then drawn pass by pass (x64) |
| `ObjectMgr.UpdateObject`, `ObjectMgr.EnumVisibleObjects` | the server's object updates applied, and the walks over every visible object (x64) |
| `UI.Update`, `UI.Render`, `FrameScript.SignalEvent` | the interface's frames updated and drawn, and an event fired to its Lua (x64) |

On the stock client (x86), the client's own work has no zones: there, what is not inside a
runtime zone is the client's. On both, each mod's handlers have a colour of their own, the same
from one run to the next.

Two views answer the usual questions directly:

- **Statistics** (the sigma button) lists every zone by total time. Sort by it and look for your
  mod's id: that is what your handlers cost, all events together.
- **Find zone** shows how one zone's duration is spread. A handler that is usually fast but now
  and then takes ten times longer shows as a second hump: that is the stutter.

While a mod runs memory accounting (`wxl.footprint`, see
[Building a mod](building-a-mod.md#profiling-memory)), the viewer also draws three graphs under
the timeline: `storm bytes`, `storm blocks` and `accounting tables`.

## 4. Add your own zones

The zones the runtime opens tell you which handler is slow. To see which part of it is, open
zones inside it. A mod never links Tracy itself: it asks the runtime, through the service
`wxl.profile` (abi `0.1.12`), so that everything lands on the same timeline.

### From C++

Include `wxl/services/profile.hpp`. `WXL_PROFILE_ZONE` makes a zone from that line to the end of
the enclosing block:

```cpp
#include "wxl/services/profile.hpp"

void Journal::Flush()
{
    WXL_PROFILE_ZONE("Journal::Flush");          // until the end of Flush
    for (const Entry& entry : pending_)
    {
        WXL_PROFILE_ZONE_NAMED(zone, "Journal::Write");
        zone.Text(entry.name);                    // shown with the zone in the viewer
        Write(entry);
    }
    wxl::ProfileService::Plot("journal entries", static_cast<double>(pending_.size()));
}
```

The first time a zone's line runs, the runtime records its name, function, file and line once;
after that, opening the zone costs a check while no viewer is connected. Give each zone a name
that says what the code does, not which function it is in: the viewer shows the function and the
line already.

The rest of `wxl::ProfileService`:

| Call | What it does |
|---|---|
| `Connected()` | whether a viewer is connected: skip work done only to be measured |
| `Plot(name, value)` | a point on a graph of that name, drawn under the timeline: a count, a size |
| `Message(text)` | a line of text on the timeline, at this moment: "cache rebuilt" |
| `FrameMark(name)` | ends a frame of a frame set of your own, shown beside the client's frames: one per batch your mod processes, say |

`WXL_PROFILE_ZONE_COLOR(name, 0xRRGGBB)` gives a zone a colour. A zone must end on the thread
that opened it, which the block scope makes sure of.

### From Lua

Lua mods use the `wxl.profile` module. `Begin` opens a zone, `End` closes the innermost one:

```lua
function WorldScript:OnUpdate(delta)
    wxl.profile.Begin("count units")
    local units = 0
    for _ in pairs(tracked) do units = units + 1 end
    wxl.profile.Text(units .. " units")
    wxl.profile.End()

    wxl.profile.Plot("tracked units", units)
end
```

Your handler is already a zone of its own, `<your id>: OnUpdate`; these appear inside it. When a
handler returns, or stops on an error, whatever it left open is closed with it, so a forgotten
`End` cannot spill into the rest of the frame. `Plot`, `Message` and `FrameMark` are the C++
ones. The module came with abi `0.1.12`: a mod that uses it asks for `"abi": "0.1.12"` in its
`mod.json`, and one that asks for less is told so at its first use of `wxl.profile`.

### The GPU's time

On the Vulkan renderer, the runtime puts each frame on a GPU timeline named "Vulkan" beside the
CPU's. A mod that draws into the frame through `wxl.render` (inside `OnRenderPoint`) can time its
own passes there (abi `0.1.33`): `wxl::ProfileService::GpuZoneBegin("bloom")` before them and
`GpuZoneEnd()` after, or `wxl.profile.GpuZoneBegin("bloom")` and `wxl.profile.GpuZoneEnd()` in Lua.
The times come once the GPU has finished the frame, a frame or two later; a zone left open closes
with the handler. On Direct3D 9, or outside a point, the calls do nothing.

## 5. What a zone costs

Measured on the runtime's own test harness, under Wine, with the profiler started and no viewer
connected (the harness's `--bench`):

| | x86 | x64 |
|---|---|---|
| a C++ mod's zone, `WXL_PROFILE_ZONE` | 3 ns | 2.5 ns |
| an event raised to two handlers, with the runtime's zones | 41 ns | 31 ns |
| the same in a runtime built without the profiler | 39 ns | 31 ns |

A Lua `Begin` and `End` add the cost of two calls from Lua to that. With a viewer connected, a
zone costs a few tens of nanoseconds and its data travels over the network: profile with the
zones you need, not one per line.
