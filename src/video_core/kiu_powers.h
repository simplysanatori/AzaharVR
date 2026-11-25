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
/// (pixels, from the top left).
inline constexpr uint32_t kLeft = 0, kTop = 176, kWide = 136, kHigh = 64;
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

/// One of the bar's quads to draw again: its corners in the bottom screen's pixels (from the top
/// left; a triangle strip's order), texture coordinates and colour.
struct Sprite {
    std::array<float, 4> x{}, y{}, u{}, v{};
    std::array<float, 4> color{}; ///< straight alpha
    uint32_t texture = 0;         ///< which of the draws' textures
};

/// Which of the bar's quads these are, by their place (Chapter 1's save states, EUR: the batch's
/// quads and the game's own flush list; KIU_HOOKS.md "Powers"). In the order drawn: the paper (the
/// selected place's piece tinted gold), the name bubble's nine patches in the same draw, the cards
/// behind the icons, the icons, the counts, the arrows, the name.
enum class Part : uint8_t { Other, Paper, Bubble, Card, Icon, Count, Arrow, Name };
constexpr Part PartOf(float x0, float y0, float x1, float y1) {
    const float w = x1 - x0, h = y1 - y0;
    const auto is = [](float v, float want) { return v > want - 0.6f && v < want + 0.6f; };
    if (x0 < -3.0f || x1 > static_cast<float>(kWide) + 1.0f) {
        return Part::Other;
    }
    if (is(y0, 198) && is(y1, 240)) {
        return Part::Paper;
    }
    if (is(w, 38) && is(h, 38) && is(y0, 202)) {
        return Part::Card;
    }
    if (is(w, 32) && is(h, 32) && is(y0, 203)) {
        return Part::Icon;
    }
    if (is(h, 14) && is(y0, 224)) {
        return Part::Count;
    }
    if (is(w, 13) && is(h, 17) && is(y0, 210)) {
        return Part::Arrow;
    }
    if (is(y0, 180) && is(y1, 195)) {
        return Part::Name;
    }
    return y0 > 175.4f && y1 < 203.6f && (is(h, 12) || is(h, 3)) ? Part::Bubble : Part::Other;
}
constexpr bool Wanted(Part p) {
    return p == Part::Bubble || p == Part::Icon || p == Part::Count || p == Part::Arrow ||
           p == Part::Name;
}

/// The strip's opacity for the game's next pictures (the XR thread; 0: not drawn).
inline std::atomic<float> g_alpha{0.0f};

/// The emulation thread: the batch whose draws are looked at (none: quads 0), and whether the
/// bottom screen's XR image has the strip (for as long as the switch is on).
struct Live {
    std::atomic<uint32_t> vertex_base{0}, index_offset{0}, quads{0};
    std::atomic<bool> strip{false};

    void Set(const KiuHud::Batch& b, bool with_strip) {
        vertex_base.store(b.vertex_base, std::memory_order_relaxed);
        index_offset.store(b.index_offset, std::memory_order_relaxed);
        quads.store(b.quads, std::memory_order_relaxed);
        strip.store(with_strip, std::memory_order_relaxed);
    }
    KiuHud::Batch Get() const {
        return {vertex_base.load(std::memory_order_relaxed),
                index_offset.load(std::memory_order_relaxed), quads.load(std::memory_order_relaxed)};
    }
};
inline Live g_live;

} // namespace VideoCore::KiuPowers
