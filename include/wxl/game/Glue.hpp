// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "wxl/game/Binding.hpp"
#include "wxl/game/Script.hpp"
#include "wxl/offsets/CSimpleModel.hpp"
#include "wxl/offsets/CSimpleModelFFX.hpp"
#include "wxl/offsets/FrameScript_Object.hpp"

/// The model frame of the login and character-select screens: its 3D render callback and its
/// script methods.
namespace wxl::game::glue
{
    namespace off = wxl::offsets;

    /// The glue screens' 3D render callback.
    constexpr uintptr_t kModelRender = off::CSimpleModelFFX::Render;

    using ModelRenderFn = off::CSimpleModelFFX::RenderFn;
    /// Registers the frame type's script methods.
    constexpr uintptr_t kRegisterMethods = off::CSimpleModel::RegisterScriptMethods;

    using RegisterMethodsFn = off::CSimpleModel::RegisterScriptMethodsFn;
    /// A script method's Lua name and function.
    struct Method { const char* name; int(__cdecl* function)(void* state); };

    using MethodFn = int(__cdecl*)(void* state);

    /// Adds script methods (living for the process) to the frame type: from a kRegisterMethods
    /// detour, after the original.
    inline void AddMethods(void* target, const Method* methods, int count)
    { Native<off::FrameScript_Object::FillScriptMethodTableFn>(off::FrameScript_Object::FillScriptMethodTable)(target, const_cast<Method*>(methods), count); }

    /// The frame a script method was called on, claiming the class's type id if unset; the same
    /// pointer kModelRender receives.
    inline void* MethodSelf(void* state)
    {
        return script::ObjectThis(state, script::ClassTypeId(off::CSimpleModel::s_objectType));
    }
}
