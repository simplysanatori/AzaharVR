# Handoff: Pit's "shadow" in Angel's Aim's first person (branch work/kiu-shadow)

The owner's report: in Kid Icarus: Uprising, Angel's Aim, Pit's shadow shows in first person and
looks "like the head".

## Cause and evidence

It is not the round shadow. It is Pit's own model, which the game draws see-through when its
camera is near him; first person puts the eye inside him.

- **The round shadow is already out whenever first person is on.** `want_player_shadow` is
  `GroupWanted(first_person, …)` (`src/frame_xr/kiu_hooks.cpp:507-508`), and `first_person` is
  `Kiu::FirstPerson::On()`, which is Angel's Aim in effect or the developer switch
  (`src/frame_xr/kiu_first_person.cpp:102-111`). No test switch is needed. Only
  `AZAHARFRAME_KIU_NO_PLAYER_SHADOW=0` keeps it in. The stub covers land only because the air
  has no round shadow at all (`src/frame_xr/input/KIU_HOOKS.md:551`, "The air": the list is
  empty).
- **What the owner's pictures show.** In `frame-shots/s2/20261122045200_1.jpg` and `…045158`
  (an air battle, the weapon in the hand), with the contrast raised, there is a pale see-through
  shape in the middle of the view, in front of the eye and between it and the reticle. It is a
  head with shoulders widening below it: Pit seen from just behind his head. Since the air has
  no round shadow, this can only be his model. `s3/20261122051523_1.jpg` is very dark. With the
  brightness raised it shows no round disc; there is a faint pale upright shape in its middle and
  a large dark angular shape at the lower right. It is too dark to be sure what those are.
  `s3/…050801` is the app list and `s3/…051000` is the pause screen; neither shows it.
- **Why the model shows (from the notes).** "Why Pit is faded" (`KIU_HOOKS.md:436`): the game
  fades its player when the camera comes near him. "Pit's own body" (`KIU_HOOKS.md:634`):
  looking down in first person on land shows "a see-through white shape of his legs and body",
  and first person did not hide him. The eye is 12.3 units up on land (in his head,
  `input/kiu_hooks.h:650`) and 3 up in the air (`kEyeOffsetAir`, `kiu_hooks.cpp:57`, used at
  `:563`), with his head ahead of it.
- **Why it was left that way.** `AZAHARFRAME_KIU_HIDE_PLAYER=1` (a scale write, 1 to 0.001) was
  seen to take him away on land (`KIU_HOOKS.md:205`). It was kept out of Angel's Aim because in
  the air it washes the picture out and he takes less damage there.

So the over-the-shoulder view is not the cause (it is a test switch, `AZAHARFRAME_KIU_SHOULDER`,
and the owner's pictures are first person). Neither is the stub being limited to a test switch.

## Changes (commit c40c850ae)

- `src/frame_xr/input/kiu_hooks.h:795` `HideInFirstPerson(first_person, land, shots_from_hand,
  eye_moved)`. First person hides Pit (the existing `HideWrite` scale write, `:776`) only:
  - on land;
  - with the shots from the hand (Aiming = Direct Aim). Their start is the mailbox's, so the
    shrunk model's muzzle does not matter;
  - with the eye at his middle. An eye moved by `SHOULDER`, or by an `EYE_OFFSET` with a side or
    ahead part, is there to see him.
- `src/frame_xr/kiu_hooks.cpp:482, 678-693`: the hide block uses
  `GroupWanted(by_mode, HIDE_PLAYER)`, so the switch keeps working as an on/off with no UI:
  - no line (the default): first person on land hides him;
  - `AZAHARFRAME_KIU_HIDE_PLAYER=0`: never hides him (A/B);
  - `=1`: hides him on land and in the air, as before.

  Cutscenes and pauses (the playing flag down), the air, and leaving first person all put the
  scale back. The log line gains ` (first person, on land)`.
- Diagnostics (read only):
  - `HOOK_TRACE=1` ends with `round shadows N`, the count in the game's list ([0xD49490] + 0x14;
    `RoundShadowCount`, `input/kiu_hooks.h:807`; `kiu_hooks.cpp:748`).
  - `FP_TRACE=1` adds `cam_node_44=`, the scene camera node's ten words from +0x44 as floats
    (`kiu_hooks.cpp:291-301`). This is to find its near plane, a possible fix for the air.
- Tests: `src/tests/frame_xr/kiu_hooks.cpp:1298` ("first person hides the player on land") and
  `:1325` ("the round shadows' count").
- Notes: a new `KIU_HOOKS.md` section "Pit's own model in first person (AzaharFrame kiupit)"
  (`:556`). Updated the switch rows, `kiu_hooks.h`'s and `kiu_first_person.h`'s header comments,
  and `docs/TECHNICAL_NOTES.md`.

## Only syntax-checked

Nothing was built or run, and no unit test was run.

`C:/src/tools/syncheck.py` could not be used. The main build's `compile_commands.json` was
rewritten at 06:01 while this work was going on, and it now lists only 10 cubeb files. So the
same check (MSVC `/Zs`, the main build's own defines, includes and flags, taken from
`build-ON/build.ninja`, with the worktree's `src`) was run by hand on these files, and all passed:

- `src/frame_xr/kiu_hooks.cpp`
- `src/frame_xr/kiu_first_person.cpp`
- `src/tests/frame_xr/kiu_hooks.cpp`
- `src/tests/frame_xr/angels_aim_mode.cpp`

A deliberate error was caught, so the check does work. The Linux (GCC/Clang) and Frame builds
were not compiled.

## Verify (Linux desktop with xrmock, then the Frame)

1. Unit tests: `bin/Release/tests "[kiuhook]"` in the Linux build. The two new cases must pass,
   and so must the existing ones.
2. Use the European game, Angel's Aim, Aiming = Direct Aim. Put `AZAHARFRAME_KIU_HOOK_TRACE=1`
   in `~/.config/AzaharFrame/test-switches.txt` (live).
3. Practice Range or Chapter 1's land section:
   - The log must say `the player's model hidden (local scale 0.001) (first person, on land)`.
     The trace must show `scale 0.0010` and `round shadows 0` in the Practice Range.
   - Look down: no see-through legs or body, no dark disc.
   - Set `AZAHARFRAME_KIU_HIDE_PLAYER=0`: he is back (`shown again`), and the trace shows scale 1.
     Remove the line: he is hidden again.
4. Land gameplay with him hidden (the open risk). In Chapter 1's land section, compare against
   `HIDE_PLAYER=0`:
   - damage taken from the same enemies;
   - whether close (melee) attacks and dash attacks still hit;
   - that he does not fall through floors;
   - that pickups are collected;
   - that the land camera still follows.
5. Pause, a cutscene, death and the end of the level: he shows again during each (the scale goes
   back to 1). Load a save state made while hidden: nothing is left at 0.001 outside first
   person.
6. Air battle (not fixed): the see-through head is expected to remain.
   - To test a way to fix it, set `AZAHARFRAME_KIU_EYE_OFFSET=0,3,2` (then `0,3,4`) during the
     air battle only. In first person a side or ahead part is laid along the camera's level
     view, +z ahead. Note whether the head goes, and whether the shots still leave the hand.
     Remove the line before the land section, because it also replaces the land eye.
   - With `AZAHARFRAME_KIU_FP_TRACE=1`, send `cam_node_44=` from an air battle and from land, to
     look for the near plane.
7. Frame: repeat 3, 4 and 6 on the headset. Check the weapon in the hand and the laser are
   unchanged: they are the app's own models, not the game's.

## Proposed strings

No UI text was added or changed. Log lines only:

| Original | Proposed |
|---|---|
| `xr: Kid Icarus hooks: the player's model hidden (local scale 0.001)` | the same, plus ` (first person, on land)` when first person hid him |
| `xr: Kid Icarus hooks: t … [groups] player …` | the same, ending `; round shadows N` (or `-`) |
| FP trace `… fov=… aspect=…` | adds ` cam_node_44=<ten floats>` |

## What's left

- **Land gameplay with the scale write.** Hurt boxes, melee and dash hits, floor collision and
  pickups were never checked (step 4). If any of them is wrong, `HIDE_PLAYER=0` takes the hide
  out. The proper fix would then be to skip his model's draw in the game's code; that code was
  never looked for.
- **The air.** Not fixed. The candidates are an eye ahead of his head (step 6), a near plane
  moved out (step 6's trace), or skipping his model's draw.
- **The menu-toggle work on the other branch.** `AZAHARFRAME_KIU_HIDE_PLAYER` now has three
  states: no line means first person's choice, `0` means off, `1` means always. A menu toggle
  for it should write `0` or remove the line, not `1`, unless it means "hide in the air too".
- **The user guide.** It does not mention this yet. Wait for the headset check first.
- **Aiming Off or Gyro in Angel's Aim.** Pit stays drawn (faded), by design, until the game's own
  shot start with him shrunk has been looked at.
