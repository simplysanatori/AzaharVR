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

/// AzaharFrame kiu133: a test aid (AZAHARFRAME_KIU_DRAW_LOG, frame_xr/kiu_hooks.cpp): while set,
/// every draw is logged (texture 0, its size and format, the vertex count, the blend and depth
/// state, the colour buffer); the hooks set it for one VBlank to the next each time the switch's
/// value changes (for 40 ms).
inline std::atomic<bool> g_draw_log{false};

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
