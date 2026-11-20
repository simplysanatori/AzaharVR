// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cctype>

#include "frame_xr/input/gyro_aim.h"
#include "frame_xr/vis/stats_place.h"
#include "frame_xr/vis/wrist_screen.h"

// AzaharFrame kiuwrist
namespace FrameXr::Vis {

using Input::Posef;
using Input::Quatf;
using Input::Vec3f;

bool WristScreenSwitchOn(const std::optional<std::string>& value) {
    if (!value) {
        return false;
    }
    std::string v = *value;
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return v == "1" || v == "on" || v == "true";
}

Input::Hand WristScreenHand(uint32_t motion_controls) {
    // As KiuTriggers::AimingHand: the left aims with Left Controller (2), else the right.
    return Input::GyroAimSource(motion_controls) == 2 ? Input::Hand::Right : Input::Hand::Left;
}

Posef WristScreenLocalPose(Input::Hand hand, bool beside_stats) {
    Posef p = WristLocalPose(); // the left wrist's (stats_place.cpp says how it was found)
    if (beside_stats) {
        // Along the forearm toward the elbow: the way the picture reads is toward the hand.
        const Vec3f toward_hand = Input::Rotate(p.orientation, {1.0f, 0.0f, 0.0f});
        p.position = p.position - toward_hand * kWristScreenBesideStats;
    }
    if (hand == Input::Hand::Right) {
        // Mirrored across the controller's middle (x to -x): the rotation M R M with M the
        // mirror, which is (x, -y, -z, w) for a quaternion. Its +X then points the mirror image
        // of the left one's other way (toward the elbow), which is what keeps the picture
        // upright and reading left to right on the right wrist; +Y (the picture's top) and +Z
        // (its front, inward) are the mirror images of the left one's.
        p.position.x = -p.position.x;
        p.orientation = {p.orientation.x, -p.orientation.y, -p.orientation.z, p.orientation.w};
    }
    return p;
}

Posef WristScreenPose(const Posef& aim, Input::Hand hand, bool beside_stats) {
    const Posef local = WristScreenLocalPose(hand, beside_stats);
    return {Input::Normalized(Input::Multiply(aim.orientation, local.orientation)),
            Input::Transform(aim, local.position)};
}

WristScreen::Out WristScreen::Update(bool wanted, bool tracked, const Posef& aim,
                                     Input::Hand hand, bool beside_stats, const Vec3f& eye) {
    Out out;
    const bool was = shown_;
    if (wanted && tracked) {
        out.pose = WristScreenPose(aim, hand, beside_stats);
        out.facing_deg = WristFacingDeg(out.pose, eye);
        out.distance_m = Input::Length(out.pose.position - eye);
        shown_ = WristShown(out.facing_deg, shown_);
    } else {
        shown_ = false; // lost, or not wanted: gone at once
    }
    out.shown = shown_;
    out.changed = shown_ != was;
    return out;
}

} // namespace FrameXr::Vis
