// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.vk: the runtime's Vulkan device, raw.

#ifndef WXL_SERVICE_VK_HPP
#define WXL_SERVICE_VK_HPP

#include <cstdint>

#include "wxl/sdk.hpp"
#include "wxl/services/rows.hpp"
#include "wxl/services/vk.h"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_vk_api>
    {
#define WXL_API_TABLE "wxl/services/vk.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief The typed facade over wxl.vk, for a mod that records Vulkan commands of its own inside
     *        RenderPoint (wxl/services/vk.h says the rules).
     *
     *     auto cmd = static_cast<VkCommandBuffer>(wxl::VkService::CommandBuffer());
     *     if (!cmd) return;
     *     ... vkCmd*(cmd, ...) ...
     *     wxl::GxService::InvalidateState();
     *
     *   Instance(), PhysicalDevice(), Device(), Queue()   the dispatchable handles, as void*
     *   QueueFamily(), ApiVersion(), GetInstanceProcAddr()
     *   CommandBuffer()                                  the frame's, inside a point; null elsewhere
     *   Image(h, resolved), ImageView(h, resolved), Buffer(h)  a wxl.render resource's objects (uint64_t)
     *   SceneColor(), SceneDepth()                       the frame's images
     *   Frame(), FramesCompleted()                       when what a frame used may be destroyed
     */
    class VkService : public detail::Facade<VkService, wxl_vk_api>, public api::facade::Methods<wxl_vk_api>
    {
    public:
        static constexpr const char* kName    = WXL_VK_SERVICE;
        static constexpr uint32_t    kVersion = WXL_VK_SERVICE_VERSION;
    };
} // namespace wxl

#endif // WXL_SERVICE_VK_HPP
