// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame hudmove: HUD Spacing (experimental, per game, 100 % = the game's own by default).
//
// In the Full modes the game draws more than its own frame (Render Margin, the scales in Field
// of View) and the screen is made large, so a HUD the game puts at its frame's edges ends up at
// the edges of the view, or past them (Mario Kart 7). HUD Spacing sets the flat 2D drawing on the
// Primary screen's distance from the middle as a percentage of the game's own, separately across
// and up and down: below 100 % it comes in, above it spreads out.
//
// Which draws move is the vertex shader's existing rule for 2D (glsl_shader_gen.cpp VrFactor): a
// vertex with w = 1 is 2D (an orthographic projection; the world's perspective vertices have
// their depth in w), and a 2D vertex on or past an edge of the view keeps its place, so
// full-screen 2D passes (masks, copies of the last frame) still cover the whole picture. Only the
// 2D vertices inside the view are scaled about the middle: no register of any game is read, so
// a game whose HUD is not drawn with w = 1 shows no change rather than a wrong one.
//
// Mario Kart 7 draws its race HUD (rank, item box) through a perspective projection instead (w
// about 1525), every vertex at the far end of the depth range: w - z = 0.049 (clip z after the
// shader's flip; 0 is the far plane), where its world's vertices keep w - z = 2.1 to 3 (desktop
// census from boot through the menus, a race and the first-person view, 476 million vertices:
// none below 2.10). Its preset turns on a second rule
// (ImmersivePreset::hud_far_plane): a vertex with w - z below 0.5 is scaled the same way (one
// on or past an edge of the view, |x| or |y| >= 0.999 w, keeps the world's factor, so a
// full-screen pass there stays whole). Off for every other game, so their world can never meet
// it. Its menus draw the same way and move too.
//
// The 3DS's bottom screen is drawn with the same 2D vertices; a draw into its 240 x 320 viewport
// gets no HUD scale (the rasterizer checks it per draw, ForDraw).
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

namespace VideoCore::HudSpacing {

/// The settings' range: an element's distance from the middle in percent of the game's own.
/// 90 % across moves an element at the edge of a 400 pixel picture 20 pixels in. The owner's range
/// (2026-11-18): 0 to 150, for full control; 0 draws the HUD at the middle (kZeroScale).
inline constexpr int kMinSpacing = 0;
inline constexpr int kMaxSpacing = 150;
inline constexpr int kDefaultSpacing = 100;
/// The shader's 2D test (VrFactor): |w - 1| below this.
inline constexpr float kFlatW = 0.0001f;
/// The shader's edge test (VrFactor): a 2D vertex at or past this keeps its place.
inline constexpr float kEdge = 0.999f;
/// The far plane test (VrFactor, with hud_far_plane): |w - z| below this (clip units).
inline constexpr float kFarPlane = 0.5f;
/// What the rasterizer accepts (anything else is taken as 1, no change).
inline constexpr float kMinScale = 0.01f;
inline constexpr float kMaxScale = 1.5f;
/// The scale 0 % gives: the shader divides by the scale, so 0 itself is never sent.
inline constexpr float kZeroScale = kMinScale;

inline int ClampSpacing(int spacing) {
    return std::clamp(spacing, kMinSpacing, kMaxSpacing);
}

/// The scale of 2D positions about the middle for `spacing`: exactly 1 at 100, 0.9 at 90, and
/// kZeroScale at 0 (never 0: the shader divides by it).
inline float ScaleForSpacing(int spacing) {
    return std::max(static_cast<float>(ClampSpacing(spacing)) / 100.0f, kZeroScale);
}

/// A scale the rasterizer can use: finite and within kMinScale to kMaxScale, else 1.
inline float UsableScale(float scale) {
    return std::isfinite(scale) && scale >= kMinScale && scale <= kMaxScale ? scale : 1.0f;
}

/// The two scales: across the picture (clip y: the top screen is drawn turned) and up and down
/// (clip x). `far_plane`: the game's HUD is drawn on the far plane (its preset's hud_far_plane).
struct Scales {
    float across = 1.0f;
    float up_down = 1.0f;
    bool far_plane = false;

    bool IsIdentity() const {
        return across == 1.0f && up_down == 1.0f;
    }
    bool operator==(const Scales& o) const {
        return across == o.across && up_down == o.up_down && far_plane == o.far_plane;
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
inline Scales ForFrame(bool engaged, unsigned mode, int spacing_x, int spacing_y,
                       float menu_weight) {
    if (!engaged || (mode != 2 && mode != 3)) {
        return {};
    }
    const float w = std::isfinite(menu_weight) ? std::clamp(menu_weight, 0.0f, 1.0f) : 0.0f;
    const auto eased = [w](int spacing) {
        const float s = ScaleForSpacing(spacing);
        return w == 0.0f ? s : s + (1.0f - s) * w;
    };
    return {eased(spacing_x), eased(spacing_y)};
}

/// The scales one draw uses (emulation thread, per draw): none for a draw into the bottom
/// screen's viewport (240 x 320, drawn turned), `s` for everything else.
inline Scales ForDraw(Scales s, float viewport_width, float viewport_height) {
    if (viewport_width == 240.0f && viewport_height == 320.0f) {
        return {};
    }
    return s;
}

/// Whether a vertex lies at the far plane (clip z = w, as the shader sees it: after the z flip).
inline bool OnFarPlane(float z, float w) {
    return std::abs(w - z) < kFarPlane;
}

/// The divisor of one clip component in the vertex shader (VrFactor with the HUD scale): the
/// periphery `factor` of that axis for every vertex; for a 2D vertex (w = 1) on or past the edge
/// 1, inside the view `factor / hud_scale`; with `far_plane`, a vertex at the far plane inside
/// the view `factor / hud_scale` too (on or past the edge `factor`).
inline float ClipDivisor(float clip, float z, float w, float factor, float hud_scale,
                         bool far_plane) {
    if (std::abs(w - 1.0f) < kFlatW) {
        return std::abs(clip) >= kEdge ? 1.0f : factor / hud_scale;
    }
    if (far_plane && OnFarPlane(z, w)) {
        return std::abs(clip) >= kEdge * std::abs(w) ? factor : factor / hud_scale;
    }
    return factor;
}

/// A clip position after the vertex shader (the C++ form of VrFactor): `clip_x` is up and down,
/// `clip_y` across, `z` after the shader's z flip. `factor` is across, `factor_y` up and down
/// (as shader_uniforms.h).
struct Clip {
    float x, y, w;
    float z = 0.0f;
};
inline Clip ApplyVertex(Clip p, float factor, float factor_y, Scales s) {
    return {p.x / ClipDivisor(p.x, p.z, p.w, factor_y, s.up_down, s.far_plane),
            p.y / ClipDivisor(p.y, p.z, p.w, factor, s.across, s.far_plane), p.w, p.z};
}

} // namespace VideoCore::HudSpacing
