// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <catch2/catch_test_macros.hpp>

#include "common/alt_core_types.h"
#include "frame_cores/rom_info.h"
#include "frame_cores/second_screen.h"

using Common::AltCore::CoreKind;
using namespace FrameCores;

TEST_CASE("MapScreenAvailable follows the gba-ds rule", "[frame_cores]") {
    MapScreenFacts f;
    CHECK_FALSE(MapScreenAvailable(f)); // no profile

    f.profile_found = true;
    f.crop_w = 240;
    f.crop_h = 160;
    CHECK(MapScreenAvailable(f));

    f.logo_only = true;
    CHECK_FALSE(MapScreenAvailable(f));
    f.logo_only = false;

    f.crop_h = 0;
    CHECK_FALSE(MapScreenAvailable(f)); // empty crop

    f.tight_w = 100; // the tight size wins when it is set
    f.tight_h = 80;
    CHECK(MapScreenAvailable(f));
    f.tight_h = 0;
    CHECK_FALSE(MapScreenAvailable(f));
}

TEST_CASE("HasTwoScreens by system and game code", "[frame_cores]") {
    CHECK_FALSE(HasTwoScreens(CoreKind::NDS, "B8KE"));
    CHECK_FALSE(HasTwoScreens(CoreKind::DSi, "B8KE"));
    CHECK_FALSE(HasTwoScreens(CoreKind::N3DS, "B8KE"));
    CHECK_FALSE(HasTwoScreens(CoreKind::GB, "B8KE"));
    CHECK_FALSE(HasTwoScreens(CoreKind::GBA, ""));
    CHECK_FALSE(HasTwoScreens(CoreKind::GBA, "ZZZZ")); // no profile
    CHECK_FALSE(HasTwoScreens(CoreKind::GBA, "AMKE")); // logo-only profile
    CHECK(HasTwoScreens(CoreKind::GBA, "B8KE"));       // a map profile
    CHECK(HasTwoScreens(CoreKind::GBA, "B8KP"));       // a variant of it
}

TEST_CASE("SystemOf and FilterMatches", "[frame_cores]") {
    RomInfo info;
    CHECK(SystemOf(info) == GameSystem::N3DS);
    info.kind = CoreKind::NDS;
    CHECK(SystemOf(info) == GameSystem::DS);
    info.kind = CoreKind::DSi;
    CHECK(SystemOf(info) == GameSystem::DS);
    info.kind = CoreKind::GBA;
    CHECK(SystemOf(info) == GameSystem::GBA);
    info.kind = CoreKind::GB;
    CHECK(SystemOf(info) == GameSystem::GB);
    info.is_gbc = true;
    CHECK(SystemOf(info) == GameSystem::GBC);

    CHECK(FilterMatches(SystemFilter::All, GameSystem::GBA));
    CHECK(FilterMatches(SystemFilter::N3DS, GameSystem::N3DS));
    CHECK_FALSE(FilterMatches(SystemFilter::N3DS, GameSystem::DS));
    CHECK(FilterMatches(SystemFilter::DS, GameSystem::DS));
    CHECK_FALSE(FilterMatches(SystemFilter::GBA, GameSystem::GB));
    CHECK(FilterMatches(SystemFilter::GB, GameSystem::GB));
    CHECK(FilterMatches(SystemFilter::GB, GameSystem::GBC));
    CHECK_FALSE(FilterMatches(SystemFilter::GB, GameSystem::GBA));
}
