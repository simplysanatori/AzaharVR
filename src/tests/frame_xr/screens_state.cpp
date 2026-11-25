// Copyright 2026 AzaharVR contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <catch2/catch_test_macros.hpp>

#include "frame_xr/screens_state.h"

TEST_CASE("XR game screens: a game runs on while its screens are made again", "[frame_xr]") {
    FrameXr::ScreensState screens;
    const auto is = [&screens](bool live, bool game_on) {
        return screens.Live() == live && screens.GameOn() == game_on;
    };
    CHECK(is(false, false));
    screens.SetLive(true); // it starts
    CHECK(is(true, true));
    for (int i = 0; i < 2; ++i) {
        screens.SetRemade(true);
        CHECK(is(true, true));
        screens.SetLive(false);
        CHECK(is(false, true));
        screens.SetLive(true);
        CHECK(is(true, true));
    }
    screens.SetLive(false); // it stops
    CHECK(is(false, false));

    // The next one, which stops before its screens are there again
    screens.SetLive(true);
    screens.SetRemade(true);
    screens.SetLive(false);
    CHECK(is(false, true));
    screens.SetRemade(false);
    CHECK(is(false, false));

    // And a game that only goes off and on (a 3DS game's quick load)
    screens.SetLive(true);
    screens.SetLive(false);
    CHECK(is(false, false));
    screens.SetLive(true);
    CHECK(is(true, true));
}
