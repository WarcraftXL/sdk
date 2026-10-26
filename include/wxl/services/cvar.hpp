// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.cvar: console variables a mod registers, and the client's own.

#ifndef WXL_SERVICE_CVAR_HPP
#define WXL_SERVICE_CVAR_HPP

#include <cstdint>

#include "wxl/sdk.hpp"
#include "wxl/services/cvar.h"
#include "wxl/services/rows.hpp"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_cvar_api>
    {
#define WXL_API_TABLE "wxl/services/cvar.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief The typed facade over wxl.cvar.
     *
     *     using wxl::CVarService;
     *     // A setting saved in Config.wtf, between 0.5 and 3.
     *     CVarService::Register("myMod_scale", "1", WXL_CVAR_SAVED, "Size of my mod's frame");
     *     CVarService::SetRange("myMod_scale", 0.5, 3.0);
     *     double scale = CVarService::GetNumber("myMod_scale").ValueOr(1.0);
     *     CVarService::SetNumber("myMod_scale", 2.0);   // CVarChanged follows
     *
     *   Register(name, default, flags, help), Release(name)   the mod's own variables
     *   SetRange(name, min, max)                             the numbers one accepts
     *   Exists(name), Flags(name)                            whether it is there, what it is
     *   Get, GetNumber, GetBool, GetDefault (name)           its value, read three ways
     *   Set, SetNumber, SetBool (name, value), Reset(name)   a new value, or the default
     *
     * Follow a variable with WorldScript::OnCVarChanged. Get answers a view of the client's
     * text, valid until the variable changes. WXL_E_UNSUPPORTED from a runtime older than abi
     * 0.1.31.
     */
    class CVarService : public detail::Facade<CVarService, wxl_cvar_api>, public api::facade::Methods<wxl_cvar_api>
    {
    public:
        static constexpr const char* kName    = WXL_CVAR_SERVICE;
        static constexpr uint32_t    kVersion = WXL_CVAR_SERVICE_VERSION;
    };
} // namespace wxl

#endif // WXL_SERVICE_CVAR_HPP
