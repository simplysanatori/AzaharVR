// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>

#include "common/alt_core_types.h"

namespace FrameCores {

/// The controls profile in use (Settings::values.current_input_profile) as a DS, GBA or Game Boy
/// pad and touch screen: the same devices the emulated 3DS reads, so keyboard, gamepads, the VR
/// controllers and every remapping work as in a 3DS game. ZL, ZR and the C-Stick are on none of
/// these consoles.
class ProfileInput {
public:
    ProfileInput();
    ~ProfileInput();

    /// Reads the profile again after it changed. Any thread but the one changing it.
    void Reload();

    /// The state now. The touch is in fractions of a `touch_width` x `touch_height` screen.
    Common::AltCore::InputState Read(uint32_t touch_width, uint32_t touch_height) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace FrameCores
