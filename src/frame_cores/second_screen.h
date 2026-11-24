// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "common/alt_core_types.h"

namespace FrameCores {

/// What a gba-ds profile says about its picture; a game's own values, copied out.
struct MapScreenFacts {
    bool profile_found = false;
    bool logo_only = false; ///< no map
    int tight_w = 0, tight_h = 0; ///< used when tight_w > 0
    int crop_w = 0, crop_h = 0;
    // What game.c draws besides
    bool hud = false;
    bool sheet = false; ///< screens copied over as they come up
    bool table = false; ///< the pinball table on both screens
    bool pick = false;  ///< a compilation: the game running in it has the picture
};

/// The gba-ds rule: a profile, not logo-only, and a picture (tight size, else crop size) that is
/// not empty.
bool MapScreenAvailable(const MapScreenFacts& facts);

/// Whether gba-ds has a Secondary Screen for the game: a map, or what game.c draws.
/// `with_game`: game.c is in this build.
bool GbaDsScreenAvailable(const MapScreenFacts& facts, bool with_game);

/// Held around gba-ds's profile_set_rom, and around profile_find on a thread that is not the
/// game's: the first changes what the second reads.
std::unique_lock<std::mutex> LockProfiles();

/// Whether a game's map is captured now. `frame`: frames run since the start or a reset; `since`:
/// those since the last capture was asked for; `interval`: the profile's, 0 for the usual.
bool MapCaptureDue(bool has_image, bool room_changed, uint64_t frame, uint64_t since,
                   uint64_t interval);

/// Whether the list shows the Two Screens badge for a game: it has a second screen picture.
/// `game_code` is the header's four letters. Safe on the scan thread.
bool HasTwoScreens(Common::AltCore::CoreKind kind, const std::string& game_code);

} // namespace FrameCores
