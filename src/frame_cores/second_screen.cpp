// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/second_screen.h"

extern "C" {
#include "profiles.h"
}

namespace FrameCores {

bool MapScreenAvailable(const MapScreenFacts& facts) {
    if (!facts.profile_found || facts.logo_only) {
        return false;
    }
    const bool tight = facts.tight_w > 0;
    const int w = tight ? facts.tight_w : facts.crop_w;
    const int h = tight ? facts.tight_h : facts.crop_h;
    return w > 0 && h > 0;
}

bool HasTwoScreens(Common::AltCore::CoreKind kind, const std::string& game_code) {
    switch (kind) {
    case Common::AltCore::CoreKind::GBA: {
        if (game_code.empty()) {
            return false;
        }
        const MapProfile* p = profile_find(game_code.c_str());
        MapScreenFacts facts;
        if (p) {
            facts.profile_found = true;
            facts.logo_only = p->logoOnly;
            facts.tight_w = p->tight_w;
            facts.tight_h = p->tight_h;
            facts.crop_w = p->crop_w;
            facts.crop_h = p->crop_h;
        }
        return MapScreenAvailable(facts);
    }
    case Common::AltCore::CoreKind::GB:
        // A companion-profile rule for Game Boy and Pokemon games goes here, and in a GBA
        // case above, as "companion found OR the rule above".
        return false;
    default:
        return false; // 3DS and DS games already have two screens
    }
}

} // namespace FrameCores
