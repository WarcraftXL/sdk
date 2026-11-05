// SPDX-License-Identifier: GPL-3.0-or-later
// Caps the master volume and counts interface sounds: two interceptors on client functions.

#include <wxl/offsets/SE2.hpp>
#include <wxl/offsets/free/Script.hpp>
#include <wxl/wxl.hpp>

using namespace wxl;

namespace
{
    namespace off = offsets;

    constexpr float kMaxVolume = 0.5f;
}

class Quiet : public Extension
{
    Interception cap, count;
    uint32_t     sounds = 0;

    void OnStart() override
    {
        cap = Intercept<off::SE2::SetMasterVolumeFn, off::SE2::SetMasterVolume>().Before([](auto& call)
        {
            float& volume = call.template Arg<0>();
            if (volume > kMaxVolume) volume = kMaxVolume;
        });
        count = Intercept<off::Script_PlaySoundFn, off::Script_PlaySound>().After([this](auto&)
        {
            ++sounds;
        });
        if (!cap || !count) Log::Warn("the sound functions could not be intercepted");
    }

    void OnWorldLeave(MapId) override
    {
        Log::Info("{} interface sound(s) this visit", sounds);
    }
};

Export<Quiet> extension;
