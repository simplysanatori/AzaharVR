// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharVR foveation: the layout of the shading rate image (fixed foveated rendering) for the
// immersive eye picture. Pure (unit-tested). AZAHARFRAME_XR_FOVEATION=0|1|2: 1 = 2x2 shading
// outside the central area, 2 = also 4x4 at the far edges.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace VideoCore::Foveation {

/// The test switch (read at the start; it is put in the environment from test-switches.txt).
inline constexpr const char* kSwitch = "AZAHARFRAME_XR_FOVEATION";

/// 0 (off, also for anything else), 1 or 2.
inline int LevelFromText(const char* text) {
    if (!text || !*text) {
        return 0;
    }
    const long v = std::strtol(text, nullptr, 10);
    return v >= 2 ? 2 : (v == 1 ? 1 : 0);
}

inline int LevelFromEnv() {
    return LevelFromText(std::getenv(kSwitch));
}

/// A texel of the shading rate image: log2(width) << 2 | log2(height) of the fragment size.
inline constexpr std::uint8_t kRate1x1 = 0;
inline constexpr std::uint8_t kRate2x2 = (1 << 2) | 1;
inline constexpr std::uint8_t kRate4x4 = (2 << 2) | 2;

/// The rate of the texel at (x, y) of a w x h image. The centre of the full rate is at
/// (centre_x, centre_y) of the picture; the rings are ellipses of the picture (radius 1 = half its
/// width/height). Level 1: 1x1 inside 0.6, 2x2 outside. Level 2: 1x1 inside 0.5, 2x2 to 0.85,
/// 4x4 beyond.
inline std::uint8_t RateAt(int level, std::uint32_t x, std::uint32_t y, std::uint32_t w,
                           std::uint32_t h, float centre_y = 0.5f, float centre_x = 0.5f) {
    if (level <= 0) {
        return kRate1x1;
    }
    const float dx = ((static_cast<float>(x) + 0.5f) / static_cast<float>(w) - centre_x) * 2.0f;
    const float dy =
        ((static_cast<float>(y) + 0.5f) / static_cast<float>(h) - centre_y) * 2.0f;
    const float d = std::sqrt(dx * dx + dy * dy);
    if (level == 1) {
        return d <= 0.6f ? kRate1x1 : kRate2x2;
    }
    return d <= 0.5f ? kRate1x1 : (d <= 0.85f ? kRate2x2 : kRate4x4);
}

/// The rate image for a picture of width x height pixels with square texels of `texel` pixels.
/// When the active eye picture occupies a sub-rectangle `(rect_x, rect_y, rect_w, rect_h)` inside
/// a padded surface (e.g. 240 x 400 inside 256 x 512), the foveal ellipses are centred and scaled
/// to that sub-rectangle instead of the full padded surface.
inline std::vector<std::uint8_t> MakeRateImage(int level, std::uint32_t width,
                                               std::uint32_t height, std::uint32_t texel,
                                               float centre_y = 0.5f, float centre_x = 0.5f,
                                               std::uint32_t rect_x = 0, std::uint32_t rect_y = 0,
                                               std::uint32_t rect_w = 0,
                                               std::uint32_t rect_h = 0) {
    const std::uint32_t w = (width + texel - 1) / texel;
    const std::uint32_t h = (height + texel - 1) / texel;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h);
    const bool subrect = rect_w > 0 && rect_h > 0 &&
                         (rect_x != 0 || rect_y != 0 || rect_w != width || rect_h != height);
    if (!subrect) {
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                out[static_cast<std::size_t>(y) * w + x] =
                    RateAt(level, x, y, w, h, centre_y, centre_x);
            }
        }
        return out;
    }
    const float rw = static_cast<float>(rect_w);
    const float rh = static_cast<float>(rect_h);
    const float rx = static_cast<float>(rect_x);
    const float ry = static_cast<float>(rect_y);
    const float t = static_cast<float>(texel);
    for (std::uint32_t y = 0; y < h; ++y) {
        const float py = (static_cast<float>(y) + 0.5f) * t;
        const float dy = ((py - ry) / rh - centre_y) * 2.0f;
        for (std::uint32_t x = 0; x < w; ++x) {
            if (level <= 0) {
                out[static_cast<std::size_t>(y) * w + x] = kRate1x1;
                continue;
            }
            const float px = (static_cast<float>(x) + 0.5f) * t;
            const float dx = ((px - rx) / rw - centre_x) * 2.0f;
            const float d = std::sqrt(dx * dx + dy * dy);
            out[static_cast<std::size_t>(y) * w + x] =
                level == 1 ? (d <= 0.6f ? kRate1x1 : kRate2x2)
                           : (d <= 0.5f ? kRate1x1 : (d <= 0.85f ? kRate2x2 : kRate4x4));
        }
    }
    return out;
}

/// Is a framebuffer of this size the eye picture (the top screen: 400 x 240, or as stored 240 x
/// 400, or power-of-two padded 256 x 512 / 512 x 256, at the resolution scale)? Scale 2 and up
/// only (immersive mode renders higher).
inline bool IsEyePicture(std::uint32_t width, std::uint32_t height, std::uint32_t res_scale) {
    if (res_scale < 2) {
        return false;
    }
    if (width % res_scale != 0 || height % res_scale != 0) {
        return false;
    }
    const std::uint32_t w = width / res_scale;
    const std::uint32_t h = height / res_scale;
    return (w == 400 && h == 240) || (w == 240 && h == 400) || (w == 512 && h == 256) ||
           (w == 256 && h == 512);
}

} // namespace VideoCore::Foveation
