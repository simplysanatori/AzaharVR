// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <bit>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "frame_xr/kiu_powers.h"
#include "frame_xr/vis/power_menu.h"
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
    b = a;
    b.name_timer = 4.0f;
    CHECK_FALSE(G::Touched(a, b));
    CHECK(G::Touched(b, a)); // asked for again
    b = a;
    b.used = true;
    CHECK(G::Touched(a, b));
    CHECK_FALSE(G::Touched(b, b));
    CHECK(G::DotChanged(G::Read(Air(1.99f)), G::Read(Air(2.01f))));
    CHECK(G::DotChanged(G::Read(Air(2.01f)), G::Read(Air(1.01f))));
    CHECK_FALSE(G::DotChanged(G::Read(Air(1.5f)), G::Read(Air(1.6f))));
    CHECK_FALSE(G::DotChanged(a, G::Read(Air(1.6f))));
}

TEST_CASE("Kid Icarus powers: the switches", "[frame_xr][kiupowers]") {
    CHECK(P::AtFrom(std::nullopt) == P::At::Hand);
    CHECK(P::AtFrom("bar") == P::At::Bar);
    CHECK(P::AtFrom("right") == P::At::Right);
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
                const KH::Place hud = KH::PlaceFor(w, tan_x, tan_y, 0.1f);
                for (const P::At at : {P::At::Bar, P::At::Right}) {
                    const P::Rect r = P::WithHud(at, hud, tan_x, tan_y, 136.0f, 64.0f);
                    INFO("linked " << linked << " size " << size << " height " << height << " at "
                                   << static_cast<int>(at));
                    clear(r, hud);
                }
            }
        }
    }
    const KH::Place hud = KH::PlaceFor({}, tan_x, tan_y, 0.0f);
    const P::Rect bar = P::WithHud(P::At::Bar, hud, tan_x, tan_y, 136.0f, 64.0f);
    CHECK(std::abs(bar.left - (hud.bar_across - hud.half_across)) < 1e-5f); // in line with the bar
    const P::Rect right = P::WithHud(P::At::Right, hud, tan_x, tan_y, 136.0f, 64.0f);
    CHECK(std::abs(right.right - (hud.score_across + hud.half_across)) < 1e-5f);
    const float deg = 2.0f * std::atan(0.5f * (bar.right - bar.left) * tan_x) * 57.2958f;
    CHECK(deg > 14.5f); // about 15.7 degrees wide
    CHECK(deg < 16.5f);
    const KH::Place small = KH::PlaceFor({.size = 50}, tan_x, tan_y, 0.0f);
    const P::Rect half = P::WithHud(P::At::Bar, small, tan_x, tan_y, 136.0f, 64.0f);
    CHECK(std::abs((half.right - half.left) - 0.5f * (bar.right - bar.left)) < 1e-4f);
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
}
