// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.engine: the layout hashes of the engine's structures on this runtime.

#ifndef WXL_SERVICE_ENGINE_HPP
#define WXL_SERVICE_ENGINE_HPP

#include <cstdint>

#include "wxl/sdk.hpp"
#include "wxl/services/engine.h"
#include "wxl/services/rows.hpp"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_engine_api>
    {
#define WXL_API_TABLE "wxl/services/engine.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief The typed facade over wxl.engine.
     *
     *   Layout(name)  the layout hash of an engine type on this runtime: WXL_E_NOT_FOUND for a
     *                 type it does not describe, WXL_E_UNSUPPORTED on a runtime without the service
     *
     * A mod compares it with the hash its engine headers carry (`wxl::engine::CM2Shared::
     * kLayoutHash`); wxl/engine/check.hpp does that for a list of types.
     */
    class EngineService : public detail::Facade<EngineService, wxl_engine_api>,
                          public api::facade::Methods<wxl_engine_api>
    {
    public:
        static constexpr const char* kName    = WXL_ENGINE_SERVICE;
        static constexpr uint32_t    kVersion = WXL_ENGINE_SERVICE_VERSION;
    };
} // namespace wxl

#endif // WXL_SERVICE_ENGINE_HPP
