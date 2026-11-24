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
