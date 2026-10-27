// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// What the service rows cannot marshal by kind: the rows marked `shim`. Two take a callback: a
// Lua mod hands over a Lua function; the shim keeps it, registers a native function that runs
// it, and does again on every new script context what the C service says a mod must redo there.
//
//   wxl.script.RegisterFunction(name, fn [, context])   a global of the client's script that
//                                                       calls fn, in every context or one
//   wxl.script.RegisterEvent(name [, context])          an event its frames can listen to
//
// Both are kept for the mod's life and replayed at each ScriptVmReady; they end when the mod's
// state closes (a reload, a disable, the runtime stopping). A global the client already holds
// keeps pointing at the shim, which answers with an error until the mod registers it again.
//
// Two keep what a mod added, which the host can only see as wxl.lua's:
//
//   wxl.storage.AddSearchPath(directory, priority)      a folder searched for the client's
//   wxl.storage.RemoveSearchPath(directory)             files, until the mod removes it
//
// A mod's folders leave the search when it is disabled or its state closes; one that two mods
// added stays until both have let it go.
//
// Three keep what a mod states about memory, also seen by the host as wxl.lua's:
//
//   wxl.footprint.Snapshot()                            reports the heap of every Lua mod as
//                                                       "<id>/heap", then takes the picture
//   wxl.footprint.Report(name, bytes, count)            a consumer of the mod's own, reported
//                                                       as "<id>/<name>"
//   wxl.footprint.WatchPressure(bytes)                  the mod's threshold; the highest of all
//                                                       Lua mods' is wxl.lua's
//
// They are taken back when the mod is disabled or its state closes.
//
// The rest of wxl.storage's shims keep what a Lua mod holds or hand it a table:
//
//   wxl.storage.OverrideFile(name, other)               the mod's overrides, kept as its
//   wxl.storage.RestoreFile(name)                       redirects are
//   wxl.storage.HashFile(name [, fresh])                a table: md5 (hex), size, layer, source
//   wxl.storage.HashFileAsync(name, fn [, fresh])       fn called at a later update, unless the
//                                                       mod went first
//   wxl.storage.ReloadAsset(name)                       a table: textures, models, objects, busy
//
// And wxl.relay's keep each Lua mod's services and requests:
//
//   wxl.relay.Publish(name, fn [, cancelled])           fn(request, method, ...): what it returns
//                                                       answers; nothing, an answer to come
//   wxl.relay.Unpublish(name)
//   wxl.relay.Call(name, method, args, fn)              fn(true, ...) or fn(false, reason) later
//   wxl.relay.Answer(request, ...), .Fail(request, reason), .Cancel(request)
//
// A mod that goes takes its overrides and services back and cancels its requests; an answer that
// comes for it afterwards finds nobody.

// Three keep the camera and the pointer a mod holds, also seen by the host as wxl.lua's:
//
//   wxl.camera.SetView(position, forward, fov)          the view; the last Lua mod to set one
//   wxl.camera.SetViewRolled(position, forward, fov,    has it applied, the one set before
//                            roll)                      comes back when it lets go, and the
//   wxl.camera.ReleaseView()                            runtime's view goes with the last
//   wxl.input.CaptureCursor(captured)                   the pointer, held while one Lua mod
//   wxl.input.CaptureCursorAt(x, y)                     holds it, at the point of the last to
//                                                       capture or move it
//   wxl.input.BlockMouseLook(blocked)                   the game's mouse look, kept out while
//                                                       one Lua mod blocks it
//   wxl.pose.Head(unit, yaw, pitch, smoothing)          a unit's head, the last Lua mod to turn
//   wxl.pose.ReleaseHead(unit)                          it holding it
//   wxl.highlight.Set(target, color, outline, glow,    an object highlighted, the last Lua mod
//                     glow_strength)                    to set it holding it; Clear, ClearAll take
//   wxl.highlight.Clear(target), .ClearAll()            the mod's own off
//   wxl.selectioncircle.Hide(kinds, reactions),         the game's circles hidden or restyled,
//                      .SetStyle(kinds, reactions,      the last Lua mod to change a kind and
//                                style), .Reset(...)    reaction, or an object, holding it; Reset,
//   wxl.selectioncircle.HideObject(target),             ResetObject and ResetAll give back only
//                      .SetObjectStyle(target, style),  what the mod holds
//                      .ResetObject(target), .ResetAll()
//   wxl.cvar.Register(name, default, flags, help)      a console variable, held by the Lua mod that
//   wxl.cvar.Release(name)                              registered it; another Lua mod's Register of
//                                                       it is refused, and it is released when its
//                                                       mod goes (the variable stays)
//   wxl.terrain.SetHeights(heights)                    the ground's edits, each Lua mod's in a
//   wxl.terrain.Reset(chunk_x, chunk_y), .ResetAll()    layer of wxl.lua's own, which its reset and
//                                                       its going take off alone
//   wxl.terrain.Trace, .Locate, .Vertices,              answer a table
//              .VerticesIn, .Stats
//
// wxl.texture's take a table and answer several values; nothing is kept for the mod:
//
//   wxl.texture.Make(source, width, height             path, right, bottom, texture width and
//                    [, groups [, tint]])              height, or nil and why; ".\\" is the
//                                                      mod's own folder
//   wxl.texture.Stats()                                a table of what the service holds
//
// They are let go when the mod is disabled or its state closes.
//
// wxl.render's take tables, files of the mod's folder and push constants as numbers:
//
//   wxl.render.CreatePipeline(desc)                    a Pipeline from a table: kind, name,
//                                                      vertex and fragment (SPIR-V bytes, or a
//                                                      .spv file of the mod's folder), colors,
//                                                      depth, samples, blend, depth_test,
//                                                      depth_write, cull, topology, by name
//   wxl.render.Begin(pass)                             a pass from a table: colors (Images,
//                                                      "scene_color", {image, clear = {r,g,b,a}}),
//                                                      depth (an Image, "scene_depth", {image,
//                                                      clear = z})
//   wxl.render.Draw, .DrawIndexed, .Fullscreen,        push constants (a batch's constants) as a
//              .DrawBatch                              table of numbers, each a 32-bit float, or a
//                                                      string of bytes
//   wxl.render.ReplaceShader(name, stage, spirv)       a client shader's program replaced, held by
//   wxl.render.RestoreShader(name, stage)              the last Lua mod to replace it and given
//                                                      back when it goes

#ifndef WXL_LUA_SHIMS_HPP
#define WXL_LUA_SHIMS_HPP

#include <string_view>

#include "Lua.hpp"

namespace wxl::lua
{
    class ScriptMod;

    namespace shims
    {
        /// The function serving a shim row (`service`, `Name`), or null when none is written.
        /// It takes the ScriptMod as its first upvalue.
        lua_CFunction Find(std::string_view service, std::string_view name);

        /// Starts replaying registrations at each new script context. Called once, by the runtime.
        void Start();

        /// Stops: nothing is replayed any more.
        void Stop();

        /// Everything a mod registered ends with its state: called before the state closes.
        void Forget(ScriptMod& mod);

        /// What a mod holds goes: the folders it added leave the search path, its footprint
        /// reports and its pressure threshold are taken back, its camera view and its hold on the
        /// pointer are let go. It is disabled, or its state closes.
        void ReleaseHeld(ScriptMod& mod);

        /// Whether a registered function is running, inside the client's script: no mod's state
        /// may be closed then.
        bool Calling();
    } // namespace shims
} // namespace wxl::lua

#endif // WXL_LUA_SHIMS_HPP
