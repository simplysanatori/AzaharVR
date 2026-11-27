// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharVR foveation: where the eyes look, in the eye picture's own coordinates (0..1, v down),
// for the shading rate image's centre. The XR thread publishes it (frame_xr/controls.cpp), the
// renderer reads it. Pure (unit-tested).

#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

namespace VideoCore::Gaze {

enum class Source : int { None = 0, Meta = 1, Ext = 2 };

/// The picture the game's top screen makes in the view: 126 x 112 degrees, its middle 5.1 degrees
/// below the headset's forward direction (immersive.cpp's log line).
inline constexpr float kHalfTanX = 1.9626f; // tan(63 degrees)
inline constexpr float kHalfTanY = 1.4826f; // tan(56 degrees)
inline constexpr float kMiddleTanY = -0.0892f; // tan(-5.1 degrees)
/// Where straight ahead lands in the picture (the fixed centre).
inline constexpr float kForwardU = 0.5f;
inline constexpr float kForwardV = 0.5f + kMiddleTanY / (2.0f * kHalfTanY); // 0.47

/// A view direction as its tangents (x / -z, y / -z) to the picture's (u, v), v down.
inline void TanToPicture(float tx, float ty, float& u, float& v) {
    u = 0.5f + tx / (2.0f * kHalfTanX);
    v = 0.5f - (ty - kMiddleTanY) / (2.0f * kHalfTanY);
}

inline std::atomic<bool> g_valid{false};
inline std::atomic<float> g_u{kForwardU};
inline std::atomic<float> g_v{kForwardV};
inline std::atomic<int> g_source{0};
inline std::atomic<std::uint32_t> g_samples{0};       ///< frames asked
inline std::atomic<std::uint32_t> g_valid_samples{0}; ///< of them with a valid gaze
inline std::atomic<bool> g_available{false};          ///< headset runtime offers eye tracking
inline std::atomic<bool> g_wanted{false};             ///< foveation with gaze is on

/// One frame's gaze (a view direction as tangents, valid or not). Smoothed a little: a saccade
/// should not show coarse pixels where the eyes land.
inline void Publish(bool valid, float tx, float ty, Source source) {
    g_samples.fetch_add(1, std::memory_order_relaxed);
    if (!valid || !std::isfinite(tx) || !std::isfinite(ty)) {
        g_valid.store(false, std::memory_order_relaxed);
        return;
    }
    g_valid_samples.fetch_add(1, std::memory_order_relaxed);
    float u, v;
    TanToPicture(tx, ty, u, v);
    u = std::fmin(std::fmax(u, -0.1f), 1.1f);
    v = std::fmin(std::fmax(v, -0.1f), 1.1f);
    if (g_valid.load(std::memory_order_relaxed)) { // exponential smoothing
        constexpr float kKeep = 0.5f;
        u = kKeep * g_u.load(std::memory_order_relaxed) + (1.0f - kKeep) * u;
        v = kKeep * g_v.load(std::memory_order_relaxed) + (1.0f - kKeep) * v;
    }
    g_u.store(u, std::memory_order_relaxed);
    g_v.store(v, std::memory_order_relaxed);
    g_source.store(static_cast<int>(source), std::memory_order_relaxed);
    g_valid.store(true, std::memory_order_relaxed);
}

/// The centre to use now (the gaze, or straight ahead when there is none).
inline void Centre(float& u, float& v) {
    if (g_wanted.load(std::memory_order_relaxed) && g_valid.load(std::memory_order_relaxed)) {
        u = g_u.load(std::memory_order_relaxed);
        v = g_v.load(std::memory_order_relaxed);
    } else {
        u = kForwardU;
        v = kForwardV;
    }
}

/// A picture point (u, v; v down) in the rate image of a w x h framebuffer, as 0..1 along its
/// columns and rows. The surface is the PICA's: 240 x 400, the column going up the screen and the
/// row along it, left to right; a 400 x 240 one is taken as it is. flip: 1 = reverse the rows,
/// 2 = reverse the columns (a test switch, if the picture's side is mistaken).
inline void PictureToImage(float u, float v, std::uint32_t w, std::uint32_t h, int flip, float& cx,
                           float& cy) {
    if (w < h) {
        cx = 1.0f - v;
        cy = u;
    } else {
        cx = u;
        cy = v;
    }
    if (flip & 1) {
        cy = 1.0f - cy;
    }
    if (flip & 2) {
        cx = 1.0f - cx;
    }
}

} // namespace VideoCore::Gaze
