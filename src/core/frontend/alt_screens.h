// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <optional>
#include "core/frontend/framebuffer_layout.h"

// AzaharVR: DS, DSi, GBA and Game Boy games in the window. Their screens go where the 3DS's would
// (framebuffer_layout.h, every layout setting), each fitted into its 3DS screen's rectangle.
namespace Layout {

/// The running game's screens, in its own pixels.
struct AltScreens {
    u32 top_width = 0;
    u32 top_height = 0;
    u32 bottom_width = 0; ///< 0: the game has one screen
    u32 bottom_height = 0;

    bool HasBottom() const {
        return bottom_width != 0 && bottom_height != 0;
    }
    bool operator==(const AltScreens&) const = default;
};

/// Set while such a game runs; nothing for 3DS games. The windows' layouts follow at their next
/// update.
void SetAltScreens(std::optional<AltScreens> screens);
std::optional<AltScreens> GetAltScreens();

/// `layout`, made by `option`'s factory for the 3DS, with the game's screens in it. A screen keeps
/// its shape unless the settings say stretch; with Single Screen (one-screen games always) it may
/// use the whole window, as a 3DS screen does.
FramebufferLayout FitAltScreens(FramebufferLayout layout, const AltScreens& screens,
                                Settings::LayoutOption option);

} // namespace Layout
