// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Auto Fire A and Auto Fire B: X and Y of a GBA or Game Boy game, which has no such buttons.

#pragma once

#include <cstdint>

#include "common/alt_core_types.h"

namespace FrameCores {

class AutoFire {
public:
    /// About 15 presses a second.
    static constexpr uint32_t kOn = 2;
    static constexpr uint32_t kOff = 2;

    /// Once for a frame of the game's: the buttons with the presses in.
    uint32_t Frame(uint32_t buttons) {
        using namespace Common::AltCore;
        return buttons | Press(a, (buttons & BtnX) != 0, BtnA) |
               Press(b, (buttons & BtnY) != 0, BtnB);
    }

private:
    static uint32_t Press(uint32_t& frames, bool held, uint32_t button) {
        if (!held) {
            frames = 0;
            return 0;
        }
        return frames++ % (kOn + kOff) < kOn ? button : 0;
    }

    uint32_t a = 0; ///< frames X has been held
    uint32_t b = 0;
};

} // namespace FrameCores
