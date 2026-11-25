// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cctype>
#include <cmath>

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

Posef WristScreenLocalPose(Input::Hand hand, bool beside_stats, WristSide side, float width,
                           WristAt at) {
    Posef p = WristLocalPose(side); // the left wrist's (stats_place.cpp says how it was found)
    if (beside_stats) {
        // Along the forearm toward the elbow: the way the picture reads is toward the hand.
        const Vec3f toward_hand = Input::Rotate(p.orientation, {1.0f, 0.0f, 0.0f});
        p.position = p.position - toward_hand * WristScreenBesideStats(width);
    }
    // AzaharFrame kiu140: a drawn arm's own line, which the controller's handle does not lie
    // along. The panel turns with it (the shortest way), and stays upright beside it.
    Vec3f middle{kWristMiddleX, p.position.y, p.position.z};
    {
        // A right hand's in the left's space: the mirror at the end is of the whole place.
        Vec3f wrist = at.wrist, elbow = at.elbow;
        if (hand == Input::Hand::Right) {
            wrist.x = -wrist.x;
            elbow.x = -elbow.x;
        }
        const Vec3f span = elbow - wrist;
        const float length = Input::Length(span);
        if (at.arm && length > 1e-3f) {
            const Vec3f to_hand = span * (-1.0f / length);
            const Vec3f old_axis = Input::Rotate(p.orientation, {1.0f, 0.0f, 0.0f});
            const Vec3f cross = Input::Cross(old_axis, to_hand);
            const float s = Input::Length(cross), c = Input::Dot(old_axis, to_hand);
            const Vec3f old_middle = middle;
            if (s > 1e-5f && c > -0.99f) {
                const Vec3f n = cross * (1.0f / s);
                const float half = 0.5f * std::atan2(s, c);
                const Quatf q{n.x * std::sin(half), n.y * std::sin(half), n.z * std::sin(half),
                              std::cos(half)};
                p.position = old_middle + Input::Rotate(q, p.position - old_middle);
                p.orientation = Input::Normalized(Input::Multiply(q, p.orientation));
            }
            const float t = Input::Dot(old_middle - wrist, span) / (length * length);
            middle = wrist + span * std::clamp(t, 0.0f, 1.0f);
            p.position = p.position + (middle - old_middle);
        }
    }
    // AzaharFrame kiu140: round the wrist's line (through the handle's middle, along the picture's
    // own +X) and along it. The picture keeps reading along the forearm and facing out.
    if (at.angle_deg != 0.0f || at.up_arm_m != 0.0f || at.out_m != 0.0f) {
        const Vec3f axis = Input::Rotate(p.orientation, {1.0f, 0.0f, 0.0f});
        const float half = 0.5f * at.angle_deg * 3.14159265f / 180.0f;
        const float s = std::sin(half);
        const Quatf turn{axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
        p.position = middle + Input::Rotate(turn, p.position - middle) - axis * at.up_arm_m;
        if (at.out_m != 0.0f) {
            const Vec3f off = p.position - middle;
            const Vec3f across = off - axis * Input::Dot(off, axis);
            p.position = p.position + Input::Normalized(across) * at.out_m;
        }
        p.orientation = Input::Normalized(Input::Multiply(turn, p.orientation));
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

Posef WristScreenPose(const Posef& aim, Input::Hand hand, bool beside_stats, WristSide side,
                      float width, WristAt at) {
    const Posef local = WristScreenLocalPose(hand, beside_stats, side, width, at);
    return {Input::Normalized(Input::Multiply(aim.orientation, local.orientation)),
            Input::Transform(aim, local.position)};
}

int UprightTurns(const Posef& panel, const Vec3f& head_up, int now, float keep_deg) {
    const float x = Input::Dot(head_up, Input::Rotate(panel.orientation, {1.0f, 0.0f, 0.0f}));
    const float y = Input::Dot(head_up, Input::Rotate(panel.orientation, {0.0f, 1.0f, 0.0f}));
    if (x * x + y * y < 0.04f) {
        return now; // seen edge on, or nothing to go by
    }
    const float turned = std::atan2(-x, y) * 180.0f / 3.14159265f; // the picture's top to it
    float off = turned - 90.0f * static_cast<float>(now);
    off -= 360.0f * std::round(off / 360.0f);
    if (std::abs(off) < keep_deg) {
        return now;
    }
    return (static_cast<int>(std::lround(turned / 90.0f)) + 4) % 4;
}

Posef TurnedQuarters(const Posef& panel, int turns) {
    const float half = 0.25f * 3.14159265f * static_cast<float>(turns);
    return {Input::Normalized(Input::Multiply(
                panel.orientation, Quatf{0.0f, 0.0f, std::sin(half), std::cos(half)})),
            panel.position};
}

WristScreen::Out WristScreen::Update(bool wanted, bool tracked, const Posef& aim,
                                     Input::Hand hand, bool beside_stats, const Vec3f& eye,
                                     WristSide side, float width, WristAt at,
                                     const Vec3f& head_up) {
    Out out;
    const bool was = shown_;
    if (wanted && tracked) {
        out.pose = WristScreenPose(aim, hand, beside_stats, side, width, at);
        turns_ = UprightTurns(out.pose, head_up, turns_, shown_ ? kUprightShownDeg : kUprightDeg);
        out.pose = TurnedQuarters(out.pose, turns_);
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

// AzaharFrame kiuaim
bool WristAbsSwitchOn(const std::optional<std::string>& value) {
    return WristHoldSwitchOn(value); // on unless it says off
}

// AzaharFrame kiupanel
bool WristHoldSwitchOn(const std::optional<std::string>& value) {
    if (!value) {
        return true;
    }
    std::string v = *value;
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return !(v == "0" || v == "off" || v == "false");
}

WristAimHold::Out WristAimHold::Update(bool on_screen, int64_t now_ns) {
    Out out;
    const bool was = holding_;
    if (on_screen) {
        holding_ = true;
        last_on_ns_ = now_ns;
    } else if (holding_ && now_ns - last_on_ns_ > kGraceNs) {
        holding_ = false;
    }
    out.hold = holding_;
    out.changed = holding_ != was;
    return out;
}

} // namespace FrameXr::Vis
