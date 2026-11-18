// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuvr: Kid Icarus: Uprising in first person in the Full (6DoF) view, a test build
// behind test switches only (no setting, no row, no text in the app). European game only (the
// game-code hooks it stands on, kiu_hooks.h, are mapped for that one):
//
//   AZAHARFRAME_KIU_FIRST_PERSON=1    together: the scene's eye at the character's (the eye
//                                     hook); the world at life size (the leaning scale below);
//                                     the render culls off, the land camera's follow off and
//                                     Right Stick turning on (the switches AZAHARFRAME_KIU_EYE,
//                                     _NOCULL, _NO_FOLLOW and _RIGHT_STICK_TURN, as if set):
//                                     all live. And, read once as the game boots: the run plays
//                                     in mode 3 ("Full (6DoF): Other Games") with this game's
//                                     registers (view 90, eye indicator 87,2), whatever its
//                                     settings say. That is set in memory only, as a preset
//                                     sets it: it reaches the game's own settings file only if
//                                     they are saved during the run (the in-game menu saves
//                                     them when a row is changed); then Immersive Mode has to
//                                     be turned Off for the game again by hand. With the switch
//                                     turned off while the game runs, the run stays in mode 3
//                                     (the game's own third person camera, head tracked).
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
//                                     the ray, each with its own scatter, the lock target kept;
//                                     bullets out of the game camera's view live on (as
//                                     AZAHARFRAME_KIU_BULLET_KEEP).
//   AZAHARFRAME_KIU_FP_TRACE=1|2      a line a second (2: a VBlank) with the transform's inputs
//                                     and what it gives (kiu_hooks.cpp).
//   AZAHARFRAME_KIU_FP_PROBE=<hex>    with the trace: the actor at that address, where it is in
//                                     the world and in the head's view.
// All live. The transform is input/kiu_room.h (pure, unit tested); the notes are in
// input/KIU_HOOKS.md ("First person in 6DoF").
//
// Threads: the XR thread publishes what mode 3 draws with (the rest pose, the head, the scale;
// immersive.cpp) and the aiming hand's ray (controls.cpp); the emulation thread's hook
// (kiu_hooks.cpp) takes the latest of both at each VBlank.

#pragma once

#include <cstdint>
#include "frame_xr/input/kiu_room.h"

namespace FrameXr::Kiu::FirstPerson {

/// The switch is on (live). Any thread.
bool On();
/// The switch is on and `title` is the game it is for: mode 3 is used for it. Any thread.
bool ForTitle(uint64_t title);
/// Mode 3's registers for this game (the settings' View Matrix Register and Eye Indicator
/// Register), found to be right for it on the desktop (KIU_HOOKS.md).
inline constexpr int kViewRegister = 90;
inline constexpr const char* kEyeIndicator = "87,2";
/// The frontend, when Kid Icarus: Uprising boots (after its settings are loaded, before it
/// runs) with the switch on: Immersive Mode 3 and the two registers for this run.
void OnGameBoot();
/// The leaning scale in use, game units per metre: life size, or the override. Any thread.
float UnitsPerMetre();

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

/// AZAHARFRAME_KIU_AIM_NO_MOD, or AZAHARFRAME_KIU_AIM_6DOF (live). Any thread.
bool AimNoModWanted();
/// AZAHARFRAME_KIU_AIM_6DOF with first person on (live). Any thread.
bool Aim6DofOn();
/// AZAHARFRAME_KIU_SHOTS_6DOF with first person on (live). Any thread.
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
                        const Input::KiuRoom::ReticleScreen& screen, const Sample& sample);

} // namespace FrameXr::Kiu::FirstPerson
