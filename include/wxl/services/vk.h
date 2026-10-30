// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The raw Vulkan service: the runtime's Vulkan device, for a mod that records commands of its own.
//
//   instance, physical_device, device, queue, queue_family, get_instance_proc_addr, api_version
//                                   the device (dispatchable handles, as pointers)
//   command_buffer                  inside RenderPoint: the frame's command buffer, no rendering open
//   image, image_view, buffer       a wxl.render resource's Vulkan objects (non-dispatchable: 64 bits)
//   scene_color, scene_depth        the frame's images
//   frame, frames_completed         the open frame's number, and the last one the GPU finished
//
// A mod that uses it knows Vulkan; the runtime trusts it. The rules:
//   - no submission, no wait, no queue operation: the frame is the runtime's to submit;
//   - commands only into command_buffer, inside RenderPoint, with no rendering open but the mod's
//     own (begun and ended by the mod in the same handler); then wxl.gx's invalidate_state, so
//     that the renderer binds its own state again;
//   - wxl.render's images are in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL between passes (a
//     multisampled one's own image in VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL), the frame's colour in
//     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL and its depth in
//     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: a mod that changes a layout puts it back,
//     with the barriers it needs, before the handler returns;
//   - what the mod makes itself on the device is its own to destroy, once frames_completed() has
//     reached the frame that last used it, and before the device goes (SwapchainLost with the
//     device's objects, then the mod's unload).
// The validation layer (WarcraftXL.ini [gx] validation = 1) reports a mod's mistakes in the log.
//
// Main thread only. Every member answers null, 0 or WXL_E_UNSUPPORTED on a device that is not the
// runtime's Vulkan renderer (wxl.gx's api says which it is).

#ifndef WXL_SERVICE_VK_H
#define WXL_SERVICE_VK_H

#include "wxl/abi.h"

#define WXL_VK_SERVICE         "wxl.vk"
#define WXL_VK_SERVICE_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wxl_vk_api
{
    uint32_t struct_size;

    void*(WXL_CALL* instance)(void);        ///< VkInstance
    void*(WXL_CALL* physical_device)(void); ///< VkPhysicalDevice
    void*(WXL_CALL* device)(void);          ///< VkDevice
    void*(WXL_CALL* queue)(void);           ///< VkQueue: the one queue frames go to
    uint32_t(WXL_CALL* queue_family)(void); ///< its family's index
    /// PFN_vkGetInstanceProcAddr of the loader the runtime uses: every other function from it.
    void*(WXL_CALL* get_instance_proc_addr)(void);
    /// The device's Vulkan version (VK_MAKE_API_VERSION); at least 1.3.
    uint32_t(WXL_CALL* api_version)(void);

    /// Inside RenderPoint: the frame's VkCommandBuffer, the runtime's rendering ended. Null elsewhere.
    void*(WXL_CALL* command_buffer)(void);

    /// A wxl.render image's VkImage, its VkImageView (the resolved one's for a multisampled image
    /// when @p resolved), a buffer's VkBuffer. WXL_E_NOT_FOUND for no such resource of the mod's.
    wxl_result(WXL_CALL* image)(wxl_handle image, wxl_bool resolved, uint64_t* out);
    wxl_result(WXL_CALL* image_view)(wxl_handle image, wxl_bool resolved, uint64_t* out);
    wxl_result(WXL_CALL* buffer)(wxl_handle buffer, uint64_t* out);

    /// The frame's colour and depth VkImage (multisampled when it is), inside a frame.
    wxl_result(WXL_CALL* scene_color)(uint64_t* out);
    wxl_result(WXL_CALL* scene_depth)(uint64_t* out);

    /// The open frame's number, and the last number whose commands the GPU has finished.
    wxl_result(WXL_CALL* frame)(uint64_t* out);
    wxl_result(WXL_CALL* frames_completed)(uint64_t* out);
} wxl_vk_api;

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WXL_SERVICE_VK_H
