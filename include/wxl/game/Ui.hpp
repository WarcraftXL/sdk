// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/CSimpleTop.hpp"

/// Hides or shows the whole interface while the world keeps drawing. Show it again before a reload.
namespace wxl::game::ui
{
    namespace off = wxl::offsets;

    /// The interface's render flag, or null before the interface root exists.
    inline int* Flag()
    {
        void* root = Read<void*>(off::CSimpleTop::s_instance);
        return root ? &At<int>(root, off::CSimpleTop::fields::m_allowProtectedActions) : nullptr;
    }

    /// True when shown, and before the interface root exists.
    inline bool IsShown()      { int* f = Flag(); return f ? (*f != 0) : true; }
    inline void Show(bool on)  { int* f = Flag(); if (f) *f = on ? 1 : 0; }
}
