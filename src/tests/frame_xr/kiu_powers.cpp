// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <array>
#include <bit>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "frame_xr/immersive_layout.h"
#include "frame_xr/kiu_powers.h"
#include "frame_xr/vis/power_menu.h"
#include "video_core/kiu_hud.h"
#include "video_core/kiu_powers.h"

using namespace FrameXr;
namespace G = FrameXr::Kiu::Powers;
namespace P = FrameXr::Vis::Powers;
namespace KP = VideoCore::KiuPowers;
namespace Ui = FrameXr::Kiu::Ui;

namespace {
struct Memory {
    std::map<uint32_t, uint32_t> words;
    bool Read32(uint32_t addr, uint32_t& value) const {
        const auto it = words.find(addr);
        if (it == words.end()) {
            return false;
        }
        value = it->second;
        return true;
    }
};
constexpr uint32_t kCtx = 0x14155330, kP = kCtx + G::kPanel;
/// Chapter 1 as read on the desktop: five powers, the third selected.
Memory Land() {
    Memory m;
    m.words = {{Ui::kBattleUi + Ui::kContext, kCtx}, {kCtx + Ui::kWide, 320},
               {kCtx + Ui::kHigh, 240},              {kCtx + Ui::kPage, 0},
               {kCtx + Ui::kPause, 0},               {kCtx + Ui::kMessage, 0},
               {kCtx + Ui::kMessage + 4, 0},         {kCtx + Ui::kFlags, 1},
               {kCtx + Ui::kVertexBase, 0x24155000}, {kCtx + Ui::kIndexOffset, 0x12C00},
               {kCtx + Ui::kBuffer, 0x14155000},
               {kP + G::kFlags, 0},                  {kP + G::kShown, G::kName},
               {kP + G::kPlace, 2},                  {kP + G::kCount, 5},
               {kP + G::kNameTimer, std::bit_cast<uint32_t>(5.0f)},
               {kP + G::kCharge, 0},                 {kP + G::kEntries + 16, 0x002A2A03}};
    return m;
}
Memory Air(float charge) {
    Memory m = Land();
    m.words[kP + G::kFlags] = G::kAir;
    m.words[kP + G::kCharge] = std::bit_cast<uint32_t>(charge);
    return m;
}
P::Frame FrameOf(const Memory& m) {
    P::Frame f;
    f.game = G::Read(m);
    f.battle = f.tracked = true;
    f.dt_s = 1.0f / 120.0f;
    return f;
}
P::Show::Out Run(P::Show& show, const P::Settings& s, const P::Frame& f, float seconds) {
    P::Show::Out out;
    for (int i = 0; i < static_cast<int>(seconds * 120.0f); ++i) {
        out = show.Update(s, f);
    }
    return out;
}
} // namespace

TEST_CASE("Kid Icarus powers: the code that names the fields", "[frame_xr][kiupowers]") {
    Memory eur, usa;
    for (const auto& c : G::kCodeEur) {
        eur.words[c.addr] = c.word;
    }
    for (const auto& c : Ui::kCodeEur) {
        eur.words[c.addr] = c.word;
    }
    for (const auto& c : G::kCodeUsaRev1) {
        usa.words[c.addr] = c.word;
    }
    for (const auto& c : Ui::kCodeUsaRev1) {
        usa.words[c.addr] = c.word;
    }
    CHECK(G::CodeMatches(eur, false));
    CHECK(G::CodeMatches(usa, true));
    CHECK_FALSE(G::CodeMatches(eur, true));
    eur.words[G::kCodeEur[2].addr] = 0;
    CHECK_FALSE(G::CodeMatches(eur, false));
    for (std::size_t i = 0; i < G::kCodeEur.size(); ++i) {
        CHECK(G::kCodeEur[i].word == G::kCodeUsaRev1[i].word);
    }
}

TEST_CASE("Kid Icarus powers: read on land and in the air", "[frame_xr][kiupowers]") {
    const G::State land = G::Read(Land());
    CHECK(land.known);
    CHECK_FALSE(land.air);
    CHECK(land.count == 5);
    CHECK(land.place == 2);
    CHECK(land.uses == 3);
    CHECK(land.name);
    CHECK(land.quads == 400);
    const G::State air = G::Read(Air(1.52f));
    CHECK(air.known);
    CHECK(air.air);
    CHECK(air.charge == 1.52f);
    SECTION("what makes no sense is not read") {
        Memory m = Land();
        m.words[kP + G::kPlace] = 5;
        CHECK_FALSE(G::Read(m).known);
        m = Land();
        m.words[kP + G::kCount] = 14;
        CHECK_FALSE(G::Read(m).known);
        CHECK_FALSE(G::Read(Air(3.5f)).known);
        CHECK_FALSE(G::Read(Air(std::nanf(""))).known);
        m = Land();
        m.words[kCtx + Ui::kFlags] = 0; // the battle UI draws nothing
        CHECK_FALSE(G::Read(m).known);
        CHECK_FALSE(G::Read(Memory{}).known);
    }
    SECTION("paused") {
        Memory m = Land();
        m.words[kCtx + Ui::kPause] = 1;
        CHECK(G::Read(m).paused);
    }
}

TEST_CASE("Kid Icarus powers: what shows them", "[frame_xr][kiupowers]") {
    const G::State a = G::Read(Land());
    CHECK_FALSE(G::Touched({}, a)); // the first look
    CHECK_FALSE(G::Touched(a, a));
    G::State b = a;
    b.place = 3;
    CHECK(G::Touched(a, b));
    b.count = 4; // one used up: the list moves
    CHECK_FALSE(G::Touched(a, b));
    b = a;
    b.name_timer = 4.0f;
    CHECK_FALSE(G::Touched(b, a)); // its name again
    b = a;
    b.used = true;
    b.uses = 2;
    CHECK_FALSE(G::Touched(a, b));
    CHECK(G::DotChanged(G::Read(Air(1.99f)), G::Read(Air(2.01f))));
    CHECK(G::DotChanged(G::Read(Air(2.01f)), G::Read(Air(1.01f))));
    CHECK_FALSE(G::DotChanged(G::Read(Air(1.5f)), G::Read(Air(1.6f))));
    CHECK_FALSE(G::DotChanged(a, G::Read(Air(1.6f))));
}

TEST_CASE("Kid Icarus powers: the switches", "[frame_xr][kiupowers]") {
    CHECK(P::AtFrom(std::nullopt) == P::At::Hand);
    CHECK(P::AtFrom("bar") == P::At::Bar);
    CHECK(P::AtFrom("middle") == P::At::Middle);
    CHECK(P::AtFrom("score") == P::At::Score);
    CHECK(P::AtFrom("right") == P::At::Score);
    CHECK(P::AtFrom("left") == P::At::Hand);
    CHECK(P::OpacityFrom(std::nullopt) == 1.0f);
    CHECK(P::OpacityFrom("40") == 0.4f);
    CHECK(P::OpacityFrom("10") == 1.0f);
    CHECK(P::OpacityFrom("40%") == 1.0f);
}

TEST_CASE("Kid Icarus powers: over the hand after a press, then away", "[frame_xr][kiupowers]") {
    P::Settings s;
    s.powers = true;
    s.powers_opacity = 0.8f;
    P::Show show;
    P::Frame f = FrameOf(Land());
    CHECK(Run(show, s, f, 1.0f).powers == 0.0f);
    f.game.place = 3;
    CHECK(show.Update(s, f).powers == 0.8f); // at once
    CHECK(Run(show, s, f, 1.4f).powers == 0.8f);
    const float fading = Run(show, s, f, 0.2f).powers;
    CHECK(fading > 0.0f);
    CHECK(fading < 0.8f);
    CHECK(Run(show, s, f, 0.3f).powers == 0.0f);
    SECTION("never with the Bottom Screen, untracked, or out of battle") {
        f.game.place = 4;
        f.bottom_up = true;
        CHECK(show.Update(s, f).powers == 0.0f);
        f.bottom_up = false;
        CHECK(show.Update(s, f).powers == 0.0f); // not later either
        f.game.place = 1;
        f.tracked = false;
        CHECK(show.Update(s, f).powers == 0.0f);
        f.tracked = true;
        f.game.place = 2;
        f.battle = false;
        CHECK(show.Update(s, f).powers == 0.0f);
    }
    SECTION("the aim near it") {
        f.game.place = 4;
        f.aimed = true;
        CHECK(show.Update(s, f).powers < 0.8f);
        f.game.place = 0;
        Run(show, s, f, 0.2f);
        CHECK(std::abs(show.Update(s, f).powers - 0.8f * P::kAimedShare) < 1e-4f);
        f.aimed = false;
        CHECK(std::abs(Run(show, s, f, 0.3f).powers - 0.8f * P::kAimedShare) < 1e-4f);
        CHECK(Run(show, s, f, 0.3f).powers == 0.8f);
    }
}

TEST_CASE("Kid Icarus powers: with the HUD they stay", "[frame_xr][kiupowers]") {
    P::Settings s;
    s.powers = s.dots = true;
    s.at = P::At::Bar;
    P::Show show;
    P::Frame f = FrameOf(Land());
    f.tracked = false;
    f.bottom_up = f.bottom_taken = true;
    f.aimed = true;
    CHECK(Run(show, s, f, 5.0f).powers == 1.0f);
    f.battle = false;
    CHECK(show.Update(s, f).powers == 0.0f);
    f = FrameOf(Air(1.5f));
    f.tracked = false;
    CHECK(Run(show, s, f, 5.0f).powers == 0.0f);
    CHECK(show.Update(s, f).dots == P::kDotsShare);
}

TEST_CASE("Kid Icarus powers: the dots", "[frame_xr][kiupowers]") {
    P::Settings s;
    s.dots = true;
    P::Show show;
    P::Frame f = FrameOf(Air(0.5f));
    CHECK(show.Update(s, f).dots == P::kNoDotShare);
    f.game.charge = 1.0f;
    CHECK(show.Update(s, f).dots == 1.0f);
    CHECK(Run(show, s, f, 0.9f).dots == 1.0f);
    CHECK(Run(show, s, f, 0.2f).dots == P::kDotsShare);
    f.bottom_up = true; // Persistent: they stay
    CHECK(show.Update(s, f).dots == P::kDotsShare);
    f.bottom_taken = true;
    CHECK(show.Update(s, f).dots == 0.0f);
    s.dots = false;
    f.bottom_taken = false;
    CHECK(show.Update(s, f).dots == 0.0f);

    CHECK(P::DotsFor(0.0f) == 1);
    CHECK(P::DotsFor(0.9f) == 1);
    CHECK(P::DotsFor(1.0f) == 2);
    CHECK(P::DotsFor(2.5f) == 3);
    CHECK(P::DotsFor(3.0f) == 3);
    CHECK(P::DotsWidthPx(3) == P::kDotsWide);
    std::vector<uint8_t> px;
    P::PaintDots(1.5f, 1.0f, px);
    REQUIRE(px.size() == static_cast<std::size_t>(P::kDotsWide * P::kDotsHigh * 4));
    const auto at = [&px](int x, int y) { return px.data() + 4 * (y * P::kDotsWide + x); };
    const int middle = P::kDotPad + P::kDotPx / 2, next = P::kDotPx + P::kDotGapPx;
    CHECK(at(middle, middle)[2] > 190); // the full one's blue
    CHECK(at(middle, middle)[0] < 40);
    CHECK(at(middle + next, middle + 20)[0] > 180); // the filling one's lower half
    CHECK(at(middle + next, middle - 20)[0] < 40);
    CHECK(at(middle + next, middle - 20)[3] > 140); // see-through
    CHECK(at(middle + next, middle - 20)[3] < 170);
    CHECK(at(middle + 2 * next, middle)[3] == 0); // no third yet
    CHECK(at(0, 0)[3] == 0);
    // The frame: light, with dark on both sides of it, the empty part of one's too.
    for (const int x0 : {P::kDotPad, P::kDotPad + next}) {
        const int y = P::kDotPad + P::kDotPx / 2 - 10;
        const auto column = [&](float r) { // at this radius, left of the middle
            return x0 + static_cast<int>(48.0f - std::sqrt(r * r / (2.5625f * 2.5625f) - 90.0f));
        };
        CHECK(at(column(111.0f), y)[0] > 200);
        CHECK(at(column(118.5f), y)[0] < 60);
        CHECK(at(column(118.5f), y)[3] > 200);
        CHECK(at(column(103.5f), y)[0] < 60);
        CHECK(at(column(103.5f), y)[3] > 200);
    }
    CHECK(P::kDotsShare >= 0.7f); // full ones stay easy to see
    P::PaintDots(1.5f, 0.5f, px);
    CHECK(at(middle, middle)[3] == 128);
    CHECK(P::DotsKey(1.50f, 1.0f) == P::DotsKey(1.51f, 1.0f));
    CHECK(P::DotsKey(1.50f, 1.0f) != P::DotsKey(1.53f, 1.0f));
    CHECK(P::DotsKey(1.50f, 1.0f) != P::DotsKey(1.50f, 0.9f));
}

TEST_CASE("Kid Icarus powers: over the hand, upright and facing the eyes", "[frame_xr][kiupowers]") {
    Input::Posef aim;
    aim.orientation = Input::FromBasis({0, 0, -1}, {1, 0, 0}, {0, -1, 0}); // any
    aim.position = {-0.25f, -0.4f, -0.45f};
    const Input::Vec3f eye{0, 0, 0};
    const P::Place p = P::OverHand(aim, eye, 0.11f, 13.6f, 136.0f / 64.0f);
    CHECK(std::abs(p.pose.position.y + 0.29f) < 1e-5f);
    const Input::Vec3f right = Input::Rotate(p.pose.orientation, {1, 0, 0});
    CHECK(std::abs(right.y) < 1e-5f); // level
    const Input::Vec3f front = Input::Rotate(p.pose.orientation, {0, 0, 1});
    CHECK(Input::Dot(front, Input::Normalized(eye - p.pose.position)) > 0.9999f);
    CHECK(p.width > 0.13f);
    CHECK(p.width < 0.16f);
    CHECK(std::abs(p.width / p.height - 136.0f / 64.0f) < 1e-4f);
    CHECK(P::OverHand(aim, eye, 0.11f, 13.6f, 2.0f, 0.1f).width == 0.1f);
    aim.position = {0, 0, -2.0f};
    CHECK(std::abs(P::OverHand(aim, eye, 0.11f, 13.6f, 2.0f).width - 0.16f) < 1e-5f);

    Input::Posef ray; // from the eyes, straight ahead
    P::Place ahead{{{0, 0, 0, 1}, {0, 0, -1.0f}}, 0.2f, 0.1f};
    CHECK(P::AngleTo(ray, ahead) < 0.01f);
    ahead.pose.position.x = 0.15f; // its edge 5 cm aside at 1 m
    CHECK(std::abs(P::AngleTo(ray, ahead) - 2.86f) < 0.05f);
    ahead.pose.position = {0, 0, 1.0f};
    CHECK(P::AngleTo(ray, ahead) > 90.0f);
}

TEST_CASE("Kid Icarus powers: with the HUD, clear of its pieces", "[frame_xr][kiupowers]") {
    namespace KH = VideoCore::KiuHud;
    const float tan_x = 1.96f, tan_y = 1.48f;
    const auto clear = [&](const P::Rect& r, const KH::Place& hud) {
        const float t = std::tan(5.9f * 3.14159265f / 180.0f);
        CHECK_FALSE(P::Overlap(r, {-t / tan_x, t / tan_x, -t / tan_y, t / tan_y}));
        const P::Rect bar{hud.bar_across + hud.half_across * KH::kBarLeft,
                          hud.bar_across + hud.half_across * KH::kBarRight,
                          hud.bar_up + hud.half_up * P::kBarBottom, hud.bar_up + hud.half_up};
        CHECK_FALSE(P::Overlap(r, bar));
        const P::Rect score{hud.score_across + hud.half_across * KH::kScoreLeft,
                            hud.score_across + hud.half_across * KH::kScoreRight,
                            hud.score_up + hud.half_up * P::kScoreBottom, hud.score_up + hud.half_up};
        CHECK_FALSE(P::Overlap(r, score));
        CHECK(r.left >= -1.0f);
        CHECK(r.right <= 1.0f);
        CHECK(r.down >= -1.0f);
        CHECK(r.up <= 1.0f);
    };
    for (const bool linked : {true, false}) {
        for (const int size : {40, 100, 200}) {
            for (const float height : {-30.0f, 0.0f, 12.0f, 25.0f}) {
                KH::Wanted w;
                w.size = size;
                w.linked = linked;
                w.height = w.bar_height = w.score_height = height;
                for (const P::At at : {P::At::Bar, P::At::Middle, P::At::Score}) {
                    P::Settings s;
                    s.powers = true;
                    s.at = at;
                    w.link_gap = P::HudGap(s);
                    const KH::Place hud = KH::PlaceFor(w, tan_x, tan_y, 0.1f);
                    const P::Rect r = P::WithHud(at, hud, tan_x, tan_y, 136.0f, 80.0f, 42.0f);
                    INFO("linked " << linked << " size " << size << " height " << height << " at "
                                   << static_cast<int>(at));
                    clear(r, hud);
                }
            }
        }
    }
    const KH::Place hud = KH::PlaceFor({}, tan_x, tan_y, 0.0f);
    const P::Rect bar = P::WithHud(P::At::Bar, hud, tan_x, tan_y, 136.0f, 80.0f, 42.0f);
    CHECK(std::abs(bar.left - (hud.bar_across - hud.half_across)) < 1e-5f); // in line with the bar
    const P::Rect right = P::WithHud(P::At::Score, hud, tan_x, tan_y, 136.0f, 80.0f, 42.0f);
    CHECK(std::abs(right.right - (hud.score_across + hud.half_across)) < 1e-5f);
    const float deg = 2.0f * std::atan(0.5f * (bar.right - bar.left) * tan_x) * 57.2958f;
    CHECK(deg > 14.5f); // about 15.7 degrees wide
    CHECK(deg < 16.5f);
    const KH::Place small = KH::PlaceFor({.size = 50}, tan_x, tan_y, 0.0f);
    const P::Rect half = P::WithHud(P::At::Bar, small, tan_x, tan_y, 136.0f, 80.0f, 42.0f);
    CHECK(std::abs((half.right - half.left) - 0.5f * (bar.right - bar.left)) < 1e-4f);

    // Close: under the bar and the score by a fiftieth of the frame's half height.
    const float gap = 0.02f * hud.half_up;
    CHECK(std::abs(bar.up - (hud.bar_up + hud.half_up * P::kBarBottom - gap)) < 1e-5f);
    CHECK(std::abs(right.up - (hud.score_up + hud.half_up * P::kScoreBottom - gap)) < 1e-5f);
    // In the middle: the linked HUD makes room, the row level with both.
    P::Settings middle;
    middle.dots = true;
    middle.at = P::At::Middle;
    const float more = P::HudGap(middle);
    CHECK(more > 0.4f);
    middle.at = P::At::Score;
    CHECK(P::HudGap(middle) == 0.0f);
    const KH::Place wide = KH::PlaceFor({.link_gap = more}, tan_x, tan_y, 0.0f);
    const P::Rect m = P::WithHud(P::At::Middle, wide, tan_x, tan_y, 136.0f, 80.0f, 42.0f);
    const float bar_right = wide.bar_across + wide.half_across * KH::kBarRight;
    const float score_left = wide.score_across + wide.half_across * KH::kScoreLeft;
    CHECK(m.left - bar_right > 0.0f);
    CHECK(m.left - bar_right < 0.05f * wide.half_across);
    CHECK(std::abs((m.left - bar_right) - (score_left - m.right)) < 1e-5f);
    const float row = 42.0f * P::kHudScale / 120.0f * wide.half_up;
    CHECK(std::abs((m.up - 0.5f * row) - (wide.bar_up + wide.half_up * 0.5f * (1 + P::kBarBottom))) <
          1e-5f);
    // Its unit's middle stays where it is asked for.
    const float unit_left = wide.bar_across + wide.half_across * KH::kBarLeft;
    const float unit_right = wide.score_across + wide.half_across * KH::kScoreRight;
    CHECK(std::abs(unit_left + unit_right) < 1e-5f);
    // Not linked, next to each other: no room, under both.
    KH::Wanted tight;
    tight.linked = false;
    tight.bar_across = -10.0f;
    tight.score_across = 8.0f;
    const KH::Place close = KH::PlaceFor(tight, tan_x, tan_y, 0.0f);
    const P::Rect under = P::WithHud(P::At::Middle, close, tan_x, tan_y, 136.0f, 80.0f, 42.0f);
    CHECK(under.up < close.bar_up + close.half_up * P::kBarBottom);

    // The player's own move: degrees, from the place.
    const P::Rect same = P::Moved(bar, tan_x, tan_y, 0.0f, 0.0f);
    CHECK(same.left == bar.left);
    CHECK(same.up == bar.up);
    const P::Rect moved = P::Moved(bar, tan_x, tan_y, 10.0f, -5.0f);
    const auto degrees = [](float low, float high, float t) {
        return std::atan(0.5f * (low + high) * t) * 57.29578f;
    };
    CHECK(std::abs(degrees(moved.left, moved.right, tan_x) - degrees(bar.left, bar.right, tan_x) -
                   10.0f) < 0.01f);
    CHECK(std::abs(degrees(moved.down, moved.up, tan_y) - degrees(bar.down, bar.up, tan_y) + 5.0f) <
          0.01f);
    CHECK(P::Moved(bar, tan_x, tan_y, 40.0f, 40.0f).up <= 1.0f + 1e-5f);
}

// AzaharFrame kiuhs3a: the power menu in the bar's and the score's plane, and how far its sliders
// go: clear above and clear below both.
TEST_CASE("Kid Icarus powers: on the HUD, its depth and the sliders' reach",
          "[frame_xr][kiuhs3a]") {
    namespace KH = VideoCore::KiuHud;
    namespace L = FrameXr::Immersive::Layout;
    constexpr float kDeg = 3.14159265f / 180.0f;
    constexpr float kHalfIpd = 0.0339f;
    L::HeadLocked s;
    s.tan_half_y = std::tan(56.0f * kDeg);
    s.aspect = std::tan(63.0f * kDeg) / s.tan_half_y;
    s.pitch_rad = -5.1f * kDeg;
    s.distance = 4.57f;
    const float tan_x = s.tan_half_y * s.aspect, tan_y = s.tan_half_y;
    const auto angle = [](const Input::Vec3f& a, const Input::Vec3f& b) {
        const Input::Vec3f c{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        return std::atan2(Input::Length(c), Input::Dot(a, b)) / kDeg;
    };
    for (const bool linked : {true, false}) {
        for (const P::At at : {P::At::Bar, P::At::Middle, P::At::Score}) {
            for (const float height : {10.0f, 22.0f, 30.0f}) {
                INFO("linked " << linked << " at " << static_cast<int>(at) << " height " << height);
                KH::Wanted w;
                w.linked = linked;
                w.height = w.bar_height = w.score_height = height;
                P::Settings set;
                set.powers = true;
                set.at = at;
                w.link_gap = P::HudGap(set);
                const KH::Place hud = KH::PlaceFor(w, tan_x, tan_y, s.pitch_rad);
                const P::Rect place = P::WithHud(at, hud, tan_x, tan_y, 136.0f, 80.0f, 42.0f);
                // The same point of both eyes' screens, as the bar and the score: the same depth.
                Input::Vec3f ray[2];
                for (int eye = 0; eye < 2; eye++) {
                    const Input::Posef screen = L::FullQuadInView(s, 3, eye, kHalfIpd, kHalfIpd);
                    const P::Place q =
                        P::OnScreen(place, screen, 0.5f * s.width(), 0.5f * s.height());
                    CHECK(q.pose.orientation.x == screen.orientation.x); // faces as the screen
                    CHECK(q.pose.orientation.w == screen.orientation.w);
                    CHECK(std::abs(q.width - 0.5f * (place.right - place.left) * s.width()) <
                          1e-4f);
                    ray[eye] = Input::Normalized(q.pose.position -
                                                 Input::Vec3f{L::EyeX(eye, kHalfIpd), 0, 0});
                }
                CHECK(angle(ray[0], ray[1]) < 0.005f); // was 0.49 degrees (one quad, 8 m away)
                // The sliders' ends, in degrees.
                const auto up_deg = [&](float v) {
                    return std::atan(v * tan_y) / kDeg + s.pitch_rad / kDeg;
                };
                const float hud_top =
                    up_deg(std::max(hud.bar_up, hud.score_up) + hud.half_up);
                const float hud_bottom = up_deg(std::min(hud.bar_up, hud.score_up) +
                                                hud.half_up * P::kBarBottom);
                const P::Rect high = P::Moved(place, tan_x, tan_y, 0.0f, 40.0f);
                const P::Rect low = P::Moved(place, tan_x, tan_y, 0.0f, -40.0f);
                CHECK(up_deg(high.down) > hud_top + 1.0f);
                CHECK(up_deg(low.up) < hud_bottom - 20.0f);
                CHECK(high.up <= 1.0f + 1e-5f);
                // Just over the bar and the score: a slider's stop a degree or so clear.
                bool above = false;
                for (int d = 0; d <= 40 && !above; d++) {
                    const P::Rect r =
                        P::Moved(place, tan_x, tan_y, 0.0f, static_cast<float>(d));
                    above = up_deg(r.down) > hud_top && up_deg(r.down) < hud_top + 1.6f;
                }
                CHECK(above);
            }
        }
    }
}

TEST_CASE("Kid Icarus powers: a card over the hand faces the eyes", "[frame_xr][kiupowers]") {
    const Input::Vec3f hand{-0.25f, -0.3f, -0.4f};
    P::Billboard b;
    const Input::Vec3f first = b.Update(hand, {0, 0, 0}, 0.0f);
    CHECK(Input::Length(first - Input::Normalized(Input::Vec3f{} - hand)) < 1e-5f);
    // Small head motion (1 cm): as it was, to the bit.
    const Input::Vec3f still = b.Update(hand, {0.01f, -0.005f, 0.005f}, 0.011f);
    CHECK(still.x == first.x);
    CHECK(still.y == first.y);
    CHECK(still.z == first.z);
    // The head somewhere else: it follows, to within the leash.
    const Input::Vec3f eye{0.4f, 0.1f, 0.2f};
    Input::Vec3f now{};
    float last = 180.0f;
    for (int i = 0; i < 60; ++i) {
        now = b.Update(hand, eye, 0.011f);
        const float off = std::acos(std::min(
                              Input::Dot(now, Input::Normalized(eye - hand)), 1.0f)) * 57.29578f;
        CHECK(off <= last + 1e-3f); // never past it
        last = off;
    }
    CHECK(last < P::kLeashDeg + 0.2f);
    b.Reset();
    CHECK(Input::Length(b.Update(hand, eye, 0.011f) - Input::Normalized(eye - hand)) < 1e-5f);
    // Upright and the same whatever the hand's own turn.
    Input::Posef level{{0, 0, 0, 1}, hand}, tilted{{0.2f, 0.1f, 0.6f, 0.76f}, hand};
    const P::Place p = P::OverHand(level, eye, 0.11f, 13.6f, 1.7f, 0.0f, now);
    const P::Place q = P::OverHand(tilted, eye, 0.11f, 13.6f, 1.7f, 0.0f, now);
    CHECK(p.pose.orientation.w == q.pose.orientation.w);
    CHECK(p.pose.orientation.z == q.pose.orientation.z);
    const Input::Vec3f side = Input::Rotate(p.pose.orientation, {1, 0, 0});
    CHECK(std::abs(side.y) < 1e-4f); // no roll
    const Input::Vec3f out = Input::Rotate(p.pose.orientation, {0, 0, 1});
    CHECK(Input::Length(out - now) < 1e-3f);
    // Under the eyes it does not spin.
    P::Billboard low;
    low.Update({0, -0.5f, -0.05f}, {0, 0, 0}, 0.0f);
    const Input::Vec3f kept = low.Update({0.05f, -0.5f, 0.02f}, {0, 0, 0}, 0.5f);
    CHECK(kept.z > 0.0f);
    CHECK(std::abs(kept.x) < 0.05f);
}

TEST_CASE("Kid Icarus powers: with the HUD the name is under the row", "[frame_xr][kiupowers]") {
    const auto at = [](KP::Part part, float top, float bottom) {
        KP::Sprite s;
        s.part = part;
        s.y = {top, top, bottom, bottom};
        return KP::BelowRow(s);
    };
    CHECK(at(KP::Part::Paper, 198, 240).y[0] == 160.0f); // the row at the strip's top
    CHECK(at(KP::Part::Icon, 203, 235).y[2] == 197.0f);
    // The bubble upside down, its tail's tip up; the name the right way up in its middle.
    CHECK(at(KP::Part::Bubble, 191, 203).y[2] == 199.0f);
    CHECK(at(KP::Part::Bubble, 176, 188).y[0] == 226.0f);
    const KP::Sprite name = at(KP::Part::Name, 180, 195);
    CHECK(name.y[0] == 207.0f);
    CHECK(name.y[2] == 222.0f);
    CHECK(at(KP::Part::Bubble, 165, 180).y[0] <= 240.0f); // popped up
}

TEST_CASE("Kid Icarus powers: the strip under the bottom screen's picture", "[frame_xr][kiupowers]") {
    for (const uint32_t scale : {1u, 4u, 8u}) {
        const uint32_t w = 320 * scale, plain = KP::ImageHeight(scale, false),
                       with = KP::ImageHeight(scale, true);
        CHECK(plain == 240 * scale);
        CHECK(KP::PictureHeight(w, plain) == plain);
        CHECK(KP::PictureHeight(w, with) == plain);
        CHECK(KP::StripIn(w, plain).width == 0);
        const KP::PixelRect r = KP::StripIn(w, with);
        CHECK(r == KP::PixelRect{0, plain + KP::kGapPx, KP::kWide * scale, KP::kHigh * scale});
        CHECK(r.y + r.height == with);
    }
    CHECK(KP::StripIn(0, 0).width == 0);
    CHECK(KP::StripIn(1281, 1232).width == 0);
}

TEST_CASE("Kid Icarus powers: which of the bar's quads is which", "[frame_xr][kiupowers]") {
    // Chapter 1, three powers on view, the name up (the batch in a save state; the draws' census).
    using KP::Part;
    using KP::Tev;
    const auto part = [](float x0, float y0, float x1, float y1, Tev tev = Tev::Modulate,
                         float u0 = 0, float v0 = 0, float u1 = 10, float v1 = 10) {
        // The sheet's pixels from its top left, as the game's coordinates.
        return KP::PartOf({x0, y0, x1, y1, u0 / 256, 1 - v1 / 256, u1 / 256, 1 - v0 / 256, tev});
    };
    CHECK(part(0, 198, 47, 240) == Part::Paper);
    CHECK(part(47, 198, 85, 240) == Part::Paper); // the gold one
    CHECK(part(16, 176, 22, 188, Tev::Modulate, 183, 72, 189, 84) == Part::Bubble);
    CHECK(part(60, 188, 70, 191, Tev::Modulate, 189, 84, 199, 84) == Part::Bubble);
    CHECK(part(109, 191, 116, 203, Tev::Modulate, 199, 84, 206, 96) == Part::Bubble);
    // The bubble larger and higher as a power is used, lower as it goes: still the bubble.
    CHECK(part(6, 165, 14, 180, Tev::Modulate, 183, 72, 189, 84) == Part::Bubble);
    CHECK(part(118, 186, 126, 200, Tev::Modulate, 199, 84, 206, 96) == Part::Bubble);
    // The same place from another part of the sheet is not.
    CHECK(part(16, 176, 22, 188, Tev::Modulate, 10, 211, 48, 249) == Part::Other);
    CHECK(part(49, 202, 87, 240) == Part::Card);
    CHECK(part(49, 203, 81, 235, Tev::Icon) == Part::Icon);
    CHECK(part(75, 224, 87, 238, Tev::Icon) == Part::Count);
    CHECK(part(-2, 210, 11, 227) == Part::Arrow);
    CHECK(part(121, 210, 134, 227) == Part::Arrow);
    CHECK(part(20, 180, 109.5f, 195, Tev::Text) == Part::Name);
    CHECK(part(8, 168, 124, 190, Tev::Text) == Part::Name); // larger, as a power is used
    // Not the bar's: the page's corner, the help at the right, the orbs' numbers, other words.
    CHECK(part(0, 122, 118, 240) == Part::Other);
    CHECK(part(193, 204, 224, 240) == Part::Other);
    CHECK(part(0, 0, 320, 240) == Part::Other);
    CHECK(part(49, 164, 62, 180) == Part::Other);
    CHECK(part(0, 55, 148, 83, Tev::Text) == Part::Other);
    CHECK(part(172, 180, 320, 195, Tev::Text) == Part::Other);
    CHECK(part(49, 203, 81, 235, Tev::None) == Part::Other);
    // Sliding to the next power: a place further out, held to the three places when drawn.
    CHECK(part(-8, 203, 24, 235, Tev::Icon) == Part::Icon);
    CHECK(part(125, 203, 157, 235, Tev::Icon) == Part::Icon);
    CHECK(part(151, 224, 163, 238, Tev::Icon) == Part::Count);
    CHECK(part(222, 203, 254, 235, Tev::Icon) == Part::Other);
    CHECK(KP::Clipped(Part::Icon));
    CHECK(KP::Clipped(Part::Count));
    CHECK_FALSE(KP::Clipped(Part::Arrow));
    CHECK_FALSE(KP::Clipped(Part::Name));
    // No paper but the selected power's gold piece (ABGR), and that one only when asked for.
    const uint32_t white = 0xBFFFFFFF, gold = 0xBF00D2FF;
    CHECK_FALSE(KP::IsGold(Part::Paper, white));
    CHECK(KP::IsGold(Part::Paper, gold));
    CHECK_FALSE(KP::IsGold(Part::Bubble, gold));
    CHECK_FALSE(KP::Wanted(Part::Paper, white, true));
    CHECK(KP::Wanted(Part::Paper, gold, true));
    CHECK_FALSE(KP::Wanted(Part::Paper, gold, false));
    CHECK_FALSE(KP::Wanted(Part::Card, white, true));
    CHECK(KP::Wanted(Part::Icon, 0, false));
    CHECK(KP::Wanted(Part::Name, 0xFF000000, false));
    CHECK_FALSE(KP::Wanted(Part::Other, white, true));
}

TEST_CASE("Kid Icarus powers: how a draw is coloured", "[frame_xr][kiupowers]") {
    using KP::Stage;
    using KP::Tev;
    const Stage pass{0x0FFF0FFF, 0, 0, 0}, hand_on{0x00000EE3, 0, 0, 0};
    const Stage modulate{0x0F030F03, 0, 0x00010001, 0}, icon{0x0FE300F0, 0x200, 0x00010004, 0},
        text{0x0F030FE0, 0x2000, 0x00010000, 0};
    uint32_t at = 9;
    CHECK(KP::TevOf({pass, pass, modulate, pass, pass, pass}, &at) == Tev::Modulate); // the paper
    CHECK(at == 2);
    CHECK(KP::TevOf({pass, hand_on, modulate, pass, pass, pass}) == Tev::Modulate); // the arrows
    CHECK(KP::TevOf({pass, hand_on, icon, pass, pass, pass}, &at) == Tev::Icon);
    CHECK(at == 2);
    CHECK(KP::TevOf({pass, hand_on, text, pass, pass, pass}) == Tev::Text);
    // Anything else is not drawn again: the icons' stage with nothing handing on the texture, the
    // words of other panels (alpha from the texture's alpha), a scale, no stage at all.
    CHECK(KP::TevOf({pass, pass, icon, pass, pass, pass}) == Tev::None);
    CHECK(KP::TevOf({pass, pass, Stage{0x0F030FE0, 0, 0x00010000, 0}, pass, pass, pass}) ==
          Tev::None);
    CHECK(KP::TevOf({pass, pass, Stage{0x0F030F03, 0, 0x00010001, 1}, pass, pass, pass}) ==
          Tev::None);
    CHECK(KP::TevOf({pass, pass, modulate, text, pass, pass}) == Tev::Text);
    CHECK(KP::TevOf({pass, pass, pass, pass, pass, pass}) == Tev::None);
}

TEST_CASE("Kid Icarus powers: the bar's own fade is divided out", "[frame_xr][kiupowers]") {
    using KP::Part;
    using KP::Sprite;
    using KP::Tev;
    const auto sprite = [](Part part, Tev tev, float alpha, float constant = 1.0f) {
        Sprite s;
        s.part = part;
        s.tev = tev;
        s.color[3] = alpha;
        s.constant = constant;
        return s;
    };
    // In full: the gold piece is 0xBF, the icons take the constant, the arrows the vertex.
    std::vector<Sprite> bar{sprite(Part::Paper, Tev::Modulate, KP::kGoldAlpha),
                            sprite(Part::Icon, Tev::Icon, 0.0f, 1.0f),
                            sprite(Part::Arrow, Tev::Modulate, 1.0f)};
    CHECK(KP::BarFade(bar) == 1.0f);
    // Faded to 40 %, the bubble on its own way out: 0.4.
    bar = {sprite(Part::Paper, Tev::Modulate, KP::kGoldAlpha * 0.4f),
           sprite(Part::Icon, Tev::Icon, 0.0f, 0.4f), sprite(Part::Arrow, Tev::Modulate, 0.4f),
           sprite(Part::Bubble, Tev::Modulate, 0.9f)};
    CHECK(std::abs(KP::BarFade(bar) - 0.4f) < 1e-4f);
    // Nothing, or all but gone: nothing to divide by.
    CHECK(KP::BarFade(std::vector<Sprite>{}) == 1.0f);
    CHECK(KP::BarFade(std::vector<Sprite>{sprite(Part::Arrow, Tev::Modulate, 0.01f)}) == 1.0f);

    // Divided out: an idle bar's as opaque as one in full, whatever the player's opacity; the
    // bubble and the name keep theirs.
    CHECK(std::abs(KP::Unfaded(Part::Icon, 0.4f, 0.4f) - 1.0f) < 1e-4f);
    CHECK(std::abs(KP::Unfaded(Part::Paper, KP::kGoldAlpha * 0.4f, 0.4f) - KP::kGoldAlpha) < 1e-4f);
    CHECK(KP::Unfaded(Part::Arrow, 0.5f, 0.4f) == 1.0f);
    CHECK(KP::Unfaded(Part::Bubble, 0.9f, 0.4f) == 0.9f);
    CHECK(KP::Unfaded(Part::Name, 0.3f, 0.4f) == 0.3f);
}

TEST_CASE("Kid Icarus powers: the setting, and the switches over it", "[frame_xr][kiupowers]") {
    P::Switches none;
    CHECK(P::SettingsFor(0, 50, none) == P::Settings{}); // Off
    CHECK(P::SettingsFor(5, 50, none) == P::Settings{}); // not a value of it
    const std::array<P::At, 4> places{P::At::Hand, P::At::Bar, P::At::Middle, P::At::Score};
    for (uint32_t menu = 1; menu <= 4; ++menu) {
        const P::Settings s = P::SettingsFor(menu, 50, none);
        CHECK(s.powers);
        CHECK(s.dots);
        CHECK(s.gold);
        CHECK(s.at == places[menu - 1]);
        CHECK(s.powers_opacity == 0.5f);
        CHECK(s.dots_opacity == 0.5f);
        CHECK(s.stay_s == 1.5f);
    }
    CHECK(P::SettingsFor(1, 5, none).powers_opacity == 0.2f);
    // A switch that is set wins, one by one.
    P::Switches sw;
    sw.powers = "1";
    CHECK(P::SettingsFor(0, 100, sw).powers);
    CHECK_FALSE(P::SettingsFor(0, 100, sw).dots);
    sw.powers = "0";
    sw.at = "right";
    sw.dots_opacity = "40";
    sw.gold = "0";
    const P::Settings s = P::SettingsFor(2, 70, sw);
    CHECK_FALSE(s.powers);
    CHECK(s.dots);
    CHECK(s.at == P::At::Score);
    CHECK(s.powers_opacity == 0.7f);
    CHECK(s.dots_opacity == 0.4f);
    CHECK_FALSE(s.gold);
    sw.dots = "0";
    CHECK_FALSE(P::SettingsFor(2, 70, sw).dots);
}

TEST_CASE("Kid Icarus powers: an arrow is drawn without its edge", "[frame_xr][kiupowers]") {
    namespace KP = VideoCore::KiuPowers;
    KP::Sprite s; // the right one
    s.x = {121, 134, 121, 134};
    s.y = {210, 210, 227, 227};
    s.u = {55 / 256.0f, 68 / 256.0f, 55 / 256.0f, 68 / 256.0f};
    s.v = {1 - 94 / 256.0f, 1 - 94 / 256.0f, 1 - 111 / 256.0f, 1 - 111 / 256.0f};
    const KP::Sprite in = KP::Inset(s);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(std::abs(in.x[i] - (i & 1 ? 133.5f : 121.5f)) < 1e-3f);
        CHECK(std::abs(in.y[i] - (i & 2 ? 226.5f : 210.5f)) < 1e-3f);
        CHECK(std::abs(in.u[i] * 256 - (i & 1 ? 67.5f : 55.5f)) < 1e-3f);
        CHECK(std::abs((1 - in.v[i]) * 256 - (i & 2 ? 110.5f : 94.5f)) < 1e-3f);
    }
}
