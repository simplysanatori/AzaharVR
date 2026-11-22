// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiufix: the Mods tab's Angel's Aim segment (Cheats | Mods | Textures | Angel's
// Aim), shown only while Kid Icarus: Uprising (any region) is open; built like the CTGP-7 segment
// (ctgp7_tab.h). Material 3, one page that scrolls as a whole:
//
//   About Angel's Aim  [Alpha]                                       (title, an assist chip)
//   Alpha Pre-Release · Very Experimental · Have Fun!                (the owner's words)
//   Angel's Aim brings the game into VR: ...                         (short paragraphs)
//   [↗ Report an Issue]
//   Bug reports and feature ideas welcome.
//   ─────────────────────────────────────────────────────────────
//   Settings                                    [Global | Per-Game] ⋮  (AzaharFrame kiu132)
//   Aiming / View / Bottom Screen / Weapon        (group titles; each Angel's Aim option, the
//                                                  same Id as its other place: AngelsAimGroups)
//   Mods & Pack                                                      (group title)
//   Required for Single-Screen Modes                                 (title)
//   Not used in the Angel's Aim immersive mode: ...                  (kiu132 note)
//   Darkshade's C-Stick & Gyro Mod                     Installed / Not Installed
//   (the catalog's C-Stick & Gyro rows, as Install Mods draws them: Install, its versions)
//   ─────────────────────────────────────────────────────────────
//   Suggested                                                        (title)
//   Kid Icarus Uprising Weapons HD                     Installed / Not Installed
//   (the catalog's weapons and effects texture pack: installed and on in the game's packs)
//
// The values come from the Qt side (citra_qt/frame_mods.cpp, FrameXr::SetInGameMenuAngelsAim).
// Report an Issue asks "Open GitHub?" first, then sends OpenUrl with `report_url`: the app's
// GitHub issues page for a new issue, prefilled for Angel's Aim with the app's version, the
// game's region and version, and whether the mod is installed (nothing personal).

#pragma once

#include <string>
#include <vector>

#include "frame_xr/menu/mods_segments.h"

namespace FrameXr::Menu {

struct AngelsAimModel {
    bool shown = false;         ///< Kid Icarus: Uprising is open
    bool recommended = false;   ///< the badge: Angel's Aim is mapped for this copy (Europe only)
    /// AzaharFrame kiu132: Angel's Aim's first person works in this copy (the European game,
    /// FrameXr::Kiu::IsAngelsAimTitle): its rows show only then
    bool mapped = false;
    bool mod_installed = false; ///< a C-Stick & Gyro mod (FrameXr::CStickModInstalled)
    bool textures_pack = false; ///< the catalog's weapons and effects pack is on (kAngelsAimPackIds)
    std::string report_url;     ///< Report an Issue's page ("" none)
    bool operator==(const AngelsAimModel&) const = default;
};

/// The segment's strings (the owner approves them: handoff/reports/52/kiufix).
inline constexpr const char* kAngelsAimSegment = "Angel's Aim";
inline constexpr const char* kAngelsAimTitle = "About Angel's Aim";
inline constexpr const char* kAngelsAimAlpha = "Alpha";
inline constexpr const char* kAngelsAimStatus = "Alpha Pre-Release \xC2\xB7 Very Experimental \xC2\xB7 Have Fun!";
inline constexpr const char* kAngelsAimAbout1 =
    "Angel's Aim brings Kid Icarus: Uprising into VR. Aim with your controller: Direct Aim puts "
    "the reticle where you point, Gyro Aim moves it as you turn your hand. Your equipped weapon "
    "is in your hand, and each real shot buzzes.";
inline constexpr const char* kAngelsAimAbout2 =
    "It's the first step of a bigger project: full 6DOF aiming.";
inline constexpr const char* kAngelsAimReportLine = "Bug reports and feature ideas welcome.";
/// AzaharFrame kiu132: "Required" until 1.3.0. PROPOSED strings (kiu132): they need the owner's
/// approval. The C-Stick mods are needed for the single-screen modes and are left out of a run
/// in Angel's Aim's immersive mode (FrameXr::Kiu::FirstPerson::LeaveOutCStickModFor).
inline constexpr const char* kAngelsAimRequired = "Required for Single-Screen Modes";
inline constexpr const char* kAngelsAimModNote =
    "Not used in the Angel's Aim immersive mode: it's left out when the game starts in it.";
/// The toasts: the mod was left out of this run; the mode was chosen while the mod is loaded.
inline constexpr const char* kCStickModLeftOutToast = "Angel's Aim: C-Stick Mod Not Loaded";
inline constexpr const char* kCStickModRestartToast =
    "C-Stick Mod Loaded: Restart the Game for Angel's Aim";
/// AzaharFrame kiu132: the settings' group titles (PROPOSED). "Bottom Screen" is the owner's.
inline constexpr const char* kAngelsAimGroupAiming = "Aiming";
inline constexpr const char* kAngelsAimGroupView = "View";
inline constexpr const char* kAngelsAimGroupBottomScreen = "Bottom Screen";
inline constexpr const char* kAngelsAimGroupWeapon = "Weapon";
inline constexpr const char* kAngelsAimGroupModsAndPack = "Mods & Pack";
inline constexpr const char* kAngelsAimModName = "Darkshade's C-Stick & Gyro Mod";
inline constexpr const char* kAngelsAimSuggested = "Suggested";
/// One row for the weapons and effects textures, named as the catalog names the pack.
inline constexpr const char* kAngelsAimTexturesPack = "Kid Icarus Uprising Weapons HD";
inline constexpr const char* kAngelsAimSettings = "Settings";
inline constexpr const char* kAngelsAimInstalled = "Installed";
inline constexpr const char* kAngelsAimNotInstalled = "Not Installed";
/// Send Report's dialog (citra_qt/frame_crash_dialog.cpp, in Azahar's window with the VR keyboard).
inline constexpr const char* kAngelsAimSendTitle = "Send Report";
inline constexpr const char* kAngelsAimBug = "Bug";
inline constexpr const char* kAngelsAimIdea = "Idea";
inline constexpr const char* kAngelsAimBugNote = "What Happened? (Optional)";
inline constexpr const char* kAngelsAimIdeaNote = "What's Your Idea? (Optional)";
inline constexpr const char* kAngelsAimConsent =
    "Sends this report and your Angel's Aim settings to the developer.";
inline constexpr const char* kAngelsAimSending = "Sending...";
inline constexpr const char* kAngelsAimSent = "Report Sent";
/// AzaharFrame kiuweap2: the prompt when Aiming is turned on (Off to Direct Aim or Gyro Aim) in
/// the VR menu, once (vr_angels_aim_intro_shown): what Angel's Aim has on, and where to change it
/// (tab_menu.h AngelsIntroDialog). Approved by the owner (1.3.0).
inline constexpr const char* kAngelsIntroTitle = "Angel's Aim Is On";
inline constexpr const char* kAngelsIntroBody =
    "Here's what's on. Change any of it under Aiming.";
inline constexpr const char* kAngelsIntroControls = "Show Aiming";
inline constexpr const char* kAngelsIntroOk = "OK";
/// AzaharFrame kiufire: Trigger Layout (the Controls tab's Aiming rows, Kid Icarus only, with
/// Aiming on; vr_kiu_trigger_layout). Approved by the owner (1.3.0), the tips too.
inline constexpr const char* kTriggerLayoutRow = "Trigger Layout";
inline constexpr const char* kTriggerLayoutFireAndPower = "Fire and Power";
inline constexpr const char* kTriggerLayoutOriginal = "Original";
inline constexpr const char* kTriggerLayoutFireAndPowerTip =
    "Your aiming hand's trigger only fires. The other trigger uses your power, and the bumpers "
    "pick it.";
inline constexpr const char* kTriggerLayoutOriginalTip =
    "Both triggers fire and also press the game's own buttons, as before.";
/// AzaharFrame kiutoggles: Angel's Aim's first-person options, once test switches only (the
/// Settings tab's Immersive section, under Immersive Mode, while Kid Icarus: Uprising is open and
/// Immersive Mode is Angel's Aim; vr_kiu_wrist_screen, vr_kiu_talk_panel, vr_kiu_over_shoulder,
/// vr_kiu_snap_turn). Approved by the owner (1.3.0), the Turning choices too. All of them are here.
/// AzaharFrame kiu131: Bottom Screen (replaces Secondary Screen on Wrist and Talk Panel), its
/// size, opacity and place, Reticle Size, and Wrist Side (the Stats tab, every wrist display).
/// "Bottom Screen" is the owner's own name (a hardware-bound exception, as Bottom Screen
/// Reticle). PROPOSED strings: they need the owner's approval.
inline constexpr const char* kBottomScreenRow = "Bottom Screen";
inline constexpr const char* kBottomScreenOff = "Off";
inline constexpr const char* kBottomScreenPersistent = "Persistent";
inline constexpr const char* kBottomScreenWrist = "Wrist";
inline constexpr const char* kBottomScreenTip =
    "Persistent: always in view, below it; grab it with a grip to move it. Wrist: on your free "
    "hand's wrist; look at it like a watch, and point at it to touch.";
inline constexpr const char* kBottomScreenSizeRow = "Bottom Screen Size";
inline constexpr const char* kBottomScreenSizeTip = "How big the Bottom Screen is, in view or on your wrist.";
inline constexpr const char* kBottomScreenOpacityRow = "Bottom Screen Opacity";
inline constexpr const char* kBottomScreenOpacityTip =
    "How solid the Bottom Screen looks. Lower lets you see through it.";
inline constexpr const char* kBottomScreenResetRow = "Reset Bottom Screen Position";
inline constexpr const char* kBottomScreenResetTip = "Puts the Bottom Screen back below your view.";
inline constexpr const char* kReticleSizeRow = "Reticle Size";
inline constexpr const char* kReticleSizeTip = "How big the game's reticle is.";
inline constexpr const char* kWristSideRow = "Wrist Side";
inline constexpr const char* kWristSideBack = "Back";
inline constexpr const char* kWristSideInside = "Inside";
inline constexpr const char* kWristSideTip =
    "Where wrist displays sit: on the back of your wrist like a watch, or on the inside.";
inline constexpr const char* kOverShoulderRow = "Over the Shoulder";
inline constexpr const char* kOverShoulderTip =
    "Moves your view behind and above Pit, so you can see him.";
inline constexpr const char* kTurningRow = "Turning";
inline constexpr const char* kTurningSmooth = "Smooth";
/// Turning's snap choices after Smooth (Menu::kSnapTurnDegrees' 30, 45, 60, 90).
inline constexpr const char* kTurningSnaps[] = {"30\xC2\xB0", "45\xC2\xB0", "60\xC2\xB0",
                                                "90\xC2\xB0"};
inline constexpr const char* kTurningTip =
    "How the right stick turns you: smoothly, or in steps of the chosen angle.";
inline constexpr const char* kAngelsAimNotSent = "Couldn't Send Report";
inline constexpr const char* kAngelsAimNotSentHint = "Try Again, or Use Report an Issue.";
/// Report an Issue's page: a new issue, titled and labelled for Angel's Aim (GitHub ignores a
/// label the repository does not have). The body is added by the Qt side (AngelsAimReportUrl).
inline constexpr const char* kAngelsAimIssuesUrl =
    "https://github.com/simplysanatori/AzaharFrame/issues/new?title=%5BAngel%27s%20Aim%5D%20&labels=Angel%27s%20Aim";

/// The page with a prefilled body (percent-encoded; kept short, as the menu's links are at most
/// 300 characters): the app's version, the game's region and version, the mod's state.
std::string AngelsAimReportUrl(const std::string& app_version, const std::string& game,
                               bool mod_installed);
/// A URL is Report an Issue's page (its prefix): the "Open GitHub?" dialog names github.com.
bool IsAngelsAimReportUrl(const std::string& url);

/// The C-Stick mods' catalogue ids (FrameXr::kCStickModIds, controls_presets.h; the menu library
/// does not link the emulator's: a unit test keeps the two the same).
inline constexpr const char* kAngelsAimModIds[] = {"kiu-cstick-gyro",
                                                   "kiu-cstick-gyro-widescreen-16-9",
                                                   "kiu-cstick-gyro-ultrawide-21-9",
                                                   "kiu-cstick-aim"};
/// The texture pack's catalogue ids (core/custom_tex: the 3ds-game-fixes texture catalog). The row
/// says Installed when a pack with one of them is on for the game, whatever form it is kept in
/// (Optimize for Frame's ASTC copy too).
inline constexpr const char* kAngelsAimPackIds[] = {"kiu-weapons-hd-4x"};
/// The catalog rows (indices of `available`) the Required section lists: the C-Stick mods.
std::vector<int> AngelsAimModRows(const ModsListModel& m);

} // namespace FrameXr::Menu
