// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// The Cheats tab's Download button for a DS, GBA or Game Boy game (the 3DS one is frame_cheats.cpp):
// finds the game's file in libretro-database (frame_cores/cheat_db.h) off the GUI thread and adds
// its cheats, off, to the game's list. GUI thread only.

#pragma once

#include <cstdint>
#include <functional>

#include "frame_xr/menu/cheats_tab.h"

namespace FrameCores {
class AltCoreHost;
}

namespace FrameAltCheats {

/// Starts the download for the game `host` runs (nothing when one is under way). `running` gives
/// the host again when the download ends (null when the game is gone); `changed` is called on the
/// GUI thread when the state changes, to publish the model again.
void Start(FrameCores::AltCoreHost& host, std::function<FrameCores::AltCoreHost*()> running,
           std::function<void()> changed);

/// Add Cheat (a name, then the code, on the game's keyboard in VR and in dialogs in Flat, checked
/// for the game's system); the cheat is added, on, and saved in the game's own list.
void Add(FrameCores::AltCoreHost& host, std::function<FrameCores::AltCoreHost*()> running,
         std::function<void()> changed);

/// The model's download state and status for the game with this key (Idle for any other).
void Fill(std::uint64_t key, FrameXr::Menu::CheatsModel& model);

} // namespace FrameAltCheats
