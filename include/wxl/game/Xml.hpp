// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "wxl/game/Binding.hpp"
#include "wxl/game/Script.hpp"
#include "wxl/offsets/CScriptRegion.hpp"
#include "wxl/offsets/CSimpleAlphaAnim.hpp"
#include "wxl/offsets/CSimpleAnim.hpp"
#include "wxl/offsets/CSimpleAnimGroup.hpp"
#include "wxl/offsets/CSimpleFrame.hpp"
#include "wxl/offsets/CSimplePathAnim.hpp"
#include "wxl/offsets/CSimpleRotationAnim.hpp"
#include "wxl/offsets/CSimpleScaleAnim.hpp"
#include "wxl/offsets/CSimpleTexture.hpp"
#include "wxl/offsets/CSimpleTranslationAnim.hpp"
#include "wxl/offsets/FrameScript_Object.hpp"
#include "wxl/offsets/XMLNode.hpp"
#include "wxl/offsets/free/S.hpp"
#include "wxl/offsets/free/Script.hpp"

/// XML load entries and node access; the parser keeps unknown attributes, read by a detour after a LoadXML.
/// Regions load on a subobject (ObjectOf), animations and groups on the FrameScript_Object base.
namespace wxl::game::xml
{
    namespace off = wxl::offsets;

    // Layouts not in the atlas: CSimpleAnim, CSimpleAnimGroup, XMLNode, the script slot pair.
    inline constexpr size_t kAnimGroupField           = 0x28; // CSimpleAnim -> its CSimpleAnimGroup
    inline constexpr size_t kAnimGroupTargetField     = 0x30;
    inline constexpr size_t kAnimGroupOrderCountField = 0x44;
    inline constexpr size_t kAnimGroupPlayStateField  = 0x8D; // u8
    inline constexpr size_t kAnimGroupOrderCursorField = 0x90;
    inline constexpr size_t kNodeFirstChild  = 0x08;
    inline constexpr size_t kNodeName        = 0x14;
    inline constexpr size_t kNodeText        = 0x18;
    inline constexpr size_t kNodeNextSibling = 0x34;
    inline constexpr ptrdiff_t kRegionToScriptObject = -0x20;
    inline constexpr float kLayoutReferenceWidth = 1024.0f;
    inline constexpr size_t kLayoutFrameSetWidthSlot  = 0x1C; // byte offsets in the CLayoutFrame vtable
    inline constexpr size_t kLayoutFrameSetHeightSlot = 0x20;
    inline constexpr size_t kGetScriptByNameSlot      = 0x0C;
    inline constexpr int kScriptSlotRef = 0, kScriptSlotTaint = 1;
    inline constexpr const char* kDefaultScriptSignature = "return function(self) %s end";
    using LayoutFrameSetExtentFn = void(__fastcall*)(void* region, void* edx, float value);
    using GetScriptByNameFn = int*(__fastcall*)(void* object, void* edx, const char* name, const char** signatureOut);

    /// A parsed XML node.
    using Node = void*;

    /// The load entry of every region and frame type.
    using RegionLoadXMLFn = off::CScriptRegion::LoadXMLFn;

    constexpr uintptr_t kRegionLoadXML = off::CScriptRegion::LoadXML;

    /// The <Scripts> load entries, one per family of script-bearing object.
    using ScriptsLoadXMLFn = off::CSimpleAnimGroup::LoadXML_ScriptsFn;
    constexpr uintptr_t kFrameLoadXMLScripts     = off::CSimpleFrame::LoadXML_Scripts;
    constexpr uintptr_t kAnimLoadXMLScripts      = off::CSimpleAnim::LoadXML_Scripts;
    constexpr uintptr_t kAnimGroupLoadXMLScripts = off::CSimpleAnimGroup::LoadXML_Scripts;

    /// The <AnimationGroup> and <Animation> load entries; neither reaches kRegionLoadXML.
    /// CSimpleAnim::LoadXML covers every <Animation> subclass.
    using AnimLoadXMLFn = off::CSimpleAnim::LoadXMLFn;
    constexpr uintptr_t kAnimGroupLoadXML = off::CSimpleAnimGroup::LoadXML;
    constexpr uintptr_t kAnimLoadXML      = off::CSimpleAnim::LoadXML;

    /// The apply entries (vtable slot 0x2C) of each animation type. Each acts on anim->group->target.
    using AnimApplyFn = off::CSimplePathAnim::ApplyFn;
    constexpr uintptr_t kTranslationAnimApply = off::CSimpleTranslationAnim::Apply;
    constexpr uintptr_t kRotationAnimApply    = off::CSimpleRotationAnim::Apply;
    constexpr uintptr_t kScaleAnimApply       = off::CSimpleScaleAnim::Apply;
    constexpr uintptr_t kAlphaAnimApply       = off::CSimpleAlphaAnim::Apply;
    constexpr uintptr_t kPathAnimApply        = off::CSimplePathAnim::Apply;

    /// The base destructor every animation type ends in.
    using AnimDestructorFn = off::CSimpleAnim::dtorFn;
    constexpr uintptr_t kAnimDestructor = off::CSimpleAnim::dtor;

    /// Address of anim->group->target, or null when the animation has no group. The target is a
    /// FrameScript_Object base; writing it retargets the group's animations.
    inline void** AnimTargetSlot(void* anim)
    {
        if (!anim) return nullptr;

        void* group = At<void*>(anim, kAnimGroupField);
        if (!group) return nullptr;

        return reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(group) + kAnimGroupTargetField);
    }

    /// The AnimationGroup:Play binding. It reads no argument, so `group:Play(true)` ignores the flag.
    using AnimGroupPlayBindingFn = off::Script_Play_4A6EA0Fn;
    constexpr uintptr_t kAnimGroupPlayBinding = off::Script_Play_4A6EA0;

    /// The animation group a script method was called on (claiming the type id if unset); a script
    /// error for any other object.
    inline void* AnimGroupSelf(void* state)
    {
        return script::ObjectThis(state, script::ClassTypeId(off::CSimpleAnimGroup::s_objectType));
    }

    /// True while playing or paused.
    inline bool AnimGroupIsPlaying(void* group)
    {
        if (!group) return false;
        return At<uint8_t>(group, kAnimGroupPlayStateField) != 0;
    }

    /// The <Texture> load entry.
    using TextureLoadXMLFn = off::CSimpleTexture::LoadXMLFn;
    constexpr uintptr_t kTextureLoadXML = off::CSimpleTexture::LoadXML;

    /// An attribute's value (name case-insensitive), or null. Valid until the next UI reload.
    inline const char* Attribute(Node node, const char* name)
    {
        if (!node || !name) return nullptr;
        return Native<off::XMLNode::GetAttributeByNameFn>(off::XMLNode::GetAttributeByName)(node, nullptr, name);
    }

    /// Parses attribute text as a boolean, as the stock attributes are parsed.
    inline bool TextToBool(const char* value)
    {
        if (!value || !*value) return false;
        return Native<off::StringToBOOL_8154E0Fn>(off::StringToBOOL_8154E0)(value) != 0;
    }

    /// Parses attribute text as a number, as the stock attributes are parsed.
    inline float TextToNumber(const char* value)
    {
        if (!value || !*value) return 0.0f;
        return Native<off::SStrToFloatFn>(off::SStrToFloat)(value);
    }

    /// False when the attribute is absent or empty.
    inline bool AttributeIsTrue(Node node, const char* name)
    { return TextToBool(Attribute(node, name)); }

    inline Node FirstChild(Node node)
    {
        if (!node) return nullptr;
        return At<Node>(node, kNodeFirstChild);
    }

    inline Node NextSibling(Node node)
    {
        if (!node) return nullptr;
        return At<Node>(node, kNodeNextSibling);
    }

    /// The element's text, or null when empty; for a script handler, its inline body.
    inline const char* NodeText(Node node)
    {
        if (!node) return nullptr;
        return At<const char*>(node, kNodeText);
    }

    /// The element name.
    inline const char* NodeName(Node node)
    {
        if (!node) return nullptr;
        return At<const char*>(node, kNodeName);
    }

    /// The FrameScript_Object base of the region subobject a region LoadXML receives as `this`.
    inline void* ObjectOf(void* region)
    {
        if (!region) return nullptr;
        return reinterpret_cast<uint8_t*>(region) + kRegionToScriptObject;
    }

    /// Pushes the Lua table of a FrameScript_Object base, registering it if needed (it then lives for
    /// the process); false, stack untouched, on failure.
    inline bool PushScriptObject(void* state, void* object)
    {
        if (!state || !object) return false;

        const int ref = Native<off::FrameScript_Object::GetLuaObjectRefFn>(off::FrameScript_Object::GetLuaObjectRef)(object, nullptr);
        if (ref == 0) return false;

        script::RawGetI(state, script::kRegistryIndex, ref);
        if (script::Type(state, -1) != script::kTypeTable)
        {
            script::SetTop(state, -2);
            return false;
        }
        return true;
    }

    /// PushScriptObject for the `this` a region LoadXML receives.
    inline bool PushObject(void* state, void* region)
    {
        return PushScriptObject(state, ObjectOf(region));
    }

    /// Sets a <Texture>'s file (backslashes, no extension); false, texture unchanged, when it does not load.
    inline bool SetTexture(void* region, const char* file, bool horizTile, bool vertTile)
    {
        void* object = ObjectOf(region);
        if (!object || !file || !*file) return false;

        const int filter = Read<int>(off::CSimpleTexture::s_textureFilterMode);
        return Native<off::CSimpleTexture::SetTexture_4859E0Fn>(off::CSimpleTexture::SetTexture_4859E0)(
                   object, nullptr, file, horizTile ? 1 : 0, vertTile ? 1 : 0, filter, 0) != 0;
    }

    /// Sets a <Texture>'s coordinates to an axis-aligned rectangle.
    inline void SetTexCoord(void* region, float left, float right, float top, float bottom)
    {
        void* object = ObjectOf(region);
        if (!object) return;

        const float corners[8] = { left, top, left, bottom, right, top, right, bottom };
        Native<off::CSimpleTexture::SetTexCoordFn>(off::CSimpleTexture::SetTexCoord)(object, nullptr, corners);
    }

    /// Converts pixels at the reference width to layout units, as <AbsDimension> does.
    inline float PixelsToLayout(float pixels)
    {
        const float aspect = Read<float>(off::s_aspectCompensation);
        const float scale  = Read<float>(off::s_ddcWidthScale);
        if (aspect == 0.0f) return 0.0f;
        return scale * (pixels / (aspect * kLayoutReferenceWidth));
    }

    /// Sets a region's width in layout units, through its virtual setter.
    inline void SetWidth(void* region, float width)
    {
        if (!region) return;

        auto fn = Virtual<LayoutFrameSetExtentFn>(region, kLayoutFrameSetWidthSlot / sizeof(void*));
        fn(region, nullptr, width);
    }

    /// Sets a region's height in layout units, through its virtual setter.
    inline void SetHeight(void* region, float height)
    {
        if (!region) return;

        auto fn = Virtual<LayoutFrameSetExtentFn>(region, kLayoutFrameSetHeightSlot / sizeof(void*));
        fn(region, nullptr, height);
    }

    /// An object's slot for a named script (name case-insensitive): {registry key, taint source},
    /// or null when the class has no such script.
    inline int* ScriptSlot(void* object, const char* name)
    {
        if (!object || !name || !*name) return nullptr;

        const char* signature = kDefaultScriptSignature;
        auto fn = Virtual<GetScriptByNameFn>(object, kGetScriptByNameSlot / sizeof(void*));
        return fn(object, nullptr, name, &signature);
    }

    /// The handler's registry key, or 0 when the slot is empty.
    inline int SlotHandler(const int* slot)
    {
        if (!slot) return 0;
        const int ref = slot[kScriptSlotRef];
        return ref > 0 ? ref : 0;
    }

    /// Installs a handler (taking ownership of `ref`), releasing the previous one and clearing the taint.
    inline void SetSlotHandler(void* state, int* slot, int ref)
    {
        if (!slot || ref <= 0) return;

        const int previous = slot[kScriptSlotRef];
        if (previous > 0) script::Unref(state, script::kRegistryIndex, previous);

        slot[kScriptSlotRef]   = ref;
        slot[kScriptSlotTaint] = 0;
    }
}
