// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The highlight service: a world object outlined and lit by a soft glow, drawn by the runtime.
//
//   supported           whether this runtime draws highlights on the device it runs now
//   unsupported_reason  why not, in a sentence; empty when it does
//   set                 outlines an object in a colour, with an outline width and a glow, until
//                       cleared; called again, it changes the style
//   clear               takes a mod's highlight off an object
//   clear_all           takes off every highlight of the calling mod
//   drawn               how many of the objects' batches the last highlighted frame drew again
//
// What is drawn: the object's model and everything hanging on the same top model (its weapons,
// its rider or its mount), where they are seen and nowhere else: a highlight is never seen
// through a wall, nor through anything else in front of it. Around what is seen, a sharp outline
// `outline` pixels wide (0 for none), and a glow that fades over `glow` pixels (0 for none),
// `glow_strength` (0 to 1) bright, both in `color`. Over the object itself nothing is drawn.
// Translucent parts (a spell's glow on a weapon, a ghost) are not outlined; parts the game cuts
// out by their texture's alpha (hair, a mane, leaves, a cloak's edge) are outlined as the game
// shows them. Widths are pixels of a picture 1080 lines high, the outline up to 16 and the glow up
// to 64, and drawn in proportion at any other height, so that an outline looks the same at any
// resolution; a mod that wants a far object's outline thinner scales them itself.
//
// Colour: 0xRRGGBB; the top byte is reserved: pass 0.
//
// Each highlight belongs to the mod that set it: an object has at most one highlight per mod, a
// mod's highlights go when it is disabled or unloaded, and an object's when it leaves view. Up to
// 32 objects are highlighted at once, all mods together. Where two mods highlight the same object,
// the last set is drawn.
//
// The runtime draws it after the world and before the interface, every frame something is
// highlighted; nothing is drawn, and nothing costs, while nothing is. Where the device or the
// runtime cannot draw it (the recompiled client until its Vulkan renderer's render points exist,
// a device without the shader model the passes need), `supported` answers false and every `set`
// WXL_E_UNSUPPORTED, and `unsupported_reason` says why: a mod disables its feature with that line
// rather than failing silently.
//
// Main thread only: a call off it answers WXL_E_INVALID.

#ifndef WXL_SERVICE_HIGHLIGHT_H
#define WXL_SERVICE_HIGHLIGHT_H

#include "wxl/abi.h"

#define WXL_HIGHLIGHT_SERVICE         "wxl.highlight"
#define WXL_HIGHLIGHT_SERVICE_VERSION 1

/// The widest outline and glow the runtime draws, in pixels: wider ones are drawn this wide.
#define WXL_HIGHLIGHT_MAX_OUTLINE 16.0f
#define WXL_HIGHLIGHT_MAX_GLOW    64.0f
/// How many objects can be highlighted at once, all mods together.
#define WXL_HIGHLIGHT_MAX_OBJECTS 32

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wxl_highlight_api
{
    uint32_t struct_size;

    /// Whether this runtime draws highlights on the device it runs now. It can change once: false
    /// before the device is made, then what the device allows.
    wxl_bool(WXL_CALL* supported)(void);

    /// Why highlights are not drawn, in a sentence for a log; empty when they are.
    wxl_str(WXL_CALL* unsupported_reason)(void);

    /**
     * @brief Highlights an object until cleared, or changes the calling mod's highlight of it.
     * @param color          0xRRGGBB
     * @param outline        the sharp outline's width in pixels at 1080 lines, 0 to 16; 0 for none
     * @param glow           how far the glow reaches in pixels at 1080 lines, 0 to 64; 0 for none
     * @param glow_strength  how bright the glow is at the object's edge, 0 to 1
     *
     * WXL_E_UNSUPPORTED where highlights are not drawn (see unsupported_reason), WXL_E_NOT_FOUND
     * for an object gone, WXL_E_INVALID for a value that is no number or below 0, or when 32
     * objects are highlighted already. Widths above the maximum are drawn at the maximum.
     */
    wxl_result(WXL_CALL* set)(wxl_handle target, uint32_t color, float outline, float glow, float glow_strength);

    /// Takes the calling mod's highlight off an object. WXL_E_NOT_FOUND when it has none there.
    wxl_result(WXL_CALL* clear)(wxl_handle target);

    /// Takes off every highlight of the calling mod.
    void(WXL_CALL* clear_all)(void);

    /// How many batches of highlighted objects the last frame with highlights drew again: 0 when
    /// none was in view, or nothing was highlighted.
    uint32_t(WXL_CALL* drawn)(void);
} wxl_highlight_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_HIGHLIGHT_H
