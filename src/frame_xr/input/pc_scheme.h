// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstdint>
#include "frame_xr/input/default_bindings.h"
#include "frame_xr/input/input_frame.h"
#include "frame_xr/input/mapper.h"

// AzaharVR
// The PC controller schemes (docs/PCVR_PLAN.md section 3.3) in front of the Mapper. Pure logic:
// no OpenXR, Qt or emulator calls. With the Frame's scheme everything passes through untouched,
// so the Frame's mapping is the Mapper's own, as before.
//
// Two steps a display frame (XR thread):
//  - Normalize, for everyone that reads the controllers (the menus, the picker, the game): the
//    Menu role from the right trackpad (Index) or both stick clicks (Touch, Focus 3), the D-pad
//    from the left trackpad (Index); and the stick-click taps, for Game.
//  - Game, in a game with the menu closed: the frame the Mapper gets (grips as L and R, no
//    Menu, no stick clicks, the left stick as the D-pad in D-pad mode) and what the scheme
//    itself does: Start and Select, the VR menu, Quick Save, Quick Load and Fast Forward.
//    Merge puts that into the Mapper's output.
namespace FrameXr::Input {

struct PcSchemeConfig {
    float grip_press = 0.60f; ///< a grip is L or R above this ...
    float grip_release = 0.40f; ///< ... until below this
    float trigger_press = 0.55f; ///< the Mapper's trigger thresholds (Fast Forward's pull)
    float trigger_release = 0.40f;
    float trackpad_press = 0.50f; ///< a trackpad is pressed above this force ...
    float trackpad_release = 0.30f; ///< ... until below this
    float trackpad_zone = 0.30f; ///< a press nearer the centre than this is no D-pad direction
    float stick_dpad_press = 0.50f; ///< the left stick as the D-pad: a direction past this ...
    float stick_dpad_release = 0.35f; ///< ... until back within this
    /// A stick click that starts with its stick pushed further than this is ignored.
    float click_max_deflection = 0.5f;
    int64_t menu_hold_ns = 1'000'000'000; ///< the Menu role held this long: the VR menu
    int64_t menu_tap_max_ns = 300'000'000; ///< only a shorter press is Start (a tap)
    int64_t pulse_ns = 100'000'000; ///< Start and Select are held this long after a tap
    int64_t quick_load_hold_ns = 500'000'000; ///< Menu + the left grip held this long
    int64_t quick_cooldown_ns = 1'000'000'000; ///< between two chord actions
};

/// What the scheme did this frame, beside the Mapper's frame.
struct PcSchemeOut {
    bool start = false;  ///< Start held this frame (after a tap of the Menu role)
    bool select = false; ///< Select held this frame (after a right stick click)
    bool menu_toggle = false;
    float menu_hold_progress = 0.0f;
    bool quick_save = false;
    bool quick_load = false;
    bool toggle_fast_forward = false;
    /// The left stick click switched the left stick (dpad_mode() is the new mode).
    bool dpad_mode_changed = false;
};

class PcScheme {
public:
    explicit PcScheme(const PcSchemeConfig& config = {}) : config_(config) {}

    /// The scheme from now on. A change forgets every held state.
    void SetScheme(ControllerScheme scheme);
    ControllerScheme scheme() const {
        return scheme_;
    }
    /// Anything but the Frame's.
    bool active() const {
        return scheme_ != ControllerScheme::Frame;
    }

    /// See above. Does nothing with the Frame's scheme.
    void Normalize(InputFrame& in);

    /// See above. With the Frame's scheme it returns `in` and `out` stays empty.
    InputFrame Game(const InputFrame& in, PcSchemeOut& out);

    /// Puts the scheme's part (Game's `out`) into the Mapper's output.
    static void Merge(const PcSchemeOut& pc, MapperOutput& out);

    /// The left stick is the D-pad (Touch, Focus 3); remembered per game by the caller.
    bool dpad_mode() const {
        return dpad_mode_;
    }
    void SetDpadMode(bool on) {
        dpad_mode_ = on;
        stick_dir_ = -1;
    }

    /// Forgets the held state (the D-pad mode stays). Call when the game ends or input is lost.
    void Reset();

    /// AzaharVR: what Edit Controls listens to with a PC scheme: the presses as the game gets
    /// them. The grips are the bumpers (L and R), the left stick pushed one way is that D-pad
    /// direction (Touch, Focus 3), and Menu and View are off (Start and Select are the scheme's
    /// own; no 3DS button moves to them). `prev` is the frame this returned last time (the
    /// thresholds' hysteresis). With the Frame's scheme it returns `in`.
    static InputFrame ListenFrame(ControllerScheme scheme, const InputFrame& in,
                                  const InputFrame& prev, const PcSchemeConfig& config = {});

private:
    PcSchemeConfig config_;
    ControllerScheme scheme_ = ControllerScheme::Frame;
    bool dpad_mode_ = false;

    // Normalize
    bool right_pad_down_ = false;
    bool left_pad_down_ = false;
    std::array<bool, kNumHands> click_down_{};
    std::array<bool, kNumHands> click_ignored_{}; ///< started with the stick pushed over
    bool both_clicks_ = false; ///< both were down together during this press
    std::array<bool, kNumHands> tap_{}; ///< a single click was let go this frame

    // Game
    std::array<bool, kNumHands> grip_down_{};
    std::array<bool, kNumHands> swallow_grip_{};
    bool trigger_r_down_ = false;
    bool swallow_trigger_r_ = false;
    bool menu_down_ = false;
    int64_t menu_since_ns_ = 0;
    bool menu_fired_ = false;
    bool chord_used_ = false;
    bool save_pending_ = false;
    bool load_pending_ = false;
    int64_t load_since_ns_ = 0;
    bool load_fired_ = false;
    bool ff_pending_ = false;
    bool quick_done_ = false;
    int64_t quick_last_ns_ = 0;
    int64_t start_until_ns_ = -1;
    int64_t select_until_ns_ = -1;
    int stick_dir_ = -1; ///< the left stick's D-pad direction: 0 up, 1 down, 2 left, 3 right
};

} // namespace FrameXr::Input
