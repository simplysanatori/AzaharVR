// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include "common/settings.h"
#include "frame_xr/input/controls.h"

// AzaharFrame
// The default VR input profile: which virtual gamepad control drives which 3DS button or stick.
// This is CitraVR's mapping with the Frame's real D-pad as the D-pad, and with L and R on the
// bumpers (the grips only grab the screens). The table is the single source of truth: the default profile strings, the tests and MAPPING.md all follow it.

namespace FrameXr::Input {

struct ButtonBinding {
    Control control;
    Settings::NativeButton::Values button;
};

struct AnalogBinding {
    AnalogControl control;
    Settings::NativeAnalog::Values analog;
};

/// The pad's A, B, X, Y are the controls in the places of the 3DS's A, B, X, Y: the mapper swaps the
/// Frame's buttons by position (or not, "Face Buttons" setting), so this table does not change.
///
/// Controls that are deliberately unbound by default: GripL, GripR (they grab the screens, handled
/// by the mapper), StickClickL and StickClickR (tap or hold
/// acts on the screens, handled by the mapper before the game sees it).
inline constexpr std::array<ButtonBinding, 14> kDefaultButtonBindings{{
    {Control::A, Settings::NativeButton::A},
    {Control::B, Settings::NativeButton::B},
    {Control::X, Settings::NativeButton::X},
    {Control::Y, Settings::NativeButton::Y},
    {Control::DpadUp, Settings::NativeButton::Up},
    {Control::DpadDown, Settings::NativeButton::Down},
    {Control::DpadLeft, Settings::NativeButton::Left},
    {Control::DpadRight, Settings::NativeButton::Right},
    {Control::BumperL, Settings::NativeButton::L},
    {Control::BumperR, Settings::NativeButton::R},
    {Control::TriggerL, Settings::NativeButton::ZL},
    {Control::TriggerR, Settings::NativeButton::ZR},
    {Control::Menu, Settings::NativeButton::Start},
    {Control::View, Settings::NativeButton::Select},
}};

inline constexpr std::array<AnalogBinding, 2> kDefaultAnalogBindings{{
    {AnalogControl::StickL, Settings::NativeAnalog::CirclePad},
    {AnalogControl::StickR, Settings::NativeAnalog::CStick},
}};

/// Applies the default profile to a pad state, giving the 3DS side of a mapper result.
struct ThreeDsPadState {
    std::array<bool, Settings::NativeButton::NumButtons> buttons{};
    std::array<Vec2f, Settings::NativeAnalog::NumAnalogs> analogs{};
};

// AzaharVR: the controller schemes (docs/PCVR_PLAN.md section 3.3), one row per interaction
// profile, picked at run time from xrGetCurrentInteractionProfile. A scheme turns the physical
// controls into the controls above (grips into BumperL / BumperR, a stick click into View, ...),
// so the tables above and the Controls dialog's profile apply to every controller unchanged. The
// Frame's row is the Frame's own mapping: the PC adapter (pc_scheme.h) passes it through as is.
// The paths each profile binds are in actions.cpp (kIndexPaths, kFocus3Paths, ...).

enum class ControllerScheme : uint8_t { Frame, Touch, Index, Focus3 };

/// Where the Menu role (tap: Start, held 1 s: the VR menu, held: the chords) comes from.
enum class MenuSource : uint8_t { MenuButton, RightTrackpadPress };
/// Where the D-pad comes from.
enum class DpadSource : uint8_t {
    FrameDpad,        ///< a real D-pad (the Frame's left controller)
    LeftStickToggle,  ///< the left stick, while its click has switched it to the D-pad (four-way)
    LeftTrackpadPress ///< the left trackpad pressed: top, bottom, left or right of it
};

struct ControllerSchemeTable {
    ControllerScheme scheme;
    const char* profile; ///< the interaction profile path
    const char* name;    ///< for the log
    MenuSource menu;
    DpadSource dpad;
    /// In a game the grips are L and R (BumperL, BumperR); they grab nothing. Off: the Frame's
    /// grips grab the screens.
    bool grips_are_lr;
    /// Both stick clicks together take the Menu role (if the runtime keeps the Menu button).
    bool both_clicks_menu;
    /// A right stick click (tap) is Select (View); a left one switches the D-pad mode when `dpad`
    /// is LeftStickToggle. Off: the Frame's stick-click screen gestures.
    bool stick_clicks_for_game;
    /// Face buttons by label (A is A): the button letters are the 3DS's own.
    bool face_buttons_by_label;
};

inline constexpr std::array<ControllerSchemeTable, 4> kControllerSchemes{{
    {ControllerScheme::Frame, "/interaction_profiles/valve/frame_controller", "Steam Frame",
     MenuSource::MenuButton, DpadSource::FrameDpad, false, false, false, false},
    {ControllerScheme::Touch, "/interaction_profiles/oculus/touch_controller", "Touch",
     MenuSource::MenuButton, DpadSource::LeftStickToggle, true, true, true, true},
    {ControllerScheme::Index, "/interaction_profiles/valve/index_controller", "Index",
     MenuSource::RightTrackpadPress, DpadSource::LeftTrackpadPress, true, false, true, true},
    {ControllerScheme::Focus3, "/interaction_profiles/htc/vive_focus3_controller", "Vive Focus 3",
     MenuSource::MenuButton, DpadSource::LeftStickToggle, true, true, true, true},
}};

inline const ControllerSchemeTable& SchemeTable(ControllerScheme scheme) {
    return kControllerSchemes[static_cast<std::size_t>(scheme)];
}

/// The scheme of an interaction profile path. Touch's for a profile with no row (Touch-like
/// controllers SteamVR may present: meta/touch_controller_plus, the wands, simple_controller);
/// the Frame's for none (no controllers yet), which is the mapping of every build before.
inline ControllerScheme SchemeForProfile(std::string_view profile) {
    if (profile.empty()) {
        return ControllerScheme::Frame;
    }
    for (const auto& row : kControllerSchemes) {
        if (profile == row.profile) {
            return row.scheme;
        }
    }
    return ControllerScheme::Touch;
}

inline ThreeDsPadState ApplyDefaultBindings(const ControlState& controls) {
    ThreeDsPadState pad;
    for (const auto& binding : kDefaultButtonBindings) {
        if (controls.Get(binding.control)) {
            pad.buttons[binding.button] = true;
        }
    }
    for (const auto& binding : kDefaultAnalogBindings) {
        pad.analogs[binding.analog] = controls.GetStick(binding.control);
    }
    return pad;
}

} // namespace FrameXr::Input
