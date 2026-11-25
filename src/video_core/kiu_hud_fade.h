// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuhs3a: HUD Opacity (Kid Icarus: Uprising, HUD: In View): the health bar and the
// score as one see-through picture. A piece is several draws over each other (the bar: its frame,
// its fill, a shading masked by the fill's alpha, the wing), so a lower alpha a draw would let the
// lower ones show through the upper ones, and hide the world more the more there are. Instead what
// is behind them is kept before the first (a copy of those two parts of the eye's picture), they are
// drawn as ever, and what was behind is drawn over them at (1 - opacity), in the same place, before
// the next other draw: out = opacity * HUD + (1 - opacity) * behind, to the pixel.
//
// Only the battle UI's own batch (kiu_hud.h As::Piece): the bar's and the score's groups. At 100
// percent nothing is done. The game's alpha is left as the draws wrote it.
//
// Pure, unit tested; the Vulkan renderer's part is vk_kiu_hud_fade.h.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "video_core/kiu_hud.h"

namespace VideoCore::KiuHudFade {

/// The opacity (1: nothing is done). The XR thread writes, the emulation thread reads.
inline std::atomic<float> g_opacity{1.0f};

/// vr_kiu_hud_opacity's percent: the opacity of a battle's HUD in the view (`hud`), 1 for anything
/// else.
inline float OpacityFor(int percent, bool hud) {
    return hud ? static_cast<float>(std::clamp(percent, 20, 100)) / 100.0f : 1.0f;
}

/// A rectangle in the eye's picture, clip coordinates (x up, y across to the left; -1 to 1).
struct Box {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    bool Empty() const {
        return !(x0 < x1) || !(y0 < y1);
    }
    void Add(float x, float y) {
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
    }
};

/// Where the pieces of a group may be, with a margin: what is kept.
inline Box GroupBox(const KiuHud::Place& p, KiuHud::Group group) {
    const bool bar = group == KiuHud::Group::Bar;
    const float up = bar ? p.bar_up : p.score_up;
    const float across = bar ? p.bar_across : p.score_across;
    Box b;
    b.x0 = up + p.half_up * 0.4f;
    b.x1 = up + p.half_up * 1.3f;
    const float left = across + p.half_across * (bar ? -1.3f : KiuHud::kScoreLeft - 0.2f);
    const float right = across + p.half_across * (bar ? KiuHud::kBarRight + 0.2f : 1.3f);
    b.y0 = -right;
    b.y1 = -left;
    return b;
}

/// A rectangle in an image's pixels.
struct Pixels {
    int32_t x = 0, y = 0;
    uint32_t width = 0, height = 0;
    bool Empty() const {
        return width == 0 || height == 0;
    }
    bool operator==(const Pixels&) const = default;
};
/// `b` in the image, for a viewport there (a size below 0: turned over) and the part drawn to.
inline Pixels ToPixels(const Box& b, int vx, int vy, int vw, int vh, const Pixels& within) {
    if (b.Empty()) {
        return {};
    }
    const auto at = [](float n, int from, int size) {
        return static_cast<float>(from) + (n + 1.0f) * 0.5f * static_cast<float>(size);
    };
    const float ax = at(b.x0, vx, vw), bx = at(b.x1, vx, vw);
    const float ay = at(b.y0, vy, vh), by = at(b.y1, vy, vh);
    const float w0 = static_cast<float>(within.x), h0 = static_cast<float>(within.y);
    const float w1 = w0 + static_cast<float>(within.width);
    const float h1 = h0 + static_cast<float>(within.height);
    const float x0 = std::clamp(std::floor(std::min(ax, bx)), w0, w1);
    const float x1 = std::clamp(std::ceil(std::max(ax, bx)), w0, w1);
    const float y0 = std::clamp(std::floor(std::min(ay, by)), h0, h1);
    const float y1 = std::clamp(std::ceil(std::max(ay, by)), h0, h1);
    if (!(x1 > x0) || !(y1 > y0)) {
        return {};
    }
    return {static_cast<int32_t>(x0), static_cast<int32_t>(y0), static_cast<uint32_t>(x1 - x0),
            static_cast<uint32_t>(y1 - y0)};
}
inline Pixels Intersect(const Pixels& a, const Pixels& b) {
    const int32_t x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
    const int32_t x1 = std::min(a.x + static_cast<int32_t>(a.width),
                                b.x + static_cast<int32_t>(b.width));
    const int32_t y1 = std::min(a.y + static_cast<int32_t>(a.height),
                                b.y + static_cast<int32_t>(b.height));
    if (x1 <= x0 || y1 <= y0) {
        return {};
    }
    return {x0, y0, static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)};
}
inline Pixels Union(const Pixels& a, const Pixels& b) {
    if (a.Empty() || b.Empty()) {
        return a.Empty() ? b : a;
    }
    const int32_t x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    const int32_t x1 = std::max(a.x + static_cast<int32_t>(a.width),
                                b.x + static_cast<int32_t>(b.width));
    const int32_t y1 = std::max(a.y + static_cast<int32_t>(a.height),
                                b.y + static_cast<int32_t>(b.height));
    return {x0, y0, static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)};
}

/// The draw about to be drawn (RasterizerAccelerated::TurnSoftwareVertices).
struct Piece {
    float opacity = 1.0f; ///< below 1: a piece of the bar or the score to fade
    KiuHud::Group group = KiuHud::Group::Middle;
    Box box;              ///< its own
    std::array<Box, 2> groups; ///< the bar's and the score's, where kept
    bool Fades() const {
        return opacity < 1.0f && !box.Empty();
    }
};
/// `v`: a piece's vertices, moved (clip, over the factors the shader divides by).
inline Piece PieceFor(float opacity, const KiuHud::Place& place, KiuHud::Group group,
                      const KiuHud::Vertex* v, std::size_t n, float factor_across,
                      float factor_up) {
    Piece out;
    if (!(opacity < 1.0f) || place.page ||
        (group != KiuHud::Group::Bar && group != KiuHud::Group::Score)) {
        return out;
    }
    for (std::size_t i = 0; i < n; ++i) {
        out.box.Add(v[i].x / v[i].w / factor_up, v[i].y / v[i].w / factor_across);
    }
    out.opacity = std::max(opacity, 0.0f);
    out.group = group;
    out.groups = {GroupBox(place, KiuHud::Group::Bar), GroupBox(place, KiuHud::Group::Score)};
    return out;
}

} // namespace VideoCore::KiuHudFade
