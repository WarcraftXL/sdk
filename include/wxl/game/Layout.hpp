// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "wxl/game/Binding.hpp"
#include "wxl/game/Script.hpp"
#include "wxl/offsets/CLayoutFrame.hpp"
#include "wxl/offsets/CSimpleTexture.hpp"
#include "wxl/offsets/FrameScript_Object.hpp"

/// The layout scale of frames and regions (CLayoutFrame). Only CSimpleFrame stores a scale of its
/// own; a region's effective scale is overwritten with its parent's.
namespace wxl::game::layout
{
    namespace off = wxl::offsets;

    /// CLayoutFrame's scale setter, and its byte offset in the CLayoutFrame vtable.
    constexpr uintptr_t kSetScale     = off::CLayoutFrame::SetLayoutScale;
    constexpr uintptr_t kSetScaleSlot = 0x14;

    /// `this` is the CLayoutFrame subobject; see LayoutFrameOf.
    using SetScaleFn = off::CLayoutFrame::SetLayoutScaleFn;

    /// CSimpleTexture's scalar deleting destructor.
    constexpr uintptr_t kTextureDestructor = off::CSimpleTexture::dtor_483010;
    using TextureDestructorFn = off::CSimpleTexture::dtor_483010Fn;

    // CScriptRegion layout from its FrameScript_Object base (not in the atlas).
    inline constexpr size_t kLayoutFrameOffset    = 0x20; // the CLayoutFrame base subobject
    inline constexpr size_t kEffectiveScaleOffset = 0x7C; // float, CLayoutFrame + 0x5C
    inline constexpr size_t kParentOffset         = 0x94; // CSimpleFrame*

    /// The CLayoutFrame subobject of a FrameScript_Object base.
    inline void* LayoutFrameOf(void* object)
    { return static_cast<char*>(object) + kLayoutFrameOffset; }

    /// The FrameScript_Object base of a CLayoutFrame subobject.
    inline void* ObjectOf(void* layoutFrame)
    { return static_cast<char*>(layoutFrame) - kLayoutFrameOffset; }

    /// Own scale times every ancestor's, as last computed; the value Frame:GetEffectiveScale returns.
    inline float EffectiveScale(void* object)
    {
        return At<float>(object, kEffectiveScaleOffset);
    }

    /// The parent frame, or null.
    inline void* Parent(void* object)
    {
        return At<void*>(object, kParentOffset);
    }

    /// The parent's effective scale, or 1 without a parent.
    inline float ParentEffectiveScale(void* object)
    {
        void* parent = Parent(object);
        return parent ? EffectiveScale(parent) : 1.0f;
    }

    /// Sets an object's absolute scale and queues its relayout; true when it changed. `entry` is the
    /// trampoline when kSetScale is detoured; a nonzero `force` applies an unchanged value too.
    inline bool SetScaleThrough(SetScaleFn entry, void* object, float scale, int force)
    {
        return entry(LayoutFrameOf(object), nullptr, scale, force) != 0;
    }

    /// CSimpleTexture's script type id, claimed from the shared counter if unset.
    inline int TextureTypeId()
    {
        return script::ClassTypeId(off::CSimpleTexture::s_objectType);
    }

    /// The texture a script method was called on, as its FrameScript_Object base; a script error otherwise.
    inline void* TextureSelf(void* state)
    {
        return script::ObjectThis(state, TextureTypeId());
    }
}
