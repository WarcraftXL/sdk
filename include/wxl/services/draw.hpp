// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.draw: shapes drawn in the world.

#ifndef WXL_SERVICE_DRAW_HPP
#define WXL_SERVICE_DRAW_HPP

#include <cstdint>
#include <initializer_list>
#include <vector>

#include "wxl/sdk.hpp"
#include "wxl/services/draw.h"
#include "wxl/services/rows.hpp"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_draw_api>
    {
#define WXL_API_TABLE "wxl/services/draw.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief What wxl.draw draws, built by kind.
     *
     *     auto ring = wxl::DrawShape::Ring(target_pos, 8.0f, 6.0f);
     *     auto path = wxl::DrawShape::Line(points).OnGround();
     *
     * A line's and a polygon's points are kept by address: they must live until the call that
     * takes the shape returns (the service copies them).
     */
    struct DrawShape : wxl_draw_shape
    {
        DrawShape() : wxl_draw_shape{} { struct_size = sizeof(wxl_draw_shape); }

        static DrawShape Line(const std::vector<wxl_vec3>& points, bool closed = false)
        {
            DrawShape s;
            s.kind        = WXL_DRAW_LINE;
            s.points      = points.data();
            s.point_count = static_cast<uint32_t>(points.size());
            if (closed) s.flags |= WXL_DRAW_CLOSED;
            return s;
        }
        static DrawShape Circle(wxl_vec3 center, float radius)
        {
            DrawShape s;
            s.kind   = WXL_DRAW_CIRCLE;
            s.center = center;
            s.radius = radius;
            return s;
        }
        static DrawShape Ring(wxl_vec3 center, float radius, float inner)
        {
            DrawShape s = Circle(center, radius);
            s.kind      = WXL_DRAW_RING;
            s.inner     = inner;
            return s;
        }
        /// A cone (inner 0) or an arc of a ring, @p angle radians wide around @p facing.
        static DrawShape Sector(wxl_vec3 center, float radius, float facing, float angle, float inner = 0.0f)
        {
            DrawShape s = Circle(center, radius);
            s.kind      = WXL_DRAW_SECTOR;
            s.facing    = facing;
            s.angle     = angle;
            s.inner     = inner;
            return s;
        }
        /// @p length along @p facing, @p width across it.
        static DrawShape Rectangle(wxl_vec3 center, float length, float width, float facing)
        {
            DrawShape s;
            s.kind   = WXL_DRAW_RECTANGLE;
            s.center = center;
            s.size   = wxl_vec3{length, width, 0.0f};
            s.facing = facing;
            return s;
        }
        static DrawShape Polygon(const std::vector<wxl_vec3>& points)
        {
            DrawShape s;
            s.kind        = WXL_DRAW_POLYGON;
            s.points      = points.data();
            s.point_count = static_cast<uint32_t>(points.size());
            return s;
        }
        /// Lines every @p spacing yards over a rectangle @p length by @p width, turned to @p facing.
        static DrawShape Grid(wxl_vec3 center, float length, float width, float spacing, float facing = 0.0f)
        {
            DrawShape s = Rectangle(center, length, width, facing);
            s.kind      = WXL_DRAW_GRID;
            s.spacing   = spacing;
            return s;
        }
        static DrawShape Box(wxl_vec3 center, wxl_vec3 size, float facing = 0.0f, float pitch = 0.0f, float roll = 0.0f)
        {
            DrawShape s;
            s.kind   = WXL_DRAW_BOX;
            s.center = center;
            s.size   = size;
            s.facing = facing;
            s.pitch  = pitch;
            s.roll   = roll;
            return s;
        }
        static DrawShape Sphere(wxl_vec3 center, float radius)
        {
            DrawShape s = Circle(center, radius);
            s.kind      = WXL_DRAW_SPHERE;
            return s;
        }
        static DrawShape Cylinder(wxl_vec3 center, float radius, float height)
        {
            DrawShape s = Circle(center, radius);
            s.kind      = WXL_DRAW_CYLINDER;
            s.size.z    = height;
            return s;
        }

        /// A ground shape lies flat at center.z everywhere.
        DrawShape& Flat()
        {
            flags |= WXL_DRAW_FLAT;
            return *this;
        }
        /// A line lies on the ground.
        DrawShape& OnGround()
        {
            flags |= WXL_DRAW_ON_GROUND;
            return *this;
        }
        /// A line's opacity at each point (0 to 1, as many as its points), kept by address as the
        /// points are (abi 0.1.38).
        DrawShape& PointAlphas(const std::vector<float>& alphas)
        {
            point_alphas = alphas.empty() ? nullptr : alphas.data();
            return *this;
        }
    };

    /**
     * @brief How a wxl.draw shape looks: white lines 2 pixels wide, no fill, hidden behind walls,
     *        until told otherwise.
     *
     *     auto style = wxl::DrawStyle(0xE03030).Fill(0.25f).Width(3.0f).Pulse(0.8f);
     */
    struct DrawStyle : wxl_draw_style
    {
        explicit DrawStyle(uint32_t rgb = 0xFFFFFFu, float line_alpha = 1.0f) : wxl_draw_style{}
        {
            struct_size = sizeof(wxl_draw_style);
            color       = rgb & 0xFFFFFFu;
            alpha       = line_alpha;
            width       = 2.0f;
            depth       = WXL_DRAW_DEPTH_TESTED;
        }
        DrawStyle& Fill(float opacity)
        {
            fill_alpha = opacity;
            return *this;
        }
        DrawStyle& Width(float pixels)
        {
            width = pixels;
            return *this;
        }
        DrawStyle& OnTop()
        {
            depth = WXL_DRAW_DEPTH_ON_TOP;
            return *this;
        }
        DrawStyle& Pulse(float seconds)
        {
            pulse = seconds;
            return *this;
        }
        DrawStyle& Fade(float in_seconds, float out_seconds)
        {
            fade_in  = in_seconds;
            fade_out = out_seconds;
            return *this;
        }

        // abi 0.1.38 ---------------------------------------------------------------------------

        /// The inside in its own colour, not the lines'.
        DrawStyle& FillColor(uint32_t rgb)
        {
            fill_color = rgb & 0xFFFFFFu;
            flags |= WXL_DRAW_STYLE_FILL_COLOR;
            return *this;
        }
        /// The lines' edges fade over @p pixels.
        DrawStyle& Softness(float pixels)
        {
            softness = pixels;
            return *this;
        }
        /// A line's opacity goes from @p start at its first point to @p end at its last.
        DrawStyle& LineAlpha(float start, float end)
        {
            alpha_start = start;
            alpha_end   = end;
            flags |= WXL_DRAW_STYLE_LINE_ALPHA;
            return *this;
        }
        /// A shadow under the lines: @p x pixels right, @p y down, its stroke @p soft pixels wider
        /// and fading over as many.
        DrawStyle& Shadow(uint32_t rgb = 0x000000u, float opacity = 0.6f, float x = 1.5f, float y = 1.5f, float soft = 1.0f)
        {
            shadow_color    = rgb & 0xFFFFFFu;
            shadow_alpha    = opacity;
            shadow_x        = x;
            shadow_y        = y;
            shadow_softness = soft;
            return *this;
        }
        /// The inside drawn with a texture the client loads; @p right and @p bottom the part of it
        /// the picture fills (wxl.texture's answer), 1 for a whole texture. The path is kept by
        /// address until the call that takes the style returns.
        DrawStyle& Texture(const char* path, float right = 1.0f, float bottom = 1.0f)
        {
            texture        = path;
            texture_right  = right;
            texture_bottom = bottom;
            return *this;
        }
        /// How the texture lies: @p mode (wxl_draw_uv), one picture's size (of the bounds, or yards
        /// for WORLD), turned by @p rotation radians and moved by @p offset_x, @p offset_y pictures.
        DrawStyle& Uv(uint32_t mode, float size_x = 1.0f, float size_y = 1.0f, float rotation = 0.0f, float offset_x = 0.0f,
                      float offset_y = 0.0f)
        {
            uv          = mode;
            uv_size_x   = size_x;
            uv_size_y   = size_y;
            uv_rotation = rotation;
            uv_offset_x = offset_x;
            uv_offset_y = offset_y;
            return *this;
        }
        /// How the inside goes onto the ground (wxl_draw_blend).
        DrawStyle& Blend(uint32_t mode)
        {
            blend = mode;
            return *this;
        }
        /// The inside fades out over @p yards towards its border.
        DrawStyle& Feather(float yards)
        {
            feather = yards;
            return *this;
        }
        /// The inside fades out from the centre outwards over @p part of the radius (0 to 1).
        DrawStyle& RadialFade(float part)
        {
            radial_fade = part;
            return *this;
        }
        /// What the scene hides of the shape drawn at @p opacity of its own (abi 0.1.39).
        DrawStyle& Occluded(float opacity)
        {
            occluded_alpha = opacity;
            return *this;
        }
    };

    /**
     * @brief The typed facade over wxl.draw.
     *
     *     // Each frame, a grid under the aimed point (immediate mode: nothing to remove).
     *     wxl::DrawService::Draw(wxl::DrawShape::Grid(aim, 12.0f, 12.0f, 1.0f), wxl::DrawStyle(0x40C0FF, 0.7f).Width(1.5f));
     *
     *     // A telegraph that stays three seconds and fades out.
     *     wxl_handle cone = wxl::DrawService::Add(wxl::DrawShape::Sector(at, 15.0f, facing, 1.2f),
     *                                             wxl::DrawStyle(0xFF4020).Fill(0.3f).Fade(0.1f, 0.4f), 3.0f);
     *     wxl::DrawService::Attach(cone, boss, WXL_DRAW_ATTACH_TURN);
     *
     *     // Cracks multiplied into the ground, soft at its border (abi 0.1.38).
     *     wxl::DrawService::Add(wxl::DrawShape::Circle(at, 4.0f),
     *                           wxl::DrawStyle(0xFFFFFF, 0.0f).Fill(0.9f).Texture("SPELLS\\CrackedGround.blp")
     *                               .Blend(WXL_DRAW_BLEND_MODULATE).Feather(1.0f), 30.0f);
     *
     *   Supported(), UnsupportedReason()        whether this runtime draws them, and why not
     *   Draw(shape, style)                      for the next frame of the world only
     *   Add(shape, style, duration)             kept: a handle; duration 0 until removed
     *   Update(h, shape), Move(h, center, facing), SetStyle(h, style), SetDuration(h, seconds)
     *   Attach(h, object, flags), Detach(h)     follows an object, gone when it leaves view
     *   Remove(h), Clear()                      one kept shape, or every shape of the mod
     *   Stats()                                 what the last frame drew and what it cost
     *
     * The same picture on Direct3D 9 and on the Vulkan renderer.
     */
    class DrawService : public detail::Facade<DrawService, wxl_draw_api>, public api::facade::Methods<wxl_draw_api>
    {
    public:
        static constexpr const char* kName    = WXL_DRAW_SERVICE;
        static constexpr uint32_t    kVersion = WXL_DRAW_SERVICE_VERSION;

        /// The kept shape goes now.
        static void Remove(wxl_handle shape)
        {
            const wxl_draw_api* api = Raw();
            if (detail::Carries(api, &wxl_draw_api::remove)) api->remove(shape);
        }

        /// What the last frame drew and what it cost.
        static Expected<wxl_draw_stats> Stats()
        {
            const wxl_draw_api* api = Raw();
            if (!detail::Carries(api, &wxl_draw_api::stats)) return Unexpected{WXL_E_UNSUPPORTED};
            wxl_draw_stats stats{};
            stats.struct_size       = sizeof stats;
            const wxl_result result = api->stats(&stats);
            if (result != WXL_OK) return Unexpected{result};
            return stats;
        }
    };
} // namespace wxl

#endif // WXL_SERVICE_DRAW_HPP
