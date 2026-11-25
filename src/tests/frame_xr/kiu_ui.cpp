// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <array>
#include <cmath>
#include <map>
#include <utility>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "frame_xr/immersive_layout.h"
#include "frame_xr/kiu_first_person.h"
#include "frame_xr/kiu_ui.h"
#include "video_core/kiu_hud.h"

using namespace FrameXr::Kiu;

namespace {
/// The game's memory as the hooks read it: a word not set is not mapped.
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
constexpr uint32_t kCtx = 0x14155330;
/// A battle in play: the HUD shown, nothing said, not paused.
Memory Battle() {
    Memory m;
    m.words = {{Ui::kBattleUi + Ui::kContext, kCtx}, {kCtx + Ui::kWide, 400},
               {kCtx + Ui::kHigh, 240},              {kCtx + Ui::kPage, 0},
               {kCtx + Ui::kPause, 0},               {kCtx + Ui::kMessage, 0},
               {kCtx + Ui::kMessage + 4, 0},         {kCtx + Ui::kFlags, 1},
               {kCtx + Ui::kVertexBase, 0x24155000}, {kCtx + Ui::kIndexOffset, 0x12C00}};
    return m;
}
} // namespace

TEST_CASE("Kid Icarus UI: the code that names the fields", "[frame_xr][kiu1s]") {
    Memory eur, usa;
    for (const auto& c : Ui::kCodeEur) {
        eur.words[c.addr] = c.word;
    }
    for (const auto& c : Ui::kCodeUsaRev1) {
        usa.words[c.addr] = c.word;
    }
    CHECK(Ui::CodeMatches(eur, false));
    CHECK(Ui::CodeMatches(usa, true));
    CHECK_FALSE(Ui::CodeMatches(eur, true));
    CHECK_FALSE(Ui::CodeMatches(usa, false));
    eur.words[Ui::kCodeEur[7].addr] = 0x10101;
    CHECK_FALSE(Ui::CodeMatches(eur, false));
    for (std::size_t i = 0; i < Ui::kCodeEur.size(); ++i) {
        CHECK(Ui::kCodeEur[i].word == Ui::kCodeUsaRev1[i].word); // the same instructions
        CHECK(Ui::kCodeEur[i].addr != Ui::kCodeUsaRev1[i].addr);
    }
}

TEST_CASE("Kid Icarus UI: what the battle UI shows", "[frame_xr][kiu1s]") {
    Memory m = Battle();
    Ui::State s = Ui::Read(m);
    CHECK(s.known);
    CHECK(s.shown);
    CHECK_FALSE(s.page);
    CHECK_FALSE(s.paused);
    CHECK_FALSE(s.talk);
    CHECK(s.context == kCtx);
    CHECK(s.vertex_base == 0x24155000);
    CHECK(s.index_offset == 0x12C00);
    CHECK(s.quads == 400);

    SECTION("a page instead of the HUD: the results' flag, and the other pages' two") {
        m.words[kCtx + Ui::kPage] = 3;
        CHECK(Ui::Read(m).page);
        m.words[kCtx + Ui::kPage] = 2; // its layout bit alone
        CHECK_FALSE(Ui::Read(m).page);
        for (const uint32_t bit : {0x10000u, 0x100u}) {
            m.words[kCtx + Ui::kFlags] = 1 | bit;
            CHECK(Ui::Read(m).page);
        }
        m.words[kCtx + Ui::kFlags] = 0x10100; // the pass draws nothing
        CHECK_FALSE(Ui::Read(m).page);
    }
    SECTION("the pause screen: either of its two bits") {
        for (const uint32_t bit : {1u, 0x800u}) {
            m.words[kCtx + Ui::kPause] = bit;
            CHECK(Ui::Read(m).paused);
        }
        m.words[kCtx + Ui::kPause] = 0x40000000;
        CHECK_FALSE(Ui::Read(m).paused);
    }
    SECTION("the talk and its screen") {
        m.words[kCtx + Ui::kMessage + 4] = 0x14100000;
        m.words[kCtx + Ui::kMessage] = 0x01000101;
        CHECK(Ui::Read(m).talk);
        CHECK(Ui::Read(m).talk_below);
        m.words[kCtx + Ui::kMessage] = 0x00000101;
        CHECK(Ui::Read(m).talk);
        CHECK_FALSE(Ui::Read(m).talk_below);
        m.words[kCtx + Ui::kMessage] = 0x01000001;
        CHECK_FALSE(Ui::Read(m).talk);
    }
    SECTION("the talk's pictures: the portraits, or an illustration") {
        constexpr uint32_t kUi = kCtx + Ui::kBottom, kPortraits = 0x14200000;
        CHECK_FALSE(Ui::Read(m).pictures); // not there
        m.words[kUi] = 0;
        m.words[kUi + Ui::kPortraits] = kPortraits;
        m.words[kUi + Ui::kIllustration] = 0;
        m.words[kUi + Ui::kPieces] = 0;
        m.words[kPortraits] = 0;
        m.words[kPortraits + Ui::kPlaces] = 0;
        CHECK_FALSE(Ui::Read(m).pictures);
        for (const uint32_t place : {1u, 2u, 4u, 8u, 5u}) {
            m.words[kPortraits + Ui::kPlaces] = place;
            CHECK(Ui::Read(m).pictures);
        }
        m.words[kPortraits + Ui::kPlaces] = 0x10; // none of the four
        CHECK_FALSE(Ui::Read(m).pictures);
        m.words[kPortraits + Ui::kPlaces] = 1;
        for (const uint32_t hidden : {0x1000000u, 0x2000000u}) {
            m.words[kPortraits] = hidden;
            CHECK_FALSE(Ui::Read(m).pictures);
        }
        m.words[kUi + Ui::kIllustration] = 0x3F800000; // 1: over the portraits
        CHECK(Ui::Read(m).pictures);
        m.words[kUi] = Ui::kBottomPieces; // only with one of its pieces up
        CHECK_FALSE(Ui::Read(m).pictures);
        m.words[kUi + Ui::kPieces] = 2;
        m.words[kUi + Ui::kPieces + 4 + 36] = 0x10000;
        CHECK(Ui::Read(m).pictures);
        m.words[kUi + Ui::kPieces + 4 + 36] = 0xFFFEFFFF;
        CHECK_FALSE(Ui::Read(m).pictures);
        m.words[kUi] = 0;
        CHECK(Ui::Read(m).pictures);
        m.words[kUi] = Ui::kBottomOther; // the bottom screen has another picture
        CHECK_FALSE(Ui::Read(m).pictures);
        m.words[kUi] = 0;
        m.words[kCtx + Ui::kFlags] = 1 | Ui::kFlagOtherBottom;
        CHECK_FALSE(Ui::Read(m).pictures);
        m.words[kCtx + Ui::kFlags] = 0; // nothing drawn
        CHECK_FALSE(Ui::Read(m).pictures);
        m.words[kCtx + Ui::kFlags] = 1;
        m.words[kUi + Ui::kIllustration] = 0;
        m.words[kPortraits] = 0;
        CHECK(Ui::Read(m).pictures);
        CHECK_FALSE(Ui::Read(m).illustration); // the portraits alone
        for (const uint32_t bad : {0u, 0x14200002u, 0xE0000000u}) {
            m.words[kUi + Ui::kPortraits] = bad;
            CHECK_FALSE(Ui::Read(m).pictures);
        }
    }
    SECTION("no UI, or no sense in it: nothing known") {
        for (const uint32_t bad : {0u, 0x14155332u, 0xE0000000u, 0x00100000u}) {
            m.words[Ui::kBattleUi + Ui::kContext] = bad;
            CHECK(Ui::Read(m) == Ui::State{});
        }
        m.words[Ui::kBattleUi + Ui::kContext] = kCtx;
        m.words[kCtx + Ui::kWide] = 256;
        CHECK(Ui::Read(m) == Ui::State{});
        m.words[kCtx + Ui::kWide] = 320; // between its passes
        CHECK(Ui::Read(m).known);
        m.words.erase(kCtx + Ui::kMessage);
        CHECK(Ui::Read(m) == Ui::State{});
        CHECK(Ui::Read(Memory{}) == Ui::State{});
    }
    SECTION("a menu outside a battle, by its classes") {
        CHECK(s.menu == Ui::kNoMenu);
        m.words[Ui::kMenuHolder] = 0; // a battle's
        CHECK(Ui::Read(m).menu == Ui::kNoMenu);
        m.words[Ui::kMenuHolder] = 0x149A6930;
        m.words[0x149A6930] = Ui::kMenuEur.holder;
        m.words[0x149A693C] = 0x14012688;
        m.words[0x14012688] = Ui::kMenuEur.body;
        m.words[0x14012688 + Ui::kMenuId] = 0x53544133; // stale text after its byte
        CHECK(Ui::Read(m).menu == 51);
        CHECK(Ui::Read(m, Ui::kMenuUsaRev1).menu == Ui::kNoMenu);
        m.words[Ui::kBattleUi + Ui::kContext] = 0; // known without a battle's UI
        CHECK(Ui::Read(m).menu == 51);
        CHECK_FALSE(Ui::Read(m).known);
        for (const uint32_t id : {0u, 3u, 58u}) {
            m.words[0x14012688 + Ui::kMenuId] = id;
            CHECK(Ui::Read(m).menu == static_cast<int>(id));
        }
        for (const uint32_t id : {59u, 255u}) { // not a number of the game's
            m.words[0x14012688 + Ui::kMenuId] = id;
            CHECK(Ui::Read(m).menu == Ui::kNoMenu);
        }
        m.words[0x14012688] = Ui::kMenuEur.holder;
        CHECK(Ui::Read(m).menu == Ui::kNoMenu);
    }
    SECTION("the batch is left alone when its buffers make no sense") {
        m.words[kCtx + Ui::kVertexBase] = 0x14155000; // not physical
        CHECK(Ui::Read(m).known);
        CHECK(Ui::Read(m).quads == 0);
        m = Battle();
        m.words[kCtx + Ui::kIndexOffset] = 0x12C04; // not two whole buffers
        CHECK(Ui::Read(m).quads == 0);
        m.words[kCtx + Ui::kIndexOffset] = 0xC0 * 1025;
        CHECK(Ui::Read(m).quads == 0);
    }
}

TEST_CASE("Kid Icarus UI: an answer counts from its second VBlank", "[frame_xr][kiu1s]") {
    Ui::Steady s;
    CHECK_FALSE(s.Update(true));
    CHECK_FALSE(s.Update(false));
    CHECK_FALSE(s.Update(true));
    CHECK(s.Update(true));
    CHECK(s.Update(false));
    CHECK(s.Update(true));
    CHECK(s.Update(false));
    CHECK_FALSE(s.Update(false));
}

TEST_CASE("Kid Icarus UI: which 2D is a page, and the pause panel", "[frame_xr][kiu1s]") {
    using namespace FirstPerson;
    CHECK(WholePage(false, false)); // outside a battle, as before
    CHECK(WholePage(false, true));
    CHECK_FALSE(WholePage(true, false));
    CHECK(WholePage(true, true)); // the results while the battle's camera is still in
    CHECK(PausePanelShown(true, false));
    CHECK_FALSE(PausePanelShown(true, true));
    CHECK_FALSE(PausePanelShown(false, false));
}

TEST_CASE("Kid Icarus UI: the page while the battle's camera is in", "[frame_xr][kiu1s]") {
    using namespace VideoCore::KiuHud;
    const Place p = PagePlaceInBattle(std::tan(63.0f * 0.01745329f), std::tan(56.0f * 0.01745329f));
    CHECK(p.on);
    CHECK(p.page);
    // 45 degrees high and 5:3, seen: its edges in the picture.
    CHECK(std::abs(std::atan(p.half_up * std::tan(56.0f * 0.01745329f)) / 0.01745329f - 22.5f) < 0.01f);
    CHECK(std::abs(p.half_across * std::tan(63.0f * 0.01745329f) /
                       (p.half_up * std::tan(56.0f * 0.01745329f)) -
                   kFrameAspect) < 0.001f);
    CHECK_FALSE(PagePlaceInBattle(0.0f, 1.0f).on);
    // Never larger than the picture.
    CHECK(PagePlaceInBattle(0.2f, 0.2f).half_across == 1.0f);
}

TEST_CASE("Kid Icarus UI: a battle's draws as the desktop logged them", "[frame_xr][kiu1s]") {
    // Chapter 2's climb, 2026-11-24: base and indices, first index of each draw of the top screen.
    const VideoCore::KiuHud::Batch b{0x20151B20, 0x12C00, Ui::QuadsFor(0x12C00)};
    const std::pair<uint32_t, uint32_t> hud[] = {{0x20164720, 12}, {0x20164738, 18}, {0x2016475C, 24},
                                                 {0x2016478C, 6},  {0x20164798, 6},  {0x201647A4, 12}};
    uint32_t next = 0x20164720;
    for (const auto& [at, n] : hud) {
        CHECK(VideoCore::KiuHud::InBatch(b, 0x20151B20, at - 0x20151B20, n));
        CHECK(at == next); // one after the other, 13 quads
        next = at + n * 2;
    }
    CHECK(next == 0x20164720 + 13 * 12);
    // The console's battery, of the dialogs' context, which the guess took for the HUD.
    const VideoCore::KiuHud::Draw battery{true, false, true, true, 0x2188B680, 6, 256, 128, 9};
    CHECK(VideoCore::KiuHud::IsBattery(battery)); // still moved with the score
    CHECK_FALSE(VideoCore::KiuHud::IsBattery({true, false, true, true, 0x218B1300, 6, 256, 256, 13}));
    CHECK_FALSE(VideoCore::KiuHud::InBatch(b, 0x20A15828, 0x20A21828 - 0x20A15828, 6));
}

TEST_CASE("Kid Icarus UI: the HUD's draws by the game's own batch", "[frame_xr][kiu1s]") {
    using namespace VideoCore::KiuHud;
    const Batch b{0x24155000, 0x8000, 1024};
    CHECK(InBatch(b, 0x24155000, 0x8000, 6));
    CHECK(InBatch(b, 0x24155000, 0x8000 + 12 * 1023, 6));
    CHECK_FALSE(InBatch(b, 0x24155000, 0x8000 + 12 * 1023, 12)); // past its last quad
    CHECK_FALSE(InBatch(b, 0x24155000, 0x8000 + 6, 6));          // between two quads
    CHECK_FALSE(InBatch(b, 0x24155000, 0x7FF4, 6));
    CHECK_FALSE(InBatch(b, 0x24155000, 0x8000, 4));
    CHECK_FALSE(InBatch(b, 0x24155000, 0x8000, 0));
    CHECK_FALSE(InBatch(b, 0x24156000, 0x8000, 6));
    CHECK_FALSE(InBatch(Batch{}, 0, 0, 6)); // not known

    // A piece of two quads whose own middles fall in two groups goes with its own middle.
    const Place p = PlaceFor({}, std::tan(63.0f * 0.01745329f), std::tan(56.0f * 0.01745329f), 0.0f);
    const auto quad = [](Vertex* v, float a0, float a1) {
        const float up[6] = {0.8f, 0.9f, 0.8f, 0.9f, 0.9f, 0.8f};
        const float at[6] = {a0, a0, a1, a0, a1, a1};
        for (int i = 0; i < 6; ++i) {
            v[i] = {up[i], at[i], 0.2f, 1.0f};
        }
    };
    Vertex whole[12];
    quad(whole, -0.9f, -0.3f);  // the bar's side
    quad(whole + 6, -0.3f, 0.1f); // its end, past where a quad alone counts as the bar's
    MovePiece(whole, 12, p, 1.0f, 1.0f);
    const float a0 = whole[0].y / whole[0].w, joint = whole[2].y / whole[2].w,
                joint2 = whole[6].y / whole[6].w;
    CHECK(joint == joint2); // still joined
    CHECK(a0 < joint);
    Vertex parts[12];
    quad(parts, -0.9f, -0.3f);
    quad(parts + 6, -0.3f, 0.1f);
    Move(parts, 6, p, 1.0f, 1.0f);
    Move(parts + 6, 6, p, 1.0f, 1.0f);
    CHECK(parts[2].y / parts[2].w != parts[6].y / parts[6].w); // quad by quad it came apart
}

TEST_CASE("Kid Icarus UI: a menu counts from its second VBlank, and goes over to the next",
          "[frame_xr][kiu1s]") {
    Ui::SteadyMenu s;
    CHECK(s.Update(3) == Ui::kNoMenu);
    CHECK(s.Update(Ui::kNoMenu) == Ui::kNoMenu);
    CHECK(s.Update(3) == Ui::kNoMenu);
    CHECK(s.Update(3) == 3);
    CHECK(s.Update(1) == 3); // the hub, then the chapters: never none between
    CHECK(s.Update(1) == 1);
    CHECK(s.Update(Ui::kNoMenu) == 1); // a single miss
    CHECK(s.Update(1) == 1);
    CHECK(s.Update(Ui::kNoMenu) == 1);
    CHECK(s.Update(5) == 1); // two different reads are neither
    for (int i = 0; i < 11; ++i) {
        CHECK(s.Update(Ui::kNoMenu) == 1);
    }
    CHECK(s.Update(Ui::kNoMenu) == Ui::kNoMenu); // gone: 12 VBlanks
    CHECK(s.Update(0) == Ui::kNoMenu); // the title is a menu too
    CHECK(s.Update(0) == 0);
}

TEST_CASE("Kid Icarus UI: the game's menus, two screens instead of the view", "[frame_xr][kiu1s]") {
    namespace L = FrameXr::Immersive::Layout;
    L::ScreenState s;
    s.mode = 3;
    s.live = s.view_valid = true;
    CHECK(L::Decide(s).immersive);
    CHECK_FALSE(L::Decide(s).show_bottom);
    s.game_menu = true;
    CHECK_FALSE(L::Decide(s).immersive);
    CHECK(L::Decide(s).show_bottom);
    s.hide_bottom = true; // Hide Bottom Screen: the menus are touched, so shown all the same
    CHECK(L::Decide(s).show_bottom);
    s.hide_bottom = false;
    s.bottom_toggled = true; // the player had it shown already
    CHECK(L::Decide(s).show_bottom);
    s.game_menu = false; // and it is as it was
    CHECK(L::Decide(s).show_bottom);
    CHECK_FALSE(L::Decide(s).immersive);
    s.bottom_toggled = false;
    CHECK(L::Decide(s).immersive);
    // The other modes are left alone.
    s.game_menu = true;
    for (const uint32_t mode : {0u, 1u}) {
        s.mode = mode;
        L::ScreenState without = s;
        without.game_menu = false;
        CHECK(L::Decide(s).immersive == L::Decide(without).immersive);
        CHECK(L::Decide(s).show_bottom == L::Decide(without).show_bottom);
    }
}

TEST_CASE("Kid Icarus UI: the portraits' state and whose the layer is", "[frame_xr][kiu1s]") {
    using namespace FirstPerson;
    using enum PortraitState;
    CHECK(PortraitsFor(false, true, true, false) == Off);
    CHECK(PortraitsFor(true, false, true, false) == Off); // no battle
    CHECK(PortraitsFor(true, true, false, false) == Ready);
    CHECK(PortraitsFor(true, true, true, false) == Up);
    CHECK(PortraitsFor(true, true, true, true) == Ready); // the pause screen is over them
    CHECK(PortraitsHere(Up, false, false, false));
    CHECK(PortraitsHere(Ready, false, false, false)); // its layer, shrunk
    CHECK_FALSE(PortraitsHere(Off, false, false, false));
    CHECK_FALSE(PortraitsHere(Up, true, false, false)); // the Secondary Screen in the view
    CHECK_FALSE(PortraitsHere(Up, false, true, false)); // the pause panel
    CHECK_FALSE(PortraitsHere(Up, false, false, true)); // on the wrist
}

TEST_CASE("Kid Icarus UI: the portraits' part of the Secondary Screen's image", "[frame_xr][kiu1s]") {
    using namespace FirstPerson;
    CHECK(Cut{}.Whole());
    CHECK_FALSE(kPortraitCut.Whole());
    const CutRect r = CutIn(kPortraitCut, 1280, 960);
    CHECK(r.x == 0);
    CHECK(r.y == 168);
    CHECK(r.width == 1280);
    CHECK(r.height == 792); // to its last row
    for (const int k : {1, 2, 3, 5, 10}) { // every Internal Resolution: inside, to the edge
        const CutRect a = CutIn(kPortraitCut, 320 * k, 240 * k);
        CHECK(a.y == 42 * k);
        CHECK(a.y + a.height == 240 * k);
        const CutRect b = CutIn({37, 11, 101, 53}, 320 * k + 1, 240 * k - 1); // not a whole step
        CHECK(b.x >= 0);
        CHECK(b.x + b.width <= 320 * k + 1);
        CHECK(b.y + b.height <= 240 * k - 1);
    }
    // Its place: clear of the middle (15 degrees across, 12 up and down), inside 40 to the side.
    const float half_up =
        0.5f * kPortraitWidthDeg * static_cast<float>(kPortraitCut.height) / kPortraitCut.width;
    CHECK(kPortraitSideDeg - 0.5f * kPortraitWidthDeg >= 15.0f);
    CHECK(kPortraitSideDeg + 0.5f * kPortraitWidthDeg <= 40.0f);
    CHECK(kPortraitDownDeg + half_up < 13.0f); // above the words
    CHECK(kPortraitDownDeg > 0.0f);
}

TEST_CASE("Kid Icarus UI: the menu panel's two screens", "[frame_xr][kiu1s]") {
    using namespace FirstPerson;
    CHECK(MenuPlaceFor(0).width_deg == kMenuPlace.width_deg);
    CHECK(MenuPlaceFor(51).width_deg == kMenuPlace.width_deg);
    CHECK(MenuPlaceFor(Ui::kMenuCauldron).width_deg == kMenuPlaceCauldron.width_deg);
    CHECK(kMenuPlace.width_deg > kMenuPlace.top_width_deg); // the Secondary Screen the large one
    for (const MenuPlace& p : {kMenuPlace, kMenuPlaceCauldron}) {
        // Their edges, in degrees (each faces the eye): apart, and in an easy view.
        const auto half = [](float width_deg, float aspect) {
            return std::atan(std::tan(0.5f * width_deg * 0.01745329f) / aspect) / 0.01745329f;
        };
        const float bottom_top = -p.down_deg + half(p.width_deg, 4.0f / 3.0f);
        const float top_bottom = p.up_deg - half(p.top_width_deg, 5.0f / 3.0f);
        CHECK(top_bottom > bottom_top);
        CHECK(top_bottom - bottom_top < 4.0f);
        CHECK(p.up_deg + half(p.top_width_deg, 5.0f / 3.0f) < 30.0f);
        CHECK(p.down_deg + half(p.width_deg, 4.0f / 3.0f) < 30.0f);
        // Clear of each other by a degree, the letters of both to read (0.7 degrees and more).
        CHECK(MenuGapDeg(p) == Catch::Approx(top_bottom - bottom_top).margin(0.01));
        CHECK(MenuGapDeg(p) >= 1.0f);
        CHECK(LetterDeg(p.width_deg, 320.0f) >= 0.9f);
        CHECK(LetterDeg(p.top_width_deg, 400.0f) >= 0.7f);
        // The one pointed at: its middle below the eyes, its top edge not above them by much.
        CHECK(bottom_top < 4.0f);
    }
}
TEST_CASE("Kid Icarus UI: a panel left where the head was is upright", "[frame_xr][kiu1s]") {
    using namespace FirstPerson;
    const auto mul = [](std::array<float, 4> a, std::array<float, 4> b) {
        return std::array<float, 4>{a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
                                    a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                                    a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
                                    a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
    };
    const auto about = [](int axis, float deg) {
        std::array<float, 4> q{0, 0, 0, std::cos(0.5f * deg * 0.01745329f)};
        q[static_cast<std::size_t>(axis)] = std::sin(0.5f * deg * 0.01745329f);
        return q;
    };
    const auto same = [](std::array<float, 4> a, std::array<float, 4> b) {
        const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
        return std::abs(std::abs(dot) - 1.0f) < 1e-4f;
    };
    const auto no_roll = [](std::array<float, 4> q) { return NoRoll(q[0], q[1], q[2], q[3]); };
    CHECK(same(no_roll({0, 0, 0, 1}), {0, 0, 0, 1}));
    // Turned 40 left, looking 20 down, the head tilted 25: the tilt goes, the rest stays.
    const auto level = mul(about(1, 40.0f), about(0, -20.0f));
    CHECK(same(no_roll(mul(level, about(2, 25.0f))), level));
    CHECK(same(no_roll(level), level));
    // Looking at the floor: no further down than kPanelPitchDeg.
    CHECK(same(no_roll(mul(about(1, -70.0f), about(0, -85.0f))),
               mul(about(1, -70.0f), about(0, -kPanelPitchDeg))));
}
TEST_CASE("Kid Icarus UI: the pause panel's layer through a battle", "[frame_xr][kiu1s]") {
    using namespace FirstPerson;
    using enum PortraitState;
    CHECK(LayerFor(Off, false, true) == Off);
    CHECK(LayerFor(Off, true, false) == Off); // no battle
    CHECK(LayerFor(Off, true, true) == Ready);
    CHECK(LayerFor(Up, true, true) == Up);
    CHECK(LayerFor(Ready, false, true) == Ready);
    // Paused, the panel has the layer; the wrist screen, while it is up, has it instead.
    CHECK_FALSE(PortraitsHere(Ready, false, true, false));
    CHECK(PausePanelShown(true, false));
    CHECK_FALSE(PausePanelShown(true, true));
}
