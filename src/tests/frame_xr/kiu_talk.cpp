// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <bit>
#include <map>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "frame_xr/kiu_talk.h"
#include "video_core/kiu_powers.h"
#include "video_core/kiu_talk.h"

using namespace FrameXr;
namespace T = FrameXr::Kiu::Talk;
namespace Ui = FrameXr::Kiu::Ui;
namespace KT = VideoCore::KiuTalk;
namespace KP = VideoCore::KiuPowers;

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
constexpr uint32_t F(float v) {
    return std::bit_cast<uint32_t>(v);
}
constexpr uint32_t kCtx = 0x14155330, kUi = kCtx + Ui::kBottom, kFaces = 0x14200000;
/// Two speakers, as in Chapter 2's flight.
Memory Two(uint32_t left = 0xFFFFFFFF, uint32_t right = 0xFFFFFFFF, float wipe = 0.0f) {
    Memory m;
    m.words = {{kUi + Ui::kPortraits, kFaces}, {kFaces, 0}, {kFaces + Ui::kPlaces, 3}};
    for (uint32_t i = 0; i < 2; ++i) {
        const uint32_t place = kFaces + T::kFirstPlace + T::kPlaceSize * i;
        m.words[place + T::kPlaceColor] = i == 0 ? left : right;
        m.words[place + T::kPlaceWipe] = F(wipe);
    }
    return m;
}
std::array<KT::Stage, 6> PortraitTev() {
    std::array<KT::Stage, 6> s{};
    s[1] = {0x0F330FE3, 0x00042001, 0x00020001, 1, 0x00AAAAAA};
    s[2] = {0x00FF0F0F, 0x00000001, 0x00090001, 0, 0};
    return s;
}
KT::Quad Rect(float x0, float y0, float x1, float y1, uint32_t color = 0xFFFFFFFF) {
    KT::Quad q;
    q.x = {x0, x1, x0, x1};
    q.y = {y0, y0, y1, y1};
    q.u = {0, 1, 0, 1};
    q.v = {1, 1, 0, 0};
    q.color = {color, color, color, color};
    return q;
}
KT::DrawIn Portrait(KT::Quad q = Rect(-5, 0, 251, 256)) {
    KT::DrawIn d;
    d.textured = d.over = true;
    d.width = d.height = 256;
    d.format = KT::kEtc1;
    d.texture = 0x21B20780;
    d.tev = KT::Tev::Portrait;
    d.quads = 1;
    d.first = q;
    return d;
}
KT::DrawIn Flat(uint32_t quads, KT::Quad q) {
    KT::DrawIn d;
    d.over = true;
    d.quads = quads;
    d.first = q;
    return d;
}
KT::DrawIn Piece(KT::Quad q = Rect(53, 56, 273, 184)) {
    KT::DrawIn d = Flat(1, q);
    d.textured = true;
    d.width = 256;
    d.height = 128;
    d.format = 3;
    d.texture = 0x21ACA700;
    d.tev = KT::Tev::Plain;
    return d;
}
} // namespace

TEST_CASE("Kid Icarus talk: the strip in the bottom screen's image", "[frame_xr][kiutalk]") {
    for (uint32_t k = 1; k <= 8; ++k) {
        for (const bool powers : {false, true}) {
            const uint32_t w = 320 * k, without = KP::ImageHeight(k, powers);
            const uint32_t with = KT::ImageHeight(without, k, true);
            CHECK(KT::ImageHeight(without, k, false) == without);
            CHECK(KT::StripIn(w, without) == KT::PixelRect{});
            CHECK(KT::Without(w, without) == without);
            CHECK(with <= KT::kMostHigh);
            if (with == without) { // too high
                CHECK(without + KT::ExtraHeight(k) > KT::kMostHigh);
                continue;
            }
            CHECK(KT::StripIn(w, with) == KT::PixelRect{0, with - 240 * k, w, 240 * k});
            CHECK(KT::Without(w, with) == without);
            // The powers' strip and the picture are where they were.
            CHECK(KP::StripIn(w, with) == KP::StripIn(w, without));
            CHECK(KP::PictureHeight(w, with) == 240 * k);
        }
    }
    CHECK(KT::StripIn(1281, 1928) == KT::PixelRect{});
    CHECK(KT::StripIn(1280, 1900) == KT::PixelRect{});
    CHECK(KP::PictureHeight(1000, 750) == 750);
    // In the strip's quad a row is as high, and where it is, as in the card's.
    const KT::Grown g = KT::GrownFrom(198.0f);
    CHECK(g.height == Catch::Approx(240.0f));
    CHECK(0.5f * g.height + g.up == Catch::Approx(0.5f * 198.0f + KT::kCardTop));
    CHECK(-0.5f * g.height + g.up == Catch::Approx(-0.5f * 198.0f));
}

TEST_CASE("Kid Icarus talk: how the game colours its draws", "[frame_xr][kiutalk]") {
    uint32_t constant = 0;
    CHECK(KT::TevOf(PortraitTev(), &constant) == KT::Tev::Portrait);
    CHECK(constant == 0xAAAAAA);
    auto paper = PortraitTev(); // the power bar's: the same first stage left over
    paper[1].scales = 0;
    paper[2] = {0x0F030F03, 0, 0x00010001, 0, 0};
    CHECK(KT::TevOf(paper) == KT::Tev::Plain);
    auto other = PortraitTev();
    other[1].scales = 0;
    CHECK(KT::TevOf(other) == KT::Tev::None);
    other = PortraitTev();
    other[2].ops = 0x00010001;
    CHECK(KT::TevOf(other) == KT::Tev::None);
    other = PortraitTev();
    std::swap(other[1], other[0]); // a stage between
    CHECK(KT::TevOf(other) == KT::Tev::None);
    std::array<KT::Stage, 6> text{}; // the words, the icons
    text[2] = {0x0F030FE0, 0x2000, 0x00010000, 0, 0};
    CHECK(KT::TevOf(text) == KT::Tev::None);
    text[2] = {0x0FE300F0, 0x200, 0x00010004, 0, 0xFFFFFFFF};
    CHECK(KT::TevOf(text) == KT::Tev::None);
    CHECK(KT::TevOf({}) == KT::Tev::None);
}

TEST_CASE("Kid Icarus talk: the portraits' draws", "[frame_xr][kiutalk]") {
    CHECK(KT::PartOf(Portrait(), false) == KT::Part::Portrait);
    CHECK(KT::PartOf(Portrait(Rect(69, 0, 325, 256)), false) == KT::Part::Portrait);
    CHECK(KT::PartOf(Portrait(Rect(325, -20, 69, 236, 0x80808080)), false) == KT::Part::Portrait);
    // Wiped in: two quads, the upper one's top see-through.
    KT::DrawIn wiped = Portrait(Rect(-35, 88, 221, 152));
    wiped.quads = 2;
    wiped.first.color = {0x00FFFFFF, 0x00FFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    CHECK(KT::PartOf(wiped, false) == KT::Part::Portrait);
    CHECK(KT::SideOf(Rect(-5, 0, 251, 256)) == KT::Kind::PortraitLeft);
    CHECK(KT::SideOf(Rect(325, 0, 69, 256)) == KT::Kind::PortraitRight);

    const auto not_one = [](auto&& change) {
        KT::DrawIn d = Portrait();
        change(d);
        return KT::PartOf(d, false) == KT::Part::Other;
    };
    CHECK(not_one([](KT::DrawIn& d) { d.tev = KT::Tev::Plain; }));
    CHECK(not_one([](KT::DrawIn& d) { d.over = false; }));
    CHECK(not_one([](KT::DrawIn& d) { d.format = 9; }));
    CHECK(not_one([](KT::DrawIn& d) { d.width = 128; }));
    CHECK(not_one([](KT::DrawIn& d) { d.texture = 0x181A8000; }));
    CHECK(not_one([](KT::DrawIn& d) { d.quads = 3; }));
    CHECK(not_one([](KT::DrawIn& d) { d.first = Rect(0, 0, 38, 38); }));
    CHECK(not_one([](KT::DrawIn& d) { d.first.x[2] = std::bit_cast<float>(0x7FC00000u); }));
}

TEST_CASE("Kid Icarus talk: an illustration's draws", "[frame_xr][kiutalk]") {
    const KT::DrawIn black = Flat(1, Rect(0, 55, 320, 185, 0xFF000000));
    CHECK(KT::PartOf(black, false) == KT::Part::IllustrationOpens);
    CHECK(KT::PartOf(Flat(1, Rect(0, 100, 320, 140, 0xFF000000)), false) ==
          KT::Part::IllustrationOpens); // opening
    CHECK(KT::PartOf(Flat(1, Rect(0, 0, 320, 240, 0xFF000000)), false) == KT::Part::Other); // a fade
    CHECK(KT::PartOf(Flat(1, Rect(0, 55, 320, 185, 0xCC000000)), false) == KT::Part::Other);
    CHECK(KT::PartOf(Flat(3, Rect(0, 0, 320, 64, 0xFF50869C)), false) == KT::Part::Other);
    CHECK(KT::PartOf(Piece(), false) == KT::Part::Other); // nothing without the black
    CHECK(KT::PartOf(Piece(), true) == KT::Part::Piece);
    CHECK(KT::PartOf(Flat(4, Rect(0, 0, 320, 55, 0xFF1F1D16)), true) ==
          KT::Part::IllustrationCloses);
    CHECK(KT::PartOf(Portrait(), true) == KT::Part::Wrong);
    KT::DrawIn text = Piece();
    text.tev = KT::Tev::None;
    CHECK(KT::PartOf(text, true) == KT::Part::Wrong);
    CHECK(KT::PartOf(Flat(1, Rect(0, 0, 320, 55)), true) == KT::Part::Wrong);
}

TEST_CASE("Kid Icarus talk: what the game says it draws", "[frame_xr][kiutalk]") {
    Memory eur, usa;
    for (const auto& c : T::kCodeEur) {
        eur.words[c.addr] = c.word;
    }
    for (const auto& c : T::kCodeUsaRev1) {
        usa.words[c.addr] = c.word;
    }
    CHECK(T::CodeMatches(eur, false));
    CHECK(T::CodeMatches(usa, true));
    CHECK_FALSE(T::CodeMatches(eur, true));
    eur.words[0x9E3114] = F(305.0f);
    CHECK_FALSE(T::CodeMatches(eur, false));

    const auto portraits = [](const Memory& m) {
        const KT::Expected e = T::ExpectedOf(m, kCtx, true, false);
        CHECK(e.known);
        return e.portraits;
    };
    CHECK(T::ExpectedOf(Memory{}, kCtx, false, false) == KT::Expected{true, 0, false});
    CHECK_FALSE(T::ExpectedOf(Memory{}, kCtx, true, false).known);
    CHECK(portraits(Two()) == 2);
    CHECK(portraits(Two(0xFF808080)) == 2);          // dimmed
    CHECK(portraits(Two(0x00FFFFFF)) == 1);          // not in yet
    CHECK(portraits(Two(~0u, ~0u, 0.5f)) == 2);      // half wiped
    CHECK(portraits(Two(~0u, ~0u, 1.0f)) == 0);      // wiped away
    Memory hidden = Two();
    hidden.words[kFaces] = 0x1000000;
    CHECK(portraits(hidden) == 0);

    // An illustration over them.
    Memory m = Two();
    m.words[kUi + Ui::kPieces] = 1;
    m.words[kUi + T::kFirstPiece + T::kPieceAlpha] = F(1.0f);
    m.words[kUi + Ui::kIllustration] = F(1.0f);
    CHECK(T::ExpectedOf(m, kCtx, true, true) == KT::Expected{true, 0, true});
    m.words[kUi + Ui::kIllustration] = F(0.5f); // coming in: its picture not yet
    CHECK(T::ExpectedOf(m, kCtx, true, true) == KT::Expected{true, 2, false});
    m.words[kUi + Ui::kIllustration] = F(0.9f);
    CHECK(T::ExpectedOf(m, kCtx, true, true) == KT::Expected{true, 2, true});
    m.words[kUi + T::kFirstPiece + T::kPieceAlpha] = 0;
    CHECK(T::ExpectedOf(m, kCtx, true, true) == KT::Expected{true, 2, false});
}

TEST_CASE("Kid Icarus talk: today's card when a draw can't be told", "[frame_xr][kiutalk]") {
    const KT::Expected two{true, 2, false};
    KT::Choice c;
    CHECK_FALSE(c.Card(two, {2, 0, false}));
    for (int i = 0; i < KT::kMisses - 1; ++i) { // a picture or two apart: nothing changes
        CHECK_FALSE(c.Card(two, {1, 0, false}));
    }
    CHECK_FALSE(c.Card(two, {2, 0, false}));
    for (int i = 0; i < KT::kMisses - 1; ++i) {
        CHECK_FALSE(c.Card(two, {1, 0, false}));
    }
    CHECK(c.Card(two, {1, 0, false}));
    for (int i = 0; i < KT::kMisses - 1; ++i) {
        CHECK(c.Card(two, {2, 0, false}));
    }
    CHECK_FALSE(c.Card(two, {2, 0, false}));
    CHECK_FALSE(c.Card({true, 0, false}, {2, 0, false})); // more than it says: still theirs
    CHECK(c.Card({}, {2, 0, false}));                     // another game's code: at once

    KT::Choice wrong, piece;
    bool w = false, p = false;
    for (int i = 0; i < KT::kMisses; ++i) {
        w = wrong.Card(two, {2, 0, true});
        p = piece.Card({true, 0, true}, {0, 0, false});
    }
    CHECK(w);
    CHECK(p);
    CHECK_FALSE(KT::Choice{}.Card({true, 0, true}, {0, 3, false}));
}

TEST_CASE("Kid Icarus talk: the switch", "[frame_xr][kiutalk]") {
    CHECK(T::CutoutFor(nullptr, true));
    CHECK(T::CutoutFor("1", true));
    CHECK_FALSE(T::CutoutFor("0", true));
    CHECK_FALSE(T::CutoutFor(nullptr, false));
    KT::Live live;
    for (const KT::Expected e : {KT::Expected{}, KT::Expected{true, 3, false},
                                 KT::Expected{true, 0, true}}) {
        live.SetExpected(e);
        CHECK(live.GetExpected() == e);
    }
}
