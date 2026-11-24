// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <array>
#include <cmath>
#include <limits>
#include <vector>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "video_core/kiu_sheets.h"

using Catch::Approx;
using namespace VideoCore::KiuSheets;

namespace {
using M4 = std::array<std::array<float, 4>, 4>;

/// A camera that keeps z as w (a perspective view): some corners land behind it.
constexpr M4 kCamera{{{0.4f, 0.0f, 0.1f, 2.0f},
                      {0.0f, 0.7f, 0.2f, -1.0f},
                      {0.1f, 0.0f, -1.0f, -10.0f},
                      {0.0f, 0.3f, 1.0f, 1.5f}}};

std::array<float, 4> Clip(float x, float y, float z, const M4& m = kCamera) {
    std::array<float, 4> r{};
    for (int i = 0; i < 4; ++i) {
        r[i] = m[i][0] * x + m[i][1] * y + m[i][2] * z + m[i][3];
    }
    return r;
}
/// A point of a level sheet: its texture coordinates are its place.
Vertex At(float x, float z, float alpha, float y = 0.0f) {
    Vertex v;
    v.pos = Clip(x, y, z);
    v.color = {0.3f, 0.4f, 0.5f, alpha};
    v.more = {0.25f * x + 1.0f, 0.5f * z - 2.0f, x, z, 0, 0, 1, x, 0, z};
    return v;
}
void Quad(std::vector<Vertex>& out, Vertex a, Vertex b, Vertex c, Vertex d) {
    out.insert(out.end(), {a, b, c, a, c, d});
}
/// A square about (cx, cz), `half` to each side.
std::vector<Vertex> Square(float cx, float cz, float half, std::array<float, 4> alpha) {
    std::vector<Vertex> v;
    Quad(v, At(cx - half, cz - half, alpha[0]), At(cx + half, cz - half, alpha[1]),
         At(cx + half, cz + half, alpha[2]), At(cx - half, cz + half, alpha[3]));
    return v;
}
bool Has(const std::vector<Added>& rim, const Vertex& v) {
    for (const Added& a : rim) {
        bool same = true;
        for (int i = 0; i < 4; ++i) {
            same &= std::abs(a.v.pos[i] - v.pos[i]) < 1e-4f;
            same &= std::abs(a.v.color[i] - v.color[i]) < 1e-6f;
        }
        for (int i = 0; i < 10; ++i) {
            same &= std::abs(a.v.more[i] - v.more[i]) < 1e-4f;
        }
        if (same) {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("Kid Icarus sheets: when", "[frame_xr][kiu140c]") {
    REQUIRE(ModeFor(true, false, nullptr) == Mode::Rim);
    REQUIRE(ModeFor(true, false, "1") == Mode::Rim);
    REQUIRE(ModeFor(true, false, "") == Mode::Rim);
    REQUIRE(ModeFor(true, false, "0") == Mode::Off);
    REQUIRE(ModeFor(true, false, "2") == Mode::Plain);
    REQUIRE(ModeFor(false, false, nullptr) == Mode::Off);
    REQUIRE(ModeFor(false, false, "1") == Mode::Off);
    REQUIRE(ModeFor(true, true, nullptr) == Mode::Off); // a cutscene
    REQUIRE(ModeFor(true, true, "2") == Mode::Off);
}

TEST_CASE("Kid Icarus sheets: which draws", "[frame_xr][kiu140c]") {
    const Draw d{true, false, true, true, true, kSheets[0].indices, kSheets[0].stride};
    REQUIRE(MayBeSheet(d) == &kSheets[0]);
    const auto not_one = [&](auto change) {
        Draw x = d;
        change(x);
        REQUIRE(MayBeSheet(x) == nullptr);
    };
    not_one([](Draw& x) { x.top_viewport = false; }); // the Secondary Screen's
    not_one([](Draw& x) { x.depth_test = true; });
    not_one([](Draw& x) { x.added = false; });
    not_one([](Draw& x) { x.textured = false; });
    not_one([](Draw& x) { x.indexed_u8 = false; });
    not_one([](Draw& x) { x.indices = 303; });
    not_one([](Draw& x) { x.stride = 24; });

    const std::array<uint8_t, 8> vertices{1, 2, 3, 4, 5, 6, 7, 8};
    const std::array<uint8_t, 3> indices{0, 1, 2};
    const Sheet s{3, 2, 4, Fnv(vertices.data(), 8), Fnv(indices.data(), 3)};
    REQUIRE(IsSheet(s, vertices.data(), indices.data()));
    REQUIRE_FALSE(IsSheet(s, nullptr, indices.data()));
    REQUIRE_FALSE(IsSheet(s, vertices.data(), nullptr));
    auto other = vertices;
    other[7] = 9;
    REQUIRE_FALSE(IsSheet(s, other.data(), indices.data()));
    const std::array<uint8_t, 3> turned{0, 2, 1};
    REQUIRE_FALSE(IsSheet(s, vertices.data(), turned.data()));
}

TEST_CASE("Kid Icarus sheets: a strip outside each open edge", "[frame_xr][kiu140c]") {
    const auto v = Square(3.0f, 20.0f, 2.0f, {1.0f, 0.5f, 0.8f, 1.0f});
    const auto rim = Rim(v, 2.5f);
    // Four open edges, the diagonal is shared: six vertices each.
    REQUIRE(rim.size() == 4 * 6);
    // The sheet's own corners, and theirs 2.5 times out, see-through, the texture going on.
    REQUIRE(Has(rim, At(1.0f, 18.0f, 1.0f)));
    REQUIRE(Has(rim, At(5.0f, 22.0f, 0.8f)));
    for (const float x : {-2.0f, 8.0f}) {
        for (const float z : {15.0f, 25.0f}) {
            REQUIRE(Has(rim, At(x, z, 0.0f)));
        }
    }
    for (const Added& a : rim) {
        REQUIRE(a.from < v.size());
    }
}

TEST_CASE("Kid Icarus sheets: the strip faces as the sheet does and leaves no gap",
          "[frame_xr][kiu140c]") {
    // Looked at from above with a plain camera, w = 1.
    const M4 flat{{{1, 0, 0, 0}, {0, 0, 1, 0}, {0, 1, 0, 0}, {0, 0, 0, 1}}};
    auto v = Square(0.0f, 0.0f, 1.0f, {1, 1, 1, 1});
    const float xz[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
    for (int i = 0; i < 6; ++i) {
        v[i].pos = Clip(xz[i][0], 0, xz[i][1], flat);
    }
    const auto area = [](const Vertex& a, const Vertex& b, const Vertex& c) {
        return 0.5f * ((b.pos[0] - a.pos[0]) * (c.pos[1] - a.pos[1]) -
                       (c.pos[0] - a.pos[0]) * (b.pos[1] - a.pos[1]));
    };
    REQUIRE(area(v[0], v[1], v[2]) > 0.0f);
    const auto rim = Rim(v, 2.5f);
    float sum = 0.0f;
    for (std::size_t i = 0; i < rim.size(); i += 3) {
        const float a = area(rim[i].v, rim[i + 1].v, rim[i + 2].v);
        REQUIRE(a > 0.0f);
        sum += a;
    }
    REQUIRE(sum == Approx(5.0f * 5.0f - 2.0f * 2.0f));
}

TEST_CASE("Kid Icarus sheets: the faded side is left out", "[frame_xr][kiu140c]") {
    // The wall's side: both its corners faded out. The edges that end there are kept.
    const auto rim = Rim(Square(0.0f, 30.0f, 5.0f, {1.0f, 1.0f, 0.0f, 0.0f}), 2.5f);
    REQUIRE(rim.size() == 3 * 6);
    // The strips' corners are still about the whole sheet's middle.
    REQUIRE(Has(rim, At(-12.5f, 17.5f, 0.0f)));
    REQUIRE(Has(rim, At(12.5f, 42.5f, 0.0f)));
    // Faded out all round.
    REQUIRE(Rim(Square(0, 30, 5, {0, 0, 0, 0}), 2.5f).empty());
    REQUIRE(Rim(Square(0, 30, 5, {kNoAlpha, 0, kNoAlpha, 0}), 2.5f).empty());
    REQUIRE(Rim(Square(0, 30, 5, {1.0f / 255.0f, 0, 0, 0}), 2.5f).size() == 2 * 6);
}

TEST_CASE("Kid Icarus sheets: several in a draw, each about its own middle",
          "[frame_xr][kiu140c]") {
    auto v = Square(0.0f, 30.0f, 2.0f, {1, 1, 1, 1});
    const auto second = Square(40.0f, 60.0f, 4.0f, {1, 1, 1, 1});
    v.insert(v.end(), second.begin(), second.end());
    const auto rim = Rim(v, 2.5f);
    REQUIRE(rim.size() == 8 * 6);
    REQUIRE(Has(rim, At(5.0f, 35.0f, 0.0f)));
    REQUIRE(Has(rim, At(50.0f, 70.0f, 0.0f)));
    REQUIRE(Has(rim, At(30.0f, 50.0f, 0.0f)));
}

TEST_CASE("Kid Icarus sheets: places kept twice are one point", "[frame_xr][kiu140c]") {
    // Two squares side by side whose shared edge has other texture coordinates on each side.
    auto v = Square(-2.0f, 30.0f, 2.0f, {1, 1, 1, 1});
    auto second = Square(2.0f, 30.0f, 2.0f, {1, 1, 1, 1});
    for (Vertex& p : second) {
        p.more[0] += 7.0f;
    }
    v.insert(v.end(), second.begin(), second.end());
    REQUIRE(Rim(v, 2.5f).size() == 6 * 6);
}

TEST_CASE("Kid Icarus sheets: behind the eye", "[frame_xr][kiu140c]") {
    // Corners at w > 0, at w = 0 exactly and behind (w < 0): the strip is the plane's, undivided.
    const auto v = Square(0.0f, -1.5f, 4.0f, {1, 1, 1, 1});
    bool behind = false, before = false;
    for (const Vertex& p : v) {
        behind |= p.pos[3] < 0.0f;
        before |= p.pos[3] > 0.0f;
    }
    REQUIRE((behind && before));
    const auto rim = Rim(v, 2.5f);
    REQUIRE(rim.size() == 4 * 6);
    for (const float x : {-10.0f, 10.0f}) {
        for (const float z : {-11.5f, 8.5f}) {
            REQUIRE(Has(rim, At(x, z, 0.0f)));
        }
    }
    auto zero = v;
    for (Vertex& p : zero) {
        p.pos[3] = 0.0f;
    }
    for (const Added& a : Rim(zero, 2.5f)) {
        REQUIRE(a.v.pos[3] == 0.0f);
        REQUIRE(std::isfinite(a.v.pos[0]));
    }
    REQUIRE(Rim(zero, 2.5f).size() == 4 * 6);
}

TEST_CASE("Kid Icarus sheets: any camera after it", "[frame_xr][kiu140c]") {
    // The head's turn comes after the strip (TurnSoftwareVertices): the same as before it.
    const M4 turn{{{0.9f, 0.1f, 0.2f, 3.0f},
                   {-0.1f, 1.1f, 0.0f, 0.5f},
                   {0.0f, 0.2f, 0.8f, -2.0f},
                   {0.3f, 0.0f, 0.1f, 1.2f}}};
    const auto turned = [&](std::array<float, 4> p) {
        std::array<float, 4> r{};
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                r[i] += turn[i][j] * p[j];
            }
        }
        return r;
    };
    const auto v = Square(1.0f, 12.0f, 3.0f, {1, 1, 0.4f, 1});
    auto w = v;
    for (Vertex& p : w) {
        p.pos = turned(p.pos);
    }
    const auto after = Rim(v, 2.5f), before = Rim(w, 2.5f);
    REQUIRE(after.size() == before.size());
    for (const Added& a : after) {
        Vertex p = a.v;
        p.pos = turned(p.pos);
        REQUIRE(Has(before, p));
    }
}

TEST_CASE("Kid Icarus sheets: nothing open, nothing added", "[frame_xr][kiu140c]") {
    // A closed shape: four triangles about a tetrahedron.
    const Vertex a = At(0, 30, 1), b = At(4, 30, 1), c = At(2, 34, 1), d = At(2, 32, 1, 3.0f);
    REQUIRE(Rim({a, b, c, a, d, b, b, d, c, c, d, a}, 2.5f).empty());
    REQUIRE(Rim({a, b, c, a, d, b, b, d, c}, 2.5f).size() == 3 * 6); // one side off
    REQUIRE(Rim({}, 2.5f).empty());
}

TEST_CASE("Kid Icarus sheets: degenerate draws", "[frame_xr][kiu140c]") {
    const Vertex a = At(0, 30, 1), b = At(4, 30, 1), c = At(2, 34, 1);
    REQUIRE(Rim({a, b}, 2.5f).empty());
    REQUIRE(Rim({a, b, c, a}, 2.5f).empty());
    REQUIRE(Rim({a, b, c}, 1.0f).empty()); // no strip
    REQUIRE(Rim({a, b, c}, std::numeric_limits<float>::quiet_NaN()).empty());
    REQUIRE(Rim({a, a, b, b, b, b}, 2.5f).empty()); // no area anywhere
    // A triangle of no area beside a good one opens nothing and closes nothing.
    REQUIRE(Rim({a, b, c, a, b, b}, 2.5f).size() == 3 * 6);
    Vertex bad = c;
    bad.pos[1] = std::numeric_limits<float>::infinity();
    REQUIRE(Rim({a, b, bad}, 2.5f).empty());
    bad = c;
    bad.color[3] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(Rim({a, b, bad}, 2.5f).empty());
    REQUIRE(Rim(std::vector<Vertex>(kMaxVertices + 3, a), 2.5f).empty());
}
