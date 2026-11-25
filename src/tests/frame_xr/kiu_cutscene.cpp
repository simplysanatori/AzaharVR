// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <cmath>
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

TEST_CASE("Kid Icarus cutscene: known by the game's flag, its angle counts at once", "[kiu140]") {
    FrameXr::Kiu::CutsceneWatch known, plain;
    bool a = false, b = false;
    for (int i = 0; i < 10; ++i) {
        a = known.Update(true, 0, 8, true);
        b = plain.Update(true, 0, 8);
    }
    REQUIRE(a);
    REQUIRE_FALSE(b);
}

TEST_CASE("Kid Icarus cutscenes: the angles count until the scene camera has its own again",
          "[frame_xr][kiu140]") {
    FrameXr::Kiu::WideAngle w;
    REQUIRE_FALSE(w.Update(false, 0.0f, true, 1.954f)); // never in: the game's own business
    REQUIRE(w.Update(true, 1.954f, true, 0.785f));
    REQUIRE(w.Update(false, 0.0f, true, 1.954f)); // out, the node as it was
    REQUIRE(w.Fov() == 1.954f);
    REQUIRE_FALSE(w.Update(false, 0.0f, true, 0.576f)); // the game's own camera
    REQUIRE(w.Fov() == 0.0f);
    REQUIRE_FALSE(w.Update(false, 0.0f, true, 1.954f));
    REQUIRE(w.Update(true, 1.954f, false, 0.0f));
    REQUIRE_FALSE(w.Update(false, 0.0f, false, 0.0f)); // no scene
}

TEST_CASE("Kid Icarus cutscenes: the closest shot still fills the view", "[frame_xr][kiu140]") {
    namespace L = FrameXr::Immersive::Layout;
    const L::ViewFov view{};
    const float deg = L::KiuNarrowestDeg(5.0f / 3.0f, view);
    const L::HeadLocked wide = L::SolveKiuFirstPerson(45.0f, 5.0f / 3.0f, view);
    const L::HeadLocked s = L::SolveKiuFirstPerson(deg, 5.0f / 3.0f, view);
    REQUIRE(deg > 10.0f);
    REQUIRE(deg < 40.0f);
    REQUIRE(s.factor_y <= L::kKiuFpFactorMax + 1e-3f);
    REQUIRE(std::abs(s.tan_half_y - wide.tan_half_y) < 0.02f);
    // Closer than that, the picture is smaller than the view.
    REQUIRE(L::SolveKiuFirstPerson(0.5f * deg, 5.0f / 3.0f, view).tan_half_y <
            0.6f * s.tan_half_y);
}
