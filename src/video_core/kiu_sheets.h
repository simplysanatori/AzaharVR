// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiu140: Kid Icarus: Uprising's cloud sheets in Angel's Aim, a soft rim past their
// edges.
//
// Chapter 2's climb flies through six cloud sheets, one draw of 306 vertices: added light, no
// depth test, and a vertex alpha that is 0 along the wall and 1 at the two outer corners, where
// the sheet ends. The game's own camera (about 45 degrees, up the wall) never looks far enough to
// see where; first person's (126 x 112 degrees, and the head) does, as straight lines.
//
// Such a draw is shaded on the CPU and drawn as it is, and a strip is added outside each edge that
// only one triangle has and that is not already faded out: from the edge to kOut times its
// distance from the sheet's middle, going to alpha 0. Places and texture coordinates are linear in
// clip space before the division, so the strip is the same whatever the camera, behind the eye
// (w <= 0) too. These sheets are tents (the middle is raised round the path): the strip goes on
// down as its corners lie from the middle, less steeply than the sheet's own side.
//
// Only the draws of a list, known by their vertices and indices byte for byte: a rule would widen
// water, light beams and grass cards as well. Only in first person, not in its cutscenes
// (frame_xr/kiu_hooks.cpp), AZAHARFRAME_KIU_SHEETS=0 keeps it off: the draw is then the GPU's,
// untouched.
//
// Pure, unit tested (src/tests/frame_xr/kiu_sheets.cpp); the rasterizer finds the draws and adds
// the strips (rasterizer_accelerated.cpp SheetDraw, TurnSoftwareVertices).

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <utility>
#include <vector>

namespace VideoCore::KiuSheets {

enum class Mode : int {
    Off,
    Rim,
    Plain, ///< a test (AZAHARFRAME_KIU_SHEETS=2): on the CPU with nothing added
};

/// What this VBlank asks for. `text`: the switch, null when it is not set.
inline Mode ModeFor(bool first_person, bool cutscene, const char* text) {
    if (!first_person || cutscene) {
        return Mode::Off;
    }
    if (text && text[0] != '\0' && text[1] == '\0') {
        if (text[0] == '0') {
            return Mode::Off;
        }
        if (text[0] == '2') {
            return Mode::Plain;
        }
    }
    return Mode::Rim;
}
/// Set each VBlank (frame_xr/kiu_hooks.cpp), read by the rasterizer a draw.
inline std::atomic<Mode> g_mode{Mode::Off};

/// One known draw: `indices` 8-bit indices into `vertices` vertices of `stride` bytes.
struct Sheet {
    uint32_t indices, vertices, stride;
    uint64_t vertex_hash, index_hash; ///< Fnv of the bytes
};
inline constexpr std::array kSheets{
    // Chapter 2, the climb up the wall (stage a0201, stg_a02m03_01, a02m01c1_cloud02).
    Sheet{306, 78, 20, 0xCFAE5C3275C7751EULL, 0x69172339DDC0D26CULL},
};

inline uint64_t Fnv(const uint8_t* p, std::size_t n) {
    uint64_t h = 0xCBF29CE484222325ULL;
    for (std::size_t i = 0; i < n; ++i) {
        h = (h ^ p[i]) * 0x100000001B3ULL;
    }
    return h;
}

/// The state of a draw that may be a sheet's.
struct Draw {
    bool top_viewport = false; ///< 240 x 400
    bool depth_test = false;
    bool added = false; ///< blended: source alpha, one
    bool textured = false;
    bool indexed_u8 = false;
    uint32_t indices = 0;
    uint32_t stride = 0; ///< of the one vertex array
};
/// The list's entry it may be (then its bytes are looked at), or null.
constexpr const Sheet* MayBeSheet(const Draw& d) {
    if (d.top_viewport && !d.depth_test && d.added && d.textured && d.indexed_u8) {
        for (const Sheet& s : kSheets) {
            if (s.indices == d.indices && s.stride == d.stride) {
                return &s;
            }
        }
    }
    return nullptr;
}
inline bool IsSheet(const Sheet& s, const uint8_t* vertices, const uint8_t* indices) {
    return vertices && indices && Fnv(vertices, std::size_t{s.vertices} * s.stride) == s.vertex_hash &&
           Fnv(indices, s.indices) == s.index_hash;
}

/// How far out the strip goes, of the edge's distance from the sheet's middle.
inline constexpr float kOut = 1.6f;
/// A test: the rim's length (AZAHARFRAME_KIU_SHEETS_OUT, live; kOut without it).
inline std::atomic<float> g_out{kOut};
/// Alpha up to this is faded out already: half a step of the game's 8 bits.
inline constexpr float kNoAlpha = 0.5f / 255.0f;
inline constexpr std::size_t kMaxVertices = 3072;

struct Vertex {
    std::array<float, 4> pos;   ///< clip
    std::array<float, 4> color; ///< alpha last
    std::array<float, 10> more; ///< texture coordinates and the like, linear as `pos` is
};
/// One of the strips' vertices (a triangle list); what `v` does not hold is that of the draw's
/// vertex `from`.
struct Added {
    uint32_t from;
    Vertex v;
};

/// The strips of a draw's vertices (a triangle list). A sheet is what hangs together by shared
/// places; its middle is that of the vertices on its open edges. Empty: nothing open, or not a
/// draw to add to (not a list, too long, a number that is none).
inline std::vector<Added> Rim(const std::vector<Vertex>& in, float out) {
    std::vector<Added> rim;
    const std::size_t n = in.size();
    if (n < 3 || n % 3 != 0 || n > kMaxVertices || !(out > 1.0f)) {
        return rim;
    }
    for (const Vertex& v : in) {
        for (const auto* a : {v.pos.data(), v.color.data()}) {
            for (int i = 0; i < 4; ++i) {
                if (!std::isfinite(a[i])) {
                    return rim;
                }
            }
        }
    }
    // The same place is the same point: the same vertex shaded twice, or one the model keeps twice.
    std::vector<uint32_t> point(n), first; // a vertex's point; a point's first vertex
    std::vector<uint32_t> slots(std::bit_ceil(n * 2), UINT32_MAX);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t h = 0;
        for (const float x : in[i].pos) {
            h = (h ^ std::bit_cast<uint32_t>(x + 0.0f)) * 0x9E3779B1u; // + 0: -0 is 0
            h ^= h >> 15;
        }
        std::size_t at = h & (slots.size() - 1);
        while (slots[at] != UINT32_MAX && in[first[slots[at]]].pos != in[i].pos) {
            at = (at + 1) & (slots.size() - 1);
        }
        if (slots[at] == UINT32_MAX) {
            slots[at] = static_cast<uint32_t>(first.size());
            first.push_back(i);
        }
        point[i] = slots[at];
    }
    const uint32_t points = static_cast<uint32_t>(first.size());
    // The sheets: points joined by triangles.
    std::vector<uint32_t> root(points);
    std::iota(root.begin(), root.end(), 0u);
    const auto find = [&](uint32_t p) {
        while (root[p] != p) {
            p = root[p] = root[root[p]];
        }
        return p;
    };
    // Each triangle's edges: the two points (lower first) and the corner it starts at.
    const auto next = [](uint32_t corner) { return corner - corner % 3 + (corner + 1) % 3; };
    std::vector<uint64_t> edges;
    edges.reserve(n);
    for (uint32_t t = 0; t < n; t += 3) {
        const uint32_t p[3] = {point[t], point[t + 1], point[t + 2]};
        if (p[0] == p[1] || p[1] == p[2] || p[2] == p[0]) {
            continue; // no area
        }
        for (uint32_t i = 0; i < 3; ++i) {
            const uint32_t a = p[i], b = p[(i + 1) % 3];
            edges.push_back(uint64_t{std::min(a, b)} << 40 | uint64_t{std::max(a, b)} << 20 |
                            (t + i));
            root[find(a)] = find(b);
        }
    }
    std::sort(edges.begin(), edges.end());
    struct Edge {
        uint32_t a, b; ///< vertices, as the triangle goes round
    };
    std::vector<Edge> open;
    for (std::size_t i = 0, j; i < edges.size(); i = j) {
        for (j = i + 1; j < edges.size() && edges[j] >> 20 == edges[i] >> 20; ++j) {
        }
        if (j == i + 1) {
            const uint32_t corner = static_cast<uint32_t>(edges[i] & 0xFFFFF);
            open.push_back({corner, next(corner)});
        }
    }
    // Each sheet's middle.
    struct Middle {
        std::array<double, 14> sum{};
        int count = 0;
    };
    std::vector<Middle> middles(points);
    std::vector<bool> counted(points);
    for (const Edge& e : open) {
        for (const uint32_t i : {e.a, e.b}) {
            if (counted[point[i]]) {
                continue;
            }
            counted[point[i]] = true;
            Middle& m = middles[find(point[i])];
            for (std::size_t k = 0; k < 4; ++k) {
                m.sum[k] += in[i].pos[k];
            }
            for (std::size_t k = 0; k < 10; ++k) {
                m.sum[4 + k] += in[i].more[k];
            }
            ++m.count;
        }
    }
    rim.reserve(open.size() * 6);
    const auto outside = [&](uint32_t i) {
        const Middle& m = middles[find(point[i])];
        const auto at = [&](float x, std::size_t k) {
            const double c = m.sum[k] / m.count;
            return static_cast<float>(c + out * (x - c));
        };
        Vertex v = in[i];
        for (std::size_t k = 0; k < 4; ++k) {
            v.pos[k] = at(v.pos[k], k);
        }
        for (std::size_t k = 0; k < 10; ++k) {
            v.more[k] = at(v.more[k], 4 + k);
        }
        v.color[3] = 0.0f;
        return v;
    };
    for (const Edge& e : open) {
        if (in[e.a].color[3] <= kNoAlpha && in[e.b].color[3] <= kNoAlpha) {
            continue; // faded out already
        }
        // The triangle over the edge goes round the other way: the sheet's own facing.
        const Added b{e.b, in[e.b]}, a2{e.a, outside(e.a)};
        rim.push_back(b);
        rim.push_back({e.a, in[e.a]});
        rim.push_back(a2);
        rim.push_back(b);
        rim.push_back(a2);
        rim.push_back({e.b, outside(e.b)});
    }
    return rim;
}

} // namespace VideoCore::KiuSheets
