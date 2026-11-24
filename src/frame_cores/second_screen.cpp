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

bool GbaDsScreenAvailable(const MapScreenFacts& facts, bool with_game) {
    if (MapScreenAvailable(facts)) {
        return true;
    }
    return with_game && facts.profile_found &&
           (facts.hud || facts.sheet || facts.table || facts.pick);
}

std::unique_lock<std::mutex> LockProfiles() {
    static std::mutex mutex;
    return std::unique_lock(mutex);
}

bool MapCaptureDue(bool has_image, bool room_changed, uint64_t frame, uint64_t since,
                   uint64_t interval) {
    return (!has_image && frame >= 30 && since >= 30) || (room_changed && since >= 15) ||
           since >= (interval > 0 ? interval : 120);
}

bool HasTwoScreens(Common::AltCore::CoreKind kind, const std::string& game_code) {
    switch (kind) {
    case Common::AltCore::CoreKind::GBA: {
        if (game_code.empty()) {
            return false;
        }
        const auto lock = LockProfiles();
        const MapProfile* p = profile_find(game_code.c_str());
        MapScreenFacts facts;
        if (p) {
            facts.profile_found = true;
            facts.logo_only = p->logoOnly;
            facts.tight_w = p->tight_w;
            facts.tight_h = p->tight_h;
            facts.crop_w = p->crop_w;
            facts.crop_h = p->crop_h;
            facts.hud = p->hud.on[0].addr != 0;
            facts.sheet = p->logoOnly && p->snap.showMin > 0;
            facts.table = p->logoOnly && p->tall.on[0].addr != 0;
            facts.pick = p->pick != nullptr;
        }
#ifdef HAVE_GBADS_GAME
        return GbaDsScreenAvailable(facts, true);
#else
        return GbaDsScreenAvailable(facts, false);
#endif
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
