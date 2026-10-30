// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The terrain's grid, as the 3.3.5a client lays it out: pure arithmetic, no client, no runtime.
// wxl.terrain answers through these functions on both runtimes, the C++ facade exposes them, and
// a mod may use them to plan edits without a call.
//
// --- The grid ------------------------------------------------------------------------------
//
// A map is 64 x 64 tiles of 16 x 16 chunks: 1024 x 1024 chunks, each 100/3 yards on a side.
// A chunk is named by its column and row on the map, `chunk_x` and `chunk_y` (0 to 1023), the
// numbers the client keeps for it:
//
//     chunk_x = floor((17066.666 - world y) / 33.333332)
//     chunk_y = floor((17066.666 - world x) / 33.333332)
//
// so chunk_x grows as world y falls (towards the east) and chunk_y as world x falls (towards the
// south). A chunk has 145 vertices, stored row by row: a row of 9 outer vertices, then (but after
// the last) a row of 8 inner ones, each in the middle of the square of four outer vertices around
// it ("V9" and "V8"). Vertex `index`:
//
//     outer (row r 0-8, column c 0-8)   index = 17 * r + c
//     inner (row r 0-7, column c 0-7)   index = 17 * r + 9 + c
//
// A row runs along world x, a column along world y, one cell (4.1666665 yards) apart:
//
//     outer:  x = 17066.666 - chunk_y * 33.333332 - r * 4.1666665
//             y = 17066.666 - chunk_x * 33.333332 - c * 4.1666665
//     inner:  the same, less half a cell (2.0833333) on both
//
// The outer vertices of a chunk's edge are also the edge of its neighbour: chunk (x, y)'s outer
// column 8 is the same ground as chunk (x + 1, y)'s column 0, its row 8 as chunk (x, y + 1)'s
// row 0. A cell (row r, column c, 0-7) is the square between four outer vertices, drawn as four
// triangles around its inner vertex: towards outer (r, c)-(r, c + 1), (r, c + 1)-(r + 1, c + 1),
// (r + 1, c + 1)-(r + 1, c) and (r + 1, c)-(r, c). The ground's height at a point is the height
// of the triangle under it, as the client's own height query and collision find it.
//
// Heights are absolute world z. A chunk's holes are a 16-bit mask over its 4 x 4 squares of
// 2 x 2 cells: cell (r, c) is a hole when bit 4 * (r / 2) + (c / 2) is set.

#ifndef WXL_TERRAIN_GRID_HPP
#define WXL_TERRAIN_GRID_HPP

#include <cmath>
#include <cstdint>

namespace wxl::terrain
{
    /// Half the map's side: where chunk 0 starts, on both axes.
    constexpr float kOrigin = 17066.666f;
    /// A chunk's side, in yards.
    constexpr float kChunkSize = 33.333332f;
    /// A cell's side: the distance between two outer vertices.
    constexpr float kCellSize = 4.1666665f;
    /// Half a cell: how far an inner vertex sits from the outer one before it.
    constexpr float kHalfCell = 2.0833333f;
    /// Chunks on a side of the map.
    constexpr int32_t kChunksPerSide = 1024;
    /// Cells on a side of a chunk.
    constexpr int32_t kCellsPerChunk = 8;
    /// Vertices of a chunk: 81 outer and 64 inner.
    constexpr uint32_t kVertexCount = 145;

    /// A point of the plane, in world yards.
    struct Point2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    /// A point or a direction in world yards.
    struct Point3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    /// A vertex of a chunk, as its row and column among its kind.
    struct VertexPlace
    {
        int32_t row   = 0;
        int32_t col   = 0;
        bool    inner = false;
    };

    /// Whether a chunk column or row is on the map.
    constexpr bool ChunkOnMap(int32_t chunk_x, int32_t chunk_y)
    {
        return chunk_x >= 0 && chunk_x < kChunksPerSide && chunk_y >= 0 && chunk_y < kChunksPerSide;
    }

    /// The index of outer vertex (row, col), both 0-8.
    constexpr uint32_t OuterIndex(int32_t row, int32_t col) { return static_cast<uint32_t>(17 * row + col); }

    /// The index of inner vertex (row, col), both 0-7.
    constexpr uint32_t InnerIndex(int32_t row, int32_t col) { return static_cast<uint32_t>(17 * row + 9 + col); }

    /// Whether an index (0-144) is an inner vertex.
    constexpr bool IsInner(uint32_t index) { return index % 17 >= 9; }

    /// The row and column of a vertex index (0-144).
    constexpr VertexPlace PlaceOf(uint32_t index)
    {
        const int32_t row = static_cast<int32_t>(index / 17);
        const int32_t col = static_cast<int32_t>(index % 17);
        return col >= 9 ? VertexPlace{row, col - 9, true} : VertexPlace{row, col, false};
    }

    /// The north-west corner of a chunk: its outer vertex (0, 0), in the plane.
    constexpr Point2 ChunkCorner(int32_t chunk_x, int32_t chunk_y)
    {
        return Point2{kOrigin - static_cast<float>(chunk_y) * kChunkSize,
                      kOrigin - static_cast<float>(chunk_x) * kChunkSize};
    }

    /// Where a vertex of a chunk is in the plane: the corner and its offset, computed as the client
    /// computes it.
    constexpr Point2 VertexPlane(int32_t chunk_x, int32_t chunk_y, uint32_t index)
    {
        const Point2      corner = ChunkCorner(chunk_x, chunk_y);
        const VertexPlace place  = PlaceOf(index);
        const float       half   = place.inner ? kHalfCell : 0.0f;
        return Point2{corner.x - static_cast<float>(place.row) * kCellSize - half,
                      corner.y - static_cast<float>(place.col) * kCellSize - half};
    }

    /// Where a vertex is from its chunk's corner, in yards: exact small numbers, which the
    /// arithmetic of a triangle is done in, as the client does it.
    constexpr Point2 VertexLocal(uint32_t index)
    {
        const VertexPlace place = PlaceOf(index);
        const float       half  = place.inner ? kHalfCell : 0.0f;
        return Point2{-static_cast<float>(place.row) * kCellSize - half, -static_cast<float>(place.col) * kCellSize - half};
    }

    /// Where a point is on the grid.
    struct Locus
    {
        int32_t  chunk_x = 0;
        int32_t  chunk_y = 0;
        int32_t  cell_row = 0;  ///< the cell's row in the chunk, 0-7 (along world x)
        int32_t  cell_col = 0;  ///< the cell's column, 0-7 (along world y)
        float    along_row = 0; ///< how far into the cell along its row, 0 to 1
        float    along_col = 0; ///< how far into the cell along its column, 0 to 1
        uint32_t nearest   = 0; ///< the chunk's vertex nearest to the point
        bool     on_map    = false;
    };

    /// The chunk, cell and nearest vertex of a point of the plane.
    inline Locus Locate(float x, float y)
    {
        Locus      at;
        const double u = (static_cast<double>(kOrigin) - x) / kCellSize; // global cell rows
        const double v = (static_cast<double>(kOrigin) - y) / kCellSize; // global cell columns
        const double ru = std::floor(u), rv = std::floor(v);
        int64_t     gr = static_cast<int64_t>(ru), gc = static_cast<int64_t>(rv);
        const int64_t last = static_cast<int64_t>(kChunksPerSide) * kCellsPerChunk;
        at.on_map = gr >= 0 && gc >= 0 && gr <= last && gc <= last && !(gr == last && u > ru) && !(gc == last && v > rv);
        if (!at.on_map) return at;
        // A point on the map's last line belongs to the last cell.
        if (gr == last) gr = last - 1;
        if (gc == last) gc = last - 1;
        at.chunk_y   = static_cast<int32_t>(gr / kCellsPerChunk);
        at.chunk_x   = static_cast<int32_t>(gc / kCellsPerChunk);
        at.cell_row  = static_cast<int32_t>(gr % kCellsPerChunk);
        at.cell_col  = static_cast<int32_t>(gc % kCellsPerChunk);
        at.along_row = static_cast<float>(u - static_cast<double>(gr));
        at.along_col = static_cast<float>(v - static_cast<double>(gc));
        // The nearest of the cell's five vertices: its centre when within half a cell's
        // diagonal box of it, else the nearest corner.
        const float dr = at.along_row - 0.5f, dc = at.along_col - 0.5f;
        if (std::fabs(dr) + std::fabs(dc) <= 0.5f)
            at.nearest = InnerIndex(at.cell_row, at.cell_col);
        else
            at.nearest = OuterIndex(at.cell_row + (at.along_row >= 0.5f ? 1 : 0),
                                    at.cell_col + (at.along_col >= 0.5f ? 1 : 0));
        return at;
    }

    /// Whether cell (row, col) of a chunk with these holes is a hole.
    constexpr bool CellIsHole(uint16_t holes, int32_t row, int32_t col)
    {
        return (holes >> (4 * (row >> 1) + (col >> 1))) & 1u;
    }

    /// The three vertex indices of triangle `tri` (0-3) of cell (row, col): the inner vertex,
    /// then two outer corners, going round the cell: 0 the row's start side, 1 the column's end,
    /// 2 the row's end, 3 the column's start.
    constexpr void CellTriangle(int32_t row, int32_t col, int32_t tri, uint32_t out[3])
    {
        const uint32_t a = OuterIndex(row, col), b = OuterIndex(row, col + 1);
        const uint32_t c = OuterIndex(row + 1, col + 1), d = OuterIndex(row + 1, col);
        out[0] = InnerIndex(row, col);
        switch (tri & 3)
        {
            case 0: out[1] = a; out[2] = b; break;
            case 1: out[1] = b; out[2] = c; break;
            case 2: out[1] = c; out[2] = d; break;
            default: out[1] = d; out[2] = a; break;
        }
    }

    /// Which of a cell's four triangles a point lies over, from its place in the cell (0 to 1 on
    /// both): the triangle of the nearest side.
    constexpr int32_t TriangleAt(float along_row, float along_col)
    {
        // Above the main diagonal is the row's start side or the column's end; below, the others.
        const bool before_main = along_row <= along_col;        // row start or column end
        const bool before_anti = along_row + along_col <= 1.0f; // row start or column start
        if (before_main) return before_anti ? 0 : 1;
        return before_anti ? 3 : 2;
    }

    inline Point3 Sub(Point3 a, Point3 b) { return Point3{a.x - b.x, a.y - b.y, a.z - b.z}; }
    inline Point3 Cross(Point3 a, Point3 b)
    {
        return Point3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    inline float Dot(Point3 a, Point3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    /// The normal of a triangle, facing up, not unit: its length is twice the triangle's area.
    inline Point3 UpNormal(Point3 a, Point3 b, Point3 c)
    {
        Point3 n = Cross(Sub(b, a), Sub(c, a));
        if (n.z < 0.0f) n = Point3{-n.x, -n.y, -n.z};
        return n;
    }

    /// A direction made unit; straight up for a null one.
    inline Point3 Unit(Point3 n)
    {
        const float length = std::sqrt(Dot(n, n));
        if (!(length > 1e-12f)) return Point3{0.0f, 0.0f, 1.0f};
        return Point3{n.x / length, n.y / length, n.z / length};
    }

    /// The world position of vertex `index` of a chunk given its 145 absolute heights.
    inline Point3 VertexWorld(int32_t chunk_x, int32_t chunk_y, uint32_t index, const float* heights)
    {
        const Point2 p = VertexPlane(chunk_x, chunk_y, index);
        return Point3{p.x, p.y, heights[index]};
    }

    /// The height of a chunk's ground at a point of one of its cells, on the triangle under it:
    /// what the client's height query answers. `heights` are the chunk's 145 absolute heights.
    inline float HeightInCell(int32_t chunk_x, int32_t chunk_y, const float* heights, int32_t row, int32_t col,
                              float x, float y)
    {
        const Point2 corner = ChunkCorner(chunk_x, chunk_y);
        const float  lx = x - corner.x, ly = y - corner.y;
        const float  ar = -lx / kCellSize - static_cast<float>(row);
        const float  ac = -ly / kCellSize - static_cast<float>(col);
        uint32_t     tri[3];
        CellTriangle(row, col, TriangleAt(ar, ac), tri);
        Point3 v[3];
        for (int k = 0; k < 3; ++k)
        {
            const Point2 p = VertexLocal(tri[k]);
            v[k]           = Point3{p.x, p.y, heights[tri[k]]};
        }
        const Point3 n = UpNormal(v[0], v[1], v[2]);
        if (!(n.z > 0.0f)) return v[0].z;
        // On the plane n . (p - a) = 0.
        return v[0].z - (n.x * (lx - v[0].x) + n.y * (ly - v[0].y)) / n.z;
    }

    /// The unit normal of a cell's triangle `tri` (0-3).
    inline Point3 TriangleNormal(const float* heights, int32_t row, int32_t col, int32_t tri)
    {
        uint32_t idx[3];
        CellTriangle(row, col, tri, idx);
        Point3 v[3];
        for (int k = 0; k < 3; ++k)
        {
            const Point2 p = VertexLocal(idx[k]);
            v[k]           = Point3{p.x, p.y, heights[idx[k]]};
        }
        return Unit(UpNormal(v[0], v[1], v[2]));
    }

    /// The unit normal of the triangle of a cell under a point.
    inline Point3 NormalInCell(int32_t chunk_x, int32_t chunk_y, const float* heights, int32_t row, int32_t col,
                               float x, float y)
    {
        const Point2 corner = ChunkCorner(chunk_x, chunk_y);
        const float  ar     = (corner.x - x) / kCellSize - static_cast<float>(row);
        const float  ac     = (corner.y - y) / kCellSize - static_cast<float>(col);
        return TriangleNormal(heights, row, col, TriangleAt(ar, ac));
    }

    /// Where a segment crosses a triangle, both sides counted: the fraction of the way from
    /// `start` (0 to 1), or a negative number when it does not.
    inline float SegmentTriangle(Point3 start, Point3 delta, Point3 a, Point3 b, Point3 c)
    {
        const Point3 e1 = Sub(b, a), e2 = Sub(c, a);
        const Point3 p  = Cross(delta, e2);
        const float  det = Dot(e1, p);
        if (std::fabs(det) < 1e-12f) return -1.0f;
        const float  inv = 1.0f / det;
        const Point3 s   = Sub(start, a);
        const float  u   = Dot(s, p) * inv;
        if (u < -1e-6f || u > 1.0f + 1e-6f) return -1.0f;
        const Point3 q = Cross(s, e1);
        const float  v = Dot(delta, q) * inv;
        if (v < -1e-6f || u + v > 1.0f + 1e-6f) return -1.0f;
        const float t = Dot(e2, q) * inv;
        return (t >= 0.0f && t <= 1.0f) ? t : -1.0f;
    }

    /// Every chunk holding global outer vertex (R, C), the map's outer vertices counted from its
    /// north-west corner (0 to 8192 on both): one, two on a chunk's edge, four on a corner. Calls
    /// `visit(chunk_x, chunk_y, index)` for each.
    template <class Visit>
    void ForOuterCopies(int32_t R, int32_t C, Visit&& visit)
    {
        const int32_t last = kChunksPerSide * kCellsPerChunk;
        if (R < 0 || C < 0 || R > last || C > last) return;
        for (int32_t dy = 0; dy <= 1; ++dy)
            for (int32_t dx = 0; dx <= 1; ++dx)
            {
                const int32_t cy = R / kCellsPerChunk - dy, cx = C / kCellsPerChunk - dx;
                const int32_t lr = R - cy * kCellsPerChunk, lc = C - cx * kCellsPerChunk;
                if (cx < 0 || cy < 0 || cx >= kChunksPerSide || cy >= kChunksPerSide) continue;
                if (lr < 0 || lr > kCellsPerChunk || lc < 0 || lc > kCellsPerChunk) continue;
                visit(cx, cy, OuterIndex(lr, lc));
            }
    }

    /// Every copy of a vertex: itself first, then, for an outer vertex on its chunk's edge, the
    /// same ground in the neighbouring chunks. Calls `visit(chunk_x, chunk_y, index)` for each.
    template <class Visit>
    void ForCopies(int32_t chunk_x, int32_t chunk_y, uint32_t index, Visit&& visit)
    {
        visit(chunk_x, chunk_y, index);
        const VertexPlace place = PlaceOf(index);
        if (place.inner) return;
        ForOuterCopies(chunk_y * kCellsPerChunk + place.row, chunk_x * kCellsPerChunk + place.col,
                       [&](int32_t cx, int32_t cy, uint32_t i) {
                           if (cx != chunk_x || cy != chunk_y) visit(cx, cy, i);
                       });
    }

    /// The chunk an outer vertex of the map's global grid (R, C) is reported in when it is named
    /// once: the chunk whose cell it starts (row and column below 8), or the last one on the map's
    /// far edges.
    inline void OuterHome(int32_t R, int32_t C, int32_t& chunk_x, int32_t& chunk_y, uint32_t& index)
    {
        const int32_t last = kChunksPerSide - 1;
        chunk_y = R / kCellsPerChunk > last ? last : R / kCellsPerChunk;
        chunk_x = C / kCellsPerChunk > last ? last : C / kCellsPerChunk;
        index   = OuterIndex(R - chunk_y * kCellsPerChunk, C - chunk_x * kCellsPerChunk);
    }

    /// Every vertex of the map within `radius` yards of (x, y) in the plane, each once: an outer
    /// vertex on a chunk's edge in its home chunk (OuterHome). Calls
    /// `visit(chunk_x, chunk_y, index, Point2 place)`, by global row, then column, outer before
    /// inner.
    template <class Visit>
    void ForVerticesInCircle(float x, float y, float radius, Visit&& visit)
    {
        if (!(radius >= 0.0f)) return;
        const int64_t last = static_cast<int64_t>(kChunksPerSide) * kCellsPerChunk;
        const double  u    = (static_cast<double>(kOrigin) - x) / kCellSize;
        const double  v    = (static_cast<double>(kOrigin) - y) / kCellSize;
        const double  span = static_cast<double>(radius) / kCellSize + 1.0;
        int64_t       r0 = static_cast<int64_t>(std::floor(u - span)), r1 = static_cast<int64_t>(std::ceil(u + span));
        int64_t       c0 = static_cast<int64_t>(std::floor(v - span)), c1 = static_cast<int64_t>(std::ceil(v + span));
        if (r0 < 0) r0 = 0;
        if (c0 < 0) c0 = 0;
        if (r1 > last) r1 = last;
        if (c1 > last) c1 = last;
        const double rr = static_cast<double>(radius) * radius;
        for (int64_t R = r0; R <= r1; ++R)
        {
            for (int pass = 0; pass < 2; ++pass)
            {
                const bool inner = pass == 1;
                if (inner && R >= last) break;
                for (int64_t C = c0; C <= c1; ++C)
                {
                    if (inner && C >= last) break;
                    const double half = inner ? kHalfCell : 0.0;
                    const double px   = kOrigin - static_cast<double>(R) * kCellSize - half;
                    const double py   = kOrigin - static_cast<double>(C) * kCellSize - half;
                    const double dx = px - x, dy = py - y;
                    if (dx * dx + dy * dy > rr) continue;
                    int32_t  cx = 0, cy = 0;
                    uint32_t index = 0;
                    if (inner)
                    {
                        cy    = static_cast<int32_t>(R / kCellsPerChunk);
                        cx    = static_cast<int32_t>(C / kCellsPerChunk);
                        index = InnerIndex(static_cast<int32_t>(R % kCellsPerChunk), static_cast<int32_t>(C % kCellsPerChunk));
                    }
                    else
                        OuterHome(static_cast<int32_t>(R), static_cast<int32_t>(C), cx, cy, index);
                    visit(cx, cy, index, VertexPlane(cx, cy, index));
                }
            }
        }
    }

    /// The vertices whose normal depends on the height of vertex `index` of chunk
    /// (chunk_x, chunk_y), as (chunk_x, chunk_y, index) triples, the vertex itself first, the
    /// copies of an edge vertex in the neighbouring chunks included. Calls `visit` for each; a
    /// vertex may be visited more than once.
    template <class Visit>
    void ForNormalNeighbours(int32_t chunk_x, int32_t chunk_y, uint32_t index, Visit&& visit)
    {
        // In the map's global grid: outer vertex (R, C), inner vertex (R, C) of cell (R, C).
        const VertexPlace place = PlaceOf(index);
        const int32_t     R     = chunk_y * kCellsPerChunk + place.row;
        const int32_t     C     = chunk_x * kCellsPerChunk + place.col;
        const int32_t     last  = kChunksPerSide * kCellsPerChunk;
        auto outer = [&](int32_t r, int32_t c) { ForOuterCopies(r, c, visit); };
        auto inner = [&](int32_t r, int32_t c) {
            if (r < 0 || c < 0 || r >= last || c >= last) return;
            visit(c / kCellsPerChunk, r / kCellsPerChunk, InnerIndex(r % kCellsPerChunk, c % kCellsPerChunk));
        };
        if (place.inner)
        {
            inner(R, C);
            outer(R, C);
            outer(R, C + 1);
            outer(R + 1, C);
            outer(R + 1, C + 1);
            return;
        }
        outer(R, C);
        // The four outer neighbours along the rows and columns share a triangle with it, as do
        // the inner vertices of the four cells around it.
        outer(R - 1, C);
        outer(R + 1, C);
        outer(R, C - 1);
        outer(R, C + 1);
        inner(R - 1, C - 1);
        inner(R - 1, C);
        inner(R, C - 1);
        inner(R, C);
    }

    /// The normal of a vertex: the sum of the up-facing normals of the triangles around it,
    /// each weighted by its area, made unit. `height(cx, cy, index, z)` answers a vertex's
    /// absolute height and false when its chunk is not there; a triangle with a vertex missing is
    /// left out, so a vertex at the edge of what is loaded leans on its own side.
    template <class Height>
    Point3 VertexNormal(int32_t chunk_x, int32_t chunk_y, uint32_t index, Height&& height)
    {
        const VertexPlace place = PlaceOf(index);
        const int32_t     R     = chunk_y * kCellsPerChunk + place.row;
        const int32_t     C     = chunk_x * kCellsPerChunk + place.col;
        const int32_t     last  = kChunksPerSide * kCellsPerChunk;
        Point3            sum{};

        // A triangle of global cell (r, c): its inner vertex and two outer corners, given as
        // global outer coordinates.
        auto corner = [&](int32_t r, int32_t c, Point3& out) -> bool {
            // Any loaded chunk holding it: an edge vertex is the same ground in each.
            bool  found = false;
            float z     = 0.0f;
            ForOuterCopies(r, c, [&](int32_t cx, int32_t cy, uint32_t i) {
                if (!found) found = height(cx, cy, i, z);
            });
            if (!found) return false;
            // From the vertex whose normal this is, in whole cells: exact.
            out = Point3{-static_cast<float>(r - R) * kCellSize - (place.inner ? -kHalfCell : 0.0f),
                         -static_cast<float>(c - C) * kCellSize - (place.inner ? -kHalfCell : 0.0f), z};
            return true;
        };
        auto centre = [&](int32_t r, int32_t c, Point3& out) -> bool {
            if (r < 0 || c < 0 || r >= last || c >= last) return false;
            const int32_t  cy = r / kCellsPerChunk, cx = c / kCellsPerChunk;
            const uint32_t i  = InnerIndex(r % kCellsPerChunk, c % kCellsPerChunk);
            float          z  = 0.0f;
            if (!height(cx, cy, i, z)) return false;
            out = Point3{-static_cast<float>(r - R) * kCellSize - kHalfCell - (place.inner ? -kHalfCell : 0.0f),
                         -static_cast<float>(c - C) * kCellSize - kHalfCell - (place.inner ? -kHalfCell : 0.0f), z};
            return true;
        };
        // The four triangles of cell (r, c), as corner pairs in global outer coordinates.
        const int32_t sides[4][4] = {{0, 0, 0, 1}, {0, 1, 1, 1}, {1, 1, 1, 0}, {1, 0, 0, 0}};
        auto add = [&](int32_t r, int32_t c, int32_t side) {
            Point3 m, a, b;
            if (!centre(r, c, m)) return;
            if (!corner(r + sides[side][0], c + sides[side][1], a)) return;
            if (!corner(r + sides[side][2], c + sides[side][3], b)) return;
            const Point3 n = UpNormal(m, a, b);
            sum            = Point3{sum.x + n.x, sum.y + n.y, sum.z + n.z};
        };
        if (place.inner)
        {
            for (int32_t side = 0; side < 4; ++side) add(R, C, side);
            return Unit(sum);
        }
        // The cells around an outer vertex and the two triangles of each that touch it.
        add(R - 1, C - 1, 1); add(R - 1, C - 1, 2); // it is that cell's (r + 1, c + 1)
        add(R - 1, C, 2);     add(R - 1, C, 3);     // its (r + 1, c)
        add(R, C - 1, 0);     add(R, C - 1, 1);     // its (r, c + 1)
        add(R, C, 3);         add(R, C, 0);         // its (r, c)
        return Unit(sum);
    }

    /// A unit normal as the client stores it: three signed bytes, x, y and z in 127ths.
    inline void PackNormal(Point3 n, int8_t out[3])
    {
        const float v[3] = {n.x, n.y, n.z};
        for (int i = 0; i < 3; ++i)
        {
            float s = std::round(v[i] * 127.0f);
            if (s > 127.0f) s = 127.0f;
            if (s < -127.0f) s = -127.0f;
            out[i] = static_cast<int8_t>(s);
        }
    }

    /// One step of a walk along a segment's cells: the global cell (row, column) the segment
    /// crosses, in order, from `start` to `finish` in the plane. `visit(row, col)` returns false
    /// to stop. At most `limit` cells are visited.
    template <class Visit>
    void WalkCells(Point2 start, Point2 finish, uint32_t limit, Visit&& visit)
    {
        const double u0 = (static_cast<double>(kOrigin) - start.x) / kCellSize;
        const double v0 = (static_cast<double>(kOrigin) - start.y) / kCellSize;
        const double u1 = (static_cast<double>(kOrigin) - finish.x) / kCellSize;
        const double v1 = (static_cast<double>(kOrigin) - finish.y) / kCellSize;
        int64_t      r  = static_cast<int64_t>(std::floor(u0)), c = static_cast<int64_t>(std::floor(v0));
        const int64_t r1 = static_cast<int64_t>(std::floor(u1)), c1 = static_cast<int64_t>(std::floor(v1));
        const double du = u1 - u0, dv = v1 - v0;
        const int    sr = du > 0 ? 1 : (du < 0 ? -1 : 0), sc = dv > 0 ? 1 : (dv < 0 ? -1 : 0);
        const double inf = 1e300;
        const double tdr = sr ? std::fabs(1.0 / du) : inf, tdc = sc ? std::fabs(1.0 / dv) : inf;
        double       tr  = sr ? ((sr > 0 ? (std::floor(u0) + 1.0 - u0) : (u0 - std::floor(u0))) * tdr) : inf;
        double       tc  = sc ? ((sc > 0 ? (std::floor(v0) + 1.0 - v0) : (v0 - std::floor(v0))) * tdc) : inf;
        for (uint32_t n = 0; n < limit; ++n)
        {
            if (!visit(r, c)) return;
            if (r == r1 && c == c1) return;
            if (tr < tc)
            {
                if (tr > 1.0) return;
                r += sr;
                tr += tdr;
            }
            else
            {
                if (tc > 1.0) return;
                c += sc;
                tc += tdc;
            }
        }
    }
} // namespace wxl::terrain

#endif // WXL_TERRAIN_GRID_HPP
