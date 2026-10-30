// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The layout check a mod built against the engine headers makes at load.

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "wxl/engine/support.hpp"
#include "wxl/services/engine.hpp"

namespace wxl::engine
{
    /**
     * @brief What the runtime says of the engine types @p T...: an empty string when it lays out
     *        every one of them as these headers do, otherwise one sentence naming each type that
     *        differs and why.
     *
     * A type differs when the runtime's layout hash (wxl.engine's layout) is not the one these
     * headers were generated with: the client it runs on was built from another fork, and a
     * member read through these headers may land on other bytes. A runtime that predates
     * wxl.engine (abi 0.1.22) cannot say, and that is reported too.
     */
    template <class... T>
    std::string LayoutMismatch()
    {
        std::string out;
        if (!EngineService::Available())
            return "the runtime publishes no wxl.engine (it predates abi 0.1.22), so it cannot say whether "
                   "it lays out the engine types this mod reads as the mod's headers do";
        auto check = [&out](const char* name, uint64_t built) {
            auto served = EngineService::Layout(name);
            char line[256];
            if (!served)
                std::snprintf(line, sizeof line, "%s (this runtime does not describe it)", name);
            else if (*served != built)
                std::snprintf(line, sizeof line, "%s (layout %016llX here, %016llX in the mod's headers)", name,
                              static_cast<unsigned long long>(*served), static_cast<unsigned long long>(built));
            else
                return;
            out += out.empty() ? "" : ", ";
            out += line;
        };
        (check(T::kName, T::kLayoutHash), ...);
        if (!out.empty())
            out = "the engine types " + out +
                  " differ from the headers this mod was built against: rebuild it against the SDK of this runtime";
        return out;
    }

    /**
     * @brief Checks the engine types @p T... and, when the runtime lays any of them out
     *        differently, stops the mod with LayoutMismatch's sentence.
     *
     * Called from OnLoad, before the mod reads any of them:
     *
     *     void OnLoad() override
     *     {
     *         if (!wxl::engine::RequireLayouts<wxl::engine::CM2Shared, wxl::engine::M2Data>()) return;
     *         ...
     *     }
     *
     * @return true when every type matches; false once the mod has been stopped.
     */
    template <class... T>
    bool RequireLayouts()
    {
        const std::string why = LayoutMismatch<T...>();
        if (why.empty()) return true;
        wxl::detail::FailMod(why.c_str());
        return false;
    }
}
