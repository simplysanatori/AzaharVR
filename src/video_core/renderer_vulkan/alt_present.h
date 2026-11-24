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

inline std::array<f32, 4 * 4> Orthographic(u32 width, u32 height) {
    // clang-format off
    return {2.f / width, 0.f,          0.f, -1.f,
            0.f,         2.f / height, 0.f, -1.f,
            0.f,         0.f,          1.f,  0.f,
            0.f,         0.f,          0.f,  1.f};
    // clang-format on
}

} // namespace Vulkan
