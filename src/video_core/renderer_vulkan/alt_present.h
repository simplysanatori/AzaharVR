// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <array>
#include "common/common_types.h"
#include "common/vector_math.h"

// What both of AltXrRenderer's outputs (the XR screens, the window) send the 3DS's present shaders.
namespace Vulkan {

struct AltScreenVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};
static_assert(sizeof(AltScreenVertex) == sizeof(float) * 4,
              "AltScreenVertex must match XrPresentTarget vertex layout");

struct AltPresentUniformData {
    std::array<f32, 4 * 4> modelview{};
    Common::Vec4f i_resolution{};
    Common::Vec4f o_resolution{};
    int screen_id_l = 0;
    int screen_id_r = 0;
    int layer = 0;
    int reverse_interlaced = 0;
};
static_assert(sizeof(AltPresentUniformData) == 112,
              "AltPresentUniformData must match present shader push constants");

/// The overlay's texture among the present shader's three: the right eye's, which these games
/// never have.
constexpr u32 kAltOverlaySlot = 1;

/// Internal Resolution's highest for a GBA or Game Boy game: the headset's highest for a 3DS game.
constexpr u32 kAltMaxScale = 8;
/// The size a picture is never scaled past: the 3DS's top screen at that.
constexpr u32 kAltMaxWidth = 400 * kAltMaxScale;
constexpr u32 kAltMaxHeight = 240 * kAltMaxScale;
/// The most while it is not known how big the headset shows the screen.
constexpr u32 kAltUnknownScale = 4;
/// A picture with an overlay, whose pixels are smaller than the game's, is scaled by this or more.
constexpr u32 kAltOverlayScale = 2;
/// The headset's own resampling shows somewhat more than its eye image's pixels.
constexpr float kAltXrOversample = 1.25f;
/// How far past the middle between two scales the shown size goes before the scale follows.
constexpr float kAltScaleHold = 0.15f;

/// Whole times a GBA or Game Boy picture of this size is drawn for Internal Resolution `setting`
/// (0: Auto, all the screen shows). `shown`: display pixels one of the game's pixels covers (0: not
/// known); a picture scaled past the nearest whole number to it shimmers when it is shown smaller
/// again, and is no sharper. `last`: the scale now, kept while `shown` is about halfway between two
/// (0: none).
constexpr u32 AltPictureScale(u32 setting, u32 width, u32 height, float shown, u32 last) {
    const u32 most = std::min(setting == 0 ? kAltMaxScale : setting, kAltMaxScale);
    u32 scale = std::min(most, kAltUnknownScale);
    if (shown > 0.0f) {
        scale = static_cast<u32>(std::clamp(shown, 1.0f, static_cast<float>(kAltMaxScale)) + 0.5f);
        const float off = shown - static_cast<float>(last);
        if (last > 0 && off < 0.5f + kAltScaleHold && off > -0.5f - kAltScaleHold) {
            scale = last;
        }
        scale = std::min(scale, most);
    }
    const u32 fits = std::min(kAltMaxWidth / std::max(width, 1u),
                              kAltMaxHeight / std::max(height, 1u));
    return std::clamp(scale, 1u, std::max(fits, 1u));
}

/// A screen's quad. Upright (not `landscape`): turned 90 degrees anticlockwise, as the 3DS's
/// screens. An overlay is drawn by the same quad, so it covers the screen exactly.
constexpr std::array<AltScreenVertex, 4> AltScreenQuad(float x, float y, float w, float h,
                                                       bool landscape) {
    if (landscape) {
        return {{
            {x, y, 0.0f, 0.0f},
            {x + w, y, 1.0f, 0.0f},
            {x, y + h, 0.0f, 1.0f},
            {x + w, y + h, 1.0f, 1.0f},
        }};
    }
    return {{
        {x, y, 1.0f, 0.0f},
        {x + w, y, 1.0f, 1.0f},
        {x, y + h, 0.0f, 0.0f},
        {x + w, y + h, 0.0f, 1.0f},
    }};
}

inline std::array<f32, 4 * 4> Orthographic(u32 width, u32 height) {
    // clang-format off
    return {2.f / width, 0.f,          0.f, -1.f,
            0.f,         2.f / height, 0.f, -1.f,
            0.f,         0.f,          1.f,  0.f,
            0.f,         0.f,          0.f,  1.f};
    // clang-format on
}

} // namespace Vulkan
