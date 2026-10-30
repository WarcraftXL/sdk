// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The ImGui service: the runtime's Dear ImGui and ImPlot, drawn in the game's frame, for the
// windows mods add.
//
// The runtime owns one Dear ImGui context and one ImPlot context, draws them over the game's
// frame (after its interface, before the frame is shown), with the game's own Direct3D 9 device,
// and hands them the game window's input. A mod adds its windows from the ImGuiFrame event, which
// comes once a frame between the frame's start and its drawing while some mod listens; with
// nobody listening, nothing is set up and nothing is drawn.
//
// A mod compiles Dear ImGui and ImPlot from the SDK (wxl::imgui) into its own binary, and points
// its copy at the runtime's contexts and allocator: the C++ facade's Bind() does all of it, and
// refuses when the two copies are not the same version. Mouse and keyboard input go to ImGui when
// it wants them -- a window under the pointer, a text field being typed in -- and the game does
// not see them then. ImGui does not change the pointer's shape. The device going away and coming
// back (a resize, Alt+Tab in full screen) is handled by the runtime.
//
//   version       the version both copies must have: "<Dear ImGui>;<ImPlot>", "1.91.9;0.16"
//   context       the ImGuiContext*
//   plot_context  the ImPlotContext*
//   allocators    the allocator functions the contexts were made with, to hand to
//                 ImGui::SetAllocatorFunctions
//
// No script reaches it: its members are a native mod's.

#ifndef WXL_SERVICE_IMGUI_H
#define WXL_SERVICE_IMGUI_H

#include "wxl/abi.h"

#define WXL_IMGUI_SERVICE         "wxl.imgui"
#define WXL_IMGUI_SERVICE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wxl_imgui_api
{
    uint32_t struct_size;

    /// "<Dear ImGui version>;<ImPlot version>" of the runtime's copy.
    wxl_str(WXL_CALL* version)(void);

    /// The runtime's ImGuiContext*.
    void*(WXL_CALL* context)(void);

    /// The runtime's ImPlotContext*.
    void*(WXL_CALL* plot_context)(void);

    /// The functions the runtime's contexts allocate and free with, as
    /// ImGui::GetAllocatorFunctions gives them: an ImGuiMemAllocFunc, an ImGuiMemFreeFunc, and
    /// the user data both take.
    void(WXL_CALL* allocators)(void** alloc, void** release, void** user);
} wxl_imgui_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_IMGUI_H
