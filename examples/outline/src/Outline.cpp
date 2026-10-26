// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// Outline: the player's target outlined, where it is seen, on nothing but wxl.render.
//
// While the world is drawn, each batch the game draws of the target's model (ModelBatchDrawn,
// root_model) is kept for the frame (KeepBatch). After the world (RenderPoint after_world), the kept
// batches are drawn again into a mask with a program of the mod's (shaders/mask.frag), tested
// against the frame's depth so that nothing in front of the target is outlined over; then a
// full-screen program (shaders/edge.frag) colours the pixels around the mask, onto the frame.
// On a device wxl.render does not drive, the mod logs why once and does nothing.

#include <cstdint>
#include <vector>

#include "wxl/objects.hpp"
#include "wxl/sdk.hpp"
#include "wxl/services/m2.hpp"
#include "wxl/services/render.hpp"

#include "edge_frag.h" // kOutlineEdgeFrag: shaders/edge.frag
#include "mask_frag.h" // kOutlineMaskFrag: shaders/mask.frag

namespace
{
    constexpr const char* kTag = "outline";

    /// What the outline looks like: its colour (alpha: how much it covers) and width in pixels.
    constexpr float kColor[4] = {1.0f, 0.8f, 0.2f, 0.9f};
    constexpr float kWidth    = 3.0f;

    struct EdgePush
    {
        float    color[4];
        float    pixel[2];
        float    width;
        uint32_t mask;
        uint32_t nearest;
    };

    class Outline final : public wxl::RenderScript
    {
    public:
        ~Outline() override { Release(); }

        void OnModelBatchDrawn(void*, void* root_model, uint32_t, int32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                               uint32_t blend_mode, uint32_t, uint32_t, uint32_t, uint32_t, float, float, void* batch) override
        {
            // Opaque parts of the target's model; blended ones (an enchant's glow) are no part of its shape.
            if (!batch || !target_ || root_model != target_ || blend_mode > 1) return;
            if (void* kept = wxl::RenderService::KeepBatch(batch)) kept_.push_back(kept);
        }

        void OnRenderPoint(wxl_render_point point, uint64_t, uint32_t width, uint32_t height, uint32_t) override
        {
            if (point == WXL_RENDER_POINT_AFTER_OPAQUE_WORLD)
            {
                // The world is being drawn: the target of this frame, its batches from here on.
                kept_.clear();
                target_ = TargetModel();
                return;
            }
            if (point != WXL_RENDER_POINT_AFTER_WORLD) return;
            const std::vector<const void*> batches = std::move(kept_);
            kept_.clear();
            if (batches.empty() || !width || !height || !Ready()) return;

            // The mask: the target where it is seen.
            wxl::RenderPass masked;
            masked.Color(mask_, 0.0f, 0.0f, 0.0f, 0.0f).Depth(WXL_RENDER_SCENE_DEPTH);
            if (wxl::RenderService::Begin(masked) != WXL_OK) return;
            for (const void* batch : batches) wxl::RenderService::DrawBatch(batch, maskPipeline_, nullptr, 0);
            wxl::RenderService::End();

            // The edge around it, onto the frame.
            const wxl::Expected<uint32_t> slot    = wxl::RenderService::ImageSlot(mask_);
            const wxl::Expected<uint32_t> nearest = wxl::RenderService::Sampler(0);
            if (!slot || !nearest) return;
            const EdgePush push{{kColor[0], kColor[1], kColor[2], kColor[3]},
                                {1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height)},
                                kWidth * static_cast<float>(height) / 1080.0f,
                                *slot,
                                *nearest};
            wxl::RenderPass onto;
            onto.Color(WXL_RENDER_SCENE_COLOR);
            if (wxl::RenderService::Begin(onto) != WXL_OK) return;
            wxl::RenderService::Fullscreen(edgePipeline_, &push, sizeof push);
            wxl::RenderService::End();
        }

        void Release()
        {
            wxl::RenderService::DestroyPipeline(maskPipeline_);
            wxl::RenderService::DestroyPipeline(edgePipeline_);
            wxl::RenderService::DestroyImage(mask_);
            maskPipeline_ = edgePipeline_ = mask_ = WXL_HANDLE_NONE;
        }

    private:
        /// The model the target draws with (the top of what hangs on it); null without a target.
        static const void* TargetModel()
        {
            const wxl::Expected<wxl::Unit> target = wxl::Player::Local().Target();
            if (!target || !*target) return nullptr;
            return wxl::M2Service::ObjectModel(*target);
        }

        /// The mask and the programs, made at the first frame wxl.render draws.
        bool Ready()
        {
            if (mask_ && maskPipeline_ && edgePipeline_) return true;
            if (said_) return false;
            if (!wxl::RenderService::Supported())
            {
                wxl::Log(WXL_LOG_INFO, kTag, "no outline on this client: {}", wxl::RenderService::UnsupportedReason());
                said_ = true;
                return false;
            }
            // The frame's size and samples, so that the frame's depth tests it.
            mask_ = wxl::RenderService::CreateScreenImage(1, WXL_RENDER_FORMAT_R8, 0);
            wxl::RenderPipelineDesc mask = wxl::RenderPipelineDesc::Batch(kOutlineMaskFrag, sizeof kOutlineMaskFrag);
            mask.Named("outline mask").Colors({WXL_RENDER_FORMAT_R8}).Depth(WXL_RENDER_FORMAT_DEPTH32F, WXL_RENDER_DEPTH_LESS_EQUAL, false);
            maskPipeline_ = wxl::RenderService::CreatePipeline(mask);
            wxl::RenderPipelineDesc edge = wxl::RenderPipelineDesc::Fullscreen(kOutlineEdgeFrag, sizeof kOutlineEdgeFrag);
            edge.Named("outline edge").Blend(0, WXL_RENDER_BLEND_ALPHA);
            edgePipeline_ = wxl::RenderService::CreatePipeline(edge);
            if (mask_ && maskPipeline_ && edgePipeline_) return true;
            wxl::Log(WXL_LOG_WARN, kTag, "the outline's image or programs were refused (the log says why); no outline");
            Release();
            said_ = true;
            return false;
        }

        const void*              target_ = nullptr;
        std::vector<const void*> kept_;
        wxl_handle               mask_         = WXL_HANDLE_NONE;
        wxl_handle               maskPipeline_ = WXL_HANDLE_NONE;
        wxl_handle               edgePipeline_ = WXL_HANDLE_NONE;
        bool                     said_         = false;
    };

    class OutlineMod final : public wxl::Mod
    {
    public:
        wxl_result OnLoad() override
        {
            wxl::ScriptMgr::Add(&outline_);
            return WXL_OK;
        }

        void OnUnload() override
        {
            wxl::ScriptMgr::Remove(&outline_);
            outline_.Release();
        }

    private:
        Outline outline_;
    };
} // namespace

WXL_MOD(OutlineMod, "wxl.outline", "Outline")
