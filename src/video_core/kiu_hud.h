// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiu140: Kid Icarus: Uprising's HUD in Angel's Aim, whole pieces moved into the view.
//
// First person's picture is the game's frame over the whole view (126 x 112 degrees), so the HUD
// the game puts at its frame's edges was 40 to 60 degrees aside and as flat as the frame is
// tall (the game's 5:3 frame drawn 1.32 wide); HUD Spacing scaled vertex by vertex and left those
// on the frame's edge behind (vr_hud_spacing.h), which stretched the pieces that touch it.
//
// The game's flat 2D draws on the top screen (a desktop census, Chapter 1 air: ten small draws an
// eye, no depth test, source alpha blending, textures from its own memory) are shaded on the CPU
// and drawn as a smaller frame of the game's own shape, placed in the view: straight pieces,
// nothing pinned. A draw that covers the frame (a fade), or copies a picture the GPU drew (its
// texture in VRAM: wipes, the game's glow passes), and one with any vertex not flat (marks in the
// world) stays as it is.
//
// Clip space as the vertex shader has it: x is up and down, y across (the top screen is drawn
// turned); +x up and +y to the right.
//
// Pure, unit tested (src/tests/frame_xr/kiu_hud.cpp); the XR thread publishes the place
// (frame_xr/immersive.cpp) and the rasterizer moves the vertices (TurnSoftwareVertices).

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace VideoCore::KiuHud {

/// What the settings ask for: degrees to the right and up from straight ahead.
struct Wanted {
    int size = 100;
    bool linked = true;
    float across = 0.0f, height = 22.0f; ///< linked
    float bar_across = -20.0f, bar_height = 22.0f;
    float score_across = 20.0f, score_height = 22.0f;
};

/// How the HUD's frame is drawn, in the screen's own coordinates (-1 to 1 over the picture shown):
/// half sizes, its middle's height, and each top group's offset (where the frame's middle would
/// be if it were all that group).
struct Place {
    bool on = false;
    float half_across = 1.0f, half_up = 1.0f;
    float middle_up = 0.0f;
    float bar_across = 0.0f, bar_up = 0.0f;
    float score_across = 0.0f, score_up = 0.0f;
};

/// Degrees across at HUD Size 100 %, and the height of the frame's middle.
inline constexpr float kWidthDeg = 60.0f, kMiddleDeg = 6.0f;
inline constexpr float kFrameAspect = 400.0f / 240.0f;
/// The two top groups in the frame (a desktop picture: the bar from -1 to -0.33, the score from
/// 0.58 to 1, both about 0.86 up), and what a draw is told from them by.
inline constexpr float kTopUp = 0.6f, kBarAside = -0.2f, kScoreAside = 0.4f;
inline constexpr float kBarLeft = -1.0f, kBarRight = -0.33f;
inline constexpr float kScoreLeft = 0.58f, kScoreRight = 1.0f;
inline constexpr float kGroupUp = 0.86f, kLinkGap = 0.08f;
/// Pieces in the frame's lower part go this much (half frames) further down: clear of the reticle.
inline constexpr float kLow = -0.35f, kLowDown = 0.7f;

/// The place for the settings and the screen first person shows (half angles' tangents, and its
/// own pitch, up positive).
inline Place PlaceFor(const Wanted& w, float tan_half_x, float tan_half_y, float pitch_rad) {
    Place p;
    if (!(tan_half_x > 0.05f) || !(tan_half_y > 0.05f) || !std::isfinite(pitch_rad)) {
        return p;
    }
    constexpr float kDeg = 3.14159265f / 180.0f;
    const float s = static_cast<float>(std::clamp(w.size, 40, 200)) / 100.0f;
    const float t = std::tan(0.5f * kWidthDeg * kDeg) * s;
    const auto across_at = [&](float deg) {
        return std::tan(std::clamp(deg, -50.0f, 50.0f) * kDeg) / tan_half_x;
    };
    const auto up_at = [&](float deg) {
        return std::tan(std::clamp(std::clamp(deg, -50.0f, 50.0f) * kDeg - pitch_rad, -1.2f, 1.2f)) /
               tan_half_y;
    };
    p.on = true;
    p.half_across = t / tan_half_x;
    p.half_up = t / kFrameAspect / tan_half_y;
    p.middle_up = up_at(kMiddleDeg);
    constexpr float kBarMiddle = 0.5f * (kBarLeft + kBarRight);
    constexpr float kScoreMiddle = 0.5f * (kScoreLeft + kScoreRight);
    if (w.linked) {
        // One unit, its middle where it is asked for: the bar, the gap, the score.
        constexpr float kBarWide = kBarRight - kBarLeft, kScoreWide = kScoreRight - kScoreLeft;
        constexpr float kLeft = -0.5f * (kBarWide + kLinkGap + kScoreWide);
        p.bar_across = across_at(w.across) + p.half_across * (kLeft - kBarLeft);
        p.score_across =
            across_at(w.across) + p.half_across * (kLeft + kBarWide + kLinkGap - kScoreLeft);
        p.bar_up = p.score_up = up_at(w.height) - p.half_up * kGroupUp;
    } else {
        p.bar_across = across_at(w.bar_across) - p.half_across * kBarMiddle;
        p.bar_up = up_at(w.bar_height) - p.half_up * kGroupUp;
        p.score_across = across_at(w.score_across) - p.half_across * kScoreMiddle;
        p.score_up = up_at(w.score_height) - p.half_up * kGroupUp;
    }
    return p;
}

/// The state of a draw that may be the HUD's.
struct Draw {
    bool top_viewport = false; ///< 240 x 400
    bool depth_test = false;
    bool blended = false; ///< source alpha, one minus source alpha
    bool textured = false;
    uint32_t texture = 0; ///< physical address
    uint32_t vertices = 0;
};
inline constexpr uint32_t kVramStart = 0x18000000, kVramEnd = 0x18600000;
inline constexpr uint32_t kMaxVertices = 1024;
constexpr bool MayBeHud(const Draw& d) {
    return d.top_viewport && !d.depth_test && d.blended && d.textured && d.vertices != 0 &&
           d.vertices <= kMaxVertices && !(d.texture >= kVramStart && d.texture < kVramEnd);
}

struct Vertex {
    float x, y, z, w; ///< clip: x up, y across
};
/// Whether these vertices (a triangle list) are a piece to move: all flat, not the whole frame.
inline bool Movable(const Vertex* v, std::size_t n) {
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (std::size_t i = 0; i < n; ++i) {
        if (!(std::abs(v[i].w - 1.0f) < 1e-4f) || !std::isfinite(v[i].x) ||
            !std::isfinite(v[i].y)) {
            return false;
        }
        x0 = std::min(x0, v[i].x);
        x1 = std::max(x1, v[i].x);
        y0 = std::min(y0, v[i].y);
        y1 = std::max(y1, v[i].y);
    }
    return n >= 3 && !(x1 - x0 > 1.9f && y1 - y0 > 1.9f);
}
/// One piece moved: a quad (two triangles in a row) or a triangle, by its middle. `factor`s: what
/// the vertex shader divides by (the game drawing past its frame). The result has w = 2, so the
/// shader's rule for flat vertices leaves it alone.
inline void Move(Vertex* t, std::size_t n, const Place& p, float factor_across, float factor_up) {
    float up = 0.0f, across = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        up += t[i].x / static_cast<float>(n);
        across += t[i].y / static_cast<float>(n);
    }
    float at_across = 0.0f, at_up = p.middle_up, rise = 0.0f;
    if (up > kTopUp && across < kBarAside) {
        at_across = p.bar_across;
        at_up = p.bar_up;
    } else if (up > kTopUp && across > kScoreAside) {
        at_across = p.score_across;
        at_up = p.score_up;
    } else if (up < kLow) {
        rise = -kLowDown;
    }
    for (std::size_t i = 0; i < n; ++i) {
        Vertex& v = t[i];
        const float u = at_up + p.half_up * (v.x + rise);
        const float a = at_across + p.half_across * v.y;
        v = {2.0f * u * factor_up, 2.0f * a * factor_across, 2.0f * v.z, 2.0f};
    }
}

/// The place in force. One writer (the XR thread, once a frame), read by the emulation thread a
/// draw; a read across a write mixes two frames' numbers, which are the same or next to it.
struct Live {
    std::array<std::atomic<float>, 7> f{};
    std::atomic<bool> on{false};

    void Set(const Place& p) {
        const float v[7] = {p.half_across, p.half_up,      p.middle_up, p.bar_across,
                            p.bar_up,      p.score_across, p.score_up};
        for (int i = 0; i < 7; ++i) {
            f[i].store(v[i], std::memory_order_relaxed);
        }
        on.store(p.on, std::memory_order_release);
    }
    Place Get() const {
        Place p;
        p.on = on.load(std::memory_order_acquire);
        float* const v[7] = {&p.half_across, &p.half_up,      &p.middle_up, &p.bar_across,
                             &p.bar_up,      &p.score_across, &p.score_up};
        for (int i = 0; i < 7; ++i) {
            *v[i] = f[i].load(std::memory_order_relaxed);
        }
        return p;
    }
};
inline Live g_live;

} // namespace VideoCore::KiuHud
