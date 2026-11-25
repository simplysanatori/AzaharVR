// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <catch2/catch_test_macros.hpp>
#include "video_core/kiu_clear.h"

using VideoCore::KiuClear::Pictures;

TEST_CASE("Kid Icarus: a picture's first world draw clears its colour, once", "[video_core][kiuhs3]") {
    Pictures p;
    CHECK_FALSE(p.First(0x18380000, true)); // no picture begun
    p.Fill(0x18380000, 0x18400000); // the game's two fills, then both eyes
    p.Fill(0x18300000, 0x18380000);
    CHECK(p.First(0x18380000, true));
    CHECK_FALSE(p.First(0x18380000, true));
    CHECK(p.First(0x18300000, true)); // the other eye's own
    CHECK_FALSE(p.First(0x18300000, true));
    CHECK_FALSE(p.First(0x18200000, true)); // no such depth
    p.Fill(0x18380000, 0x18400000);
    p.Fill(0x18300000, 0x18380000);
    CHECK_FALSE(p.First(0x18380000, false)); // it starts with another draw: left as it is
    CHECK_FALSE(p.First(0x18380000, true));
    CHECK(p.First(0x18300000, true));
    p.Fill(5, 5);
    CHECK_FALSE(p.First(5, true));
}

TEST_CASE("Kid Icarus: more fills than places keep the eyes' newest", "[video_core][kiuhs3]") {
    Pictures p;
    for (u32 i = 0; i < 12; ++i) {
        p.Fill(i * 0x100, (i + 1) * 0x100);
    }
    CHECK(p.First(11 * 0x100, true));
    for (u32 i = 0; i < 3; ++i) { // the same two filled over and over stay two
        p.Fill(0x18380000, 0x18400000);
        p.Fill(0x18300000, 0x18380000);
    }
    CHECK(p.First(0x183FFFFF, true));
    CHECK(p.First(0x18300000, true));
}
