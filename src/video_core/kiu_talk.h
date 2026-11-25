// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiutalk: Kid Icarus: Uprising's talk portraits and illustrations alone, with nothing
// behind them (Character Portraits; AZAHARFRAME_KIU_TALK_CUTOUT=0: the card cut from the Secondary
// Screen's picture instead). The game draws them on its bottom screen with the battle UI's 2D batch
// (kiu_hud.h Batch). Each draw of the bottom pass is looked at (renderer_vulkan/vk_kiu_talk.h), the
// portraits and the illustration's pieces are kept, and after the bottom screen's picture they are
// drawn once more, from the same textures and coloured as the game does, into a transparent copy of
// the bottom screen under that picture, in the same XR image (below the powers' strip, kiu_powers.h).
//
// Pure but for the live state (unit tested: src/tests/frame_xr/kiu_talk.cpp).

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "video_core/kiu_hud.h"

namespace VideoCore::KiuTalk {

inline constexpr uint32_t kScreenWide = 320, kScreenHigh = 240;
/// Between the strip and what is above it, in the XR image's pixels.
inline constexpr uint32_t kGapPx = 8;
/// No strip in an image that would be higher (today's card then).
inline constexpr uint32_t kMostHigh = 4096;

/// The bottom screen's XR image with the strip: the height it has `without`, and more.
constexpr uint32_t ExtraHeight(uint32_t scale) {
    return kGapPx + kScreenHigh * scale;
}
constexpr uint32_t ImageHeight(uint32_t without, uint32_t scale, bool strip) {
    const uint32_t high = without + ExtraHeight(scale);
    return strip && high <= kMostHigh ? high : without;
}
struct PixelRect {
    uint32_t x = 0, y = 0, width = 0, height = 0;
    bool operator==(const PixelRect&) const = default;
};
/// The strip is the image's lowest part. Zero size: the image has none. Above it is the picture (240
/// a step high), or that and the powers' strip (kiu_powers.h: 80 a step and their gap).
constexpr PixelRect StripIn(uint32_t width, uint32_t height) {
    const uint32_t scale = width / kScreenWide;
    if (scale == 0 || width % kScreenWide != 0 || height <= ExtraHeight(scale)) {
        return {};
    }
    const uint32_t without = height - ExtraHeight(scale), picture = kScreenHigh * scale;
    if (without != picture && without != picture + kGapPx + 80 * scale) {
        return {};
    }
    return {0, height - picture, width, picture};
}
/// The image's height without the strip.
constexpr uint32_t Without(uint32_t width, uint32_t height) {
    const PixelRect strip = StripIn(width, height);
    return strip.width != 0 ? strip.y - kGapPx : height;
}

/// The card is the picture from this row down (frame_xr/kiu_first_person.h kPortraitCut). The strip
/// has all its rows, as the tallest portraits reach over it: its quad is higher than the card's, at
/// the top only, so what is in both is at the same place.
inline constexpr uint32_t kCardTop = 42;
/// The strip's quad, from the card's height: its height, and how far its middle is above the card's.
struct Grown {
    float height, up;
};
constexpr Grown GrownFrom(float card_height) {
    const float row = card_height / static_cast<float>(kScreenHigh - kCardTop);
    return {row * static_cast<float>(kScreenHigh), 0.5f * row * static_cast<float>(kCardTop)};
}

/// One TEV stage's registers.
struct Stage {
    uint32_t sources = 0x0FFF0FFF, modifiers = 0, ops = 0, scales = 0, constant = 0;
    constexpr bool PassThrough() const {
        return sources == 0x0FFF0FFF && modifiers == 0 && ops == 0 && scales == 0;
    }
};
/// How the game colours a draw (a census of the bottom screen in talk, EUR; KIU_HOOKS.md "Talk"):
/// Plain: the texture times the vertex colour (an illustration's pieces). Portrait, two stages the
/// UI uses for nothing else: colour 1 - (1 - texture) x constant x 2, times the vertex colour (how
/// the one who isn't speaking is dimmed); alpha twice the texture's red and green together, times
/// the vertex alpha (the portraits' textures have no alpha of their own). Any other: not drawn.
enum class Tev : uint8_t { None, Plain, Portrait };
constexpr Tev TevOf(const std::array<Stage, 6>& stages, uint32_t* constant = nullptr) {
    int last = -1, before = -1;
    for (int i = 0; i < 6; ++i) {
        if (!stages[i].PassThrough()) {
            before = last;
            last = i;
        }
    }
    if (last < 0 || stages[last].scales != 0 || stages[last].modifiers > 1) {
        return Tev::None;
    }
    const Stage& b = stages[last];
    if (b.sources == 0x0F030F03 && b.modifiers == 0 && b.ops == 0x00010001) {
        return Tev::Plain;
    }
    if (before == last - 1 && b.sources == 0x00FF0F0F && b.modifiers == 1 && b.ops == 0x00090001) {
        const Stage& a = stages[before];
        if (a.sources == 0x0F330FE3 && a.modifiers == 0x00042001 && a.ops == 0x00020001 &&
            a.scales == 1) {
            if (constant) {
                *constant = a.constant & 0xFFFFFFu;
            }
            return Tev::Portrait;
        }
    }
    return Tev::None;
}

/// One of the quads: its corners in the bottom screen's pixels (from the top left; a triangle
/// strip's order), texture coordinates (the game's) and colours (ABGR; the top two of a portrait
/// wiped in or out are see-through).
struct Quad {
    std::array<float, 4> x{}, y{}, u{}, v{};
    std::array<uint32_t, 4> color{};
};
constexpr bool Finite(const Quad& q) {
    for (int i = 0; i < 4; ++i) {
        for (const float f : {q.x[i], q.y[i], q.u[i], q.v[i]}) {
            if (!(f > -2048.0f && f < 2048.0f)) {
                return false;
            }
        }
    }
    return true;
}
enum class Kind : uint8_t { PortraitLeft, PortraitRight, Illustration };
struct Sprite {
    Quad quad;
    Kind kind = Kind::Illustration;
    Tev tev = Tev::None;
    uint32_t constant = 0; ///< Tev::Portrait: 0xBBGGRR
    uint32_t texture = 0;  ///< which of the draws' textures
};
/// The two sides' middles are at 123 and 197.
constexpr Kind SideOf(const Quad& q) {
    return 0.25f * (q.x[0] + q.x[1] + q.x[2] + q.x[3]) < 160.0f ? Kind::PortraitLeft
                                                                : Kind::PortraitRight;
}
/// The picture's part of an illustration: what is above and below is covered by the bands (the
/// lines are rows 55 and 184).
inline constexpr uint32_t kWindowTop = 56, kWindowBottom = 184;

/// What a draw of the bottom pass in the batch is.
struct DrawIn {
    bool textured = false;
    uint32_t width = 0, height = 0, format = 0; ///< texture 0's
    uint32_t texture = 0;                       ///< physical address
    bool over = false;                          ///< source alpha, one minus source alpha
    Tev tev = Tev::None;
    uint32_t quads = 0;
    Quad first; ///< of the quads
};
inline constexpr uint32_t kEtc1 = 12;
enum class Part : uint8_t { Other, Portrait, IllustrationOpens, IllustrationCloses, Piece, Wrong };
constexpr bool IsPortrait(const DrawIn& d) {
    const float wide = d.first.x[1] - d.first.x[0]; // mirrored: below 0
    return d.textured && d.over && d.tev == Tev::Portrait && d.format == kEtc1 && d.width == 256 &&
           d.height == 256 && !(d.texture >= KiuHud::kVramStart && d.texture < KiuHud::kVramEnd) &&
           (d.quads == 1 || d.quads == 2) && Finite(d.first) &&
           (wide > 255.0f || wide < -255.0f) && wide < 257.0f && wide > -257.0f;
}
/// The black behind an illustration: one opaque black quad over the whole width, its middle the
/// screen's, then its pieces (one quad a draw), then its bands and lines (four quads, not textured).
constexpr bool OpensIllustration(const DrawIn& d) {
    if (d.textured || d.quads != 1 || !Finite(d.first)) {
        return false;
    }
    const Quad& q = d.first;
    for (const uint32_t c : q.color) {
        if (c != 0xFF000000u) {
            return false;
        }
    }
    const float middle = 0.5f * (q.y[0] + q.y[2]);
    return q.x[0] < 1.0f && q.x[1] > 319.0f && middle > 119.0f && middle < 121.0f &&
           q.y[2] - q.y[0] < 132.0f;
}
constexpr Part PartOf(const DrawIn& d, bool in_illustration) {
    if (IsPortrait(d)) {
        return in_illustration ? Part::Wrong : Part::Portrait;
    }
    if (!in_illustration) {
        return OpensIllustration(d) ? Part::IllustrationOpens : Part::Other;
    }
    if (!d.textured) {
        return d.quads == 4 ? Part::IllustrationCloses : Part::Wrong;
    }
    return d.over && d.tev == Tev::Plain && d.quads == 1 && Finite(d.first) ? Part::Piece
                                                                            : Part::Wrong;
}

/// What a picture had.
struct Seen {
    uint32_t portraits = 0, pieces = 0;
    bool wrong = false; ///< something in it is not as known
};
/// What the game's own state says it draws (frame_xr/kiu_talk.h).
struct Expected {
    bool known = false; ///< its code is as known
    uint32_t portraits = 0;
    bool illustration = false; ///< with at least one piece
    bool operator==(const Expected&) const = default;
};
/// Pictures shown from the first miss to the card, and back from it: what the game says and what it
/// drew may be a picture or two apart.
inline constexpr int kMisses = 8;
/// Today's card instead of the strip's own: when the code is not as known at once, when less is
/// found than the game says it draws, or something is not as known, for kMisses pictures.
class Choice {
public:
    bool Card(const Expected& e, const Seen& s) {
        if (!e.known) {
            card_ = true;
            run_ = 0;
            return true;
        }
        const bool bad =
            s.wrong || s.portraits < e.portraits || (e.illustration && s.pieces == 0);
        run_ = bad == card_ ? 0 : run_ + 1;
        if (run_ >= kMisses) {
            card_ = bad;
            run_ = 0;
        }
        return card_;
    }

private:
    bool card_ = false;
    int run_ = 0;
};

struct Live {
    std::atomic<uint32_t> vertex_base{0}, index_offset{0}, quads{0};
    std::atomic<uint32_t> expected{0}; ///< bit 31: known, bit 30: illustration, else portraits
    std::atomic<bool> strip{false};    ///< the image has it: the setting on and the game running
    std::atomic<bool> shown{false};    ///< the XR thread: the layer is the talk's
    std::atomic<bool> trace{false};    ///< log what is kept

    void Set(const KiuHud::Batch& b) {
        vertex_base.store(b.vertex_base, std::memory_order_relaxed);
        index_offset.store(b.index_offset, std::memory_order_relaxed);
        quads.store(b.quads, std::memory_order_relaxed);
    }
    KiuHud::Batch Get() const {
        return {vertex_base.load(std::memory_order_relaxed),
                index_offset.load(std::memory_order_relaxed), quads.load(std::memory_order_relaxed)};
    }
    void SetExpected(const Expected& e) {
        expected.store((e.known ? 0x80000000u : 0) | (e.illustration ? 0x40000000u : 0) |
                           (e.portraits & 0xFF),
                       std::memory_order_relaxed);
    }
    Expected GetExpected() const {
        const uint32_t v = expected.load(std::memory_order_relaxed);
        return {(v & 0x80000000u) != 0, v & 0xFF, (v & 0x40000000u) != 0};
    }
};
inline Live g_live;

} // namespace VideoCore::KiuTalk
