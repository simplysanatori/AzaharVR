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

/// The right eye's left edge, where a layer moved to the right leaves room. False: nothing is there
/// (kBeyond): every layer ends where it ends in the left eye's picture, which stays whole. The edge
/// the eyes see is the layer's own, at its depth, and a HUD on the screen is whole in both. True:
/// both eyes' pictures are cut (Cut), with what the HUD has there. Layers moved to the left
/// (Directed) are cut either way: they stand in front of the screen's edges.
constexpr bool kCutsEdge = false;

/// What a picture shows where there is no layer left.
constexpr uint32_t kBeyond = 0xFF000000u;

/// The columns a layer moved this far leaves at the picture's left edge.
constexpr int32_t EdgeColumns(int32_t shift) {
    return kCutsEdge ? 0 : std::max(shift, 0);
}

/// FrameOutput::layer_shift of a picture whose farthest layer is moved this far.
constexpr float EdgeShift(int32_t farthest, uint32_t width_px) {
    return kCutsEdge || farthest < 0 ? static_cast<float>(farthest) / static_cast<float>(width_px)
                                     : 0.0f;
}

/// How much of each eye's picture is left out. A layer the right eye has moved brings what is
/// beyond the game's picture in at one edge, and what the other eye has there is in neither: each
/// eye loses as much as the farthest layer moves, which leaves a window that stands where the
/// nearest layer does. So nothing is cut off by an edge behind it, and no edge has anything the
/// game does not show.
///
/// `shift`: FrameOutput::layer_shift. Every layer's is between 0 and it.
constexpr float Cut(float shift) {
    return shift < 0.0f ? -shift : shift;
}

/// The eye's right edge is cut, else its left one.
constexpr bool CutsRightEdge(bool right_eye, float shift) {
    return right_eye && shift < 0.0f;
}

/// Where the middle of what is left goes, a share of the picture's width to the right of the
/// screen's middle, so that the window is in the middle of the screen and the layers are as far
/// apart as the pictures have them.
constexpr float Offset(bool right_eye, float shift) {
    if (shift >= 0.0f) {
        return 0.0f;
    }
    return right_eye ? 0.5f * shift : -0.5f * shift;
}

/// The same for a screen that takes touches, whose left eye's picture is then where it is without
/// depth: a tap lands where the left eye sees it.
constexpr float OffsetInPlace(bool right_eye, float shift) {
    const float half = 0.5f * Cut(shift);
    return CutsRightEdge(right_eye, shift) ? -half : half;
}

/// Between the XR thread and the emulation thread.
struct Shared {
    /// XR thread: MaxShift of the widest screen on view that has depth (else the Primary Screen).
    std::atomic<float> max_shift{kOnScreen};
    /// The frontend: Invert Depth. The cores read it every frame.
    std::atomic<bool> inverted{false};
    /// XR thread: the bottom screen is in the main slot (Swap Screens). The cores read it every
    /// frame.
    std::atomic<bool> swapped{false};
};

inline Shared& Get() {
    static Shared shared;
    return shared;
}

} // namespace Common::LayerDepth
