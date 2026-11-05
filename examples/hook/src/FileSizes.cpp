// SPDX-License-Identifier: GPL-3.0-or-later
// The largest file the client sized while in a map: a raw Hook on a hook point (low_level).

#include <wxl/offsets/free/S.hpp>
#include <wxl/wxl.hpp>

using namespace wxl;

namespace
{
    Hook<offsets::SFileGetFileSizeFn> g_fileSize;
    uint32_t                          g_largest = 0;

    // The detour: same signature and convention as the client function. Unguarded: keep it simple.
    uint32_t __stdcall FileSize(void* file, uint32_t* high)
    {
        const uint32_t size = g_fileSize(file, high); // the next link of the chain, then the client
        if (size > g_largest) g_largest = size;
        return size;
    }
}

class FileSizes : public Extension
{
    void OnStart() override
    {
        if (!g_fileSize.Attach("SFileGetFileSize", &FileSize))
        {
            Log::Warn("SFileGetFileSize could not be hooked");
            return;
        }
        Log::Info("hooked SFileGetFileSize");
    }

    void OnWorldLeave(MapId map) override
    {
        Log::Info("largest file in map {}: {} bytes", map, g_largest);
        g_largest = 0;
    }
};

Export<FileSizes> extension;
