// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuvr: the game's own view matrix of the last world draw mode 3 patched (three
// rows of four, as the game set them, before the patch), for Kid Icarus: Uprising's first person
// trace (frame_xr/kiu_first_person.h), which compares it with the game's scene camera. Stored
// only while `wanted` (a test switch). One writer (the thread that draws), read on the emulation
// thread at a VBlank.

#pragma once

#include <array>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>
#include "common/common_types.h"

namespace VideoCore::Immersive {

struct WorldViewTap {
    std::atomic<bool> wanted{false};
    std::atomic<u32> seq{0}; ///< odd while it is written
    std::atomic<u32> count{0};
    std::array<std::atomic<float>, 12> rows{};

    void Store(const float (&m)[3][4]) {
        seq.fetch_add(1, std::memory_order_acq_rel);
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 4; ++j) {
                rows[i * 4 + j].store(m[i][j], std::memory_order_relaxed);
            }
        }
        count.fetch_add(1, std::memory_order_relaxed);
        seq.fetch_add(1, std::memory_order_acq_rel);
    }
    /// The last matrix stored and how many were so far; false when none, or one is being written.
    bool Load(float out[12], u32& stored) const {
        const u32 before = seq.load(std::memory_order_acquire);
        if ((before & 1u) != 0) {
            return false;
        }
        for (int i = 0; i < 12; ++i) {
            out[i] = rows[i].load(std::memory_order_relaxed);
        }
        stored = count.load(std::memory_order_relaxed);
        return stored != 0 && seq.load(std::memory_order_acquire) == before;
    }
};
inline WorldViewTap g_world_view_tap;

/// AzaharFrame kiu2d: mode 3 turns the draws whose shaders run on the CPU (a geometry shader's:
/// vr_view_matrix.h ClipCorrection) with the world. Set each VBlank by Kid Icarus: Uprising's
/// first person (frame_xr/kiu_hooks.cpp; AZAHARFRAME_KIU_FP_2D=0 keeps it off); off elsewhere.
inline std::atomic<bool> g_soft_draw_turn{false};

/// AzaharFrame kiu140: the bottom screen is on view while immersive mode is engaged (Kid Icarus:
/// Uprising's Persistent or Wrist bottom screen): its draws keep the game's own size and camera
/// (no periphery factor, no head), so it looks the same in and out of battles. Set by the XR
/// thread each frame (frame_xr/immersive.cpp).
inline std::atomic<bool> g_bottom_as_drawn{false};
/// AzaharFrame kiu140: text drawn into a texture keeps its own size (Kid Icarus first person).
inline std::atomic<bool> g_text_as_drawn{false};

/// AzaharFrame kiu140: the angle the game draws its world with, counted over the uploads of its
/// camera into the top screen's viewport, in any view (patched or not): those within kNear of the
/// angle first person gave the battle camera (`want`, the tangent of half of it up and down) and
/// the others. A cutscene draws through a camera of its own, which that never reaches
/// (frame_xr/kiu_cutscene.h). `reg` is the view matrix's register, 0: off. Counted on the
/// emulation thread, read there at each VBlank (frame_xr/kiu_hooks.cpp).
struct DrawnAngle {
    static constexpr float kNear = 0.08f;
    std::atomic<u32> reg{0};
    std::atomic<float> want{0.0f};
    std::atomic<u32> match{0}, other{0};
    std::atomic<float> last_other{0.0f}; ///< the last other one's tangent (for the log)

    /// Enough of them this VBlank: the rest of its uploads need not be looked at.
    bool Enough() const {
        return match.load(std::memory_order_relaxed) + other.load(std::memory_order_relaxed) >= 64;
    }
    void Count(float tan_half_y) {
        const float w = want.load(std::memory_order_relaxed);
        const float off = w > 0.0f ? tan_half_y / w - 1.0f : 1.0f;
        if (off < kNear && off > -kNear) {
            match.fetch_add(1, std::memory_order_relaxed);
        } else {
            other.fetch_add(1, std::memory_order_relaxed);
            last_other.store(tan_half_y, std::memory_order_relaxed);
        }
    }
};
inline DrawnAngle g_drawn_angle;

/// AzaharFrame kiu131: Reticle Size. The draws with this texture (the game's reticle; 0: none)
/// are shaded on the CPU and scaled by g_reticle_scale about their middle on the screen, before
/// any kiu2d turn. Set each VBlank by Kid Icarus: Uprising's first person; 0 and 1 elsewhere.
inline std::atomic<u32> g_reticle_texture{0};
inline std::atomic<float> g_reticle_scale{1.0f};
/// Whether the draw with this texture (texture 0's address, 0 when it is off) is the reticle
/// and is to be scaled.
inline bool ReticleScaled(u32 texture0) {
    const u32 t = g_reticle_texture.load(std::memory_order_relaxed);
    return t != 0 && texture0 == t && g_reticle_scale.load(std::memory_order_relaxed) != 1.0f;
}

/// AzaharVR: in Angel's Aim's first person, the game's cone of streaks round the camera (a dark
/// and a bright one) is not drawn: from behind, in VR, it is a grid of streaks to a point. Set
/// by the hooks.
inline std::atomic<bool> g_hide_camera_cone{false};
/// The cone's draws: the texture (address, size, format), 36 vertices, no depth test.
inline bool CameraConeDraw(u32 address, u32 width, u32 height, u32 format, u32 vertices,
                           bool depth_test) {
    if (!g_hide_camera_cone.load(std::memory_order_relaxed) || vertices != 36 || depth_test) {
        return false;
    }
    return (address == 0x18402B00u && width == 128 && height == 128 && format == 12) ||
           (address == 0x1843B080u && width == 64 && height == 64 && format == 1);
}

/// AzaharFrame kiumark: the markers over enemies and people (test switches
/// AZAHARFRAME_KIU_MARKER_TEX=<hex>[,<hex>...] and AZAHARFRAME_KIU_MARKER_SIZE=<percent>, live):
/// the draws with these textures are scaled like the reticle, by g_marker_scale.
inline std::array<std::atomic<u32>, 4> g_marker_textures{};
inline std::atomic<float> g_marker_scale{1.0f};
/// The point a marker is scaled about (AZAHARFRAME_KIU_MARKER_ANCHOR): 0 its middle, 1 and 2 the
/// low and high ends across clip x (up and down on the turned top screen), 3 and 4 across clip y.
inline std::atomic<int> g_marker_anchor{1};
/// AZAHARFRAME_KIU_MARKER_DROP=<percent>: the anchor moved that part of the marker's height on
/// past the anchor's edge (toward the enemy), so a smaller marker sits lower.
inline std::atomic<float> g_marker_drop{0.25f};
/// AZAHARFRAME_KIU_MARKER_FAR=<w>: past this depth (clip w) a marker shrinks as 1/w, so a far
/// enemy's marker is small (0: off), down to a quarter.
inline std::atomic<float> g_marker_far{0.0f};
/// Whether this texture is one of the markers (not the reticle).
inline bool IsMarker(u32 texture0) {
    for (const auto& t : g_marker_textures) {
        if (texture0 != 0 && t.load(std::memory_order_relaxed) == texture0) {
            return true;
        }
    }
    return false;
}
/// The scale of the draw with this texture about its middle: the reticle's, a marker's, or 1.
inline float DrawScale(u32 texture0) {
    if (ReticleScaled(texture0)) {
        return g_reticle_scale.load(std::memory_order_relaxed);
    }
    const float k = g_marker_scale.load(std::memory_order_relaxed);
    if (texture0 != 0 && (k != 1.0f || g_marker_far.load(std::memory_order_relaxed) > 0.0f)) {
        for (const auto& t : g_marker_textures) {
            if (t.load(std::memory_order_relaxed) == texture0) {
                return k;
            }
        }
    }
    return 1.0f;
}

/// AzaharFrame kiu133: a test aid (AZAHARFRAME_KIU_DRAW_LOG, frame_xr/kiu_hooks.cpp): while set,
/// every draw is logged (texture 0, its size and format, the vertex count, the blend and depth
/// state, the colour buffer); the hooks set it for one VBlank to the next each time the switch's
/// value changes (for 40 ms).
inline std::atomic<bool> g_draw_log{false};

/// AzaharFrame kiugrid: a test aid (AZAHARFRAME_KIU_HIDE_TEX=<hex>[,<hex>...], live): draws whose
/// texture 0 is at one of these addresses are not drawn (0 = none).
inline std::array<std::atomic<u32>, 32> g_hide_textures{};
inline bool HideTexture(u32 texture0) {
    for (const auto& t : g_hide_textures) {
        const u32 v = t.load(std::memory_order_relaxed);
        if (v != 0 && v == texture0) {
            return true;
        }
    }
    return false;
}

/// AzaharFrame kiuhud: a test aid (AZAHARFRAME_KIU_HUD_CENSUS=<n>): every draw into the top
/// screen's viewport without a depth test is shaded on the CPU and logged once a kind (its
/// texture, blend, quads, place and what was done with it); a new n, or a new `state` (what the
/// hooks know of the game's UI), lists them again. 0: off.
inline std::atomic<u32> g_hud_census{0}, g_hud_census_state{0};
/// AzaharFrame kiuhud: AZAHARFRAME_KIU_HUD_COST=1: the time spent telling and moving the HUD's
/// draws (nanoseconds, draws asked, draws moved), taken by the hooks every few seconds.
inline std::atomic<bool> g_hud_cost{false};
inline std::atomic<u64> g_hud_cost_ns{0}, g_hud_cost_asked{0}, g_hud_cost_moved{0};

/// AzaharFrame kiu133: Pit Opacity (frame_xr/kiu_pit_opacity.cpp). Pit's draws are known by their
/// first vertex array's address: the hooks have the world's draws collected while he is drawn
/// (phase 1) and while his model is hidden (phase 2); his are the ones only in phase 1. Then
/// those of them the game draws opaque are blended with the constant alpha g_pit_alpha.
struct PitDraws {
    std::mutex mutex;
    std::vector<u32> seen[2]; ///< the depth-tested draws' addresses in phase 1 and 2
    std::vector<u32> pit;     ///< sorted: Pit's
    std::atomic<int> collect{0};
    std::atomic<float> alpha{1.0f};

    void Seen(u32 attr0) {
        const int c = collect.load(std::memory_order_relaxed);
        if (c == 1 || c == 2) {
            std::scoped_lock lock{mutex};
            seen[c - 1].push_back(attr0);
        }
    }
    /// The alpha for this draw: below 1 only for one of Pit's while Pit Opacity is in effect.
    float AlphaFor(u32 attr0) {
        const float a = alpha.load(std::memory_order_relaxed);
        if (a >= 1.0f) {
            return 1.0f;
        }
        std::scoped_lock lock{mutex};
        return std::binary_search(pit.begin(), pit.end(), attr0) ? a : 1.0f;
    }
};
inline PitDraws g_pit_draws;

} // namespace VideoCore::Immersive
