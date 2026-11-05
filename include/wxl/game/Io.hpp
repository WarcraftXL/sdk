// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/SFile.hpp"
#include "wxl/offsets/free/S.hpp"

/// File access through the client's archives.
namespace wxl::game::io
{
    namespace off = wxl::offsets;

    /// Open flag: load the whole file into the handle's buffer.
    constexpr uint32_t kOpenWholeFile = 0x20000;

    /// Opens a file; nonzero on success.
    inline int FileOpen(const char* name, uint32_t flags, void** outHandle)
    {
        return Native<off::SFile::OpenExFn>(off::SFile::OpenEx)(nullptr, name, flags, outHandle);
    }

    /// The low dword of the size; the high one goes to `sizeHigh`.
    inline uint32_t FileSize(void* handle, uint32_t* sizeHigh)
    {
        return Native<off::SFileGetFileSizeFn>(off::SFileGetFileSize)(handle, sizeHigh);
    }

    /// Reads up to `len` bytes, the count to `read`; nonzero on success.
    inline int FileRead(void* handle, void* dst, uint32_t len, uint32_t* read)
    {
        return Native<off::SFileReadFileFn>(off::SFileReadFile)(handle, dst, len, read, nullptr, 0);
    }

    /// Seeks from `method` (0 begin, 1 current, 2 end); the low dword of the new position.
    inline uint32_t FileSeek(void* handle, int32_t distLow, uint32_t* distHigh, uint32_t method)
    {
        return Native<off::SFile::SetFilePointerFn>(off::SFile::SetFilePointer)(handle, distLow, distHigh, method);
    }

    /// Nonzero on success.
    inline int FileClose(void* handle)
    {
        return Native<off::SFileCloseFileFn>(off::SFileCloseFile)(handle);
    }
}
