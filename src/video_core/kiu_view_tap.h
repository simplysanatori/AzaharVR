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
#include <atomic>
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

} // namespace VideoCore::Immersive
