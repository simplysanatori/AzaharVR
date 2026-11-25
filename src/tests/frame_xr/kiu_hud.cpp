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
    // `across` to the right: clip's y is to the left.
    Vertex t[3] = {{up, -across, 0.2f, 1.0f},
                   {up + 1e-4f, -across, 0.2f, 1.0f},
                   {up, -across + 1e-4f, 0.2f, 1.0f}};
    Move(t, 3, p, f, fy);
    REQUIRE(t[0].w == 2.0f);
    REQUIRE(t[0].z / t[0].w == Approx(0.2f));
    return {std::atan(-t[0].y / t[0].w / f * kTanX) / kRad,
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

TEST_CASE("Kid Icarus HUD: what a draw is, by what is known of the game's 2D",
          "[frame_xr][kiuhud]") {
    const Draw piece{true, false, true, true, 0x218D1300, 42, 256, 256, 9};
    const Draw battery{true, false, true, true, 0x2188B680, 6, 256, 128, 9};
    const Draw subtitle{true, false, true, true, 0x181A8000, 12, 512, 128, 0};
    const Draw list{true, false, true, true, 0x181A8000, 24, 512, 128, 0};
    Draw line{true, false, true, false, 0, 12}; // a page's rule: no texture
    Draw mark{true, false, false, true, 0x18400000, 30, 256, 256, 12}; // added light, from VRAM
    Draw glyphs{true, false, true, true, 0x27C06080, 60, 128, 32, 11}; // a menu's words

    SECTION("the batch not known (another build, the code differs): the old way") {
        CHECK(Decide(piece, false, false, false, false) == As::Guess);
        CHECK(Decide(battery, false, false, false, false) == As::WithScore);
        CHECK(Decide(subtitle, false, false, false, false) == As::Subtitle);
        CHECK(Decide(list, false, false, false, false) == As::Guess);
        CHECK(Decide(line, false, false, false, false) == As::Not);
        CHECK(Decide(mark, false, false, false, false) == As::Not);
        CHECK(Decide(piece, false, false, false, true) == As::Not); // the reticle
    }
    SECTION("a battle whose batch is known: its draws and the battery, no others") {
        CHECK(Decide(piece, true, true, false, false) == As::Piece);
        CHECK(Decide(line, true, true, false, false) == As::Piece); // whatever its state
        CHECK(Decide(subtitle, true, true, false, false) == As::Subtitle);
        CHECK(Decide(list, true, true, false, false) == As::Piece);
        CHECK(Decide(battery, false, true, false, false) == As::WithScore);
        CHECK(Decide(piece, false, true, false, false) == As::Not); // another context's
        CHECK(Decide(mark, false, true, false, false) == As::Not);
        CHECK(Decide(piece, true, true, false, true) == As::Not);
    }
    SECTION("a page: the battle UI's draws whatever their state, the others by the guess") {
        CHECK(Decide(line, true, true, true, false) == As::Page);
        CHECK(Decide(piece, true, true, true, false) == As::Page);
        CHECK(Decide(list, false, true, true, false) == As::Page);
        CHECK(Decide(battery, false, true, true, false) == As::Page);
        CHECK(Decide(glyphs, false, false, true, false) == As::Page); // the hub: no battle UI
        CHECK(Decide(battery, false, false, true, false) == As::Page);
        CHECK(Decide(line, false, false, true, false) == As::Not);
        CHECK(Decide(mark, false, false, true, false) == As::Not);
    }
}

TEST_CASE("Kid Icarus HUD: the battery and the health bar's ornament, as the census has them",
          "[frame_xr][kiuhud]") {
    // Clip's y is to the left: the health bar 0.3 to 1, the score and the battery -1 to -0.6.
    const auto quad = [](Vertex* v, float u0, float u1, float y0, float y1) {
        const float up[6] = {u0, u1, u0, u1, u1, u0};
        const float at[6] = {y0, y0, y1, y0, y1, y1};
        for (int i = 0; i < 6; ++i) {
            v[i] = {up[i], at[i], 0.1f, 1.0f};
        }
    };
    const Place p = At();
    Vertex end[6], wing[6], score[6], battery[6], whole[6];
    quad(end, 0.8f, 0.95f, 0.3f, 0.45f); // the ornament's right end: it was left behind
    quad(wing, 0.7f, 0.9f, 0.8f, 1.0f);
    quad(score, 0.6f, 1.0f, -1.0f, -0.6f);
    quad(battery, 0.9f, 1.0f, -1.0f, -0.9f);
    quad(whole, 0.9f, 1.0f, -1.0f, -0.9f);
    CHECK(GroupAt(0.875f, -0.375f) == Group::Bar);
    CHECK(GroupAt(0.8f, 0.8f) == Group::Score);
    Move(end, 6, p, 1.0f, 1.0f);
    Move(wing, 6, p, 1.0f, 1.0f);
    MovePiece(score, 6, p, 1.0f, 1.0f);
    Move(battery, 6, p, 1.0f, 1.0f);
    MoveAs(whole, 6, p, 1.0f, 1.0f, Group::Score);
    // Their places on view are theirs in the game's frame, to one another.
    CHECK(wing[2].y / 2.0f - end[0].y / 2.0f == Approx(p.half_across * 0.7f).margin(1e-4));
    CHECK(wing[0].x / 2.0f - end[0].x / 2.0f == Approx(p.half_up * -0.1f).margin(1e-4));
    CHECK(battery[0].y / 2.0f == Approx(score[0].y / 2.0f).margin(1e-4));
    CHECK(battery[1].x / 2.0f == Approx(score[1].x / 2.0f).margin(1e-4));
    for (int i = 0; i < 6; ++i) {
        CHECK(whole[i].x == battery[i].x);
        CHECK(whole[i].y == battery[i].y);
    }
    // The bar left of the score.
    CHECK(end[0].y > score[2].y);
}

TEST_CASE("Kid Icarus HUD: a page's draw, quad by quad the same way; the dim stays whole",
          "[frame_xr][kiuhud]") {
    const auto quad = [](Vertex* v, float u0, float u1, float a0, float a1) {
        const float up[6] = {u0, u1, u0, u1, u1, u0};
        const float at[6] = {a0, a0, a1, a0, a1, a1};
        for (int i = 0; i < 6; ++i) {
            v[i] = {up[i], at[i], 0.3f, 1.0f};
        }
    };
    const Place p = PagePlaceInBattle(kTanX, kTanY);
    REQUIRE(p.page);
    Vertex v[18];
    quad(v, -0.96f, 1.0f, -1.0f, 1.02f);   // the dim over the picture
    quad(v + 6, 0.6f, 0.9f, -1.0f, -0.3f); // where the bar's group would be
    quad(v + 12, -0.9f, -0.6f, 0.5f, 1.0f); // the frame's lower part
    CHECK(MovePage(v, 18, p, 1.0f, 1.0f) == 18);
    for (int i = 0; i < 6; ++i) { // the shader keeps it over the whole view
        CHECK(v[i].w == 1.0f);
        CHECK(std::abs(v[i].x) == 1.0f);
        CHECK(std::abs(v[i].y) == 1.0f);
    }
    for (int i = 6; i < 18; ++i) {
        CHECK(v[i].w == 2.0f);
    }
    // One scale and one middle for all of it: the layout is the game's.
    CHECK(v[6].x / 2.0f == Approx(p.half_up * 0.6f));
    CHECK(v[6].y / 2.0f == Approx(p.half_across * -1.0f));
    CHECK(v[12].x / 2.0f == Approx(p.half_up * -0.9f));
    CHECK(v[12].y / 2.0f == Approx(p.half_across * 0.5f));
    Vertex world[6];
    quad(world, 0.0f, 0.2f, 0.0f, 0.2f);
    world[3].w = 7.0f;
    CHECK(MovePage(world, 6, p, 1.0f, 1.0f) == 0);
}

TEST_CASE("Kid Icarus HUD: a menu's 2D in the view, in the HUD's frame", "[frame_xr][kiuhud]") {
    Wanted w;
    const Place hud = At(w), p = PageInView(w, kTanX, kTanY, -5.0f * kRad);
    REQUIRE(p.on);
    REQUIRE(p.page);
    CHECK(p.half_across == hud.half_across);
    CHECK(p.half_up == hud.half_up);
    // The frame's corners: 30 degrees aside at HUD Size 100 %, whatever the game draws past it.
    for (const float f : {1.0f, 2.8f, 5.0f}) {
        Vertex v[3] = {{-0.8f, 1.0f, 0, 1}, {-0.7f, 1.0f, 0, 1}, {-0.8f, 0.9f, 0, 1}};
        CHECK(MovePage(v, 3, p, f, f) == 3);
        CHECK(std::atan(v[0].y / v[0].w / f * kTanX) / kRad == Approx(30.0f).margin(0.01f));
        const float up = std::atan(v[0].x / v[0].w / f * kTanY) / kRad - 5.0f;
        CHECK(up < kMiddleDeg);
        CHECK(up > kMiddleDeg - 20.0f);
    }
    w.size = 150;
    CHECK(PageInView(w, kTanX, kTanY, 0.0f).half_across > p.half_across);
    CHECK_FALSE(PageInView(w, 0.0f, kTanY, 0.0f).on);
}

TEST_CASE("Kid Icarus HUD: the hub's cards go with its words; the veil has no border",
          "[frame_xr][kiuhud]") {
    Draw d;
    d.top_viewport = d.depth_test = d.blended = d.textured = true;
    d.vertices = 6;
    d.width = d.height = 64;
    d.format = 4;
    CHECK(MayBeCard(d));
    Draw room = d;
    room.vertices = 24;
    CHECK_FALSE(MayBeCard(room));
    room = d;
    room.depth_test = false;
    CHECK_FALSE(MayBeCard(room));

    const Place p = PageInView(Wanted{}, kTanX, kTanY, 0.0f);
    REQUIRE(p.page);
    REQUIRE(p.cards);
    CHECK_FALSE(PagePlace(4.0f, 4.0f).cards);
    CHECK_FALSE(PagePlaceInBattle(kTanX, kTanY).cards);

    // The bag's middle and a digit at the same place of the frame land on the same place.
    const float w = 40.0f;
    Vertex bag[3] = {{-0.65f * w, -0.8f * w, -0.5f * w, w},
                     {-0.80f * w, -1.0f * w, -0.5f * w, w},
                     {-0.10f * w, -0.6f * w, -0.5f * w, w}};
    REQUIRE(IsCard(bag, 3));
    MoveCard(bag, 3, p, 3.0f, 4.0f, -1.0f);
    Vertex digit[6] = {{-0.65f, -0.8f, 0, 1}, {-0.6f, -0.8f, 0, 1}, {-0.65f, -0.7f, 0, 1},
                       {-0.6f, -0.8f, 0, 1},  {-0.6f, -0.7f, 0, 1}, {-0.65f, -0.7f, 0, 1}};
    REQUIRE(MovePage(digit, 6, p, 3.0f, 4.0f) == 6);
    CHECK(bag[0].x / bag[0].w == Approx(digit[0].x / digit[0].w));
    CHECK(bag[0].y / bag[0].w == Approx(digit[0].y / digit[0].w));
    CHECK(bag[0].w == w);
    CHECK(bag[0].z / w == Approx(-1.0f + 0.5f * kCardDepth)); // in front of the room

    Vertex room_quad[2] = {{0, 0, 0, 131.0f}, {0, 0, 0, 206.0f}};
    CHECK_FALSE(IsCard(room_quad, 2)); // not at one distance
    Vertex flat_quad[2] = {{0, 0, 0, 1.0f}, {0, 0, 0, 1.0f}};
    CHECK_FALSE(IsCard(flat_quad, 2));

    Vertex veil[12];
    for (int i = 0; i < 12; ++i) {
        veil[i] = {(i % 2 ? -0.67f : -1.32f) * w, (i % 3 ? 1.24f : -1.36f) * w, -0.5f * w, w};
    }
    REQUIRE(IsVeil(veil, 12));
    CHECK_FALSE(IsVeil(bag, 3));
    const auto soft = SoftVeil(veil, 12);
    float inside = 0.0f;
    for (const auto& q : soft) {
        const float up = q.v.x / w, at = q.v.y / w;
        const bool rim = up < -1.319f || up > -0.671f || at < -1.359f || at > 1.239f;
        CHECK(q.alpha == (rim ? 0.0f : 1.0f));
        inside += q.alpha;
    }
    CHECK(inside == 6.0f + 4 * 3.0f + 4 * 2.0f); // the middle, the sides, two a corner
}
