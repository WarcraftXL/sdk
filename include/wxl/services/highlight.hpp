// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.highlight: a world object outlined and lit by a glow.

#ifndef WXL_SERVICE_HIGHLIGHT_HPP
#define WXL_SERVICE_HIGHLIGHT_HPP

#include <cstdint>

#include "wxl/sdk.hpp"
#include "wxl/services/highlight.h"
#include "wxl/services/rows.hpp"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_highlight_api>
    {
#define WXL_API_TABLE "wxl/services/highlight.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief The typed facade over wxl.highlight.
     *
     *     if (!wxl::HighlightService::Supported())
     *         wxl::Log(WXL_LOG_INFO, "my-mod", "no outline: {}", wxl::HighlightService::UnsupportedReason());
     *     // The target in red, a 3-pixel outline and a glow over 12 pixels at half strength.
     *     wxl::HighlightService::Set(target, 0xFFE03030u, 3.0f, 12.0f, 0.5f);
     *     ...
     *     wxl::HighlightService::Clear(target);
     *
     *   Supported()                                      whether this runtime draws them now
     *   UnsupportedReason()                              why not, for a log line
     *   Set(target, color, outline, glow, glow_strength) highlights it until cleared
     *   Clear(target), ClearAll()                        takes the mod's highlights off
     *   Drawn()                                          batches the last highlighted frame drew
     *
     * Never seen through walls. WXL_E_UNSUPPORTED from Set on a runtime that does not draw them
     * (the recompiled client for now) or older than abi 0.1.27.
     */
    class HighlightService : public detail::Facade<HighlightService, wxl_highlight_api>,
                             public api::facade::Methods<wxl_highlight_api>
    {
    public:
        static constexpr const char* kName    = WXL_HIGHLIGHT_SERVICE;
        static constexpr uint32_t    kVersion = WXL_HIGHLIGHT_SERVICE_VERSION;
    };
} // namespace wxl

#endif // WXL_SERVICE_HIGHLIGHT_HPP
