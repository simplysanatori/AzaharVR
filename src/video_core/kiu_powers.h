// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiupowers: Kid Icarus: Uprising's power bar, its icons, arrows and name alone, with
// nothing behind them (frame_xr/vis/power_menu.h; test switch AZAHARFRAME_KIU_POWERS, off by
// default). The game draws the bar on its bottom screen with the battle UI's 2D batch (kiu_hud.h
// Batch). Each draw of the bottom pass is looked at (Vulkan::RasterizerVulkan::Draw), the quads of
// the bar that are wanted are kept (Wanted below), and after the bottom screen's picture they are
// drawn once more, from the same textures (a texture pack's, when one replaces them), into a
// transparent part of the bottom screen's XR image under that picture (renderer_vulkan_xr.cpp).
//
// Pure but for the live state (unit tested: src/tests/frame_xr/kiu_powers.cpp).

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "video_core/kiu_hud.h"

namespace VideoCore::KiuPowers {

/// The part of the bottom screen the bar, its name bubble and nothing else that is wanted is in
/// (pixels, from the top left). The bubble is at 176 to 203 at rest and pops up larger, to about
/// 165, as a power is used.
inline constexpr uint32_t kLeft = 0, kTop = 160, kWide = 136, kHigh = 80;
inline constexpr uint32_t kScreenWide = 320, kScreenHigh = 240;
/// Between the bottom screen's picture and the strip under it, in the XR image's pixels.
inline constexpr uint32_t kGapPx = 8;

/// The XR image of the bottom screen with the strip: its height for `scale`, and the picture's and
/// the strip's parts of one `width` x `height` (an image with no strip is 3/4 as high as wide).
constexpr uint32_t ImageHeight(uint32_t scale, bool strip) {
    return kScreenHigh * scale + (strip ? kGapPx + kHigh * scale : 0);
}
constexpr uint32_t PictureHeight(uint32_t width, uint32_t height) {
    return std::min(height, width * kScreenHigh / kScreenWide);
}
struct PixelRect {
    uint32_t x = 0, y = 0, width = 0, height = 0;
    bool operator==(const PixelRect&) const = default;
};
/// Zero size: the image has no strip.
constexpr PixelRect StripIn(uint32_t width, uint32_t height) {
    const uint32_t scale = width / kScreenWide;
    if (scale == 0 || width % kScreenWide != 0 || height != ImageHeight(scale, true)) {
        return {};
    }
    return {0, kScreenHigh * scale + kGapPx, kWide * scale, kHigh * scale};
}

/// How the game colours a draw (its TEV, read from the registers in a census of the bar's draws):
/// the texture times the vertex colour (paper, bubble, arrows); the icons' and counts' (the
/// texture mixed toward the vertex colour by its alpha, which greys a power that can't be used;
/// alpha: the texture's times the stage's constant, the bar's fade); the name's (the vertex
/// colour, alpha: the texture's red times the vertex alpha). Any other: not drawn again.
enum class Tev : uint8_t { None, Modulate, Icon, Text };
/// One TEV stage's registers.
struct Stage {
    uint32_t sources = 0, modifiers = 0, ops = 0, scales = 0;
    constexpr bool PassThrough() const {
        return sources == 0x0FFF0FFF && modifiers == 0 && ops == 0 && scales == 0;
    }
};
/// From the six stages: the last that does something decides (and, for the icons, the one before
/// it, which hands on the texture). `constant`: the index of the stage whose constant counts.
constexpr Tev TevOf(const std::array<Stage, 6>& stages, uint32_t* constant = nullptr) {
    int last = -1, before = -1;
    for (int i = 0; i < 6; ++i) {
        if (!stages[i].PassThrough()) {
            before = last;
            last = i;
        }
    }
    if (last < 0 || stages[last].scales != 0) {
        return Tev::None;
    }
    const Stage& b = stages[last];
    if (constant) {
        *constant = static_cast<uint32_t>(last);
    }
    if (b.sources == 0x0F030F03 && b.modifiers == 0 && b.ops == 0x00010001) {
        return Tev::Modulate;
    }
    if (b.sources == 0x0F030FE0 && b.modifiers == 0x2000 && b.ops == 0x00010000) {
        return Tev::Text;
    }
    if (b.sources == 0x0FE300F0 && b.modifiers == 0x200 && b.ops == 0x00010004 && before >= 0 &&
        stages[before].sources == 0x00000EE3 && stages[before].modifiers == 0 &&
        stages[before].ops == 0 && stages[before].scales == 0) {
        return Tev::Icon;
    }
    return Tev::None;
}

/// Which of the bar's quads these are (Chapter 1's save states and a census of the draws, EUR;
/// KIU_HOOKS.md "Powers"). In the order drawn: the paper (the selected place's piece tinted gold),
/// the name bubble's nine patches in the same draw, the cards behind the icons, the icons, the
/// counts, the arrows, the name.
enum class Part : uint8_t { Other, Paper, Bubble, Card, Icon, Count, Arrow, Name };
/// The three places' part of the bar: the game's scissor for the cards, icons and counts, which
/// slide through it when another power is selected.
inline constexpr float kPlacesLeft = 8.0f, kPlacesRight = 124.0f;
/// A quad: its corners' range on the bottom screen, its texture coordinates' range (the game's:
/// of the texture, v from the bottom), and how its draw is coloured.
struct QuadIn {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    Tev tev = Tev::Modulate;
};
constexpr Part PartOf(const QuadIn& q) {
    const float w = q.x1 - q.x0, h = q.y1 - q.y0;
    const auto is = [](float v, float want) { return v > want - 0.6f && v < want + 0.6f; };
    // Sliding: a place further out on either side.
    const bool in_places = q.x1 > kPlacesLeft - 40.0f && q.x0 < kPlacesRight + 40.0f;
    // The bubble and its name move and grow as they come, go and as a power is used.
    const bool by_bubble = q.x0 > -10.0f && q.x1 < static_cast<float>(kWide) + 14.0f &&
                           q.y0 > static_cast<float>(kTop) - 20.0f && q.y1 < 206.0f;
    if (q.tev == Tev::Text) {
        return by_bubble && h > 8.0f && w > 4.0f ? Part::Name : Part::Other;
    }
    if (q.tev == Tev::Icon) {
        if (is(w, 32) && is(h, 32) && is(q.y0, 203)) {
            return in_places ? Part::Icon : Part::Other;
        }
        return is(h, 14) && is(q.y0, 224) && w < 14.6f && in_places ? Part::Count : Part::Other;
    }
    if (q.tev != Tev::Modulate) {
        return Part::Other;
    }
    if (is(w, 38) && is(h, 38) && is(q.y0, 202)) {
        return in_places ? Part::Card : Part::Other;
    }
    // The bubble's nine patches: 183 to 206 across and 72 to 96 down of the sheet's 256.
    constexpr float kE = 1.0f / 256.0f;
    if (by_bubble && q.u0 > 183 * kE - kE && q.u1 < 206 * kE + kE && q.v0 > 1 - 96 * kE - kE &&
        q.v1 < 1 - 72 * kE + kE) {
        return Part::Bubble;
    }
    if (q.x0 < -3.0f || q.x1 > static_cast<float>(kWide) + 1.0f) {
        return Part::Other;
    }
    if (is(q.y0, 198) && is(q.y1, 240)) {
        return Part::Paper;
    }
    if (is(w, 13) && is(h, 17) && is(q.y0, 210)) {
        return Part::Arrow;
    }
    return Part::Other;
}
/// The paper's piece behind the selected power: the one that is not white (gold, FFD200).
constexpr bool IsGold(Part p, uint32_t abgr) {
    return p == Part::Paper && (abgr & 0xFFFFFFu) != 0xFFFFFFu;
}
/// What is drawn again: no paper but the gold piece (when asked for), no cards.
constexpr bool Wanted(Part p, uint32_t abgr, bool gold) {
    return p == Part::Bubble || p == Part::Icon || p == Part::Count || p == Part::Arrow ||
           p == Part::Name || (gold && IsGold(p, abgr));
}
/// Held to the three places, as the game's scissor does.
constexpr bool Clipped(Part p) {
    return p == Part::Icon || p == Part::Count || p == Part::Card;
}

/// One of the bar's quads to draw again: its corners in the bottom screen's pixels (from the top
/// left; a triangle strip's order), texture coordinates (the game's) and colour.
struct Sprite {
    std::array<float, 4> x{}, y{}, u{}, v{};
    std::array<float, 4> color{}; ///< straight alpha
    float constant = 1.0f;        ///< Tev::Icon: the constant's alpha
    uint32_t texture = 0;         ///< which of the draws' textures
    Part part = Part::Other;
    Tev tev = Tev::Modulate;
};

/// The bar's own fade (it goes to about 40 % when nothing is selected for a while): the most
/// opaque of what is drawn, 1 for a bar in full. The gold piece is 0xBF of it. Drawn again without
/// it (divided out): what shows is the player's opacity alone.
constexpr float kGoldAlpha = 191.0f / 255.0f;
constexpr float OwnAlpha(const Sprite& s) {
    const float a = s.tev == Tev::Icon ? s.constant : s.color[3];
    return s.part == Part::Paper ? a / kGoldAlpha : a;
}
template <typename Sprites>
constexpr float BarFade(const Sprites& sprites) {
    float most = 0.0f;
    for (const Sprite& s : sprites) {
        if (s.part != Part::Bubble && s.part != Part::Name) { // those have a fade of their own
            most = std::max(most, OwnAlpha(s));
        }
    }
    return most > 0.05f ? std::min(most, 1.0f) : 1.0f;
}

/// The strip's opacity for the game's next pictures (the XR thread; 0: not drawn).
inline std::atomic<float> g_alpha{0.0f};

/// The emulation thread: the batch whose draws are looked at (none: quads 0), and whether the
/// bottom screen's XR image has the strip: for as long as the switch is on and the game runs,
/// whatever it shows (a change remakes the image's swapchain).
struct Live {
    std::atomic<uint32_t> vertex_base{0}, index_offset{0}, quads{0};
    std::atomic<bool> strip{false};
    std::atomic<bool> gold{true}; ///< the selected power's gold piece too

    void Set(const KiuHud::Batch& b) {
        vertex_base.store(b.vertex_base, std::memory_order_relaxed);
        index_offset.store(b.index_offset, std::memory_order_relaxed);
        quads.store(b.quads, std::memory_order_relaxed);
    }
    KiuHud::Batch Get() const {
        return {vertex_base.load(std::memory_order_relaxed),
                index_offset.load(std::memory_order_relaxed), quads.load(std::memory_order_relaxed)};
    }
};
inline Live g_live;

} // namespace VideoCore::KiuPowers
