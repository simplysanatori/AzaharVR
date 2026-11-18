// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame bfidr: Black Frame Insertion's helpers (frame_xr/bfi_helpers.h): on with it, put
// back with it unless the player changed them since; nothing locked.

#include <catch2/catch_test_macros.hpp>
#include "frame_xr/bfi_helpers.h"
#include "frame_xr/immersive_names.h"

namespace H = FrameXr::BfiHelpers;

TEST_CASE("Black Frame Insertion's helpers: on with it, put back with it", "[frame_xr][bfidr]") {
    // Both off: both on, both marked, the toast.
    H::Change c = H::OnBfi(false, true, {});
    CHECK(c.state.dynamic_resolution);
    CHECK(c.state.panel);
    CHECK(c.state.dynamic_resolution_by_bfi);
    CHECK(c.state.panel_by_bfi);
    CHECK(c.turned_on);
    CHECK_FALSE(c.put_back);
    // Off again, nothing changed since: both back off.
    c = H::OnBfi(true, false, c.state);
    CHECK_FALSE(c.state.dynamic_resolution);
    CHECK_FALSE(c.state.panel);
    CHECK_FALSE(c.state.dynamic_resolution_by_bfi);
    CHECK_FALSE(c.state.panel_by_bfi);
    CHECK(c.put_back);

    // Already on: left as they are, not marked, and left on when it goes off.
    c = H::OnBfi(false, true, {true, true, false, false});
    CHECK_FALSE(c.turned_on);
    CHECK_FALSE(c.state.dynamic_resolution_by_bfi);
    c = H::OnBfi(true, false, c.state);
    CHECK(c.state.dynamic_resolution);
    CHECK(c.state.panel);
    CHECK_FALSE(c.put_back);

    // The value unchanged: nothing.
    c = H::OnBfi(true, true, {false, false, false, false});
    CHECK_FALSE(c.state.dynamic_resolution);
    CHECK_FALSE(c.turned_on);
}

TEST_CASE("Black Frame Insertion's helpers: the player's choice holds", "[frame_xr][bfidr]") {
    H::State s = H::OnBfi(false, true, {}).state;
    // The player turns Dynamic Resolution off: not locked, and it stays off.
    s.dynamic_resolution = false;
    s = H::OnPlayer(s, true, false);
    CHECK_FALSE(s.dynamic_resolution_by_bfi);
    CHECK(s.panel_by_bfi);
    // Turned off and on again by the player: theirs now, so it stays on when Black Frame
    // Insertion goes off; the panel, untouched, goes back off.
    s.dynamic_resolution = true;
    s = H::OnPlayer(s, true, false);
    const H::Change c = H::OnBfi(true, false, s);
    CHECK(c.state.dynamic_resolution);
    CHECK_FALSE(c.state.panel);
    CHECK(c.put_back);
}

TEST_CASE("Black Frame Insertion's helpers: the toast", "[frame_xr][bfidr]") {
    namespace Names = FrameXr::Immersive::Names;
    CHECK(Names::BfiHelpersToast(true, true, true) ==
          "Dynamic Resolution and Primary Screen as Panel On for Black Frame Insertion");
    CHECK(Names::BfiHelpersToast(true, false, true) == "Dynamic Resolution On for Black Frame Insertion");
    CHECK(Names::BfiHelpersToast(false, true, false).empty()); // putting them back: no toast
    CHECK(Names::BfiHelpersToast(false, false, true).empty());
}
