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
#include <optional>
#include <vector>

namespace VideoCore::Foveation {

/// The test switch (read at the start; it is put in the environment from test-switches.txt).
inline constexpr const char* kSwitch = "AZAHARFRAME_XR_FOVEATION";

/// 0 (off, also for anything else), 1 (Low), 2 (Medium), or 3 (High).
inline int LevelFromText(const char* text) {
    if (!text || !*text) {
        return 0;
    }
    const long v = std::strtol(text, nullptr, 10);
    return v >= 3 ? 3 : (v == 2 ? 2 : (v == 1 ? 1 : 0));
}

/// Returns the test-switch override when AZAHARFRAME_XR_FOVEATION is set and non-empty.
inline std::optional<int> EnvOverride() {
    const char* env = std::getenv(kSwitch);
    if (!env || !*env) {
        return std::nullopt;
    }
    return LevelFromText(env);
}

inline int LevelFromEnv() {
    return LevelFromText(std::getenv(kSwitch));
}

/// Effective foveation level (0..3): AZAHARFRAME_XR_FOVEATION overrides if set; otherwise uses
/// the menu setting only when eye tracking is available on the headset (Steam Frame).
inline int EffectiveLevel(std::uint32_t setting_level, bool eye_tracking_available) {
    if (const auto env = EnvOverride(); env.has_value()) {
        return *env;
    }
    if (!eye_tracking_available) {
        return 0;
    }
    return static_cast<int>(std::min<std::uint32_t>(setting_level, 3u));
}

/// A texel of the shading rate image: log2(width) << 2 | log2(height) of the fragment size.
inline constexpr std::uint8_t kRate1x1 = 0;
inline constexpr std::uint8_t kRate1x2 = (0 << 2) | 1;
inline constexpr std::uint8_t kRate2x2 = (1 << 2) | 1;
inline constexpr std::uint8_t kRate2x4 = (1 << 2) | 2;
inline constexpr std::uint8_t kRate4x4 = (2 << 2) | 2;

inline constexpr std::uint32_t kMinInnerPct = 10;
inline constexpr std::uint32_t kMaxInnerPct = 80;
inline constexpr std::uint32_t kMinMiddlePct = 20;
inline constexpr std::uint32_t kMaxMiddlePct = 100;
inline constexpr std::uint32_t kMaxRateIndex = 4;

/// Shading rate byte for a menu rate index: 0=1x1, 1=1x2, 2=2x2, 3=2x4, 4=4x4.
inline constexpr std::uint8_t RateByte(std::uint32_t index) {
    switch (index) {
    case 0:
        return kRate1x1;
    case 1:
        return kRate1x2;
    case 2:
        return kRate2x2;
    case 3:
        return kRate2x4;
    default:
        return kRate4x4;
    }
}

/// Complete foveation parameters for the three concentric rings around the gaze point.
struct Params {
    int level = 0; ///< 0 = Off, 1 = Medium, 2 = High (or env override 1..3)
    std::uint32_t inner_pct = 40;
    std::uint32_t middle_pct = 70;
    std::uint32_t middle_rate = 2; ///< 0=1x1, 1=1x2, 2=2x2, 3=2x4, 4=4x4
    std::uint32_t outer_rate = 4;
    bool operator==(const Params&) const = default;
};

/// Preset parameters for VR menu notches: 1 = Medium (40%/70%, 2x2/4x4), 2 = High (20%/45%,
/// 2x2/4x4). Any level <= 0 is Off.
inline constexpr Params PresetParams(int level) {
    if (level <= 0) {
        return Params{0, 40, 70, 2, 4};
    }
    if (level == 1) {
        return Params{1, 40, 70, 2, 4};
    }
    return Params{2, 20, 45, 2, 4};
}

/// Parameters for a test-switch override (AZAHARFRAME_XR_FOVEATION=0..3):
/// 1 = legacy 60% 1x1 / 2x2 outside, 2 = Medium (40%/70% 2x2/4x4), 3 = High (20%/45% 2x2/4x4).
inline constexpr Params EnvParams(int env_level) {
    if (env_level <= 0) {
        return Params{0, 40, 70, 2, 4};
    }
    if (env_level == 1) {
        return Params{1, 60, 100, 2, 2};
    }
    if (env_level == 2) {
        return Params{2, 40, 70, 2, 4};
    }
    return Params{3, 20, 45, 2, 4};
}

/// Effective foveation parameters: AZAHARFRAME_XR_FOVEATION overrides if set; otherwise uses the
/// VR menu settings only when eye tracking is available on the headset (Steam Frame).
inline Params EffectiveParams(std::uint32_t setting_level, bool eye_tracking_available,
                              std::uint32_t inner_pct, std::uint32_t middle_pct,
                              std::uint32_t middle_rate, std::uint32_t outer_rate) {
    if (const auto env = EnvOverride(); env.has_value()) {
        return EnvParams(*env);
    }
    if (!eye_tracking_available || setting_level == 0) {
        return Params{0, 40, 70, 2, 4};
    }
    return Params{
        static_cast<int>(std::min<std::uint32_t>(setting_level, 2u)),
        std::clamp(inner_pct, kMinInnerPct, kMaxInnerPct),
        std::clamp(middle_pct, kMinMiddlePct, kMaxMiddlePct),
        std::min(middle_rate, kMaxRateIndex),
        std::min(outer_rate, kMaxRateIndex),
    };
}

/// Shading rate for a normalised ellipse distance `d` from the fovea centre.
inline std::uint8_t RateForDistance(const Params& p, float d) {
    if (p.level <= 0) {
        return kRate1x1;
    }
    const float inner = static_cast<float>(p.inner_pct) * 0.01f;
    const float middle = std::max(inner, static_cast<float>(p.middle_pct) * 0.01f);
    if (d <= inner) {
        return kRate1x1;
    }
    if (d <= middle) {
        return RateByte(p.middle_rate);
    }
    return RateByte(p.outer_rate);
}

inline std::uint8_t RateForDistance(int level, float d) {
    return RateForDistance(EnvParams(level), d);
}

/// The rate of the texel at (x, y) of a w x h image. The centre of the full rate is at
/// (centre_x, centre_y) of the picture; the rings are ellipses of the picture (radius 1 = half its
/// width/height).
inline std::uint8_t RateAt(const Params& p, std::uint32_t x, std::uint32_t y, std::uint32_t w,
                           std::uint32_t h, float centre_y = 0.5f, float centre_x = 0.5f) {
    if (p.level <= 0) {
        return kRate1x1;
    }
    const float dx = ((static_cast<float>(x) + 0.5f) / static_cast<float>(w) - centre_x) * 2.0f;
    const float dy =
        ((static_cast<float>(y) + 0.5f) / static_cast<float>(h) - centre_y) * 2.0f;
    return RateForDistance(p, std::sqrt(dx * dx + dy * dy));
}

inline std::uint8_t RateAt(int level, std::uint32_t x, std::uint32_t y, std::uint32_t w,
                           std::uint32_t h, float centre_y = 0.5f, float centre_x = 0.5f) {
    return RateAt(EnvParams(level), x, y, w, h, centre_y, centre_x);
}

/// The rate image for a picture of width x height pixels with square texels of `texel` pixels.
/// When the active eye picture occupies a sub-rectangle `(rect_x, rect_y, rect_w, rect_h)` inside
/// a padded surface (e.g. 240 x 400 inside 256 x 512), the foveal ellipses are centred and scaled
/// to that sub-rectangle instead of the full padded surface.
inline std::vector<std::uint8_t> MakeRateImage(const Params& p, std::uint32_t width,
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
                    RateAt(p, x, y, w, h, centre_y, centre_x);
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
            if (p.level <= 0) {
                out[static_cast<std::size_t>(y) * w + x] = kRate1x1;
                continue;
            }
            const float px = (static_cast<float>(x) + 0.5f) * t;
            const float dx = ((px - rx) / rw - centre_x) * 2.0f;
            const float d = std::sqrt(dx * dx + dy * dy);
            out[static_cast<std::size_t>(y) * w + x] = RateForDistance(p, d);
        }
    }
    return out;
}

inline std::vector<std::uint8_t> MakeRateImage(int level, std::uint32_t width,
                                               std::uint32_t height, std::uint32_t texel,
                                               float centre_y = 0.5f, float centre_x = 0.5f,
                                               std::uint32_t rect_x = 0, std::uint32_t rect_y = 0,
                                               std::uint32_t rect_w = 0,
                                               std::uint32_t rect_h = 0) {
    return MakeRateImage(EnvParams(level), width, height, texel, centre_y, centre_x, rect_x, rect_y,
                         rect_w, rect_h);
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
