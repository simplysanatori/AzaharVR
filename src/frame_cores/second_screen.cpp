// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/second_screen.h"

#include <algorithm>
#include <array>
#include <string_view>

#include "frame_cores/frame_driver.h"

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
    default:
        return false; // Game Boy games have no gba-ds picture; 3DS and DS games have two screens
    }
}

GameBadges BadgeRule(Common::AltCore::CoreKind kind, bool pokedaisy, bool gbads) {
    if (kind != Common::AltCore::CoreKind::GBA && kind != Common::AltCore::CoreKind::GB) {
        return {};
    }
    return {.two_screens = pokedaisy || gbads, .pokedaisy = pokedaisy};
}

bool MayBePokeDaisy(const RomInfo& info) {
    if (info.kind == Common::AltCore::CoreKind::GB) {
        return true;
    }
    if (info.kind != Common::AltCore::CoreKind::GBA) {
        return false;
    }
    // Ruby, Sapphire, FireRed, LeafGreen and Emerald in any language; hacks keep the code or title
    constexpr std::array<std::string_view, 5> kCodes{"AXV", "AXP", "BPR", "BPG", "BPE"};
    const std::string_view code = std::string_view(info.game_code).substr(0, 3);
    return std::find(kCodes.begin(), kCodes.end(), code) != kCodes.end() ||
           info.title.starts_with("POKEMON");
}

GameBadges BadgesOf(const RomInfo& info, const std::string& path) {
    bool pokedaisy = false;
#ifdef HAVE_POKEDAISY
    pokedaisy = MayBePokeDaisy(info) && PokeDaisyKnows(path);
#else
    (void)path;
#endif
    return BadgeRule(info.kind, pokedaisy, HasTwoScreens(info.kind, info.game_code));
}

} // namespace FrameCores
