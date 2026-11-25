# Kid Icarus: Uprising: the app's own game-code hooks (test build)

A test build behind test switches only: no setting, no row, no text in the app. Code:
`input/kiu_hooks.h` (pure: the patch table, the rules, the mailbox, the player),
`input/kiu_hooks_stubs.h` (the stubs' words, generated), `kiu_hooks.{h,cpp}` (the emulation
thread's side), `tools/kiu_hooks/` (the stubs' ARM source and their checks), tests in
`tests/frame_xr/kiu_hooks.cpp`.

Everything here is for the **European game, title 0004000000030200, original code (code.bin md5
b107c9b1)**. Every address is of that build; **USA Rev 1** has its own tables ("Other builds"
below). **V** = seen working in the running game on the
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
- **Before anything**, the title must be a mapped one and nine words of the camera update must
  match that build's (`kiu_turn.h`, `MatchBuild`: the European game and USA Rev 1; USA Rev 0 and
  the Japanese game are left off).
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
| `AZAHARFRAME_KIU_HIDE_PLAYER=1` | the actor's local scale 1 → 0.001 (a data write; exactly 0.001 goes back to 1 when off, or when not playing). AzaharFrame kiupit: first person does it by itself on land ("Pit's own model in first person" below); `=0` keeps him drawn there |
| `AZAHARFRAME_KIU_HIDE_BY=draw\|scale` | how first person hides Pit (AzaharFrame kiu131; default `draw`): `draw` clears his model's "drawn" byte (land and air, body, wings and weapon); `scale` is the kiupit local-scale write, land only (the A/B: his weapon shows while he fires) ("Pit's own model, by its draw" below) |
| `AZAHARFRAME_KIU_SHOT_TEST=yaw,pitch[,dx,dy,dz]` | the player's shots turned by that many degrees from the game's aim (yaw about the world's up axis: positive is to the left; pitch up about the game camera's level side axis) and, with the last three, started at the player's position + dx to the side (positive: to the right), dy up, dz ahead of the game camera's level view |
| `AZAHARFRAME_KIU_SHOT_NO_LOCK=1` | with SHOT_TEST: no lock target (no homing) |
| `AZAHARFRAME_KIU_ENEMY_KEEP=1` | passed fliers fly their path again, for good |
| `AZAHARFRAME_KIU_ENEMY_KEEP_S=<seconds>` | the same until an enemy is that old; replaces ENEMY_KEEP |
| `AZAHARFRAME_KIU_GROUP_KEEP=1` | land spawner groups are not despawned |
| `AZAHARFRAME_KIU_NO_FOLLOW=1` | the land camera no longer takes back part of a turn while the player runs |
| `AZAHARFRAME_KIU_HOOK_TRACE=1` (or 2) | a status line a second (2: one a VBlank): `xr: Kid Icarus hooks: t … [groups] player … scene eye … round shadows N` (AzaharFrame kiupit: the count in the game's round-shadow list, `-` when it cannot be read) |

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

### C-Stick mods and Angel's Aim (AzaharFrame kiu132)

The C-Stick mods (`kiu-cstick-gyro`, `-widescreen-16-9`, `-ultrawide-21-9`, hosiniku's
`kiu-cstick-aim-*`) replace the game's code. They are what aims in Flat and in the other modes
(the probe's mod route, `kiu_probe.cpp`); Angel's Aim's first person is made for the game without
them.

- **What the hooks check before writing (V, the code):** the title and the nine camera words
  (`KiuTurn::CodeMatches`), each site's original word, each group's context words and the cave;
  anything else leaves that group out. Darkshade's v172 EUR base and 21:9 builds pass these
  checks (above), so with one loaded the hooks go in beside it. With the mod found, the probe
  takes the mod's aim route instead of Angel's Aim's own (`AimNoModWanted` is not used), "aim
  hold" stays out by its context (0x6D219C), and `NOCULL` jumps past the mod's hook at 0x7732D0.
  First person thus runs on code it was not made for: the clash (the symptoms: I; never run
  here). hosiniku's builds: not checked.
- **Which modes:** only 4 (Angel's Aim), and only where it is in effect (the European game, in
  VR: `AngelsAimFor`). Modes 1 to 3 and a stored 4 in the other regions take none of first
  person's routes: the mod keeps aiming there, as in Flat.
- **At start** (`citra_qt.cpp` BootGame, after the game's values and the presets): with
  `LeaveOutCStickModFor`, every turned-on C-Stick mod's files (`CStickModFiles`) go into
  `FileSys::ModSkip`, so the loader leaves them out of this run; the mod library is unchanged.
  Log: `xr: Angel's Aim: the C-Stick mod is left out of this run (...)`, the loader's `... left
  out of this run` per file, then the toast **Angel's Aim: C-Stick Mod Not Loaded** (V, desktop
  mock with a stand-in mod, reports/95/kiu132). Any other run: `the C-Stick mod loads (...)`.
  A hand-installed `exheader.bin` (its own item in the library) goes out with a left-out
  `code.bin`, and a widescreen build's shape is not given to `AutoShape` in that run.
- **Chosen during a run with a mod loaded** (the VR menu or Azahar's window: the setting is read
  each VBlank): Angel's Aim stays out (`CStickModBlocksFor`; mode 4 plays as Other Games) and the
  toast **C-Stick Mod Loaded: Restart the Game for Angel's Aim** shows once each time it is
  asked for. The choice is kept, so the restart leaves the mod out. Refusing the switch was not
  chosen: the setting is right for the next start, and Azahar's window would need the same
  refusal.
- **Risks:** a save state made with the mod's code loaded brings that code back into a run that
  left the mod out (the hooks' checks still guard their own writes); switching to Flat after a
  start in Angel's Aim leaves the run without the mod until a restart.

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
| `AZAHARFRAME_KIU_SHEETS=0\|1\|2` | Chapter 2's cloud sheets in first person (default 1; `video_core/kiu_sheets.h`): 1, the sheets' draw on the CPU with a soft rim past its edges; 0, the draw as it was, on the GPU; 2, on the CPU with nothing added (a test) |
| `AZAHARFRAME_KIU_SHEETS_SKY=1\|first,hold,far,level,slope` | A test, off without it: in place of the rim, each sheet goes on from its hard edges, first in one ring that holds a level (from `first` to `hold`), then one that fades out (to `far`), in the sheet's own sizes; `level` -1 to 1 (0 the alpha of the sheet's middle), `slope` 0 level, 1 down as its sides |
| `AZAHARFRAME_KIU_FP_TALK=0\|1`, `AZAHARFRAME_KIU_FP_TALK_AT=<down>,<width>` | first person's talk panel (default 0; 1 shows it; unset: the setting Bottom Screen's Persistent, `vr_kiu_bottom_screen`, AzaharFrame kiu131; before it Talk Panel): the Secondary Screen, where this game shows its talk, as a small panel with the head, its middle 24° below straight ahead, 34° wide, 0.8 m away (degrees; "The talk and the HUD in first person" below) |
| `AZAHARFRAME_KIU_FP_FIELD=<percent>` | the share of the view's angle the picture fills (50 to 100, default 100): the wide camera that much narrower, the screen still at true size, a dark border around it (live; desktop, AzaharFrame kiufp3: the screen 126 × 112° at 100, 101 × 89° at 80, 76 × 67° at 60, the scene camera 111.5°, 89.2° and 66.9° high, the game drawing 1.00× its frame each time, a black border around it; kiuview's "no change" was its rig writing the switch before the app started) |
| `AZAHARFRAME_KIU_FP_EDGE=<percent>` | the picture's edge fades to black over that share of its half size (0 to 50, default 0); the projection path only, which needs the controller models' files (without them the models' pass is never made and the screen goes as quads: why kiuview's mock never drew it); at `FP_FIELD` 100 most of the edge is outside the view. Desktop, AzaharFrame kiufp3, `FP_FIELD=80`: the picture's brightness against no edge, across the left edge, reaches full at 15 % of the half width with 15 and at about 30 % with 30 (pictures reports/91/kiufp3/pics/edge-*); the HUD pieces at the edge fade with it |
| `AZAHARFRAME_KIU_SHOULDER=<back>,<up>[,<side>]` | in third person (unset: the setting Third Person, `vr_kiu_over_shoulder`, 2.5 m back and 0.5 m up when on; `0` off whatever the setting; AzaharFrame kiutoggles): the eye this many metres behind, above and to the right of the first-person eye along the camera's level view (game units = metres × the leaning scale, 7.7 per metre at life size); `AZAHARFRAME_KIU_EYE_OFFSET` wins |
| `AZAHARFRAME_KIU_WRIST_SCREEN=0\|1` | Angel's Aim's Secondary Screen on the off hand's wrist (default 0, off; unset: the setting Bottom Screen's Wrist, `vr_kiu_bottom_screen`, AzaharFrame kiu131; before it Secondary Screen on Wrist; `1` wins over Persistent): shown while the wrist is turned up, touched by the aiming hand's beam without a shot ("The Secondary Screen on the wrist" below) |
| `AZAHARFRAME_KIU_FP_FACTOR=<f>` | the most the game draws past its own frame on either axis in first person (1 to 8, default 6); past it the screen is smaller on that axis ("The screen in first person" below) |
| `AZAHARFRAME_KIU_PAGE=0`, `AZAHARFRAME_KIU_HUD_EXACT=0` | off: the 2D as before ("Single screen" below; AzaharFrame kiuhud); on without them, first person only, live |
| `AZAHARFRAME_KIU_HUD_CENSUS=<n>`, `AZAHARFRAME_KIU_HUD_COST=1` | log only, off by default: each kind of 2D draw of the top screen once (and again for another `<n>`) with what was made of it; the HUD's cost every 300 VBlanks |
| `AZAHARFRAME_KIU_TALK_TOP=1`, `AZAHARFRAME_KIU_PAUSE_PANEL=1`, `AZAHARFRAME_KIU_PORTRAITS=1`, `AZAHARFRAME_KIU_MENU_SCREENS=1`, `AZAHARFRAME_KIU_MENU_PANEL=1`, `AZAHARFRAME_KIU_UI_TRACE=1` | the single screen's, each off without it, first person only, live ("Single screen" below; AzaharFrame kiu1s) |
| `AZAHARFRAME_KIU_FP_TRACE=1\|2`, `AZAHARFRAME_KIU_FP_PROBE=<hex>` | a line of `key=value` words a second (2: a VBlank): the scene camera, the view register, the rest pose, the head, the hand's ray in the world, the reticle, the mailbox; and an actor's place in the world and in the head's view. AzaharFrame kiupit: `cam_node_44=` the scene camera node's ten words from +0x44, as floats (to find its near plane) |

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
- **Third person (AzaharFrame kiuview).** `AZAHARFRAME_KIU_SHOULDER=back,up[,side]` in
  metres adds to the first-person eye (land 12.3 units up in his axes, air 3), along the
  camera's level view, so both keep their own height. Desktop: 2,0.6 in Chapter 1's air battle
  moved the eye offset from (0.2, 3.0, 0.0) to (-2.3, 7.6, 15.1) units (4.6 up, 15.3 back: 0.6
  and 2.0 m) and shows Pit ahead and below, faded. kiuview's land run (1.5,0.4) never took the
  switch (its rig wrote it before the run began).
- **Why Pit is faded (AzaharFrame kiufp3, V on the desktop, Practice Range).** The game fades
  its player when the camera comes near him (V); that it does so to keep the view clear is
  inferred (I). The shoulder eye is the scene's camera, so the same rule applies. With 0.5 m
  up he is faded at 1, 1.5 and 2 m back (also at 2 m with the hand aimed 20° up, and at 1.5 m
  with it 35° right or with 0.8 m to the side), solid at 2.3, 2.6, 4 and 8 m, and at 2 m back
  with 0.8 m to the side (2.2 m away). At 2 m with the hand 35° right he was solid too: he had
  turned to aim, so the distance the game measures moved. So the edge lies between 2.0 and
  2.3 m back (about 16 to 18 units), and the aim's direction acts only through his pose. The
  actor and its owner hold no word that differs between faded and solid (a memory diff of
  0x1000 and 0x800 bytes), so the fade lives in the model's drawing; left as the game does it.
  At `SHOULDER=1.5,0.5` he stays faded; `2.5,0.5` shows him solid on land and in the air.
  Pictures: reports/91/kiufp3/pics/s1-*, s2-*, t1-*.
- **Shots in third person (AzaharFrame kiufp3, V on the desktop, Practice Range,
  `SHOULDER=2.5,0.5`, charged shots, aa6dof2's method).** 0.00° off the hand's ray at head yaw
  0, 20 and 40 right and 20 left with 15 down; each passes the aim point 0.00 units away (526
  units on). The path each flew (its first to its last point, about 255 units, 29 to 31 points)
  is 0.000° off the ray too, so no homing turned it. They start at the hand's place behind the moved eye (2.1 m behind Pit, 1.7 m up),
  not at Pit: they fly from behind him, along the laser.
- **Lock-on marks in third person (AzaharFrame kiufp3, desktop, Chapter 1's air battle,
  `SHOULDER=2.5,0.5`, projection path).** By eye the triangles stay above their enemies at rest,
  with the head 20° left and 15° down, Pit solid in front (pictures
  reports/91/kiufp3/pics/t1-*); not measured to the pixel.
- **A wall behind (AzaharFrame kiufp3, desktop, Chapter 1's land alley, the camera turned with
  the stick).** Nothing keeps the moved eye out of walls: at 1.5 and 3 m back it was still in the
  alley; at 5 m it went through the wall behind, which is then not drawn (seen from its back),
  so the picture shows the ground and trees outside, the game's green guide signs large, and
  Pit small beyond (pictures reports/91/kiufp3/pics/w2-*). Not fixed: the offset is added
  with no check against the level. The shots, the laser and the sprite correction follow the moved eye with no change
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

### Pit's own model in first person (AzaharFrame kiupit)

The owner's report (Frame, 2026-11-22): in Angel's Aim "Pit's shadow" shows in first person and
looks like his head.

- **Not the round shadow.** The "player shadow" group is in whenever first person is on
  (`kiu_hooks.cpp`, `want_player_shadow`: `GroupWanted(first_person, …)`; no test switch needed,
  only `NO_PLAYER_SHADOW=0` keeps it out), and in the air there is no round shadow at all (above).
  The owner's air pictures (an air battle, the weapon in the hand) show a pale see-through
  shape in the middle of the view, a head with shoulders below it, in front of the eye and
  between it and the reticle: the shape of Pit seen from just behind his head, drawn faded. His
  dark picture (likely a land section) shows no round disc; a faint pale upright shape is in its
  middle (I: too dark to be sure).
- **What it is (I, from the notes above and the pictures).** The game draws its player faded
  when its camera is near him ("Why Pit is faded"), and first person puts the eye inside him: on
  land 12.3 units up, in his head; in the air 3 up (`kEyeOffsetAir`), with his head ahead of it.
  So his head (in the air) and his body looking down (on land, "Pit's own body") stay on view,
  see-through.
- **On land: hidden.** First person writes `HIDE_PLAYER`'s scale by itself
  (`KH::HideInFirstPerson`): on land, with the shots from the hand (Aiming = Direct Aim: their
  start is the mailbox's) and the eye at his middle (no `SHOULDER`, no `EYE_OFFSET` with a side
  or ahead part: those are there to see him). `HIDE_PLAYER=0` keeps him drawn (an A/B);
  `HIDE_PLAYER=1` hides him anywhere, as before. Cutscenes and pauses (the playing flag down) and
  the air put the scale back (`HideWrite`). The log says `the player's model hidden (local scale
  0.001) (first person, on land)`. Not seen on the headset or the desktop yet: whether he takes
  less damage on land (his hurt boxes, as in the air), whether his close attacks still hit, and
  whether anything else of him shrinks with the scale.
- **In the air: not done.** The scale write is not usable there (above). Ways not tried: the eye
  ahead of his head (live, `EYE_OFFSET=0,3,<ahead>` during an air battle only, as it applies on
  land too: in first person a side or ahead part lays it along the camera's level view, +z
  ahead); a near plane far enough out to cut him (the scene camera node's, not found:
  `FP_TRACE`'s `cam_node_44=` lists the words after its shape); skipping his model's draw (its
  code was never looked for).
- **To see the round shadows.** `HOOK_TRACE=1` ends its line with `round shadows N`, the count
  in the game's list ([0xD49490] + 0x14): 0 in the Practice Range with the group in, 1 with
  `NO_PLAYER_SHADOW=0` (as measured for "Pit's entry" above; this line itself not run).
- **Checked on the desktop (AzaharFrame kiu130, V, the real game on the mock, Chapter 1's land
  section, Direct Aim, the palm weapon 49 of the save states).**
  - Standing and running (the left stick held, about 50 units a second), looking ahead and 50°
    and 75° down: no see-through body, wings or round disc. With `HIDE_PLAYER=0` the same moves
    show his see-through wings and arms in the lower half of the view while he runs: what was
    still seen while running.
  - `HOOK_TRACE=2` (a line a VBlank): `scale 0.0010` the whole time, standing and running; the
    log has one `hidden ... (first person, on land)` after the state loads.
  - `round shadows`: 3 standing and running (the level's other shadows), 4 with
    `NO_PLAYER_SHADOW=0` standing and running; running far off, 1 with it and 0 without. So his
    round shadow stays out while he runs too; with it in, the soft disc shows under the eye.
  - Third Person (the setting): `scale 1.0000`, no hide line, Pit solid. `SHOULDER=0` live
    gives first person and the hide; the switch taken out, the shoulder view and scale 1 again.
  - Still seen with him hidden: the game's green guidance arrow, which lies on the floor beside
    him (seen solid from third person; in first person its tip is at the left edge of the
    view) and is not his; and, while firing or just after, the palm weapon's ghostly hand at
    full size below the view's middle. The palm's hand is the game's weapon effect: the scale
    write does not shrink it, and neither does shrinking the two kinds of effect actors
    (`EFFECT_NORMAL_INFO`, `EFFECT_BLEND_INFO`) near him. Not fixed (its draw was not found).
  - Not checked: damage taken (no enemy hit him in the runs), pickups, melee and dash hits.
    Floor collision: he ran about 300 units over the floor at the same height (y 0.010).
  - Aiming Off and Gyro Aim keep him drawn (`HideInFirstPerson` needs the shots from the hand).
- **The air.** Left as it is (the owner: no shadow there).


### The doubled weapon (AzaharFrame kiu130)

AzaharFrame kiu131 found the cause: Pit's own weapon, not the bullet ("Pit's own model, by its
draw" below). What follows is kiu130's reading.

The owner's report: a second weapon-like thing in front of the weapon in hand, with some weapons.
In the owner's PC pictures (an air battle, the First Blade) a silver arrowhead sits on the
blade's tip, and two seconds later one lies further along the line from the muzzle to the
reticle; another picture shows it with no weapon in view. Its edges are drawn without MSAA:
the game's picture, not this app's layer.

- **The game's rapid-fire bullet (I, from the owner's pictures; the start V on the desktop).**
  With `AZAHARFRAME_KIU_WEAPON_DRAW=0` nothing weapon-like is in the game's picture while idle
  in an air battle, only shots while firing. Every pooled rapid bullet starts exactly on the
  ray's origin, the weapon in hand's muzzle (0.00 units along and off the ray; Practice Range),
  so each new bullet is drawn on the tip, at the world's life size. That the arrowhead is the
  First Blade's bullet is inferred: every save state on the desktop carries the palm (weapon
  49), so the blade, and its bullet's length, were never seen here.
- **The fix.** First person's shots start past the muzzle along the ray,
  `KH::kShotAheadDefault` = 6 units (about 0.8 m; `KH::ShotAheadFor`, `ShotFromHand`), a
  judgement: clear of a blade's tip from the bullet's first frame. The switch is
  `AZAHARFRAME_KIU_SHOT_AHEAD=<units>` (live, 0 to 30; `0` is the muzzle, as before; `3` to
  compare).
  - Measured, Practice Range: charged and rapid shots start 3.00, 6.00 or 10.00 units along
    the ray as set and 0.00 off it; the charged path stays on the ray (0.00).
  - Hits, Chapter 1's air battle (state 4), the score after the same 6 s of rapid fire: `0`
    960 and 840 (840 before the change), `6` 600 and 960, `3` 840. No clear difference; runs
    of the same setting differ by as much.
  - Not checked: a target nearer than 6 units to the hand (in the Practice Range the dummy's
    place against the ray could not be set up reliably), and the blade.
- **Pit's own weapon.** On land he is hidden with it. The palm's ghostly hand still shows while
  firing ("Pit's own model in first person" above); not fixed. In the air his weapon was not
  seen in the game's picture (the palm).
- **Third Person** (`Kiu::FirstPerson::ShoulderOn`, `Vis::WeaponModelDrawnFor`): the game
  draws Pit with his own weapon there, so the weapon in hand (and a controller in its place)
  is left out of the image while the view is in third person, at run time only. No setting
  is written; first person draws it again. The fit, muzzle, Laser Sight and the shots from the
  hand go on. `WEAPON_DRAW`, when set, wins (`1` draws it in third person too). The log's
  `xr: weapon drawn:` line ends `; the model left out (Third Person: Pit holds his own)`.
  V on the mock: no weapon in the hand with Pit solid and his palm on; `SHOULDER=0` live, the
  weapon in the hand again.

### The game's sprites (AzaharFrame kiu2d)

- **What they are (V, desktop, Chapter 1's air battle).** The purple lock-on triangles are
  drawn through the game's geometry shader. The emulator runs those draws on the CPU with the
  game's own registers, so mode 3's patch of register 90 never reached them: they stayed where
  the game's camera put them while the world turned with the head. Skipping the CPU-shaded
  draws takes every triangle away (V); the HUD is drawn on the host's GPU and is untouched.
  The hit numbers and score pop-ups are likely the same kind of draw (a second CPU-shaded
  program with a 256 x 256 texture) but were not seen on the desktop (I). AzaharFrame kiufp3
  looked again: Chapter 1's air battle (state 4), fire held 10 s with the head 20° left, the
  emulator's own screenshots every 0.4 s (25 of them): 16 kills (score 0 to 1920), hearts and
  explosions drawn, no number at any kill. Charged shots (a tap every 1.6 s, as the owner's
  "45.0" picture #21 of session 4, Chapter 1's air battle, at a charged shot's blast), 24
  screenshots every 0.3 s: 7 kills (score 0 to 840), no number either. So no number to measure; still
  unverified.
  (Dense mock dumps, four swapchains every 0.33 or 0.5 s, stalled the emulator 1 to 5 s a frame
  and froze the battle: use the RPC's screenshots for such runs.)
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
  takes it away on land (V). The player's round shadow is not it (`NO_PLAYER_SHADOW=0`: the
  same picture). AzaharFrame kiupit: first person now hides him on land by itself ("Pit's own
  model in first person" above).

### The talk and the HUD in first person (AzaharFrame kiuview)

- **The talk is on the Secondary Screen (V, desktop, EUR, Chapter 1's air battle).** The
  portraits and the words ("If these are Underworld troops...", "Resurrected. Yes.") are drawn
  on the 3DS's lower screen, the words in its top fifth; the Primary picture shows only the life
  bar and the score (emulator screenshots of both screens every 2 s, 16 of them). The Full modes
  never show the Secondary Screen (`Screens`: `show_bottom = false` in modes 2 and 3), so in
  Angel's Aim the talk was not on view at all.
- **The talk panel** (`AZAHARFRAME_KIU_FP_TALK=1`; off by default; kiupanel: for reading only, see "The talk panel for reading only" below): the Secondary
  Screen as a quad with the head, its middle 24° below straight ahead (18.75° below the view's
  middle on the desktop mock), 34° wide and 25.9° high at 0.8 m, nearer than first person's
  screen so it is drawn over it. Its words run from about 11° to 16° below straight ahead. The angles
  are the code's (the mock trace has the quad's size, 0.49 × 0.37 m, not its pose). Not run:
  the panel goes into controls.cpp's pointable Secondary slot, so the laser aimed low may
  point at it and touch the Secondary Screen; hence off by default.
- **The HUD's corners.** The wide camera's frame is 126° × 112° about the view's middle
  (−5.25°), so the life bar and the score (rows 5 to 30 of 240, 0 to 120 px from the sides)
  sit about 42 to 50° above straight ahead and 38 to 63° to the side. kiuview tried HUD Spacing
  in first person (a switch, `AZAHARFRAME_KIU_FP_HUD`); AzaharFrame kiufp3 took it out. The life
  bar and the score touch the frame's edge, and the shader's HUD rule (glsl_shader_gen.cpp
  `VrFactor`) works on each vertex alone: a 2D vertex on the edge (|x| or |y| at least 0.999)
  keeps its place, so full-screen passes stay whole. So their outer corners stayed put and their
  inner ones moved in, and they stretched (the life bar across the view, the score icon several
  times its size). A fix needs each element's whole draw, which a vertex shader does not see, and
  would change HUD Spacing for every game. `AZAHARFRAME_KIU_FP_FIELD` brings the HUD in without
  a stretch (it is part of the smaller picture).

### The Secondary Screen on the wrist (AzaharFrame kiuwrist)

`AZAHARFRAME_KIU_WRIST_SCREEN=1` (live; off by default). Angel's Aim in effect only.

- **Where.** On the off hand's wrist like a watch: the right hand with Motion Controls' Left
  Controller, else the left one (as the aiming hand is picked). The left wrist is the wrist
  stats' place (AzaharFrame kiu131: now Wrist Side's, Back by default, "Wrist Side" below;
  `vis/stats_place.cpp` WristLocalPose: along the forearm on the inner side, 2.3 cm
  clear of the controller, turned 10 degrees up); the right wrist is it mirrored, turned so the
  picture is upright and reads left to right (`vis/wrist_screen.cpp`). With Stats On Wrist (the
  left controller) and the screen on the left wrist, the screen moves 11 cm along the forearm
  toward the elbow, clear of the stats.
- **Size.** 10 x 7.5 cm (4:3). At 0.35 to 0.41 m (a watch glance; the mock's wrist-up pose is
  0.41 m) it is 16.3 to 13.7 degrees wide, 20 to 23 of its 320 pixels a degree, taken to be
  about the headset's own in the middle of the view (an assumption, to check on the headset);
  the handheld's own screen at 0.30 m is 11.7 degrees.
  The power orbs (about 32 px) are 10 mm, 1.6 degrees or more: easy laser taps.
- **When.** Shown while its front is within 35 degrees of the eyes, hidden past 40 (the wrist
  stats' hysteresis). No fade: hidden it is neither drawn nor touched. Held to play (the mock's
  rest pose, pointing ahead) it is 57 degrees off: hidden.
- **First person stays engaged.** The bottom screen shown by the bumper chord or in focus pauses
  the Full view (as before); the wrist screen is not that: `Immersive::Screens` puts the bottom
  screen on the wrist only while first person is engaged (`ScreenLayers::bottom_on_wrist`), from
  the pose the controls set for that display frame (`SetWristScreen`).
- **Touch, no shot.** The mapper takes the wrist screen as the bottom panel: a trigger pulled
  with the aiming hand's beam (the controller's aim pose, as the bottom screen's beam always
  was; not the weapon's axis) on it is a touch at u x 320, v x 240 and presses no ZL or ZR, so
  neither Fire route (Original's L with either trigger, Fire and Power's L) sees it. Pulled off
  it, or with the wrist down, it fires as always. A grip on it grabs nothing
  (`PanelGeometry::bottom_fixed`). It is no screen for the toast's depth or the stats' placing.
- **Measured (mock, Chapter 1's air battle, Direct Aim with the right hand, the wrist screen on
  the left wrist):**

  | Phase | Wrist screen | Touch (3DS px) | The game's held word during the pull |
  |---|---|---|---|
  | wrist down (rest), pull pointing ahead | not submitted | none | L and ZR (a shot) |
  | wrist up, centre at -0.06, -0.28, -0.30 m | submitted, 0.10 x 0.08 m, facing 0.0 deg, 0.41 m | | |
  | tap at u 0.50, v 0.50 | | 160.0, 120.0 | no L, no ZR |
  | tap at u 0.25, v 0.75 | | 80.0, 180.0 | no L, no ZR |
  | tap at u 0.85, v 0.20 | | 272.0, 48.0 | no L, no ZR |
  | wrist turned 50 deg away, pull at where it was | hidden (facing 50.0 deg) | none | L and ZR (a shot) |

  Two runs of that script (w2; w3 with taps at u 0.10 and 0.235, v 0.91, the power orbs' place:
  32.0, 218.4 and 75.2, 218.4 px, as expected), the same results. With the mock's pose trace the
  submitted layer is at -0.06000, -0.28000, -0.30000 with the orientation the script asked for,
  and the beam's dot during a tap 5 mm in front of the tap point along the screen's normal.
  The right wrist (w4r: Motion Controls' Left Controller, the left hand aims and taps): the
  layer at 0.06000, -0.28000, -0.30000, the left one's orientation mirrored; the same three
  taps at 160.0, 120.0, 80.0, 180.0 and 272.0, 48.0 px with no L, ZL or ZR; the control pulls
  (wrist down, wrist turned away) L and ZL (a shot).
- **What a tap does in the game (w5: taps held 1 s, `AZAHARFRAME_KIU_LIGHTGUN_TRACE=1`, the
  air).** The touch reaches the game: at 160, 120 and 272, 48 the aim object's flags at `+0x84`
  went 0x41 to **0x43** for the length of the touch (a stylus touch, the game's own touch aim),
  and back to 0x41; at 32, 218 (the power orb's place) they stayed 0x41 (a button there, not the
  aim area; I). The reticle stayed where Direct Aim wrote it (`reticle` = `written` = the ray's
  pixel) all through. But the aiming hand pointing down at the wrist is itself a Direct Aim ray:
  the reticle went to the frame's lower left edge (0.5, 222 with the ray at -4.9, 222) while
  the beam was on the wrist. In the air nothing turned; **on land the reticle past the aim box
  turns the camera (the clamp above), not run on the desktop**: a tap there may turn the view.
  Next step if it does: hold Direct Aim's reticle (and the 6DoF aim) where it was while the beam
  is on the wrist screen. (kiupanel: run on land, it did not turn; the hold is in, see "The
  wrist screen holds the aim".)

### The wrist screen holds the aim (AzaharFrame kiupanel)

- **Land, measured (mock, Chapter 1's land battle, state slot 5, Direct Aim with the right hand,
  `AZAHARFRAME_KIU_WRIST_SCREEN=1`; the land camera's yaw at cam `+0x2AC` polled every 10 ms).**
  Before (integ17): the yaw did not move at all (0.00 deg in every phase: head down 40 deg, the
  wrist up, three taps, the wrist turned away, a held trigger); the reading's own check, the
  right stick held right for 1 s, turned it 90.25 deg. Why no turn: in first person the aim box
  hook (`AimBox`) makes the land aim box the whole screen, so the game's clamp never turns the
  camera, and Right Stick turning (on in first person) stops the Turn Zone (`kiu_probe.cpp`:
  `out.turning = false`); the probe's own line reads "air, box 0.5 to 399.5" on land for that
  reason. What did happen:
  the reticle ran from 203.5, 124.5 to the lower left with each tap (57.9, 190.8; 78.6, 191.7;
  51.3, 207.8; 108.3, 175.8 of 400 x 240).
- **The hold** (`AZAHARFRAME_KIU_WRIST_HOLD`, default on; `vis/wrist_screen.h` WristAimHold,
  controls.cpp): while the aiming hand's beam is on the wrist screen as the touch takes it
  (`Input::PanelUnderRay` of its aim pose: the same hit as the touch), the ray first person
  aims and shoots with (`FirstPerson::PublishHand`) is the last one from before, republished
  each frame; 250 ms after the beam leaves, it is free again. After (fix5, the same script):
  the reticle stayed at 126.3, 163.1 through the three taps (it had moved there on the hand's
  way in: the script's three poses between rest and the tap, before the beam reached the
  screen); the taps landed at 160, 120 / 80, 180 / 272, 48 with no ZR or L; the yaw 0.00 deg
  all through; "the aim held" logged as the beam reached the screen and "free again" 250 ms
  after the wrist hid. The worst case (after the change, with the hold on and off): the hand 60
  deg left and 70 deg down with the wrist hidden put the reticle on the frame's bottom edge
  (y 239.5) for 1.5 s, and the yaw still did not move. On the headset the hand sweeps in, so the hold starts where the beam
  enters the screen: the reticle follows the beam until then.
- **The stray drag.** A trigger still held when the wrist turned past 40 deg kept the touch:
  before, the aim object's flags stayed 0x43 (a touch) for all 121 samples of the 2 s after the
  wrist hid. Now the mapper ends a touch that began on the wrist screen as soon as the bottom
  screen is not it (`Mapper::touch_on_fixed_`): released where it was (160, 120) on the frame
  the wrist hid, the flags back to 0x41 for all 121 samples,
  and the held trigger blocked (no ZR or L) until let go.

### The talk panel for reading only (AzaharFrame kiupanel)

- **Its pose, measured (mock, Chapter 1's air, the head level at the origin,
  `MOCK_XR_TRACE_POSE=5`):** the bottom quad at 0.000, -0.325, -0.731 m, turned -24.0 deg
  about x, 0.489 x 0.367 m: 0.800 m away, 24.0 deg below straight ahead, 34.0 x 25.8 deg (what
  the code computes).
- **Before, it took the beam** (it was controls.cpp's pointable Secondary slot): a trigger
  pulled with the hand held low and centred (0.08, -0.35, -0.30 m) pointing straight ahead
  touched the Secondary Screen (flags 0x43, 32 samples) and fired nothing (ZR 0); aimed at the
  panel's middle or its lower part, the same. The rest hand (0.25 m to the side) straight ahead
  missed it and fired.
- **Now** (`ScreenLayers::bottom_pointable` false for it): the panel is drawn but is no target
  (no touch, cursor or grab). Measured with the panel shown (talkfix): all four pulls fire (ZR
  and L: 23, 28, 13, 30 samples), no touch (0x43: 0). The wrist screen is the one to touch. With
  both switches the wrist wins while it is up (the bottom quad at the wrist, 0.10 x 0.075 m,
  0.415 m away) and the talk panel is back when it is down.
- **On the committed build** (ffe6b52cf, FP_TALK=1 set): the same, the four pulls fire (ZR 26,
  31, 16, 30 samples), no touch; land (fix5b) the same hold and drag numbers, and with FP_TALK
  unset no 0.49 x 0.37 m quad was submitted in 1800 frames (the default is off).
- **Still off by default** (`AZAHARFRAME_KIU_FP_TALK=1` to show it): the owner has not seen it on
  the headset and finds first person's view full already. A candidate for default on after that
  check.

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
- Other regions and game versions: USA Rev 1 is mapped ("Other builds"); USA Rev 0 (same title,
  its code check differs) and the Japanese game have no table and stay off.
- Multiplayer: not run. The shot stub compares the owner with the local player's actor (found
  through the local aim object), so other players' shots should pass untouched (I).
- (Settled, "Shots from the hand" below: the game's own aim quaternion at a shot is read by the
  stub from the caller's frame.)
- Shots outside the one spawn call (dash shots, melee, powers) are not hooked.
- Whether the rail's axes always are the world's (they were in Chapter 1's air section).

### Angel's Aim for 1.3.0 (AzaharFrame kiu131)

#### Pit's own model, by its draw: the doubled weapon

- **What it was (V, desktop).** Pit's own weapon, not a bullet. The actor's model node
  (`[actor + 0x34C]`, class vtable 0xD1E990) has child nodes (class 0xD1E880, `+0x1C` = the
  model + 4) for the arm and the weapon. While he fires they get world scale 1 even with the
  actor's local scale at 0.001 (kiupit's hide), so on land a full-size see-through blade showed
  from his feet to the reticle. In the air he was never hidden, so his raised weapon's tip
  showed ahead of the eye. `SHOT_AHEAD` 0, 6 and 15 gave the same ghost; `HIDE_PLAYER=0` showed
  him whole. The scene camera's near plane (0xE14BBC: 6, 10, 16) and the node's `+0x3C` flags did
  nothing.
- **The fix.** The model's word `+0xB4`, byte `+0xB5` (e.g. 0x14010101): 1 drawn, 0 not.
  Clearing it hides his body, wings and weapon. On land the game sets it again about every game
  frame (median 33 ms), so the VBlank pass clears it each time. In the air the game rarely sets
  it again. Done in first person, land and air, unless Third Person or the eye offset
  moves the eye (`KH::HideByDrawInFirstPerson`, `KH::ModelDrawWrite`). It puts the byte back only
  on a node it cleared (or, once a run, one a save state brought cleared). The log says it once
  per change: `the player's model hidden by its draw (first person, land)` / `drawn again`.
- **Measured (V, mock).** On land (the owner's Chapter 2 state, the blade, firing), no ghost
  with `draw`; with `HIDE_BY=scale`, the ghost. In the air (Chapter 1, the palm, 6 s of fire),
  `draw` against `HIDE_PLAYER=0`: the same scores (0/360/600/720/720/960) and HP (89), and the
  same picture brightness.
- **Not checked.** The blade in the air (no such state here); the palm's ghostly hand on its own.
  Hits on Pit are not drawn and are not changed.

#### Bottom Screen (`vr_kiu_bottom_screen`: Off, Persistent, Wrist)

One per-game row replaces Talk Panel and Secondary Screen on Wrist
(`Kiu::FirstPerson::BottomScreenNow`). While it is Off, the two old keys still decide (the wrist
wins), so old files keep what they had. Choosing it in the menu clears them. The test
switches `FP_TALK` and `WRIST_SCREEN` act on top (`BottomScreenWithSwitches`).

- **Persistent.** The talk panel's place (24° below, 34° wide, 0.8 m) moved by
  `vr_kiu_bottom_x` / `_y`: degrees right of and below it (`PersistentInView`: always 0.8 m away,
  facing the eye). It is for reading only: no touch, cursor or shot, as kiupanel. Either grip
  pressed with its beam on it holds it (`Vis::StatsGrab` in the head's frame). While held,
  that hand's grip does not reach the game (so Grip Frees Aim and grip turns do not happen
  then). When it is let go, the place is saved for the game (MenuSetting KiuBottomX/Y, in
  tenths of a degree, taken while the menu is closed). Reset Bottom Screen Position (a menu
  button) puts both back to 0. V, mock (land, `MOCK_XR_TRACE_POSE=5`): the beam on it, grip,
  hand turned 20° right and 10° up, let go at 12.1° right and 5.0° up. The game's file then
  had `vr_kiu_bottom_x=12.1`, `vr_kiu_bottom_y=-5`, and the next start showed it there.
- **Wrist.** As kiuwrist, on the Wrist Side (below), 10 × 7.5 cm times the size.
- **Bottom Screen Size** (50 to 200 %): the Persistent panel's angular width, capped at 100°.
  On the wrist, its metres (up to 20 cm). V: 60 % 0.29 m and 150 % 0.76 m wide at 0.8 m; the
  wrist at 200 % 0.20 m wide, still beside the stats.
- **Bottom Screen Opacity** (20 to 100 %): the XR present shader writes the opacity as alpha,
  colour premultiplied (`layer` push constant / 1000; `layer` % 1000 is still the dim). The
  bottom quad gets `XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT` below 100 %. V: the
  dumped bottom swapchain's alpha is 0.60 and 0.30 at 60 and 30 %. That the Frame's runtime
  blends it is not checked here (the mock does not composite).

#### Wrist Side (`vr_wrist_side`, global: Back, Inside)

For both wrist displays: the Bottom Screen on the wrist, and Stats Position On Wrist in every
game (`Vis::WristLocalPose(side)`). Inside is the kiuwrist place (the palm side,
`x = 0.022`). Back mirrors it across the handle's middle (`x = −0.0135`) to `x = −0.049`,
facing out, tilted 10° the same way, so it is the same distance from the wrist. It still reads
toward the hand, so it is upright and reads left to right with the forearm across the body.
The right wrist is the left one mirrored, as before. The facing rule is unchanged (shown under
35°, hidden past 40°), so each side shows in its own pose. Back shows with the back of the
hand to the eyes (the watch pose) and Inside with the palm to the face; the other pose and
the playing pose hide both. Unit tests: `[kiu131]` in `kiu_wrist_screen.cpp`.
V, mock, both sides, both wrists (Motion Controls' Left Controller for the right): shown and
hidden as above. With the stats on the left wrist, the screen sits 11 cm (more when larger)
toward the elbow, not overlapping. The left-hand poses come from the test's `aim_for`.

#### The reticle's size (Reticle Size, `vr_kiu_reticle_size`)

- **Where the reticle is drawn (V, desktop).** It is not one of the CPU-shaded sprites that
  kiu2d turns. Scaling every CPU-shaded draw 2.5× about its middle grew the shots' sparks and
  glow, but the ring stayed the same size. The draws seen only while firing (a per-second
  count of texture 0 per draw, air) include one GPU draw with a 64 × 64 texture at 0x1810AF00
  (format 9), 3 a frame in each eye's buffer: the reticle (ring, diamond). The same address is
  the reticle on land too (the owner's Chapter 2 state).
- **The scale.** In first person, the draws with that texture (`KH::kReticleTexture`;
  `AZAHARFRAME_KIU_RETICLE_TEX=<hex>` for another) are not accelerated. They go through the
  software vertex path, and their vertices are scaled about the draw's middle on the screen
  (`RasterizerAccelerated::TurnSoftwareVertices`, before the kiu2d turn;
  `video_core/kiu_view_tap.h` `g_reticle_texture`, `g_reticle_scale`). At 100 % nothing changes
  (the draw stays on the GPU). The aim, the shots and the lock-on marks are not touched.
  `AZAHARFRAME_KIU_RETICLE_SIZE=<percent>` (live, 30 to 150) wins over the setting.
- **Measured (mock).** Air (Chapter 1), firing, live 100 → 50 → 150 → 100: the ring half and
  one and a half times as wide; the lock-on triangles the same; the score kept rising (480,
  840, 1080, 1560). Head turned 20° left and 10° down, 99 % (the CPU path) against 100 % (the
  GPU): the ring in the same place, within a few pixels. Land (Chapter 2, the blade): 50 and
  150 % the same way.
- **Not checked.** Other reticles (a charged shot's, other weapons' if they differ): one with
  another texture keeps its size. The address is the game's VRAM layout, which another chapter
  could change: the reticle then keeps its size, or (a risk) whatever other texture sits at
  that address is scaled instead. Off 100 % only.

## Other builds (AzaharFrame kiuusa)

**USA Rev 1** (title 0004000000030100, text 0x100000–0xC950F0, md5 c8e49bee…) is mapped; every
other build is off.

- **One table per build.** `kiu_turn.h` `Build` names its title, code check and the classes
  (aim land/air, camera, owner, model, actor; the reticle texture). `kiu_hooks.h` `Code` names its
  patch rows, context rows and stub image. `MatchBuild(title, memory)` picks the build whose
  title and nine code words both match, so USA Rev 0, which shares the title, stays off. The
  European game's tables are those above; its behaviour is unchanged.
- **How USA Rev 1's addresses were found.** Both builds' text was dumped through the RPC server
  (the dumps stay local). `tools/kiu_hooks/relocate/` maps one onto the other:
  - `codemap.py`: unique 4-word anchors give a piecewise shift (303 segments). Every branch is
    checked to reach the mapped target; the only misses are float literals.
  - `datamap.py`: pairs the pointer literals. Data and bss are at +0. Rodata is +0 below
    0xCAC13C, then −0x90 or −0xA0.
  - `resolve.py`: every hook address and word, checked (93 of 93).
  - `gen_usa.py`: writes the rows.
  The code words at the patch sites are the same as the European game's, apart from branch
  offsets.
- **Stubs.** The stubs are position-independent. The USA image is the European one with its six
  return and call literals relocated (`ImageFor`, `kStubTargetsUsaRev1`). It goes at the same
  `0xC95380`, with the mailbox at `0xC95F00`: USA's text ends at 0xC950F0, and its tail up to
  0xC96000 is all zero with no word referring to it.
- **Seen on the mock (V, USA Rev 1, mode 4):**
  - every group goes in;
  - shots follow the hand's ray (0.00° on land; 0.00–0.21° for three shots in the air);
  - the 2D sprites turn with the head;
  - Reticle Size works (the same texture, 0x1810AF00), and so does Third Person;
  - the Bottom Screen's talk and wrist test switches work, and the touch reaches the game;
  - a 345 s session ran without a crash.
- **Not mapped for USA Rev 1:** the C-Stick & Gyro mod leave-out and the Recommended preset are
  for the European game only. `kiu_range.h` (unused) is not mapped either.
- **The player's model.** It is reached through the actor's model link (`FindModel`:
  `[[actor + 0x1E4] + 0x24]`, whose `+0x1C` is the actor). `[actor + 0x34C]` holds the model only
  in some states, in both builds; it is now the fallback.

### Angel's Aim for 1.3.0, continued (AzaharFrame kiu133)

#### Pit's model node, found: the doubling while firing

- **The cause (V, desktop).** kiu131's hide read the model only at `[actor + 0x34C]`. That word
  holds it only in some states. In the owner's firing air state it is 1, so nothing was hidden
  and the log had no `hidden by its draw` line. The same was true in the Practice Range and
  Chapter 1's land section. The actor holds an inline holder {class 0xD760B8, the actor,
  0xffff0001, the model}, at a place that differs by battle: the model pointer is at + 0x344
  (the owner's air state), + 0x34C (Chapter 1 air, Chapter 2 land), or + 0x3CC (the Practice
  Range, Chapter 1 land).
- **The fix (`kiu_model.h`, `KiuModel::FindModel`).** It tries the actor's link first:
  `[actor + 0x1E4]`, whose + 0x1C is the actor and + 0x24 the model. This is the kiuusa lane's
  reading. It holds in every state read, and in one USA state the link is on the heap. Next it
  tries the offset found before, then + 0x34C, then the holder's shape from + 0x300 to + 0x480.
  Any result must be of the model class. It is looked up again when the actor changes, each
  VBlank while it is found, and every 30 VBlanks while it is not. The log says it once per
  change: `the player's model node … (through [actor + 0x1E4])`, `(actor + 0x…)` or
  `not found`.
- **Measured (mock, deterministic pairs: held, the same steps, RT from 5 s).** These pairs
  compare `HIDE_PLAYER=0` with the fix:
  - The owner's air state while firing: a blue and white blade at the reticle, gone with the
    fix.
  - Chapter 1 land with the palm: the ghostly hands are gone, and the shots' glow stays.
  - The score keeps rising while he is hidden (23710 → 25340).
  - The Chapter 2 state 9 is saved on the pause screen, so it was not usable.

#### Camera (`vr_kiu_over_shoulder`, `vr_kiu_game_camera`: First Person, Third Person, Game's Camera)

One choice row stored in two keys, so old files keep their meaning. If `game_camera` is on, the
view is the Game's Camera; otherwise `over_shoulder` decides
(`Kiu::FirstPerson::ViewFor`, `ViewOverShoulder`, `ViewGameCamera`).
`AZAHARFRAME_KIU_GAME_CAMERA=0|1` wins over the setting. `EYE_OFFSET` turns it off.

- **The Game's Camera** drops only the eye group (the eye at Pit's head and the shoulder offset).
  The rest of first person stays: the head turns the game's view, the shots leave along the
  hand's ray, and the reticle, the 2D sprites and the wide view all work. Pit is drawn, since
  the hide by draw needs the eye at his head. The weapon in hand is left out in both outside
  views (`OutsideViewOn`), because Pit holds his own.
- **Measured (mock, the owner's air state).** Firing, the head turned 30° and then 30°/-15°, the
  hand moved. Pit and his weapon are drawn behind the reticle. The game's shots leave his weapon
  and hit (the score rose). The sprites face the eye, and there is no second weapon.
- **Not checked.** Comfort: the game's camera moves on its own (sways, turns at corners) with the
  head on top. That is for the owner to judge in the headset.

#### Pit Opacity (`vr_kiu_pit_opacity`, 20 to 100 %, the outside views only)

- **How the game draws him (V, `AZAHARFRAME_KIU_DRAW_LOG=<any new value>`, one line per draw for
  40 ms).** About 6 draws (vertex data in VRAM near 0x181E8000) for the body, the wings and the
  weapon, plus glow draws (additive, no depth write). When the camera is near, the game's own
  fade switches his draws from opaque (src 1, dst 0) to src alpha. No word in the model node
  differs between faded and solid.
- **Finding his draws (`kiu_pit_opacity.cpp`).** This runs once per model node, after the view
  has settled:
  - It collects the depth-tested draws' first vertex address for 4 VBlanks with him drawn.
  - It skips 3 VBlanks, then collects for 4 more with his drawn byte cleared.
  - His draws are the ones only in the first set (`KiuModel::PitDrawsFrom`). More than 48 means
    the calibration failed, and none are used.
  - The log line is `Pit Opacity: N of his draws found`. A failed try (none, or more than 48)
    is tried again, up to 3 times, each skipping 2 VBlanks more.
  - He blinks out for about 7 VBlanks (~0.1 s) the first time an outside view below 100 % is
    in effect for a model node (a new battle or a save state load brings a new node).
- **The blend (`vk_rasterizer.cpp`, `kiu_view_tap.h` `PitDraws`).** Those of his draws that the
  game draws opaque get the constant alpha (src ConstantAlpha, dst OneMinusConstantAlpha). The
  next draw's `SyncDrawState` restores the game's blending. Depth is unchanged, so he still hides
  what is behind him in depth, and nothing else is touched. Draws the game already blends
  (its own fade, the glows) keep their own blending.
  `AZAHARFRAME_KIU_PIT_OPACITY=<percent>` (live) wins over the setting.
- **Measured (mock, the Practice Range).** The Game's Camera at 100, 50 and 20 %, then Over the
  Shoulder at 20 %: his body, wings and legs are see-through, and the floor and the target
  show through him. 6 draws were found, with 64 draws while drawn and 18 while hidden.
  The owner's air state, firing, 50 % from the start: 5 found (108 with him, 42 without); in
  the Game's Camera and then Third Person he is see-through, and the enemies, the lock-on
  marks and the shots stay solid. No weapon in hand in either view.
- **Risks.** The addresses are learnt once per model node. If the game moves his vertex data
  later (another weapon, a power), he stays solid until the next model node. A depth-writing see-through draw hides his own parts behind it (a back arm), as in any
  one-pass blend.

**The right stick's click (AzaharFrame kiucam, `vr_kiu_stick_click`: 0 Switch Camera, 1 Swap Screens; per game, live).** In a battle a tap switches First Person and Third Person (First Person from the Game's Camera), in both keys as the Camera row does; the Qt side writes the game's file, as for the row. Not in a cutscene, the game's pause screen or one of its menus (`kiu_ui.h`, read each VBlank), nor with the screens swapped: there it is Swap Screens (`CameraTap::Switches`). Drawn by the projection layer, the picture fades to black in 0.05 s, the camera switches, and it comes back over 0.05 s after 0.15 s of black (0.25 s in all; a factor in the screen's shader, nothing waits: the game, the controls and the shots go on). The black has a fixed end: a picture carries its head pose's time, not the camera it was drawn from. Primary Screen as Panel has no fade: it switches at once.

**1.3.0: the Game's Camera is held back.** Its view was broken on the headset (Pit far off in the air; the camp's view wrong). `Menu::kGameCameraOffered` (`menu/angels_aim_tab.h`) is false: Camera lists First Person and Third Person, and a saved `vr_kiu_game_camera` is not used. `AZAHARFRAME_KIU_GAME_CAMERA=1` still turns it on for work on it.

### Powers (AzaharFrame kiupowers; `vr_kiu_power_menu`, off)

- **Power Menu** (`vr_kiu_power_menu`: 0 Off, 1 Off Hand, 2 Health Bar, 3 Middle, 4 Score; `vr_kiu_power_menu_opacity`, 20 to 100; per game, live,
  Angel's Aim only; no menu rows yet): the powers on land and the dots in the air, there. Over the hand the powers only while another is picked,
  facing the eyes and upright (`Billboard`: it follows once they are 2 degrees off); with the HUD all battle, the name under the row (`BelowRow`),
  moved by `vr_kiu_power_menu_across` and `_height` (degrees, -40 to 40, from the place). In the middle the linked HUD's two pieces make room
  (`KiuHud::Wanted::link_gap`); not linked and too close, it is under both. `AZAHARFRAME_KIU_POWERS*` and `_POWER_DOTS*`, when set, win (`SettingsFor`).

The game's power panel is at +0xB64 of the battle UI's 2D context (`[0xEE3098 + 0x140]`, `kiu_ui.h`), on the bottom screen
(`kiu_powers.h`; its code words are checked first, EUR and USA Rev 1; USA never run).

| Field | What |
|---|---|
| +0 & 0x4000 | in the air (the dots), else the bar |
| +4 & 0x20000, & 0x10000000 | the name bubble is up; a power was just used |
| +0x104 f32 | the name's timer, refilled by each step |
| +0x118, +0x184, +0x11C + 8n | the selected place, how many, a power (uses in its first byte) |
| +0x1B4 f32 | the charge in the air, 0 to 3 dots (about 0.0194 a second) |

- **The bar's draws** (`video_core/kiu_powers.h`). Draws of the bottom pass (viewport 240 x 320) of that context. Read from guest memory, told
  apart by place, texture and TEV; the wanted ones are drawn again from the same textures (a pack's too) into a transparent strip under the
  bottom screen's picture, in the same XR image (`RendererVulkan::DrawKiuPowers`). In the game's order: paper (the selected place's piece tinted
  FFD200, alpha 0xBF: "the gold"), the name bubble (u 183 to 206, v 72 to 96 of the sheet's 256), the cards, the icons, the counts, the arrows,
  the name. Left out: the paper but the gold piece, the cards.
- **Their TEV** (any other is not drawn again): paper, bubble, arrows: texture x vertex colour; icons and counts: the texture mixed toward the
  vertex colour by its alpha, alpha = the texture's x a stage constant; the name: the vertex colour, alpha = the texture's red x the vertex alpha.
- The cards, icons and counts are cut to x 8 to 124, as the game's scissor does. The bar's idle fade (its alphas, 0.5 seen) is divided out; the
  opacity multiplies the finished strip, so nothing shows through the icons.
- **In the air** any of D-pad Left, Right and Down spends a dot. With Fire and Power in Angel's Aim the bumpers send nothing there
  (`AZAHARFRAME_KIU_SKY_BUMPERS=1`: as before); the off hand's trigger (D-pad Down) does it.
  V (desktop): both bumpers: nothing held, the charge goes on; the trigger: 1.736 to 0.737, once. The player's own charge is
  `[0xD635E0] + 0x1238`; the panel's is set from it, so writing the panel's does not stick.
- Switches: `vis/power_menu.h`.

### Single screen (AzaharFrame kiu1s; test switches, all off)

Where to look was learnt from **UprisingHack** (https://github.com/patricksmill/UprisingHack,
GPL-3.0-or-later), a PC port of the game, from its notes on USA Rev 1. Each address was found again
in both builds by its code; no code or layout of it is used.

- **The battle UI (`kiu_ui.h`).** One 2D context, `[0xEE3098 + 0x140]`, for both screens: the top pass
  (EUR 0x60D538, USA 0x60D3EC), the bottom pass (0x60DA88, 0x60D93C). 30 code words that name the
  fields are checked first; another word and nothing is read.

| Field | What |
|---|---|
| +0x1C, +0x28 | the vertices' base (physical), the quads' index offset (after two buffers of 0x60 a quad: 400 quads). V: the HUD's draws, to the byte (land and air; the first HUD draw is quad 0, they add up to +0x128) |
| +0x31B8 bit 0 | the UI draws; `& 0x10100`: a page instead of the HUD |
| +0x218C bit 0 | a page (a dim band and two columns) instead of the bar; the score is still drawn. I: the battle's results; never seen set |
| +0x243C & 0x801 | the pause screen is drawn |
| +0x3124 (+0, +1, +3, +4) | the talk's message: shown, shown, its screen (0 top, 1 bottom), its line |
| +0xB64 | the bottom screen's own UI, flags first: & 0x800 another picture instead of all below (its pass, +0x31B8 & 0x400, too) |
| +0xB64 +0xD8 | the portraits' object (EUR 0x9E2D4C draws it): +0 & 0x3000000 hidden, +8 & 0xF its four places in use. V: set as they start to fade in, clear 0.4 s after the words, as they are gone |
| +0xB64 +0x1FC, +0x200 | an illustration's share of the picture (the portraits have the rest); with flag 0x1000000 only while one of the pieces at +0x200 is up. V: Chapter 1's flight, Medusa's line: 0 to 1 in 0.3 s, 1 for 10 s |

- **`AZAHARFRAME_KIU_PAGE` (on; `0` off).** The 2D is a page (`PagePlace`) outside a battle as before, and also (45 degrees high, `PagePlaceInBattle`)
  while the game itself draws a page instead of the HUD, two VBlanks in a row, or a menu is up: its number is the byte `[[[0xE6F98C] + 0xC] + 0x72C]`
  (classes 0xCFC660 and 0xCFC3B4; USA 0xA0 lower), the holder null in a battle. V: the title 0, the page after Pit falls 51 (CONTINUE). That page comes 2 s after
  the battle's camera is out. The battle's results (state: flag 0x100 of the context): whole, its rules too; its dim's corners are put exactly at the frame's
  and not turned, so it covers the whole view. The hub (menu 3): the game's words and number are flat 2D, but the veil behind the words, the heart bag and its shadow are
  depth tested cards in front of the room's camera (one 64 x 64 RGBA4 texture, 40 units away, 6 or 12 vertices; nothing else of the room is: census). They all go to the
  HUD's frame (`PageInView`, `MoveCard`), in front of the room's depth; the veil, which hangs off three sides of the game's screen, is drawn again with all sides fading (`SoftVeil`).
  V: bag and number together, following the head, turned 15 degrees and 35 down. The room's own sheets and floor reflection still end where the game's frame did.
- **`AZAHARFRAME_KIU_HUD_EXACT` (on; `0` off).** The HUD's draws are the top screen's draws of that context
  (vertex base and index offset, `KiuHud::InBatch`) and no others but the console's battery (the dialogs' context; `IsBattery`), each moved as one by its own middle.
  The flush (EUR 0x92F5A0) only adds to a list that is replayed later for both eyes, so a draw's caller
  is not known. Batch not known: the guess (`MayBeHud`), as without the switch. V (air, land): every HUD draw logged as exact; on land the bar and score are on the same pixels as by the guess (best match at no shift).
  Clip y is across to the LEFT: until kiuhud the guess had the bar's and the score's groups swapped, so the bar's right end (0.3 to 0.45) went with the middle.
- **`AZAHARFRAME_KIU_TALK_TOP=1`.** One word (EUR 0x60D804, USA 0x60D6B8: `bne`, the message is the bottom
  screen's, to `mov r0, r0`), group "talk on top": the top pass draws the line with the game's own function for
  the top screen (EUR 0x9E9D2C), as Subtitles: Upper does. The bottom keeps its line, the setting is not touched.
  V: Practice Range, the words of the bottom screen on top from the switch on (two quads more in the top pass).
- **`AZAHARFRAME_KIU_PAUSE_PANEL=1`.** While the pause screen is drawn in a battle, the Secondary Screen is a
  panel in front (36 degrees wide, 4 below where the head looked, 0.8 m), pointable, still where the head was as
  the game paused, upright (`NoRoll`; at most 50 degrees up or down); a re-centre takes the head again. The Secondary
  Screen up on the wrist wins. Its layer is the portraits' (`LayerFor`): there, shrunk, whenever the view is on, so pausing
  adds none. Direct Aim holds no aim on it and the beam is the usual one.
  V: START: up in the same frame, 6 layers before and after; head turned 25 left and 12 down: the panel 24.6 left, 16 down;
  Resume, Retreat and Yes by the beam and the trigger (246,83 and 225,171 asked and touched, ZL, ZR and L 0), the hub after.
  Not run: the Options pages, a re-centre while paused. The mock does not composite, so the panel over the picture was not seen.
- **Character Portraits** (`vr_kiu_talk_portraits`, off; `vr_kiu_talk_portraits_opacity`, 20 to 100, per game, live).
  The setting asks for the card below; `AZAHARFRAME_KIU_PORTRAITS`, when set, wins (1 on, else off). The card is at its own opacity,
  blended as the Bottom Screen's. With no `AZAHARFRAME_KIU_TALK_TOP` set, the words go on the Primary Screen too (`TalkTopFor`;
  with the card or without) unless Bottom Screen is Persistent: on land the game has them below only.
- **`AZAHARFRAME_KIU_PORTRAITS=1`.** While the game draws the talk's portraits (or an illustration), that part of the Secondary
  Screen's own picture (rows 42 to 240 of 320 x 240, `kPortraitCut`; a plain card) is a quad with the head, 25 degrees to the off
  hand's side, 7.5 below, 16 wide, to look at only. No code patch. All through a battle the quad is there,
  shrunk to nothing between the lines and while paused, so a line changes neither the number of layers nor how the bottom screen is
  drawn. Not while the Secondary Screen is on view anyway (Persistent, the wrist, the pause panel). V: an air battle: up with the
  portraits, the rect 1280 x 792 at 0,168 of 1280 x 960, away while paused and 0.35 s after the words, 6 layers all the time. The
  card at 13 below (the plan) was over the words' left end at their usual size, so it is above them. On land the same (25.3 left,
  7.2 below in the trace), away with Bottom Screen: Persistent. An illustration is in the same cut (rows 55 to 184) and shows in
  the card. A boss's card is not shown.
- **The characters alone** (`video_core/kiu_talk.h`; with Character Portraits, `AZAHARFRAME_KIU_TALK_CUTOUT=0` for the card). The draws of the
  bottom pass that are the portraits' and the illustration's are drawn again, coloured as the game does, into a transparent copy of the
  bottom screen under its picture in the same XR image (below the powers' strip); the card's quad shows that, all rows (the tallest
  portraits reach over row 42). No frame, backdrop, words or power bar.
  - A portrait (EUR 0x9E2D4C): one draw, one quad of 256 x 256 (two while wiped in or out: the upper one's top see-through), x -5 or 69 +
    the place's, mirrored by flag 0x400; its texture 256 x 256 ETC1. TEV, two stages: (1 - texture) x AAAAAA x 2, then 1 - that, times the
    vertex colour (the place's +0x20: its tint and fade); alpha min(1, 2 x (red + green)) x the vertex alpha
    (drawn again without its lowest 4 %: a pack's black is not quite 0).
  - An illustration (0xA0F9B8): an untextured black quad (320 wide, 130 high), its pieces (one quad a draw, texture x vertex colour), four
    untextured quads (bands and lines). Pieces are cut to rows 56 to 184; the black beside the picture is left out.
  - How many the game says it draws (`frame_xr/kiu_talk.h`: the places' colour and wipe, the pieces' alpha). Fewer found, or an illustration
    not as known, for 8 pictures: the strip has the card's part of the picture instead. The code another: that at once.
  - The opacity is on the whole, so nothing shows through a portrait in front of another.
- **The weapon in the hand** (no switch, first person): not drawn, nor Pit's arm and the muzzle's effects, while one of the game's
  menus is up outside a battle (`WeaponHiddenFor`, by its own `SteadyMenu`: the one-frame gaps between menus do not count). The
  Practice Range's shooting is a battle with no menu and the pause screen is none either: the weapon stays there.
- **`AZAHARFRAME_KIU_MENU_SCREENS=1`.** While one of the game's menus is up (`SteadyMenu`, MENUID 0 to 58, two VBlanks; gone
  after 12 with none), the normal two screens instead of the view (`ScreenState::game_menu`; the bottom one whatever was chosen
  for it, and its hide button does nothing). V: the title (0), the Cauldron (5), off into the battle (none 0.8 s before its UI is
  there), the pause and its Options pages (none), Retreat, the hub (3), the page after Pit falls (51: both screens, 2 s after
  the battle's camera is out). The game goes from one menu to the next within a frame.
- **`AZAHARFRAME_KIU_MENU_PANEL=1`.** The same menus, as a panel: the Secondary Screen 40 degrees wide, its middle 12 below, the
  Primary Screen above it, 32 wide, 14.5 above (the Cauldron: 34 at 14 below, 38 at 11.75 above), a degree and more between them
  (`MenuGapDeg`), 0.8 m, level, where the head was as the first menu came up; a re-centre moves it; the grips move neither
  (`PanelGeometry::no_drag`); no swap on it; it stays while a wrist is turned up. The layer is kept, shrunk, in the view
  between a menu and a battle (`LayerFor`). With either switch Direct Aim holds no aim in a menu: Keep Reticle on Screen made
  the aiming hand's beam touch nothing while the Primary Screen is a screen. The stick and the buttons go to the game as ever.
  V, all by the beam and the trigger, each pixel asked and touched, ZL, ZR and L 0: the title (0), the file (2), the main menu
  (4), the hub (3) and back; the Cauldron (5): a drag along the intensity bar (155,22 to 93,22: 4.0 to 2.2) and Move Out, the
  battle, Retreat, the hub again. A grip on the panel moved nothing. Not run: the rooms past the hub, a swap, a re-centre, a wrist turned up (the mock's wrist screen did not come up), the stick and buttons.

### 60 FPS (AzaharFrame kiu60; `vr_kiu_60fps`, off; `AZAHARFRAME_KIU_60FPS=1` / `=0` wins over it)

`core/file_sys/kiu_60fps.h`, EUR Rev 0 and USA Rev 1, put into the code as the game starts (the loader, an IPS made in memory);
code that isn't exactly the game's is left alone, with a log line. The setting is read at the start (a toast says so if it's
changed after). Run by the desktop's flat build and VR mock; see reports/102/rig60 for the runs.

- **Five words** make the game's loop 60: the vsync manager's rate and interval (30 and 2 to 60 and 1), the update's length
  (2.0 to 1.0 of a 60th), and what reads them. Nothing else of the game's counts updates where it was looked for.
- **The touch camera.** The game's touch speed at 60 was doubled where the stylus moved every other VBlank: an update with no movement
  in it kept the last speed that was not zero (a ring of two), so a step was counted twice. 25 words (`kTouch`) in the
  speed's update replace the ring by the mean of the last two updates, held for two more when it is zero, and ease it at 60 not 30. What isn't
  changed: the six-update history a flick's speed is the biggest of (100 ms at 60, 200 at 30).
- **Right Stick, Snap Turn.** By the host's clock (`kiu_turn.h`): no change.
- **Turn Zone.** A push is taken once an update: it's scaled by the game's own update length (`UpdateShareFor`).
- **Save states** carry the code and the rate they were made with. At a VBlank, if the code is the other of the two and the vsync manager's
  rate too, all of it is written over (`TurnTo`, logged) to what the run was started at (`g_run`, from the loader); otherwise it is left.
- **A mod's `exefs/code.ips`** with the same words (made from this table by the test `[.kiu60mod]`): the loader finds its code there already, puts nothing on and says
  so; `g_run` is 60 whatever the option. A code file that differs (a C-Stick mod's) is left alone with a log line.
