// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <catch2/catch_test_macros.hpp>
#include "frame_xr/immersive_layout.h"
#include "frame_xr/kiu_cutscene.h"

using FrameXr::Kiu::CutsceneWatch;

namespace {
bool Run(CutsceneWatch& w, int vblanks, uint32_t match, uint32_t other, bool wide = true) {
    bool on = false;
    for (int i = 0; i < vblanks; ++i) {
        // The game draws every other VBlank.
        on = w.Update(wide, i % 2 ? 0 : match, i % 2 ? 0 : other);
    }
    return on;
}
} // namespace

TEST_CASE("Kid Icarus cutscenes: told from play by the angle drawn", "[frame_xr][kiu140]") {
    CutsceneWatch w;
    // A battle starts with the game's own camera for a moment: not a cutscene.
    REQUIRE_FALSE(Run(w, 20, 0, 40));
    REQUIRE_FALSE(Run(w, 300, 40, 3));
    // Some draws through other cameras (a portrait, a reflection) in play.
    REQUIRE_FALSE(Run(w, 300, 40, 60));
    // A cutscene: within about 0.2 s.
    REQUIRE_FALSE(Run(w, 1, 0, 40));
    REQUIRE(Run(w, 16, 0, 40));
    // A fade to black keeps it.
    REQUIRE(Run(w, 120, 0, 0));
    REQUIRE(Run(w, 30, 1, 40));
    // Play again: back within 0.15 s.
    REQUIRE_FALSE(Run(w, 9, 40, 2));
    // One game frame of another camera is no cutscene.
    REQUIRE_FALSE(Run(w, 2, 0, 40));
    REQUIRE_FALSE(Run(w, 60, 40, 2));
    // The battle over (or the wide camera out): off at once, and settled again after.
    REQUIRE(Run(w, 30, 0, 40));
    REQUIRE_FALSE(Run(w, 1, 0, 40, false));
    REQUIRE_FALSE(Run(w, 40, 0, 40));
}

TEST_CASE("Kid Icarus cutscenes: the Primary Screen, as under the in-game menu",
          "[frame_xr][kiu140]") {
    namespace L = FrameXr::Immersive::Layout;
    L::ScreenState s;
    s.mode = 3;
    s.live = s.view_valid = true;
    REQUIRE(L::Decide(s).immersive);
    s.cutscene = true;
    REQUIRE_FALSE(L::Decide(s).immersive);
    REQUIRE_FALSE(L::Decide(s).show_bottom); // the bottom screen as it was
    s.mode = 1;
    REQUIRE(L::Decide(s).immersive); // Wide: not a head-locked view
}

TEST_CASE("Kid Icarus cutscenes: the game's own object, two VBlanks in a row",
          "[frame_xr][kiu140]") {
    FrameXr::Kiu::EventWatch w;
    REQUIRE_FALSE(w.Update(true));
    REQUIRE(w.Update(true));
    REQUIRE(w.Update(false)); // a single miss
    REQUIRE(w.Update(true));
    REQUIRE(w.Update(false));
    REQUIRE_FALSE(w.Update(false));
}
