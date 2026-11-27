// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiu: the immersive settings' names and tooltips, in one place, so a name changes in
// one edit: the VR menu, the toasts, the help diagram and Azahar's window all take them from here.
// Header only (the menu library and Qt use it without the rest of frame_xr).

#pragma once

#include <algorithm> // AzaharFrame natural: World Scale's text
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace FrameXr::Immersive::Names {

/// Settings::values.vr_immersive_wider_view_level (was "Wider View").
inline constexpr const char* kRenderMargin = "Render Margin";
/// AzaharFrame fovaxis: Render Margin per axis (Settings::values.vr_immersive_render_margin_x,
/// _y; render_margin.h): the VR menu's and Azahar's window's rows, which replace the one Render
/// Margin row (the toasts and logs keep kRenderMargin). Shown with Stretch only (Field of View
/// hides them; Layout::MarginFactor).
inline constexpr const char* kRenderMarginHorizontal = "Render Margin: Horizontal";
inline constexpr const char* kRenderMarginHorizontalTip =
    "Draws more of the game left and right of the 3DS screen, up to 1.6 times its view, and the "
    "screen grows with it. Off adds nothing.";
inline constexpr const char* kRenderMarginVertical = "Render Margin: Vertical";
inline constexpr const char* kRenderMarginVerticalTip =
    "Draws more of the game above and below the 3DS screen, up to 1.6 times its view, and the "
    "screen grows with it. Off adds nothing.";
/// Settings::values.vr_immersive_scale_x and _y (were Narrow Screen and Narrow Height).
/// AzaharFrame fovaxis: in Field of View a narrower screen crops; Stretch squeezes.
inline constexpr const char* kHorizontalScale = "Horizontal Scale";
inline constexpr const char* kHorizontalScaleTip =
    "Makes the screen wider or narrower. With Stretch, narrower brings the side HUD in.";
inline constexpr const char* kVerticalScale = "Vertical Scale";
inline constexpr const char* kVerticalScaleTip = "Makes the immersive screen taller or shorter.";
/// AzaharFrame hudmove: Settings::values.vr_immersive_hud_spacing_x and _y, HUD Spacing
/// (experimental; video_core/vr_hud_spacing.h). The rows and their tips: approved by the owner
/// (1.3.0).
inline constexpr const char* kHudSpacingHorizontal = "HUD Spacing: Horizontal";
inline constexpr const char* kHudSpacingHorizontalTip =
    "Experimental. In the Full modes, lower brings the game's HUD in from the sides, closer into "
    "view; higher spreads it out. 100% is the game's own.";
inline constexpr const char* kHudSpacingVertical = "HUD Spacing: Vertical";
inline constexpr const char* kHudSpacingVerticalTip =
    "Experimental. In the Full modes, lower brings the game's HUD in from above and below, closer "
    "into view; higher spreads it out. 100% is the game's own.";
/// AzaharFrame fovaxis: the note under Horizontal or Vertical Scale's label (VR menu) and after
/// its value (Azahar's window) when the settings go past the caps against warping
/// (Layout::ScalesMayWarp).
inline constexpr const char* kMayWarp = "May Warp";
/// AzaharFrame fovscale: Settings::values.vr_immersive_scale_mode (0 Stretch, 1 Field of View):
/// how the two scales act. The VR menu's segmented row and Azahar's window.
inline constexpr const char* kScaleMode = "Scale Mode";
inline constexpr const char* kScaleModeTip = "How Horizontal and Vertical Scale change the picture.";
inline constexpr const char* kScaleStretch = "Stretch";
inline constexpr const char* kScaleStretchTip =
    "Stretches the whole picture, HUD included. The world looks wider or thinner.";
inline constexpr const char* kScaleFieldOfView = "Field of View";
inline constexpr const char* kScaleFieldOfViewTip =
    "Shows more or less of the game, cropping when smaller. No warping up to +60%.";
/// AzaharVR foveation: Settings::values.vr_foveated_rendering (0 Off, 1 Low, 2 Medium, 3 High).
inline constexpr const char* kFoveatedRendering = "Foveated Rendering";
inline constexpr const char* kFoveatedRenderingTip =
    "Experimental. Lowers shading rate outside your gaze in Immersive Mode to save GPU time.";
inline constexpr const char* kFoveationOff = "Off";
inline constexpr const char* kFoveationLow = "Low";
inline constexpr const char* kFoveationMedium = "Medium";
inline constexpr const char* kFoveationHigh = "High";
/// AzaharFrame fovaxis: Fill View (VR menu, under Scale Mode): sets the screen's size and
/// Horizontal Scale once so the screen fills most of the view across (Layout::FillView, 1.2.0:
/// across only; Vertical Scale stays); the toast is Layout::FillToast.
inline constexpr const char* kFillView = "Fill View";
/// 1.2.0 fix lane B: unapproved wording (was "Sets the screen's size and both scales once to
/// cover your view, as far as they reach. Adjust from there.").
inline constexpr const char* kFillViewTip =
    "Sets the screen's size and Horizontal Scale once so the screen fills most of your view "
    "across. Vertical Scale stays. Adjust from there.";

// AzaharFrame natural: 3D Depth in the Full modes, World Scale and Follow Screen Size. Material 3
// Title Case for names and choices, sentence case for tooltips.

/// The 3D Depth row (VR menu, Graphics): the 3DS slider in Normal and Wide, a share of the
/// natural 3D in the Full modes (vr_immersive_depth).
inline constexpr const char* kDepth3d = "3D Depth";
inline constexpr const char* kDepth3dTip =
    "How deep the 3D looks. In the Full modes, 100% puts the farthest scenery at infinity.";
/// Azahar's window, VR group: the Full modes' 3D Depth.
inline constexpr const char* kImmersiveDepth = "Immersive 3D Depth";
inline constexpr const char* kImmersiveDepthTip =
    "How deep the 3D looks in the Full modes. At 100%, the farthest scenery is at infinity.";

/// Settings::values.vr_immersive_world_scale (was the Depth Factor Multiplier).
inline constexpr const char* kWorldScale = "World Scale";
inline constexpr const char* kWorldScaleTip =
    "How big the game world feels. Hold a grip and push the stick up or down to change it.";
inline constexpr const char* kWorldScaleWindowTip =
    "How big the game world feels in the Full modes. In VR, hold the immersive screen with a "
    "grip and push the stick up or down.";

/// World Scale's range, percent (100: the default), and the slider's stops: the R20 series from
/// 10 to 400 (6 a doubling, each 12 % more than the last).
inline constexpr float kWorldScaleAuto = 100.0f;
inline constexpr float kWorldScaleMin = 10.0f;
inline constexpr float kWorldScaleMax = 400.0f;
inline constexpr std::array<float, 33> kWorldScaleStops{
    10,  11,  12,  14,  16,  18,  20,  22,  25,  28,  32,  36,  40,  45,  50,  56,  63,
    71,  80,  90,  100, 112, 125, 140, 160, 180, 200, 224, 250, 280, 315, 355, 400};
/// Within the range (the default for anything not finite).
inline float ClampWorldScale(float percent) {
    return std::isfinite(percent) ? std::clamp(percent, kWorldScaleMin, kWorldScaleMax)
                                  : kWorldScaleAuto;
}
/// The stop nearest `percent` on the log scale.
inline int WorldScaleStop(float percent) {
    const float p = ClampWorldScale(percent);
    int best = 0;
    for (int i = 1; i < static_cast<int>(kWorldScaleStops.size()); ++i) {
        if (std::abs(std::log(kWorldScaleStops[static_cast<std::size_t>(i)] / p)) <
            std::abs(std::log(kWorldScaleStops[static_cast<std::size_t>(best)] / p))) {
            best = i;
        }
    }
    return best;
}
/// The grip's World Scale after a ratio of the stick's push: exact, only clamped (never snapped to
/// a slider stop; the menu's slider shows the nearest stop, the value stays as it is).
inline float WorldScaleAfterGrip(float before, float ratio) {
    return ClampWorldScale(ClampWorldScale(before) * ratio);
}
/// The whole percent the toast shows for `percent` (what WorldScaleText prints).
inline long WorldScaleWhole(float percent) {
    return std::lround(ClampWorldScale(percent));
}
/// How often the grip's toast may change (nanoseconds). Every change of text redraws the toast row
/// and uploads the whole toast atlas through a blocking copy on the frame thread (which shares the
/// GPU queue with the game): above about 100 % the whole percent changes every frame, 120 uploads a
/// second, which stalls the frames and the game while the grip is held. The value itself is
/// computed every frame; the toast shows it at this rate.
inline constexpr int64_t kWorldToastIntervalNs = 80'000'000;
/// When the grip's toast changes: at most every kWorldToastIntervalNs, and only to another whole
/// percent. A value that changed and then stopped is shown by the next call after the interval.
struct WorldScaleToastGate {
    long shown = -1;
    int64_t last_ns = 0;
    bool Due(long whole, int64_t now_ns, bool force = false) {
        if (whole == shown || (!force && shown >= 0 && now_ns - last_ns < kWorldToastIntervalNs)) {
            return false;
        }
        shown = whole;
        last_ns = now_ns;
        return true;
    }
    void Reset() {
        shown = -1;
        last_ns = 0;
    }
};
/// "45%", "100%" (as rounded).
inline std::string WorldScaleText(float percent) {
    return std::to_string(WorldScaleWhole(percent)) + "%";
}
/// "World Scale: 45%", "World Scale: 100%" (the grip's toast and the menu's).
inline std::string WorldScaleToast(float percent) {
    return std::string(kWorldScale) + ": " + WorldScaleText(percent);
}

/// 1.2.0 fix lane B (session 651, triage item 5), all unapproved: the toast of grip + stick
/// sideways, which resizes the Full modes' screen. It said "Follow Screen Size: On" on release,
/// which read like a toggle; it now says what happened: the size (Layout::Placement::size, 100 %
/// the world at its own scale), "World Scale Follows" on release when Follow Screen Size kept the
/// world matched to the new size, and "May Feel Locked" while the head-to-camera gain is under
/// Layout::kLockedGain (the screen bigger than the game's view: a head turn barely turns the
/// camera, so the picture seems to follow the head).
inline constexpr const char* kScreenSize = "Screen Size";
inline constexpr const char* kMayFeelLocked = "May Feel Locked";
/// 1.2.0 fix lane B (triage item 6), unapproved: both grips + D-pad. A scale at its end says so
/// ("Vertical Scale: +100% (Max)", Layout::ScaleToast), and a press while no grip holds the
/// screen (the chord latched after the holding grip let go, or the screen paused) says what to
/// do instead of nothing.
inline constexpr const char* kAtMax = "Max";
inline constexpr const char* kAtMin = "Min";
inline constexpr const char* kHoldScreenFirst = "Hold the Screen First";
/// AzaharFrame aa6dof2, unapproved: Angel's Aim keeps its screen at true size (Layout::GripOnScreen),
/// so a grip there holds nothing and the D-pad under it changes nothing: this toast says so
/// instead (once a grab for the stick under a grip, and on each such D-pad press).
inline constexpr const char* kAngelsAimScreenFixed = "Angel's Aim: Screen Size Fixed";
/// The separator of a toast's parts (as "Motion Clarity: Experimental \u00b7 May Flicker").
inline constexpr const char* kToastPartSeparator = " \u00b7 ";
/// The whole percent ScreenSizeToast shows for a size.
inline long ScreenSizeWhole(float size) {
    return std::isfinite(size) ? std::lround(size * 100.0f) : 0;
}
/// "Screen Size: 137%", "Screen Size: 137% \u00b7 World Scale Follows",
/// "Screen Size: 137% \u00b7 World Scale Follows \u00b7 May Feel Locked".
inline std::string ScreenSizeToast(float size, bool follows, bool locked);

/// Settings::values.vr_immersive_follow_screen.
inline constexpr const char* kFollowScreen = "Follow Screen Size";
inline constexpr const char* kFollowScreenTip =
    "When you resize the screen, the world grows or shrinks with it.";
inline constexpr const char* kFollowScreenWindowTip =
    "When you resize the screen in the Full modes, the world grows or shrinks with it. Off keeps "
    "the world's size.";
/// AzaharFrame flash: Motion Clarity (frame_xr/motion_clarity.h; its name in the code, the keys,
/// the switches and the log). AzaharFrame fixa: shown as "Black Frame Insertion" (the owner's
/// rename, 2026-11-15), the label of both VR menu rows: the Immersive section's
/// (Settings::values.vr_motion_clarity_immersive, the Full modes) and the Graphics section's
/// (vr_motion_clarity_normal, the Off and Wide modes).
inline constexpr const char* kMotionClarity = "Black Frame Insertion";
/// AzaharFrame projdef: experimental (the owner's A/B, 1.1.1-590); its tooltip says so. fixa: each
/// row's tooltip says where it works.
/// AzaharFrame bfidr: each tooltip ends with the rate it needs (the owner's wording, 2026-11-17).
inline constexpr const char* kMotionClarityTip =
    "Experimental. In the Full modes, adds a black frame after each picture for a clearer view "
    "on head turns. Uses Primary Screen as Panel. May flicker. Needs a 120 Hz display.";
/// rel120: Azahar's window keeps the still true line about slower games.
inline constexpr const char* kMotionClarityWindowTip =
    "Experimental. In the Full modes, adds a black frame after each picture for a clearer view "
    "on head turns. Uses Primary Screen as Panel. May flicker. Games that run under 60 fps show "
    "as before. Needs a 120 Hz display.";
/// AzaharFrame fixa: the Graphics section's row (vr_motion_clarity_normal): only the top screen
/// flashes, while it is the Primary one.
inline constexpr const char* kMotionClarityNormalTip =
    "Experimental. In the Off and Wide modes, adds a black frame after each picture on the "
    "Primary screen for clearer motion. May flicker. Needs a 120 Hz display.";
inline constexpr const char* kMotionClarityNormalWindowTip =
    "Experimental. In the Off and Wide modes, adds a black frame after each picture on the "
    "Primary screen for clearer motion. May flicker. Games that run under 60 fps show as before. "
    "Needs a 120 Hz display.";
/// AzaharFrame bfidr: the toast when turning Black Frame Insertion on turned its helpers on
/// (frame_xr/bfi_helpers.h); it takes the place of "May Flicker" then (one toast at a time).
/// Putting them back when it goes off shows nothing (the rows show it). The owner's wording,
/// 2026-11-18.
inline std::string BfiHelpersToast(bool dynamic_resolution, bool panel, bool on) {
    std::string what;
    if (!on) {
        return {};
    }
    if (dynamic_resolution && panel) {
        what = "Dynamic Resolution and Primary Screen as Panel";
    } else if (dynamic_resolution) {
        what = "Dynamic Resolution";
    } else if (panel) {
        what = "Primary Screen as Panel";
    } else {
        return {};
    }
    return what + " On for Black Frame Insertion";
}
/// AzaharFrame bfidr: the note on both Black Frame Insertion rows (VR menu) and boxes (Azahar's
/// window) while the display runs at a rate it does not flash at (MotionClarity::EvenRate): a
/// note, never a lock. The owner's wording, 2026-11-17.
inline std::string NeedsRefreshNote(int display_hz) {
    return "Needs 120 Hz (Display at " + std::to_string(display_hz) + " Hz)";
}
/// AzaharFrame projdef: the chip beside Motion Clarity's label in the VR menu.
inline constexpr const char* kExperimental = "Experimental";
/// AzaharFrame projdef: Azahar's window (Configure) and Send Report name the two rows apart; the
/// toast each time either row is turned on. fixa: the VR menu's "In Immersive Modes" row is gone
/// (the Immersive section's row is that key).
inline constexpr const char* kMotionClarityImmersiveWindow =
    "Black Frame Insertion in Immersive Modes";
inline constexpr const char* kMotionClarityNormalWindow =
    "Black Frame Insertion in Off and Wide Modes";
/// fixa: "Black Frame Insertion: Experimental \u00b7 May Flicker" is wider than the toast's one line
/// (958 of 944 px), so "Experimental" goes: the rows' chip says it.
inline constexpr const char* kMotionClarityImmersiveToast = "Black Frame Insertion: May Flicker";
/// AzaharFrame fixa: Settings::values.vr_motion_clarity_match_bottom: the row under each Black
/// Frame Insertion row while that row is on (VR menu), its Configure box, its Send Report line.
/// It names the 3DS's bottom screen: only the top screen flashes (the owner's exception to
/// Primary/Secondary, 2026-11-15).
inline constexpr const char* kMatchBottom = "Match Bottom Screen Brightness";
inline constexpr const char* kMatchBottomTip =
    "Black Frame Insertion adds a black frame after each picture, which dims it. This dims the "
    "bottom screen to match.";
/// AzaharFrame projdef: Settings::values.vr_screens_in_projection, as its opposite: the Primary
/// screen drawn as a panel (a quad layer) in the Full modes. The VR menu's row (with the
/// Experimental chip), its tooltip, its note while Motion Clarity takes the panel, its toasts;
/// the Configure box uses the label and tooltip.
inline constexpr const char* kScreenAsPanel = "Primary Screen as Panel";
inline constexpr const char* kScreenAsPanelTip =
    "Experimental. Draws the Primary screen as a separate panel in the Full modes. It may double "
    "on fast head turns.";
inline constexpr const char* kScreenAsPanelNote = "Used by Black Frame Insertion";
inline constexpr const char* kScreenAsPanelOnToast = "Primary Screen as Panel: May Double";
inline constexpr const char* kScreenAsPanelOffToast = "Primary Screen as Panel: Off";

/// The toast's part after resizing the screen with Follow Screen Size on (ScreenSizeToast; 1.2.0
/// fix lane B, unapproved: it was the whole toast, "Follow Screen Size: On").
inline constexpr const char* kWorldScaleFollowsToast = "World Scale Follows";
inline std::string ScreenSizeToast(float size, bool follows, bool locked) {
    std::string text =
        std::string(kScreenSize) + ": " + std::to_string(ScreenSizeWhole(size)) + "%";
    if (follows) {
        text += std::string(kToastPartSeparator) + kWorldScaleFollowsToast;
    }
    if (locked) {
        text += std::string(kToastPartSeparator) + kMayFeelLocked;
    }
    return text;
}

/// AzaharFrame integ8: Azahar's window (Graphics, Enhancements: Depth, Settings::values.factor_3d),
/// global or a game's properties. AzaharFrame natural: the Full modes use Immersive 3D Depth.
inline constexpr const char* kDepth3dWindowTip =
    "How deep the 3D looks. The Full immersive modes use Immersive 3D Depth instead.";

/// AzaharFrame natural: the toast when the 3D Factor shortcuts change the Full modes' 3D Depth:
/// "3D Depth: 45%".
inline std::string DepthToast(unsigned percent) {
    return std::string(kDepth3d) + ": " + std::to_string(percent > 100 ? 100 : percent) + "%";
}

// AzaharFrame kiumode: Immersive Mode's choices (Settings::values.vr_immersive_mode). Stored 0 to
// 4; the list shows them in this order in every game and in Global (AzaharFrame kiurows: it was
// Kid Icarus: Uprising's alone), Angel's Aim (stored 4) above Other Games (stored 3, which keeps
// its value for every game, preset and older build). Outside Kid Icarus: Uprising a stored 4
// plays as Other Games (Settings::ImmersiveRenderModeOf). The VR menu and Azahar's window both
// use these.
inline constexpr const char* kModeOff = "Off";
inline constexpr const char* kModeWide = "Wide (3DoF)";
inline constexpr const char* kModeZelda = "Full (6DoF): Ocarina of Time, Majora's Mask";
/// Kid Icarus: Uprising's first person. Approved by the owner (2026-11-18).
inline constexpr const char* kModeAngelsAim = "Full (6DoF): Kid Icarus: Uprising / Angel's Aim";
inline constexpr const char* kModeOtherGames = "Full (6DoF): Other Games";
inline constexpr std::uint32_t kModeStoredAngelsAim = 4;
/// The choices in the shown order.
inline constexpr std::array<const char*, 5> kModeChoices{kModeOff, kModeWide, kModeZelda,
                                                         kModeAngelsAim, kModeOtherGames};
inline constexpr int kModeChoiceCount = 5;
/// The shown index of a stored mode: a value past the list shows as its last choice.
constexpr int ModeIndexOf(std::uint32_t stored) {
    if (stored == kModeStoredAngelsAim) { // the fourth choice
        return 3;
    }
    if (stored >= 3) { // Other Games (and any value past the list)
        return 4;
    }
    return static_cast<int>(stored);
}
/// The stored mode of a shown index.
constexpr std::uint32_t ModeValueAt(int index) {
    const int i = std::clamp(index, 0, kModeChoiceCount - 1);
    if (i >= 3) {
        return i == 3 ? kModeStoredAngelsAim : 3u;
    }
    return static_cast<std::uint32_t>(i);
}
/// The stored mode of a shown index, keeping `before` when that index shows it (a stored 7 shown
/// as Other Games stays 7 until another choice is made).
constexpr std::uint32_t ModeValueAt(int index, std::uint32_t before) {
    return ModeIndexOf(before) == index ? before : ModeValueAt(index);
}

} // namespace FrameXr::Immersive::Names
