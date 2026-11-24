# gbads_cmake

Builds mGBA (`mgba`), gba-ds's modules without SDL (`gbads_core`) and gba-ds's `game.c` and `logo.c`
(`gbads_game`, Linux only). Nothing in `externals/gba-ds` or `externals/mgba` is changed.

## gbads_game

`game/gbads_game.c` includes `game.c` whole and runs its `game_run()` on a thread the host gives it
(`src/frame_cores/gbads_driver.cpp`), a frame at a time. `game/gbads_game.h` is the interface.

- `game/sdl/SDL.h`: the SDL names used, all `static`; no `SDL_` symbol leaves the object. Textures are plain
  pictures, the clock moves only when the loop sleeps, and there is no sound device.
- `game/gbads_rename.h`: names of gba-ds's that would be global here get a `gds_` in front.
- `game/gbads_ui_stubs.h`: `ui.c`, `paths.c`, `cheats.c` and the rest of the app.
- `game/gbads_logo.c`: `logo.c`, whole, with a start and an end for each game. See Logos.

### What is redirected, and why

| In game.c | Here |
|---|---|
| `static Emu emu` | a pointer to the host's `Emu` (a macro): one core, no copy |
| `emu_open`, `emu_close`, `emu_reset` of it | nothing: the host's |
| `emu_frame` of it | from `run_frame` the host's step; from anywhere else (drawn again without sprites) the core itself |
| `emu_write*` to it | dropped, but in a frame that is put back |
| `profile_patch_rom` | never |
| `emu_patch_plan` | nothing: an empty plan would stay and keep a patch beside the ROM from every later open |
| `profile_set_rom` | the host's, under its lock |
| frame-ahead and keep-sound threads | refused: they run the game out of turn |
| `fopen`, `getenv`, `nanosleep`, `fprintf` | no files, no environment, the clock, the log |

### What is dropped

Its menus, quick bar, hotkeys, on-screen messages, overlays, bezels, shaders, colour correction, screenshots,
bug reports, achievements, rewind, fast-forward, patches, cheats, saves, states, sun level, four-face and SNES
layouts, the player's own and scraped logos, and the game's name where there is no logo. The sound is taken
before the loop sees it.

### Logos

A title screen's logo is made from the player's own ROM: a second, silent core runs it to its title screen. No
picture is shipped. Real: all of `logo.c`, `ui_load_logo`, `ui_logo_surface`, `ui_logo_note`, `logo_generation`.
As `ui.c` with no custom logos: `ui_logo_provisional` and `ui_logo_custom` (always false). Still stubs:
`ui_title_block`, `ui_resample`, `ui_load_surface`, `ui_load_image`.

- **Where:** `GbadsHost.logo_dir`, the game's folder's `logo/`, under `ui.c`'s names (`CODE.png`, `CODE-rN.png`,
  `CODE-CRC.png`). NULL: no logos at all. Nothing is written anywhere else, and no picture is ever deleted.
- **When:** asked for by `ui_load_logo` at the game's start, if there is no file: urgent, on `logo.c`'s queue
  (paused for the length of the game, as in gba-ds), at nice 19. The loop shows it once it is there. gba-ds's
  game list does this before the game starts; here it is beside it.
- **Failed:** not tried again until the program starts again (`logo.c`'s `tried`). Cut short by the game's end is
  not failed. Without a logo a title screen has no second screen, and plain grey stands where a logo would.
- **Files:** `logo.c`'s `rename` first reads the new file back and syncs it (stb ignores a full disk). Read with
  gba-ds's own `stb_image.h`, all `static`, PNG only, no larger than the GBA's screen; no `stbi_` name leaves
  the object.
- **End:** `gds_put_back` joins the queue's thread before anything else.

The thread runs beside the loop. What it shares with it:

| Static | The logo's thread | |
|---|---|---|
| emu.c `hideSlots` | takes, sets and frees a slot under `hideLock`; looks for its own in each line drawn, without | a slot is found by its renderer, which is never another core's |
| `split.installed`, `.main` | reads them without a lock (`emu_state_load`, `emu_close`) | only compared with its own renderer; never equal |
| `rewindOwner` | compares it | no rewind here |
| `plan`, `patched`; `biosData` | under `patchedLock`; `biosLock` | |
| `emu_bios_path`, `emu_solar_level` | reads them | the first is set with no game open; the second is an int |
| the rest (`bandOn`, `groupHide`, `copyAbove`...) | not reached | behind the split's hooks or `grouped` |
| mGBA's default logger, lzma's CRC table | written with what is there already | |
| profiles.c `romSwap`, `swapped` | reads them without `LockProfiles` | `profile_set_rom` runs before it starts and after it is joined |

The map capture's thread already meets `hideSlots` and `split` the same way.

### When gba-ds is updated

- A name missing at compile or link time is a new function of the app: add it to `gbads_ui_stubs.h` or `SDL.h`,
  and to `gbads_rename.h` if it is global. To list them: compile `game.c` alone with our `SDL.h`, then
  `nm -u` it against `libgbads_core.a` and `libmgba.a`.
- `gbads_game.c` goes by the names of `run_frame` and `snapshot_hiding_sprites_poke`, and by statics it sets or
  frees (`gds_fresh`, `gds_put_back`): a rename does not compile. A new static that outlives a game does: look
  at `game_run_inner`'s start.
- `gds_logo_begin` sets back what `logo_shutdown` leaves: look at both, and at what else `logo.c` keeps.
  `gds_logo_path` copies `ui.c`'s `extracted_path_for`. New `emu.c` statics: the table above.
- `gds_state_loaded` copies the end of `state_load`: compare it.
- `game_run_inner` must still call `ui_events`, `run_frame` and `ui_present` once a pass. If it does not, the
  driver drops to the game alone with a line in the log.
- Search for new calls that write files, start threads or run frames.
