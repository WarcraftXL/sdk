// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/free/S.hpp"

/// The client's heap. Blocks are interchangeable with the client's own allocations.
namespace wxl::game::mem
{
    namespace off = wxl::offsets;

    /// Allocates from the client heap; null on failure; `file` and `line` tag the block.
    inline void* Alloc(uint32_t size, const char* file = "wxl", int line = 0, uint32_t flags = 0)
    {
        return Native<off::SMemAllocFn>(off::SMemAlloc)(size, file, line, flags);
    }

    /// Frees a client heap block.
    inline void Free(void* ptr, const char* file = "wxl", int line = 0, uint32_t flags = 0)
    {
        Native<off::SMemFreeFn>(off::SMemFree)(ptr, file, line, flags);
    }
}
