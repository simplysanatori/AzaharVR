// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuhook: the app's own patches to Kid Icarus: Uprising's original code, in the
// game's memory, a test build behind test switches only (no setting, no row, no text in the
// app). The European game (0004000000030200) with the expected code words; anything else stays
// untouched and says why once. All switches are live (read at each VBlank):
//
//   AZAHARFRAME_KIU_NOCULL=1          the game's two render culls pass everything, so what is
//                                     outside the game camera's view is still drawn.
//   AZAHARFRAME_KIU_BULLET_KEEP=1     bullets that leave the game camera's view live on (the
//                                     game ends them in air battles).
//   AZAHARFRAME_KIU_EYE=1             first person from the game's side: the scene camera's eye
//                                     is the player's position plus an offset, land and air; the
//                                     view direction stays the game's.
//   AZAHARFRAME_KIU_EYE_OFFSET=x,y,z  that offset in the player's own axes (default 0,12.3,0).
//   AZAHARFRAME_KIU_HIDE_PLAYER=1     the player's model shrunk away (a scale write; back when
//                                     off).
//   AZAHARFRAME_KIU_SHOT_TEST=yaw,pitch[,dx,dy,dz]
//                                     the player's shots turned by that many degrees from the
//                                     game's aim (yaw about the world's up axis, pitch up about
//                                     the game camera's level side axis) and, with the last
//                                     three, started from the player's position plus that offset
//                                     (side, up, ahead of the game camera's level view).
//   AZAHARFRAME_KIU_SHOT_NO_LOCK=1    with SHOT_TEST: the shots get no lock target (no homing).
//   AZAHARFRAME_KIU_ENEMY_KEEP=1      flying enemies whose scripted path ends fly it again
//                                     instead of vanishing (they come round and keep shooting).
//   AZAHARFRAME_KIU_ENEMY_KEEP_S=<seconds>
//                                     the same until an enemy has been on its path that long,
//                                     then it leaves as in the game; replaces ENEMY_KEEP.
//   AZAHARFRAME_KIU_GROUP_KEEP=1      land spawner groups are not despawned as the player moves
//                                     on.
//   AZAHARFRAME_KIU_NO_FOLLOW=1       the land camera no longer takes back part of a turn while the
//                                     player runs (its eased follow gets no movement input).
//   AZAHARFRAME_KIU_HOOK_TRACE=1|2    a status line a second (2: one a VBlank): the groups, the
//                                     player, the scene eye.
//
// The pure part (the patch table, the stubs' image, the mailbox, the rules) is
// input/kiu_hooks.h; the addresses and the stubs' source are in input/KIU_HOOKS.md and
// tools/kiu_hooks/. Here: the emulation thread's side, at each VBlank after Direct Aim's and
// Right Stick turning's (common/frame_emu_poll.h), in VR and in Flat.

#pragma once

namespace FrameXr::Kiu::Hooks {

/// The frontend, when Kid Icarus: Uprising boots: the emulation thread's hook is set (once). It
/// costs a few reads a VBlank while that game runs and writes nothing unless a switch is on, or
/// a save state brought our patches back with every switch off (then they are taken out).
void OnGameBoot();

} // namespace FrameXr::Kiu::Hooks
