# pokedaisy_cmake

Builds PokeDaisy's shared C code as the static library `pokedaisy` (EXCLUDE_FROM_ALL, nothing links it yet).
Only configured when `ENABLE_ALT_CORES` is on and `externals/pokedaisy/` holds PokeDaisy's sources; otherwise
it prints a status line and is skipped. PokeDaisy's own files are not modified.

Pinned commit: `60b0c887dba2dc97890aa9fc5ea39db17e7cc7e0` (lidor30/pokedaisy). Its mGBA pin is stock 0.10.5.

## What goes in

- `3ds/core/*.c` except `pd_library*` and `pd_save*` (the host supplies library and save files).
- `3ds/host`: `pd_session`, `pd_items`, `pd_rom_options`. `pd_mixer.c` is left out (see stubs).
- `linux/`: `fly flash flight field_items tm_teach wild hud quick_menu gb_field polished_field`.
- `app/src/main/cpp/`: `pk_call pk_flight pk_host pk_gb_field pk_cheats pk_wide pk_wide_gb`.

## Why each shim, define and stub

- `stubs/pokedaisy_shim.h`, force-included: our mGBA renamed `color_t`, `desiredVideoDimensions` and the
  `BASE_*`/`SIZE_*`/`SAVEDATA_*`/`IDLE_LOOP_*` names since 0.10.5. The shim maps the old names to ours.
- `stubs/pokedaisy_stubs.c`:
  - armGBA and gpSP are 3DS-only back ends; their probes return false so they never claim a core.
  - `pd_mixer_attach/frame/release` do nothing. The real mixer plants a breakpoint over the game's mixer in
    work RAM, which our save states and mapcap would copy.
- `PK_WIDE_CACHE`: without it `pd_session.c` misses seven `pk_wide_*` declarations.
- `PK_WIDE_NO_GUARD`: `pk_wide_view` would otherwise install process-wide SIGSEGV/BUS/FPE/ILL handlers over our
  crash reporter and the JITs.
- `PK_WIDE_MAX_VIEW_W/H=400/240`: EXPAND's buffers, about 10 MB of zeroed data instead of upstream's 800x800.
- `ui_notice=pd_ui_notice`: gba-ds's `ui.h` also exports `ui_notice`.
- mGBA defines (`USE_PTHREADS`, `M_CORE_GBA`, ...) come from linking the `mgba` target, so struct layouts match.
- Hidden visibility: 170 names have no `pd_`/`pk_` prefix.

PokeDaisy must not run beside gba-ds's sprite hiding on one core: both re-hook `drawScanline` and call each
other without end.

## Licence

PokeDaisy is GPL-3.0 (`LICENSE` in its tree) with an additional section 7(b) term. The term is in its
`NOTICE` file, not in `LICENSE`: anyone conveying the program or a work based on it must keep the attribution
"Based on PokeDaisy by Lidor Itzhari - https://github.com/lidor30/pokedaisy" (1) in that `NOTICE`, kept with
the source, (2) in the work's README or equivalent, and (3) in credits or About screens. `NOTICE` also lists
PokeDaisy's own third-party components. Nothing here is copied into this tree.
