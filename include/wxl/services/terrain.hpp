// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The typed facade over wxl.terrain: the ground of the map the player is on, read and reshaped.

#ifndef WXL_SERVICE_TERRAIN_HPP
#define WXL_SERVICE_TERRAIN_HPP

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "wxl/sdk.hpp"
#include "wxl/services/rows.hpp"
#include "wxl/services/terrain.h"
#include "wxl/terrain/grid.hpp"

namespace wxl::api::facade
{
    template <>
    struct Methods<wxl_terrain_api>
    {
#define WXL_API_TABLE "wxl/services/terrain.def"
#include "wxl/services/methods.inc"
    };
} // namespace wxl::api::facade

namespace wxl
{
    /**
     * @brief The typed facade over wxl.terrain.
     *
     *     // Lower the ground one yard within three of the aimed point, then put it back.
     *     if (auto hit = wxl::TerrainService::Trace(eyes, far))
     *     {
     *         std::vector<wxl_terrain_height> edits;
     *         for (const wxl_terrain_vertex& v : wxl::TerrainService::VerticesIn(hit->point.x, hit->point.y, 3.0f))
     *             if (v.loaded) edits.push_back({v.chunk_x, v.chunk_y, v.index, v.z - 1.0f});
     *         wxl::TerrainService::SetHeights(edits);
     *         ...
     *         wxl::TerrainService::ResetAll();
     *     }
     *
     *   Height(x, y[, original])          the ground's height under a point, or nothing
     *   Presence(x, y)                    ground, a hole, a chunk not loaded, or none
     *   Trace(start, finish)              where a segment first meets the ground
     *   Locate(x, y)                      the chunk, cell and nearest vertex of a point
     *   Vertices(chunk_x, chunk_y[, original])
     *                                     the 145 vertices' world positions, empty when not loaded
     *   VerticesIn(x, y, radius)          every vertex within a radius, each once, with its chunk
     *                                     and index: a brush in world space
     *   SetHeights(edits[, layer])        vertices set to absolute heights, all or none
     *   Reset(chunk_x, chunk_y[, layer]), ResetAll([layer])
     *                                     the mod's edits taken off
     *   Stats()                           what the service holds and what the last edit cost
     *
     * The grid's arithmetic (indices, rows, world positions, the height of a triangle) is in
     * wxl/terrain/grid.hpp, namespace wxl::terrain, callable with no runtime. Edits go in layer 0
     * unless another is named; each layer of a mod is reset on its own, and all go when the mod
     * unloads.
     */
    class TerrainService : public detail::Facade<TerrainService, wxl_terrain_api>,
                           public api::facade::Methods<wxl_terrain_api>
    {
    public:
        static constexpr const char* kName    = WXL_TERRAIN_SERVICE;
        static constexpr uint32_t    kVersion = WXL_TERRAIN_SERVICE_VERSION;

        /// The calling mod's layer provider (abi 0.1.42): @p fn is told of each texture layer as
        /// the client makes it and answers its look; null takes it off.
        /// @p flags (abi 0.1.43): WXL_TERRAIN_LAYER_PARAMS_EVERY_DRAW, or 0.
        static wxl_result SetLayerProvider(wxl_terrain_layer_fn fn, void* user = nullptr, uint32_t flags = 0)
        {
            const wxl_terrain_api* api = Raw();
            if (!detail::Carries(api, &wxl_terrain_api::set_layer_provider)) return WXL_E_UNSUPPORTED;
            if (!fn) return api->set_layer_provider(nullptr);
            wxl_terrain_layer_provider provider{};
            provider.struct_size = sizeof provider;
            provider.fn          = fn;
            provider.user        = user;
            provider.flags       = flags;
            return api->set_layer_provider(&provider);
        }

        /// The ground's height under a point; nothing where there is none (a hole, a chunk not
        /// loaded, no terrain), off the main thread or on a runtime without the service.
        static std::optional<float> Height(float x, float y, bool original = false)
        {
            const wxl_terrain_api* api = Raw();
            if (!detail::Carries(api, &wxl_terrain_api::height)) return std::nullopt;
            double z = 0.0;
            if (api->height(x, y, original ? WXL_TRUE : WXL_FALSE, &z) != WXL_TRUE) return std::nullopt;
            return static_cast<float>(z);
        }

        /// Whether there is ground under a point; WXL_TERRAIN_NONE on a runtime without the
        /// service.
        static wxl_terrain_presence Presence(float x, float y)
        {
            const wxl_terrain_api* api = Raw();
            if (!detail::Carries(api, &wxl_terrain_api::presence)) return WXL_TERRAIN_NONE;
            return static_cast<wxl_terrain_presence>(api->presence(x, y));
        }

        /// Where a segment first meets the loaded ground.
        static std::optional<wxl_terrain_hit> Trace(wxl_vec3 start, wxl_vec3 finish)
        {
            const wxl_terrain_api* api = Raw();
            if (!detail::Carries(api, &wxl_terrain_api::trace)) return std::nullopt;
            wxl_terrain_hit hit{};
            hit.struct_size = sizeof hit;
            if (api->trace(start, finish, &hit) != WXL_TRUE) return std::nullopt;
            return hit;
        }

        /// The chunk, cell and nearest vertex of a point; nothing off the map. `loaded` is false
        /// on a runtime without the service, which this answers from the grid alone.
        static std::optional<wxl_terrain_place> Locate(float x, float y)
        {
            const wxl_terrain_api* api = Raw();
            wxl_terrain_place      place{};
            place.struct_size = sizeof place;
            if (detail::Carries(api, &wxl_terrain_api::locate))
            {
                if (api->locate(x, y, &place) != WXL_TRUE) return std::nullopt;
                return place;
            }
            const terrain::Locus at = terrain::Locate(x, y);
            if (!at.on_map) return std::nullopt;
            place.chunk_x  = at.chunk_x;
            place.chunk_y  = at.chunk_y;
            place.cell_row = at.cell_row;
            place.cell_col = at.cell_col;
            place.nearest  = at.nearest;
            place.loaded   = WXL_FALSE;
            return place;
        }

        /// A loaded chunk's 145 vertices, in index order; empty when it is not loaded.
        static std::vector<wxl_vec3> Vertices(int32_t chunk_x, int32_t chunk_y, bool original = false)
        {
            std::vector<wxl_vec3>  out;
            const wxl_terrain_api* api = Raw();
            if (!detail::Carries(api, &wxl_terrain_api::vertices)) return out;
            out.resize(WXL_TERRAIN_CHUNK_VERTICES);
            const uint32_t count = api->vertices(chunk_x, chunk_y, original ? WXL_TRUE : WXL_FALSE, out.data(),
                                                 static_cast<uint32_t>(out.size()));
            out.resize(count < out.size() ? count : out.size());
            return out;
        }

        /// Every vertex within `radius` yards of (x, y), each once, with its chunk and index for
        /// SetHeights; empty off the map, for a radius beyond WXL_TERRAIN_RADIUS_LIMIT, or on a
        /// runtime without the member.
        static std::vector<wxl_terrain_vertex> VerticesIn(float x, float y, float radius)
        {
            std::vector<wxl_terrain_vertex> out;
            const wxl_terrain_api*          api = Raw();
            if (!detail::Carries(api, &wxl_terrain_api::vertices_in)) return out;
            const uint32_t count = api->vertices_in(x, y, radius, nullptr, 0);
            out.resize(count);
            const uint32_t written = count ? api->vertices_in(x, y, radius, out.data(), count) : 0;
            out.resize(written < count ? written : count);
            return out;
        }

        /// Sets vertices to absolute heights, all or none, in a layer of the mod's.
        static wxl_result SetHeights(const std::vector<wxl_terrain_height>& edits, uint32_t layer = 0)
        {
            const wxl_terrain_api* api = Raw();
            if (!detail::Carries(api, &wxl_terrain_api::set_heights)) return WXL_E_UNSUPPORTED;
            return api->set_heights(edits.data(), static_cast<uint32_t>(edits.size()), layer);
        }

        /// Takes a layer's edits off a chunk.
        static wxl_result Reset(int32_t chunk_x, int32_t chunk_y, uint32_t layer = 0)
        {
            const wxl_terrain_api* api = Raw();
            if (!detail::Carries(api, &wxl_terrain_api::reset_chunk)) return WXL_E_UNSUPPORTED;
            return api->reset_chunk(chunk_x, chunk_y, layer);
        }

        /// Takes a layer's edits off everywhere.
        static wxl_result ResetAll(uint32_t layer = 0)
        {
            const wxl_terrain_api* api = Raw();
            if (!detail::Carries(api, &wxl_terrain_api::reset_all)) return WXL_E_UNSUPPORTED;
            return api->reset_all(layer);
        }

        /// What the service holds and what its last edit cost.
        static Expected<wxl_terrain_stats> Stats()
        {
            const wxl_terrain_api* api = Raw();
            if (!detail::Carries(api, &wxl_terrain_api::stats)) return Unexpected{WXL_E_UNSUPPORTED};
            wxl_terrain_stats stats{};
            stats.struct_size       = sizeof stats;
            const wxl_result result = api->stats(&stats);
            if (result != WXL_OK) return Unexpected{result};
            return stats;
        }
    };
} // namespace wxl

#endif // WXL_SERVICE_TERRAIN_HPP
