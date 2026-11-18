// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame bfidr: Black Frame Insertion's helpers (the owner, 2026-11-17). Turning the Full
// modes' Black Frame Insertion on (vr_motion_clarity_immersive: the VR menu's Immersive row or
// Azahar's window, global or this game) also turns on, where they are off, and so the player
// sees them on:
//   - Dynamic Resolution (vr_dynamic_resolution): its rules keep the flashing clean under load;
//   - Primary Screen as Panel (vr_screens_in_projection off): the flashing uses the panel.
// Nothing is locked: either can be turned off again, and that choice holds. Turning Black Frame
// Insertion off puts back what it turned on, only where the player has not changed it since
// (vr_bfi_set_dynamic_resolution, vr_bfi_set_panel: "Black Frame Insertion turned it on", kept
// at the same scope as the row, in the game's file too): the panel without the flashing doubles
// on head turns, so it does not stay on unseen. The Off and Wide modes' row (vr_motion_clarity_
// normal) has none: neither helper does anything there. Pure (unit-tested:
// src/tests/frame_xr/bfi_helpers.cpp).

#pragma once

namespace FrameXr::BfiHelpers {

/// The helpers as the settings hold them at the row's scope, and what Black Frame Insertion
/// turned on.
struct State {
    bool dynamic_resolution = false;
    bool panel = false; ///< Primary Screen as Panel (!vr_screens_in_projection)
    bool dynamic_resolution_by_bfi = false;
    bool panel_by_bfi = false;
};

struct Change {
    State state;
    bool turned_on = false; ///< a helper was turned on (the toast)
    bool put_back = false;  ///< a helper was turned off again
};

/// The Full modes' Black Frame Insertion turned on (`on`) or off, from `now`. Nothing changes when
/// the row's value does not.
inline Change OnBfi(bool was_on, bool on, State now) {
    Change c{now};
    if (on == was_on) {
        return c;
    }
    State& s = c.state;
    if (on) {
        if (!s.dynamic_resolution) {
            s.dynamic_resolution = true;
            s.dynamic_resolution_by_bfi = true;
            c.turned_on = true;
        }
        if (!s.panel) {
            s.panel = true;
            s.panel_by_bfi = true;
            c.turned_on = true;
        }
        return c;
    }
    if (s.dynamic_resolution_by_bfi && s.dynamic_resolution) {
        s.dynamic_resolution = false;
        c.put_back = true;
    }
    if (s.panel_by_bfi && s.panel) {
        s.panel = false;
        c.put_back = true;
    }
    s.dynamic_resolution_by_bfi = false;
    s.panel_by_bfi = false;
    return c;
}

/// The player set a helper themselves: Black Frame Insertion no longer put it there, so turning
/// it off leaves that helper as it is.
inline State OnPlayer(State now, bool dynamic_resolution_set, bool panel_set) {
    if (dynamic_resolution_set) {
        now.dynamic_resolution_by_bfi = false;
    }
    if (panel_set) {
        now.panel_by_bfi = false;
    }
    return now;
}

} // namespace FrameXr::BfiHelpers
