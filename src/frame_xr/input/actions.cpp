// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "common/frame_product.h" // AzaharVR
#include "common/logging/log.h"
#include "frame_xr/input/actions.h"
#include "frame_xr/input/pose_guard.h" // AzaharFrame ctrlglitch: TrackBits
#include "frame_xr/xr_calls.h" // AzaharFrame: calls through the run-time loader

// AzaharFrame
namespace FrameXr::Input {

struct Actions::Impl {
    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    std::array<XrPath, kNumHands> hand_path{};

    // Per hand (subaction paths /user/hand/left and /user/hand/right)
    XrAction trigger = XR_NULL_HANDLE;
    XrAction squeeze = XR_NULL_HANDLE;
    XrAction bumper = XR_NULL_HANDLE;
    XrAction stick = XR_NULL_HANDLE;
    XrAction stick_click = XR_NULL_HANDLE;
    XrAction aim_pose = XR_NULL_HANDLE;
    XrAction haptic = XR_NULL_HANDLE; ///< AzaharFrame kiugyro: Light Gun's recoil (output)
    bool haptic_bound = false;
    int pulses = 0;
    // Right controller
    XrAction a = XR_NULL_HANDLE;
    XrAction b = XR_NULL_HANDLE;
    XrAction x = XR_NULL_HANDLE;
    XrAction y = XR_NULL_HANDLE;
    XrAction menu = XR_NULL_HANDLE;
    // Left controller
    XrAction view = XR_NULL_HANDLE;
    XrAction dpad_up = XR_NULL_HANDLE;
    XrAction dpad_down = XR_NULL_HANDLE;
    XrAction dpad_left = XR_NULL_HANDLE;
    XrAction dpad_right = XR_NULL_HANDLE;
    // AzaharVR: the Index trackpads (PC builds only; null on the Frame's).
    XrAction trackpad = XR_NULL_HANDLE;
    XrAction trackpad_force = XR_NULL_HANDLE;

    std::array<XrSpace, kNumHands> aim_space{};
    bool created_set = false;
};

namespace {

XrPath ToPath(XrInstance instance, const char* text) {
    XrPath path = XR_NULL_PATH;
    xrStringToPath(instance, text, &path);
    return path;
}

XrAction MakeAction(XrActionSet set, const char* name, const char* localized, XrActionType type,
                    const XrPath* subaction_paths, uint32_t subaction_count) {
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = type;
    std::snprintf(info.actionName, sizeof(info.actionName), "%s", name);
    std::snprintf(info.localizedActionName, sizeof(info.localizedActionName), "%s", localized);
    info.countSubactionPaths = subaction_count;
    info.subactionPaths = subaction_paths;
    XrAction action = XR_NULL_HANDLE;
    const XrResult result = xrCreateAction(set, &info, &action);
    if (XR_FAILED(result)) {
        LOG_ERROR(Input, "xrCreateAction({}) failed: {}", name, static_cast<int>(result));
        return XR_NULL_HANDLE;
    }
    return action;
}

bool GetBool(XrSession session, XrAction action, XrPath sub) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = action;
    info.subactionPath = sub;
    XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_FAILED(xrGetActionStateBoolean(session, &info, &state))) {
        return false;
    }
    return state.isActive && state.currentState;
}

float GetFloat(XrSession session, XrAction action, XrPath sub) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = action;
    info.subactionPath = sub;
    XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
    if (XR_FAILED(xrGetActionStateFloat(session, &info, &state))) {
        return 0.0f;
    }
    return state.isActive ? state.currentState : 0.0f;
}

Vec2f GetVec2(XrSession session, XrAction action, XrPath sub) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = action;
    info.subactionPath = sub;
    XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
    if (XR_FAILED(xrGetActionStateVector2f(session, &info, &state)) || !state.isActive) {
        return {};
    }
    return {state.currentState.x, state.currentState.y};
}

bool PoseActive(XrSession session, XrAction action, XrPath sub) {
    XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
    info.action = action;
    info.subactionPath = sub;
    XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
    if (XR_FAILED(xrGetActionStatePose(session, &info, &state))) {
        return false;
    }
    return state.isActive == XR_TRUE;
}

using Bindings = std::vector<XrActionSuggestedBinding>;

#ifdef AZAHARVR_PC
// AzaharVR: the suggested bindings of the PC controllers, as data (docs/PCVR_PLAN.md section 3.3).
// Which role each action plays is the scheme's (default_bindings.h kControllerSchemes).
enum class Act : uint8_t {
    Stick, StickClick, Trigger, Squeeze, A, B, X, Y, Menu, Aim, Trackpad, TrackpadForce
};
struct SuggestedPath {
    Act act;
    const char* path;
};
/// Valve Index: left A and B are X and Y (X lower, Y upper, as on Touch); the grips from
/// squeeze/force (squeeze/value rises from simply holding it); the trackpads, read as the D-pad
/// (left) and the Menu role (right) by the scheme. The System buttons belong to SteamVR.
constexpr SuggestedPath kIndexPaths[] = {
    {Act::Stick, "/user/hand/left/input/thumbstick"},
    {Act::Stick, "/user/hand/right/input/thumbstick"},
    {Act::StickClick, "/user/hand/left/input/thumbstick/click"},
    {Act::StickClick, "/user/hand/right/input/thumbstick/click"},
    {Act::Trigger, "/user/hand/left/input/trigger/value"},
    {Act::Trigger, "/user/hand/right/input/trigger/value"},
    {Act::Squeeze, "/user/hand/left/input/squeeze/force"},
    {Act::Squeeze, "/user/hand/right/input/squeeze/force"},
    {Act::A, "/user/hand/right/input/a/click"},
    {Act::B, "/user/hand/right/input/b/click"},
    {Act::X, "/user/hand/left/input/a/click"},
    {Act::Y, "/user/hand/left/input/b/click"},
    {Act::Trackpad, "/user/hand/left/input/trackpad"},
    {Act::Trackpad, "/user/hand/right/input/trackpad"},
    {Act::TrackpadForce, "/user/hand/left/input/trackpad/force"},
    {Act::TrackpadForce, "/user/hand/right/input/trackpad/force"},
    {Act::Aim, "/user/hand/left/input/aim/pose"},
    {Act::Aim, "/user/hand/right/input/aim/pose"},
};
/// Vive Focus 3 and XR Elite: Touch's layout (X, Y and Menu on the left). squeeze/value came in
/// the extension's second revision; squeeze/click is the fallback.
constexpr SuggestedPath kFocus3Paths[] = {
    {Act::Stick, "/user/hand/left/input/thumbstick"},
    {Act::Stick, "/user/hand/right/input/thumbstick"},
    {Act::StickClick, "/user/hand/left/input/thumbstick/click"},
    {Act::StickClick, "/user/hand/right/input/thumbstick/click"},
    {Act::Trigger, "/user/hand/left/input/trigger/value"},
    {Act::Trigger, "/user/hand/right/input/trigger/value"},
    {Act::Squeeze, "/user/hand/left/input/squeeze/value"},
    {Act::Squeeze, "/user/hand/right/input/squeeze/value"},
    {Act::A, "/user/hand/right/input/a/click"},
    {Act::B, "/user/hand/right/input/b/click"},
    {Act::X, "/user/hand/left/input/x/click"},
    {Act::Y, "/user/hand/left/input/y/click"},
    {Act::Menu, "/user/hand/left/input/menu/click"},
    {Act::Aim, "/user/hand/left/input/aim/pose"},
    {Act::Aim, "/user/hand/right/input/aim/pose"},
};
#endif

bool Suggest(XrInstance instance, const char* profile, const Bindings& bindings) {
    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = ToPath(instance, profile);
    suggested.suggestedBindings = bindings.data();
    suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    const XrResult result = xrSuggestInteractionProfileBindings(instance, &suggested);
    LOG_INFO(Input, "suggested bindings for {}: {}", profile, static_cast<int>(result));
    return XR_SUCCEEDED(result);
}

} // namespace

Actions::~Actions() {
    Destroy();
}

bool Actions::Create(XrInstance instance, bool has_frame_controller, bool has_focus3) {
    Destroy();
    impl_ = new Impl;
    Impl& s = *impl_;
    s.instance = instance;
    s.hand_path[0] = ToPath(instance, "/user/hand/left");
    s.hand_path[1] = ToPath(instance, "/user/hand/right");

    XrActionSetCreateInfo set_info{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(set_info.actionSetName, "azaharframe_input");
    std::snprintf(set_info.localizedActionSetName, sizeof(set_info.localizedActionSetName), "%s",
                  Common::FrameProduct::kName); // AzaharVR
    if (XR_FAILED(xrCreateActionSet(instance, &set_info, &action_set_))) {
        LOG_ERROR(Input, "xrCreateActionSet failed");
        return false;
    }

    const XrPath* hands = s.hand_path.data();
    s.trigger = MakeAction(action_set_, "trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, hands, 2);
    s.squeeze = MakeAction(action_set_, "squeeze", "Grip", XR_ACTION_TYPE_FLOAT_INPUT, hands, 2);
    s.bumper = MakeAction(action_set_, "bumper", "Bumper", XR_ACTION_TYPE_BOOLEAN_INPUT, hands, 2);
    s.stick = MakeAction(action_set_, "stick", "Stick", XR_ACTION_TYPE_VECTOR2F_INPUT, hands, 2);
    s.stick_click =
        MakeAction(action_set_, "stick_click", "Stick Click", XR_ACTION_TYPE_BOOLEAN_INPUT, hands, 2);
    s.aim_pose = MakeAction(action_set_, "aim_pose", "Aim", XR_ACTION_TYPE_POSE_INPUT, hands, 2);
    s.a = MakeAction(action_set_, "button_a", "A", XR_ACTION_TYPE_BOOLEAN_INPUT, nullptr, 0);
    s.b = MakeAction(action_set_, "button_b", "B", XR_ACTION_TYPE_BOOLEAN_INPUT, nullptr, 0);
    s.x = MakeAction(action_set_, "button_x", "X", XR_ACTION_TYPE_BOOLEAN_INPUT, nullptr, 0);
    s.y = MakeAction(action_set_, "button_y", "Y", XR_ACTION_TYPE_BOOLEAN_INPUT, nullptr, 0);
    s.menu = MakeAction(action_set_, "menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT, nullptr, 0);
    s.view = MakeAction(action_set_, "view", "View", XR_ACTION_TYPE_BOOLEAN_INPUT, nullptr, 0);
    s.dpad_up =
        MakeAction(action_set_, "dpad_up", "D-Pad Up", XR_ACTION_TYPE_BOOLEAN_INPUT, nullptr, 0);
    s.dpad_down = MakeAction(action_set_, "dpad_down", "D-Pad Down", XR_ACTION_TYPE_BOOLEAN_INPUT,
                             nullptr, 0);
    s.dpad_left = MakeAction(action_set_, "dpad_left", "D-Pad Left", XR_ACTION_TYPE_BOOLEAN_INPUT,
                             nullptr, 0);
    s.dpad_right = MakeAction(action_set_, "dpad_right", "D-Pad Right",
                              XR_ACTION_TYPE_BOOLEAN_INPUT, nullptr, 0);

#ifdef AZAHARVR_PC
    s.trackpad = MakeAction(action_set_, "trackpad", "Trackpad", XR_ACTION_TYPE_VECTOR2F_INPUT,
                            hands, 2);
    s.trackpad_force = MakeAction(action_set_, "trackpad_force", "Trackpad Press",
                                  XR_ACTION_TYPE_FLOAT_INPUT, hands, 2);
#else
    (void)has_focus3;
#endif
    const XrAction all[] = {s.trigger, s.squeeze,  s.bumper,   s.stick,    s.stick_click, s.aim_pose,
                            s.a,       s.b,        s.x,        s.y,        s.menu,        s.view,
                            s.dpad_up, s.dpad_down, s.dpad_left, s.dpad_right};
    for (const XrAction action : all) {
        if (action == XR_NULL_HANDLE) {
            return false;
        }
    }

    const auto path = [&](const char* text) { return ToPath(instance, text); };
    // AzaharFrame kiugyro: the controllers' haptics (Light Gun's recoil). Every profile has the
    // output; a runtime that refused it gets the bindings again without it, as Moonlight XR does.
    s.haptic = MakeAction(action_set_, "haptic", "Recoil", XR_ACTION_TYPE_VIBRATION_OUTPUT, hands, 2);
    const auto with_haptic = [&](Bindings b) {
        if (s.haptic != XR_NULL_HANDLE) {
            b.push_back({s.haptic, path("/user/hand/left/output/haptic")});
            b.push_back({s.haptic, path("/user/hand/right/output/haptic")});
        }
        return b;
    };

    if (has_frame_controller) {
        // Paths as SteamVR names them for the Steam Frame controllers (the same ones KartFrame's
        // and GalaxyFrame's bindings use). The left /input/system is never bound: it belongs to
        // the dashboard.
        const auto frame_bindings = [&](const char* squeeze_name) {
            char left_squeeze[96];
            char right_squeeze[96];
            std::snprintf(left_squeeze, sizeof(left_squeeze), "/user/hand/left/input/%s",
                          squeeze_name);
            std::snprintf(right_squeeze, sizeof(right_squeeze), "/user/hand/right/input/%s",
                          squeeze_name);
            return Bindings{
                {s.stick, path("/user/hand/left/input/thumbstick")},
                {s.stick, path("/user/hand/right/input/thumbstick")},
                {s.stick_click, path("/user/hand/left/input/thumbstick/click")},
                {s.stick_click, path("/user/hand/right/input/thumbstick/click")},
                {s.trigger, path("/user/hand/left/input/trigger/value")},
                {s.trigger, path("/user/hand/right/input/trigger/value")},
                {s.squeeze, path(left_squeeze)},
                {s.squeeze, path(right_squeeze)},
                {s.bumper, path("/user/hand/left/input/bumper/click")},
                {s.bumper, path("/user/hand/right/input/bumper/click")},
                {s.a, path("/user/hand/right/input/a/click")},
                {s.b, path("/user/hand/right/input/b/click")},
                {s.x, path("/user/hand/right/input/x/click")},
                {s.y, path("/user/hand/right/input/y/click")},
                {s.menu, path("/user/hand/right/input/menu/click")},
                {s.view, path("/user/hand/left/input/view/click")},
                {s.dpad_up, path("/user/hand/left/input/dpad_up/click")},
                {s.dpad_down, path("/user/hand/left/input/dpad_down/click")},
                {s.dpad_left, path("/user/hand/left/input/dpad_left/click")},
                {s.dpad_right, path("/user/hand/left/input/dpad_right/click")},
                {s.aim_pose, path("/user/hand/left/input/aim/pose")},
                {s.aim_pose, path("/user/hand/right/input/aim/pose")},
            };
        };
        // SteamVR has taken the grip as squeeze/value and as grip/value.
        constexpr const char* kFrame = "/interaction_profiles/valve/frame_controller";
        s.haptic_bound = true;
        if (!Suggest(instance, kFrame, with_haptic(frame_bindings("squeeze/value"))) &&
            !Suggest(instance, kFrame, with_haptic(frame_bindings("grip/value")))) {
            s.haptic_bound = false;
            if (!Suggest(instance, kFrame, frame_bindings("squeeze/value"))) {
                Suggest(instance, kFrame, frame_bindings("grip/value"));
            }
        }
    }

    // Fallback for development with another runtime or controller (no D-pad, View or bumpers).
    const Bindings touch_bindings{
                {s.stick, path("/user/hand/left/input/thumbstick")},
                {s.stick, path("/user/hand/right/input/thumbstick")},
                {s.stick_click, path("/user/hand/left/input/thumbstick/click")},
                {s.stick_click, path("/user/hand/right/input/thumbstick/click")},
                {s.trigger, path("/user/hand/left/input/trigger/value")},
                {s.trigger, path("/user/hand/right/input/trigger/value")},
                {s.squeeze, path("/user/hand/left/input/squeeze/value")},
                {s.squeeze, path("/user/hand/right/input/squeeze/value")},
                {s.a, path("/user/hand/right/input/a/click")},
                {s.b, path("/user/hand/right/input/b/click")},
                {s.x, path("/user/hand/left/input/x/click")},
                {s.y, path("/user/hand/left/input/y/click")},
                {s.menu, path("/user/hand/left/input/menu/click")},
                {s.aim_pose, path("/user/hand/left/input/aim/pose")},
                {s.aim_pose, path("/user/hand/right/input/aim/pose")},
            };
    constexpr const char* kTouch = "/interaction_profiles/oculus/touch_controller";
    if (Suggest(instance, kTouch, with_haptic(touch_bindings))) {
        s.haptic_bound = s.haptic_bound || !has_frame_controller;
    } else {
        Suggest(instance, kTouch, touch_bindings);
    }
#ifdef AZAHARVR_PC
    // AzaharVR: Index and Vive Focus 3, from the tables above.
    const auto action_of = [&](Act act) {
        switch (act) {
        case Act::Stick:
            return s.stick;
        case Act::StickClick:
            return s.stick_click;
        case Act::Trigger:
            return s.trigger;
        case Act::Squeeze:
            return s.squeeze;
        case Act::A:
            return s.a;
        case Act::B:
            return s.b;
        case Act::X:
            return s.x;
        case Act::Y:
            return s.y;
        case Act::Menu:
            return s.menu;
        case Act::Aim:
            return s.aim_pose;
        case Act::Trackpad:
            return s.trackpad;
        case Act::TrackpadForce:
            return s.trackpad_force;
        }
        return XrAction{XR_NULL_HANDLE};
    };
    // `squeeze_from` / `squeeze_to`: a squeeze path's ending swapped (the Focus 3 fallback).
    const auto table = [&](const auto& paths, const char* squeeze_from, const char* squeeze_to) {
        Bindings b;
        for (const SuggestedPath& p : paths) {
            const XrAction action = action_of(p.act);
            if (action == XR_NULL_HANDLE) {
                continue;
            }
            std::string text = p.path;
            const std::size_t at = squeeze_from ? text.find(squeeze_from) : std::string::npos;
            if (p.act == Act::Squeeze && at != std::string::npos) {
                text.replace(at, std::strlen(squeeze_from), squeeze_to);
            }
            b.push_back({action, path(text.c_str())});
        }
        return b;
    };
    constexpr const char* kIndex = "/interaction_profiles/valve/index_controller";
    if (!Suggest(instance, kIndex, with_haptic(table(kIndexPaths, nullptr, nullptr)))) {
        Suggest(instance, kIndex, table(kIndexPaths, nullptr, nullptr));
    }
    if (has_focus3) {
        constexpr const char* kFocus3 = "/interaction_profiles/htc/vive_focus3_controller";
        if (!Suggest(instance, kFocus3, with_haptic(table(kFocus3Paths, nullptr, nullptr))) &&
            !Suggest(instance, kFocus3,
                     with_haptic(table(kFocus3Paths, "squeeze/value", "squeeze/click")))) {
            Suggest(instance, kFocus3, table(kFocus3Paths, "squeeze/value", "squeeze/click"));
        }
    }
#endif
    return true;
}

std::string Actions::CurrentProfile(XrSession session) const { // AzaharVR
    if (impl_ == nullptr || session == XR_NULL_HANDLE) {
        return {};
    }
    for (const int h : {1, 0}) {
        XrInteractionProfileState state{XR_TYPE_INTERACTION_PROFILE_STATE};
        if (XR_FAILED(xrGetCurrentInteractionProfile(
                session, impl_->hand_path[static_cast<std::size_t>(h)], &state)) ||
            state.interactionProfile == XR_NULL_PATH) {
            continue;
        }
        char text[XR_MAX_PATH_LENGTH] = {};
        uint32_t length = 0;
        if (XR_SUCCEEDED(xrPathToString(impl_->instance, state.interactionProfile, sizeof(text),
                                        &length, text))) {
            return text;
        }
    }
    return {};
}

bool Actions::Pulse(XrSession session, int hand, int64_t duration_ns, float amplitude) {
    if (impl_ == nullptr || impl_->haptic == XR_NULL_HANDLE || !impl_->haptic_bound ||
        session == XR_NULL_HANDLE || hand < 0 || hand >= kNumHands) {
        return false;
    }
    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
    info.action = impl_->haptic;
    info.subactionPath = impl_->hand_path[static_cast<std::size_t>(hand)];
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.duration = duration_ns;
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED; // the runtime's own
    vibration.amplitude = amplitude;
    const XrResult result = xrApplyHapticFeedback(
        session, &info, reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
    if (impl_->pulses++ == 0 || XR_FAILED(result)) {
        LOG_INFO(Input, "recoil on the {} controller: {}", hand == 0 ? "left" : "right",
                 static_cast<int>(result));
    }
    return XR_SUCCEEDED(result);
}

bool Actions::CreateSpaces(XrSession session) {
    if (impl_ == nullptr) {
        return false;
    }
    Impl& s = *impl_;
    s.session = session;
    for (int h = 0; h < kNumHands; ++h) {
        XrActionSpaceCreateInfo info{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        info.action = s.aim_pose;
        info.subactionPath = s.hand_path[static_cast<std::size_t>(h)];
        info.poseInActionSpace.orientation.w = 1.0f;
        if (XR_FAILED(xrCreateActionSpace(session, &info,
                                          &s.aim_space[static_cast<std::size_t>(h)]))) {
            LOG_ERROR(Input, "xrCreateActionSpace failed for hand {}", h);
            return false;
        }
    }
    return true;
}

bool Actions::Attach(XrSession session) {
    if (impl_ == nullptr || action_set_ == XR_NULL_HANDLE) {
        return false;
    }
    XrSessionActionSetsAttachInfo info{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    info.countActionSets = 1;
    info.actionSets = &action_set_;
    const XrResult result = xrAttachSessionActionSets(session, &info);
    if (XR_FAILED(result)) {
        LOG_ERROR(Input, "xrAttachSessionActionSets failed: {}", static_cast<int>(result));
        return false;
    }
    return CreateSpaces(session);
}

bool Actions::Sync(XrSession session, XrSpace base, XrTime time, InputFrame& out, bool sync,
                   XrDuration ahead) {
    if (impl_ == nullptr) {
        return false;
    }
    Impl& s = *impl_;
    if (sync) {
        XrActiveActionSet active{action_set_, XR_NULL_PATH};
        XrActionsSyncInfo sync_info{XR_TYPE_ACTIONS_SYNC_INFO};
        sync_info.countActiveActionSets = 1;
        sync_info.activeActionSets = &active;
        if (XR_FAILED(xrSyncActions(session, &sync_info))) {
            return false;
        }
    }

    InputFrame frame;
    frame.time_ns = static_cast<int64_t>(time);
    for (int h = 0; h < kNumHands; ++h) {
        const auto i = static_cast<std::size_t>(h);
        const XrPath sub = s.hand_path[i];
        HandInput& hand = frame.hand[i];
        hand.trigger = GetFloat(session, s.trigger, sub);
        hand.grip = GetFloat(session, s.squeeze, sub);
        hand.bumper = GetBool(session, s.bumper, sub);
        hand.stick = GetVec2(session, s.stick, sub);
        hand.stick_click = GetBool(session, s.stick_click, sub);
        if (s.trackpad != XR_NULL_HANDLE) { // AzaharVR: PC builds only
            hand.trackpad = GetVec2(session, s.trackpad, sub);
            hand.trackpad_force = GetFloat(session, s.trackpad_force, sub);
        }

        if (PoseActive(session, s.aim_pose, sub) && s.aim_space[i] != XR_NULL_HANDLE) {
            XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
            if (XR_SUCCEEDED(xrLocateSpace(s.aim_space[i], base, time, &location))) {
                constexpr XrSpaceLocationFlags kValid =
                    XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
                // AzaharFrame ctrlglitch: what SteamVR says of it (tracked, or only inferred).
                const XrSpaceLocationFlags f = location.locationFlags;
                hand.track = static_cast<uint8_t>(
                    ((f & XR_SPACE_LOCATION_POSITION_VALID_BIT) ? kTrackPositionValid : 0) |
                    ((f & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) ? kTrackOrientationValid : 0) |
                    ((f & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) ? kTrackPositionTracked : 0) |
                    ((f & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) ? kTrackOrientationTracked
                                                                     : 0));
                if ((location.locationFlags & kValid) == kValid) {
                    hand.active = true;
                    hand.aim.orientation = {location.pose.orientation.x, location.pose.orientation.y,
                                            location.pose.orientation.z, location.pose.orientation.w};
                    hand.aim.position = {location.pose.position.x, location.pose.position.y,
                                         location.pose.position.z};
                }
            }
            // AzaharFrame ctrlglitch: the pose for the next display time, where the models'
            // image drawn now is on view.
            XrSpaceLocation next{XR_TYPE_SPACE_LOCATION};
            if (hand.active && ahead > 0 &&
                XR_SUCCEEDED(xrLocateSpace(s.aim_space[i], base, time + ahead, &next))) {
                constexpr XrSpaceLocationFlags kValid =
                    XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
                if ((next.locationFlags & kValid) == kValid) {
                    hand.ahead_valid = true;
                    hand.aim_ahead.orientation = {next.pose.orientation.x, next.pose.orientation.y,
                                                  next.pose.orientation.z, next.pose.orientation.w};
                    hand.aim_ahead.position = {next.pose.position.x, next.pose.position.y,
                                               next.pose.position.z};
                }
            }
        }
    }
    frame.a = GetBool(session, s.a, XR_NULL_PATH);
    frame.b = GetBool(session, s.b, XR_NULL_PATH);
    frame.x = GetBool(session, s.x, XR_NULL_PATH);
    frame.y = GetBool(session, s.y, XR_NULL_PATH);
    frame.menu = GetBool(session, s.menu, XR_NULL_PATH);
    frame.view = GetBool(session, s.view, XR_NULL_PATH);
    frame.dpad_up = GetBool(session, s.dpad_up, XR_NULL_PATH);
    frame.dpad_down = GetBool(session, s.dpad_down, XR_NULL_PATH);
    frame.dpad_left = GetBool(session, s.dpad_left, XR_NULL_PATH);
    frame.dpad_right = GetBool(session, s.dpad_right, XR_NULL_PATH);
    out = frame;
    return true;
}

void Actions::Destroy() {
    if (impl_ != nullptr) {
        for (XrSpace& space : impl_->aim_space) {
            if (space != XR_NULL_HANDLE) {
                xrDestroySpace(space);
                space = XR_NULL_HANDLE;
            }
        }
        delete impl_;
        impl_ = nullptr;
    }
    if (action_set_ != XR_NULL_HANDLE) {
        xrDestroyActionSet(action_set_); // destroys its actions too
        action_set_ = XR_NULL_HANDLE;
    }
}

} // namespace FrameXr::Input
