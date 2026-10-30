// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.imgui: the runtime's Dear ImGui and ImPlot, for a mod's windows. A mod
// that includes this links wxl::imgui, which compiles Dear ImGui and ImPlot into it.

#ifndef WXL_SERVICE_IMGUI_HPP
#define WXL_SERVICE_IMGUI_HPP

#include <cstdint>
#include <cstring>

#include <imgui.h>
#include <implot.h>
#include "wxl/sdk.hpp"
#include "wxl/services/imgui.h"
#include "wxl/services/rows.hpp"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_imgui_api>
    {
#define WXL_API_TABLE "wxl/services/imgui.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief The typed facade over wxl.imgui.
     *
     *     class Panel : public wxl::RenderScript
     *     {
     *         void OnImGuiFrame(uint64_t, float) override
     *         {
     *             if (!wxl::ImGuiService::Bind()) return;   // this mod's ImGui on the runtime's context
     *             ImGui::Begin("My mod");
     *             ImGui::SliderFloat("Speed", &speed_, 0.0f, 10.0f);
     *             ImGui::End();
     *         }
     *         float speed_ = 1.0f;
     *     };
     *
     * Bind() points the mod's copy of Dear ImGui and ImPlot at the runtime's contexts and
     * allocator. It answers false, and logs once why, when the runtime has no wxl.imgui (abi older
     * than 0.1.25) or a copy of another version: draw nothing then.
     */
    class ImGuiService : public detail::Facade<ImGuiService, wxl_imgui_api>,
                         public api::facade::Methods<wxl_imgui_api>
    {
    public:
        static constexpr const char* kName    = WXL_IMGUI_SERVICE;
        static constexpr uint32_t    kVersion = WXL_IMGUI_SERVICE_VERSION;

        /// The version this mod's copy is: what the runtime's must be.
        static constexpr const char* kCopy = IMGUI_VERSION ";" IMPLOT_VERSION;

        /// Points this mod's ImGui and ImPlot at the runtime's; call it in OnImGuiFrame first.
        static bool Bind()
        {
            static int state = 0; // 0 not tried, 1 bound, 2 refused
            const wxl_imgui_api* api = Raw();
            if (state == 2 || !detail::Carries(api, &wxl_imgui_api::allocators)) return Refuse(state, "this runtime has no wxl.imgui");
            if (state == 0)
            {
                const wxl_str version = api->version();
                if (std::strlen(kCopy) != version.size || std::memcmp(kCopy, version.data, version.size) != 0)
                    return Refuse(state, "the runtime's Dear ImGui is another version than this mod's; rebuild against its SDK");
            }
            void* alloc   = nullptr;
            void* release = nullptr;
            void* user    = nullptr;
            api->allocators(&alloc, &release, &user);
            ImGui::SetAllocatorFunctions(reinterpret_cast<ImGuiMemAllocFunc>(alloc),
                                         reinterpret_cast<ImGuiMemFreeFunc>(release), user);
            ImGui::SetCurrentContext(static_cast<ImGuiContext*>(api->context()));
            ImPlot::SetCurrentContext(static_cast<ImPlotContext*>(api->plot_context()));
            state = 1;
            return ImGui::GetCurrentContext() != nullptr;
        }

    private:
        static bool Refuse(int& state, const char* why)
        {
            if (state != 2) Log(WXL_LOG_WARN, "wxl.imgui", why);
            state = 2;
            return false;
        }
    };
} // namespace wxl

#endif // WXL_SERVICE_IMGUI_HPP
