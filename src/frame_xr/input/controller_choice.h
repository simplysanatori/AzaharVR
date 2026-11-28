// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

#include "frame_xr/input/controller_names.h"
#include "frame_xr/input/default_bindings.h"

// AzaharVR pchelp
// Which controllers the VR controls are for: the Controllers setting (Settings::values.
// vr_controller_type, global, PC builds only) over the scheme of the interaction profile SteamVR
// reports, and the names that follow from it (Edit Controls' device, the log lines). The scheme
// chosen drives the input (pc_scheme.h), the controls help and its drawings. Pure, but for the
// one atomic the XR thread keeps up to date.
namespace FrameXr::Input {

/// The scheme the controls use: `detected` (from the interaction profile) for Automatic or an
/// unknown value, else the one the setting names. The Frame's build always has the Frame's.
constexpr ControllerScheme ChosenScheme(uint32_t setting, ControllerScheme detected, bool pc) {
    if (!pc) {
        return ControllerScheme::Frame;
    }
    switch (static_cast<ControllerType>(setting)) {
    case ControllerType::SteamFrame:
        return ControllerScheme::Frame;
    case ControllerType::MetaQuest:
        return ControllerScheme::Touch;
    case ControllerType::ValveIndex:
        return ControllerScheme::Index;
    case ControllerType::ViveFocus3:
        return ControllerScheme::Focus3;
    default:
        return detected;
    }
}

/// For the log: "Automatic", "Meta Quest", ...
constexpr const char* ControllerTypeName(uint32_t setting) {
    return setting < kControllerTypeNames.size() ? kControllerTypeNames[setting] : "Automatic";
}

/// The VR controllers' device in Edit Controls: on PC named after the scheme ("Meta Quest
/// Controllers"), on the Frame "Frame Controllers" as before.
constexpr const char* ControllersDeviceName(ControllerScheme scheme, bool pc) {
    if (!pc) {
        return "Frame Controllers";
    }
    switch (scheme) {
    case ControllerScheme::Touch:
        return "Meta Quest Controllers";
    case ControllerScheme::Index:
        return "Valve Index Controllers";
    case ControllerScheme::Focus3:
        return "Vive XR Elite Controllers";
    case ControllerScheme::Frame:
    default:
        return "Steam Frame Controllers";
    }
}

/// The control that has the Menu role (tap Start, hold the VR menu, held the chords) on a PC
/// scheme: "Menu", or "Right Trackpad" (Index). The Frame's chords are View's.
constexpr const char* MenuRoleName(ControllerScheme scheme) {
    if (scheme == ControllerScheme::Frame) {
        return "View";
    }
    return SchemeTable(scheme).menu == MenuSource::RightTrackpadPress ? "Right Trackpad" : "Menu";
}

/// For the log lines of the chords: "View, then Right Bumper" on the Frame, "Menu, then Right
/// Grip" with Touch, and so on. `hand` 0 left, 1 right; `trigger` the right trigger.
inline std::string ChordLogName(ControllerScheme scheme, int hand, bool trigger) {
    const bool pc = scheme != ControllerScheme::Frame && SchemeTable(scheme).grips_are_lr;
    std::string s = std::string{MenuRoleName(scheme)} + ", then ";
    if (trigger) {
        return s + "Right Trigger";
    }
    return s + (hand == 0 ? "Left " : "Right ") + (pc ? "Grip" : "Bumper");
}

/// The scheme in use (the XR thread sets it; the controls help, Edit Controls' device name and
/// the in-game menu read it).
inline std::atomic<ControllerScheme> g_active_scheme{ControllerScheme::Frame};

} // namespace FrameXr::Input
