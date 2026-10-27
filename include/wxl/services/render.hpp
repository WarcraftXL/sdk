// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.render: a mod's passes, pipelines and images in the client's frame,
// on the Vulkan renderer.

#ifndef WXL_SERVICE_RENDER_HPP
#define WXL_SERVICE_RENDER_HPP

#include <cstdint>
#include <initializer_list>

#include "wxl/sdk.hpp"
#include "wxl/services/render.h"
#include "wxl/services/rows.hpp"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_render_api>
    {
#define WXL_API_TABLE "wxl/services/render.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief The typed facade over wxl.render.
     *
     *     // Once: a pipeline of the mod's SPIR-V, an image of half the frame's size.
     *     wxl::RenderPipelineDesc desc = wxl::RenderPipelineDesc::Fullscreen(kTintFrag, sizeof kTintFrag);
     *     desc.Colors({WXL_RENDER_FORMAT_BGRA8}).Blend(0, WXL_RENDER_BLEND_ALPHA);
     *     wxl_handle tint = wxl::RenderService::CreatePipeline(desc);
     *
     *     // In a RenderScript:
     *     void OnRenderPoint(wxl_render_point point, uint64_t, uint32_t, uint32_t, uint32_t) override
     *     {
     *         if (point != WXL_RENDER_POINT_AFTER_WORLD) return;
     *         wxl::RenderPass pass;
     *         pass.Color(WXL_RENDER_SCENE_COLOR);
     *         if (wxl::RenderService::Begin(pass) != WXL_OK) return;
     *         const float color[4] = {1.0f, 0.5f, 0.0f, 0.2f};
     *         wxl::RenderService::Fullscreen(tint, color, sizeof color);
     *         wxl::RenderService::End();
     *     }
     *
     * Every member of the C table is a method of the same name (CreateImage, ImageSlot, Draw,
     * DrawBatch, KeepBatch, SceneColorSlot, ReplaceShader, Dispatch, BonePalette, ...);
     * DestroyImage, DestroyBuffer and DestroyPipeline give resources back. On a device it does not drive, Supported() is false and
     * the calls answer WXL_E_UNSUPPORTED or 0.
     */
    class RenderService : public detail::Facade<RenderService, wxl_render_api>, public api::facade::Methods<wxl_render_api>
    {
    public:
        static constexpr const char* kName    = WXL_RENDER_SERVICE;
        static constexpr uint32_t    kVersion = WXL_RENDER_SERVICE_VERSION;

        static void DestroyImage(wxl_handle image)
        {
            const wxl_render_api* api = Raw();
            if (detail::Carries(api, &wxl_render_api::destroy_image)) api->destroy_image(image);
        }
        static void DestroyBuffer(wxl_handle buffer)
        {
            const wxl_render_api* api = Raw();
            if (detail::Carries(api, &wxl_render_api::destroy_buffer)) api->destroy_buffer(buffer);
        }
        static void DestroyPipeline(wxl_handle pipeline)
        {
            const wxl_render_api* api = Raw();
            if (detail::Carries(api, &wxl_render_api::destroy_pipeline)) api->destroy_pipeline(pipeline);
        }
    };

    /// A wxl_render_pipeline_desc filled in steps: one colour attachment of the frame's format,
    /// nothing blended, no depth, by default.
    struct RenderPipelineDesc : wxl_render_pipeline_desc
    {
        RenderPipelineDesc() : wxl_render_pipeline_desc{}
        {
            struct_size      = sizeof(wxl_render_pipeline_desc);
            color_count      = 1;
            color_formats[0] = WXL_RENDER_FORMAT_BGRA8;
        }

        /// The mod's vertex and fragment programs.
        static RenderPipelineDesc Graphics(const void* vertex_spirv, uint32_t vertex_bytes, const void* fragment_spirv, uint32_t fragment_bytes)
        {
            RenderPipelineDesc d;
            d.kind          = WXL_RENDER_PIPELINE_GRAPHICS;
            d.vertex        = vertex_spirv;
            d.vertex_size   = vertex_bytes;
            d.fragment      = fragment_spirv;
            d.fragment_size = fragment_bytes;
            return d;
        }
        /// The mod's fragment program over the whole pass.
        static RenderPipelineDesc Fullscreen(const void* fragment_spirv, uint32_t fragment_bytes)
        {
            RenderPipelineDesc d;
            d.kind          = WXL_RENDER_PIPELINE_FULLSCREEN;
            d.fragment      = fragment_spirv;
            d.fragment_size = fragment_bytes;
            return d;
        }
        /// The mod's compute program (abi 0.1.34), run by RenderService::Dispatch.
        static RenderPipelineDesc Compute(const void* compute_spirv, uint32_t compute_bytes)
        {
            RenderPipelineDesc d;
            d.kind         = WXL_RENDER_PIPELINE_COMPUTE;
            d.color_count  = 0;
            d.compute      = compute_spirv;
            d.compute_size = compute_bytes;
            return d;
        }
        /// The mod's fragment program behind a client batch's vertex program.
        static RenderPipelineDesc Batch(const void* fragment_spirv, uint32_t fragment_bytes)
        {
            RenderPipelineDesc d;
            d.kind          = WXL_RENDER_PIPELINE_BATCH;
            d.fragment      = fragment_spirv;
            d.fragment_size = fragment_bytes;
            d.cull          = WXL_RENDER_CULL_BATCH;
            return d;
        }

        RenderPipelineDesc& Named(const char* text)
        {
            name = text;
            return *this;
        }
        /// The colour attachments' formats, in order (at most WXL_RENDER_MAX_COLORS).
        RenderPipelineDesc& Colors(std::initializer_list<uint32_t> formats)
        {
            color_count = 0;
            for (uint32_t f : formats)
                if (color_count < WXL_RENDER_MAX_COLORS) color_formats[color_count++] = f;
            return *this;
        }
        RenderPipelineDesc& Depth(uint32_t format, uint32_t test, bool write)
        {
            depth_format = format;
            depth_test   = test;
            depth_write  = write ? WXL_TRUE : WXL_FALSE;
            return *this;
        }
        RenderPipelineDesc& Samples(uint32_t count)
        {
            samples = count;
            return *this;
        }
        RenderPipelineDesc& Blend(uint32_t attachment, uint32_t mode)
        {
            if (attachment < WXL_RENDER_MAX_COLORS) blend[attachment] = mode;
            return *this;
        }
        RenderPipelineDesc& Cull(uint32_t mode)
        {
            cull = mode;
            return *this;
        }
        RenderPipelineDesc& Topology(uint32_t value)
        {
            topology = value;
            return *this;
        }
    };

    /// A wxl_render_pass filled in steps.
    struct RenderPass : wxl_render_pass
    {
        RenderPass() : wxl_render_pass{} { struct_size = sizeof(wxl_render_pass); }

        /// One more colour attachment (an image, or WXL_RENDER_SCENE_COLOR), kept as it is.
        RenderPass& Color(wxl_handle image)
        {
            if (color_count < WXL_RENDER_MAX_COLORS) colors[color_count++] = image;
            return *this;
        }
        /// One more colour attachment, cleared to (r, g, b, a) first.
        RenderPass& Color(wxl_handle image, float r, float g, float b, float a)
        {
            if (color_count >= WXL_RENDER_MAX_COLORS) return *this;
            clear |= 1u << color_count;
            clear_colors[color_count][0] = r;
            clear_colors[color_count][1] = g;
            clear_colors[color_count][2] = b;
            clear_colors[color_count][3] = a;
            colors[color_count++]         = image;
            return *this;
        }
        /// The depth attachment (an image, or WXL_RENDER_SCENE_DEPTH), kept as it is.
        RenderPass& Depth(wxl_handle image)
        {
            depth = image;
            return *this;
        }
        /// The depth attachment, cleared to @p value first.
        RenderPass& Depth(wxl_handle image, float value)
        {
            depth       = image;
            clear      |= WXL_RENDER_CLEAR_DEPTH;
            clear_depth = value;
            return *this;
        }
    };
} // namespace wxl

#endif // WXL_SERVICE_RENDER_HPP
