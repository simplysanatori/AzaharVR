// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiufix: the Mods tab's Angel's Aim segment (Cheats | Mods | Textures | Angel's
// Aim), shown only while Kid Icarus: Uprising (any region) is open. Material 3, a page that never
// scrolls and a page a group (AimGroup), each with its More:
//
//   Angel's Aim  [Alpha]                        [Global | Per-Game] ⋮
//   Immersive Mode                              [its menu button]
//   Aiming / View / HUD / Bottom Screen / Weapon     (their rows: AimPlaces)
//   Mods & Pack      Required for Single-Screen Modes (the catalog's C-Stick & Gyro rows, as
//                    Install Mods draws them), Suggested (the weapons and effects texture pack)
//   About Angel's Aim      the owner's words, Send Report, Report an Issue
//
// AzaharFrame kiuaim: the only place of the rows that act in Kid Icarus: Uprising alone. Aiming, Gyro
// Sensitivity, Recoil and its two, Laser Sight, Keep Reticle on Screen and Grip Frees Aim act in any
// game: they are on the Controls tab too.
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
    std::string hand_zip;       ///< AzaharFrame kiu140: a hand model's zip in Downloads, or ""
    bool hand_installed = false; ///< AzaharFrame kiu140: a hand model for Pit's arm is there
    std::string report_url;     ///< Report an Issue's page ("" none)
    bool operator==(const AngelsAimModel&) const = default;
};

/// The segment's strings (the owner approves them: handoff/reports/52/kiufix).
inline constexpr const char* kAngelsAimSegment = "Angel's Aim";
inline constexpr const char* kAngelsAimTitle = "About Angel's Aim";
inline constexpr const char* kAngelsAimAlpha = "Alpha";
inline constexpr const char* kAngelsAimStatus = "Alpha Pre-Release \xC2\xB7 Very Experimental \xC2\xB7 Have Fun!";
inline constexpr const char* kAngelsAimAbout1 =
    "Kid Icarus: Uprising in first-person VR: full 6DoF, with weapon mesh models. Or play in "
    "widescreen with Darkshade's C-Stick & Gyro mod.";
inline constexpr const char* kAngelsAimAbout2 =
    "First step toward Uprising VR.";
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
/// AzaharFrame kiu132: the groups' names (PROPOSED). "Bottom Screen" is the owner's.
inline constexpr const char* kAngelsAimGroupAiming = "Aiming";
inline constexpr const char* kAngelsAimGroupView = "View";
inline constexpr const char* kAngelsAimGroupBottomScreen = "Bottom Screen";
inline constexpr const char* kAngelsAimGroupWeapon = "Weapon";
inline constexpr const char* kAngelsAimGroupModsAndPack = "Mods & Pack";
inline constexpr const char* kAngelsAimModName = "Darkshade's C-Stick & Gyro Mod";
inline constexpr const char* kAngelsAimSuggested = "Suggested";
/// One row for the weapons and effects textures, named as the catalog names the pack.
inline constexpr const char* kAngelsAimTexturesPack = "Kid Icarus: Uprising HD";
/// AzaharFrame kiu140: the row that takes a hand model from a zip, and its toast (approved
/// 2026-11-24).
inline constexpr const char* kAngelsAimHandModel = "Pit's Hand Model";
/// With such a zip in Downloads and no hand yet (PROPOSED): its state, and Install in place of
/// Install from Zip (Id::InstallFromZip's index).
inline constexpr const char* kAngelsAimHandFound = "Found in Downloads";
inline constexpr const char* kAngelsAimHandInstall = "Install";
inline constexpr int kAngelsAimHandFoundIndex = 100;
inline constexpr const char* kAngelsAimHandToast = "Hand Model Installed";
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
    "Here's what's on. Change any of it in Angel's Aim.";
inline constexpr const char* kAngelsIntroControls = "Show Angel's Aim";
inline constexpr const char* kAngelsIntroOk = "OK";
/// AzaharFrame kiufire: Trigger Layout (the page's Aiming group, with Aiming on;
/// vr_kiu_trigger_layout). Approved by the owner (1.3.0), the tips too.
inline constexpr const char* kTriggerLayoutRow = "Trigger Layout";
inline constexpr const char* kTriggerLayoutFireAndPower = "Fire and Power";
inline constexpr const char* kTriggerLayoutOriginal = "Original";
inline constexpr const char* kTriggerLayoutFireAndPowerTip =
    "Your aiming hand's trigger only fires. The other trigger uses your power, and the bumpers "
    "pick it.";
inline constexpr const char* kTriggerLayoutOriginalTip =
    "Both triggers fire and also press the game's own buttons.";
/// AzaharFrame kiutoggles: Angel's Aim's first-person options, once test switches only (while Kid
/// Icarus: Uprising is open and Immersive Mode is Angel's Aim; vr_kiu_wrist_screen,
/// vr_kiu_talk_panel, vr_kiu_over_shoulder, vr_kiu_snap_turn). Approved by the owner (1.3.0), the Turning choices too. All of them are here.
/// AzaharFrame kiu131: Bottom Screen (replaces Secondary Screen on Wrist and Talk Panel), its
/// size, opacity and place, Reticle Size, and Wrist Side (the Stats tab, every wrist display).
/// "Bottom Screen" is the owner's own name (a hardware-bound exception, as Bottom Screen
/// Reticle). PROPOSED strings: they need the owner's approval.
inline constexpr const char* kBottomScreenRow = "Bottom Screen";
inline constexpr const char* kBottomScreenOff = "Off";
inline constexpr const char* kBottomScreenPersistent = "In View";
inline constexpr const char* kBottomScreenWrist = "Wrist";
inline constexpr const char* kBottomScreenTip =
    "In View: low in front of you; hold a grip on it to move it. Wrist: on "
    "your free hand's wrist; turn your wrist to see it, and point at it to touch.";
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
    "Where wrist displays sit: on the back of your wrist, or on the inside.";
inline constexpr const char* kKiuWristAngleRow = "Wrist Angle"; // AzaharFrame kiu140 PROPOSED
inline constexpr const char* kKiuWristAngleTip =
    "Where round your wrist the Bottom Screen sits. 0\u00B0 is the back of your wrist, 180\u00B0 "
    "the inside.";
inline constexpr const char* kKiuWristAlongRow = "Arm Position"; // AzaharFrame kiu140 PROPOSED
inline constexpr const char* kKiuWristAlongTip =
    "How far up your arm the Bottom Screen sits. Below 0 is toward your hand.";
inline constexpr const char* kKiuCutscenesRow = "Cutscenes"; // AzaharFrame kiu140 PROPOSED
inline constexpr const char* kKiuCutscenesTip =
    "In View: cutscenes play around you, in third person. Primary Screen: on the Primary Screen.";
inline constexpr const char* kKiuHudRow = "HUD"; // AzaharFrame kiu140 PROPOSED
inline constexpr const char* kKiuHudTip =
    "In View: the health bar, the score and the rest sit inside your view. Game's Own: where the game puts them.";
inline constexpr const char* kKiuHudSizeRow = "HUD Size"; // AzaharFrame kiu140 PROPOSED
inline constexpr const char* kKiuHudSizeTip =
    "How big the HUD is.";
inline constexpr const char* kKiuArmRow = "Pit's Arm"; // AzaharFrame kiu140 PROPOSED
inline constexpr const char* kKiuArmTip =
    "Your free hand is Pit's arm instead of the controller.";
inline constexpr const char* kKiuHudLinkedRow = "Health and Score"; // AzaharFrame kiu140 PROPOSED
inline constexpr const char* kKiuHudLinkedTip =
    "Linked: side by side, moved as one. Separate: each has its own place.";
inline constexpr const char* kKiuHudAcrossRow = "HUD Left/Right"; // AzaharFrame kiu140 PROPOSED
inline constexpr const char* kKiuHudAcrossTip =
    "How far left or right the health bar and score are. 0\u00B0 is straight ahead.";
inline constexpr const char* kKiuHudHeightRow = "HUD Height"; // AzaharFrame kiu140 PROPOSED
inline constexpr const char* kKiuHudHeightTip =
    "How high the health bar and score are. 0\u00B0 is straight ahead.";
inline constexpr const char* kKiuBarAcrossRow = "Health Bar Left/Right"; // AzaharFrame kiu140 PROPOSED
inline constexpr const char* kKiuBarAcrossTip =
    "How far left or right the health bar is. 0\u00B0 is straight ahead.";
inline constexpr const char* kKiuBarHeightRow = "Health Bar Height"; // AzaharFrame kiu140 PROPOSED
inline constexpr const char* kKiuBarHeightTip =
    "How high the health bar is. 0\u00B0 is straight ahead.";
inline constexpr const char* kKiuScoreAcrossRow = "Score Left/Right"; // AzaharFrame kiu140 PROPOSED
inline constexpr const char* kKiuScoreAcrossTip =
    "How far left or right the score is. 0\u00B0 is straight ahead.";
inline constexpr const char* kKiuScoreHeightRow = "Score Height"; // AzaharFrame kiu140 PROPOSED
inline constexpr const char* kKiuScoreHeightTip =
    "How high the score is. 0\u00B0 is straight ahead.";
inline constexpr const char* kKiuSubtitleHeightRow = "Subtitle Height"; // AzaharFrame kiu140 PROPOSED
inline constexpr const char* kKiuSubtitleHeightTip =
    "How high the subtitles are. 0\u00B0 is straight ahead.";
inline constexpr const char* kKiuSubtitleSizeRow = "Subtitle Size"; // AzaharFrame kiu140 PROPOSED
inline constexpr const char* kKiuSubtitleSizeTip =
    "How big the subtitles are.";
inline constexpr const char* kKiuHideOffHandRow = "Free Hand"; // AzaharFrame kiuhands (the owner's choice)
inline constexpr const char* kKiuHideOffHandTip =
    "Your free hand isn't drawn when Hidden. The Bottom Screen on your wrist stays.";
/// AzaharFrame kiuaim: Character Portraits (vr_kiu_talk_portraits, vr_kiu_talk_portraits_opacity:
/// the single screen lane's settings). PROPOSED strings, the tips too.
inline constexpr const char* kKiuTalkPortraitsRow = "Character Portraits";
inline constexpr const char* kKiuTalkPortraitsTip =
    "Shows the characters when they talk.";
inline constexpr const char* kKiuTalkPortraitsOpacityRow = "Character Portraits Opacity";
inline constexpr const char* kKiuTalkPortraitsOpacityTip =
    "How solid the characters look. Lower lets you see through them.";
/// Asked for by the owner (2026-11-25); the strings PROPOSED.
inline constexpr const char* kKiuPortraitsLinkedRow = "Portraits and Subtitles";
inline constexpr const char* kKiuPortraitsLinkedTip =
    "Linked: moved as one. Separate: each has its own place.";
inline constexpr const char* kKiuPortraitsAcrossRow = "Character Portraits Left/Right";
inline constexpr const char* kKiuPortraitsAcrossTip =
    "How far left or right the characters are. 0\u00B0 is their usual place.";
inline constexpr const char* kKiuPortraitsHeightRow = "Character Portraits Height";
inline constexpr const char* kKiuPortraitsHeightTip =
    "How high the characters are. 0\u00B0 is their usual place.";
/// AzaharFrame kiuaim: Power Menu (vr_kiu_power_menu, _across, _height, _opacity: the power lane's
/// settings). The row's tip is approved; the other tips PROPOSED.
inline constexpr const char* kKiuPowerMenuRow = "Power Menu";
inline constexpr const char* kKiuPowerMenuOff = "Off";
inline constexpr const char* kKiuPowerMenuOffHand = "Off Hand";
inline constexpr const char* kKiuPowerMenuHealthBar = "Health Bar";
inline constexpr const char* kKiuPowerMenuMiddle = "Middle";
inline constexpr const char* kKiuPowerMenuScore = "Score";
inline constexpr const char* kKiuPowerMenuTip =
    "Off Hand: when you pick one. Elsewhere: always in battles.";
inline constexpr const char* kKiuPowerMenuAcrossRow = "Power Menu Left/Right";
inline constexpr const char* kKiuPowerMenuAcrossTip =
    "How far left or right the power menu is. 0\u00B0 is its usual place.";
inline constexpr const char* kKiuPowerMenuHeightRow = "Power Menu Height";
inline constexpr const char* kKiuPowerMenuHeightTip =
    "How high the power menu is. 0\u00B0 is its usual place.";
inline constexpr const char* kKiuPowerMenuOpacityRow = "Power Menu Opacity";
inline constexpr const char* kKiuPowerMenuOpacityTip =
    "How solid the power menu looks. Lower lets you see through it.";
/// AzaharFrame kiucam: R3 (vr_kiu_stick_click). The row and its choices are the
/// owner's; the tip PROPOSED.
inline constexpr const char* kKiuStickClickRow = "R3";
inline constexpr const char* kKiuStickClickCamera = "Switch Camera";
inline constexpr const char* kKiuStickClickSwap = "Swap Screens";
inline constexpr const char* kKiuStickClickTip =
    "In battles, R3 switches First Person and Third Person.";
/// AzaharFrame kiu60: 60 FPS (vr_kiu_60fps), under View's More. PROPOSED strings, the toast too
/// (the setting changed while the game runs: it counts from the next start).
inline constexpr const char* kKiu60FpsRow = "60 FPS";
inline constexpr const char* kKiu60FpsTip =
    "Runs the game at 60 FPS. Needs more power. Restart the game to change it.";
inline constexpr const char* kKiu60FpsOnToast = "Restart the Game for 60 FPS";
inline constexpr const char* kKiu60FpsOffToast = "Restart the Game for 30 FPS";
inline constexpr const char* kOverShoulderRow = "Third Person";
inline constexpr const char* kOverShoulderTip =
    "Moves your view behind and above Pit, so you can see him.";
/// AzaharFrame kiu133: Third Person became one of Camera's choices (the row's old name and
/// tip, approved, are its choice name), with First Person and the Game's Camera; Pit Opacity for
/// the two views from outside Pit. PROPOSED strings: they need the owner's approval.
inline constexpr const char* kCameraRow = "Camera";
/// AzaharFrame 1.3.0: the Game's Camera is not offered yet (its view is broken on the headset):
/// Camera lists First Person and Third Person only, and a saved vr_kiu_game_camera is not
/// used. AZAHARFRAME_KIU_GAME_CAMERA=1 still turns it on, for work on it.
inline constexpr bool kGameCameraOffered = false;
/// AzaharFrame kiu140: Pit's Arm is not offered yet (the owner, 2026-11-24): no row, no hand model
/// row, the controller on the free hand. AZAHARFRAME_KIU_PIT_ARM=1 draws it, for work on it.
inline constexpr bool kPitArmOffered = false;
inline constexpr const char* kCameraFirstPerson = "First Person";
inline constexpr const char* kCameraGame = "Game's Camera";
inline constexpr const char* kCameraTip = "Where you see from: Pit's eyes, or behind and above Pit.";
/// The tip once the Game's Camera is offered (Kiu::FirstPerson::kGameCameraOffered).
inline constexpr const char* kCameraTipGame =
    "Where you see from: Pit's eyes, behind and above Pit, or where the game itself puts its "
    "camera.";
inline constexpr const char* kPitOpacityRow = "Pit Opacity";
inline constexpr const char* kPitOpacityTip =
    "How solid Pit looks when you see him from behind. Lower lets you see through him.";
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

// ---- AzaharFrame kiuaim: the page with fewer rows on view (handoff/reports/101/aim-page,
// approved 2026-11-25) ----
enum class Id : int; // tab_menu.h
struct MenuModel;
/// A row's place on the page: Reset Bottom Screen Position shows with In View moved, Gyro
/// Sensitivity with Gyro Aim.
bool AimPageRowShown(const MenuModel& model, Id id);
/// The page's own tooltip for a row whose text is ExplainFor's elsewhere (approved), or null.
const char* AimPageTip(Id id);
inline constexpr const char* kAimTipAiming =
    "Hold the grip to free your aim.";
inline constexpr const char* kAimTipGyroSensitivity =
    "How far the reticle moves as you turn your hand. Applies to every game.";
inline constexpr const char* kAimTipRecoil =
    "Haptic feedback in the aiming controller on each shot.";
inline constexpr const char* kAimTipRecoilCharged = "How strong it is for a charged shot.";
inline constexpr const char* kAimTipRecoilRapid = "How strong it is for each rapid-fire shot.";

/// The page: a front view (Immersive Mode, then a row a group) that never scrolls, and a page a
/// group with at most kAimPageMost rows; the rest of a group is under More.
struct MenuView;
enum class AimGroup : int { Aiming, View, Hud, BottomScreen, Weapon, ModsAndPack, About, Count };
inline constexpr int kAimFrontMost = 8, kAimPageMost = 5;
inline constexpr const char* kAimPageTitle = "Angel's Aim";
inline constexpr const char* kAngelsAimGroupHud = "HUD";
inline constexpr const char* kAimMore = "More";
/// A group's value on the front view. Weapon's: "In Hand" is approved, kAimWeaponOff PROPOSED.
inline constexpr const char* kAimWeaponInHand = "In Hand";
inline constexpr const char* kAimWeaponOff = "Off";
const char* AimGroupName(AimGroup group); ///< About's is kAngelsAimTitle
/// A More page's title: "<group> \u00B7 More" (PROPOSED: made of approved words).
std::string AimMoreTitle(AimGroup group);
struct AimPage {
    std::vector<Id> rows; ///< on the group's page
    std::vector<Id> more; ///< under its More row
};
/// A group's rows now: AimingRows' and AngelsAimFirstPersonRows' (so each keeps its own
/// condition), less AimPageRowShown's. Mods & Pack and About have none (drawn, not rows).
/// Places kept for rows that come with their own lanes: Power Menu after Subtitle Size (HUD);
/// HUD Position, HUD Opacity and Power Menu Opacity at the end of HUD's More; Character Portraits at
/// the end of Bottom Screen; Single Screen on the front view after Immersive Mode, while the
/// mode is not Angel's Aim. A row neither list places goes to Aiming's More (from AimingRows) or
/// View's (from the first-person rows), so nothing is lost: AimPageUnplaced lists them.
AimPage AimPageRows(const MenuModel& model, AimGroup group);
std::vector<Id> AimPageUnplaced(const MenuModel& model);
/// What the page's scope control counts and Use Global Values for All clears (the per-game ones:
/// GameValueRows): Immersive Mode, the rows there are now, and the rows of Kid Icarus: Uprising's
/// own that wait for Aiming or their switch (as the Controls tab counts its rows, shown or not).
std::vector<Id> AimScopeRows(const MenuModel& model);
/// The groups with a row on the front view, in order: those with rows, Mods & Pack, About.
std::vector<AimGroup> AimFrontGroups(const MenuModel& model);
/// "Direct Aim", "First Person \u00B7 Smooth", "In View", "Wrist", "In Hand", "Installed"; "" none.
/// Mods & Pack: Installed with the mod and the pack (and a hand model, once offered), Not
/// Installed with none, else "1 of 2 Installed" (PROPOSED).
std::string AimGroupValue(const MenuModel& model, AimGroup group);
/// Id::AngelsAimGroup's targets: a group's value (its row on the front view), Back, More.
inline constexpr int kAimBackIndex = 100, kAimMoreIndex = 101;
/// What shows for the view's fields: the front view (-1) when the group has no row now.
struct AimShown {
    int group = -1;
    bool more = false;
};
AimShown AimPageShown(const MenuModel& model, const MenuView& view);
/// Opens the page from elsewhere in the menu (the Settings tab's link, Show Angel's Aim): its
/// front view (`group` -1) or a group's page.
void AimPageOpen(MenuView& view, int group);
/// A press on one of those targets; true when the view changed (its scroll starts again).
bool AimPagePress(const MenuModel& model, MenuView& view, int index);
/// B, or Back: More to its group's page, a group's page to the front view (where the group's
/// row is). False on the front view: nothing to go back to.
bool AimPageBack(const MenuModel& model, MenuView& view);
/// Each frame: the fields are what shows, and the front view once the segment is left (so the
/// menu opens on it). Without the page (another game) the Mods tab leaves its segment.
void AimPageKeep(const MenuModel& model, MenuView& view);
/// The Bottom Screen's page is on view: the real one stays where it is meanwhile (phase 4).
bool AimBottomPageOpen(const MenuModel& model, const MenuView& view);

} // namespace FrameXr::Menu
