// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Layer Depth's limits. The right eye's picture has its far layers moved to the right; on a screen
// in the headset they must never be farther apart than the eyes, which would have to turn outwards.
// Shifts are shares of the picture's width, so a core needs neither the IPD nor the screen's size.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace Common::LayerDepth {

constexpr uint32_t kMaxLevel = 3;
/// No headset to look outwards in.
constexpr float kNoLimit = 1.0f;
/// While the screen's size is not known yet: every layer is on it.
constexpr float kOnScreen = 1e-6f;

/// The farthest a layer may move on a screen `width_m` wide (IPD / W).
constexpr float MaxShift(float ipd_m, float width_m) {
    return ipd_m > 0.0f && width_m > 0.0f ? std::min(ipd_m / width_m, kNoLimit) : kOnScreen;
}

/// The whole pixels a layer `step` steps back moves at `level`, on a picture `width_px` wide whose
/// farthest layer is `farthest` steps back. Past the limit the layers are nearer to each other, in
/// the same order.
constexpr uint32_t LayerShift(uint32_t level, uint32_t step, uint32_t farthest, uint32_t width_px,
                              float max_shift) {
    if (farthest == 0) {
        return 0;
    }
    const float limit = max_shift * static_cast<float>(width_px) / static_cast<float>(farthest);
    const float per_step = std::min(static_cast<float>(std::min(level, kMaxLevel)), limit);
    return per_step > 0.0f ? static_cast<uint32_t>(per_step * static_cast<float>(step)) : 0;
}

/// Invert Depth: the layers move to the left instead, so the order is the other way round and the
/// farthest comes nearest. What stays on the screen is the same either way.
constexpr int32_t Directed(uint32_t shift, bool inverted) {
    const auto pixels = static_cast<int32_t>(shift);
    return inverted ? -pixels : pixels;
}

/// How far both eyes' pictures move, the same way, so that the farthest layer's two are half the
/// shift to each side of where a game without depth has it. Metres, to the right.
constexpr float Recentre(float farthest_shift, float width_m) {
    return -0.5f * farthest_shift * width_m;
}

/// Between the XR thread and the emulation thread.
struct Shared {
    /// XR thread: MaxShift of the Primary Screen on view.
    std::atomic<float> max_shift{kOnScreen};
    /// The frontend: Invert Depth. The cores read it every frame.
    std::atomic<bool> inverted{false};
    /// Emulation thread: the farthest layer's shift, a share of the width, below 0 while inverted;
    /// 0: no right picture.
    std::atomic<float> farthest_shift{0.0f};
};

inline Shared& Get() {
    static Shared shared;
    return shared;
}

} // namespace Common::LayerDepth
