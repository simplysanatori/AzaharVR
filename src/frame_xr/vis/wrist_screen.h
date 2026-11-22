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
#include "frame_xr/vis/stats_place.h"

namespace FrameXr::Vis {

inline constexpr const char* kKiuWristScreenSwitch = "AZAHARFRAME_KIU_WRIST_SCREEN";
/// The switch's value: "1" (or "on", "true") turns it on; anything else, or none, leaves it off.
bool WristScreenSwitchOn(const std::optional<std::string>& value);
/// AzaharFrame kiutoggles: wanted by the switch when it is set (on or off), else by the setting
/// (Settings::values.vr_kiu_wrist_screen, off by default).
inline bool WristScreenOn(const std::optional<std::string>& value, bool setting) {
    return value ? WristScreenSwitchOn(value) : setting;
}

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
/// AzaharFrame kiu131: on the wrist side chosen (stats_place.h WristSide; Inside by default, as
/// before) and `width` wide (Bottom Screen Size: beside the stats it moves that much further).
Input::Posef WristScreenLocalPose(Input::Hand hand, bool beside_stats,
                                  WristSide side = WristSide::Inside,
                                  float width = kWristScreenWidth);
/// In LOCAL space for that controller's aim pose.
Input::Posef WristScreenPose(const Input::Posef& aim, Input::Hand hand, bool beside_stats,
                             WristSide side = WristSide::Inside, float width = kWristScreenWidth);
/// How far toward the elbow a screen `width` wide moves beside the wrist stats.
constexpr float WristScreenBesideStats(float width) {
    return 0.5f * 0.10f + 0.5f * width + 0.01f;
}

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
               bool beside_stats, const Input::Vec3f& eye, WristSide side = WristSide::Inside,
               float width = kWristScreenWidth);
    void Reset() {
        shown_ = false;
    }

private:
    bool shown_ = false;
};

/// AzaharFrame kiupanel: Angel's Aim's aim held while the aiming hand points at the wrist screen
/// (live test switch AZAHARFRAME_KIU_WRIST_HOLD, on by default; "0", "off" or "false" turn it
/// off). Pointing down at the wrist is still the aiming ray: unheld, first person's aim takes it
/// and the reticle runs to the frame's lower-left edge (the mock, Chapter 1 land: from 203, 124
/// to 58, 191 of 400 x 240). Held, the ray Direct Aim, the reticle and the shots take is the
/// last one from before the beam reached the screen, until kGraceNs after it left (a tap
/// released at the screen's edge, a hand on its way back), so the reticle stays and nothing
/// turns.
inline constexpr const char* kKiuWristHoldSwitch = "AZAHARFRAME_KIU_WRIST_HOLD";
bool WristHoldSwitchOn(const std::optional<std::string>& value);

class WristAimHold {
public:
    static constexpr int64_t kGraceNs = 250'000'000;
    struct Out {
        bool hold = false;    ///< publish the held ray, not this frame's
        bool changed = false; ///< started or ended this frame (for the log)
    };
    /// `on_screen`: the aiming hand's beam is on the wrist screen as shown this frame (the hit
    /// the touch takes); `now_ns`: this frame's time.
    Out Update(bool on_screen, int64_t now_ns);
    void Reset() {
        *this = {};
    }

private:
    bool holding_ = false;
    int64_t last_on_ns_ = 0;
};

} // namespace FrameXr::Vis
