// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuvr: Kid Icarus: Uprising in first person in the Full (6DoF) view. European game
// only (the game-code hooks it stands on, kiu_hooks.h, are mapped for that one).
//
// AzaharFrame kiumode: first person is Immersive Mode's "Full (6DoF): Kid Icarus: Uprising /
// Angel's Aim" (stored 4, listed in every Immersive Mode list; Settings::kImmersiveModeAngelsAim).
// It is drawn as mode 3 (Settings::ImmersiveRenderMode, for every reader) and is in effect in
// VR for the European game whose code the hooks checked (OnFor below); elsewhere it plays as
// Full (6DoF): Other Games and the hooks' log says why. In effect, it turns on with first
// person (On) the eye hook, the culls off, the follow off, Right Stick turning, the land aim
// box, the aim and the shots from the hand (both with Aiming = Direct Aim only) and the bullet
// keep, the timed enemy keep
// (kAngelsAimEnemyKeepS), the player's own round shadow out, his see-through model hidden on
// land with the shots from the hand (AzaharFrame kiupit; kiu_hooks.h) and the screen at true
// size; and it supplies this game's registers
// (view 90, eye indicator 87,2) in memory only (Settings::RunSuppliedSetting): none of it is
// written into the settings, so a save during play stores mode 4 and nothing else. Another mode
// chosen during play turns all of it off at the next VBlank (live).
//
//   AZAHARFRAME_KIU_FIRST_PERSON=1    the developer override: first person on (On) in whatever
//                                     mode the game runs (choose a Full mode), with the eye
//                                     hook, the culls off, the follow off, Right Stick turning
//                                     and life size as above, and the registers supplied on the
//                                     European game; the aim, shots and enemy keep keep their
//                                     own switches. It no longer sets a mode or writes a
//                                     setting (it set mode 3 and the registers in memory).
//   AZAHARFRAME_KIU_WORLD_SCALE=<u>   the leaning scale in game units per metre, over life size
//                                     (0.5 to 100).
//   AZAHARFRAME_KIU_PLAYER_EYE_M=<m>  the player's eye height in metres for life size (default
//                                     1.6): the character's eyes, 12.3 units up, are that high.
//   AZAHARFRAME_KIU_AIM_NO_MOD=1      Direct Aim's reticle writer on the game without the C-Stick
//                                     & Gyro mod (kiu_probe.h): the game's own aim object, the
//                                     "aim hold" patches (kiu_hooks.h) and the reticle's "held"
//                                     flags. Direct Aim itself is the setting, as always.
//   AZAHARFRAME_KIU_AIM_6DOF=1        with first person and Direct Aim: the reticle goes where
//                                     the hand's ray points in the game's world, seen through
//                                     the game's own camera (not where it meets a screen). On
//                                     the game without the mod it turns on _AIM_NO_MOD's parts.
//   AZAHARFRAME_KIU_SHOTS_6DOF=1      with first person: the game's own shots start at the
//                                     hand's ray's origin (the weapon's muzzle) and fly along
//                                     the ray, each with its own scatter, with no lock target
//                                     (AzaharFrame aa6dof2; AZAHARFRAME_KIU_SHOT_LOCK=1 keeps
//                                     the game's: kiu_hooks.h);
//                                     bullets out of the game camera's view live on (as
//                                     AZAHARFRAME_KIU_BULLET_KEEP).
//   AZAHARFRAME_KIU_FP_FACTOR=<f>     AzaharFrame kiufov: the most the game draws past its own
//                                     frame on either axis in first person (1 to 8, default 6:
//                                     Layout::kKiuFpFactorMax). First person's screen is at true
//                                     size (gains 1, whatever the game's screen settings are) and
//                                     the game draws as much more on each axis as fills the view
//                                     (Layout::SolveKiuFirstPerson); past this factor the screen
//                                     is smaller on that axis instead.
//   AZAHARFRAME_KIU_FP_WIDE=0|1|2     AzaharFrame kiuwide: the game's own camera as wide as the
//                                     view (default 1): in a battle the scene camera's angle
//                                     and shape are the app's (the hooks' "wide camera" group),
//                                     those that make the game's own frame what fills the view,
//                                     so the screen is the game's frame at true size (factor 1,
//                                     Layout::KiuWideCamera) and the reticle reaches every place
//                                     in view. The game's zooms do nothing then. 2: the same
//                                     but the game's own shape kept (square pixels, the HUD
//                                     undistorted; the frame then reaches past the view's sides).
//                                     0: the game's camera as it is and the game drawing past
//                                     its frame (AzaharFrame kiufov), as before.
//   AZAHARFRAME_KIU_FP_2D=0|1         AzaharFrame kiu2d: the game's sprites drawn through its
//                                     geometry shader (lock-on marks, hit numbers) turn with the
//                                     world (default 1 in first person; kiu_hooks.cpp,
//                                     vr_view_matrix.h ClipCorrection). 0: as before.
//   AZAHARFRAME_KIU_WRIST_SCREEN=0|1  AzaharFrame kiuwrist: Angel's Aim's Secondary Screen on the
//                                     off hand's wrist like a watch (default 0, off): 10 x 7.5
//                                     cm, shown while it faces the eyes (wrist turned up), first
//                                     person staying engaged; the aiming hand's beam touches it
//                                     (a touch, never a shot); a grip on it grabs nothing. The off
//                                     hand is the right one with Motion Controls' Left
//                                     Controller, else the left. Angel's Aim in effect only (not
//                                     with AZAHARFRAME_KIU_FIRST_PERSON alone). vis/wrist_screen.h,
//                                     controls.cpp, immersive.cpp (Screens). AzaharFrame
//                                     kiupanel: a touch ends when the wrist turns away with the
//                                     trigger still held (released where it was; the trigger
//                                     fires nothing until let go; input/mapper.cpp).
//   AZAHARFRAME_KIU_WRIST_HOLD=0|1    AzaharFrame kiupanel: with the wrist screen shown, the aim
//                                     is held while the aiming hand's beam is on it and for 250
//                                     ms after (default 1, on): the reticle, first person's aim
//                                     and the shots keep the ray from before (no jump to the
//                                     frame's edge). 0: the beam aims as anywhere else.
//                                     vis/wrist_screen.h WristAimHold, controls.cpp.
//   AZAHARFRAME_KIU_WRIST_ABS=0|1     AzaharFrame kiuaim: Wrist Angle counts from the back of the
//                                     wrist (default 1). 0: from Wrist Side's place, as before.
//   AZAHARFRAME_KIU_PLACE_PREVIEW=0|1 AzaharFrame kiuaim: the real Bottom Screen stays on view
//                                     (frozen: the game is paused) while the menu's Bottom
//                                     Screen page is open (default 1; immersive_layout.h
//                                     PlacePreviewFor). 0: hidden with the menu, as before.
//                                     2: with any page of the menu (mock runs).
//   AZAHARFRAME_KIU_FP_TRACE=1|2      a line a second (2: a VBlank) with the transform's inputs
//                                     and what it gives (kiu_hooks.cpp).
//   AZAHARFRAME_KIU_FP_PROBE=<hex>    with the trace: the actor at that address, where it is in
//                                     the world and in the head's view.
//   AZAHARFRAME_KIU_FP_TALK=0|1       AzaharFrame kiuview: in first person the Secondary Screen
//                                     (where this game puts its talk: the portraits and the
//                                     words, at the screen's top) is shown as a small panel that
//                                     goes with the head, below the view's middle (default 0,
//                                     hidden as before). AzaharFrame kiupanel: for reading only:
//                                     the beam neither touches nor grabs it, so a shot through
//                                     it fires (the wrist screen is the one to touch; while that
//                                     shows, this does not).
//   AZAHARFRAME_KIU_FP_TALK_AT=<down>,<width>
//                                     the panel's middle in degrees below straight ahead and its
//                                     width in degrees (defaults kTalkDownDeg, kTalkWidthDeg;
//                                     down 0 to 60, width 10 to 60).
//   AZAHARFRAME_KIU_FP_FIELD=<percent>
//                                     AzaharFrame kiuview: how much of the view the picture fills
//                                     in first person, in percent of the view's angle on each
//                                     axis (kMinFieldPercent to 100; default 100, the whole
//                                     view). Below 100 the game's camera is that much narrower
//                                     and the screen still at true size, so a dark border shows
//                                     around it (Layout::ShareCover). AzaharFrame kiufp3: works
//                                     live (desktop: the screen 126 x 112 degrees at 100, 101 x 89
//                                     at 80, 76 x 67 at 60); kiuview's "no change" was its rig
//                                     writing the switch before the run began.
//   AZAHARFRAME_KIU_FP_EDGE=<percent>
//                                     AzaharFrame kiuview: the picture's edge fades to black over
//                                     this share of its half width and half height in first
//                                     person (0 to kMaxEdgePercent; default 0, a hard edge). On
//                                     the projection path only (vis/proj_screen.h kScreenFrag),
//                                     which needs the controller models' files: without them the
//                                     models' pass is never made and the screen goes as quads
//                                     (AzaharFrame kiufp3: why kiuview never saw it on the mock).
//                                     At FP_FIELD 100 most of the edge is outside the view.
//   AZAHARFRAME_KIU_SHOULDER=<back>,<up>[,<side>]
//                                     AzaharFrame kiuview: a third-person view in first
//                                     person: the eye moved this many metres behind, above and
//                                     to the right of the first-person eye (negative: in front,
//                                     below, to the left), along the game camera's level view
//                                     (its yaw), so Pit is in sight. Metres at life size
//                                     (UnitsPerMetre). Default off. AZAHARFRAME_KIU_EYE_OFFSET
//                                     (game units, kiu_hooks.cpp) replaces the eye's offset
//                                     itself and wins over this. AzaharFrame kiufp3: Pit is drawn
//                                     faded while the eye is near him (with 0.5 up: faded at 2 m
//                                     back or less, solid at 2.3 m or more): the game's own fade
//                                     of its player near its camera. Left as the game does it;
//                                     2.5,0.5 shows him solid (KIU_HOOKS.md).
// All live. The transform is input/kiu_room.h (pure, unit tested); the notes are in
// input/KIU_HOOKS.md ("First person in 6DoF").
//
// Threads: the XR thread publishes what mode 3 draws with (the rest pose, the head, the scale;
// immersive.cpp) and the aiming hand's ray (controls.cpp); the emulation thread's hook
// (kiu_hooks.cpp) takes the latest of both at each VBlank.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include "common/settings.h" // AzaharFrame kiu131: ClearOldBottomKeys
#include "frame_xr/input/kiu_room.h"
#include "frame_xr/input/kiu_turn.h"
#include "frame_xr/kiu_scan.h"
#include "frame_xr/kiu_ui.h" // AzaharFrame kiu1s
#include "frame_xr/menu/angels_aim_tab.h" // AzaharFrame 1.3.0: kGameCameraOffered

namespace FrameXr::Kiu::FirstPerson {

/// AzaharFrame kiumode: Immersive Mode's Angel's Aim (Settings::kImmersiveModeAngelsAim).
inline constexpr uint32_t kModeAngelsAim = 4;
/// The timed enemy keep's seconds with Angel's Aim (AZAHARFRAME_KIU_ENEMY_KEEP_S wins).
inline constexpr float kAngelsAimEnemyKeepS = 12.0f;

/// AzaharFrame kiumode: Angel's Aim is asked for: the stored mode is 4 and the game is Kid
/// Icarus: Uprising (any region), or the developer switch. The hooks log why it is not in effect.
constexpr bool WantedFor(bool dev_switch, uint32_t stored_mode, uint64_t title) {
    return dev_switch || (stored_mode == kModeAngelsAim && IsKidIcarus(title));
}
/// Angel's Aim is in effect: the stored mode is 4, the game is one the hooks are mapped for (the
/// European game, or USA Rev 1: AzaharFrame kiuusa) and its code is that build's (checked by the
/// hooks: SetGameCode), and the game is on view in VR (`in_vr`: in Flat no Immersive Mode does
/// anything, nor does this one).
constexpr bool AngelsAimFor(uint32_t stored_mode, uint64_t title, bool code_ok, bool in_vr) {
    return stored_mode == kModeAngelsAim && Input::KiuTurn::IsMappedTitle(title) && code_ok &&
           in_vr;
}
/// First person is on: Angel's Aim in effect, or the developer switch.
constexpr bool OnFor(bool dev_switch, uint32_t stored_mode, uint64_t title, bool code_ok,
                     bool in_vr) {
    return dev_switch || AngelsAimFor(stored_mode, title, code_ok, in_vr);
}

/// AzaharFrame kiu132: the Kid Icarus: Uprising C-Stick mods (controls_presets.h CStickModFiles)
/// replace the game's code; Angel's Aim's first person is made for the game without them (its
/// own aim route, the "aim hold" patches, the wide camera: KIU_HOOKS.md "C-Stick mods and
/// Angel's Aim"). A turned-on one is left out of a run that starts with Angel's Aim about to be
/// in effect: the stored mode 4, the European game, in VR. Everywhere else (Flat, the other
/// modes, the other regions, where a stored 4 plays as Other Games) it loads as before: there it
/// is what aims. The library's own state is never changed.
constexpr bool LeaveOutCStickModFor(bool installed, uint32_t stored_mode, uint64_t title,
                                    bool in_vr) {
    return installed && in_vr && stored_mode == kModeAngelsAim &&
           title == Input::KiuTurn::kTitleEur;
}
/// AzaharFrame kiu132: a C-Stick mod loaded in this run keeps Angel's Aim out (a stored 4 then
/// plays as Other Games, as with any other code): Angel's Aim asked for now (the stored mode 4,
/// the European game, in VR) while `mod_loaded`. The player is asked to restart the game, which
/// leaves the mod out.
constexpr bool CStickModBlocksFor(bool mod_loaded, uint32_t stored_mode, uint64_t title,
                                  bool in_vr) {
    return LeaveOutCStickModFor(mod_loaded, stored_mode, title, in_vr);
}

/// AzaharFrame kiufix2: the shots fly along the hand: the test switch (with first person on), or
/// Angel's Aim in effect with Aiming = Direct Aim (with Off or Gyro the reticle is the game's own
/// and the shots stay on it).
constexpr bool ShotsFrom6DofFor(bool switch_on, bool first_person_on, bool angels_aim,
                                bool direct_aim) {
    return (switch_on && first_person_on) || (angels_aim && direct_aim);
}

/// First person is on (live): Angel's Aim in effect, or AZAHARFRAME_KIU_FIRST_PERSON. Any thread.
bool On();
/// AzaharFrame kiumode: Angel's Aim in effect (live): everything first person needs. Any thread.
bool AngelsAim();
/// AzaharFrame kiu130: the third-person view is in effect (live): first person on, and Third
/// Person's setting (vr_kiu_over_shoulder) or AZAHARFRAME_KIU_SHOULDER (ShoulderFor: the
/// switch, when set, wins), with no AZAHARFRAME_KIU_EYE_OFFSET (which wins over both). The game
/// draws Pit with his own weapon there, so the weapon in hand is left out of the image
/// (Vis::WeaponModelDrawnFor). Any thread.
bool ShoulderOn();
/// AzaharFrame kiu133: the Game's Camera view is in effect (live): first person on, and the
/// setting (vr_kiu_game_camera) or AZAHARFRAME_KIU_GAME_CAMERA (GameCameraFor: the switch, when
/// set, wins), with no AZAHARFRAME_KIU_EYE_OFFSET. The game's own camera places the eye (the eye
/// hook stays out); everything else of first person stays. It wins over Third Person. Any
/// thread.
bool GameCameraOn();
/// AzaharFrame kiu133: a view from outside Pit (Third Person or the Game's Camera): he is
/// drawn with his own weapon, so the weapon in hand is left out, and Pit Opacity applies. Any
/// thread.
bool OutsideViewOn();
/// AzaharFrame kiumode: Angel's Aim asked for (WantedFor; live). Any thread.
bool Wanted();
/// First person is on and `title` is a game it is for (the European game, USA Rev 1). Any thread.
bool ForTitle(uint64_t title);
/// Mode 3's registers for this game (the settings' View Matrix Register and Eye Indicator
/// Register), found to be right for it on the desktop (KIU_HOOKS.md).
inline constexpr int kViewRegister = 90;
inline constexpr const char* kEyeIndicator = "87,2";
/// AzaharFrame kiumode: the hooks, each VBlank: the running game and whether its code is the
/// one they are mapped for (false: not checked, or not that code).
void SetGameCode(uint64_t title, bool code_ok);
/// AzaharFrame kiuusa: the running game's code is one the hooks are mapped for (SetGameCode):
/// the in-game menu shows first person's rows for it. Any thread.
bool CodeMapped();
/// AzaharFrame kiumode: the hooks, each VBlank: the two registers supplied to the renderer for
/// this run (in memory only; Settings::RunSuppliedSetting), or the settings' own again.
void SupplyRegisters(bool on);
/// The frontend, when a game boots, before its presets are applied: forgets the last run's code
/// check and registers. Writes no setting (AzaharFrame kiumode).
void OnGameBoot();
/// AzaharFrame kiu132: the frontend, when a game boots (after OnGameBoot): a C-Stick mod is loaded
/// in this run (`loaded`), or one was left out of it (`left_out`, LeaveOutCStickModFor).
void SetCStickMod(bool loaded, bool left_out);
/// AzaharFrame kiu132: a C-Stick mod is loaded in this run (Angel's Aim stays out). Any thread.
bool CStickModLoaded();
/// AzaharFrame kiu132: each VBlank (the hooks): the toasts, "C-Stick Mod Not Loaded" once a run
/// with a mod left out, and "Restart the Game for Angel's Aim" each time Angel's Aim is asked for
/// while a mod is loaded (CStickModBlocksFor), with a log line each.
void NoteCStickMod();
/// AzaharFrame kiufix2: counts the runs (OnGameBoot bumps it). The hook, turning and probe state
/// resets when it changes: a Process address can be reused by the next run of the same title.
uint32_t RunCount();
/// The leaning scale in use, game units per metre: life size, or the override. Any thread.
float UnitsPerMetre();

/// AzaharFrame kiufov: AZAHARFRAME_KIU_FP_FACTOR (live), or Layout::kKiuFpFactorMax. Any thread.
float FactorMax();
/// AzaharFrame kiufov: the scene projection's vertical angle and shape as the first person pass
/// read them at the last VBlank (what the game draws its world with), published by the
/// emulation thread. Any thread: false when none was read in the last 250 ms. The shape is the
/// projection's field (5:3; a widescreen build of the C-Stick & Gyro mod leaves 5:3 there and
/// draws wider: the caller takes the mod's shape where it is known).
void PublishProjection(const Input::KiuRoom::Projection& projection);
bool SceneProjection(float& fov_y_deg, float& aspect);
/// AzaharFrame kiuwide: the game's own shape for the scene camera (its field before the app
/// widened it), published by the hooks each VBlank; 0 when not known. Any thread.
void PublishGameAspect(float aspect);
float GameAspect();

/// AzaharFrame kiuview: the talk panel (AZAHARFRAME_KIU_FP_TALK, _AT): the Secondary Screen
/// head-locked below the view's middle, kTalkDistanceM away (nearer than first person's screen,
/// so it is drawn over it).
inline constexpr float kTalkDownDeg = 24.0f;
inline constexpr float kTalkWidthDeg = 34.0f;
inline constexpr float kTalkDistanceM = 0.8f;
struct Talk {
    bool on = false;
    float down_deg = kTalkDownDeg;
    float width_deg = kTalkWidthDeg;
};
/// The two switches' texts (null: unset): on when the first is "1"; the place within its limits.
inline Talk ParseTalk(const char* on_text, const char* at_text) {
    Talk t;
    t.on = on_text && std::string_view{on_text} == "1";
    float v[2] = {};
    if (at_text && Input::KiuHooks::ParseFloats(at_text, v, 2) == 2) {
        t.down_deg = std::clamp(v[0], 0.0f, 60.0f);
        t.width_deg = std::clamp(v[1], 10.0f, 60.0f);
    }
    return t;
}
/// AzaharFrame kiutoggles: the panel is shown: by the switch when it is set ("1" on, anything else
/// off), else by the setting (Settings::values.vr_kiu_talk_panel, off by default).
constexpr bool TalkOnFor(const char* on_text, bool setting) {
    return on_text ? std::string_view{on_text} == "1" : setting;
}
/// Live, with first person on (off otherwise): the switches, or the setting. Any thread.
Talk TalkNow();
/// The panel in the head's frame (x right, y up, -z ahead): its middle, its pitch (about x; it
/// faces the eye) and its size for a picture of `aspect` (width over height).
struct TalkPlace {
    float y = 0.0f, z = 0.0f;
    float pitch_rad = 0.0f;
    float width = 0.0f, height = 0.0f;
};
inline TalkPlace TalkPanelInView(const Talk& t, float aspect, float distance = kTalkDistanceM) {
    constexpr float kDeg = 3.14159265358979f / 180.0f;
    TalkPlace p;
    const float a = t.down_deg * kDeg;
    p.y = -distance * std::sin(a);
    p.z = -distance * std::cos(a);
    p.pitch_rad = -a;
    p.width = 2.0f * distance * std::tan(0.5f * t.width_deg * kDeg);
    p.height = aspect > 0.1f ? p.width / aspect : p.width * 0.75f;
    return p;
}

/// AzaharFrame kiu131: Angel's Aim's Bottom Screen (vr_kiu_bottom_screen): the Secondary Screen
/// Off, Persistent (with the view, below it by default; moved with a grip, for reading only)
/// or on the off hand's Wrist (touched with the beam). It replaces Talk Panel (Persistent) and
/// Secondary Screen on Wrist (Wrist).
enum class BottomScreen : uint32_t { Off = 0, Persistent = 1, Wrist = 2 };
/// The stored choice; while it is Off, the two settings it replaced (Wrist won while both were
/// on). Choosing a value in the menu clears those two, so Off then stays Off.
constexpr BottomScreen BottomScreenModeFor(uint32_t stored, bool old_wrist, bool old_talk) {
    if (stored == 1u || stored == 2u) {
        return static_cast<BottomScreen>(stored);
    }
    return old_wrist ? BottomScreen::Wrist : old_talk ? BottomScreen::Persistent : BottomScreen::Off;
}
/// The test switches over it (null: unset): AZAHARFRAME_KIU_WRIST_SCREEN on ("1", "on", "true")
/// gives Wrist and off takes a Wrist away; AZAHARFRAME_KIU_FP_TALK "1" gives Persistent (unless
/// the wrist switch is on) and anything else takes a Persistent away.
inline BottomScreen BottomScreenWithSwitches(BottomScreen mode, const char* wrist,
                                             const char* talk) {
    bool wrist_on = false;
    if (wrist) {
        const std::string_view w{wrist};
        wrist_on = w == "1" || w == "on" || w == "true" || w == "ON" || w == "On" ||
                   w == "TRUE" || w == "True";
        mode = wrist_on ? BottomScreen::Wrist
                        : (mode == BottomScreen::Wrist ? BottomScreen::Off : mode);
    }
    if (talk && !wrist_on) {
        mode = std::string_view{talk} == "1"
                   ? BottomScreen::Persistent
                   : (mode == BottomScreen::Persistent ? BottomScreen::Off : mode);
    }
    return mode;
}
/// Bottom Screen Size and Opacity (percent) as factors, within the settings' limits.
constexpr float BottomScaleFor(uint32_t percent) {
    return static_cast<float>(percent < 50u ? 50u : percent > 200u ? 200u : percent) / 100.0f;
}
constexpr float BottomAlphaFor(uint32_t percent) {
    return static_cast<float>(percent < 20u ? 20u : percent > 100u ? 100u : percent) / 100.0f;
}
/// Persistent's offset limits from its default place (degrees right, and down).
inline constexpr float kBottomMaxRightDeg = 60.0f;
inline constexpr float kBottomMaxUpDeg = 45.0f;   ///< up: a negative down offset
inline constexpr float kBottomMaxDownDeg = 36.0f; ///< its middle at most 60 deg below ahead
/// The two settings Bottom Screen replaced (vr_kiu_wrist_screen, vr_kiu_talk_panel) off, the
/// global value and the game's own (if it has one), once Bottom Screen is chosen. True when the
/// game had its own value of either (its file is then to be saved too).
inline bool ClearOldBottomKeys() {
    bool game_own = false;
    const auto clear = [&game_own](Settings::SwitchableSetting<bool>& s) {
        const bool global = s.UsingGlobal();
        if (!global) {
            s.SetValue(false); // the game's own
            game_own = true;
        }
        s.SetGlobal(true);
        s.SetValue(false); // the global one
        s.SetGlobal(global);
    };
    auto& v = Settings::values;
    clear(v.vr_kiu_wrist_screen);
    clear(v.vr_kiu_talk_panel);
    return game_own;
}

struct BottomNow {
    BottomScreen mode = BottomScreen::Off;
    float scale = 1.0f; ///< Bottom Screen Size
    float alpha = 1.0f; ///< Bottom Screen Opacity
    Talk place{};       ///< Persistent: its default place (FP_TALK_AT) and width (scaled)
    float right_deg = 0.0f, down_deg = 0.0f; ///< Persistent: the grip's offset (settings)
};
/// AzaharFrame kiu140: the game plays a cutscene of its own drawing, shown on the Primary Screen
/// (kiu_cutscene.h; Cutscenes, vr_kiu_cutscenes, or AZAHARFRAME_KIU_CUTSCENES=0|1). Set each VBlank
/// by the hooks; any thread.
/// `tan_half_y`: the angle the cutscene's own camera draws (half of it, its tangent), 0 for one
/// that plays through the battle's.
void SetCutscene(bool on, float tan_half_y = 0.0f);
bool Cutscene();
/// The angle for the view to follow while such a cutscene plays in it (0: none), within 15 to 110
/// degrees.
float CutsceneAngleDeg();
/// A battle under way in first person (the wide camera in): its 2D is the HUD. Elsewhere (the
/// results, menus) the 2D is a page, which stays the game's.
void SetBattle(bool on);
bool Battle();
/// AzaharFrame kiu1s: what the game's battle UI shows (kiu_ui.h), each behind its test switch and
/// false while that is off or the UI cannot be read: a page instead of the HUD
/// (AZAHARFRAME_KIU_PAGE, on unless 0), the pause screen (AZAHARFRAME_KIU_PAUSE_PANEL=1: the Secondary Screen
/// comes up in front as a panel to point at). Set each VBlank by the hooks; any thread.
void SetBattleUi(bool page, bool pause_panel, bool menu = false);
bool BattlePage();
/// AzaharFrame kiuhud: that page is the hub's, with AZAHARFRAME_KIU_PAGE: its 2D goes in
/// the view as the HUD does (kiu_hud.h PageInView).
bool MenuPage();
bool PausePanel();
/// The 2D on view is a page, whole, rather than the HUD's pieces.
constexpr bool WholePage(bool battle, bool battle_page) {
    return !battle || battle_page;
}
/// The pause panel: degrees below straight ahead, across, and how far away (metres).
inline constexpr float kPauseDownDeg = 4.0f, kPauseWidthDeg = 36.0f;
/// The way the head looks without its roll (x, y, z, w; x right, y up, -z ahead), at most
/// kPanelPitchDeg up or down: a panel left where the head was stays upright and in reach.
inline constexpr float kPanelPitchDeg = 50.0f;
inline std::array<float, 4> NoRoll(float qx, float qy, float qz, float qw) {
    constexpr float kDeg = 3.14159265358979f / 180.0f;
    // -z turned by q.
    const float fx = -2.0f * (qx * qz + qw * qy), fy = -2.0f * (qy * qz - qw * qx),
                fz = -(1.0f - 2.0f * (qx * qx + qy * qy));
    const float flat = std::sqrt(fx * fx + fz * fz);
    const float yaw = flat > 1e-4f ? std::atan2(-fx, -fz) : 0.0f;
    const float pitch =
        std::clamp(std::atan2(fy, flat), -kPanelPitchDeg * kDeg, kPanelPitchDeg * kDeg);
    const float cy = std::cos(0.5f * yaw), sy = std::sin(0.5f * yaw);
    const float cp = std::cos(0.5f * pitch), sp = std::sin(0.5f * pitch);
    return {cy * sp, sy * cp, -sy * sp, cy * cp}; // yaw * pitch
}
/// Shown: paused, unless the Secondary Screen is up on the wrist (that one takes the touches).
constexpr bool PausePanelShown(bool paused, bool wrist_up) {
    return paused && !wrist_up;
}
/// AzaharFrame kiu1s: the talk's pictures (the portraits, an illustration) are on the Secondary
/// Screen (AZAHARFRAME_KIU_PORTRAITS=1): a part of it is shown in the view, on the off hand's
/// side. Ready all through a battle: its layer is there, shrunk to nothing, so that a line starting
/// or ending changes neither the number of layers (LayerSlots) nor how the game's bottom screen is
/// drawn.
enum class PortraitState : int { Off, Ready, Up };
constexpr PortraitState PortraitsFor(bool wanted, bool battle, bool pictures, bool paused) {
    return !wanted || !battle     ? PortraitState::Off
           : pictures && !paused ? PortraitState::Up
                                 : PortraitState::Ready;
}
/// The same layer is the pause panel's and the menu panel's (AZAHARFRAME_KIU_PAUSE_PANEL=1,
/// _MENU_PANEL=1): `keep` it there, shrunk, whenever the view is on, so that pausing, a menu coming
/// up and a battle starting after it add or take no layer (a screen's layer that comes moves every
/// quad after it to another overlay for a refresh, layer_slots.h).
constexpr PortraitState LayerFor(PortraitState portraits, bool keep) {
    return portraits == PortraitState::Off && keep ? PortraitState::Ready : portraits;
}
void SetPortraits(PortraitState state);
PortraitState Portraits();
inline constexpr float kPortraitShrunk = 1e-5f; ///< as LayerSlots::kCollapse
/// A part of the Secondary Screen's picture, in its 320 x 240 pixels.
struct Cut {
    int x = 0, y = 0, width = 320, height = 240;
    constexpr bool Whole() const {
        return x == 0 && y == 0 && width == 320 && height == 240;
    }
    bool operator==(const Cut&) const = default;
};
/// The portraits: below the words, which are up to row 40 (the band behind them goes to 56); the
/// tallest of them reach over that.
inline constexpr Cut kPortraitCut{0, 42, 320, 198};
/// Its middle, degrees to the off hand's side and below straight ahead, and its width. Above the
/// words on the Primary Screen at their usual place and size (Subtitle Height and Size), which
/// reach 28 degrees to each side, 15 and more below.
inline constexpr float kPortraitSideDeg = 25.0f, kPortraitDownDeg = 7.5f,
                       kPortraitWidthDeg = 16.0f;
/// Subtitle Height there (its default), and how far down the portraits are at another.
inline constexpr float kPortraitWordsDeg = -18.0f;
constexpr float PortraitDown(float words_deg) {
    return kPortraitDownDeg + kPortraitWordsDeg - words_deg;
}
/// Where the cut is in an image of the whole Secondary Screen of this size.
struct CutRect {
    int x = 0, y = 0, width = 0, height = 0;
};
constexpr CutRect CutIn(const Cut& cut, int image_width, int image_height) {
    const auto at = [](int v, int of, int size) { return (v * size + of / 2) / of; };
    CutRect r{at(cut.x, 320, image_width), at(cut.y, 240, image_height), 0, 0};
    r.width = at(cut.x + cut.width, 320, image_width) - r.x;
    r.height = at(cut.y + cut.height, 240, image_height) - r.y;
    return r;
}
/// The layer is theirs: the Secondary Screen is nowhere on view (it has them).
constexpr bool PortraitsHere(PortraitState s, bool persistent, bool pause_panel, bool wrist_up) {
    return s != PortraitState::Off && !persistent && !pause_panel && !wrist_up;
}
/// AzaharFrame kiu1s: the game's menu that is up outside a battle (kiu_ui.h; below 0: none) while
/// AZAHARFRAME_KIU_MENU_SCREENS=1 or AZAHARFRAME_KIU_MENU_PANEL=1 asks for it: the normal two
/// screens instead of the view, or (`panel`) the Secondary Screen as a large panel in front with
/// the Primary Screen above it.
void SetMenu(int menu, bool panel);
int MenuUp();
bool MenuPanel();
/// The menu's two screens, where the head was as the first menu came up: the Secondary Screen's
/// middle (degrees below straight ahead) and width, the Primary Screen's (above) and width. The
/// Secondary Screen is the one pointed at: large, its middle a little below the eyes, where a
/// hand at rest points; the Primary Screen is read only, above it and clear of it.
struct MenuPlace {
    float down_deg, width_deg;
    float up_deg, top_width_deg;
};
inline constexpr MenuPlace kMenuPlace{12.0f, 40.0f, 14.5f, 32.0f};
/// The Fiend's Cauldron, where both screens say something: the Primary Screen the larger one.
inline constexpr MenuPlace kMenuPlaceCauldron{14.0f, 34.0f, 11.75f, 38.0f};
constexpr MenuPlace MenuPlaceFor(int menu) {
    return menu == Ui::kMenuCauldron ? kMenuPlaceCauldron : kMenuPlace;
}
/// How high a panel `width_deg` wide is, in degrees (it faces the eye).
inline float PanelHighDeg(float width_deg, float aspect) {
    constexpr float kDeg = 3.14159265358979f / 180.0f;
    return 2.0f / kDeg * std::atan(std::tan(0.5f * width_deg * kDeg) / aspect);
}
/// The clear band between the two screens, in degrees (below 0: they overlap).
inline float MenuGapDeg(const MenuPlace& p, float top_aspect = 400.0f / 240.0f) {
    return (p.up_deg - 0.5f * PanelHighDeg(p.top_width_deg, top_aspect)) -
           (-p.down_deg + 0.5f * PanelHighDeg(p.width_deg, 320.0f / 240.0f));
}
/// The game's letters (9 pixels high) on a screen `pixels` wide shown `width_deg` wide, in degrees.
constexpr float LetterDeg(float width_deg, float pixels) {
    return 9.0f * width_deg / pixels;
}
/// AzaharFrame kiu1s: no weapon in the hand (nor the arm and effects that go with it) while one of
/// the game's menus is up outside a battle: the goddess's room, the Cauldron, the page after a
/// fall. `menu` is the steady one (Ui::SteadyMenu: the game's one-frame gaps between menus do not
/// count, and it is gone 0.2 s after the last); a battle's camera in brings the weapon back at
/// once. The Practice Range's shooting is a battle with no menu (seen), so it keeps the weapon;
/// so does the pause screen. First person, no switch. Set each VBlank by the hooks; any thread.
constexpr bool WeaponHiddenFor(int menu, bool battle) {
    return menu >= 0 && !battle;
}
void SetWeaponHidden(bool hidden);
bool WeaponHidden();
/// AzaharFrame kiu140: which of the game's menus get the normal screens: the flat ones (drawn for
/// the game's frame, so in the view what is beside it shows) unless the switch says 0 (none) or 1
/// (the goddess's room, which is a room to look round in, too).
enum class MenuScreens { None, Flat, All };
constexpr MenuScreens MenuScreensFor(const char* text) {
    return text && *text == '0' ? MenuScreens::None
           : text && *text == '1' ? MenuScreens::All
                                  : MenuScreens::Flat;
}
constexpr int MenuOnScreens(MenuScreens which, int menu) {
    return which == MenuScreens::None || (which == MenuScreens::Flat && menu == Ui::kMenuHub)
               ? Ui::kNoMenu
               : menu;
}
bool CutscenesOnScreen();
/// AzaharFrame kiu140: the HUD moved into the view (HUD, vr_kiu_hud, or AZAHARFRAME_KIU_HUD=0|1).
bool HudInView();
/// Live, with Angel's Aim's first person on (Off otherwise): the settings and switches. Any thread.
BottomNow BottomScreenNow();
/// Persistent in the head's frame (x right, y up, -z ahead): its middle `right` degrees to the
/// right and `down` degrees below straight ahead, facing the eye.
struct PanelInView {
    float qx = 0.0f, qy = 0.0f, qz = 0.0f, qw = 1.0f;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float width = 0.0f, height = 0.0f;
};
inline PanelInView PersistentInView(float right_deg, float down_deg, float width_deg, float aspect,
                                    float distance = kTalkDistanceM) {
    constexpr float kDeg = 3.14159265358979f / 180.0f;
    const float yaw = -right_deg * kDeg;  // about +y: positive turns left
    const float pitch = -down_deg * kDeg; // about +x: negative looks down
    const float cy = std::cos(0.5f * yaw), sy = std::sin(0.5f * yaw);
    const float cp = std::cos(0.5f * pitch), sp = std::sin(0.5f * pitch);
    PanelInView p;
    // q = yaw * pitch
    p.qw = cy * cp;
    p.qx = cy * sp;
    p.qy = sy * cp;
    p.qz = -sy * sp;
    const float c = std::cos(pitch);
    p.x = -distance * std::sin(yaw) * c;
    p.y = distance * std::sin(pitch);
    p.z = -distance * std::cos(yaw) * c;
    p.width = 2.0f * distance * std::tan(0.5f * width_deg * kDeg);
    p.height = aspect > 0.1f ? p.width / aspect : p.width * 0.75f;
    return p;
}

/// AzaharFrame kiu1s: the card is asked for: its setting (vr_kiu_talk_portraits), or the test
/// switch AZAHARFRAME_KIU_PORTRAITS when set ("1" on, anything else off).
constexpr bool PortraitsWantedFor(const char* switch_text, bool setting) {
    return switch_text ? std::string_view{switch_text} == "1" : setting;
}
/// The words on the Primary Screen too (the "talk on top" group): AZAHARFRAME_KIU_TALK_TOP when set
/// ("1" on, anything else off), else unless the Secondary Screen is in the view (Persistent): on
/// land the game has them on the Secondary Screen only, which the Wrist's is not looked at for
/// and Off has nowhere (the owner, 2026-11-25). The portraits' card leaves them out too.
constexpr bool TalkTopFor(const char* switch_text, bool persistent) {
    return switch_text ? std::string_view{switch_text} == "1" : !persistent;
}
/// The two above from the switches and settings now, and the card's opacity
/// (vr_kiu_talk_portraits_opacity, 20 to 100 percent; live). Any thread.
bool PortraitsWanted();
bool TalkTopWanted();
float PortraitAlpha();
/// AzaharFrame kiu1s: where the portraits' card is, in the head's frame: the one place to change
/// (the owner would later have it by the off hand itself). `right`: the off hand is the right one.
/// `words_deg`: Subtitle Height, degrees up, while the two move as one (the owner, 2026-11-25:
/// Linked), else the usual one; `across` and `up`: degrees to the right and up from there.
inline PanelInView PortraitPlace(bool right, float words_deg = kPortraitWordsDeg,
                                 float across = 0.0f, float up = 0.0f) {
    return PersistentInView(kPortraitSideDeg * (right ? 1.0f : -1.0f) + across,
                            PortraitDown(words_deg) - up, kPortraitWidthDeg,
                            static_cast<float>(kPortraitCut.width) /
                                static_cast<float>(kPortraitCut.height));
}

/// AzaharFrame kiuhs3a: the portraits in the plane of the Primary Screen's picture, where the words
/// are (kiu_hud.h MoveSubtitle: the same place in both eyes' pictures): a quad an eye, each a part of
/// that eye's screen, so they are as far as the words and face as they do, whatever the depth.
/// In the screen's own coordinates (-1 to 1, right and up).
struct InPlane {
    float u = 0.0f, v = 0.0f;           ///< its middle
    float half_u = 0.0f, half_v = 0.0f; ///< its half sizes
};
inline constexpr float kInPlaneMostDeg = 72.0f; ///< no farther aside of the screen's middle
/// `p` (facing the eye, in the head's frame) in the plane of a screen `pitch_rad` up from straight
/// ahead with these half angles: its middle in the same direction, its height as many degrees; its
/// width foreshortened as the plane's, as the words are.
inline InPlane ToPlane(const PanelInView& p, float pitch_rad, float tan_half_x, float tan_half_y) {
    InPlane out;
    const float d = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (!(d > 1e-4f) || !(tan_half_x > 0.05f) || !(tan_half_y > 0.05f) ||
        !std::isfinite(pitch_rad)) {
        return out;
    }
    const float c = std::cos(pitch_rad), s = std::sin(pitch_rad);
    float x = p.x / d;
    float y = (p.y * c + p.z * s) / d;
    float ahead = -(p.z * c - p.y * s) / d;
    const float least = std::cos(kInPlaneMostDeg * 3.14159265358979f / 180.0f);
    if (ahead < least) { // held at the cone's edge, the same way aside
        const float side = std::max(std::sqrt(x * x + y * y), 1e-4f);
        const float k = std::sqrt(1.0f - least * least) / side;
        x *= k;
        y *= k;
        ahead = least;
    }
    const float away = 1.0f / ahead; // in screen distances
    out.u = x * away / tan_half_x;
    out.v = y * away / tan_half_y;
    out.half_u = 0.5f * p.width / d * away / tan_half_x;
    out.half_v = 0.5f * p.height / d * away / tan_half_y;
    return out;
}
/// The portraits (and the power menu with the HUD) in that plane: live switch
/// AZAHARFRAME_KIU_WORDS_PLANE (0: as before, the portraits 0.8 m away, the power menu 8 m).
inline constexpr const char* kSwitchWordsPlane = "AZAHARFRAME_KIU_WORDS_PLANE";
bool WordsPlane();

/// AzaharFrame kiuview: AZAHARFRAME_KIU_FP_FIELD, a share of the view's angle (0.5 to 1).
inline constexpr int kMinFieldPercent = 50;
/// AZAHARFRAME_KIU_FP_FIELD's text as a share: null or unreadable 1, else the percent within
/// kMinFieldPercent to 100 over 100.
inline float ParseFieldShare(const char* text) {
    float v = 0.0f;
    if (!text || Input::KiuHooks::ParseFloats(text, &v, 1) != 1 || !std::isfinite(v)) {
        return 1.0f;
    }
    return std::clamp(v, static_cast<float>(kMinFieldPercent), 100.0f) / 100.0f;
}
/// The switch, live (1 when first person is off). Any thread.
float FieldShare();
/// AzaharFrame kiuview: AZAHARFRAME_KIU_FP_EDGE, the soft edge's width (0 to 0.5 of the half
/// size), 0 when first person is off. Any thread.
inline constexpr int kMaxEdgePercent = 50;
inline float ParseEdgeShare(const char* text) {
    float v = 0.0f;
    if (!text || Input::KiuHooks::ParseFloats(text, &v, 1) != 1 || !std::isfinite(v)) {
        return 0.0f;
    }
    return std::clamp(v, 0.0f, static_cast<float>(kMaxEdgePercent)) / 100.0f;
}
float EdgeShare();

/// AzaharFrame kiuview: AZAHARFRAME_KIU_SHOULDER's text in metres: back, up and side (side
/// optional, 0); false when unset, unreadable, or all of it 0. Each kept within -kMaxShoulderM to
/// kMaxShoulderM.
inline constexpr float kMaxShoulderM = 10.0f;
inline bool ParseShoulder(const char* text, float& back_m, float& up_m, float& side_m) {
    float v[3] = {};
    if (!text) {
        return false;
    }
    const int n = Input::KiuHooks::ParseFloats(text, v, 3);
    if (n < 2 || !std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2])) {
        return false;
    }
    back_m = std::clamp(v[0], -kMaxShoulderM, kMaxShoulderM);
    up_m = std::clamp(v[1], -kMaxShoulderM, kMaxShoulderM);
    side_m = n == 3 ? std::clamp(v[2], -kMaxShoulderM, kMaxShoulderM) : 0.0f;
    return back_m != 0.0f || up_m != 0.0f || side_m != 0.0f;
}
/// AzaharFrame kiutoggles: Third Person (Settings::values.vr_kiu_over_shoulder) puts the eye
/// this far behind and above first person's: Pit drawn solid (the game fades him at 2 m back or
/// less with 0.5 m up; KIU_HOOKS.md, kiufp3).
inline constexpr float kShoulderSettingBackM = 2.5f;
inline constexpr float kShoulderSettingUpM = 0.5f;
/// The third-person offset in use: the switch's text when it is set (ParseShoulder: "0" or
/// anything unreadable is off), else the setting's fixed place; false when off.
inline bool ShoulderFor(const char* switch_text, bool setting, float& back_m, float& up_m,
                        float& side_m) {
    if (switch_text) {
        return ParseShoulder(switch_text, back_m, up_m, side_m);
    }
    if (!setting) {
        return false;
    }
    back_m = kShoulderSettingBackM;
    up_m = kShoulderSettingUpM;
    side_m = 0.0f;
    return true;
}
/// AzaharFrame 1.3.0: whether the Game's Camera is offered (Menu::kGameCameraOffered).
inline constexpr bool kGameCameraOffered = Menu::kGameCameraOffered;
/// AzaharFrame kiu133: the Game's Camera from AZAHARFRAME_KIU_GAME_CAMERA's text when it is set
/// ("1" on, anything else off), else the setting (while kGameCameraOffered).
inline bool GameCameraFor(const char* switch_text, bool setting) {
    if (switch_text) {
        return switch_text[0] == '1' && switch_text[1] == '\0';
    }
    return kGameCameraOffered && setting;
}
/// AzaharFrame kiu133: the View row (Angel's Aim) from its two keys: 0 First Person, 1 Over the
/// Shoulder (vr_kiu_over_shoulder, as before), 2 the Game's Camera (vr_kiu_game_camera, which
/// wins). Choosing writes both: Third Person only for 1, the Game's Camera only for 2.
constexpr int ViewFor(bool over_shoulder, bool game_camera) {
    return game_camera && kGameCameraOffered ? 2 : over_shoulder ? 1 : 0;
}
constexpr bool ViewOverShoulder(int view) {
    return view == 1;
}
constexpr bool ViewGameCamera(int view) {
    return view == 2;
}
/// The eye's extra offset in game units along the camera's level view (x right, y up, z ahead:
/// the axes of Input::KiuHooks::LevelYawAxes), from metres back, up and right at
/// `units_per_metre`.
inline Input::KiuRoom::Vec3 ShoulderOffset(float back_m, float up_m, float side_m,
                                           float units_per_metre) {
    return {-side_m * units_per_metre, up_m * units_per_metre, -back_m * units_per_metre};
}

/// AzaharFrame kiuwide: AZAHARFRAME_KIU_FP_WIDE (live): 0 off, 1 the view's shape (default), 2
/// the game's own shape kept. Any thread.
int WideMode();
/// AzaharFrame kiuwide: the scene camera's angle (up and down, degrees) and field (the shape
/// the game's projection is built from) that make the game's own frame fill the view: the XR
/// thread, from the Full view's screen solve (Layout::KiuWideCamera, the C-Stick & Gyro mod's
/// widening taken out of the shape). The hooks take it at each VBlank: false when the switch is
/// off or nothing was published since the game booted (the last one is kept however old: it
/// follows the view alone; `fov_y_rad` in radians).
void PublishWideTarget(float fov_y_deg, float field_aspect);
bool WideTarget(float& fov_y_rad, float& field_aspect);
/// AzaharFrame kiuwide: the shape the game draws (and aims) its world with as the XR thread's
/// screen solve took it (the projection's field times a widescreen mod's widening), published
/// each display frame; the aim's first guess (KiuRoom::AimShape). 0: none since the game booted.
void PublishDrawnShape(float aspect);
float DrawnShape();

/// What mode 3 draws with this display frame, in the room (OpenXR's LOCAL space).
struct View {
    bool valid = false; ///< mode 3 is engaged for the game and the head is tracked
    int64_t host_ns = 0; ///< set when published
    Input::KiuRoom::Rest rest;
    Input::KiuRoom::Pose head;
    float units_per_metre = 0.0f; ///< the scale the head's position is given to the game with
    /// The rotation given to the game for the head (Layout::GameCameraRotation, rest frame):
    /// the head's own while the screen has its default size and place.
    Input::KiuRoom::Quat given;
    /// The screen's gains and turn (Layout::HeadLocked): 1, 1, 0, 0 while the Full view's
    /// screen has its default size and place, the only case the room lines up with the picture.
    float gain_x = 1.0f, gain_y = 1.0f, screen_pitch = 0.0f, screen_yaw = 0.0f;
    /// The game camera's shape as the immersive code measured it from the game's draws (Match
    /// Game Camera; 0: not measured): what the aim uses until the game's own aim has shown its
    /// shape (KiuRoom::AimShape).
    float measured_aspect = 0.0f;
};
/// The aiming hand's ray this display frame, in the room: from the weapon's muzzle (or the aim
/// pose) along the weapon's axis (or the pose's), as Direct Aim's and the laser's.
struct Hand {
    bool valid = false;
    int64_t host_ns = 0;
    Input::KiuRoom::Vec3 origin;
    Input::KiuRoom::Vec3 direction;
    bool muzzle = false; ///< the origin is a drawn weapon's muzzle
};
/// XR thread.
void PublishView(const View& view);
void PublishHand(const Hand& hand);

struct Sample {
    View view;
    Hand hand;
};
/// Any thread: the latest of both (not valid when older than 250 ms).
Sample Latest(int64_t host_now_ns);

/// AZAHARFRAME_KIU_AIM_NO_MOD, or AZAHARFRAME_KIU_AIM_6DOF, or Angel's Aim with Direct Aim
/// (live). Any thread.
bool AimNoModWanted();
/// AZAHARFRAME_KIU_AIM_6DOF with first person on, or Angel's Aim with Direct Aim (live). Any
/// thread.
bool Aim6DofOn();
/// AZAHARFRAME_KIU_SHOTS_6DOF with first person on, or Angel's Aim (live). Any thread.
bool Shots6DofOn();

/// The scene camera and its projection as the game's memory has them now (the camera of the
/// game frame before, at a VBlank). `ok` false: not readable, or not a camera.
struct Scene {
    bool ok = false;
    Input::KiuRoom::GameCamera camera;
    Input::KiuRoom::Projection projection;
};
Scene ReadScene(const Input::KiuHooks::Reader& mem);

/// The hand's ray in the game's world for `sample` and `scene` (the room <-> game transform).
struct WorldRay {
    bool ok = false;
    Input::KiuRoom::Vec3 origin;        ///< world
    Input::KiuRoom::Vec3 from_eye;      ///< the origin from the scene's eye, world axes
    Input::KiuRoom::Vec3 direction;     ///< world, unit
};
WorldRay HandRay(const Sample& sample, const Scene& scene);

/// AZAHARFRAME_KIU_AIM_6DOF: the reticle pixel that makes the game aim where the hand points,
/// for the aim object at `aim` (its screen `screen`): the ray followed to the depth of the
/// game's own aim point (the aim object's world position: its fixed range, or what the reticle
/// is on) and seen through the scene camera. Emulation thread.
struct AimNow {
    bool valid = false;
    Input::KiuRoom::Pixel pixel;
    bool ahead = false;              ///< the point is in front of the game's camera
    float depth = 0.0f;              ///< the game's aim point's depth, as used
    Input::KiuRoom::Vec3 point;      ///< where the ray is at that depth (world)
    Input::KiuRoom::Vec3 game_point; ///< the game's own aim point (world), as read
    float aspect = 0.0f;             ///< the shape aimed with (KiuRoom::AimShape, or its fallback)
    int aspect_readings = 0;         ///< how many readings of the game's own aim made it
};
AimNow AimThroughCamera(const Input::KiuHooks::Reader& mem, uint32_t aim,
                        const Input::KiuRoom::ReticleScreen& screen, const Sample& sample,
                        bool feeds_shape = true);
/// `feeds_shape` false (the trace): the shared measured shape is read, never updated or reset,
/// so tracing does not change the aim.

} // namespace FrameXr::Kiu::FirstPerson
