# Kid Icarus: Uprising: the app's own game-code hooks (test build)

A test build behind test switches only: no setting, no row, no text in the app. Code:
`input/kiu_hooks.h` (pure: the patch table, the rules, the mailbox, the player),
`input/kiu_hooks_stubs.h` (the stubs' words, generated), `kiu_hooks.{h,cpp}` (the emulation
thread's side), `tools/kiu_hooks/` (the stubs' ARM source and their checks), tests in
`tests/frame_xr/kiu_hooks.cpp`.

Everything here is for the **European game, title 0004000000030200, original code (code.bin md5
b107c9b1)**. Every address is of that build. **V** = seen working in the running game on the
desktop (native build, the real game) or read in the exact code; **I** = inferred.

## What it is

The app changes a few instruction words of the game's code in the emulated memory while the game
runs, and puts three small routines of its own ("stubs") into unused memory of the game. Nothing is
shipped as game files and nothing of the C-Stick & Gyro mod is used or copied; the mod is not
needed.

- **A patch** is one instruction word: `{address, the game's word, ours}`. Patches come in
  **groups**, one per switch; a group goes in or out as a whole, live.
- **The game's memory is the truth.** At each VBlank every site is read again: it holds the game's
  word, ours, or something else. Ours while its switch is off is put back. Something else (another
  build or region, a mod, a cheat) leaves the whole group out, says so once in the log
  (`xr: Kid Icarus hooks: <group>: off: the game's code differs (0x… holds 0x…)`) and is never
  written. Each group also checks a few **context words** around its sites (where it jumps to,
  what a stub assumes of the code after its site). So a save state loaded with or without patches
  is right one VBlank later (V: after a load the game's words are back for less than one game
  frame).
- **Before anything**, the title must be the European one and nine words of the camera update
  must match (`kiu_turn.h`, `CodeMatches`; the USA and Japanese builds differ there).
- **Every code word written is followed by a code cache drop** for it
  (`Core::System::InvalidateCacheRange`, all CPU cores). V on the desktop with each core (the
  x86-64 JIT, the fast interpreter, the old interpreter): a patch takes effect at once and so does
  taking it out. I: the aarch64 JIT of the headset (the same call; not run here).
- **The code cave** is the text segment's unused tail, `0xC95370–0xC96000` (0xC90 bytes; all zero
  in the game, nothing refers to it). The stubs' image goes to `0xC95380` (110 words now), written
  once before the first site that jumps to it and never changed or cleared afterwards (a guest
  thread may be stopped inside it). An image with our tag but other words (another version of the
  app in a save state) is replaced; anything else in the cave keeps the stub groups out.
- **The mailbox** is the cave's last part, `0xC95F00` (0x54 bytes). The app writes it at each
  VBlank (after the image, before any site); the stubs read it. It lives in the cave because the
  cave is read-only and executable for the game (nothing of the game can spoil it) and the
  emulator writes it like any memory. The stubs load its address from a literal and read the
  fields **through a register**, never pc-relative, so no CPU core can take a value for a constant
  (V: a changed offset shows on the next game frame with each core, no code written). The stubs
  never write the cave.

Order of one VBlank's pass (`Sync`): read the sites and context; the image if a stub group goes in
and the cave is empty; the mailbox; then the site words (the groups going out first).

## The player

`aim = [[0xE14504] + 8] − 0x10` (the land aim's class 0xCF1C34 or the air one's 0xCF1BE0),
`owner = [aim + 0x8C]` (class 0xCEAC40), **the player's actor = `[owner + 0x1C]`** (the game's
generic actor class 0xCA046C, named "CHID_PIT" at `[actor + 0x1BC]`), the camera node handed to the
scene `[owner + 0x30]`. The actor: world block at +0x40 (scale, quaternion x y z w at +0x44,
position at +0x54), local block at +0x60 (scale, quaternion +0x64, position +0x74; in the air the
parent is the rail). Every pointer is checked mapped before it is read; the playing flag (u8
0xE17514 = 1) is checked last, so with it down the actor is still known but nothing acts on it.
V on land (Practice Range) and in the air (Chapter 1).

## The mailbox (offsets from 0xC95F00)

| Offset | What | Filled by |
|---|---|---|
| 0x00 | u32 "KIUM" | the framework |
| 0x04 | u32 VBlanks written | the framework |
| 0x08 | u32 the player's actor; 0: none (the eye and shot stubs pass the game through) | the framework |
| 0x0C | u32 the camera node of the player's view | the framework |
| 0x10 | u32 eye flags, bit 0: replace the eye | the eye switch |
| 0x14 | f32 ×3 eye offset, world axes, from the player's world position | the eye switch (the offset in the actor's axes, turned by his quaternion) |
| 0x20 | u32 shot flags: bit 0 rotate, bit 1 move the start, bit 2 no lock target, bit 3 the rotation is the aim itself (with bit 0) | the shot test switch, or first person's shots |
| 0x24 | f32 ×4 shot rotation (quaternion x y z w, spawn space): each bullet's `q` becomes `rotation × q`; with bit 3 `rotation × conj(the game's aim before scatter) × q` | the same |
| 0x34 | f32 ×4 shot frame: the rotation of the player's parent (`world q × conj(local q)`; identity on land) | the framework |
| 0x44 | f32 ×3 shot offset from the player's position, in the spawn's space (world axes on land; the rail's axes in the air, which were the world's in Chapter 1) | the same |
| 0x50 | f32 seconds: a passed flier younger than this flies its path again | the timed enemy keep |

In code: `Mailbox`, `EyeRequest`, `ShotRequest`, `Fill`. First person's shots
(`AZAHARFRAME_KIU_SHOTS_6DOF`, below) fill a `ShotRequest` with `absolute`: the rotation is the
aim wanted, and the stub takes the game's own aim out of each bullet's quaternion itself, at the
spawn, so the app does not have to know the game's aim beforehand.

## The patches

| Switch | Address | The game's word | Ours | What |
|---|---|---|---|---|
| `AZAHARFRAME_KIU_NOCULL=1` | 0x77DB44 | `E20100FF` and r0, r1, #0xFF | `EA000040` b 0x77DC4C | the scene model's cull loop → its own "wholly in view" exit |
| | 0x773294 | `E1A01004` mov r1, r4 | `EAFFFFF9` b 0x773280 | the object cull's plane test → its own "visible" exit |
| `AZAHARFRAME_KIU_BULLET_KEEP=1` | 0x41FF1C | `EB0331C7` bl 0x4EC640 | `E3A00001` mov r0, #1 | a flying bullet's "in the camera's view?" always yes |
| | 0x40BD6C | `EB038233` bl 0x4EC640 | `E3A00001` | the pooled rapid bullet's |
| `AZAHARFRAME_KIU_EYE=1` | 0x63409C | `E92D4070` push {r4, r5, r6, lr} | b eye_stub | the camera hand-over's entry |
| `AZAHARFRAME_KIU_SHOT_TEST=…` | 0x5043E4 | `EBFE0493` bl 0x485638 | bl shot_stub | the player's shot spawn |
| `AZAHARFRAME_KIU_ENEMY_KEEP=1` | 0x3E8C9C | `0A000017` beq 0x3E8D00 | `0A00000E` beq 0x3E8CDC | a flier's path end: vanish → fly the path again |
| `AZAHARFRAME_KIU_ENEMY_KEEP_S=<s>` | 0x3E8C9C | `0A000017` | beq keep_stub | the same, by the enemy's age (replaces the plain keep; they share the site) |
| `AZAHARFRAME_KIU_GROUP_KEEP=1` | 0xA430FC | `E92D4FF8` push {r3-r11, lr} | `E12FFF1E` bx lr | a land spawner group's despawn does nothing |
| `AZAHARFRAME_KIU_NO_FOLLOW=1` | 0x70401C | `EB031897` bl 0x7CA280 | `E1A00000` mov r0, r0 | the land camera's eased follow no longer turns the camera while the player runs (what the call computes: I) |
| `AZAHARFRAME_KIU_AIM_NO_MOD=1`, `AZAHARFRAME_KIU_AIM_6DOF=1` ("aim hold") | 0x6D215C | `0A000002` beq 0x6D216C | `EA000002` b 0x6D216C | the land reticle's eased moves (back to its rest height after a touch, the flick's glide) do nothing, as in the air |
| | 0x6D1680 | `0A00001F` beq 0x6D1704 | `EA00001F` b 0x6D1704 | the aim box is the whole screen on the ground too (the air's box), so the game's clamp never turns the camera for the reticle |

"Aim hold" is for the game **without** the C-Stick & Gyro mod only: its context holds three words
of the reticle's code where the mod has its hooks (0x6D219C, 0x6D280C, 0x6D2F7C), so with the mod
the group stays out (`aim hold: off: the game's code differs (0x006D219C holds …)`, once) and the
mod's own route aims (below, "The game's aim").

The words at these sites, their context words and the cave are the same in the C-Stick & Gyro
mod's v172 EUR base and 21:9 builds (V by comparing the files; the 16:9 build was not at hand: I),
so the hooks go in with the mod installed too. That combination has not been run (I). With the mod,
`NOCULL`'s second word jumps past the mod's own hook at 0x7732D0 (inside the same function), which
then does not run while the switch is on.

## The stubs (source: `tools/kiu_hooks/stubs.s`)

ARM, ARMv6K + VFPv2, no relocations: every address outside the image is a literal at its end, so
the same words work wherever the image is. `tools/kiu_hooks/gen.sh` assembles the source and writes
`kiu_hooks_stubs.h` (each word with its instruction as a comment); `tools/kiu_hooks/check.sh`
assembles it again, compares, and runs the same instructions on a test bench under qemu-arm (the
maths against C, every register, sp and lr kept, each guard). Neither is part of the build.

**eye_stub** (0x63409C → `b eye_stub`). The function hands a camera's transform to the scene once
per game frame, on land and in the air: r0 = the camera node, r2 = the transform (quaternion at +4,
position at +0x14). The stub runs the replaced `push`, and when the eye flag is set, the node is
the player's and the actor still has the actor's class, replaces the position by **the actor's
world position (read then) + the mailbox offset**; then goes on at 0x6340A0. The orientation stays
the game's. It reads the actor itself because a position computed at a VBlank is one game frame
old (in the air the rail moves about 21 units a frame): V, in the air the scene's eye minus the
player's position minus the offset is under 0.05 at every VBlank (the remainder is the offset's
turn being one frame old). Free registers: r1, r12, s0–s6 (the function writes them before it reads
them), r4 and r5 after the push.

**shot_stub** (0x5043E4 → `bl shot_stub`, in place of `bl 0x485638`, the bullet spawn). At the
call: r2 = the owner's actor, s0 a float argument, `[sp+4]` = quaternion* (x y z w; forward = it
applied to +Z), `[sp+8]` = position*, `[sp+0xC]` = the lock target's handle. Only when r2 is the
mailbox's player (so other players' and enemies' shots pass untouched): the quaternion becomes
`rotation × q` (each bullet's own, so scatter and fans stay); the position becomes
**frame applied to the player's local position (read then) + the offset**; the target handle
becomes "none" (0xFFFFFE00) if asked. Then `0x485638` with every register, sp and lr as the game
set them (all it uses is saved: r4, r5, d0–d7). With flag bit 3 ("absolute") the quaternion becomes
`rotation × conj(base) × q`, `base` being the caller's own aim quaternion before each bullet's
scatter: the four floats at the caller's `sp + 0x70` (0x503F8C's frame: the fourth argument of the
shooter's aim call at 0x504014, which 0x7019A0 fills with the result of 0x4C14B0 before it adds
the weapon's tilt and the scatter; V in the code, and by the result: below). The start is taken from the player in the stub for
the same reason as the eye: in the air his local position moves about 3 units forward as he fires
(V: the start matches his position of the spawn's frame to 0.0000; a sample from before the shot
is 3.0 away).

**keep_stub** (0x3E8C9C → `beq keep_stub`). Inside the function that ends a flying enemy's
scripted path (0x3E8C34), where the placement has no "fly it again" flag: r4 = the path state.
While float `[state + 0x34]` (seconds since the enemy entered its path state; the game keeps
counting across restarts) is less than the mailbox's limit: the function's own restart code
(0x3E8CDC); else its vanish code (0x3E8D00), as the game. Uses r12, s0, s1 and the flags only.

## The switches (all live; `kiu_hooks.h`)

| Switch | Does |
|---|---|
| `AZAHARFRAME_KIU_NOCULL=1` | the two render culls pass everything |
| `AZAHARFRAME_KIU_BULLET_KEEP=1` | bullets out of the game camera's view live on |
| `AZAHARFRAME_KIU_EYE=1` | the scene camera's eye at the player; the view direction the game's |
| `AZAHARFRAME_KIU_EYE_OFFSET=x,y,z` | the eye's offset in the player's own axes (default 0,12.3,0) |
| `AZAHARFRAME_KIU_HIDE_PLAYER=1` | the actor's local scale 1 → 0.001 (a data write; exactly 0.001 goes back to 1 when off, or when not playing) |
| `AZAHARFRAME_KIU_SHOT_TEST=yaw,pitch[,dx,dy,dz]` | the player's shots turned by that many degrees from the game's aim (yaw about the world's up axis: positive is to the left; pitch up about the game camera's level side axis) and, with the last three, started at the player's position + dx to the side (positive: to the right), dy up, dz ahead of the game camera's level view |
| `AZAHARFRAME_KIU_SHOT_NO_LOCK=1` | with SHOT_TEST: no lock target (no homing) |
| `AZAHARFRAME_KIU_ENEMY_KEEP=1` | passed fliers fly their path again, for good |
| `AZAHARFRAME_KIU_ENEMY_KEEP_S=<seconds>` | the same until an enemy is that old; replaces ENEMY_KEEP |
| `AZAHARFRAME_KIU_GROUP_KEEP=1` | land spawner groups are not despawned |
| `AZAHARFRAME_KIU_NO_FOLLOW=1` | the land camera no longer takes back part of a turn while the player runs |
| `AZAHARFRAME_KIU_HOOK_TRACE=1` (or 2) | a status line a second (2: one a VBlank): `xr: Kid Icarus hooks: t … [groups] player … scene eye …` |

The hook is set when Kid Icarus: Uprising boots (`citra_qt.cpp`, `OnGameBoot`) and runs at each
VBlank after Direct Aim's and Right Stick turning's (`common/frame_emu_poll.h`, `RunPatch`), in VR
and in Flat. In Flat it also reads the switches file again once a second (no XR thread does there).
With no switch on it reads the sites each VBlank and writes nothing, unless a save state
brought our words back.

## What was seen (desktop, the real game, 2026-11-17)

- **Framework**: V with each CPU core (apply, take out, the mailbox live).
- **NOCULL**: V on land (Practice Range: the game camera turned 0.9 rad from the dummy, the picture
  turned back with the emulator's free camera: the dummy is drawn with the switch on, not with it
  off, four alternations; the game's own view unchanged). **Not shown in the air**: in Chapter 1
  the Monoeyes in a picture turned 40° and 60° were drawn with the switch off too, and at 40–120°
  pictures with it on never differed from pictures with it off by more than two runs of the same
  setting differ.
- **BULLET_KEEP**: V in the air: a charged shot turned 150° lives 8 game frames (109 units) with
  the switch off and its whole range (24 frames, 753) with it on; turned 90°: 8 frames against 25.
  A shot in view is unchanged. The pooled rapid bullet's site: in place, not exercised (I).
- **EYE**: V land and air (the scene's eye = the player + the offset; offsets live; no frame of
  lag). Not looked at: cutscenes, pauses, deaths (the playing flag drops and the game's camera
  returns: I).
- **HIDE_PLAYER**: on land the model and weapon go and **the shadow stays** (V). **In the air it is
  not usable**: the picture is washed out by a bright haze (mean brightness 165–171 → 229–231, four
  of four), a small piece of the model stays, and **the player takes less damage** from the same
  Monoeye wave (health bar 87 → 62 hidden against 87 → 55, three runs each; that his hurt boxes
  shrink with the scale is the likely reason: I). No shadow was seen in the air pictures either
  way (nothing is under him there). Land hits were not tested (the Practice Range's dummy does
  not attack).
- **SHOT_TEST**: V. Land, charged: start exact, direction within 0.01°; with the lock kept the shot
  homes from its new line; a shot started in range hits (the range's own counter: 29 damage; 0 for
  the same shot turned 25° without a lock). Land, rapid: 23 bullets start at the asked point and
  keep their scatter to 0.1° each. Air, charged: direction within 0.2° of the control's turned,
  start exact in rail space; with the lock kept a shot from the moved start bends to its target
  (130–169 units off its first line, two runs) and flies straight without it. Air, rapid: pictures
  only (their records were not found).
- **ENEMY_KEEP / _S**: V in the air (Chapter 1, idle player, 75 s): enemies alive at once 7 in
  control, 25 with ENEMY_KEEP (the player dies after about 47 s), 13 with `_S=12` (alive at 75 s),
  19 with `_S=25` (dies after about 52 s); new enemies keep arriving at the control's pace; no
  crash.
- **GROUP_KEEP**: the word goes in and out and the game ran with it (V); **never seen to act**. In
  Chapter 1's land section (reached by a scripted run of the air section) two scripted walks
  provoked no group despawn: three Monoeyes appeared and were gone within 4 s with and without the
  switch (the player was firing), then he stood at a wall. What it does to the level's progress is
  not known (I).
- **NO_FOLLOW**: V (Practice Range, and Chapter 1's land section for the 45° case; camera mode 1
  in both). The turn is the battle camera's eased follow (0x7CAFC8, called at 0x7041D0 with the
  block cam+0x4AC; 0x7CA280, called at 0x70401C with that block, was not read: by its effect it
  gives the follow the player's movement, I). Found by replacing each of the update's calls in
  turn. In Chapter 1's land section, running to the side, the follow ADDS to a written 45° turn
  (67° a second later); with the switch 45.00° holds. Control: running forward alone turns the yaw 5.5° in 4 s; a 45° turn written
  while running keeps 26.6° (90°: 60.4°); a smooth turn while running gets 68 % of what was asked.
  With the switch: 0.00°, 45.00° and 90.00° kept, 100 %. Standing and running sideways or back the
  game's camera did not turn in either case, and the pitch still re-levels (the follow's own update
  still runs; replacing that call instead stops the pitch re-level too). The third-person view
  stays usable; what it loses is the camera's own turn while the player runs. Not checked: a
  lock-on, camera modes 0 and 2, event cameras, and whatever else reads cam+0x4AC.

## Not done, not known

- The headset: nothing here ran on it (the aarch64 build compiles).
- Other regions and game versions: no table (the title and code checks leave everything off).
- Multiplayer: not run. The shot stub compares the owner with the local player's actor (found
  through the local aim object), so other players' shots should pass untouched (I).
- (Settled, "Shots from the hand" below: the game's own aim quaternion at a shot is read by the
  stub from the caller's frame.)
- Shots outside the one spawn call (dash shots, melee, powers) are not hooked.
- Whether the rail's axes always are the world's (they were in Chapter 1's air section).
