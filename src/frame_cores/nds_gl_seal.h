// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "GPU3D.h"

namespace FrameCores {

/**
 * The gaps between polygons that meet without sharing their vertices, over 1x.
 *
 * Where a vertex lies on another polygon's edge, or two edges lie on each other with vertices that
 * the DS put a part of a pixel apart, the DS's own rasteriser leaves nothing open: it fills whole
 * pixels from edge to edge. Over 1x the vertices are put on a finer grid, and what is behind shows
 * through in a dotted line. Each such gap is filled with triangles of the polygon whose edge it is,
 * between the edge and the vertices on it, which lie a little behind it: nothing that is drawn
 * without them is another than before, only what is farther away shows less.
 *
 * Opaque polygons alone: what is blended would show the triangles.
 */
class NdsGlSeal {
public:
    /// The triangles are put after the polygons of the frame. Nothing else of it is written.
    /// `scale` is the renderer's.
    void Add(melonDS::GPU3D& scene, int scale);
    /// The frame as it was.
    void Remove(melonDS::GPU3D& scene);

    std::size_t Triangles() const {
        return polygon_count;
    }

private:
    static constexpr std::size_t kPolygons = 2048; ///< of a frame, melonDS's renderer's
    static constexpr std::size_t kTriangles = 1024;
    static constexpr std::size_t kVertices = 2048 * 5; ///< of a frame, melonDS's renderer's

    struct Point {
        melonDS::s32 x, y, z, w;
        std::int32_t next; ///< in its cell
    };
    struct Edge {
        std::uint64_t key;
        std::int32_t next; ///< in its bucket
        std::uint16_t polygon;
        std::uint8_t from, to;
        bool shut; ///< another polygon has it too
    };
    struct End {
        std::uint64_t place;
        std::int32_t next; ///< in its bucket
    };
    struct Hit {
        float at;
        const Point* point;
    };

    void FindEdges(const melonDS::GPU3D& scene);
    void FindPoints(const melonDS::GPU3D& scene);
    void Make(const melonDS::GPU3D& scene);
    void Fill(const melonDS::Polygon& owner, const Edge& edge);
    /// False: there is no more room.
    bool Triangle(const melonDS::Polygon& owner, const std::array<melonDS::Vertex, 3>& corners,
                  const std::array<melonDS::s32, 3>& z, const std::array<melonDS::s32, 3>& w);

    std::vector<Point> points;
    std::vector<std::int32_t> cells;
    std::vector<Edge> edges;
    std::vector<std::int32_t> buckets;
    std::vector<End> ends;
    std::vector<Hit> hits;
    std::vector<melonDS::Polygon> polygons;
    std::vector<melonDS::Vertex> vertices;
    std::size_t polygon_count = 0;
    std::array<melonDS::Polygon*, kTriangles> replaced{}; ///< what was after the game's polygons
    melonDS::u32 game_count = 0;
    std::size_t room = 0; ///< for triangles
    int made_for = 0;     ///< scale
    int looks = 0;        ///< at points, left of the frame's
};

} // namespace FrameCores
