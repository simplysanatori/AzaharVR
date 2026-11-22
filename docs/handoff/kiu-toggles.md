# Handoff: Angel's Aim's first-person switches as settings (work/kiu-toggles)

> **Outcome (lane/kiu130, 1.3.0).** Merged, built with GCC/Clang (desktop x86-64 and the Frame's
> arm64) and tested (`[kiutoggles]`, `[kiuview]`, `[kiuturn]`, `[kiuwrist]`, the Qt and picker
> suites). The strings below are approved by the owner, the Turning choices too. Over the
> Shoulder checked on the desktop mock from its setting: the view behind and above Pit, Pit solid
> (not hidden), and the weapon in hand left out while he holds his own (KIU_HOOKS.md, "The
> doubled weapon"). The rest of this file is the Windows session's handoff, kept as written.


Branch `work/kiu-toggles`, from `ci/pcvr` 5aefa5f5c. Four player-facing test switches of Kid
Icarus: Uprising's Angel's Aim are now per-game settings with rows in the in-game menu. With the
defaults nothing changes for players, and a test switch, when set, still wins over its setting.

## The switch survey

Every `AZAHARFRAME_KIU_*` switch in the code (kiu_first_person.cpp, kiu_hooks.cpp, kiu_turn.cpp,
kiu_probe.cpp, kiu_weapon.cpp, controls.cpp, vis/wrist_screen.h, vis/weapon_fit.h,
vis/controller_models.cpp), checked against KIU_HOOKS.md, IMMERSIVE.md, TECHNICAL_NOTES.md and
the lanes' commits.

### Player-facing: made settings here

| Switch | Today's default | Why player-facing |
|---|---|---|
| `AZAHARFRAME_KIU_WRIST_SCREEN` | off | a way to see and touch the Secondary Screen in first person (the owner's example) |
| `AZAHARFRAME_KIU_FP_TALK` | off | the only way to read the game's talk in first person; the owner's notes call it "a candidate for default on" after a headset check |
| `AZAHARFRAME_KIU_SHOULDER` | off | a view choice (Pit in sight); the setting uses one fixed place, 2.5 m back and 0.5 m up, where the game draws Pit solid (KIU_HOOKS.md, kiufp3) |
| `AZAHARFRAME_KIU_SNAP_TURN` | smooth | a comfort choice for Right Stick turning, which first person always turns on |

### Player-facing, but not made settings (why)

| Switch | Why not now |
|---|---|
| `AZAHARFRAME_KIU_RIGHT_STICK_TURN` | always on in first person, its player-facing mode; the switch only adds it outside first person, a test of the turning |
| `AZAHARFRAME_KIU_HIDE_PLAYER` | the shadow lane (work/kiu-shadow) is making it three-state (unset: hidden on land in first person, `0` drawn, `1` land and air). A toggle must write `0` for off and leave the key out for its default, never `1`. Left for after that merge |
| `AZAHARFRAME_KIU_FP_FIELD`, `AZAHARFRAME_KIU_FP_EDGE` | comfort-like (a smaller picture with a border, a soft edge), but tuning values the owner has not seen on the headset; EDGE needs the projection path |
| `AZAHARFRAME_KIU_SHOT_LOCK` | the game's homing for shots from the hand; aa6dof2 chose no lock on purpose. An A/B until the owner wants aim assist |
| `AZAHARFRAME_KIU_FP_WIDE` | `0` is the old path and `2` an alternative render shape: an A/B of render paths, not a player choice |
| `AZAHARFRAME_KIU_FP_2D` | default on in first person; `0` only puts the sprites back where they were wrong (an A/B of a fix) |
| `AZAHARFRAME_KIU_ENEMY_KEEP`, `_ENEMY_KEEP_S`, `_GROUP_KEEP` | gameplay tuning (Angel's Aim's 12 s keep goes in by itself; `0` keeps it out for an A/B) |

### Debug, trace, probe, tuning, A/B (stay switches)

- Developer overrides: `FIRST_PERSON`, `AIM_NO_MOD`, `AIM_6DOF`, `SHOTS_6DOF` (on by themselves
  in Angel's Aim with Direct Aim), `EYE`, `EYE_OFFSET`, `FORCE_GYRO` (tests only), `NO_FOLLOW`,
  `NOCULL`, `BULLET_KEEP`, `NO_PLAYER_SHADOW` (an A/B; the shadow lane's work), `SHOT_TEST`,
  `SHOT_NO_LOCK`.
- Traces and probes: `HOOK_TRACE`, `FP_TRACE`, `FP_PROBE`, `CAM_TRACE`, `LIGHTGUN_TRACE`.
- Tuning values and gains: `WORLD_SCALE`, `PLAYER_EYE_M`, `FP_FACTOR`, `FP_TALK_AT`, `TURN_DPS`
  (Horizontal Turn Speed is the setting), `TURN`, `TURN_GAIN_X/_Y`, `STRETCH_EDGE`,
  `STRETCH_GAIN/_Y`, `STICK_RESET`, `STICK_LATCH`, `UNSTICK_BIT/_MOD/_EASE`, `GROUND_AIM`.
- The weapon in hand (the weapon lane's area): `WEAPON`, `FIT_<CLASS>`, `HOLD`, `ARM`,
  `ARM_WEAPON`, `ARM_FIT`, `ARM_ELBOW`, `PALM_MODE`, `PALM_FOREARM_CM`, `WEAPON_GLOW`,
  `GLOW_MOTION`, `FLASH`, `SHOT`, `OPACITY` (the last three tune existing settings).
- `WRIST_HOLD`: on by default, `0` an A/B of the aim hold.
- `FP_HUD`: taken out in kiufp3 (docs only).

## The settings added

All `Settings::values`, SwitchableSetting (global and per game), read and written by the Qt
config, reset with the rest (`RestoreGlobalState`), kept across the game's file being read back
(`ForEachMenuKey`), saved per game from the in-game menu.

| Key | Type, default | Switch that still wins when set | Read in |
|---|---|---|---|
| `vr_kiu_wrist_screen` | bool, false | `AZAHARFRAME_KIU_WRIST_SCREEN` | controls.cpp (`Vis::WristScreenOn`) |
| `vr_kiu_talk_panel` | bool, false | `AZAHARFRAME_KIU_FP_TALK` | kiu_first_person.cpp `TalkNow` (`TalkOnFor`) |
| `vr_kiu_over_shoulder` | bool, false | `AZAHARFRAME_KIU_SHOULDER` (`EYE_OFFSET` still wins over both) | kiu_hooks.cpp (`FirstPerson::ShoulderFor`) |
| `vr_kiu_snap_turn` | u32 degrees, 0 (smooth); 30, 45, 60, 90 | `AZAHARFRAME_KIU_SNAP_TURN` | kiu_turn.cpp (`KiuTurn::SnapDegreesFor`) |

"Set" means the switch has any value: `0` turns the feature off whatever the setting says.
What each feature does is unchanged; only where its on/off comes from.

## Where the rows show

The Settings tab's Immersive section, right under Immersive Mode, only while Kid Icarus: Uprising
is open (`mods.angels_aim.shown`, as Trigger Layout) and Immersive Mode is Angel's Aim (stored 4).
Not the Controls tab's Aiming rows, where Trigger Layout is: these act in Angel's Aim's first
person whatever Aiming is (Aiming Off included), and only in that mode, which is chosen on this
row. Turning sits with them for the same reason (Right Stick turning is on in first person
whatever Aiming is).

- Three switches and one segmented row (Smooth, 30°, 45°, 60°, 90°), each with a tooltip.
- Per game like the other Immersive rows: Global locks them while the game has its own value.
- The Per-Game count and Use Global Values for All count them only while shown, as the Settings
  tab does for every row (Render Margin's too).
- Azahar's Qt configuration has no Kid Icarus settings (Trigger Layout, the light gun's are
  config.cpp only), so no Qt widgets were added.
- The menu dump has four new shots: `kiutoggles-1` to `-4`.

## Strings (all PROPOSED, in `src/frame_xr/menu/angels_aim_tab.h`)

| Original | Proposed label | Proposed tip |
|---|---|---|
| `AZAHARFRAME_KIU_WRIST_SCREEN` | Secondary Screen on Wrist | Shows the Secondary Screen on your free hand's wrist. Turn your wrist up to see it, and point at it to touch. |
| `AZAHARFRAME_KIU_FP_TALK` | Talk Panel | Shows the Secondary Screen, where the characters talk, as a small panel below your view. |
| `AZAHARFRAME_KIU_SHOULDER` | Over the Shoulder | Moves your view behind and above Pit, so you can see him. |
| `AZAHARFRAME_KIU_SNAP_TURN` | Turning | How the right stick turns you: smoothly, or in steps of the chosen angle. |
| none | Smooth (Turning's first choice) | none |
| none | 30°, 45°, 60°, 90° (Turning's other choices) | none |

## Only syntax-checked

Nothing was built, run or tested. Every changed C++ file passed MSVC `/Zs` with the PC build's
flags: settings.cpp, config.cpp, citra_qt.cpp, controls.cpp, kiu_first_person.cpp,
kiu_hooks.cpp, kiu_turn.cpp, vis/wrist_screen.cpp, menu/tab_menu.cpp, menu_panel.cpp,
per_game.cpp, menu/menu_dump.cpp, and the tests tab_menu.cpp, kiu_view.cpp, kiu_turn.cpp,
kiu_wrist_screen.cpp, per_game_keep.cpp (with citra_qt.cpp's flags: the Qt tests are not in the
PC build). The pch-built files needed the precompiled header pointed at the worktree (the
shared syncheck.py reports false redefinitions for them). GCC (the Frame, `-Wall`) not run:
the one switch with no `default` (citra_qt.cpp's `MenuSetting`) has the four new cases.

Unit tests to run at integration: `[kiutoggles]`, `[kiuview]`, `[kiuturn]`, `[kiuwrist]`,
`[tabmenu]`, `[fixa]`, plus the menu dump.

## Checking on the headset (Frame build)

1. Kid Icarus: Uprising (European), Immersive Mode Angel's Aim, no `AZAHARFRAME_KIU_*` lines in
   `~/.config/AzaharFrame/test-switches.txt` for these four.
2. Settings > Immersive: the four rows under Immersive Mode, all off and Smooth. Choose another
   Immersive Mode: they go. Open another game: they are not there.
3. Secondary Screen on Wrist on: turn the off hand's wrist up; the screen shows, a trigger on it
   touches without a shot. Off: it never shows.
4. Talk Panel on: in a chapter's talk, the panel below the view. Off: none.
5. Over the Shoulder on: the eye behind and above Pit, Pit solid. Shots still go along the hand.
6. Turning 45°: a right-stick flick turns 45 degrees on land. Smooth: as before.
7. Per-Game and Global: change one in Per-Game, restart the game: kept. Use Global Value clears it.
8. A switch still wins: `AZAHARFRAME_KIU_WRIST_SCREEN=0` with the setting on: no wrist screen;
   `AZAHARFRAME_KIU_SNAP_TURN=90` with Turning Smooth: snaps 90.

## What's left

- The owner's approval of the strings, and whether Talk Panel should become default on.
- A toggle for `HIDE_PLAYER` after work/kiu-shadow merges (write `0` for shown; leave the key out
  for the default; never `1`).
- Maybe Field and Soft Edge as comfort settings, after the owner sees them on the headset.
- Angel's Aim's report (frame_mods.cpp) lists some Angel's Aim settings; these four (and Trigger
  Layout) are not in it.
- Over the Shoulder is one fixed place; a distance choice would need its own row.
