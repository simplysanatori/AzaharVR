// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <catch2/catch_test_macros.hpp>
#include "frame_xr/kiu_camera_tap.h"
#include "frame_xr/kiu_first_person.h"

using namespace FrameXr::Kiu;
using namespace FrameXr::Kiu::CameraTap;

TEST_CASE("Kid Icarus camera tap: in a battle only, else Swap Screens", "[frame_xr][kiucam]") {
    const Now battle{.first_person = true, .battle = true};
    STATIC_REQUIRE(kSwitchCamera == 0 && kSwapScreens == 1);
    CHECK(Switches(battle));
    const auto with = [&battle](auto&& change) {
        Now n = battle;
        change(n);
        return Switches(n);
    };
    CHECK_FALSE(with([](Now& n) { n.first_person = false; })); // every other game and mode
    CHECK_FALSE(with([](Now& n) { n.battle = false; }));       // the goddess's room, menus
    CHECK_FALSE(with([](Now& n) { n.game_menu = true; }));
    CHECK_FALSE(with([](Now& n) { n.cutscene = true; }));
    CHECK_FALSE(with([](Now& n) { n.paused = true; }));
    CHECK_FALSE(with([](Now& n) { n.swapped = true; })); // it swaps back
    CHECK_FALSE(with([](Now& n) { n.setting = kSwapScreens; }));
    CHECK_FALSE(Switches({}));
    // In a cutscene it does nothing: no switch, no swap.
    Now scene = battle;
    scene.cutscene = true;
    CHECK(Nothing(scene));
    CHECK_FALSE(Nothing(battle));
    CHECK_FALSE(Nothing({}));
    Now other = scene;
    other.swapped = true; // it swaps back
    CHECK_FALSE(Nothing(other));
    other = scene;
    other.setting = kSwapScreens;
    CHECK_FALSE(Nothing(other));
    other = scene;
    other.first_person = false;
    CHECK_FALSE(Nothing(other));
}

TEST_CASE("Kid Icarus camera tap: the next camera", "[frame_xr][kiucam]") {
    CHECK(NextView(0) == 1); // First Person
    CHECK(NextView(1) == 0); // Third Person
    CHECK(NextView(2) == 0); // the Game's Camera
    // As the Camera row writes it: both keys, and back through ViewFor.
    for (const int view : {0, 1}) {
        CHECK(FirstPerson::ViewFor(FirstPerson::ViewOverShoulder(view),
                                   FirstPerson::ViewGameCamera(view)) == view);
    }
}

TEST_CASE("Kid Icarus camera tap: the fade", "[frame_xr][kiucam]") {
    constexpr int64_t kMs = 1'000'000, kStart = 5'000 * kMs;
    CHECK(Brightness(-1) == 1.0f);
    CHECK(Brightness(0) == 1.0f);
    CHECK(Brightness(25 * kMs) == 0.5f);
    CHECK(Brightness(kFadeNs) == 0.0f);
    CHECK(Brightness(kFadeNs + kBlackNs) == 0.0f);
    CHECK(Brightness(2 * kFadeNs + kBlackNs) == 1.0f);
    CHECK(2 * kFadeNs + kBlackNs <= 250 * kMs); // quick enough for play
    CHECK(Brightness(int64_t{1} << 60) == 1.0f);

    Switch s;
    CHECK_FALSE(s.Due(kStart));
    CHECK(s.Brightness(kStart) == 1.0f);
    REQUIRE(s.Begin(1, kStart, true));
    CHECK_FALSE(s.Due(kStart)); // switched once it is dark
    CHECK_FALSE(s.Begin(0, kStart + 25 * kMs, true));
    CHECK_FALSE(s.Due(kStart + 49 * kMs));
    CHECK(s.Due(kStart + 50 * kMs));
    CHECK(s.Brightness(kStart + 50 * kMs) == 0.0f); // never switched in view
    CHECK(s.View() == 1);
    CHECK_FALSE(s.Due(kStart + 58 * kMs)); // once
    CHECK(s.Brightness(kStart + 58 * kMs) == 0.0f);
    CHECK_FALSE(s.Begin(0, kStart + 170 * kMs, true)); // still dark
    CHECK(s.Begin(0, kStart + 400 * kMs, true));
    CHECK(s.Due(kStart + 450 * kMs));
    CHECK(s.View() == 0);
    CHECK(s.Brightness(kStart + 1000 * kMs) == 1.0f);

    // Drawn as panels: at once, nothing darkened.
    Switch p;
    REQUIRE(p.Begin(1, kStart, false));
    CHECK(p.Brightness(kStart + 50 * kMs) == 1.0f);
    CHECK(p.Due(kStart));
    CHECK(p.Begin(0, kStart + 20 * kMs, false));
    CHECK(p.Due(kStart + 20 * kMs));

    // The display's clock started again (another session): never dark, never stuck.
    Switch c;
    REQUIRE(c.Begin(1, kStart, true));
    CHECK(c.Brightness(10 * kMs) == 1.0f);
    CHECK(c.Due(10 * kMs));
}
