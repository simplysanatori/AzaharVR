// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/nds_gl_seal.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace FrameCores {
namespace {

using melonDS::s32;
using melonDS::u32;

constexpr int kCell = 128; // of 16ths of a pixel, HiresPosition's
constexpr int kColumns = 8192 / kCell;
constexpr int kRows = 4096 / kCell;
constexpr float kNear = 2.0f;    // how far apart the DS can put what is in one place
constexpr int kReach = 4;        // more than that, and than a step along an edge is off
constexpr float kShort = 12.0f;  // what is shorter has no gap to see
constexpr float kThin = 8.0f;    // what is thinner is no surface
constexpr float kPartW = 512.0f; // of w, and what of z: one surface
constexpr float kSlackZ = 1024.0f;
constexpr int kLooks = 1 << 18; // at points, in a frame: about a millisecond
constexpr std::size_t kBuckets = 4096;
constexpr s32 kBehind = 0x400; // of z; a w has no such scale

bool IsSealed(const melonDS::Polygon& polygon) {
    return !polygon.Degenerate && !polygon.Translucent && !polygon.IsShadowMask &&
           !polygon.IsShadow && polygon.Type == 0 && polygon.NumVertices >= 3 &&
           polygon.NumVertices <= 10 && (polygon.Attr & 0x001F0000) == 0x001F0000;
}

bool IsInside(const melonDS::Vertex& vertex) {
    return vertex.HiresPosition[0] >= 0 && vertex.HiresPosition[0] < kColumns * kCell &&
           vertex.HiresPosition[1] >= 0 && vertex.HiresPosition[1] < kRows * kCell;
}

std::uint64_t Place(const melonDS::Vertex& vertex) {
    return std::uint64_t(vertex.HiresPosition[0] & 0x1FFF) |
           std::uint64_t(vertex.HiresPosition[1] & 0xFFF) << 13 |
           std::uint64_t(static_cast<u32>(vertex.Position[3])) << 25;
}

/// The same whichever way it goes.
std::uint64_t Key(const melonDS::Vertex& a, const melonDS::Vertex& b) {
    std::uint64_t p = Place(a);
    std::uint64_t q = Place(b);
    if (p > q) {
        std::swap(p, q);
    }
    return p * 0x9E3779B97F4A7C15ull ^ (q * 0xC2B2AE3D27D4EB4Full + (q >> 29));
}

} // namespace

/// The edges that one polygon alone has. One that two have is shut.
void NdsGlSeal::FindEdges(const melonDS::GPU3D& scene) {
    edges.clear();
    buckets.assign(kBuckets, -1);
    for (u32 i = 0; i < game_count; ++i) {
        const melonDS::Polygon* polygon = scene.RenderPolygonRAM[i];
        if (!polygon || !IsSealed(*polygon)) {
            continue;
        }
        const u32 count = polygon->NumVertices;
        for (u32 v = 0; v < count; ++v) {
            const u32 to = v + 1 == count ? 0 : v + 1;
            const melonDS::Vertex& a = *polygon->Vertices[v];
            const melonDS::Vertex& b = *polygon->Vertices[to];
            if (!IsInside(a) || !IsInside(b) ||
                (a.HiresPosition[0] == b.HiresPosition[0] &&
                 a.HiresPosition[1] == b.HiresPosition[1])) {
                continue;
            }
            const std::uint64_t key = Key(a, b);
            std::int32_t& bucket = buckets[key >> 40 & (kBuckets - 1)];
            std::int32_t e = bucket;
            while (e >= 0 && edges[e].key != key) {
                e = edges[e].next;
            }
            if (e >= 0) {
                edges[e].shut = true;
            } else {
                e = std::exchange(bucket, static_cast<std::int32_t>(edges.size()));
                edges.push_back({key, e, static_cast<std::uint16_t>(i),
                                 static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(to),
                                 false});
            }
        }
    }
}

/// The ends of those edges: where all edges are shut, nothing is open.
void NdsGlSeal::FindPoints(const melonDS::GPU3D& scene) {
    points.clear();
    cells.assign(kColumns * kRows, -1);
    buckets.assign(kBuckets, -1);
    ends.clear();
    for (const Edge& edge : edges) {
        if (edge.shut) {
            continue;
        }
        const melonDS::Polygon& polygon = *scene.RenderPolygonRAM[edge.polygon];
        for (const u32 v : {edge.from, edge.to}) {
            const melonDS::Vertex& vertex = *polygon.Vertices[v];
            const std::uint64_t place = Place(vertex);
            const std::uint64_t key = place * 0x9E3779B97F4A7C15ull;
            std::int32_t& bucket = buckets[key >> 40 & (kBuckets - 1)];
            std::int32_t e = bucket;
            while (e >= 0 && ends[e].place != place) {
                e = ends[e].next;
            }
            if (e >= 0) {
                continue;
            }
            e = std::exchange(bucket, static_cast<std::int32_t>(ends.size()));
            ends.push_back({place, e});
            // In every cell that an edge it is on can go through
            const s32 x = vertex.HiresPosition[0];
            const s32 y = vertex.HiresPosition[1];
            const int right = std::min((x + kReach) / kCell, kColumns - 1);
            const int low = std::min((y + kReach) / kCell, kRows - 1);
            for (int row = std::max(0, y - kReach) / kCell; row <= low; ++row) {
                for (int column = std::max(0, x - kReach) / kCell; column <= right; ++column) {
                    std::int32_t& cell = cells[row * kColumns + column];
                    const std::int32_t next =
                        std::exchange(cell, static_cast<std::int32_t>(points.size()));
                    points.push_back({x, y, polygon.FinalZ[v], vertex.Position[3], next});
                }
            }
        }
    }
}

bool NdsGlSeal::Triangle(const melonDS::Polygon& owner,
                         const std::array<melonDS::Vertex, 3>& corners, const std::array<s32, 3>& z,
                         const std::array<s32, 3>& w) {
    // Nothing is between corners in a line, which is where most are
    const auto at = [&](std::size_t i, int axis) {
        return (corners[i].HiresPosition[axis] * made_for) >> 4;
    };
    if ((at(1, 0) - at(0, 0)) * (at(2, 1) - at(0, 1)) ==
        (at(2, 0) - at(0, 0)) * (at(1, 1) - at(0, 1))) {
        return true;
    }
    if (polygon_count == room) {
        return false;
    }
    melonDS::Polygon& polygon = polygons[polygon_count];
    polygon = owner;
    polygon.NumVertices = 3;
    for (std::size_t i = 0; i < 3; ++i) {
        melonDS::Vertex& vertex = vertices[polygon_count * 3 + i];
        vertex = corners[i];
        polygon.Vertices[i] = &vertex;
        const s32 behind = owner.WBuffer ? (z[i] >> 9) + 2 : kBehind;
        polygon.FinalZ[i] = std::min<s32>(z[i] + behind, 0xFFFFFF);
        polygon.FinalW[i] = w[i];
    }
    ++polygon_count;
    return true;
}

void NdsGlSeal::Fill(const melonDS::Polygon& owner, const Edge& edge) {
    const melonDS::Vertex& a = *owner.Vertices[edge.from];
    const melonDS::Vertex& b = *owner.Vertices[edge.to];
    const float ax = static_cast<float>(a.HiresPosition[0]);
    const float ay = static_cast<float>(a.HiresPosition[1]);
    const float dx = static_cast<float>(b.HiresPosition[0]) - ax;
    const float dy = static_cast<float>(b.HiresPosition[1]) - ay;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < kShort) {
        return;
    }
    // One seen from its side has its own corners on its edge: that is no gap
    float width = 0.0f;
    for (u32 v = 0; v < owner.NumVertices; ++v) {
        const float px = static_cast<float>(owner.Vertices[v]->HiresPosition[0]) - ax;
        const float py = static_cast<float>(owner.Vertices[v]->HiresPosition[1]) - ay;
        width = std::max(width, std::abs(px * dy - py * dx) / length);
    }
    if (width < kThin) {
        return;
    }
    const s32 za = owner.FinalZ[edge.from];
    const s32 zb = owner.FinalZ[edge.to];
    const float wa = static_cast<float>(std::max<s32>(1, a.Position[3]));
    const float wb = static_cast<float>(std::max<s32>(1, b.Position[3]));

    hits.clear();
    const auto look = [&](int column, int row) {
        for (std::int32_t p = cells[row * kColumns + column]; p >= 0 && looks > 0;
             p = points[p].next) {
            --looks;
            const Point& point = points[p];
            const float px = static_cast<float>(point.x) - ax;
            const float py = static_cast<float>(point.y) - ay;
            const float off = (px * dy - py * dx) / length;
            const float along = (px * dx + py * dy) / length;
            if (std::abs(off) > kNear || along < -kNear || along > length + kNear) {
                continue;
            }
            if ((point.x == a.HiresPosition[0] && point.y == a.HiresPosition[1]) ||
                (point.x == b.HiresPosition[0] && point.y == b.HiresPosition[1])) {
                continue;
            }
            const float at = std::clamp(along / length, 0.0f, 1.0f);
            const float w = 1.0f / ((1.0f - at) / wa + at / wb);
            if (std::abs(w - static_cast<float>(point.w)) > w / kPartW + 2.0f) {
                continue;
            }
            const float z = static_cast<float>(za) + at * static_cast<float>(zb - za);
            if (!owner.WBuffer && std::abs(z - static_cast<float>(point.z)) >
                                      kSlackZ + std::abs(static_cast<float>(zb - za)) / 64.0f) {
                continue;
            }
            hits.push_back({along / length, &point});
        }
    };
    // The cells it goes through
    int column = a.HiresPosition[0] / kCell;
    int row = a.HiresPosition[1] / kCell;
    const int last_column = b.HiresPosition[0] / kCell;
    const int last_row = b.HiresPosition[1] / kCell;
    const int right = dx < 0 ? -1 : 1;
    const int down = dy < 0 ? -1 : 1;
    const auto border = [](int cell, int step) {
        return static_cast<float>((cell + (step > 0 ? 1 : 0)) * kCell);
    };
    look(column, row);
    while (column != last_column || row != last_row) {
        // How far along the next column and the next row are
        const float to_x = column == last_column ? 2.0f : (border(column, right) - ax) / dx;
        const float to_y = row == last_row ? 2.0f : (border(row, down) - ay) / dy;
        if (to_x < to_y) {
            column += right;
        } else {
            row += down;
        }
        look(column, row);
    }
    if (hits.empty()) {
        return;
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& l, const Hit& r) {
        return l.at != r.at ? l.at < r.at : l.point < r.point;
    });
    hits.erase(std::unique(hits.begin(), hits.end(),
                           [](const Hit& l, const Hit& r) {
                               return l.point->x == r.point->x && l.point->y == r.point->y;
                           }),
               hits.end());

    // Half a texel into the polygon at most: its edge is its texture's too
    s32 s = 0;
    s32 t = 0;
    for (u32 v = 0; v < owner.NumVertices; ++v) {
        s += owner.Vertices[v]->TexCoords[0];
        t += owner.Vertices[v]->TexCoords[1];
    }
    s /= static_cast<s32>(owner.NumVertices);
    t /= static_cast<s32>(owner.NumVertices);
    const auto inward = [&](melonDS::Vertex vertex) {
        vertex.TexCoords[0] +=
            static_cast<melonDS::s16>(std::clamp(s - vertex.TexCoords[0], -8, 8));
        vertex.TexCoords[1] +=
            static_cast<melonDS::s16>(std::clamp(t - vertex.TexCoords[1], -8, 8));
        return vertex;
    };
    struct Corner {
        melonDS::Vertex vertex;
        s32 z, w;
    };
    const auto make = [&](const Hit& hit) {
        const float at = std::clamp(hit.at, 0.0f, 1.0f);
        const float pw = 1.0f / ((1.0f - at) / wa + at / wb);
        const float fa = (1.0f - at) / wa * pw; // of a, in the picture
        const float fb = 1.0f - fa;
        melonDS::Vertex vertex = a;
        vertex.HiresPosition[0] = hit.point->x;
        vertex.HiresPosition[1] = hit.point->y;
        for (int i = 0; i < 3; ++i) {
            vertex.FinalColor[i] = static_cast<s32>(fa * a.FinalColor[i] + fb * b.FinalColor[i]);
        }
        for (int i = 0; i < 2; ++i) {
            vertex.TexCoords[i] =
                static_cast<melonDS::s16>(fa * a.TexCoords[i] + fb * b.TexCoords[i]);
        }
        const s32 z = owner.WBuffer ? static_cast<s32>(fa * za + fb * zb)
                                    : static_cast<s32>(za + at * static_cast<float>(zb - za));
        // melonDS scales w for a polygon's own sake
        const float w =
            1.0f / ((1.0f - at) / static_cast<float>(std::max<s32>(1, owner.FinalW[edge.from])) +
                    at / static_cast<float>(std::max<s32>(1, owner.FinalW[edge.to])));
        return Corner{inward(vertex), z, static_cast<s32>(w)};
    };
    const Corner first{inward(a), za, owner.FinalW[edge.from]};
    const Corner last{inward(b), zb, owner.FinalW[edge.to]};
    const auto triangle = [&](const Corner& l, const Corner& m, const Corner& r) {
        return Triangle(owner, {l.vertex, m.vertex, r.vertex}, {l.z, m.z, r.z}, {l.w, m.w, r.w});
    };
    // From its first end to each of them and to its other end, and over each of them, as one may be
    // of another surface than its neighbours
    Corner before = first;
    Corner corner = make(hits.front());
    for (std::size_t i = 1; i <= hits.size(); ++i) {
        const Corner after = i < hits.size() ? make(hits[i]) : last;
        if (!triangle(first, corner, after) || (i > 1 && !triangle(before, corner, after))) {
            return;
        }
        before = corner;
        corner = after;
    }
}

void NdsGlSeal::Add(melonDS::GPU3D& scene, int scale) {
    if (!scene.RenderFrameIdentical || scene.RenderNumPolygons != game_count || scale != made_for) {
        game_count = scene.RenderNumPolygons;
        made_for = scale;
        Make(scene);
    }
    // The renderer's list only, which is put back: what it held is the game's
    for (std::size_t i = 0; i < polygon_count; ++i) {
        replaced[i] = std::exchange(scene.RenderPolygonRAM[game_count + i], &polygons[i]);
    }
    scene.RenderNumPolygons = game_count + static_cast<u32>(polygon_count);
}

void NdsGlSeal::Remove(melonDS::GPU3D& scene) {
    for (std::size_t i = 0; i < polygon_count; ++i) {
        scene.RenderPolygonRAM[game_count + i] = replaced[i];
    }
    scene.RenderNumPolygons = game_count;
}

void NdsGlSeal::Make(const melonDS::GPU3D& scene) {
    polygon_count = 0;
    if (game_count == 0 || game_count >= kPolygons) {
        return;
    }
    std::size_t count = 0;
    for (u32 i = 0; i < game_count; ++i) {
        count += scene.RenderPolygonRAM[i] ? scene.RenderPolygonRAM[i]->NumVertices : 0;
    }
    if (count >= kVertices) {
        return;
    }
    room = std::min({kTriangles, kPolygons - game_count, (kVertices - count) / 3});
    if (polygons.empty()) {
        polygons.resize(kTriangles);
        vertices.resize(kTriangles * 3);
    }
    FindEdges(scene);
    FindPoints(scene);
    looks = kLooks;
    for (const Edge& edge : edges) {
        if (!edge.shut) {
            Fill(*scene.RenderPolygonRAM[edge.polygon], edge);
        }
    }
}

} // namespace FrameCores
