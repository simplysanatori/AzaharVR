// Copyright 2026 AzaharVR contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <cstdint>

namespace FrameXr {

/// The game screens, as the renderer says. Written on its thread, read on the XR thread.
class ScreensState {
public:
    /// The renderer makes images (a game runs), or none any more.
    void SetLive(bool on) {
        if (on) {
            bits = kLive;
        } else {
            bits &= ~kLive;
        }
    }
    /// The screens are about to be off for a moment, made again for the same game. Until they are
    /// live again, or it stops.
    void SetRemade(bool on) {
        if (on) {
            bits |= kRemade;
        } else {
            bits &= ~kRemade;
        }
    }

    bool Live() const {
        return (bits.load() & kLive) != 0;
    }
    /// A game runs: what it has of its own (swapped or hidden screens, its placement) stays.
    bool GameOn() const {
        return bits.load() != 0;
    }

private:
    static constexpr uint32_t kLive = 1;
    static constexpr uint32_t kRemade = 2;
    std::atomic<uint32_t> bits{0};
};

} // namespace FrameXr
