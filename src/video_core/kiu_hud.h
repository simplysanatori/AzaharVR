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

/// Where a piece from the frame's top left (the health bar) or top right (the score) goes.
enum class Side : uint32_t { Left = 0, Middle = 1, Right = 2 };
constexpr Side SideFrom(uint32_t v) {
    return v == 1u ? Side::Middle : v == 2u ? Side::Right : Side::Left;
}

/// The HUD's frame on the screen: its middle and half sizes in the screen's own coordinates (-1 to
/// 1 over the picture shown), and how far (in half frames) the two top groups are moved.
struct Place {
    bool on = false;
    float middle_across = 0.0f, middle_up = 0.0f;
    float half_across = 1.0f, half_up = 1.0f;
    float left_shift = 0.0f, left_up = 0.0f;
    float right_shift = 0.0f, right_up = 0.0f;
};

/// Degrees across at HUD Size 100 %.
inline constexpr float kWidthDeg = 52.0f;
inline constexpr float kFrameAspect = 400.0f / 240.0f;
/// The two top groups in the frame (a desktop picture: the bar from -1 to -0.33, the score from
/// 0.58 to 1) and where one under the other goes.
inline constexpr float kTopUp = 0.35f, kTopAside = 0.2f;
inline constexpr float kBarLeft = -1.0f, kBarRight = -0.33f;
inline constexpr float kScoreLeft = 0.58f, kScoreRight = 1.0f;
inline constexpr float kUnder = 0.32f;

/// The place for the settings: `size` percent, `height_deg` its middle above straight ahead,
/// `spread` percent (how far apart the two top groups are; 100 the game's), and the screen
/// first person shows (half angles' tangents, and its own pitch, up positive).
inline Place PlaceFor(int size, int height_deg, int spread, Side bar, Side score, float tan_half_x,
                      float tan_half_y, float pitch_rad) {
    Place p;
    if (!(tan_half_x > 0.05f) || !(tan_half_y > 0.05f) || !std::isfinite(pitch_rad)) {
        return p;
    }
    constexpr float kDeg = 3.14159265f / 180.0f;
    const float s = static_cast<float>(std::clamp(size, 40, 200)) / 100.0f;
    const float t = std::tan(0.5f * kWidthDeg * kDeg) * s;
    p.on = true;
    p.half_across = t / tan_half_x;
    p.half_up = t / kFrameAspect / tan_half_y;
    const float up = std::clamp(static_cast<float>(height_deg), -40.0f, 40.0f) * kDeg - pitch_rad;
    p.middle_up = std::tan(std::clamp(up, -1.2f, 1.2f)) / tan_half_y;
    const float k = static_cast<float>(std::clamp(spread, 0, 200)) / 100.0f;
    // A group's left edge for each side: the left and right ends of the frame, spread about its
    // middle, or centred.
    const auto left_edge = [k](Side side, float width) {
        switch (side) {
        case Side::Middle:
            return -0.5f * width;
        case Side::Right:
            return (1.0f - 0.5f * width) * k - 0.5f * width;
        default:
            return -(1.0f - 0.5f * width) * k - 0.5f * width;
        }
    };
    p.left_shift = left_edge(bar, kBarRight - kBarLeft) - kBarLeft;
    p.right_shift = left_edge(score, kScoreRight - kScoreLeft) - kScoreLeft;
    if (bar == score) {
        p.right_up = -kUnder; // the score under the bar
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
/// One triangle moved. `factor`s: what the vertex shader divides by (the game drawing past its
/// frame). The result has w = 2, so the shader's rule for flat vertices leaves it alone.
inline void Move(Vertex (&t)[3], const Place& p, float factor_across, float factor_up) {
    const float up = (t[0].x + t[1].x + t[2].x) / 3.0f;
    const float across = (t[0].y + t[1].y + t[2].y) / 3.0f;
    float shift = 0.0f, rise = 0.0f;
    if (up > kTopUp && across < -kTopAside) {
        shift = p.left_shift;
        rise = p.left_up;
    } else if (up > kTopUp && across > kTopAside) {
        shift = p.right_shift;
        rise = p.right_up;
    }
    for (Vertex& v : t) {
        const float u = p.middle_up + p.half_up * (v.x + rise);
        const float a = p.middle_across + p.half_across * (v.y + shift);
        v = {2.0f * u * factor_up, 2.0f * a * factor_across, 2.0f * v.z, 2.0f};
    }
}

/// The place in force. One writer (the XR thread, once a frame), read by the emulation thread a
/// draw; a read across a write mixes two frames' numbers, which are the same or next to it.
struct Live {
    std::array<std::atomic<float>, 8> f{};
    std::atomic<bool> on{false};

    void Set(const Place& p) {
        const float v[8] = {p.middle_across, p.middle_up, p.half_across, p.half_up,
                            p.left_shift,    p.left_up,   p.right_shift, p.right_up};
        for (int i = 0; i < 8; ++i) {
            f[i].store(v[i], std::memory_order_relaxed);
        }
        on.store(p.on, std::memory_order_release);
    }
    Place Get() const {
        Place p;
        p.on = on.load(std::memory_order_acquire);
        float* const v[8] = {&p.middle_across, &p.middle_up, &p.half_across, &p.half_up,
                             &p.left_shift,    &p.left_up,   &p.right_shift, &p.right_up};
        for (int i = 0; i < 8; ++i) {
            *v[i] = f[i].load(std::memory_order_relaxed);
        }
        return p;
    }
};
inline Live g_live;

} // namespace VideoCore::KiuHud
