// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuturn: Right Stick turning in Kid Icarus: Uprising's land battles, a test build
// behind test switches only (no setting, no row, no text in the app):
//
//   AZAHARFRAME_KIU_RIGHT_STICK_TURN=1   Right Stick mode. In a land battle the right stick's x
//                                        turns the camera, the game gets a centred C-Stick,
//                                        Direct Aim no longer pauses for the stick and the Turn
//                                        Zone is skipped; the stick's y does nothing. Air
//                                        battles and everything else as without it.
//   AZAHARFRAME_KIU_SNAP_TURN=30|45|60|90  Snap Turn by that angle (0 or unset: smooth).
//   AZAHARFRAME_KIU_TURN_DPS=<deg/s>     smooth turning's rate at full tilt and Horizontal Turn
//                                        Speed 1.0x (default 90).
//   AZAHARFRAME_KIU_CAM_TRACE=1          the camera's probe log, and every 10 s how many writes
//                                        were applied and lost (works with the mode off too:
//                                        then it only reads).
// All four live (read each time). The pure part, the addresses and the guards are in
// input/kiu_turn.h; the route in input/MAPPING.md ("Right Stick turning").
//
// The XR thread publishes the right stick (Publish) and asks whether a land battle runs
// (LandActive) to centre the C-Stick. The emulation thread, at each VBlank (after Direct Aim's
// write, common/frame_emu_poll.h), finds the camera from the game's own code, keeps the turn owed
// and writes it into the camera's yaw input; at each pad update it only traces. Only EUR
// (0004000000030200) with the expected code words; anything else stays off and says why once.

#pragma once

namespace FrameXr::Kiu::Turn {

/// AZAHARFRAME_KIU_RIGHT_STICK_TURN=1 (live).
bool RightStickOn();
/// The mode or its trace is on: the XR thread publishes the stick.
bool Wanted();
/// XR thread, each display frame of a game in VR with the menu closed: the right stick's x
/// (after the mapper's dead zone and the other gamepads) and Horizontal Turn Speed. Installs the
/// emulation thread's hook the first time.
void Publish(float stick_x, float turn_speed);
/// XR thread: the game's input stops (the menu opens, the game ends, VR stops): no stick.
void Stop();
/// Any thread: Right Stick mode is on and the last VBlank found a land battle (input/kiu_turn.h
/// Camera::land), within the last 200 ms.
bool LandActive();

} // namespace FrameXr::Kiu::Turn
