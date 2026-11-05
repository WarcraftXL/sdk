// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "wxl/game/Binding.hpp"
#include "wxl/offsets/SE2.hpp"
#include "wxl/offsets/TSGrowableArray.hpp"
#include "wxl/offsets/free/S.hpp"

/// The client's master volume.
namespace wxl::game::sound
{
    namespace off = wxl::offsets;

    /// The master volume's offset in a channel group.
    constexpr size_t kChannelGroupVolume = 0x08;

    /// True once the sound system is initialized.
    inline bool Available()
    {
        return Read<int>(off::s_initialized_D43814) != 0;
    }

    /// Sets the master volume, 0..1. Ignored while the sound system is down.
    inline void SetMasterVolume(float volume)
    {
        Native<off::SE2::SetMasterVolumeFn>(off::SE2::SetMasterVolume)(volume);
    }

    /// The master volume, 0..1, or 1 while the sound system is down.
    inline float MasterVolume()
    {
        if (!Available()) return 1.0f;
        void* group0 = Read<void*>(off::s_channelGroups + off::TSGrowableArray_uint8_t::fields::m_data);
        if (!group0) return 1.0f;
        return At<float>(group0, kChannelGroupVolume);
    }
}
