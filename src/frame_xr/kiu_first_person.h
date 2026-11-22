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
//                                     AzaharFrame kiuview: an over-the-shoulder view in first
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
#include <cmath>
#include <cstdint>
#include <string_view>
#include "frame_xr/input/kiu_room.h"
#include "frame_xr/input/kiu_turn.h"
#include "frame_xr/kiu_scan.h"

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
/// Angel's Aim is in effect: the stored mode is 4, the game is the European one, its code is the
/// one the hooks are mapped for (checked by the hooks: SetGameCode) and the game is on view in
/// VR (`in_vr`: in Flat no Immersive Mode does anything, nor does this one).
constexpr bool AngelsAimFor(uint32_t stored_mode, uint64_t title, bool code_ok, bool in_vr) {
    return stored_mode == kModeAngelsAim && title == Input::KiuTurn::kTitleEur && code_ok && in_vr;
}
/// First person is on: Angel's Aim in effect, or the developer switch.
constexpr bool OnFor(bool dev_switch, uint32_t stored_mode, uint64_t title, bool code_ok,
                     bool in_vr) {
    return dev_switch || AngelsAimFor(stored_mode, title, code_ok, in_vr);
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
/// AzaharFrame kiu130: the over-the-shoulder view is in effect (live): first person on, and Over
/// the Shoulder's setting (vr_kiu_over_shoulder) or AZAHARFRAME_KIU_SHOULDER (ShoulderFor: the
/// switch, when set, wins), with no AZAHARFRAME_KIU_EYE_OFFSET (which wins over both). The game
/// draws Pit with his own weapon there, so the weapon in hand is left out of the image
/// (Vis::WeaponModelDrawnFor). Any thread.
bool ShoulderOn();
/// AzaharFrame kiumode: Angel's Aim asked for (WantedFor; live). Any thread.
bool Wanted();
/// First person is on and `title` is the game it is for (the European game). Any thread.
bool ForTitle(uint64_t title);
/// Mode 3's registers for this game (the settings' View Matrix Register and Eye Indicator
/// Register), found to be right for it on the desktop (KIU_HOOKS.md).
inline constexpr int kViewRegister = 90;
inline constexpr const char* kEyeIndicator = "87,2";
/// AzaharFrame kiumode: the hooks, each VBlank: the running game and whether its code is the
/// one they are mapped for (false: not checked, or not that code).
void SetGameCode(uint64_t title, bool code_ok);
/// AzaharFrame kiumode: the hooks, each VBlank: the two registers supplied to the renderer for
/// this run (in memory only; Settings::RunSuppliedSetting), or the settings' own again.
void SupplyRegisters(bool on);
/// The frontend, when a game boots, before its presets are applied: forgets the last run's code
/// check and registers. Writes no setting (AzaharFrame kiumode).
void OnGameBoot();
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
struct BottomNow {
    BottomScreen mode = BottomScreen::Off;
    float scale = 1.0f; ///< Bottom Screen Size
    float alpha = 1.0f; ///< Bottom Screen Opacity
    Talk place{};       ///< Persistent: its default place (FP_TALK_AT) and width (scaled)
    float right_deg = 0.0f, down_deg = 0.0f; ///< Persistent: the grip's offset (settings)
};
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
/// AzaharFrame kiutoggles: Over the Shoulder (Settings::values.vr_kiu_over_shoulder) puts the eye
/// this far behind and above first person's: Pit drawn solid (the game fades him at 2 m back or
/// less with 0.5 m up; KIU_HOOKS.md, kiufp3).
inline constexpr float kShoulderSettingBackM = 2.5f;
inline constexpr float kShoulderSettingUpM = 0.5f;
/// The over-the-shoulder offset in use: the switch's text when it is set (ParseShoulder: "0" or
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
