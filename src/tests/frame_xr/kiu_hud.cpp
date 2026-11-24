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
Place Default(Side bar = Side::Left, Side score = Side::Right, int size = 100, int height = 6,
              int spread = 100) {
    return PlaceFor(size, height, spread, bar, score, kTanX, kTanY, -5.0f * kRad);
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
    Move(t, p, f, fy);
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
    const Place p = Default();
    REQUIRE(p.on);
    // All of the game's frame within 27 degrees aside and 26 up.
    for (const float a : {-1.0f, -0.33f, 0.0f, 0.58f, 1.0f}) {
        for (const float u : {-1.0f, 0.0f, 0.95f}) {
            const Seen s = See(a, u, p);
            INFO(a << ", " << u << ": " << s.across << ", " << s.up);
            REQUIRE(std::abs(s.across) < 27.0f);
            REQUIRE(std::abs(s.up) < 26.0f);
        }
    }
    // The bar at the top left, the score at the top right.
    REQUIRE(See(-0.67f, 0.85f, p).across < -12.0f);
    REQUIRE(See(-0.67f, 0.85f, p).up > 14.0f);
    REQUIRE(See(0.79f, 0.86f, p).across > 15.0f);
    // Square stays square: 0.1 of the frame's height is 24 px, across 24 px is 0.12.
    const float tall = p.half_up * 0.2f * kTanY, wide = p.half_across * 0.12f * kTanX;
    REQUIRE(tall == Approx(wide).epsilon(0.01));
    // The size scales it and the height moves it.
    REQUIRE(Default(Side::Left, Side::Right, 50).half_across == Approx(0.5f * p.half_across));
    REQUIRE(See(0, 0, Default(Side::Left, Side::Right, 100, 20)).up == Approx(20.0f).margin(0.2));
    REQUIRE(See(0, 0, p).up == Approx(6.0f).margin(0.2));
    // The game drawing past its frame (outside battles): the same place.
    const Seen a = See(-0.67f, 0.85f, p), b = See(-0.67f, 0.85f, p, 3.96f, 5.0f);
    REQUIRE(a.across == Approx(b.across).margin(0.01));
    REQUIRE(a.up == Approx(b.up).margin(0.01));
}

TEST_CASE("Kid Icarus HUD: where the bar and the score go", "[frame_xr][kiu140]") {
    const auto bar = [](const Place& p) { return See(-0.665f, 0.85f, p); };
    const auto score = [](const Place& p) { return See(0.79f, 0.85f, p); };
    REQUIRE(bar(Default(Side::Middle)).across == Approx(0.0f).margin(0.2));
    REQUIRE(bar(Default(Side::Right, Side::Left)).across > 12.0f);
    REQUIRE(score(Default(Side::Right, Side::Left)).across < -15.0f);
    // Both in one place: the score under the bar.
    const Place both = Default(Side::Middle, Side::Middle);
    REQUIRE(score(both).across == Approx(0.0f).margin(0.2));
    REQUIRE(score(both).up < bar(both).up - 3.0f);
    // The spread brings the sides in and out; the middle of the frame stays.
    const Place in = Default(Side::Left, Side::Right, 100, 6, 50);
    const Place out = Default(Side::Left, Side::Right, 100, 6, 150);
    REQUIRE(std::abs(bar(in).across) < std::abs(bar(Default()).across));
    REQUIRE(std::abs(bar(out).across) > std::abs(bar(Default()).across));
    REQUIRE(See(0, 0, in).across == Approx(See(0, 0, out).across));
    REQUIRE_FALSE(PlaceFor(100, 6, 100, Side::Left, Side::Right, 0.0f, 1.0f, 0.0f).on);
}
