// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.selectioncircle: the game's circles under its target, its pet's
// target and the move marker.

#ifndef WXL_SERVICE_SELECTIONCIRCLE_HPP
#define WXL_SERVICE_SELECTIONCIRCLE_HPP

#include <cstdint>

#include "wxl/sdk.hpp"
#include "wxl/services/rows.hpp"
#include "wxl/services/selectioncircle.h"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_selectioncircle_api>
    {
#define WXL_API_TABLE "wxl/services/selectioncircle.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief The typed facade over wxl.selectioncircle.
     *
     *     using wxl::SelectionCircleService;
     *     // No circle under the target, whatever it is.
     *     SelectionCircleService::Hide(WXL_SELECTIONCIRCLE_TARGET, 0);
     *     // Hostile targets in a texture of the mod, orange and a quarter larger.
     *     SelectionCircleService::SetStyle(WXL_SELECTIONCIRCLE_TARGET,
     *                                      WXL_SELECTIONCIRCLE_REACTION(WXL_REACTION_HOSTILE),
     *                                      "Interface\\MyMod\\Circle.blp", 0xFFFF8000u, 1.25f);
     *     ...
     *     SelectionCircleService::ResetAll();
     *
     *   Hide(kinds, reactions)                                    hides those circles
     *   SetStyle(kinds, reactions, texture, color, scale)         draws them in another look
     *   Reset(kinds, reactions)                                   gives them back
     *   HideObject(target), SetObjectStyle(target, ...), ResetObject(target)   for one object
     *   ResetAll()                                                gives back all the mod changed
     *   Drawn(), Hidden()                                         circles the last frame drew, left out
     *
     * Masks of 0 mean every kind and every reaction; texture "", color 0 and scale 0 keep the
     * game's. WXL_E_UNSUPPORTED from a runtime older than abi 0.1.28.
     */
    class SelectionCircleService : public detail::Facade<SelectionCircleService, wxl_selectioncircle_api>,
                                   public api::facade::Methods<wxl_selectioncircle_api>
    {
    public:
        static constexpr const char* kName    = WXL_SELECTIONCIRCLE_SERVICE;
        static constexpr uint32_t    kVersion = WXL_SELECTIONCIRCLE_SERVICE_VERSION;
    };
} // namespace wxl

#endif // WXL_SERVICE_SELECTIONCIRCLE_HPP
