// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The profile service: what a mod adds to the timeline a profiler viewer draws.
//
// The runtime carries one profiler client, Tracy's (https://github.com/wolfpld/tracy), and
// already marks on it every frame, every event it raises and every mod handler it calls. This
// table lets a mod add its own pieces without linking a profiler of its own: a process holds one
// profiler client, and a second one in a mod's binary would open a second connection that the
// viewer shows apart from the client's frames.
//
// Nothing is recorded while no viewer is connected: zone_begin answers 0 and every other call
// returns at once, so the calls can stay in a mod that ships. A runtime built without the
// profiler does not publish the service; the C++ facade then does nothing.
//
//   location    a place zones are named after: its name, the function and file it stands in, a
//               colour. Copied and kept for the process's life, so make one per place (the
//               facade's WXL_PROFILE_ZONE makes it once, the first time the line runs).
//   zone_begin  opens a zone on the calling thread at a location; zone_end closes it. Zones
//   zone_end    nest: close them in the reverse order, on the thread that opened them.
//   zone_text   adds a line of text to an open zone: the item, the unit, the file it worked on.
//   plot        a value on a named graph, drawn under the timeline: a count, a size, a rate.
//   message     a line of text on the timeline, at this moment.
//   frame_mark  marks the end of a frame of a named frame set of the mod's own, drawn beside the
//               client's frames, which the runtime marks itself at each present.
//   gpu_zone_begin, gpu_zone_end  (abi 0.1.33) a zone of the GPU's time, around the commands a
//               mod records inside RenderPoint on the Vulkan renderer (wxl.render, wxl.vk): drawn
//               on the "Vulkan" GPU timeline beside the frame's own zones, once the GPU has
//               finished the frame. Nest them, inside one handler: one left open is closed when
//               the handler returns. Outside a point, or on another device, they do nothing.

#ifndef WXL_SERVICE_PROFILE_H
#define WXL_SERVICE_PROFILE_H

#include "wxl/abi.h"

#define WXL_PROFILE_SERVICE         "wxl.profile"
#define WXL_PROFILE_SERVICE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wxl_profile_api
{
    uint32_t struct_size;

    /// Whether a viewer is connected: what a mod checks before work done only for the profile.
    wxl_bool(WXL_CALL* connected)(void);

    /**
     * @brief A place zones are named after, kept for the process's life.
     *
     * The strings are copied. The same five values answer the same location, so a mod that
     * reloads finds its own again. @p color is 0xRRGGBB, 0 for the viewer's own. 0 for an empty
     * name.
     */
    uintptr_t(WXL_CALL* location)(wxl_str name, wxl_str function, wxl_str file, uint32_t line,
                                  uint32_t color);

    /// Opens a zone at a location on the calling thread. 0 while no viewer is connected, and
    /// for a location of 0; closing 0 does nothing.
    uintptr_t(WXL_CALL* zone_begin)(uintptr_t location);

    /// Closes what zone_begin answered, on the thread that opened it, innermost first.
    void(WXL_CALL* zone_end)(uintptr_t zone);

    /// Adds a line of text to an open zone. Copied.
    void(WXL_CALL* zone_text)(uintptr_t zone, wxl_str text);

    /// Puts a value on a graph, created by its first value. The name is kept for the process's life.
    void(WXL_CALL* plot)(wxl_str name, double value);

    /// A line of text on the timeline, now. Copied.
    void(WXL_CALL* message)(wxl_str text);

    /// Ends a frame of a frame set named after the mod's own work; an empty name is refused, as
    /// the client's frames are the runtime's to mark.
    void(WXL_CALL* frame_mark)(wxl_str name);

    // --- abi 0.1.33 ----------------------------------------------------------------------------

    /// Opens a GPU zone named @p name (copied) in the frame's commands, inside RenderPoint.
    /// WXL_E_UNSUPPORTED on a device without GPU timestamps for mods (Direct3D 9), WXL_E_INVALID
    /// outside a point; WXL_OK and nothing recorded while no viewer is connected.
    wxl_result(WXL_CALL* gpu_zone_begin)(wxl_str name);
    /// Closes the innermost GPU zone the mod opened.
    void(WXL_CALL* gpu_zone_end)(void);
} wxl_profile_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_PROFILE_H
