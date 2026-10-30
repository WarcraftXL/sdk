// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.animation: an animation of the game played on a unit.

#ifndef WXL_SERVICE_ANIMATION_HPP
#define WXL_SERVICE_ANIMATION_HPP

#include <cstdint>

#include "wxl/sdk.hpp"
#include "wxl/services/animation.h"
#include "wxl/services/rows.hpp"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_animation_api>
    {
#define WXL_API_TABLE "wxl/services/animation.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief The typed facade over wxl.animation.
     *
     *     // The player swings once, as the client does when it mines.
     *     wxl::AnimationService::Play(wxl::Player::Local(), 62, 0, 1.0);
     *     // A working loop, kept until stopped, over the legs' run too.
     *     wxl::AnimationService::Play(wxl::Player::Local(), 62, WXL_ANIMATION_LOOP | WXL_ANIMATION_OVERRIDE, 1.2);
     *     ...
     *     wxl::AnimationService::Stop(wxl::Player::Local());
     *
     *   Play(unit, anim_id, flags, speed)   plays an AnimationData animation on a unit
     *   Stop(unit)                          ends the mod's animation, the game chooses again
     *   Playing(unit)                       the animation a mod plays on it, as asked
     *   Current(unit)                       the animation its model shows now
     *
     * UnitAnimationEnded (Object family) says when and why a mod's animation ended.
     * WXL_E_UNSUPPORTED on a runtime older than abi 0.1.37.
     */
    class AnimationService : public detail::Facade<AnimationService, wxl_animation_api>,
                             public api::facade::Methods<wxl_animation_api>
    {
    public:
        static constexpr const char* kName    = WXL_ANIMATION_SERVICE;
        static constexpr uint32_t    kVersion = WXL_ANIMATION_SERVICE_VERSION;
    };
} // namespace wxl

#endif // WXL_SERVICE_ANIMATION_HPP
