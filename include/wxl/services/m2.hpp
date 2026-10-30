// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.m2: the models objects draw with, and models a mod hangs on them.

#ifndef WXL_SERVICE_M2_HPP
#define WXL_SERVICE_M2_HPP

#include <cstdint>
#include <string_view>
#include <utility>

#include "wxl/objects.hpp"
#include "wxl/sdk.hpp"
#include "wxl/services/m2.h"
#include "wxl/services/rows.hpp"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_m2_api>
    {
#define WXL_API_TABLE "wxl/services/m2.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief The typed facade over wxl.m2.
     *
     *     void OnAppearanceChanged(uint32_t change, void*, void* model, uint32_t, uint32_t,
     *                              uint32_t slot, uint32_t display, bool) override
     *     {
     *         if (change != WXL_APPEARANCE_WORN || slot != 4) return;   // a belt put on
     *         buckle_ = wxl::M2Model(model, "Item\\Buckle.m2");
     *         buckle_.Attach(model, 53, WXL_M2_ATTACH_ROOT);
     *     }
     *
     *   ObjectModel(object)               the model an object draws with
     *   ItemDisplay(id)                   an ItemDisplayInfo row
     *   RacePrefix(race)                  "Hu", "Or": what model file names carry
     *   CreateModel(scene, path)          a model of the mod's (M2Model holds one)
     *   Attach(model, parent, point, f)   hangs it on a model (WXL_M2_ATTACH_* flags)
     *   Detach(model), Parent(model)      takes it off, what it hangs on
     *   SetGeosets(model, first, last, v) shows or hides geosets
     *   ReplaceTexture(model, type, path) a texture of its own
     *   AttachmentPoint(model, point)     where a point sits in the bind pose
     *   AttachToBone(model, parent, point, bone, offset)  an attachment point made by hand
     *
     * Models of the world (abi 0.1.21), what spell effects are made of:
     *
     *   CreateWorldModel(path)            a model of the world's scene (M2Model::World holds one)
     *   Place(model, placement)           puts it in the world, or moves it there
     *   SetTransform(model, placement)    how it sits on the point it hangs on
     *   PlayAnimation(model, anim, speed), SetAlpha(model, a), SetEmitting(model, on)
     *   ModelLoaded(model), AnimationLength(model, anim), AttachmentWorld(model, point)
     *   GroundHeight(at, up, down)        the ground under a point
     *
     * And the bones the client poses every frame, on its animate threads (M2BoneWatch holds a
     * watcher; BoneInfo reads a file's skeleton inside one).
     *
     * Main thread only, but for BoneInfo. A missing service answers null, nothing, or
     * WXL_E_UNSUPPORTED.
     */
    class M2Service : public detail::Facade<M2Service, wxl_m2_api>, public api::facade::Methods<wxl_m2_api>
    {
    public:
        static constexpr const char* kName    = WXL_M2_SERVICE;
        static constexpr uint32_t    kVersion = WXL_M2_SERVICE_VERSION;

        /// The ItemDisplayInfo row @p display_id; WXL_E_NOT_FOUND for none.
        static Expected<wxl_m2_item_display> ItemDisplay(uint32_t display_id)
        {
            const wxl_m2_api* api = Raw();
            if (!detail::Carries(api, &wxl_m2_api::item_display)) return Unexpected{WXL_E_UNSUPPORTED};
            wxl_m2_item_display row{};
            row.struct_size         = sizeof row;
            const wxl_result result = api->item_display(display_id, &row);
            if (result != WXL_OK) return Unexpected{result};
            return row;
        }

        /// Gives back a model this mod made; nothing for null.
        static void Release(void* model)
        {
            const wxl_m2_api* api = Raw();
            if (model && detail::Carries(api, &wxl_m2_api::release_model)) api->release_model(model);
        }

        /// One bone of a model file: its parent's index (-1 for the root) and its pivot.
        struct Bone
        {
            int16_t  parent;
            wxl_vec3 pivot;
        };

        /// Bone @p index of a model file (a wxl_m2_bones' file); WXL_E_NOT_FOUND past the last.
        /// Safe inside a bone watcher.
        static Expected<Bone> BoneInfo(void* file, uint32_t index)
        {
            const wxl_m2_api* api = Raw();
            if (!detail::Carries(api, &wxl_m2_api::bone_info)) return Unexpected{WXL_E_UNSUPPORTED};
            Bone             bone{-1, {}};
            const wxl_result result = api->bone_info(file, index, &bone.parent, &bone.pivot);
            if (result != WXL_OK) return Unexpected{result};
            return bone;
        }

        /// Starts a bone watcher (M2BoneWatch holds one); its token, for UnwatchBones.
        static Expected<uint32_t> WatchBones(wxl_m2_bones_filter_fn filter, wxl_m2_bones_fn fn, void* user)
        {
            const wxl_m2_api* api = Raw();
            if (!detail::Carries(api, &wxl_m2_api::watch_bones)) return Unexpected{WXL_E_UNSUPPORTED};
            uint32_t         token  = 0;
            const wxl_result result = api->watch_bones(filter, fn, user, &token);
            if (result != WXL_OK) return Unexpected{result};
            return token;
        }

        static void UnwatchBones(uint32_t token)
        {
            const wxl_m2_api* api = Raw();
            if (token && detail::Carries(api, &wxl_m2_api::unwatch_bones)) api->unwatch_bones(token);
        }
    };

    /// A wxl_m2_placement at a point, turned and scaled.
    inline wxl_m2_placement M2Placement(wxl_vec3 position, float yaw = 0.0f, float pitch = 0.0f, float roll = 0.0f,
                                        float scale = 1.0f)
    {
        wxl_m2_placement placement{};
        placement.struct_size = sizeof placement;
        placement.position    = position;
        placement.yaw         = yaw;
        placement.pitch       = pitch;
        placement.roll        = roll;
        placement.scale       = scale;
        return placement;
    }

    /**
     * @brief A model of the mod's own, given back when this goes: movable, not copyable.
     *
     * Empty when the service is missing or the scene made none; every call on an empty one does
     * nothing and answers WXL_E_INVALID.
     */
    class M2Model
    {
    public:
        M2Model() = default;
        /// Makes a model of @p path in the scene @p scene_model is in.
        M2Model(void* scene_model, std::string_view path) : model_(M2Service::CreateModel(scene_model, path)) {}
        /// Makes a model of @p path in the world's scene: it shows once placed, or hung on a model.
        static M2Model World(std::string_view path)
        {
            M2Model model;
            model.model_ = M2Service::CreateWorldModel(path);
            return model;
        }
        M2Model(M2Model&& other) noexcept : model_(std::exchange(other.model_, nullptr)) {}
        M2Model& operator=(M2Model&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                model_ = std::exchange(other.model_, nullptr);
            }
            return *this;
        }
        M2Model(const M2Model&)            = delete;
        M2Model& operator=(const M2Model&) = delete;
        ~M2Model() { Reset(); }

        void* Get() const { return model_; }
        explicit operator bool() const { return model_ != nullptr; }

        /// Gives the model back now.
        void Reset()
        {
            M2Service::Release(model_);
            model_ = nullptr;
        }

        wxl_result Attach(void* parent, uint32_t attachment, uint32_t flags = 0) const
        {
            return model_ ? M2Service::Attach(model_, parent, attachment, flags) : WXL_E_INVALID;
        }
        wxl_result AttachToBone(void* parent, uint32_t attachment, uint32_t key_bone, wxl_vec3 offset) const
        {
            return model_ ? M2Service::AttachToBone(model_, parent, attachment, key_bone, offset) : WXL_E_INVALID;
        }
        void  Detach() const { if (model_) M2Service::Detach(model_); }
        void* Parent() const { return model_ ? M2Service::Parent(model_) : nullptr; }
        void  SetGeosets(uint32_t first, uint32_t last, bool visible) const
        {
            if (model_) M2Service::SetGeosets(model_, first, last, visible);
        }
        wxl_result ReplaceTexture(uint32_t type, std::string_view path) const
        {
            return model_ ? M2Service::ReplaceTexture(model_, type, path) : WXL_E_INVALID;
        }

        wxl_result Place(const wxl_m2_placement& placement) const
        {
            return model_ ? M2Service::Place(model_, placement) : WXL_E_INVALID;
        }
        wxl_result SetTransform(const wxl_m2_placement& placement) const
        {
            return model_ ? M2Service::SetTransform(model_, placement) : WXL_E_INVALID;
        }
        wxl_result PlayAnimation(uint32_t anim_id, float speed = 1.0f) const
        {
            return model_ ? M2Service::PlayAnimation(model_, anim_id, speed) : WXL_E_INVALID;
        }
        bool Loaded() const { return model_ && M2Service::ModelLoaded(model_); }
        Expected<uint32_t> AnimationLength(uint32_t anim_id) const
        {
            if (!model_) return Unexpected{WXL_E_INVALID};
            return M2Service::AnimationLength(model_, anim_id);
        }
        void SetAlpha(float alpha) const
        {
            if (model_) M2Service::SetAlpha(model_, alpha);
        }
        void SetEmitting(bool emitting) const
        {
            if (model_) M2Service::SetEmitting(model_, emitting);
        }
        Expected<wxl_vec3> AttachmentWorld(uint32_t attachment) const
        {
            if (!model_) return Unexpected{WXL_E_INVALID};
            return M2Service::AttachmentWorld(model_, attachment);
        }

    private:
        void* model_ = nullptr;
    };

    /**
     * @brief A bone watcher, stopped when this goes: movable, not copyable.
     *
     * Its callbacks run on the client's animate threads, more than one at a time: the filter once
     * per model file, the watcher every frame for each model of a file it accepted, with the bone
     * matrices it may rewrite. They must be thread-safe, and call nothing of wxl.m2 but BoneInfo.
     *
     *     bones_ = wxl::M2BoneWatch(
     *         [](void*, wxl_str path, uint32_t, void*) -> wxl_bool {
     *             return wxl::View(path).ends_with("Cape.m2") ? WXL_TRUE : WXL_FALSE;
     *         },
     *         [](const wxl_m2_bones* bones, void*) { Simulate(*bones); });
     */
    class M2BoneWatch
    {
    public:
        M2BoneWatch() = default;
        M2BoneWatch(wxl_m2_bones_filter_fn filter, wxl_m2_bones_fn fn, void* user = nullptr)
        {
            const Expected<uint32_t> token = M2Service::WatchBones(filter, fn, user);
            if (token) token_ = *token;
        }
        M2BoneWatch(M2BoneWatch&& other) noexcept : token_(std::exchange(other.token_, 0u)) {}
        M2BoneWatch& operator=(M2BoneWatch&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                token_ = std::exchange(other.token_, 0u);
            }
            return *this;
        }
        M2BoneWatch(const M2BoneWatch&)            = delete;
        M2BoneWatch& operator=(const M2BoneWatch&) = delete;
        ~M2BoneWatch() { Reset(); }

        /// Whether the watcher runs: false when the service is missing or sixteen are live.
        explicit operator bool() const { return token_ != 0; }

        /// Stops the watcher now.
        void Reset()
        {
            M2Service::UnwatchBones(token_);
            token_ = 0;
        }

    private:
        uint32_t token_ = 0;
    };
} // namespace wxl

#endif // WXL_SERVICE_M2_HPP
