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
| 0x54 | u32 the scene camera node to widen (0xE14B88); 0: none (the camera stub passes the game through) | the wide camera |
| 0x58 | f32 the angle to give it, up and down (radians) | the same |
| 0x5C | f32 the shape to give it (across / up and down) | the same |
| 0x60 | f32 the game's own shape, kept to put back (a record, not read by the stub) | the same |

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
| first person in a battle ("wide camera") | 0x45E04C | `EDD00A0F` vldr s1, [r0, #0x3C] | b fov_stub | the scene camera's setter: the camera named in the mailbox gets our angle and shape |
| `AZAHARFRAME_KIU_FIRST_PERSON=1` ("player shadow") | 0x57B054 | `E92D4030` push {r4, r5, lr} | b shadow_stub | the player's own round shadow is not put in the frame's shadow list; everyone else's is |

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

**shadow_stub** (0x57B054 → `b shadow_stub`; AzaharFrame kiushadow). The first instruction of
the function that puts an actor's round shadow on the ground for this frame, entered by `bl` from
the actor's movement states: r0 = the state, the actor at `[r0 + 0x30]`. When that actor is the
mailbox's player the stub returns at once (`bx lr`), so his shadow is not in the list; otherwise
it runs the replaced push and goes on at 0x57B058. No player in the mailbox: every shadow, as the
game. Uses r2, r12 and the flags only (the function writes r2 before reading it and never reads
r12; its own early exit changes r2 and the flags too).

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

## First person in 6DoF (AzaharFrame kiuvr; test switches)

Code: `input/kiu_room.h` (pure: the room ↔ game transform, the reticle's projection, the shots'
aim; tests in `tests/frame_xr/kiu_room.cpp`), `kiu_first_person.{h,cpp}` (the switches, what the
XR thread hands over, the scene camera), `kiu_hooks.cpp` (the VBlank pass), `kiu_probe.cpp`
(Direct Aim's writer). The mock runs behind every **V** below were the real game, native desktop
build, VR on the mock runtime with scripted head and controller poses.

### Angel's Aim (AzaharFrame kiumode)

First person is Immersive Mode's **Full (6DoF): Kid Icarus: Uprising / Angel's Aim**, stored as 4
and listed above Other Games (3, unchanged) in every Immersive Mode list (the VR menu, the game's
Properties and the global settings). It is drawn as mode 3 by every reader (`Settings::ImmersiveRenderMode`). In
VR (not in Flat), on the European game whose code the hooks check, it turns on with first person the eye hook, the
culls off, the follow off, Right Stick turning, the land aim box, the aim from the hand with
Direct Aim (`_AIM_6DOF`), the shots from the hand and the bullet keep (`_SHOTS_6DOF`), the timed
enemy keep (12 s unless `_ENEMY_KEEP_S` says otherwise), the screen at true size and life size;
it supplies View Matrix Register 90 and Eye Indicator `87,2` to the renderer in memory only
(`Settings::RunSuppliedSetting`), so a save during play stores mode 4 and nothing else. Another
mode chosen during play turns it off at the next VBlank. Other regions and other code play it as
Other Games; the hooks' log line says why. The switches below still force their parts on.

### The switches

| Switch | Does |
|---|---|
| `AZAHARFRAME_KIU_FIRST_PERSON=1` | the developer override of Immersive Mode's Angel's Aim (below): first person on in whatever mode the game runs (choose a Full mode), live: the eye hook at the character's eyes, the culls off, the land camera's follow off, Right Stick turning on, the world at life size, and on the European game the registers 90 and `87,2` supplied in memory. It sets no mode and writes no setting (AzaharFrame kiumode; it used to set mode 3 and the registers at boot) |
| `AZAHARFRAME_KIU_WORLD_SCALE=<u>` | the leaning scale, game units per metre (0.5 to 100), over life size |
| `AZAHARFRAME_KIU_PLAYER_EYE_M=<m>` | the player's eye height for life size (default 1.6 m: the character's eyes, 12.3 units up, are that high: 7.69 units a metre) |
| `AZAHARFRAME_KIU_AIM_NO_MOD=1` | Direct Aim's reticle writer on the game without the mod ("The game's aim" below) |
| `AZAHARFRAME_KIU_AIM_6DOF=1` | with first person and Direct Aim (Aiming: Direct Aim): the reticle where the hand's ray points in the game's world, through the game's camera |
| `AZAHARFRAME_KIU_SHOTS_6DOF=1` | with first person: the game's own shots from the hand's ray; turns the bullet keep on |
| `AZAHARFRAME_KIU_FP_WIDE=0\|1\|2` | first person's scene camera (default 1): 1, as wide as the view and in its shape (the screen then needs no drawing past the frame); 2, as wide but in the game's own shape (square pixels; some of the picture falls outside the view across); 0, the game's own camera and the old draw-past-the-frame path ("The wide camera" below) |
| `AZAHARFRAME_KIU_FP_2D=0\|1` | first person's sprites (default 1 in first person): the game's lock-on marks, hit numbers and other sprites drawn through its geometry shader turn with the world; 0 leaves them where the game's camera put them ("The game's sprites" below) |
| `AZAHARFRAME_KIU_FP_TALK=0\|1`, `AZAHARFRAME_KIU_FP_TALK_AT=<down>,<width>` | first person's talk panel (default 0; 1 shows it): the Secondary Screen, where this game shows its talk, as a small panel with the head, its middle 24° below straight ahead, 34° wide, 0.8 m away (degrees; "The talk and the HUD in first person" below) |
| `AZAHARFRAME_KIU_FP_HUD=0\|on\|<p>\|<across>,<up_down>` | HUD Spacing's scales in first person while the setting is the game's own (default off; an experiment, see below) |
| `AZAHARFRAME_KIU_FP_FIELD=<percent>` | the share of the view's angle the picture fills (50 to 100, default 100): the wide camera that much narrower, the screen still at true size, a dark border around it (unit tested; on the mock, set live at 80 on land, no new screen line was logged: unproven) |
| `AZAHARFRAME_KIU_FP_EDGE=<percent>` | the picture's edge fades to black over that share of its half size (0 to 50, default 0); the projection path only (only the shader compile test: never drawn on the mock) |
| `AZAHARFRAME_KIU_SHOULDER=<back>,<up>[,<side>]` | over the shoulder: the eye this many metres behind, above and to the right of the first-person eye along the camera's level view (game units = metres × the leaning scale, 7.7 per metre at life size); `AZAHARFRAME_KIU_EYE_OFFSET` wins |
| `AZAHARFRAME_KIU_FP_FACTOR=<f>` | the most the game draws past its own frame on either axis in first person (1 to 8, default 6); past it the screen is smaller on that axis ("The screen in first person" below) |
| `AZAHARFRAME_KIU_FP_TRACE=1\|2`, `AZAHARFRAME_KIU_FP_PROBE=<hex>` | a line of `key=value` words a second (2: a VBlank): the scene camera, the view register, the rest pose, the head, the hand's ray in the world, the reticle, the mailbox; and an actor's place in the world and in the head's view |

### The transform (`kiu_room.h`)

Mode 3 draws through `view'(x) = Rc⁻¹ · (V · x − s · h)`: `V` the game's view matrix, `h` and
`Rc` the head's position and rotation in the rest frame (the head's place and heading when the
Full view started or was re-centred, level), `s` the leaning scale. So **the rest frame, scaled
by `s`, is the game camera's frame**: a point `p` of the room (rest frame, metres) is the world
point `eye + Qcam · (s · p)`, a direction `d` is `Qcam · d`. Everything is taken from the scene
camera (0xE14B90 eye, 0xE14B9C look-at, 0xE14BA8 up): the only camera there is on land and in the
air. What is made from it is an offset from the eye and a direction, never an absolute position:
the eye itself is the player plus the eye offset, which the stubs read at the frame they run in,
so the rail's 21 units a frame never enter. The camera read at a VBlank is the game frame
before's: while it turns (the rail's curves; the shake the game gives its camera when the player
fires or is hit: up to 1.8° from one VBlank to the next in the mock runs) the hand's ray in the
world is one frame old.

- **The view register (V).** For this game register 90 holds the camera's rotation only: its
  translation column is 0, the game draws everything relative to the scene's eye. The register's
  axes are the scene camera's (0.0000° apart at every VBlank of the land runs). The eye indicator
  `87,2` reads both eyes (293 of 293 two-eye swaps).
- **Why the mode is a real Immersive Mode (AzaharFrame kiumode):** besides the XR thread, the
  3D slider given to the game (`core/hle/kernel/shared_page.cpp`, `service/hid`) and the internal
  resolution read the Immersive Mode setting. With only the XR side in mode 3 the game kept its
  own stereo: each eye's picture was shifted 51 px (1600 wide) the other way, the same at every
  depth. A stored 4 is on mode 3's side of every one of them.
- **The world holds still (V, land).** The Practice Range's dark floor line along world x = 0,
  in both eyes' pictures (2400 × 1440, 27.3 px a degree), against the line through the projected
  world points computed outside the app from the trace's raw values: head yawed 20°, −25°,
  pitched 15°, yaw 30° with pitch −10°, moved 0.30 m right, 0.20 m up, 0.40 m forward, and a
  mixed pose: mean 0.4 to 1.3 px, worst 5.7 px (0.21°). The same with the C-Stick & Gyro mod's
  21:9 build, with the picture's shape as the app measures it from the game's draws (2.333): mean
  0.3 to 0.9 px, worst 2.1 px. The range's dummy is no fixed point (it turns and bobs: its middle
  is up to 1° from where its actor's position would be).
- **The stick turns the world, not the head (V, land).** The right stick held right for 0.25 s:
  the game camera's yaw −22.56° (90 °/s), the head's pose in the trace unchanged, and the floor
  line where the turned camera puts it (mean 0.6 to 1.2 px, worst 2.1).
- **In the air: I for the picture.** The same code draws it; the register's axes are the scene
  camera's there too, and the eye is the player's (the hook lane's V). No still thing was found
  to measure in the air pictures.
- **Life size.** `s` = 12.3 / the player's eye height: the floor under the rest pose is the
  character's feet (unit test). First person leans by this scale instead of the one World Scale
  and the game's 3D give, saves nothing for the game and skips World Scale's migration.
- **Not rigid when the screen is resized or moved.** The Full view's screen at its default size
  and place gives gains 1 (the trace's `gains=`): only then is the head's rotation the game
  camera's. Horizontal or Vertical Scale as Stretch, a grabbed and resized screen or a pitched
  one give the game `Layout::GameCameraRotation` instead, and the hand's ray no longer matches
  the picture. Render Margin and the scales as Field of View only draw more around: fine.

### The game's aim (V unless marked; the game without the mod)

The aim object (`aim = [[0xE14504] + 8] − 0x10`, the land or the air class) is the reticle and a
node of the scene:

- `+0xA4, +0xA8` the reticle's pixel in a screen of `+0xE0` × `+0xE8` (400 × 240) with its
  middle at `+0xE4, +0xEC`.
- `+0x54` its world position: **the game's aim point**, which the game puts each frame on the
  scene camera's ray through the pixel, at a fixed depth along the camera's view (500.6 units on
  land, 1500 in the air) or on what is under the reticle (74.7 units with the reticle on the
  Practice Range's dummy from 95 units). The pixel of a world point is the scene projection's:
  `x = cx + (w / 2) · X / (Z · tan(fov / 2) · aspect)`, `y = cy − (h / 2) · Y / (Z · tan(fov / 2))`,
  the angle read from 0xE14BC4.
- **The shape is not always the scene projection's.** The mod's 21:9 build leaves 5:3 in the
  projection's field (0xE14BC8) and aims, and draws, 2.3333 wide. So the aim takes its shape
  from the game's own aim: the aim point's `X / (Z tan)` over the pixel's share of the half
  width (`KiuRoom::AimShape`: readings with the reticle 16 px or more off the middle and still),
  starting from the shape the immersive code measured from the game's draws, else the field.
- `+0x84` flags. With bit 0 or bit 1 set the pixel is the aim and stays; with neither, the
  class's own per-frame function (vtable +0x2C: 0x76ED88 on land, 0x76E9EC in the air) puts the
  reticle back: the screen's rest point on land (200, 104), in the air the pixel its old point
  in the world is seen at now. A stylus touch gives 0x43 and leaves **0x41** after it; a value
  written from outside with no touch is taken down again by the game within a frame. Bit 3 is
  the clamp's.
- On land the function 0x6D213C eases the reticle (back to its rest height when aiming stops:
  a fifth of the way each frame; the flick's glide), and 0x6D163C keeps it in the aim box (0.7
  to 1.3 of the middle across), past which the clamp 0x6D2C08 turns the camera. In the air the
  first does nothing (no battle camera) and the box is the whole screen.

**Direct Aim without the mod** (`AZAHARFRAME_KIU_AIM_NO_MOD=1`, or `_AIM_6DOF`): the probe takes
the game's own aim object (the European game's code, checked), writes the pixel as with the mod
and, at every poll, puts the flags' 0x41 back; the "aim hold" patches stop the eased moves and
make the land box the whole screen, so the reticle stays where it is put and never turns the
camera (the stick turns). **With the mod** the probe's mod route runs, as before, and the "aim
hold" group stays out by its context: one writer in either case.

**The aim in first person** (`AZAHARFRAME_KIU_AIM_6DOF=1`): the hand's ray, through the
transform, is followed to the depth the game's own aim point has now (its fixed range, or the
thing the reticle is on: the game's own pick, read back a frame later) and that point is seen
through the scene camera: that pixel is Direct Aim's target in place of the ray's point on a
screen. A ray that leaves the game's view holds the reticle at the screen's edge on its side.

| Proof (land, Practice Range; air, Chapter 1) | Result |
|---|---|
| the reticle read back against the pixel asked, every VBlank of the second before each shot | 0.00 px, 60 of 60, land and air; flags 0x41 at all of them |
| the game's aim point against the hand's ray (seen from the ray's origin) | land 0.000°, with the reticle on the dummy 0.008° (max 0.017°); air 0.000° to 0.018° (max 0.020°) |
| rays out of the game's view (35° to 100° off) | the reticle held at the screen's edge on that side |

### Shots from the hand (`AZAHARFRAME_KIU_SHOTS_6DOF=1`)

- **The game's aim at a shot (V in code and by result).** 0x503F8C calls the shooter's aim
  (vtable +0x34 = 0x7014C8) with four pointers: the start (from 0x539120, the muzzle), the
  quaternion, and a fourth local at its `sp + 0x70`. 0x7019A0 gets the aim quaternion from
  0x4C14B0 (the direction from the start to the aim object's world position, made a quaternion
  with the world's up kept up: +z the direction, x = up × z), copies it to that fourth local,
  and only then adds the weapon's tilt and each bullet's scatter to the quaternion that goes to
  the spawn. So at 0x5043E4 the caller's `sp + 0x70` still holds the aim without scatter.
- **What the app fills.** `ShotRequest.absolute`: the rotation is the game's kind of aim
  quaternion for the hand's ray (`KiuRoom::ShotQuat`), and the stub makes each bullet's
  quaternion `rotation × conj(aim) × q`: the bullet keeps its own scatter and tilt, about the
  ray. The offset is the eye offset plus the ray's origin from the eye (the transform).
  AzaharFrame aa6dof2: no lock target (`ShotAlongRay`, the stub's "no lock target" flag), so
  the shots stay on the ray; `AZAHARFRAME_KIU_SHOT_LOCK=1` keeps the game's lock (homing). With
  the lock, a charged shot fired 8°, 15° or 25° beside the Practice Range's dummy bent all the way
  onto it (35° and 50°: straight on), with the wide camera and without it alike: on the headset
  the shots left the reticle for enemies near it (owner's session 4).
- **The ray's origin** is the one ray's (Direct Aim's and the laser's): the drawn weapon's
  muzzle (`vis/weapon_fit.h`) when the weapon is in the hand, else the aim pose.

| Proof | Result |
|---|---|
| land, charged, 6 hand poses, the ray 7° to 76° from the game camera's view, the head turned too | start 0.000 units from the ray's origin, direction 0.000° from the ray; without a lock all 31 points of the path (259 units, the shot's range) within 0.00 of the ray |
| land, the ray at the dummy (95 units) | the shot ends at it after 63 to 66 units; with the reticle 20° beside it the game still locks it and the shot bends 15 units off its line to reach it (the game's own lock) |
| land, rapid (24 to 26 pooled bullets a phase) | every bullet's line passes the ray's origin within 0.00; about the ray they scatter +0.2° ± 6.3° sideways and −6.8° ± 1.0° up; the game's own shots about its own aim in the same run: +2.8° ± 7.9° and −5.0° ± 5.4° (a rough control: its aim per bullet is estimated) |
| air (Chapter 1), charged, 7 hand poses, 0° to 100° from the game camera's view | each bullet's direction 0.0000° to 0.0001° and start 0.000 to 0.004 units from the mailbox of one VBlank between the trigger and its first sighting; against the ray of 1.6 s into the pose 0.03° to 0.57° and 0.03 to 3.4 units (the rail turned and the player moved meanwhile); the first 5 points within 0.09 to 0.58 units of the ray; a shot 100° off the game's view flew its whole range (the bullet keep) |
| air, rapid | not measured (the pooled bullets' records are not mapped in the air: I, the same stub) |
| after the stick's turn | the next shot leaves along the ray in the turned world (0.000°) |

### The four game codes (EUR)

| | original `b107c9b1` | mod base `b01e1f4a` | mod 16:9 `6a062f17` | mod 21:9 `b95ca36b` |
|---|---|---|---|---|
| the sites, context and cave of every group but "aim hold" | V (run) | V (files) | V (files) | V (files) |
| "aim hold" | in (run) | out by its context (files) | out by its context (files) | out by its context (run) |

(The table of what was run on each is in the lane's report.)

### The screen in first person (AzaharFrame kiufov)

- **True size, always.** With `AZAHARFRAME_KIU_FIRST_PERSON=1` the screen is solved by
  `Layout::SolveKiuFirstPerson` (`immersive_layout.h`), not from the game's screen settings: the
  gains are 1 (the world rigid) and the game draws past its own frame on each axis by what that
  axis of the view needs (the factors), up to `AZAHARFRAME_KIU_FP_FACTOR`. The screen is level
  in yaw, pitched to the view's middle.
- **The angle and shape** come from the scene's projection (0xE14BC4, the angle; 0xE14BC8, the
  shape: 5:3 with or without the 21:9 mod), else the live angle and the camera's shape. A
  catalog shape hint or a per-game Screen Shape other than Automatic wins over the projection's
  shape (the 21:9 mod draws 7:3 with Screen Shape 21:9).
- **Behind Pit.** `AZAHARFRAME_KIU_EYE_OFFSET=x,y,z` in first person is laid out along the
  camera's level view (its yaw), not the player's body, which turns with shots.
- **Over the shoulder (AzaharFrame kiuview).** `AZAHARFRAME_KIU_SHOULDER=back,up[,side]` in
  metres adds to the first-person eye (land 12.3 units up in his axes, air 3), along the
  camera's level view, so both keep their own height. Desktop: 2,0.6 in Chapter 1's air battle
  moved the eye offset from (0.2, 3.0, 0.0) to (-2.3, 7.6, 15.1) units (4.6 up, 15.3 back: 0.6
  and 2.0 m) and shows Pit ahead and below, faded (why: not traced). On land (Chapter 1, by a
  wall) 1.5,0.4 showed no clear change in the picture: unverified there. The shots, the laser and the sprite correction follow the moved eye with no change
  (read from the code: the hand's ray is taken from the scene's eye, so shots start behind Pit
  and fly along the laser; not measured); the head and Right Stick turning turn about the moved eye. Not
  measured: the angle of shots off the ray, a wall between the eye and Pit.
- **The land aim box.** With Right Stick turning (and so in first person) the "aim box" group
  writes the land box as the whole screen and Direct Aim never pushes the Turn Zone; the box
  words are the same with the 21:9 mod, the "aim hold" words are not.
- **Not done.** HUD pieces that touch the game's frame's edge are pinned to the drawn area's
  edge by the shader and stretch. (Pit's own round shadow under the eye: done, below.) With the
  wide camera (below, the default) the drawn area is the frame and they no longer stretch.

### The wide camera (AzaharFrame kiuwide)

In first person, once a battle has a player, the "wide camera" group gives the game's own scene
camera the headset view's angle and shape, so the game's 400×240 frame is the whole view at
true size (factor 1, gains 1) and the reticle can reach everywhere in view.

- **Where the game sets it (V, code and memory, EUR original).** The scene camera node
  0xE14B88 holds the eye at +8, the angle at +0x3C (0xE14BC4) and the shape at +0x40
  (0xE14BC8); clearing bits 0x5C of its flags (+0x6C) has the projection and cull planes rebuilt
  (built at 0xB4FB1C, planes 0xB4FBC8). The setter at 0x45E04C loads the angle the game wants
  this frame (`vldr s1, [r0, #0x3C]`), compares, stores it and clears those bits; its caller
  stores the shape (0x45DFF4, I). Land, air and the air's zooms all pass it.
- **The stub.** `b fov_stub` at 0x45E04C. If r0 is the node in the mailbox, the stub writes our
  shape and angle into the node and clears those bits when either differs, and returns from the
  setter without the game's store (so the game's zoom does nothing while it is in); otherwise
  it runs the replaced `vldr` and goes on at 0x45E050. It reads the mailbox through r12 only
  and on the pass path changes only r12 and the flags. Eleven context words (the setter's rest,
  the shape store and the projection builder's two loads).
- **The values.** `Layout::KiuWideCamera` takes the view's cover at the screen's pitch (the
  view's middle): the angle up and down and the shape across over up and down, the angle
  rounded up to 0.01°. The mock view (126×112°) gives 111.51° and 1.336; the Frame's catalog
  view about 112° and 1.32. The angle is constant: the game's own zooms are not followed.
- **With the 21:9 mod.** The mod scales the cached projection across by its own ratio, so the
  shape asked for is the view's divided by that (0.954 for 21:9 with Screen Shape 21:9), and
  the mod's drawn shape is the view's (V, the node and the screen log).
- **Back out.** The game's shape is kept (read from the node before the first change, in the
  mailbox's record) and put back only when the node still holds exactly ours; the angle is the
  game's own again at its next frame. Switching `AZAHARFRAME_KIU_FP_WIDE` 1 → 0 → 2 → 1 during
  play in the air: 1.3359 → 1.6667 (the game's) with the zoom followed again → 1.6667 → 1.3359
  (V).
- **The aim.** The reticle is the hand's ray through the wide projection; the aim box is the
  whole screen. Rays up to ±60° across, +45° up, −55° down and the four ±50°/+30°, −38°
  corners land on their pixel (0.00 px) with the game's aim point on the ray (0.000°) on land;
  −75° is held at the frame's edge (V, mock view). Shots leave along the ray at those angles.

| Run (desktop, mock view) | Reticle off its pixel | Game's aim point off the ray | Shots' direction off the ray |
|---|---|---|---|
| land, no mod | 0.00 px | 0.000° | 0.000° |
| air, no mod | 0.00 px (0.50 once) | 0.000–0.07° (one corner: 30.5°, below) | 0.0000–0.0001° (the firing VBlank's mailbox) |
| land, 21:9 mod | 0.00 px | 1.2–2.8° (the mod's own aim maths) | 0.000° |
| land, `FP_WIDE=2` | 0.00 px | 0.000° | 0.000° |
| land, `FP_WIDE=0` (control) | held at the old frame's edges (about ±35° across) | — | — |

- **Pixels.** The render target is unchanged (400×240 × the resolution factor), now spread
  over the whole view; its pixels are 0.80 as wide as tall (1.336 drawn on 5:3). The HUD is
  drawn in the same frame: its panels sit at the view's lower corners, readable and not
  stretched. `FP_WIDE=2` keeps square pixels but draws about a fifth of its width outside the
  view.
- **Open (I).** In the air, the hand ray at +50° left and −38° down puts the game's aim point
  about 30° off (twice, the same corner; the reticle is on its pixel): likely the game's own
  air aim limit or a lock. With the mod, the game's aim point is 1.2–2.8° off the ray (the
  reticle is right). Lock-on now reaches further out (an enemy 33° off was homed on). The
  camera is widened only while the battle has a player: cutscenes and menus keep the game's.

### The player's round shadow (AzaharFrame kiushadow)

In first person the "player shadow" group is in (`Kiu::FirstPerson::On()`; out again when it is
off): one word, `b shadow_stub` at 0x57B054.

- **What draws it (V on the desktop, the real game).** A circle-shadow renderer, singleton
  `[0xD49490]` (made by 0x238938 with room for 64; its shader binds ViewMtx, ProjMtx, Position,
  Normal, Front and Color and loads "CircleShadow"), keeps a list rebuilt each frame: +0
  positions (12 bytes each), +0xC room, +0x10 parameters (0x40 bytes each), +0x14 count.
  0x505340 → 0x4B0158 appends one shadow (position, normal, front, radius, alpha).
- **Pit's entry (V).** In the Practice Range the list holds one shadow: under Pit (y 0.3),
  radius 6.5, alpha 120/255. Holding the list's room at 0 empties it and the soft dark disc under
  Pit's feet is gone from the picture; it comes back with the room. The ShadowDisp anime
  command's byte (`[actor + 0x200] + 0x8D`) does not take it out (V: the entry stays).
- **Who appends it.** 0x57B054(state, mode), called from Pit's movement states (0x551238 and
  0x551828): gates on the state's fields, then 0x5B26AC(actor, 6.5) (a ray down, then 0x505340)
  or 0x50533C. Radius 6.5 and alpha 120 are its literals (V); that Pit's entry comes from it is V
  by the stub (only it is skipped, the entry goes).
- **Everyone else's.** In Chapter 1's land section the list holds three more shadows (radius 9,
  far away; one is under a food pickup): they stay with the group in and Pit's goes (V, list).
  They come another way (0x57C5C4 → 0x5A41E8), so the stub's pass-through for another actor's
  movement state is V only on the stub's test bench; no enemy with a round shadow was found on
  the desktop (the Practice Range's Monoeye casts none), so "enemies keep theirs" is I.
- **The air.** No round shadow in Chapter 1's air section (the list is empty; V).
- **The four codes.** The function 0x57B054–0x57B1C8, 0x5B26AC–0x5B2824, the callers
  0x551238–0x551838 and the empty cave are byte for byte the same in the original, the mod's
  base, 16:9 and 21:9 builds (V, files).


### The game's sprites (AzaharFrame kiu2d)

- **What they are (V, desktop, Chapter 1's air battle).** The purple lock-on triangles are
  drawn through the game's geometry shader. The emulator runs those draws on the CPU with the
  game's own registers, so mode 3's patch of register 90 never reached them: they stayed where
  the game's camera put them while the world turned with the head. Skipping the CPU-shaded
  draws takes every triangle away (V); the HUD is drawn on the host's GPU and is untouched.
  The hit numbers and score pop-ups are likely the same kind of draw (a second CPU-shaded
  program with a 256 x 256 texture) but were not seen on the desktop (I).
- **The fix.** Each eye's world draw leaves its turn in clip space, H = P * (V' * V^-1) * P^-1
  (P the world projection, registers 86 to 89; V and V' the view rows as the game set them and
  as mode 3 drew them), and the CPU-shaded draws into that eye's colour buffer are turned by it
  (`vr_view_matrix.h` ClipCorrection; `rasterizer_accelerated.cpp` TurnSoftwareVertices). Per
  colour buffer: one eye's sprites come after the other eye's world. The turns are forgotten
  at each new game frame (`SetVRData`), so a frame with sprites and no world takes none. The
  sprites' clip depth is their point's (z + w = 10, the projection's, at every sprite vertex
  read), so the lean and each eye's half distance are exact too.
- **Measured (mock, Chapter 1's air battle; the same game frame with `FP_2D` 1 and 0; 1600 x
  964 per eye; median over the triangles):**

  | Head (from the rest pose) | Triangles moved by the fix | Predicted (the turn at the middle) |
  |---|---|---|
  | rest | 4, -29 px | 0, -30 (the mock view's 5.25 degree screen pitch) |
  | 15 degrees down | 3, -111 px | 0, -116 |
  | lean 0.3 m right, 0.2 m up | 0, -30 px | 0, -30 plus about 10 (at 50 units) |

  Enemies between the two runs: 0 to 1 px (the same frame). Head turns of 20 degrees left and
  25 left with 10 up were run too; the triangles' matching between runs was too unreliable
  there for a number, by eye they sit above their enemies with the fix and off by the turn
  without it.
- **Not done.** Sprites drawn on the host's GPU (none seen in a battle) and the HUD keep the
  head-locked picture, as before. The game's own reticle ring was not checked.
- **Pit's own body (V, Chapter 1's land section).** Looking down in first person shows a
  see-through white shape of his legs and body on the floor: the game draws the player faded
  when the camera is close, and first person does not hide him. `AZAHARFRAME_KIU_HIDE_PLAYER=1`
  takes it away on land (V); it is not in Angel's Aim's set (in the air it washes the picture
  out and he takes less damage; his hurt boxes on land are unchecked). The player's round
  shadow is not it (`NO_PLAYER_SHADOW=0`: the same picture).

### The talk and the HUD in first person (AzaharFrame kiuview)

- **The talk is on the Secondary Screen (V, desktop, EUR, Chapter 1's air battle).** The
  portraits and the words ("If these are Underworld troops...", "Resurrected. Yes.") are drawn
  on the 3DS's lower screen, the words in its top fifth; the Primary picture shows only the life
  bar and the score (emulator screenshots of both screens every 2 s, 16 of them). The Full modes
  never show the Secondary Screen (`Screens`: `show_bottom = false` in modes 2 and 3), so in
  Angel's Aim the talk was not on view at all.
- **The talk panel** (`AZAHARFRAME_KIU_FP_TALK=1`; off by default, see the laser below): the Secondary
  Screen as a quad with the head, its middle 24° below straight ahead (18.75° below the view's
  middle on the desktop mock), 34° wide and 25.9° high at 0.8 m, nearer than first person's
  screen so it is drawn over it. Its words run from about 11° to 16° below straight ahead. The angles
  are the code's (the mock trace has the quad's size, 0.49 × 0.37 m, not its pose). Not run:
  the panel goes into controls.cpp's pointable Secondary slot, so the laser aimed low may
  point at it and touch the Secondary Screen; hence off by default.
- **The HUD's corners.** The wide camera's frame is 126° × 112° about the view's middle
  (−5.25°), so the life bar and the score (rows 5 to 30 of 240, 0 to 120 px from the sides)
  sit about 42 to 50° above straight ahead and 38 to 63° to the side. HUD Spacing (`AZAHARFRAME_KIU_FP_HUD`) does move
  them in, but they touch the frame's edge, whose corners the shader pins, so they stretch (the
  life bar across the view, the score icon several times its size; the cause is read from the
  shader's rule, not traced): off.

### Not done, risks

- The headset: nothing here ran on it.
- The wide camera with the 21:9 mod in the air has not been run.
- The world tilts with the game camera's pitch (3° down on land at rest; the rail's in the
  air) and shakes with the game's own camera shake: the rest frame is the game camera's, level
  only when the camera is. A level frame from the camera's yaw alone is the next step if the
  owner's check asks for it.
- Dash shots, melee and powers do not pass the spawn call (the hook lane's note).
- The hand's ray is the right hand's when no controller aims, and Direct Aim must be on for
  `AZAHARFRAME_KIU_AIM_6DOF`.
- The reticle sprite the game draws is 2D: it sits on the head-locked picture, not in the world.
  (Its lock-on marks and hit numbers turn with the world: "The game's sprites" above.)

## Not done, not known

- The headset: nothing here ran on it (the aarch64 build compiles).
- Other regions and game versions: no table (the title and code checks leave everything off).
- Multiplayer: not run. The shot stub compares the owner with the local player's actor (found
  through the local aim object), so other players' shots should pass untouched (I).
- (Settled, "Shots from the hand" below: the game's own aim quaternion at a shot is read by the
  stub from the caller's frame.)
- Shots outside the one spawn call (dash shots, melee, powers) are not hooked.
- Whether the rail's axes always are the world's (they were in Chapter 1's air section).
