// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// How big the headset shows each screen, from the XR thread (it places them) to the renderer of the
// GBA and Game Boy games, whose pictures are not scaled past it. Header only: video_core does not
// link frame_xr.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>

#include "video_core/vulkan_hooks.h"

namespace VideoCore::XrScreenCover {

/// Display pixels across a screen `width` wide, `distance` from the eye (the same unit), for an eye
/// image `eye_px` wide over `eye_tan` (tan(right) - tan(left) of its field of view), at the middle
/// of the view. 0: not known.
inline float Across(float width, float distance, float eye_tan, float eye_px) {
    if (!(width > 0.0f && distance > 0.01f && eye_tan > 0.0f && eye_px > 0.0f)) {
        return 0.0f;
    }
    return eye_px * (width / distance) / eye_tan;
}

/// Each screen's, as it was last shown. 0: not known.
inline std::array<std::atomic<float>, static_cast<std::size_t>(XrScreen::Count)> g_across{};

inline void Note(XrScreen screen, float across) {
    if (across > 0.0f) {
        g_across[static_cast<std::size_t>(screen)].store(across, std::memory_order_relaxed);
    }
}

inline float Get(XrScreen screen) {
    return g_across[static_cast<std::size_t>(screen)].load(std::memory_order_relaxed);
}

} // namespace VideoCore::XrScreenCover
