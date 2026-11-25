// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

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

/// The XR swapchain of a screen: its pixels a whole number of times, about the size that makes a
/// handheld's picture sharp edged (the console's own pixels up to 4 times). A picture over the
/// console's own size keeps the swapchain of the highest scale it may have (`max_scale`), so a
/// change of `scale` on its own (Dynamic Resolution) remakes nothing.
constexpr std::array<u32, 2> AltTargetSize(u32 width, u32 height, u32 scale, u32 max_scale) {
    constexpr u32 kMaxW = 1600;
    constexpr u32 kMaxH = 960;
    const u32 own = scale > 1 ? scale : 1;
    const u32 w = width / own > 1 ? width / own : 1;
    const u32 h = height / own > 1 ? height / own : 1;
    const u32 fit = kMaxW / w < kMaxH / h ? kMaxW / w : kMaxH / h;
    const u32 base = fit < 1 ? 1 : (fit > 4 ? 4 : fit);
    const u32 top = max_scale > own ? max_scale : own;
    u32 times = base; // 1x: as it was before there was a scale
    if (top > 1) {
        times = top;
        while (times < base) {
            times += top;
        }
    }
    return {w * times, h * times};
}

/// Whether a picture fills a target with the same number of pixels for each of its own.
constexpr bool AltTargetEven(u32 width, u32 height, u32 target_w, u32 target_h) {
    return width != 0 && height != 0 && target_w % width == 0 && target_h % height == 0 &&
           target_w / width == target_h / height;
}

/// The overlay's texture among the present shader's three: the right eye's, which these games
/// never have.
constexpr u32 kAltOverlaySlot = 1;

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
