# Azahar VR: The PC VR Plan

Azahar VR is the PC VR version of Azahar for Frame: Linux x86-64 and Windows 10/11, SteamVR, with Quest 3, Index and Vive XR Elite controllers. This document is its plan: what has to change, in what order, and what is built.

- **Based on:** a feasibility study written 2026-11-17 against Azahar for Frame **v1.2.1** (`c1ae25643`). File and line references in sections 2 to 6 are to that tag unless a section says otherwise.
- **Evidence labels:** **V** = verified in code, logs, docs or a page that was read; **I** = inferred.
- **Status:** work package 1 is under way on `lane/pcvr` (see Build Status).

## Decisions

**2026-11-22 (these replace decision 1 below)**

1. **One repository.** Azahar VR is built from this repository, beside Azahar for Frame. There is no separate fork; the private fork's plan moved here. The repository keeps its name until the PC build ships.
2. **Two products from one codebase,** picked at build time:
   - "Azahar for Frame": the Steam Frame, Linux aarch64;
   - "Azahar VR": PC, Linux x86-64 and Windows (`AZAHARVR_PC`, on for anything but aarch64 Linux).
3. **The Frame must not regress.** PC code is gated at build time (`AZAHARVR_PC`) or at run time (the interaction profile). Nothing Frame-only is deleted. The Frame's tests and its mock profile pass on every change.
4. **Data folder:** `azahar-vr` (Linux) and `AzaharVR` (Windows), and the test-switch folder `AzaharVR`. The first release copies nothing from other installs.
5. **Controls:** the section 3.3 scheme, as a draft, built as data tables so it is cheap to change.
6. **CI:** Linux x86-64 and Windows jobs beside the arm64 one. Each push needs the owner's OK (public repository).
7. **Windows and Linux together.** Windows is built in CI from the start. The owner runs the Windows tests, with SteamVR, from a CI zip and a short checklist.

**2026-11-17 (still in force, except 1)**

1. ~~A separate fork.~~ Replaced by the one-repository decision above.
2. **Black Frame Insertion only works at 120 Hz.** A guard enforces it (`src/frame_xr/motion_clarity.h`, since Azahar for Frame 1.2.1-143). On PC it is not offered, except as an experiment on wired 120 Hz headsets (section 3.2).
3. **Settings move to the menu.** Many things the Frame build does with controller shortcuts move into the VR menu (section 3.4).
4. **Controls:** one default scheme per controller family (section 3.3).

## Build Status (2026-11-22, `lane/pcvr` from `lane/integ17` a95068a9e)

| Step | State |
|---|---|
| 1. PC build | `pc/build-linux.sh` (native x86-64, `ENABLE_FRAME_XR=ON`, Sentry off). `ENABLE_FRAME_XR` now defaults on for Linux x86-64 too. A native build links. The native unit suites pass except three of this repository's own tests, which fail the same way on the unchanged tree and are open for x86-64: `tests/frame_xr/touchpad_touch.cpp` ("the dot sits on the screen"), `tests/core/mods_mega.cpp` (key, IV and CTR vectors) and `tests/core/savestate_memory.cpp` (a header check). |
| 2. PC identity | `AZAHARVR_PC` (`CMakeLists.txt`): the define is added only when it is on, so the Frame's compile lines are unchanged. `common/frame_product.h` holds the name ("Azahar VR") and the switch folder; `common_paths.h` the data folder; Azahar's legacy-data migration prompt is skipped on PC. |
| 3. Mock profiles | `MOCK_XR_PROFILE` in `tools/xrmock/xrmock.c` (`oculus/touch_controller`, `valve/index_controller`, `htc/vive_focus3_controller`, `valve/frame_controller`); the mock offers `XR_HTC_vive_focus3_controller_interaction`. `xrmock_test` checks the reported profile. |
| 4. Controls | Built (section 3.3, "As Built"). Unit tests per scheme (`src/tests/frame_xr/pc_scheme.cpp`), including one that the Frame's scheme changes nothing the mapper does. Mock runs per profile, with the game's held buttons read through the RPC server. |
| 5. Menu moves | **Not built yet.** On PC the grips are L and R in a game and grab nothing; grabbing with the menu open, a Fast Forward row and Left Stick rows are still to do (section 3.4). |
| 6. Platform shim | `src/frame_xr/platform.h` (library loading for the OpenXR and Vulkan loaders); `%APPDATA%` test switches; Windows stubs for the watchdog and the dock icon; Linux-only includes guarded in `frame_weapon_models.cpp` and `custom_tex/convert.cpp`. The crash handler, evdev and the memory guard were already guarded or portable. Guards are in the files, not in the CMake lists: the sources are referenced from tests and other targets, and a list change could not be checked without a Windows compiler. **Not compiled on Windows yet.** Still with unguarded POSIX includes (a sweep of this repository's sources): `core/steam_cef.cpp`, `core/homebrew/emerald3ds_fallback.cpp`, `frame_xr/app.cpp`; `core/mods/multipart.cpp` is partly guarded. |
| 7. CI | `.github/workflows/pcvr.yml`: `linux-x86_64` (upstream's build container) and `windows-msvc` (frontend off, then on with `openxr_loader.dll` in a zip). Runs on `ci/**` pushes and by hand. Not pushed. |
| 8. Black Frame Insertion on PC | **Not built.** The runtime and system names are already logged (`xr: runtime …`, `xr: system …`); the Motion Clarity row still shows on PC. |
| Windows | **Builds and runs** (2026-11-22, `ci/pcvr` 2beadae9d; MSVC 19.44, Windows SDK 10.0.26100, Qt 6.10.3, Vulkan SDK 1.4.304.1). Played on Windows 11 with SteamVR/OpenXR 2.19.4 and a Steam Frame (the Frame scheme, controller models from SteamVR's files), RTX 3080. Flat and VR switching, the window panel (Qt's own file pickers in VR), Mods and texture pack installs (`tar.exe`, 7-Zip; folder links need Developer Mode) and HTTPS (Mozilla's roots plus Windows' store) work. `tests.exe`: 1229 cases, 4 failing: the three above, and `test_switches.cpp:245` (a deleted file still seen for a moment on Windows). Optimize for Frame is hidden on PC (desktop GPUs read no ASTC). |

**Checked on the desktop (2026-11-22):**

- the Frame's arm64 build: every unit suite passes (`tests` 1236 cases, `frame_qt_tests`, `frame_xr_picker_tests`, the test-switch suite); its menu dump is identical to integ17's (378 files); the arm64 binary has none of the PC names;
- native x86-64: the unit suites pass, apart from three upstream-core failures that fail the same way on the unchanged tree;
- the mock (Kid Icarus: Uprising from a save state, `MOCK_XR_PROFILE`): every scripted button of the Frame, Touch, Index and Focus 3 profiles reached the game as expected (the game's held buttons read through the RPC server), and the PC build writes only `azahar-vr` and `AzaharVR`;
- a Touch mock run of the chords through the whole app: Menu + right grip saved, Menu + right trigger switched Fast Forward on and off, Menu + left grip held loaded, and Menu held 1 s opened the VR menu.

**Run-time detection.** `XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED` makes the controls look the profile up (`xrGetCurrentInteractionProfile`) and pick the scheme. The Frame's build always keeps the Frame's scheme.

## Verdict

**Feasible, and cheaper than it looks.** The XR layer is already close to runtime-agnostic, because it was built against SteamVR's own OpenXR runtime: the Frame runs SteamVR/OpenXR 2.19.x (V, Frame logs).

- **Linux x86_64 build status:** the tree builds, links and runs against the mock runtime on x86-64 Linux with `ENABLE_FRAME_XR=ON` (V, 2026-11-22; see Build Status).
- **What is Frame-only** is mostly at the edges:
  - the OpenXR loader path and the `vrcmd` paths;
  - the Steam and SteamVR shell integration (library entry, dock icon, theatre);
  - the cgroup memory guard;
  - evdev input;
  - the Frame controller profile, models and help art.
- **The two real problems are design, not porting:**
  1. **Controls.** The Frame build's scheme leans on the Frame's D-pad, bumpers and View button. Quest Touch and Index have none of them. §3.3 proposes the answer.
  2. **Black Frame Insertion** (Motion Clarity in the code). At v1.2.1 it is not limited to 120 Hz, and it is unlikely to survive a video stream. §3.2 sets the policy.

**Route: Linux x86_64 + SteamVR first, Windows second.**

- Linux first because it is the same OS and runtime family as the Frame, the build already configures, and a native build is a fast test rig.
- Windows second because that is where most PC VR users are, and where Virtual Desktop and VIVE Streaming run.

**Rough effort:**

| Scope | Effort |
|---|---|
| Linux MVP: P0–P3 plus the Linux part of P6. Quest 3 and Index playable, beams-only controllers | **about 3.5–4.5 weeks** |
| Plus P4: controller models, help art and Kid Icarus weapon re-fits | **+1.5–2 weeks** |
| Plus P5: the Windows port, with its CI and packaging | **+3–4 weeks** |
| **Total, both platforms, release quality** | **about 8–10.5 weeks** of one developer, plus a handful of short headset sessions |
| Upkeep: merging each Azahar for Frame release | about half a day per release |

---

## 1. One Repository, Two Products

- **Branches:** PC work goes on lanes like any other (`lane/pcvr`) and merges into the integration branch with the Frame's work. Azahar and CitraVR arrive as they do today.
- **Rules that keep the Frame safe:**
  - **Add, do not rewrite.** PC code goes in new files where it can (`pc_scheme.*`, `platform.h`, `pc/`). Shared files get small changes marked `// AzaharVR`.
  - **Gate, never delete.** Build time: `AZAHARVR_PC` (and `_WIN32` for Windows-only code). Run time: the interaction profile (the controls), the display period (pacing), the offered extensions.
  - **Keep the internal names.** `frame_xr`, `ENABLE_FRAME_XR`, the `AZAHARFRAME_*` switches and the `// AzaharFrame` markers stay. Only what the player sees says "Azahar VR".
  - **Before any merge:** the Frame's arm64 build, its unit suites and the menu dump against the integration branch's.
- **Versions:** Azahar VR will have its own version numbers, starting at 0.1.0.
- **Player docs** (`README.md`, `docs/USER_GUIDE.md`, `CONTROLS.md`) stay the Frame's until the PC build is real.
- **A full checkout** needs the submodules (`git submodule update --init --recursive`).

---

## 2. What Is Frame-Only

Azahar for Frame's own code is about **308 files and 119k lines** (`src/frame_xr`, `citra_qt/frame_*`, `core/frame_*`, `common/frame_*`, `common/xr_*`, `core/custom_tex`, `input_common/frame_xr*`, `vk_xr*`). `src/frame_xr` alone is about 84k lines (V). Most of it is platform-neutral C++ over OpenXR, Vulkan and Qt.

| Area | Where (v1.2.1) | Size | PC impact |
|---|---|---|---|
| **arm64 assumptions** | `CMakeLists.txt:171`: `ENABLE_FRAME_XR` defaults ON only for Linux aarch64, but the `CMAKE_DEPENDENT_OPTION` at `:174` lets you force it ON elsewhere (V). The arm64 code paths in `common/`, `core/arm/` and `math_util` are upstream and guarded, with x86_64 twins (V). | 1 line | Make the default ON for Linux x86_64 too (and later Windows). No code is arm-locked (V, grep). |
| **OpenXR loader path** | `frame_xr/xr_loader.cpp:19`: `/opt/steamvr/bin/linuxarm64/libopenxr_loader.so`, then `libopenxr_loader.so.1`/`.so` via `dlopen` (`:30-41`) (V) | 109 lines | Linux PC: the `libopenxr_loader.so.1` fallback should pick up a system loader, but this is unverified (I). Ship the loader in the tarball. Windows: `LoadLibrary("openxr_loader.dll")` and ship Khronos's loader. |
| **Vulkan loader** | `frame_xr/vulkan_bridge.cpp:88-96`: `dlopen("libvulkan.so.1")` (V) | 244 lines | Windows: `vulkan-1.dll`. |
| **Frame interaction profile** | `input/actions.h:23` `XR_VALVE_frame_controller_interaction`; `actions.cpp:233` `/interaction_profiles/valve/frame_controller`, with ABXY on the right hand, D-pad and View on the left, bumpers, Menu. Fallback `actions.cpp:262` `oculus/touch_controller` ("for development": no D-pad, View or bumpers, so no L or R at all) (V) | actions 424 + mapper 655 (80 refs to View, bumper and D-pad) + default_bindings 75 | **Main design work**, see §3.3. No Index, Vive or Focus 3 profiles exist. |
| **Controller models** | `core/frame_controller_models.h:27`: converts SteamVR's `/opt/steamvr/drivers/frame_controller/resources/rendermodels` into `.model` files in aim space (V). `vis/controller_models.cpp` (3,500 lines) draws them in a projection layer (V). | ~4.1k lines | The converter is generic over SteamVR render models. On PC, point it at `SteamVR/resources/rendermodels` with per-controller names and aim-space offsets (I). The draw pass is generic. |
| **Help and diagram art** | `vis/controller_diagram.cpp` (339), `vis/help_content.cpp` (417, 44 View, bumper and D-pad refs) (V) | ~750 lines | Frame-shaped; needs per-profile text and art. |
| **Kid Icarus weapon in hand** | `vis/weapon_fit.*`, `input/gyro_aim.*`: fits relative to the Frame's aim pose (V/I) | ~2.2k lines | Re-tune per controller family (aim poses differ between controllers, I). |
| **Refresh rate** | The app only *requests* a rate under a test switch (`session.cpp:118-133`, `AZAHARFRAME_XR_REFRESH_RATE`). The real request is `vrcmd --set-settings-int steam.app.<id>.preferredRefreshRate`, plus motion smoothing off, in `steam-frame/azaharframe.sh:288-297`, on a `linuxarm64` path, and only when Steam launched it (V). SteamVR on the Frame lists **one** rate at a time (V, Frame logs). | ~10 lines | **On PC nothing asks for 120 Hz today.** `xrRequestDisplayRefreshRateFB` probably cannot switch rates on SteamVR, which offers only the current one (I). The lever is SteamVR's per-app setting (`vrcmd` from `linux64` or Windows `win64`), or the headset or streamer setting on Quest. Keep the FB call as a best-effort extra. |
| **Pacing gate** | `common/xr_pacer.h:42` `EvenCadencePeriod`: 8.166–8.5 ms only, so 120 Hz ±2 % (V). `frame_xr/pacing.cpp:82,258` (V) | 1 function | At 72, 80, 90 and 144 Hz the gate stays off. That is correct (no even cadence). |
| **Black Frame Insertion** | `frame_xr/motion_clarity.h` (696): driven by `predictedDisplayPeriod` (`app.cpp:813-1088`). The "full rate" rule is the mean refreshes per picture ≤ `kFastEnter` = 2.5 (`:215-221`), so it engages at 72 (1.2), 90 (1.5), 120 (2) and 144 Hz (2.4) alike. Off by default (`settings.h:817,821`) (V) | 696 lines | **Not 120-gated at v1.2.1.** The guard and the PC policy are in §3.2. |
| **Frame paths and install** | `steam-frame/azaharframe.sh`: `/opt/steamvr/steamxr_linuxarm64.json` (`:283-285`), `vrcmd` (`:294`), render models (`:320`) (V). `core/frame_dock_icon.cpp:235`, `core/steam_cef.cpp:535` (which already also tries `linux64/vrcmd`) (V) | sh 429 + dock 339 + cef ~550 | PC: the loader's default `active_runtime.json`. Dock icon, theatre and library entry: off in the first PC release. |
| **Steam and SteamVR shell (Game Mode)** | `citra_qt/frame_mode.cpp` (812): gamescope detection by `XDG_CURRENT_DESKTOP` (`:473`), theatre via `vrcmd --dock-overlay`. `frame_mirror.*`, `frame_flat_dialog.*` gamescope focus rules (V) | ~1.5k lines | Already detected at run time: the non-gamescope branch is what desktop mock runs use (V). PC Flat is an ordinary desktop window. |
| **Steam entries** | `install.sh`, `azahar --frame-steam-shortcut` via Steam's CEF debugging port (V) | ~840 lines | Optional on PC. A separate non-Steam shortcut later, never sharing the Frame entry's app id. |
| **Test-switch file** | `common/frame_test_switches.h:47-53`: `$XDG_CONFIG_HOME` or `$HOME/.config/AzaharFrame/test-switches.txt`; uses `extern char** environ` and `sys/stat.h` (V) | 308 lines | Linux PC: works as is (the folder name follows the data-folder decision). Windows: `%APPDATA%`, `_environ`. |
| **Memory guard** | `azaharframe.sh:340-394` `systemd-run --user --scope` MemoryMax; `frame_xr/mem_watch.h` (256), `mem_guard.h`, `citra_qt/frame_memwarn.cpp` (176) read cgroup files (V) | ~500 lines | Not needed on a 32 GB PC (I). Linux: harmless; off by default. Windows: stub. |
| **FDM layer** | `session.cpp:136-141`: Valve's FDM injection layer, turned off with an environment variable (V) | few lines | Frame-only; harmless elsewhere. |
| **Turnip and Adreno workarounds** | `vk_instance.cpp:524-542` (by driver id; upstream), `:26` allocation timing, `vk_rasterizer.cpp:685` (V) | — | All keyed on driver id, so inactive on NVIDIA and AMD (V). |
| **ASTC texture packs** | `vk_instance.cpp:602` (ASTC on Adreno, BC on desktop); "Optimize Texture Pack for Frame" writes ASTC KTX (V) | `core/custom_tex` ~1.6k | Desktop GPUs have no ASTC LDR (I). Hide the optimizer on PC, or add a BC7 target later. |
| **Linux-only APIs (break Windows)** | `frame_xr`: `xr_loader.cpp`, `vulkan_bridge.cpp` (dlopen); `watchdog.cpp` (`/proc/self/task`, `sys/syscall.h`); `input/devices.cpp` and `input/phys_keyboard.cpp` (evdev, `linux/input.h`); `mem_watch.h`, `mem_guard.h`. `common/frame_crash.cpp` (sigaction, ucontext; `:21` `__linux__` partly). `core/frame_crash_report.cpp` (signal, utsname, unistd), `core/frame_dock_icon.cpp` (fork/wait), `core/frame_weapon_models.cpp` (`sys/resource`, `syscall`), `core/custom_tex/convert.cpp`. `citra_qt/citra_qt.cpp:163-164` (dlfcn for display-server loss), `frame_crash_dialog.cpp`, `frame_mods.cpp` (V, grep for POSIX headers and calls; ~25 files, ~13k lines, nearly all with no `_WIN32` guards) | ~25 files | **The Windows port list.** Note: `core/frame_*` and `common/frame_*` sit in **ungated** source lists (`src/core/CMakeLists.txt:49-54,92-99`, `src/common/CMakeLists.txt:38-86`), so Windows may fail **even with frame_xr off** (I: not compiled on Windows). 2026-11-22: the crash files, evdev and `citra_qt.cpp` were already guarded; the rest is guarded or stubbed now (Build Status, step 6), still uncompiled. |

Not Frame-specific (V): the swapchain format choice (`session.cpp:32-35`, from `xrEnumerateSwapchainFormats`); the layer limit from `maxLayerCount` (`vis/visuals.cpp:423`); environment blend modes chosen from what the runtime offers (`session.cpp:283-286`, falling back to opaque without passthrough). There are no `timespec` or `win32` time-conversion extensions and no `fd`-based external memory. Immersive modes 1–3, mode 3's stereo patch (`rasterizer_accelerated.cpp`), Natural 3D, Dynamic Resolution, the menu panel and the Qt mirror have no platform code beyond what is listed above.

### 2.1 Does It Build for x86_64 Linux or Windows Today?

**Gating (V):**

- `CMakeLists.txt:170-174`: `ENABLE_FRAME_XR` depends on `ENABLE_VULKAN; ENABLE_QT; NOT ANDROID; NOT ENABLE_LIBRETRO`. There is **no `NOT WIN32`**. The default is ON only for Linux aarch64.
- `AZAHARFRAME_SENTRY` (`:178`) depends on it.
- `src/CMakeLists.txt:195-211` adds the define and the subdirectory.
- `src/citra_qt/CMakeLists.txt:307-339` adds the `frame_*` Qt sources.
- `frame_qt_tests` is already `NOT WIN32` (`:373`).

**CI (V):** `.github/workflows/portable.yml` has one job, `linux-arm64` on `ubuntu-24.04-arm`. `.github/workflows/pcvr.yml` (2026-11-22) adds `linux-x86_64` and `windows-msvc`; not run yet.

**Linux x86_64 (V):**

- **Configure:** an export of v1.2.1 with its submodules, then `cmake -G Ninja -DENABLE_FRAME_XR=ON -DENABLE_VULKAN=ON -DENABLE_OPENGL=OFF -DENABLE_QT=ON -DENABLE_TESTS=ON -DAZAHARFRAME_SENTRY=OFF …`. Result: **"Configuring done", architecture x86_64, `ENABLE_FRAME_XR:BOOL=ON`**.
- **Compile:** three translation units compiled cleanly with the host g++ and the generated compile commands: `frame_xr/session.cpp`, `xr_loader.cpp` and `input/actions.cpp`. No link and no full build.

**Windows: not checked.** Expect failures in the ~25 files above (I). Upstream Azahar's own Windows build is the base to reuse (I).

---

## 3. Runtimes, Refresh Rates and Controls

### 3.1 OpenXR Features Used (V, `frame_xr.h:45-48`, `session.cpp:196-205`)

| Feature | Use | SteamVR (Frame) | SteamVR PC (Windows, Linux) | Monado / WiVRn |
|---|---|---|---|---|
| `XR_KHR_vulkan_enable2` | required | V (log) | Y (inventory, I*) | Y (inventory, I*) |
| `XR_FB_display_refresh_rate` | logs rates; request under a test switch | V (log) | Y (inventory, I*) | Y Monado (I*); WiVRn not listed |
| `XR_VALVE_frame_controller_interaction` | Frame profile | V (log) | absent (I: Frame driver only) | absent (I) |
| `XR_KHR_composition_layer_cylinder` | Screen Curve, curved backing | **V offered (log, 2.19.x)**. The inventory summary said N, which contradicts the log | likely Y, same runtime (I); verify from the first PC log | Y Monado (I*) |
| `XR_KHR_composition_layer_color_scale_bias` | Match Bottom Screen Brightness (falls back to a shader) | **not offered** (V, log) | N (inventory, I*) | N (inventory, I*) |
| Layers | QUAD (screens, backing, menus), CYLINDER, PROJECTION (controller models `controller_models.cpp:347`, HUD `hud.cpp:429`); per-eye quads via `eyeVisibility` (`app.cpp`, 7 refs) for mode 3 (V) | max 16 layers (V, log) | max 16 (I) | — |
| Spaces | LOCAL, VIEW (head-locked screens) (V) | | | |
| Blend | ALPHA_BLEND (passthrough) only if offered, else OPAQUE (V) | offered | PC SteamVR: opaque only (I) | — |

I* = from a summary of the Khronos inventory page, not read row by row. It was wrong at least about cylinder on SteamVR, so confirm each from the first PC session's `xr: runtime … extensions:` log line.

The extension surface used is the one SteamVR already offers on the Frame, minus the Frame controller extension (V). The code already treats every extension except `vulkan_enable2` as optional (V).

### 3.2 Black Frame Insertion: 120 Hz Only, and Left to Standalone

**The rule.** Black Frame Insertion only works at 120 Hz. At any other refresh rate it does nothing, and the menu row says why.

**Why a guard is needed.** At v1.2.1 the code engages at other rates too. The thresholds are V (`motion_clarity.h:215-221`, `xr_pacer.h:42`); the visual outcome off 120 Hz is I, worked out from the rules.

| Rate | Pacing gate | Black Frame Insertion at 60 fps, v1.2.1 |
|---|---|---|
| **120 Hz** | on | each picture lit 1 refresh in 2: the intended behaviour |
| **90 Hz** | off | engages (1.5 ≤ 2.5): pictures alternate between 1 and 2 refreshes on view. An uneven lit and black pattern, likely judder or flicker (I) |
| **72 / 80 Hz** | off | engages (1.2 / 1.33): mostly lit, with a black refresh every few pictures, likely flicker at about 12–20 Hz (I) |
| **144 Hz (Index)** | off | engages (2.4): alternates 2 and 3 refreshes per picture, uneven (I) |

**The guard.** Engage only when the display period is in the 120 Hz window the pacing gate already uses (8.166–8.5 ms, `EvenCadencePeriod`). Otherwise hold the screen lit.

- The guard exists (`motion_clarity.h`, the 120 Hz window; V, 2026-11-22). Azahar VR shares it: one codebase.
- Tests: the mock session at 72, 90, 120 and 144 Hz (`MOCK_XR_HZ`), plus the unit tests in `tests/frame_xr/motion_clarity.cpp`.

**The PC policy.**

| Headset link | Black Frame Insertion |
|---|---|
| Streamed (Steam Link, ALVR, Virtual Desktop, VIVE Streaming, WiVRn) | **Not offered.** The row is hidden. |
| Wired, at 120 Hz (for example an Index set to 120 Hz) | Shown under **Experimental**, off by default |
| Wired, at any other rate | Hidden |
| Unknown link | Treated as streamed |

Why streaming is out (all I):

1. The PC composites every layer into the eye images before the streaming driver encodes them.
2. Alternating lit and black frames are a worst case for a video codec: every frame differs from the last, which costs bitrate and shows as artefacts on the screens.
3. When a packet is late, the headset repeats the last frame. A repeated black frame is a dark flicker; a repeated lit frame is a double image and a brightness step.

**Telling wired from streamed (V/I).** `session.cpp:293-297` already logs `xrGetSystemProperties().systemName`. On the Frame that is `SteamVR/OpenXR : cv` (V, log). On PC, SteamVR puts the driver name after the colon (I), which tells `lighthouse` (wired) from the streaming drivers (I). Add `xrGetInstanceProperties().runtimeName` for Monado, WiVRn and VDXR. Only a driver on a short "known wired" list gets the experimental row.

**Asking for a refresh rate.** On SteamVR the lever is the per-app `preferredRefreshRate` (via `vrcmd`, as the Frame launcher does) or the headset's own setting. Steam Link, ALVR and Virtual Desktop pick the rate on the Quest side (I). Azahar VR runs at whatever rate the headset gives it; 72, 90 and 120 Hz must all play well without Black Frame Insertion.

### 3.3 Controller Schemes (a Draft the Owner Accepted, 2026-11-22)

#### What Has to Fit

The 3DS has A, B, X, Y, L, R, ZL, ZR, a D-pad, the Circle Pad, the C-Stick, Start, Select and a touch screen. On top of that come the VR menu and a few shortcuts.

The Frame controllers are a gamepad cut in two, so Azahar for Frame maps almost one to one. Quest Touch has **no D-pad, no bumpers and no View button**: seven inputs fewer. Index has no bumpers, View or Menu, but adds two trackpads.

#### What the Two Existing Schemes Do

| | CitraVR (Quest) | Azahar for Frame |
|---|---|---|
| A, B, X, Y | Touch's own A, B (right hand) and X, Y (left hand) | The Frame's four face buttons, by position |
| L, R | **Grips** | Bumpers |
| ZL, ZR | Triggers, when the pointer is not on a panel | Triggers, except on the Secondary Screen |
| Touch | Trigger on the lower panel | Trigger on the Secondary Screen |
| D-pad | Rest a thumb on one controller's thumbrest: the **other** hand's stick becomes the D-pad | A real D-pad |
| Start, Select | Buttons on the panel under the screens | **Menu** tap, **View** tap |
| Menu | Left Menu button shows or hides the lower panel | Hold **Menu** 1 second |
| Moving the screens | A mode on the lower panel, then trigger and stick | Grips grab the screens |
| Stick clicks | Not used for the game | Swap, hide, layout, reset |
| Shortcuts | None | **View** chords: Quick Save, Quick Load, Fast Forward, VR or Flat |

(CitraVR: `src/android/app/src/main/jni/vr/XrController.cpp` and `vr_main.cpp` in the `citravr` history. Azahar for Frame: `src/frame_xr/input/default_bindings.h`, `actions.cpp`, `mapper.cpp`, `MAPPING.md`.)

#### Design Rules

These come from thinking about it as a player holding the controllers.

1. **L and R must always reach the game.** Nearly every 3DS game uses them. So they go on inputs with no second job: the grips, as CitraVR does. ZL and ZR are used by far fewer games, so they can share the triggers with touch.
2. **The trigger stays the touch.** Pointing and pulling the trigger is what every VR player already does to tap something.
3. **Buttons keep their printed letters.** Touch has A, B, X and Y printed on it, and so does the 3DS. When a game says "Press X", the player presses X.
4. **Copy the 3DS's own thumb.** On a 3DS the left thumb is on the Circle Pad or on the D-pad, never both. A left stick that is one or the other loses nothing the 3DS had.
5. **The right stick stays the C-Stick.** Right stick as camera is what players expect, and the emulator presents a New 3DS.
6. **Nothing that depends on a sensor the stream may not pass on.** CitraVR's thumbrest trick is clever, but Index has no thumbrest, and it is not certain every streaming driver reports it.
7. **Only what you need mid-game stays on the controllers.** Everything else moves to the menu (§3.4). That frees the grips and the stick clicks.

#### Quest Touch (`oculus/touch_controller`)

SteamVR is expected to present the Quest 2, Quest 3 and Quest Pro controllers through this one profile (I).

| Touch control | 3DS | Notes |
|---|---|---|
| **A**, **B** (right) | A, B | By label |
| **X**, **Y** (left) | X, Y | By label |
| Left grip, right grip | L, R | Pressed above 0.60, released below 0.40, as today's grip thresholds |
| Left trigger, right trigger | ZL, ZR | Aimed at the Secondary Screen it is a touch instead, as in Azahar for Frame |
| Left stick | Circle Pad | |
| **Left stick click** | Switches the left stick between **Circle Pad** and **D-pad** | A label shows the new mode. Each game remembers its mode. In D-pad mode the stick is four-way |
| Right stick | C-Stick | |
| Right stick click | Select | |
| **Menu** (left), tap | Start | Sent when you let go, as on the Frame |
| **Menu**, held 1 second | The VR menu | The game never sees that hold |
| Hold **Menu**, then right grip | Quick Save | When you let go of the grip |
| Hold **Menu**, then hold left grip for half a second | Quick Load | |
| Hold **Menu**, then right trigger | Fast Forward on or off | |

**Menu** takes the job **View** has on the Frame, so the mapper's chord logic carries over. Shortcuts never reach the game. A stick click made while the stick is pushed past half way is still ignored, so play cannot switch modes or press Select by accident.

**If SteamVR keeps the Menu button for itself.** Some streaming set-ups use the left Menu button for their own dashboard, and it is not verified that the app receives it (I). The fallback needs no Menu button: **both stick clicks together** take Menu's whole job (tap for Start, 1 second for the VR menu, held for the three shortcuts). Stick clicks already act on release, so a double click can be told from a single one. P2 measures which of the two ships as the default.

#### Valve Index (`valve/index_controller`)

| Index control | 3DS | Notes |
|---|---|---|
| **A**, **B** (right) | A, B | By label |
| **A**, **B** (left) | X, Y | Lower button X, upper button Y: the same places as on Touch |
| Left grip, right grip | L, R | From `squeeze/force` with a firm threshold. `squeeze/value` is thought to rise from simply holding the controller (I), so it is not used; P2 checks it |
| Left trigger, right trigger | ZL, ZR | Touch on the Secondary Screen |
| Left stick | Circle Pad | |
| **Left trackpad, pressed** | D-pad | Press the top, bottom, left or right of the pad. Resting a thumb on it does nothing |
| Right stick | C-Stick | |
| Right stick click | Select | |
| **Right trackpad, pressed**: tap | Start | |
| Right trackpad, held 1 second | The VR menu | |
| Hold right trackpad, then right grip / left grip for half a second / right trigger | Quick Save / Quick Load / Fast Forward | As on Touch |

Index needs no mode switch: the left trackpad is a D-pad next to the stick, just as the 3DS has a D-pad next to the Circle Pad. The left stick click is free for **Edit Controls**. The System buttons belong to SteamVR.

#### Other Controllers

| Controller | Scheme |
|---|---|
| Vive Focus 3 and XR Elite (`htc/vive_focus3_controller`) | The Touch scheme |
| Vive wands (`htc/vive_controller`) | Pointing and menus only. Play with a gamepad |
| `khr/simple_controller` | Menus only |
| Any gamepad | Works alongside the VR controllers, as in Azahar for Frame. For demanding action games it is the best way to play |

#### Known Weak Point

**X and Y are on the left hand, with the Circle Pad.** On a 3DS the right thumb reaches all four face buttons. On Touch and Index it reaches two. Games that hold X or Y while moving (running in Super Mario 3D Land, attacks in Monster Hunter) will feel wrong with the default.

- **Today's answer:** **Edit Controls** already rebinds any input, per kind of controller.
- **To try in P2:** a **Right-Hand Buttons** option that puts X and Y on the right stick (up for X, left for Y, the places they have on a 3DS) for games that do not use the C-Stick. It is not in the default because a stick flick is a poorer button than a button.

#### Options Considered and Set Aside

| Option | Why not the default |
|---|---|
| CitraVR's thumbrest D-pad | Needs a touch sensor that Index lacks, and that a stream may not pass on. It can come back as a setting if P2 shows the sensor arrives reliably |
| D-pad on the right stick | Clashes with the C-Stick in every game that uses it. Offered as a per-game setting (**D-pad On: Left Stick** or **Right Stick**) for games with no C-Stick use, where it lets you press the D-pad while moving |
| L and R on the triggers | The triggers are the touch. A press of L while the hand happens to point at the Secondary Screen would tap it instead |
| Grips grab the screens, as on the Frame | Leaves nowhere for L and R |
| A hold-to-shift layer for the D-pad | Touch has no spare button that is comfortable to hold while the same thumb uses the stick |

#### As Built (2026-11-22)

- **Tables:** `src/frame_xr/input/default_bindings.h` `kControllerSchemes`, one row per profile: where the Menu role and the D-pad come from, whether the grips are L and R, whether both stick clicks are Menu, whether the stick clicks are for the game, whether the face buttons go by label. The suggested binding paths are tables in `actions.cpp` (`kIndexPaths`, `kFocus3Paths`; Touch keeps the existing list). Index and Focus 3 suggestions and the trackpad actions exist only in PC builds.
- **Adapter:** `src/frame_xr/input/pc_scheme.{h,cpp}`, in front of the unchanged mapper. `Normalize` gives the Menu role (Index right trackpad, both stick clicks) and the Index trackpad D-pad to every reader; `Game` turns grips into the bumpers (L and R), plays Menu's tap, hold and chords, and the stick clicks. With the Frame's scheme both do nothing.
- **Thresholds:** grips press above 0.60 and release below 0.40; the Index trackpad presses above a force of 0.50 (released below 0.30) and has a dead middle of 0.30; the stick D-pad engages past 0.50 and releases within 0.35; Menu tap under 0.3 s, hold 1 s; Quick Load after 0.5 s; 1 s between chord actions.
- **Per game:** the left stick's D-pad mode is kept in `config/custom/<title id>.dpad`; a toast says "Left Stick: D-Pad" or "Left Stick: Circle Pad".
- **Deviations from the draft above:**
  - no new `Control` values: the trackpad zones and the stick D-pad press the existing D-pad controls, so the Controls dialog and saved profiles are unchanged;
  - the Face Buttons setting is ignored for Touch, Index and Focus 3 (always by label) but still applies to other gamepads; its row is not hidden yet;
  - Vive wands and `khr/simple_controller` get the Touch scheme (whatever they bind), not a menus-only mode;
  - Kid Icarus's Grip Frees Aim and the immersive screen grab use the grips on the Frame; on PC the grips are L and R, so those are off until section 3.4's menu grab exists.
- **Also open:** the log lines for the chords still name the Frame's buttons ("View, then Right Bumper"); save, load and Fast Forward share one 1 s cooldown (the Frame keeps Fast Forward's apart); the Frame's build also looks the profile up and logs it (it keeps the Frame's scheme); the About dialog, the picker's header and `frame_mode.cpp`'s `~/.local/share/AzaharFrame` still say Azahar for Frame on PC.
- **Checks:** unit tests in `src/tests/frame_xr/pc_scheme.cpp`; mock runs per profile with `MOCK_XR_PROFILE` (the game's held buttons through the RPC server).
- **Still to measure on a headset (P2):** whether the left Menu button reaches the app over Steam Link; how Index `squeeze/force` reads; whether SteamVR offers the Focus 3 extension.

### 3.4 What Moves Into the Menu

On the Frame these are controller shortcuts. In Azahar VR they are menu rows only, unless the last column says otherwise. Most of the rows already exist.

| Action | On the Frame | In Azahar VR | Menu row |
|---|---|---|---|
| Move and resize the screens | Grips, plus the stick | **With the VR menu open**, grips grab the screens and the stick resizes them. In a game the grips are L and R | Position: new **Move Screens** hint |
| Nudge the screens, Screen Curve | Grip + D-pad | Menu only | Position: **Screen Curve** (exists); nudging is replaced by grabbing |
| Swap Screens | Right stick click, tap | Menu only | Position: **Swap Screens** (exists) |
| Hide or Show Secondary Screen | Left stick click, tap; both bumpers + **View** | Menu only | Position: **Hide Secondary Screen** (exists) |
| Screen Layout | Left stick click, held | Menu only | Position: **Screen Layout** (exists) |
| Reset Screens and View | Right stick click, held | Menu only | Position: **Reset Position** (exists) |
| Switch between VR and Flat | Both bumpers + **View** + **Menu** | Menu only | Main: **Switch to Flat** (exists) |
| Stop the game | Hold **View** 1.5 seconds | Menu only | Main: **Close Game** (exists) |
| Quick Save, Quick Load | **View** chords | Menu, and the Menu chords in §3.3 | Main (exists) |
| Fast Forward | **View** + right trigger | Menu, and the Menu chord in §3.3 | Main: new **Fast Forward** switch |
| D-pad mode, D-pad On | — | The left stick click, and a row | Controls: new rows |
| Face Buttons (by position or by label) | A setting | Hidden on Touch and Index, where it has no meaning | — |

Two things stay as they are: while a game is starting, holding the menu button for 3 seconds stops it; and on Azahar's own window, with no game running, the grips move the window.

### 3.5 Layers Through the Streaming Runtimes (I Unless Noted)

- **SteamVR plus any streaming driver** (Steam Link, ALVR, VIVE Streaming) and **WiVRn/Monado:** all composition happens on the PC, and the headset receives flattened eye images. Quads are therefore resampled once by the compositor and then video-compressed. No layer survives as a crisp headset-side layer the way it does on the Frame.
- **Virtual Desktop's VDXR:** also composites on the PC, as far as is known.
- **Head-locked screens:** on the Frame they are composited at scan-out time with the latest pose. Over a stream, the headset reprojects the flattened frame as if it were fixed in the room, so head-locked screens wobble by the pose-prediction error. That is small, but visible in immersive modes on fast head turns. **Measure it in the first headset session.**
- **Foveated encoding** (Steam Link, Virtual Desktop) softens the edges of the view. Screens near the centre are fine; wide immersive views lose edge detail.

---

## 4. PC Versus the Frame

**Better (I unless noted):**

- **GPU headroom.** The internal resolution can go to 6–10x, where the Frame default is 4x (V, `settings.h`). Dynamic Resolution may rarely engage, and mode 3's stereo doubling is cheap.
- **x86_64 dynarmic** is upstream's primary JIT target.
- **Shader compiles and texture-pack loading** are fast, and there is far more RAM, so the memory guard and preload budget matter little.
- **Development:** native builds and mock runs on a real GPU.

**Worse (I):**

- **Streaming latency** (roughly 30–50 ms) and **compression** of fine 3DS pixel art and text. The touch screen's small text is the most at risk.
- **Layer flattening:** the headset-side sharpness of quads is lost.
- **No Black Frame Insertion** on streamed headsets.
- **Controls:** no D-pad or bumpers on Touch or Index (§3.3).
- **Hardware variety:** many headsets, controllers and runtimes to test.
- **No Game Mode shell:** the Steam library entry and the SteamVR dock and theatre are SteamOS-flavoured.
- **NVIDIA on Linux:** WiVRn says NVIDIA is "not properly optimized" (V, WiVRn README); Steam Link VR on Linux is beta-quality (I).

## 5. Prior Art

- **CitraVR** (amwatson) is Quest-standalone only: an Android OpenXR app (V, `docs/TECHNICAL_NOTES.md`). Azahar for Frame descends from it, and its mode 3 stereo patch is already here.
- **jakedowns/CitraVR** is a Quest fork (V).
- **No native PC VR (SteamVR/OpenXR) 3DS emulator** turned up in a quick search (I, limited search). The PC VR practice today is Citra or Azahar side-by-side 3D shown in a virtual-screen app (I).
- This would plausibly be the first native PC VR 3DS emulator (I).

---

## 6. Phased Plan

| Phase | Goal | Files | Risks | Effort | Needs a headset? |
|---|---|---|---|---|---|
| **P0 Fork set-up, native build and mock** | The name "Azahar VR" and its own data folder; x86_64 Linux build with `ENABLE_FRAME_XR=ON`; unit tests (`src/tests/frame_xr`, `frame_qt_tests`) pass; the mock runtime built for x86_64; `tools/mockrun.sh` runs on the desktop GPU | `CMakeLists.txt:170-174`, `xr_loader.cpp` (add the system loader), `tools/xrmock/build.sh`, a new PC build script beside `steam-frame/`, the app name and data-folder constants | Link-time issues; the system Qt against the portable Qt | 3–4 d | No. The build fixes are offered back to Azahar for Frame |
| **P1 Runtime-agnostic XR core (Linux + SteamVR)** | Starts and runs a game on desktop SteamVR; the controller schemes of §3.3; the menu moves of §3.4; the 120 Hz guard (by merge or cherry-pick) and the hidden Black Frame Insertion row | `xr_loader.cpp`, `session.cpp` (system and runtime name; FB rate request best-effort), `actions.{h,cpp}` + `default_bindings.h` + `mapper.cpp`, the menu rows, a PC launcher (no systemd scope, `linux64/vrcmd` or none), gating in `citra_qt/CMakeLists.txt`, `frame_mode.cpp` | SteamVR on Linux with NVIDIA; the mapper is the most merge-sensitive file | 1–1.5 wk | Mostly no: build, mock session with a new "profile" option, unit tests for each scheme. Starting SteamVR on the desktop needs the owner's OK |
| **P2 First headset pass** | Play on a Quest 3 (Steam Link from Linux; ALVR or WiVRn as fallbacks) and, if available, a wired headset. Measure: latency; text legibility at 4x, 6x and 8x; head-locked wobble in modes 2–3; the refresh rate obtained; the layer count; **whether the left Menu button reaches the app (tap and 1 second hold)**; whether thumbrest touch arrives; how the Index grips read (`squeeze/force` against `squeeze/value`), if an Index is at hand; how the D-pad switch and the X/Y placement feel in three or four games | none (logs: `xr: system`, `runtime`, `display refresh rates offered`) | Steam Link on Linux is beta; the Menu button may be taken | 1–2 headset sessions + 3–5 d fixes | **Yes.** This is the go or no-go gate |
| **P3 Controls and pacing settled** | Fix the default from P2's findings (Menu or both stick clicks; Right-Hand Buttons or not); the "known wired" driver list for the experimental Black Frame Insertion row; a 120 Hz launcher setting for wired PC headsets (`vrcmd linux64`) | `mapper.cpp`, `default_bindings.h`, settings and menu rows, tests | Small | 1–2 d | Logic and tests: no. The verdict: yes |
| **P4 Visuals per controller** | Controller models from desktop SteamVR render models (Touch, Index, Vive, Focus 3); per-profile help diagrams; Kid Icarus weapon fits per family; hide the ASTC optimizer on PC (or add a BC7 target) | `core/frame_controller_models.*`, `vis/controller_models.cpp`, `vis/controller_diagram.cpp`, `vis/help_content.cpp`, `vis/weapon_fit.*`, `citra_qt/frame_texture_pack.h` | Aim-space offsets per model; licence (read from the player's own SteamVR install only, as now) | 1.5–2 wk (a release can ship beams-only first) | The converter and offline renders: no. Alignment and fit: yes |
| **P5 Windows port** | MSVC (or MinGW) build with `ENABLE_FRAME_XR=ON`; ship `openxr_loader.dll`; SDL gamepads in place of evdev; a crash handler stub (or a Windows unhandled-exception filter); watchdog and memory-watch stubs; `%APPDATA%` test switches | the ~25 files in §2's last row, new `frame_xr/platform_{posix,win32}.cpp` behind one small header, `src/core` and `src/common` CMake lists (gate `frame_dock_icon`, `steam_cef`, the `frame_weapon_models` priority code), a `windows-latest` CI job | MSVC strictness on fork code; Qt set-up | 2.5–3.5 wk (+ about 0.5 wk of Windows packaging in P6) | Builds run in CI. Running it needs a Windows PC with a headset |
| **P6 Packaging, CI and docs** | CI: linux-x86_64 (container) and windows-x64; a PC tarball and zip; the player docs rewritten for PC; changelog | `.github/workflows/`, PC packaging scripts beside `steam-frame/`, `docs/` | The player docs are large | 4–5 d | No |

**Order:** P0 → P1 → P2 (the go or no-go gate) → P3 → P6 (Linux release) → P4 → P5 → P6 (Windows). P5 can start alongside P3 if a second person or lane is free.

**Why Linux x86_64 + SteamVR + Quest 3 over Steam Link first:**

- it is the same runtime the code already targets;
- it uses the same OS APIs, so there is nothing to port;
- it configures and compiles today;
- P0 pays back at once, as a faster test rig for Azahar for Frame too.

**The trade-off:** most PC VR users, Virtual Desktop and VIVE Streaming (the XR Elite's route) are on Windows, so Windows matters for reach. It is deliberately second, built in CI.

**Monado and WiVRn:** "should work" (same extensions, no colour-scale extension) after SteamVR. Not the first target.

## 7. Still Open

1. **The controller schemes in section 3.3:** accepted as a draft; final defaults after the first headset pass.
2. **Section 3.4's menu moves:** grabbing the screens with the menu open, the Fast Forward row, the Left Stick rows, hiding Face Buttons on PC.
3. **Kid Icarus weapon-in-hand and Angel's Aim per controller:** later, with P4.
4. **Starting SteamVR on the development desktop** for smoke tests: needs the owner's OK.
5. **The first Windows build:** CI's `windows-msvc` job, after a push the owner approves.

## Sources

- [Khronos OpenXR runtime extension support](https://github.khronos.org/OpenXR-Inventory/runtime_extension_support.html) (its cylinder "N" for SteamVR is contradicted by the Frame's own logs)
- [CitraVR](https://github.com/amwatson/CitraVR)
- [jakedowns/CitraVR](https://github.com/jakedowns/CitraVR)
- [WiVRn](https://github.com/WiVRn/WiVRn)
- [GamingOnLinux: Steam Link VR on Linux](https://www.gamingonlinux.com/2025/09/steam-link-vr-for-meta-quest-on-linux-now-appears-to-work-in-the-latest-updates)
