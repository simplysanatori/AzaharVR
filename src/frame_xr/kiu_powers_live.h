// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiupowers: what the emulation thread (kiu_hooks.cpp) and the controls say about the
// powers, for the XR thread that shows them (power_layers.h), and the switches in force.

#pragma once

#include "frame_xr/input/types.h"
#include "frame_xr/kiu_powers.h"
#include "frame_xr/vis/power_menu.h"

namespace FrameXr::Kiu::Powers {

/// The switches now (power_menu.h); both off without them. Any thread.
Vis::Powers::Settings SettingsNow();

/// The emulation thread, each VBlank: what the game says. `playing`: first person in a battle, no
/// cutscene. Not known while neither switch is on.
void SetGame(const State& state, bool playing);
State Game(bool* playing);

/// Fire and Power's bumpers send nothing in an air battle (input/kiu_trigger_layout.h): wanted
/// (the layout, Angel's Aim, not AZAHARFRAME_KIU_SKY_BUMPERS=1: the game is read for it), and in
/// force now (the game says an air battle). Any thread.
bool AirNoBumpersWanted();
bool AirNoBumpers();

/// The XR thread, each frame (controls.cpp), before the layers are made.
struct Hands {
    bool tracked = false; ///< the off hand's
    Input::Posef off{};   ///< its aim pose
    bool aiming = false;
    Input::Posef aim{}; ///< the aiming hand's ray
};
void SetHands(const Hands& hands);
Hands HandsNow();

} // namespace FrameXr::Kiu::Powers
