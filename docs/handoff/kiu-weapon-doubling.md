# Kid Icarus: Uprising: the doubled weapon with Weapon Scale (kiuweapon)

Branch `work/kiu-weapon` (from `ci/pcvr` 5aefa5f5c). The report, in the owner's words: "fix the
weapon that's like in front of the other one when you do the weapon scale". Angel's Aim, weapon
in hand, Weapon Scale (`vr_light_gun_weapon_scale`, 50 to 150 %).

## Result in one paragraph

No code path in this app draws the weapon twice, at any Weapon Scale or Weapon Size. One model,
one fit, one hand, scaled about Pit's fist (evidence below). So no fix was committed: what was
committed is an A/B switch, a log line and regression tests, so that one run on the Frame or the
Linux desktop can name the second weapon. The likeliest one is **not ours**. It is the game's own
picture: either **the game's rapid-fire bullet** (the First Blade's bullet is a silver and blue
arrowhead that looks like a blade tip), which in first person starts at our muzzle and so sits on
the tip of the weapon in hand, at the game world's life size (about 0.13 m per game unit) while
the weapon in hand is at 0.075 m per unit times Weapon Scale, or **Pit's own weapon**, which the
game draws faded near its camera and first person does not hide in the air.

## Evidence

### This app draws one weapon

- Weapon Scale only reaches the fit: `controller_models.cpp:2690` (`tune.scale *= size_factor`)
  and `:2708` (`weapon_fits = FitWeaponToHand(...)`). Nothing else reads it to draw (grep of
  `vr_light_gun_weapon_scale`: settings, menus, presets, per-game keys, the report, this fit).
- The fit gives one part for every held class: `weapon_fit.cpp:492-493` (the game's hold: one
  `push_back`, side C, or R for a file with halves only), `:513` (the class table's held rule,
  the same). Two parts only for the orbitars' halves.
- `RecordWeapon` (`controller_models.cpp:2269`) draws, for each fit, only the components named
  as the fit's side (`:2351`), and never the flash, bullet or arm parts. The manifest of the
  owner's blade (weapon 0, made on the PC, not committed) has one part, `w000.model`, material
  `Wpn`, side C, 152 triangles; the log's "10 parts, 300 triangles" is that part plus 4 flash and
  5 bullet files, which `RecordWeapon` skips.
- Only the weapon hand carries the weapon: `visuals.cpp:1100`. One `ControllerModels` instance
  (`visuals.cpp:210`).
- The PC logs of 2026-11-22 (`AzaharVR/log`, 03:2x to 05:22) show the blade at "Weapon Size Game
  Size, 100 %: x1.00" every time; Pit's arm switches never on (no `xr: Pit's arm:` line).
- In first person the app's own projectiles are not drawn ("xr: projectiles: the game's own
  shots leave the weapon (first person): none drawn", `controls.cpp:2322`): the arrowhead in the
  owner's pictures is the game's.

### Candidates the brief listed

| Candidate | Verdict |
|---|---|
| The scaled model drawn alongside an unscaled copy | No: one fit, one draw per eye (above). Tested at every scale (below). |
| The game's own weapon still drawn while the hand model shows | Possible. `KIU_HOOKS.md:597`, `:436` and `:205`: the game draws Pit faded when its camera is near him, first person does not hide him, `AZAHARFRAME_KIU_HIDE_PLAYER=1` hides "the model and weapon" on land and is unusable in the air. Not seen in the owner's pictures. |
| A second model left over after a scale change rebuilds it | No: a scale change rebuilds nothing. It only changes the fit; the GPU model is swapped only when the weapon id changes (`controller_models.cpp:3422`), and the old one goes to `weapon_retired`, which is never drawn. |
| A texture-pack variant made twice | No: a weapon's parts are one per material, the pack's PNG replacing the game's texture in the same part (`frame_weapon_models.h` `Part::replaced`). Side note: turning the pack on mid-run does not reload a weapon already live (same `:3422` early return), so its old textures stay until the weapon changes. Not a doubling. |

### The owner's pictures (local only, not copied)

- `s3/20261122051000_1.jpg` (PC, paused): one blade, looked at along its length from the
  pommel; the laser starts at its far tip. No second copy found.
- `s2/20261122045158_1.jpg`: a silver and blue arrowhead sits on the tip of the weapon in hand.
  `s2/20261122045200_1.jpg`: the same arrowhead 20 to 30 cm out along the line to the reticle,
  where a hit is bursting. `20261122033222_1.jpg`: the same arrowhead in flight, no weapon in
  view. Its thin gold edges are drawn in dashes (the game's picture, no MSAA), unlike the weapon
  in hand (4x MSAA). That is the game's rapid-fire bullet: in first person with Direct Aim the
  game's shots start at the hand's ray's origin, our muzzle (`kiu_first_person.cpp:175`,
  `kiu_hooks.cpp` `ShotFromHand`), so each new bullet is drawn on the weapon's tip, at the game's
  scale.

Why it would look tied to Weapon Scale: the bullet's size never changes (life-size world), and
its start follows the muzzle, which Weapon Scale moves. At lower Weapon Scale the weapon shrinks
toward the fist and the bullet on its tip looks like a second, bigger blade in front of it; at
about 170 % the two match in size.

## What changed (commits on work/kiu-weapon)

1. `AZAHARFRAME_KIU_WEAPON_DRAW` (live, like every `AZAHARFRAME_KIU_` switch):
   `weapon_fit.h` `kWeaponDrawSwitch`, `WeaponModelDrawn`; `controller_models.cpp:3066`, `:3187`.
   `=0` leaves the weapon's model out of the image (no controller in its place). The fit, muzzle,
   Laser Sight, muzzle flash and the game's shots from the hand go on as before. No line, or any
   other value: drawn, as before. Default behaviour is unchanged.
2. A log line, once each time it changes (`controller_models.cpp:2832`):
   `xr: weapon drawn: 0 in 1 hand, 1 fit (C with 1 parts and 0 glows)`, with
   `; the model left out (AZAHARFRAME_KIU_WEAPON_DRAW=0)` when the switch is on. More than one
   hand, or more fits than sides, would be this app drawing the weapon twice.
3. Tests (`src/tests/frame_xr/aim_along_weapon.cpp`, tag `[kiuweapon]`): at Weapon Scale 50 to
   150 % in Game Size and Hand Size, every class's fit has as many parts as at 100 % on the same
   sides, Pit's fist stays where it is (on the handle), the weapon's axis keeps its direction,
   and the muzzle moves out from the fist by the factor; and the switch's values (only `0` hides,
   and it is live).
4. `docs/TECHNICAL_NOTES.md`: the switch's row.

## Only syntax-checked

`weapon_fit.cpp`, `controller_models.cpp` and `aim_along_weapon.cpp` passed
`syncheck.py` (MSVC `/Zs`). Nothing was built, linked or run; the new tests have not run.
Run at integration: `tests "[kiuweapon]"`, and the existing `[kiuinteg]`, `[fixc]`, `[kiufix]`,
`[kiuguns]` tags (nothing they test changed).

## Steps for the owner (Frame or Linux desktop)

The switches go in `~/.config/AzaharFrame/test-switches.txt`, one `NAME=value` a line. The file
is read again every second; the log says `xr: test switch NAME=value (live)` and a toast shows.

1. Kid Icarus: Uprising (European), Angel's Aim, Direct Aim, Weapon in Hand on, the First Blade.
   Set Weapon Scale where the doubling showed (try 50 % and 150 %). Start a battle and fire rapid
   shots, then stop firing and hold still.
2. In the log, find `xr: weapon drawn:`. Expected: `in 1 hand, 1 fit (C with ...)`. If it says
   2 hands or more fits, the doubling is ours: send the log.
3. Add `AZAHARFRAME_KIU_WEAPON_DRAW=0`. The weapon in hand goes; the laser stays.
   - The "front" weapon goes too: it was ours. Send the log and a picture.
   - It stays: it is in the game's picture. Go on.
4. With the switch still at 0, stop firing.
   - It goes away within a moment, and comes back on each shot at the tip: the game's bullet.
   - It stays while not firing: Pit's own weapon. On land, `AZAHARFRAME_KIU_HIDE_PLAYER=1`
     should take it away (see "What's left" for the branch where that is now the default).
5. Remove the switch line (or set it to 1) and the weapon is back.
6. Desktop without a headset: the same with the xrmock runtime; take the picture with the mock's
   own capture.

## Proposed strings

None. No user-facing text was added (the switch and the log line are for testing only).

## What's left

- The real fix depends on step 4:
  - **The game's bullet**: the shots could start a little ahead of the muzzle (half a bullet's
    length along the ray, in `HandRay` or `ShotFromHand`) so a new bullet never sits on the
    weapon; or accepted as how the game draws its shots. Needs the owner's choice and a test of
    hits (the start moves).
  - **Pit's weapon**: hiding the weapon alone in the air needs new research of the game's memory
    (the hide-player scale write is unusable in the air: `KIU_HOOKS.md`). Note: on
    `work/kiu-shadow`, `AZAHARFRAME_KIU_HIDE_PLAYER` has three states (no line: Pit hidden on land
    in first person, `=0` shown, `=1` hidden everywhere). With that branch merged, step 4's land
    check is the default; in the air, `=1`.
- A texture pack turned on during a run does not reload the weapon already in hand (see the
  table). Separate from this report.
- The screenshots and the manifest numbers quoted were looked at locally only; none are
  committed.
