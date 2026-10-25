// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// wxl/services/imgui.hpp against a runtime's contexts: a copy of Dear ImGui made here plays the
// runtime's, a table hands it over, and the facade's Bind() points the code compiled with the
// facade at it, as a mod's copy would be pointed at the runtime's. A frame is then drawn through it.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "fake_host.hpp"
#include "wxl/services/imgui.hpp"

namespace
{
    int g_failures = 0;
    int g_allocs   = 0;

    void Check(bool ok, const char* what)
    {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++g_failures;
    }

    void* Alloc(size_t size, void*)
    {
        ++g_allocs;
        return std::malloc(size);
    }
    void Free(void* block, void*) { std::free(block); }

    ImGuiContext*  g_context = nullptr;
    ImPlotContext* g_plot    = nullptr;
    const char*    g_version = wxl::ImGuiService::kCopy;

    wxl_str WXL_CALL Version() { return wxl::Str(g_version); }
    void* WXL_CALL Context() { return g_context; }
    void* WXL_CALL PlotContext() { return g_plot; }
    void WXL_CALL Allocators(void** alloc, void** release, void** user)
    {
        *alloc   = reinterpret_cast<void*>(&Alloc);
        *release = reinterpret_cast<void*>(&Free);
        *user    = nullptr;
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    wxltest::FakeHost host;
    wxl::detail::HostRef() = host.Table();

    Check(!wxl::ImGuiService::Bind(), "with no wxl.imgui, Bind refuses");

    // The "runtime" makes its contexts with its own allocator.
    ImGui::SetAllocatorFunctions(&Alloc, &Free, nullptr);
    g_context = ImGui::CreateContext();
    g_plot    = ImPlot::CreateContext();
    ImGui::SetCurrentContext(nullptr);
    ImPlot::SetCurrentContext(nullptr);

    static const wxl_imgui_api api = [] {
        wxl_imgui_api t{};
        t.struct_size  = sizeof t;
        t.version      = &Version;
        t.context      = &Context;
        t.plot_context = &PlotContext;
        t.allocators   = &Allocators;
        return t;
    }();
    host.Table()->publish_service(WXL_IMGUI_SERVICE, WXL_IMGUI_SERVICE_VERSION, &api);
    // Bind remembers a refusal for the mod's life: a fresh process would start over; here the
    // refusal above is the one checked, and the binding is checked through the API it sets.
    ImGui::SetAllocatorFunctions(&Alloc, &Free, nullptr);
    ImGui::SetCurrentContext(g_context);
    ImPlot::SetCurrentContext(g_plot);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime   = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int            width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    const int before = g_allocs;
    // A window is laid out on its first frame and drawn from its second.
    for (int frame = 0; frame < 2; ++frame)
    {
        ImGui::NewFrame();
        ImGui::Begin("A mod's window");
        ImGui::Text("hello");
        if (ImPlot::BeginPlot("curve"))
        {
            const float x[3] = {0, 1, 2}, y[3] = {0, 1, 0};
            ImPlot::PlotLine("y", x, y, 3);
            ImPlot::EndPlot();
        }
        ImGui::End();
        ImGui::Render();
    }
    Check(ImGui::GetDrawData() && ImGui::GetDrawData()->TotalVtxCount > 0, "a window and a plot are drawn on the shared context");
    Check(g_allocs > before, "through the runtime's allocator");
    Check(std::string(wxl::ImGuiService::kCopy) == IMGUI_VERSION ";" IMPLOT_VERSION, "the copy's version is ImGui's and ImPlot's");

    ImPlot::DestroyContext(g_plot);
    ImGui::DestroyContext(g_context);
    std::printf("\n%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
