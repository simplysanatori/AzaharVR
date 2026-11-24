// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>

#include "common/alt_core_types.h"

namespace FrameCores {

/// What a gba-ds profile says about its picture; a game's own values, copied out.
struct MapScreenFacts {
    bool profile_found = false;
    bool logo_only = false;
    int tight_w = 0, tight_h = 0; ///< used when tight_w > 0
    int crop_w = 0, crop_h = 0;
};

/// The gba-ds rule, as GbaCore::Load applies it: a profile, not logo-only, and a picture
/// (tight size, else crop size) that is not empty.
bool MapScreenAvailable(const MapScreenFacts& facts);

/// Whether the list shows the Two Screens badge for a game: it has a second screen picture.
/// `game_code` is the header's four letters. Safe on the scan thread.
bool HasTwoScreens(Common::AltCore::CoreKind kind, const std::string& game_code);

} // namespace FrameCores
