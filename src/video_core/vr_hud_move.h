// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame hudmove: Move HUD In (experimental, per game, off by default).
//
// In the Full modes the game draws more than its own frame (Render Margin, the scales in Field
// of View) and the screen is made large, so a HUD the game puts at its frame's edges ends up at
// the edges of the view, or past them (Mario Kart 7). This moves the flat 2D drawing on the
// Primary screen toward the middle by a share of its distance from the middle, separately across
// and up and down; negative values move it outward.
//
// Which draws move is the vertex shader's existing rule for 2D (glsl_shader_gen.cpp VrFactor): a
// vertex with w = 1 is 2D (an orthographic projection; the world's perspective vertices have
// their depth in w), and a 2D vertex on or past an edge of the view keeps its place, so
// full-screen 2D passes (masks, copies of the last frame) still cover the whole picture. Only the
// 2D vertices inside the view are scaled about the middle: no register of any game is read, so
// a game whose HUD is not drawn with w = 1 shows no change rather than a wrong one.
//
// The scale applies to vertices, so an element also gets that much smaller on that axis (a
// different amount across than up and down narrows or flattens it a little).
//
// Header only: the XR thread (frame_xr/immersive.cpp) works out the scales once per frame and
// publishes them with the rest of the immersive data (vr_immersive.h); the rasterizer copies
// them into the vertex uniforms. Pure functions, unit tested.

#pragma once

#include <algorithm>
#include <cmath>

namespace VideoCore::HudMove {

/// The settings' range in percent of an element's distance from the middle (positive: toward
/// the middle). +10 % across moves an element at the edge of a 400 pixel picture 20 pixels in.
inline constexpr int kMinPercent = -20;
inline constexpr int kMaxPercent = 40;
/// The shader's 2D test (VrFactor): |w - 1| below this.
inline constexpr float kFlatW = 0.0001f;
/// The shader's edge test (VrFactor): a 2D vertex at or past this keeps its place.
inline constexpr float kEdge = 0.999f;
/// What the rasterizer accepts (anything else is taken as 1, no change).
inline constexpr float kMinScale = 0.5f;
inline constexpr float kMaxScale = 1.5f;

inline int ClampPercent(int percent) {
    return std::clamp(percent, kMinPercent, kMaxPercent);
}

/// The scale of 2D positions about the middle for `percent`: 1 at 0, 0.9 at +10.
inline float ScaleForPercent(int percent) {
    return 1.0f - static_cast<float>(ClampPercent(percent)) / 100.0f;
}

/// A scale the rasterizer can use: finite and within kMinScale to kMaxScale, else 1.
inline float UsableScale(float scale) {
    return std::isfinite(scale) && scale >= kMinScale && scale <= kMaxScale ? scale : 1.0f;
}

/// The two scales: across the picture (clip y: the top screen is drawn turned) and up and down
/// (clip x).
struct Scales {
    float across = 1.0f;
    float up_down = 1.0f;

    bool IsIdentity() const {
        return across == 1.0f && up_down == 1.0f;
    }
    bool operator==(const Scales& o) const {
        return across == o.across && up_down == o.up_down;
    }
    bool operator!=(const Scales& o) const {
        return !(*this == o);
    }
};

/// The scales for one frame (XR thread). Only while immersive mode is engaged in a Full mode
/// (2 or 3): anything else (Off, Wide, paused for the Secondary screen or the in-game menu, no
/// game) is 1, so the game draws as it would without the setting. `menu_weight` is mode 2's menu
/// rule (immersive_layout.h MenuExact, 0 to 1): while the game shows a screen of its own, the
/// scales ease back to 1 with it, so the icons stay on their full-screen backdrop.
inline Scales ForFrame(bool engaged, unsigned mode, int across_percent, int up_down_percent,
                       float menu_weight) {
    if (!engaged || (mode != 2 && mode != 3)) {
        return {};
    }
    const float w = std::isfinite(menu_weight) ? std::clamp(menu_weight, 0.0f, 1.0f) : 0.0f;
    const auto eased = [w](int percent) {
        const float s = ScaleForPercent(percent);
        return w == 0.0f ? s : s + (1.0f - s) * w;
    };
    return {eased(across_percent), eased(up_down_percent)};
}

/// The divisor of one clip component in the vertex shader (VrFactor with the HUD scale): the
/// periphery `factor` of that axis for every vertex; for a 2D vertex (w = 1) on or past the edge
/// 1, inside the view `factor / hud_scale`.
inline float ClipDivisor(float clip, float w, float factor, float hud_scale) {
    if (std::abs(w - 1.0f) < kFlatW) {
        return std::abs(clip) >= kEdge ? 1.0f : factor / hud_scale;
    }
    return factor;
}

/// A clip position after the vertex shader (the C++ form of VrFactor): `clip_x` is up and down,
/// `clip_y` across. `factor` is across, `factor_y` up and down (as shader_uniforms.h).
struct Clip {
    float x, y, w;
};
inline Clip ApplyVertex(Clip p, float factor, float factor_y, Scales s) {
    return {p.x / ClipDivisor(p.x, p.w, factor_y, s.up_down),
            p.y / ClipDivisor(p.y, p.w, factor, s.across), p.w};
}

} // namespace VideoCore::HudMove
