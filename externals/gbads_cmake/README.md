# gbads_cmake

Builds mGBA (`mgba`), gba-ds's modules without SDL (`gbads_core`) and gba-ds's `game.c` (`gbads_game`, Linux
only). Nothing in `externals/gba-ds` or `externals/mgba` is changed.

## gbads_game

`game/gbads_game.c` includes `game.c` whole and runs its `game_run()` on a thread the host gives it
(`src/frame_cores/gbads_driver.cpp`), a frame at a time. `game/gbads_game.h` is the interface.

- `game/sdl/SDL.h`: the SDL names used, all `static`; no `SDL_` symbol leaves the object. Textures are plain
  pictures, the clock moves only when the loop sleeps, and there is no sound device.
- `game/gbads_rename.h`: names of gba-ds's that would be global here get a `gds_` in front.
- `game/gbads_ui_stubs.h`: `ui.c`, `paths.c`, `cheats.c` and the rest of the app.

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
layouts, and logos (no logo picture: title screens have no second screen, and plain grey stands where a logo
would). The sound is taken before the loop sees it.

### When gba-ds is updated

- A name missing at compile or link time is a new function of the app: add it to `gbads_ui_stubs.h` or `SDL.h`,
  and to `gbads_rename.h` if it is global. To list them: compile `game.c` alone with our `SDL.h`, then
  `nm -u` it against `libgbads_core.a` and `libmgba.a`.
- `gbads_game.c` goes by the names of `run_frame` and `snapshot_hiding_sprites_poke`, and by statics it sets or
  frees (`gds_fresh`, `gds_put_back`): a rename does not compile. A new static that outlives a game does: look
  at `game_run_inner`'s start.
- `gds_state_loaded` copies the end of `state_load`: compare it.
- `game_run_inner` must still call `ui_events`, `run_frame` and `ui_present` once a pass. If it does not, the
  driver drops to the game alone with a line in the log.
- Search for new calls that write files, start threads or run frames.
