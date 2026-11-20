// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuwrist: Angel's Aim's Secondary Screen on the off hand's wrist, like a watch
// (test switch AZAHARFRAME_KIU_WRIST_SCREEN=1, off by default; kiu_first_person.h). No OpenXR
// calls here (unit tested in src/tests/frame_xr/kiu_wrist_screen.cpp).
//
//   - The off hand is the hand that does not aim: the right one with Motion Controls' Left
//     Controller, else the left one (KiuTriggers::AimingHand, as Direct Aim and the shots pick
//     the aiming hand).
//   - On the left wrist the screen sits where the wrist stats do (stats_place.h WristLocalPose);
//     on the right wrist it is that pose mirrored across the controller's middle, turned so the
//     picture stays upright and reads left to right.
//   - It shows while it faces the eyes (WristShown: within kWristShowDeg, gone past
//     kWristHideDeg), with no fade: hidden it is neither drawn nor touched, so a trigger pulled
//     with the wrist down fires as always.
//   - Stats On Wrist is always on the left controller: when the screen is on the left wrist too,
//     the screen moves kWristScreenBesideStats along the forearm toward the elbow.
//
// The size: 10 x 7.5 cm (the 3DS's 4:3). Glanced at like a watch it is 0.35 to 0.41 m from the
// eyes (the desktop mock's wrist-up pose: 0.41 m): 16.3 x 12.2 to 13.7 x 10.3 degrees, 20 to 23
// of the Secondary Screen's 320 x 240 pixels a degree, taken to be about the headset's own
// pixels a degree in the middle of the view (an assumption, about 20 to 25, to check on the
// headset), so the picture is neither blurred nor wasted. The handheld's own screen (61 x 46 mm at about 0.30 m) is 11.7
// x 8.8 degrees: this is 1.2 to 1.4 times as big as seen. A 32 px power orb is 10 mm, 1.4 to 1.6
// degrees: an easy tap.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "frame_xr/input/geometry.h"
#include "frame_xr/input/types.h"

namespace FrameXr::Vis {

inline constexpr const char* kKiuWristScreenSwitch = "AZAHARFRAME_KIU_WRIST_SCREEN";
/// The switch's value: "1" (or "on", "true") turns it on; anything else, or none, leaves it off.
bool WristScreenSwitchOn(const std::optional<std::string>& value);

/// Its size on the wrist (the Secondary Screen's 320 x 240 shape).
constexpr float kWristScreenWidth = 0.10f;
constexpr float kWristScreenHeight = kWristScreenWidth * 240.0f / 320.0f;
/// How far toward the elbow it moves when the wrist stats are on the same wrist: half of each
/// panel's length along the forearm and a 1 cm gap.
constexpr float kWristScreenBesideStats = 0.5f * 0.10f + 0.5f * kWristScreenWidth + 0.01f;

/// The hand that does not aim, from Motion Controls (vr_motion_controls: 2 Left Controller).
Input::Hand WristScreenHand(uint32_t motion_controls);

/// The screen's pose in that controller's aim space (+X the way the picture reads, +Y its top,
/// +Z its front).
Input::Posef WristScreenLocalPose(Input::Hand hand, bool beside_stats);
/// In LOCAL space for that controller's aim pose.
Input::Posef WristScreenPose(const Input::Posef& aim, Input::Hand hand, bool beside_stats);

/// Each display frame: where it is and whether it shows.
class WristScreen {
public:
    struct Out {
        bool shown = false;
        Input::Posef pose{};
        float facing_deg = 180.0f; ///< its front against the direction to the eyes
        float distance_m = 0.0f;   ///< from the eyes to its middle
        bool changed = false;      ///< shown or hidden this frame (for the log)
    };
    /// `wanted`: Angel's Aim in effect with the switch on; `tracked`: the off hand's controller.
    Out Update(bool wanted, bool tracked, const Input::Posef& aim, Input::Hand hand,
               bool beside_stats, const Input::Vec3f& eye);
    void Reset() {
        shown_ = false;
    }

private:
    bool shown_ = false;
};

} // namespace FrameXr::Vis
