// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuhand: Pit's drawn arm, the one built into the app (tools/kiu_hand/make_arm.py):
// drawn when the player has no hand file of their own (pit_model.h).

#pragma once

#include <string>
#include <vector>

#include "frame_xr/vis/pit_model.h"

namespace FrameXr::Vis {

/// The drawn arm as a hand file's bytes (pit_model.h), for ParsePitModel; empty on a failure.
std::vector<uint8_t> PitArmFile();
/// The drawn arm, checked as a file is.
bool BuiltInPitModel(PitModel& out, std::string& error);

} // namespace FrameXr::Vis
