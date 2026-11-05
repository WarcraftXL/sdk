// SPDX-License-Identifier: GPL-3.0-or-later
// The args of the asset events: WMO buffers and textures.

#pragma once

#include <cstdint>

namespace wxl::events
{
    /// OnWmoRootLoad: reshape the root buffer in place through wxl::game::wmo.
    struct WmoRootLoadArgs { void* root; };

    /// OnWmoGroupLoad: reshape the group buffer in place through wxl::game::wmo.
    struct WmoGroupLoadArgs { void* group; };

    /// OnTextureUpload: a texture about to upload.
    struct TextureUploadArgs { void* texture; uint32_t width; uint32_t height; };

    /// OnBlpLoad, read-only: `name` is the virtual path asked for (compare case-insensitively),
    /// `handle` null on failure.
    struct BlpLoadArgs { const char* name; void* handle; };
}
