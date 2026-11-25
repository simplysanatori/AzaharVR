// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "video_core/kiu_hud.h"

using Catch::Approx;
using namespace VideoCore::KiuHud;

namespace {
constexpr float kRad = 3.14159265f / 180.0f;
/// First person's screen on the Frame: 126 x 112 degrees, 5 degrees down.
const float kTanX = std::tan(63.0f * kRad), kTanY = std::tan(56.0f * kRad);
Place At(const Wanted& w = {}) {
    return PlaceFor(w, kTanX, kTanY, -5.0f * kRad);
}
/// Where a vertex of the game's frame is seen (degrees across and up from straight ahead), after
/// the shader's division by the factors.
struct Seen {
    float across, up;
};
Seen See(float across, float up, const Place& p, float f = 1.0f, float fy = 1.0f) {
    // A small triangle about the point, so the point's own group decides.
    Vertex t[3] = {{up, across, 0.2f, 1.0f},
                   {up + 1e-4f, across, 0.2f, 1.0f},
                   {up, across + 1e-4f, 0.2f, 1.0f}};
    Move(t, 3, p, f, fy);
    REQUIRE(t[0].w == 2.0f);
    REQUIRE(t[0].z / t[0].w == Approx(0.2f));
    return {std::atan(t[0].y / t[0].w / f * kTanX) / kRad,
            std::atan(t[0].x / t[0].w / fy * kTanY) / kRad - 5.0f};
}
} // namespace

TEST_CASE("Kid Icarus HUD: which draws", "[frame_xr][kiu140]") {
    Draw d{true, false, true, true, 0x218D1300, 18};
    REQUIRE(MayBeHud(d));
    Draw x = d;
    x.top_viewport = false; // the bottom screen
    REQUIRE_FALSE(MayBeHud(x));
    x = d;
    x.depth_test = true; // the world and its sprites
    REQUIRE_FALSE(MayBeHud(x));
    x = d;
    x.blended = false;
    REQUIRE_FALSE(MayBeHud(x));
    x = d;
    x.texture = 0x18328000; // a copy of a picture
    REQUIRE_FALSE(MayBeHud(x));
    x = d;
    x.vertices = 4000;
    REQUIRE_FALSE(MayBeHud(x));

    const Vertex whole[3] = {{-1, -1, 0, 1}, {1, -1, 0, 1}, {-1, 1, 0, 1}};
    REQUIRE_FALSE(Movable(whole, 3)); // a fade
    const Vertex bar[3] = {{0.8f, -1, 0, 1}, {0.9f, -1, 0, 1}, {0.8f, -0.3f, 0, 1}};
    REQUIRE(Movable(bar, 3)); // on the frame's edge: still moved whole
    const Vertex world[3] = {{0.8f, -1, 0, 1}, {0.9f, -1, 0, 1}, {0.8f, -0.3f, 0, 12}};
    REQUIRE_FALSE(Movable(world, 3));
}

TEST_CASE("Kid Icarus HUD: in view, in its own shape", "[frame_xr][kiu140]") {
    const Place p = At();
    REQUIRE(p.on);
    // All of the game's frame within 31 degrees aside and 33 up and down.
    for (const float a : {-1.0f, -0.33f, 0.0f, 0.58f, 1.0f}) {
        for (const float u : {-1.0f, 0.0f, 0.95f}) {
            const Seen s = See(a, u, p);
            INFO(a << ", " << u << ": " << s.across << ", " << s.up);
            REQUIRE(std::abs(s.across) < 31.0f);
            REQUIRE(std::abs(s.up) < 33.0f);
        }
    }
    REQUIRE(See(-0.7f, -0.85f, p).up < -18.0f); // the practice counters: well under the reticle
    // Square stays square: 0.1 of the frame's height is 24 px, across 24 px is 0.12.
    const float tall = p.half_up * 0.2f * kTanY, wide = p.half_across * 0.12f * kTanX;
    REQUIRE(tall == Approx(wide).epsilon(0.01));
    Wanted w;
    w.size = 50;
    REQUIRE(At(w).half_across < 0.6f * p.half_across);
    // The game drawing past its frame (outside battles): the same place.
    const Seen a = See(-0.67f, 0.85f, p), b = See(-0.67f, 0.85f, p, 3.96f, 5.0f);
    REQUIRE(a.across == Approx(b.across).margin(0.01));
    REQUIRE(a.up == Approx(b.up).margin(0.01));
    REQUIRE_FALSE(PlaceFor({}, 0.0f, 1.0f, 0.0f).on);
}

TEST_CASE("Kid Icarus HUD: the bar and the score, linked and apart", "[frame_xr][kiu140]") {
    const auto bar_l = [](const Place& p) { return See(-0.99f, 0.86f, p).across; };
    const auto bar_r = [](const Place& p) { return See(-0.34f, 0.86f, p).across; };
    const auto score_l = [](const Place& p) { return See(0.585f, 0.86f, p).across; };
    const auto score_r = [](const Place& p) { return See(0.99f, 0.86f, p).across; };
    for (const int size : {40, 100, 200}) {
        for (const float at : {-20.0f, 0.0f, 15.0f}) {
            Wanted w;
            w.size = size;
            w.across = at;
            w.height = 10.0f;
            const Place p = At(w);
            INFO(size << " at " << at);
            // Side by side, never on each other, and one unit about where it is asked for.
            REQUIRE(bar_r(p) < score_l(p));
            REQUIRE(score_l(p) - bar_r(p) < 0.1f * (score_r(p) - bar_l(p)));
            REQUIRE(bar_l(p) < at);
            REQUIRE(score_r(p) > at);
            REQUIRE(See(-0.665f, 0.86f, p).up == Approx(10.0f).margin(0.2));
            REQUIRE(See(0.79f, 0.86f, p).up == Approx(10.0f).margin(0.2));
        }
    }
    REQUIRE(bar_l(At()) == Approx(-score_r(At())).margin(0.7)); // in the middle
    // Apart: each where it is asked for, the other not moved.
    Wanted w;
    w.linked = false;
    w.bar_across = -25.0f;
    w.bar_height = -12.0f;
    w.score_across = 30.0f;
    w.score_height = 25.0f;
    const Place p = At(w);
    REQUIRE(See(-0.665f, 0.86f, p).across == Approx(-25.0f).margin(0.2));
    REQUIRE(See(-0.665f, 0.86f, p).up == Approx(-12.0f).margin(0.2));
    REQUIRE(See(0.79f, 0.86f, p).across == Approx(30.0f).margin(0.2));
    REQUIRE(See(0.79f, 0.86f, p).up == Approx(25.0f).margin(0.2));
    // The rest of the HUD does not move with them.
    REQUIRE(See(0.0f, 0.0f, p).up == Approx(6.0f).margin(0.2));
    REQUIRE(See(0.0f, 0.0f, At()).up == Approx(6.0f).margin(0.2));
}

TEST_CASE("Kid Icarus HUD: the subtitles, where they are asked for", "[frame_xr][kiu140]") {
    Draw d{true, false, true, true, 0x181A8000, 12, 512, 128, 0};
    REQUIRE(IsSubtitle(d));
    for (const auto change : {0, 1, 2, 3}) {
        Draw x = d;
        x.width = change == 0 ? 256 : x.width;
        x.texture = change == 1 ? 0x2188B680 : x.texture;
        x.vertices = change == 2 ? 6 : x.vertices;
        x.top_viewport = change != 3;
        REQUIRE_FALSE(IsSubtitle(x));
    }
    for (const float height : {-30.0f, -18.0f, 10.0f}) {
        for (const int size : {50, 130, 250}) {
            Wanted w;
            w.subtitle_height = height;
            w.subtitle_size = size;
            const Place p = At(w);
            // At two depths of w, the game drawing past its frame or not: the line left of the
            // middle (a draw log's numbers), its middle straight ahead after.
            for (const float q : {1.0f, 3.0f}) {
                for (const float f : {1.0f, 3.96f}) {
                    Vertex v[4] = {{-0.53f * q, 0.80f * q, 0.2f * q, q},
                                   {-0.53f * q, -0.15f * q, 0.2f * q, q},
                                   {-0.77f * q, 0.80f * q, 0.2f * q, q},
                                   {-0.77f * q, -0.15f * q, 0.2f * q, q}};
                    REQUIRE(MoveSubtitle(v, 4, p, f, f));
                    const auto up = [&](int i) { return std::atan(v[i].x / v[i].w / f * kTanY) / kRad - 5.0f; };
                    const auto side = [&](int i) { return std::atan(v[i].y / v[i].w / f * kTanX) / kRad; };
                    REQUIRE(0.5f * (up(0) + up(2)) == Approx(height).margin(0.6));
                    REQUIRE(side(0) == Approx(-side(1)).margin(0.01));
                    REQUIRE(v[2].z / v[2].w == Approx(0.2f));
                    // As wide as its share of the frame, the frame 60 degrees at 100.
                    REQUIRE(std::tan(side(0) * kRad) ==
                            Approx(0.475f * std::tan(30.0f * kRad) * static_cast<float>(size) / 100.0f).margin(0.002));
                }
            }
        }
    }
    Vertex world[3] = {{0, 0, 0, 1}, {0, 1, 0, 4}, {1, 1, 0, 1}};
    REQUIRE_FALSE(MoveSubtitle(world, 3, At(), 1.0f, 1.0f));
}

TEST_CASE("Kid Icarus HUD: a page outside a battle keeps its layout", "[kiu140]") {
    using namespace VideoCore::KiuHud;
    const Place p = PagePlace(4.0f, 5.0f);
    REQUIRE(p.on);
    REQUIRE(p.page);
    // Pieces at a top corner, the middle and the foot: each over the game's own frame.
    for (const float up : {0.9f, 0.0f, -0.9f}) {
        for (const float across : {-0.9f, 0.0f, 0.9f}) {
            Vertex v[3] = {{up, across, 0, 1}, {up + 0.02f, across, 0, 1}, {up, across + 0.02f, 0, 1}};
            Move(v, 3, p, 4.0f, 5.0f);
            REQUIRE(std::abs(v[0].x / v[0].w - up) < 1e-5f);
            REQUIRE(std::abs(v[0].y / v[0].w - across) < 1e-5f);
            REQUIRE(v[0].w == 2.0f);
        }
    }
    Live live;
    live.Set(p);
    REQUIRE(live.Get().page);
    REQUIRE_FALSE(PagePlace(0.5f, 1.0f).on);
}

TEST_CASE("Kid Icarus HUD: the text texture's other draws are pieces; text targets are wide",
          "[frame_xr][kiu140]") {
    using namespace VideoCore::KiuHud;
    Draw d;
    d.top_viewport = d.blended = d.textured = true;
    d.texture = 0x181A8000;
    d.vertices = 24;
    d.width = 512;
    d.height = 128;
    REQUIRE(IsText(d));
    REQUIRE_FALSE(IsSubtitle(d));
    d.vertices = 12;
    REQUIRE(IsText(d));
    REQUIRE(IsSubtitle(d));
    d.width = 256; // a picture of the world
    REQUIRE_FALSE(IsText(d));
    REQUIRE(IsTextTarget(512.0f, 128.0f));
    REQUIRE_FALSE(IsTextTarget(240.0f, 400.0f));
    REQUIRE_FALSE(IsTextTarget(240.0f, 320.0f));
    REQUIRE_FALSE(IsTextTarget(128.0f, 256.0f));
}
