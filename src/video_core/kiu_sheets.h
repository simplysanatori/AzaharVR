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
#include <cstdlib>
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

/// What a draw's vertices (a triangle list) are: which are one point, which points hang together
/// (a sheet), and the edges only one triangle has. Empty: not a list, too long, a number that is
/// none.
struct Outline {
    struct Edge {
        uint32_t a, b; ///< vertices, as the triangle goes round
    };
    std::vector<uint32_t> point; ///< a vertex's point
    std::vector<uint32_t> first; ///< a point's first vertex
    std::vector<uint32_t> root;  ///< a point's sheet: one of its points
    std::vector<Edge> open;
};
inline Outline OutlineOf(const std::vector<Vertex>& in) {
    Outline o;
    const std::size_t n = in.size();
    if (n < 3 || n % 3 != 0 || n > kMaxVertices) {
        return o;
    }
    for (const Vertex& v : in) {
        for (const auto* a : {v.pos.data(), v.color.data()}) {
            for (int i = 0; i < 4; ++i) {
                if (!std::isfinite(a[i])) {
                    return o;
                }
            }
        }
    }
    // The same place is the same point: the same vertex shaded twice, or one the model keeps twice.
    auto& point = o.point;
    auto& first = o.first;
    point.resize(n);
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
    auto& root = o.root;
    root.resize(points);
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
    for (std::size_t i = 0, j; i < edges.size(); i = j) {
        for (j = i + 1; j < edges.size() && edges[j] >> 20 == edges[i] >> 20; ++j) {
        }
        if (j == i + 1) {
            const uint32_t corner = static_cast<uint32_t>(edges[i] & 0xFFFFF);
            o.open.push_back({corner, next(corner)});
        }
    }
    for (uint32_t p = 0; p < points; ++p) {
        root[p] = find(p);
    }
    return o;
}

/// The strips of a draw's vertices (a triangle list). A sheet is what hangs together by shared
/// places; its middle is that of the vertices on its open edges. Empty: nothing open, or not a
/// draw to add to (not a list, too long, a number that is none).
inline std::vector<Added> Rim(const std::vector<Vertex>& in, float out) {
    std::vector<Added> rim;
    if (!(out > 1.0f)) {
        return rim;
    }
    const Outline o = OutlineOf(in);
    const auto& point = o.point;
    const auto& open = o.open;
    // Each sheet's middle.
    struct Middle {
        std::array<double, 14> sum{};
        int count = 0;
    };
    std::vector<Middle> middles(o.first.size());
    std::vector<bool> counted(o.first.size());
    for (const Outline::Edge& e : open) {
        for (const uint32_t i : {e.a, e.b}) {
            if (counted[point[i]]) {
                continue;
            }
            counted[point[i]] = true;
            Middle& m = middles[o.root[point[i]]];
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
        const Middle& m = middles[o.root[point[i]]];
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
    for (const Outline::Edge& e : open) {
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

/// A test (AZAHARFRAME_KIU_SHEETS_SKY, live, off without it): the sheets go on over the whole sky
/// in place of the rim, each from its hard edges as one ring that holds a level and one that
/// fades out. Distances are in the sheet's own sizes (half its width to the sides, its depth to the
/// front), from its edges; the texture goes on at its own scale.
struct Sky {
    bool on = false;
    float first = 0.15f; ///< where the level is reached
    float hold = 6.0f;   ///< how far it is held
    float far = 10.0f;   ///< where it has faded out
    float level = 0.0f;  ///< 0: the alpha of the sheet's middle, 1: its hard edges' own, -1: none
    float slope = 0.0f;  ///< 0: level from the edges, 1: down as the sheet's own sides
};
/// "1": as above; a list: first, hold, far, level, slope, as many as are given.
inline Sky ParseSky(const char* text) {
    Sky s;
    if (!text || !*text || (text[0] == '0' && text[1] == '\0')) {
        return s;
    }
    if (text[0] != '1' || text[1] != '\0') {
        float* const v[5] = {&s.first, &s.hold, &s.far, &s.level, &s.slope};
        for (float* f : v) {
            char* end = nullptr;
            *f = std::strtof(text, &end);
            if (end == text || !std::isfinite(*f)) {
                return {};
            }
            if (*end != ',') {
                break;
            }
            text = end + 1;
        }
    }
    s.on = s.first > 0.0f && s.hold >= s.first && s.far > s.hold && s.far <= 100.0f &&
           s.level >= -1.0f && s.level <= 1.0f && s.slope >= 0.0f && s.slope <= 1.0f;
    return s.on ? s : Sky{};
}
/// The one in force: one writer (each VBlank), read a draw on the same thread or the next.
struct LiveSky {
    std::array<std::atomic<float>, 5> f{};
    std::atomic<bool> on{false};

    void Set(const Sky& s) {
        const float v[5] = {s.first, s.hold, s.far, s.level, s.slope};
        for (int i = 0; i < 5; ++i) {
            f[i].store(v[i], std::memory_order_relaxed);
        }
        on.store(s.on, std::memory_order_release);
    }
    Sky Get() const {
        Sky s;
        s.on = on.load(std::memory_order_acquire);
        float* const v[5] = {&s.first, &s.hold, &s.far, &s.level, &s.slope};
        for (int i = 0; i < 5; ++i) {
            *v[i] = f[i].load(std::memory_order_relaxed);
        }
        return s;
    }
};
inline LiveSky g_sky;

/// The sky strips of a draw's vertices. Only sheets of the one shape there is: three hard edges in
/// a row, from a faded corner out, across the front and back to a faded corner; any other sheet
/// gets nothing.
inline std::vector<Added> SkyStrips(const std::vector<Vertex>& in, const Sky& sky) {
    std::vector<Added> out;
    const Outline o = OutlineOf(in);
    if (!sky.on || o.open.empty()) {
        return out;
    }
    using V = std::array<float, 14>; // the place, then what is linear as it is
    const auto values = [](const Vertex& v) {
        V r;
        std::copy(v.pos.begin(), v.pos.end(), r.begin());
        std::copy(v.more.begin(), v.more.end(), r.begin() + 4);
        return r;
    };
    const auto mix = [](const V& a, float ka, const V& b, float kb, const V& c, float kc) {
        V r;
        for (std::size_t i = 0; i < r.size(); ++i) {
            r[i] = ka * a[i] + kb * b[i] + kc * c[i];
        }
        return r;
    };
    // Which points are on an open edge, and each sheet's middle: its other points that show.
    std::vector<bool> edge_point(o.first.size());
    for (const Outline::Edge& e : o.open) {
        edge_point[o.point[e.a]] = edge_point[o.point[e.b]] = true;
    }
    struct Middle {
        std::array<double, 14> sum{};
        double alpha = 0.0;
        int count = 0;
    };
    std::vector<Middle> middles(o.first.size());
    for (uint32_t p = 0; p < o.first.size(); ++p) {
        const Vertex& v = in[o.first[p]];
        if (edge_point[p] || v.color[3] <= kNoAlpha) {
            continue;
        }
        Middle& m = middles[o.root[p]];
        const V x = values(v);
        for (std::size_t i = 0; i < x.size(); ++i) {
            m.sum[i] += x[i];
        }
        m.alpha += v.color[3];
        ++m.count;
    }
    const auto hard = [&](const Outline::Edge& e) {
        return in[e.a].color[3] > kNoAlpha || in[e.b].color[3] > kNoAlpha;
    };
    for (const Outline::Edge& start : o.open) {
        // The row: from a faded corner, as the triangles go round.
        if (!hard(start) || in[start.a].color[3] > kNoAlpha) {
            continue;
        }
        std::array<uint32_t, 4> c{start.a, start.b, 0, 0};
        std::size_t n = 2;
        while (n < 5) {
            const uint32_t at = o.point[c[n - 1]];
            const auto it = std::find_if(o.open.begin(), o.open.end(), [&](const auto& e) {
                return hard(e) && o.point[e.a] == at;
            });
            if (it == o.open.end()) {
                break;
            }
            if (n == 4) {
                n = 5; // a row of more than three
                break;
            }
            c[n++] = it->b;
        }
        const uint32_t root = o.root[o.point[start.a]];
        const Middle& m = middles[root];
        if (n != 4 || in[c[3]].color[3] > kNoAlpha || in[c[1]].color[3] <= kNoAlpha ||
            in[c[2]].color[3] <= kNoAlpha || m.count == 0) {
            continue;
        }
        V mid;
        for (std::size_t i = 0; i < mid.size(); ++i) {
            mid[i] = static_cast<float>(m.sum[i] / m.count);
        }
        const float mid_alpha = static_cast<float>(m.alpha / m.count);
        const V w0 = values(in[c[0]]), n0 = values(in[c[1]]), n1 = values(in[c[2]]),
                w1 = values(in[c[3]]);
        // The ways out: each level along the sheet, or down as from its middle.
        const float t = sky.slope, l = 1.0f - t;
        const auto way = [&](const V& to, const V& from, float k, const V& a, const V& b) {
            const V flat = mix(to, k, from, -k, to, 0.0f);
            const V down = mix(a, 0.5f, b, 0.5f, mid, -1.0f);
            return mix(flat, l, down, t, to, 0.0f);
        };
        const V side0 = way(n0, n1, 0.5f, n0, w0), side1 = way(n1, n0, 0.5f, n1, w1);
        const V front0 = way(n0, w0, 1.0f, n0, n1), front1 = way(n1, w1, 1.0f, n0, n1);
        // The stations about the sheet: out to each side, the corners, out to the front.
        struct Station {
            uint32_t from;
            const V *at, *a, *b; ///< the corner, and the ways it goes out (either may be null)
        };
        const std::array<Station, 8> stations{{{c[0], &w0, &side0, nullptr},
                                               {c[1], &n0, &side0, nullptr},
                                               {c[1], &n0, &side0, &front0},
                                               {c[1], &n0, nullptr, &front0},
                                               {c[2], &n1, nullptr, &front1},
                                               {c[2], &n1, &side1, &front1},
                                               {c[2], &n1, &side1, nullptr},
                                               {c[3], &w1, &side1, nullptr}}};
        const std::array<float, 4> rings{0.0f, sky.first, sky.hold, sky.far};
        const auto vertex = [&](const Station& s, std::size_t ring) {
            const float k = rings[ring];
            const V x = mix(*s.at, 1.0f, s.a ? *s.a : *s.at, s.a ? k : 0.0f, s.b ? *s.b : *s.at,
                            s.b ? k : 0.0f);
            Added r{s.from, in[s.from]};
            std::copy_n(x.begin(), 4, r.v.pos.begin());
            std::copy_n(x.begin() + 4, 10, r.v.more.begin());
            if (ring != 0) {
                // The wall's side stays faded out along its whole length.
                const bool wall = in[s.from].color[3] <= kNoAlpha;
                const float to = sky.level < 0.0f ? 2.0f * mid_alpha : r.v.color[3];
                r.v.color[3] = wall || ring == 3 ? 0.0f : mid_alpha + sky.level * (to - mid_alpha);
            }
            return r;
        };
        for (std::size_t ring = 0; ring < 3; ++ring) {
            for (std::size_t i = 0; i + 1 < stations.size(); ++i) {
                const Added a = vertex(stations[i], ring), b = vertex(stations[i + 1], ring);
                const Added a2 = vertex(stations[i], ring + 1),
                            b2 = vertex(stations[i + 1], ring + 1);
                if (a.v.pos != b.v.pos) {
                    out.insert(out.end(), {b, a, a2});
                }
                out.insert(out.end(), {b, a2, b2});
            }
        }
    }
    return out;
}

} // namespace VideoCore::KiuSheets
