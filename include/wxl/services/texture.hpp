// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.texture: textures made from an SVG or a PNG.

#ifndef WXL_SERVICE_TEXTURE_HPP
#define WXL_SERVICE_TEXTURE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "wxl/sdk.hpp"
#include "wxl/services/rows.hpp"
#include "wxl/services/texture.h"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_texture_api>
    {
#define WXL_API_TABLE "wxl/services/texture.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /// A restyle of an SVG's shapes with an id: what is left unset stays as the SVG draws it.
    struct TextureGroup
    {
        std::string             id;
        std::optional<uint32_t> color;   ///< 0xAARRGGBB, its alpha the paints' opacity
        std::optional<float>    width;   ///< strokes' width times the SVG's own: 1 as drawn, 0 none
        std::optional<bool>     visible;
    };

    /**
     * @brief What a texture is made from, built a call at a time:
     *
     *     wxl::TextureRequest(".\\assets\\reticle.svg")
     *         .Size(32, 32)
     *         .Color("fill", 0xFFFF4040u)
     *         .Color("outline", 0xCC000000u).Width("outline", 2.0f);
     */
    struct TextureRequest
    {
        std::string               source;
        uint32_t                  width  = 0; ///< 0: the source's own
        uint32_t                  height = 0;
        std::vector<TextureGroup> groups;
        uint32_t                  tint = 0;   ///< 0: none

        TextureRequest() = default;
        explicit TextureRequest(std::string_view from) : source(from) {}

        TextureRequest& Size(uint32_t w, uint32_t h)
        {
            width  = w;
            height = h;
            return *this;
        }
        TextureRequest& Color(std::string_view id, uint32_t argb)
        {
            Group(id).color = argb;
            return *this;
        }
        TextureRequest& Width(std::string_view id, float times)
        {
            Group(id).width = times;
            return *this;
        }
        TextureRequest& Hide(std::string_view id)
        {
            Group(id).visible = false;
            return *this;
        }
        TextureRequest& Show(std::string_view id)
        {
            Group(id).visible = true;
            return *this;
        }
        TextureRequest& Tint(uint32_t argb)
        {
            tint = argb;
            return *this;
        }

        /// The group of an id, added when it is not there yet.
        TextureGroup& Group(std::string_view id)
        {
            for (TextureGroup& group : groups)
                if (group.id == id) return group;
            groups.push_back(TextureGroup{std::string(id), {}, {}, {}});
            return groups.back();
        }
    };

    /// A texture made, as TextureService::Make answers it.
    struct Texture
    {
        std::string path;           ///< the name to load it by
        uint32_t    width          = 0; ///< the picture, in pixels
        uint32_t    height         = 0;
        uint32_t    texture_width  = 0; ///< the texture, powers of two
        uint32_t    texture_height = 0;
        float       right          = 1.0f; ///< the picture's far texture coordinates
        float       bottom         = 1.0f;
        bool        reused         = false;
    };

    /**
     * @brief The typed facade over wxl.texture.
     *
     *     const wxl::TextureService textures;
     *     std::string why;
     *     auto made = textures.Make(wxl::TextureRequest(".\\assets\\reticle.svg").Size(32, 32)
     *                                   .Color("fill", 0xFFFF4040u), &why);
     *     if (made) // made->path, made->right, made->bottom
     *     else      WXL_LOG_WARN("no reticle: %s", why.c_str());
     *
     *   Make(request, reason)   the texture, or why there is none
     *   Stats()                 what the service holds
     *
     * WXL_E_UNSUPPORTED from a runtime older than abi 0.1.30.
     */
    class TextureService : public detail::Facade<TextureService, wxl_texture_api>,
                           public api::facade::Methods<wxl_texture_api>
    {
    public:
        static constexpr const char* kName    = WXL_TEXTURE_SERVICE;
        static constexpr uint32_t    kVersion = WXL_TEXTURE_SERVICE_VERSION;

        /// Makes a texture of an SVG or a PNG, or answers the one already made; @p reason, when
        /// given, receives why there is none.
        Expected<Texture> Make(const TextureRequest& request, std::string* reason = nullptr) const
        {
            const wxl_texture_api* api = Raw();
            if (!detail::Carries(api, &wxl_texture_api::make))
            {
                if (reason) *reason = "this runtime has no wxl.texture (abi 0.1.30)";
                return Unexpected{WXL_E_UNSUPPORTED};
            }
            std::vector<wxl_texture_group> groups;
            groups.reserve(request.groups.size());
            for (const TextureGroup& group : request.groups)
            {
                wxl_texture_group g{};
                g.struct_size = sizeof g;
                g.id          = Str(group.id);
                if (group.color)
                {
                    g.set |= WXL_TEXTURE_SET_COLOR;
                    g.color = *group.color;
                }
                if (group.width)
                {
                    g.set |= WXL_TEXTURE_SET_WIDTH;
                    g.width = *group.width;
                }
                if (group.visible)
                {
                    g.set |= WXL_TEXTURE_SET_VISIBLE;
                    g.visible = *group.visible ? WXL_TRUE : WXL_FALSE;
                }
                groups.push_back(g);
            }
            wxl_texture_request c{};
            c.struct_size = sizeof c;
            c.source      = Str(request.source);
            c.width       = request.width;
            c.height      = request.height;
            c.groups      = groups.empty() ? nullptr : groups.data();
            c.group_count = static_cast<uint32_t>(groups.size());
            c.tint        = request.tint;
            wxl_texture made{};
            made.struct_size        = sizeof made;
            const wxl_result result = api->make(&c, &made);
            if (result != WXL_OK)
            {
                if (reason) *reason = made.reason;
                return Unexpected{result};
            }
            Texture texture;
            texture.path           = made.path;
            texture.width          = made.width;
            texture.height         = made.height;
            texture.texture_width  = made.texture_width;
            texture.texture_height = made.texture_height;
            texture.right          = made.right;
            texture.bottom         = made.bottom;
            texture.reused         = made.reused != WXL_FALSE;
            return texture;
        }

        /// What the service holds.
        Expected<wxl_texture_stats> Stats() const
        {
            const wxl_texture_api* api = Raw();
            if (!detail::Carries(api, &wxl_texture_api::stats)) return Unexpected{WXL_E_UNSUPPORTED};
            wxl_texture_stats stats{};
            stats.struct_size       = sizeof stats;
            const wxl_result result = api->stats(&stats);
            if (result != WXL_OK) return Unexpected{result};
            return stats;
        }
    };
} // namespace wxl

#endif // WXL_SERVICE_TEXTURE_HPP
