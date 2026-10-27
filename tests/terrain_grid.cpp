// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// wxl/terrain/grid.hpp against the client's own arithmetic: vertex positions as
// CMapChunk::CreateVerticesWorld lays them, heights as CMap::GetHeightTerrain finds them (its code
// transcribed below), normals of planes, the copies of an edge vertex, the walk of a segment's
// cells, and what a reshaping's normals cost. Pure arithmetic: builds and runs anywhere.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <set>
#include <tuple>
#include <vector>

#include "wxl/terrain/grid.hpp"

namespace
{
    namespace grid = wxl::terrain;

    int g_failures = 0;

    void Check(bool condition, const char* what)
    {
        if (condition) return;
        std::printf("  FAIL  %s\n", what);
        ++g_failures;
    }

    bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

    /// CMap::GetHeightTerrain (0x007AD3B0) as the fork has it, the non-SSE path: the cell's
    /// triangle by its two diagonals, the plane through it, in the chunk's own coordinates.
    float ClientHeight(const float* local, float top_x, float top_y, float base_z, float x, float y, int32_t row, int32_t col)
    {
        const float S  = grid::kCellSize;
        const float sc[5][2] = {{0.0f, 0.0f}, {0.0f, -S}, {-S, -S}, {-S, 0.0f}, {-grid::kHalfCell, -grid::kHalfCell}};
        const int   fanIndices[8] = {17, 0, 0, 1, 18, 17, 1, 18};
        const int   subIndices[8] = {3, 0, 0, 1, 2, 3, 1, 2};
        const float oX = row * -S, oY = -S * col;
        const float dx = x - top_x, dy = y - top_y;
        int         fan = 0;
        if (((oX + sc[2][0]) - (oX + sc[0][0])) * ((oY + sc[2][1]) - dy) - ((oY + sc[2][1]) - (oY + sc[0][1])) * ((oX + sc[2][0]) - dx) <= 0.0f)
            fan = 1;
        if (((oX + sc[3][0]) - (oX + sc[1][0])) * ((oY + sc[3][1]) - dy) - ((oY + sc[3][1]) - (oY + sc[1][1])) * ((oX + sc[3][0]) - dx) <= 0.0f)
            fan += 2;
        const float* h = &local[row + col + 16 * row];
        const grid::Point3 a{oX + sc[subIndices[2 * fan]][0], oY + sc[subIndices[2 * fan]][1], h[fanIndices[2 * fan]]};
        const grid::Point3 b{oX + sc[subIndices[2 * fan + 1]][0], oY + sc[subIndices[2 * fan + 1]][1], h[fanIndices[2 * fan + 1]]};
        const grid::Point3 c{oX + sc[4][0], oY + sc[4][1], h[9]};
        // C4Plane::From3Pos(c, a, b): the unit normal of (a - c) x (b - c), d = -n . c.
        const grid::Point3 n = grid::Unit(grid::Cross(grid::Sub(a, c), grid::Sub(b, c)));
        const float        d = -grid::Dot(n, c);
        return base_z - (n.y * dy + n.x * dx + d) / n.z;
    }

    void Indices()
    {
        Check(grid::OuterIndex(0, 0) == 0 && grid::OuterIndex(0, 8) == 8 && grid::InnerIndex(0, 0) == 9 &&
                  grid::OuterIndex(1, 0) == 17 && grid::OuterIndex(8, 8) == 144 && grid::InnerIndex(7, 7) == 135,
              "outer (r, c) is 17r + c and inner (r, c) 17r + 9 + c");
        bool round = true;
        for (uint32_t i = 0; i < grid::kVertexCount; ++i)
        {
            const grid::VertexPlace p = grid::PlaceOf(i);
            round = round && (p.inner ? grid::InnerIndex(p.row, p.col) : grid::OuterIndex(p.row, p.col)) == i &&
                    p.inner == grid::IsInner(i);
        }
        Check(round, "every index goes to its row and column and back");
    }

    void Positions()
    {
        // CreateVerticesWorld: x from the rows (m_cOffset.y), y from the columns (m_cOffset.x).
        const int32_t cx = 517, cy = 301;
        const float   top  = 17066.666f - cy * 33.333332f;
        const float   left = 17066.666f - cx * 33.333332f;
        bool          same = true;
        for (uint32_t i = 0; i < grid::kVertexCount; ++i)
        {
            const grid::VertexPlace place = grid::PlaceOf(i);
            const float half = place.inner ? grid::kHalfCell : 0.0f;
            const grid::Point2 p = grid::VertexPlane(cx, cy, i);
            same = same && Near(p.x, top - place.row * grid::kCellSize - half, 0.01f) &&
                   Near(p.y, left - place.col * grid::kCellSize - half, 0.01f);
        }
        Check(same, "a vertex's place is CreateVerticesWorld's: x by the rows, y by the columns");

        const grid::Locus at = grid::Locate(top - 3 * grid::kCellSize - 0.1f, left - 5 * grid::kCellSize - 0.2f);
        Check(at.on_map && at.chunk_x == cx && at.chunk_y == cy && at.cell_row == 3 && at.cell_col == 5 &&
                  at.nearest == grid::OuterIndex(3, 5),
              "a point next to an outer vertex is in that vertex's cell, nearest to it");
        const grid::Locus mid = grid::Locate(top - 3.5f * grid::kCellSize, left - 5.5f * grid::kCellSize);
        Check(mid.nearest == grid::InnerIndex(3, 5), "a cell's middle is nearest its inner vertex");
        const grid::Locus corner = grid::Locate(top - 0.01f, left - 0.01f);
        Check(corner.chunk_x == cx && corner.chunk_y == cy && corner.nearest == 0, "a chunk's corner is its vertex 0");
        Check(!grid::Locate(17066.666f + 1.0f, 0.0f).on_map && !grid::Locate(0.0f, -17066.666f - 1.0f).on_map,
              "off the map is off the map");
        Check(grid::Locate(0.0f, 0.0f).chunk_x == 512 && grid::Locate(0.0f, 0.0f).chunk_y == 512, "the world's origin is chunk (512, 512)");
    }

    void Heights()
    {
        std::mt19937                          rng(7);
        std::uniform_real_distribution<float> h(-4.0f, 4.0f), u(0.0f, 0.999f);
        const int32_t cx = 600, cy = 400;
        float         local[grid::kVertexCount], absolute[grid::kVertexCount];
        const float   base = 112.5f;
        for (uint32_t i = 0; i < grid::kVertexCount; ++i)
        {
            local[i]    = h(rng);
            absolute[i] = local[i] + base;
        }
        const grid::Point2 corner = grid::ChunkCorner(cx, cy);
        float               worst = 0.0f;
        for (int n = 0; n < 20000; ++n)
        {
            const int32_t row = static_cast<int32_t>(u(rng) * 8), col = static_cast<int32_t>(u(rng) * 8);
            const float   x   = corner.x - (row + u(rng)) * grid::kCellSize;
            const float   y   = corner.y - (col + u(rng)) * grid::kCellSize;
            const float   ours   = grid::HeightInCell(cx, cy, absolute, row, col, x, y);
            const float   theirs = ClientHeight(local, corner.x, corner.y, base, x, y, row, col);
            worst = std::max(worst, std::fabs(ours - theirs));
        }
        std::printf("  height against CMap::GetHeightTerrain: worst difference %.5f yd over 20000 points\n", worst);
        Check(worst < 0.01f, "the height is the client's own, on the same triangle, everywhere in a chunk");

        bool exact = true;
        for (uint32_t i = 0; i < grid::kVertexCount; ++i)
        {
            const grid::VertexPlace place = grid::PlaceOf(i);
            const grid::Point2      p     = grid::VertexPlane(cx, cy, i);
            const int32_t row = place.inner ? place.row : std::min(place.row, 7);
            const int32_t col = place.inner ? place.col : std::min(place.col, 7);
            exact = exact && Near(grid::HeightInCell(cx, cy, absolute, row, col, p.x, p.y), absolute[i], 0.01f);
        }
        Check(exact, "at a vertex the height is the vertex's");

        // A segment straight down meets the ground at that height.
        const float        x = corner.x - 2.3f * grid::kCellSize, y = corner.y - 6.7f * grid::kCellSize;
        const float        z = grid::HeightInCell(cx, cy, absolute, 2, 6, x, y);
        float              t = -1.0f;
        for (int tri = 0; tri < 4 && t < 0.0f; ++tri)
        {
            uint32_t idx[3];
            grid::CellTriangle(2, 6, tri, idx);
            t = grid::SegmentTriangle(grid::Point3{x, y, 500.0f}, grid::Point3{0.0f, 0.0f, -1000.0f},
                                      grid::VertexWorld(cx, cy, idx[0], absolute), grid::VertexWorld(cx, cy, idx[1], absolute),
                                      grid::VertexWorld(cx, cy, idx[2], absolute));
        }
        Check(t >= 0.0f && Near(500.0f - 1000.0f * t, z, 0.01f), "a segment straight down meets the ground at its height");
        Check(grid::CellIsHole(1u << 5, 2, 3) && grid::CellIsHole(1u << 5, 3, 2) && !grid::CellIsHole(1u << 5, 2, 4),
              "hole bit 4 * (row / 2) + (col / 2)");
    }

    /// Heights of a plane z = a x + b y + c over every chunk, for the normals' sampler.
    struct Plane
    {
        float a, b, c;
        bool  loaded(int32_t cx, int32_t cy) const { return cx >= 100 && cx <= 102 && cy >= 200 && cy <= 202; }
        bool  operator()(int32_t cx, int32_t cy, uint32_t i, float& z) const
        {
            if (!loaded(cx, cy)) return false;
            const grid::Point2 p = grid::VertexPlane(cx, cy, i);
            z                    = a * p.x + b * p.y + c;
            return true;
        }
    };

    void Normals()
    {
        const Plane  flat{0.0f, 0.0f, 10.0f};
        const Plane  tilted{0.2f, -0.1f, 5.0f};
        const grid::Point3 expect = grid::Unit(grid::Point3{-0.2f, 0.1f, 1.0f});
        bool flatUp = true, tiltedRight = true;
        for (int32_t cy = 200; cy <= 202; ++cy)
            for (int32_t cx = 100; cx <= 102; ++cx)
                for (uint32_t i = 0; i < grid::kVertexCount; ++i)
                {
                    const grid::Point3 n = grid::VertexNormal(cx, cy, i, flat);
                    flatUp = flatUp && Near(n.x, 0.0f, 1e-5f) && Near(n.y, 0.0f, 1e-5f) && Near(n.z, 1.0f, 1e-5f);
                    const grid::Point3 m = grid::VertexNormal(cx, cy, i, tilted);
                    const bool right = Near(m.x, expect.x, 1e-3f) && Near(m.y, expect.y, 1e-3f) && Near(m.z, expect.z, 1e-3f);
                    if (!right && tiltedRight)
                        std::printf("  chunk (%d, %d) vertex %u: (%.4f, %.4f, %.4f), the plane's (%.4f, %.4f, %.4f)\n", cx, cy, i,
                                    m.x, m.y, m.z, expect.x, expect.y, expect.z);
                    tiltedRight = tiltedRight && right;
                }
        Check(flatUp, "flat ground's normals point up, edges of what is loaded included");
        Check(tiltedRight, "a plane's normals are the plane's, at every vertex, across chunk edges and at the edge of what is loaded");

        int8_t packed[3];
        grid::PackNormal(grid::Point3{0.0f, 0.0f, 1.0f}, packed);
        Check(packed[0] == 0 && packed[1] == 0 && packed[2] == 127, "up packs as (0, 0, 127)");
        grid::PackNormal(expect, packed);
        Check(Near(packed[0] / 127.0f, expect.x, 0.01f) && Near(packed[2] / 127.0f, expect.z, 0.01f), "a normal packs in 127ths");

        // A bump at one vertex leans the normals around it away from it, and only those.
        Plane       base{0.0f, 0.0f, 0.0f};
        const int32_t cx = 101, cy = 201;
        const uint32_t bumped = grid::OuterIndex(4, 4);
        std::set<std::tuple<int32_t, int32_t, uint32_t>> around;
        grid::ForNormalNeighbours(cx, cy, bumped, [&](int32_t x, int32_t y, uint32_t i) { around.insert({x, y, i}); });
        auto bump = [&](int32_t x, int32_t y, uint32_t i, float& z) {
            if (!base(x, y, i, z)) return false;
            if (x == cx && y == cy && i == bumped) z += 3.0f;
            return true;
        };
        bool onlyAround = true;
        for (int32_t y = 200; y <= 202; ++y)
            for (int32_t x = 100; x <= 102; ++x)
                for (uint32_t i = 0; i < grid::kVertexCount; ++i)
                {
                    const grid::Point3 n = grid::VertexNormal(x, y, i, bump);
                    const bool tilted = !Near(n.z, 1.0f, 1e-6f);
                    onlyAround = onlyAround && tilted == (around.count({x, y, i}) != 0 && !(x == cx && y == cy && i == bumped));
                }
        Check(onlyAround, "a vertex's height moves the normals of the vertices sharing a triangle with it, and no other");
        Check(around.size() == 9, "an outer vertex shares triangles with itself, four outer and four inner vertices");

        // Neighbourhood is mutual, across chunk edges too.
        bool mutual = true;
        for (const uint32_t i : {0u, 8u, 136u, 144u, 9u, 77u, 4u})
            grid::ForNormalNeighbours(cx, cy, i, [&](int32_t x, int32_t y, uint32_t j) {
                bool back = false;
                grid::ForNormalNeighbours(x, y, j, [&](int32_t bx, int32_t by, uint32_t k) {
                    bool same = false;
                    grid::ForCopies(cx, cy, i, [&](int32_t qx, int32_t qy, uint32_t q) { same = same || (qx == bx && qy == by && q == k); });
                    back = back || same;
                });
                mutual = mutual && back;
            });
        Check(mutual, "sharing a triangle is mutual, across chunk edges and corners");
    }

    void Copies()
    {
        std::vector<std::tuple<int32_t, int32_t, uint32_t>> seen;
        auto collect = [&](int32_t x, int32_t y, uint32_t i) { seen.emplace_back(x, y, i); };
        grid::ForCopies(10, 20, grid::InnerIndex(3, 3), collect);
        Check(seen.size() == 1, "an inner vertex is in one chunk");
        seen.clear();
        grid::ForCopies(10, 20, grid::OuterIndex(4, 0), collect);
        Check(seen.size() == 2 && seen[1] == std::make_tuple(9, 20, grid::OuterIndex(4, 8)),
              "an outer vertex of column 0 is also column 8 of the chunk before it");
        seen.clear();
        grid::ForCopies(10, 20, grid::OuterIndex(8, 8), collect);
        std::set<std::tuple<int32_t, int32_t, uint32_t>> corner(seen.begin(), seen.end());
        Check(seen.size() == 4 && corner.count({11, 21, 0}) && corner.count({11, 20, grid::OuterIndex(8, 0)}) &&
                  corner.count({10, 21, grid::OuterIndex(0, 8)}),
              "a corner is in four chunks");
        seen.clear();
        grid::ForCopies(0, 0, 0, collect);
        Check(seen.size() == 1, "the map's own corner is in one");
    }

    void Region()
    {
        // Every vertex of the chunks around, by its global place, against what the circle lists.
        const float x = grid::ChunkCorner(300, 400).x - 1.0f, y = grid::ChunkCorner(300, 400).y + 0.5f, r = 9.0f;
        std::set<std::pair<long, long>> expected; // global place in half cells: once per ground point
        for (int32_t cy = 398; cy <= 401; ++cy)
            for (int32_t cx = 298; cx <= 301; ++cx)
                for (uint32_t i = 0; i < grid::kVertexCount; ++i)
                {
                    const grid::Point2 p = grid::VertexPlane(cx, cy, i);
                    const float dx = p.x - x, dy = p.y - y;
                    if (dx * dx + dy * dy > r * r) continue;
                    const grid::VertexPlace place = grid::PlaceOf(i);
                    expected.insert({2L * (cy * 8 + place.row) + (place.inner ? 1 : 0), 2L * (cx * 8 + place.col) + (place.inner ? 1 : 0)});
                }
        std::set<std::pair<long, long>> listed;
        bool once = true, inside = true, home = true;
        grid::ForVerticesInCircle(x, y, r, [&](int32_t cx, int32_t cy, uint32_t i, grid::Point2 p) {
            const grid::VertexPlace place = grid::PlaceOf(i);
            once   = once && listed.insert({2L * (cy * 8 + place.row) + (place.inner ? 1 : 0),
                                            2L * (cx * 8 + place.col) + (place.inner ? 1 : 0)}).second;
            inside = inside && (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y) <= r * r * 1.0001f;
            home   = home && (place.inner || (place.row < 8 && place.col < 8));
        });
        Check(once && inside && listed == expected,
              "a circle lists every vertex within it once, a chunk's corner across four chunks included");
        Check(home, "an outer vertex is named in the chunk whose cells it starts");
        int n = 0;
        grid::ForVerticesInCircle(x, y, 0.0f, [&](int32_t, int32_t, uint32_t i, grid::Point2) { n += i == 0 ? 1 : 0; });
        Check(n == 0, "a radius of 0 off a vertex lists nothing");
        int corner = 0;
        const grid::Point2 c = grid::ChunkCorner(300, 400);
        grid::ForVerticesInCircle(c.x, c.y, 0.5f, [&](int32_t cx, int32_t cy, uint32_t i, grid::Point2) {
            corner += cx == 300 && cy == 400 && i == 0 ? 1 : 100;
        });
        Check(corner == 1, "a chunk's corner is listed once, in its own chunk");
    }

    void Walk()
    {
        // From cell (row 10, col 20) to (row 13, col 27): adjacent steps, start and end included.
        const auto at = [](double r, double c) {
            return grid::Point2{static_cast<float>(grid::kOrigin - r * grid::kCellSize), static_cast<float>(grid::kOrigin - c * grid::kCellSize)};
        };
        std::vector<std::pair<int64_t, int64_t>> cells;
        grid::WalkCells(at(10.3, 20.6), at(13.7, 27.2), 1000, [&](int64_t r, int64_t c) {
            cells.emplace_back(r, c);
            return true;
        });
        bool adjacent = true;
        for (size_t i = 1; i < cells.size(); ++i)
            adjacent = adjacent && std::abs(cells[i].first - cells[i - 1].first) + std::abs(cells[i].second - cells[i - 1].second) == 1;
        Check(!cells.empty() && cells.front() == std::make_pair<int64_t, int64_t>(10, 20) &&
                  cells.back() == std::make_pair<int64_t, int64_t>(13, 27) && adjacent && cells.size() == 1 + 3 + 7,
              "a segment's cells, in order, each next to the one before");
        cells.clear();
        grid::WalkCells(at(5.5, 5.5), at(5.5, 5.5), 10, [&](int64_t r, int64_t c) {
            cells.emplace_back(r, c);
            return true;
        });
        Check(cells.size() == 1, "a vertical segment is one cell");
    }

    void Cost()
    {
        // The normals a 3 x 3 chunk reshaping makes again, as wxl.terrain does after an edit.
        const Plane bumpy{0.05f, 0.03f, 0.0f};
        const auto  begun = std::chrono::steady_clock::now();
        float       sink  = 0.0f;
        int         count = 0;
        for (int round = 0; round < 100; ++round)
            for (int32_t cy = 200; cy <= 202; ++cy)
                for (int32_t cx = 100; cx <= 102; ++cx)
                    for (uint32_t i = 0; i < grid::kVertexCount; ++i, ++count)
                        sink += grid::VertexNormal(cx, cy, i, bumpy).z;
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begun).count();
        std::printf("  normals: %.3f us per vertex, %.1f us for every vertex of 3 x 3 chunks (%.0f)\n", us / count,
                    us / 100.0, static_cast<double>(sink) * 0.0);
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("the terrain's grid\n");
    Indices();
    Positions();
    Heights();
    Normals();
    Copies();
    Region();
    Walk();
    Cost();
    if (g_failures) std::printf("%d check(s) failed\n", g_failures);
    else std::printf("all passed\n");
    return g_failures ? 1 : 0;
}
