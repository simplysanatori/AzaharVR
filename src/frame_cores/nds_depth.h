// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Layer Depth's arithmetic for DS games: how far things are to the right in the right eye's picture.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "common/layer_depth.h"

namespace FrameCores::NdsDepth {

constexpr int kWidth = 256;

/// How many pixels to the right a layer of a priority is. Priority 0 (text, HUD) stays at the
/// screen; nothing comes in front of it.
using LayerShifts = std::array<int, 4>;

/// BGxHOFS of a text background moved right; the hardware wraps it.
constexpr uint16_t ShiftScroll(uint16_t scroll, int shift) {
    return static_cast<uint16_t>(scroll - shift);
}

struct AffinePoint {
    int32_t x;
    int32_t y;
    constexpr bool operator==(const AffinePoint&) const = default;
};

/// The line's reference point of a rotated or scaled background moved right on the screen. One
/// pixel to the right is (RotA, RotC) in the background, so both coordinates change.
constexpr AffinePoint ShiftAffine(AffinePoint ref, int16_t rot_a, int16_t rot_c, int shift) {
    return {ref.x - shift * rot_a, ref.y - shift * rot_c};
}

/// A sprite's second attribute with its x moved by its own priority. x is 9 bits: past the right
/// edge it wraps to -256 and up, and no sprite is wide enough (128 at most) to reach the screen
/// again from there. Window sprites stay, as the windows do.
constexpr uint16_t ShiftSprite(uint16_t attr0, uint16_t attr1, uint16_t attr2,
                               const LayerShifts& shifts) {
    const bool disabled = ((attr0 >> 8) & 3) == 2;
    const bool window = ((attr0 >> 10) & 3) == 2;
    if (disabled || window) {
        return attr1;
    }
    const int shift = shifts[(attr2 >> 10) & 3];
    return static_cast<uint16_t>((attr1 & ~0x1FF) | ((attr1 + shift) & 0x1FF));
}

/// Vertex::Position[3] is 20.12 fixed point, cut to 24 bits by the DS: 1.0 is 0x1000, and the most
/// is just under 4096.0.
constexpr int32_t kOneW = 0x1000;

/// x on the screen is 9 bits. Past 256 the rasteriser drops pixels; below 0 it would write into
/// the row above.
constexpr int32_t kMaxX = 0x1FF;

struct SceneShift {
    int strength;        ///< pixels to the right for what is far away
    int32_t convergence; ///< the w that sits on the screen, 20.12
    int limit;           ///< the most a vertex moves, either way, in pixels
    constexpr bool operator==(const SceneShift&) const = default;
};

/// How many pixels to the right a vertex is: strength * (1 - convergence / w), rounded, within the
/// limit. Never less for a larger w.
constexpr int VertexShift(const SceneShift& scene, int32_t w) {
    if (w <= 0) {
        return 0;
    }
    const int64_t pixels_w = int64_t{scene.strength} * (int64_t{w} - scene.convergence);
    const int64_t half = pixels_w < 0 ? -(w / 2) : w / 2;
    return static_cast<int>(
        std::clamp<int64_t>((pixels_w + half) / w, -scene.limit, scene.limit));
}

constexpr int32_t ShiftVertexX(int32_t x, int shift) {
    return std::clamp<int32_t>(x + shift, 0, kMaxX);
}

/// The whole polygon has w 1: orthographic, so drawn on the screen, not in the scene. Its vertices
/// do not move. One w for all of them is not enough: what faces the camera in the scene (a
/// billboard, a wall) has that too.
constexpr bool IsOnScreen(const int32_t* w, std::size_t count) {
    return std::all_of(w, w + count, [](int32_t value) { return value == kOneW; });
}

struct Depth {
    bool on = false; ///< the right eye is drawn
    LayerShifts layers{};
    SceneShift scene{};
    constexpr bool operator==(const Depth&) const = default;
};

/// What is far away in the scene is as deep as the backmost 2D layer, so a 2D sky and 3D hills
/// meet. Convergence 4.0 until games have their own.
constexpr Depth MakeDepth(uint32_t level, float max_shift) {
    Depth depth;
    depth.on = level > 0 && max_shift > 0.0f;
    if (depth.on) {
        for (uint32_t priority = 0; priority < 4; ++priority) {
            depth.layers[priority] = static_cast<int>(
                Common::LayerDepth::LayerShift(level, priority, 3, kWidth, max_shift));
        }
        depth.scene = {depth.layers[3], 4 * kOneW, depth.layers[3]};
    }
    return depth;
}

/// Lines of a right picture: drawn at all, not copied from the left one, and of those with 3D of
/// its own.
struct Lines {
    int drawn = 0;
    int moved = 0;
    int with_3d = 0;
};

} // namespace FrameCores::NdsDepth
