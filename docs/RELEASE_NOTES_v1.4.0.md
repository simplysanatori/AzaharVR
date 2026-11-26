# Azahar VR 1.4.0

DS, GBA and GB/GBC games now play in Azahar VR, in both the Steam Frame and PC VR builds, with 3D depth from **Layer Depth** (Experimental). Angel's Aim gets a slew of updates: a **Power Menu** in the HUD, a redesigned and fixed HUD, **Character Portraits**, and more ways to customize how you play, the biggest being **60 FPS** mode.

## DS, GBA and GB/GBC Emulation (New)

- Add a folder with `.nds`, `.gba`, `.gb` or `.gbc` files (or zips of them) to the games list, as for 3DS games. No BIOS files are needed.
- DS games run on melonDS with its JIT, with **Internal Resolution** and **Dynamic Resolution**.
- GBA and GB/GBC games run on the mGBA core, with an optional **Texture Filter**.
- **Layer Depth** (Experimental): the game's layers, or a DS game's 3D scene, spread out in depth for each eye. Set it per game.
- **Second Screen**: GBA games that gba-ds supports get a Secondary Screen of their own, and Pokémon games get [PokeDaisy](https://gitlab.com/pokedaisy/pokedaisy)'s companion pages. If a game supports both, you can choose either.
- **Console** section in the VR menu: **Console Mode**, **Close Lid** and **Microphone Blow** for DS games, which can use your microphone. **X** and **Y** are Auto Fire A and Auto Fire B in GBA and GB/GBC games.
- Save states, autosave, **Undo Load**, **Load Anyway** and **Import Save** work as for 3DS games. Each game keeps its saves, states and cheats in a folder of its own.
- Cheats for DS, GBA and GB/GBC games, downloaded from libretro-database and my ds-gba-game-fixes.
- DS widescreen hacks: TWiLight Menu++'s widescreen hacks install from the **Mods** tab.

## Kid Icarus: Uprising – Angel's Aim

- **Power Menu**: place it over your **Free Hand**, the health bar, the middle or the score, with its own opacity.
- The HUD is redesigned and fixed, with **HUD Opacity** for the health bar and score.
- **Character Portraits**: the talk's portraits and illustrations beside the subtitles, linked to them or placed on their own.
- **Markers**: the markers over enemies are smaller and sit just above them. Change their size, or turn them off.
- **R3** switches between **First Person** and **Third Person** in battles, with a short fade.
- **60 FPS** (Experimental), based on UprisingHack's frame rate changes: on by default on PC, off on the Steam Frame. On the Frame, keep it off unless you play at 1x or 2x **Internal Resolution**.
- **Cutscenes** fill the view and follow the game's camera.
- **Bottom Screen**: **Persistent** is now called **In View**.
- In first person, the streaks and grid around the camera in air and land battles are no longer drawn.
- New defaults for every Angel's Aim setting, in both the European and USA versions. Your Angel's Aim settings are reset to them once, the first time you start the game after updating.

## Emulator-Wide Improvements

- New **Games** tab in the VR menu, with covers. **Download Game Covers** is off by default; Azahar VR asks once when new games are added.
- The games list has a **System** column and filter chips.
- **Screen Layout** and **Screen Curve** can be set per game.
- New **Screen Gap**: the space between linked screens.
- **Controls** is in three sections: **Buttons**, **Pointing** and **Motion**.
- Texture packs now show when an update is available. **Delete** also removes packs you installed yourself.
- Faster texture conversion: **Optimize for Frame**, which converts a pack's PNGs to ASTC, uses more cores while no game is running.
- Azahar VR checks GitHub once at start for a newer release, and asks whether to **Open GitHub**.

### PC

- Started without VR, Azahar VR opens in a window. The **VR** button in the status bar starts it again in your headset, and says why if VR can't start.
- With Touch, Index or Vive controllers, Steam Frame controllers are no longer drawn in your hands.
- Windows: the menus are larger when Windows itself is at 100% scaling.

### Fixes

- Windows: games no longer close to the desktop at start with Virtual Desktop and other OpenXR runtimes besides SteamVR.
- Windows: Azahar VR no longer closes at start when installed in a folder with letters outside your Windows language.
- Turning off **Touch Screen Input** no longer hides the 3DS's reticle.
- Kid Icarus: Uprising: no faint square behind the talk's portraits with the HD texture pack.

## Known

- Kid Icarus: Uprising: some letters can be missing from the subtitles and the results screen.
- Kid Icarus: Uprising: a translucent band can show in some air battles.
- PC, streaming to a Steam Frame from SteamVR: SteamVR uses foveated streaming. If you notice blur, change **Foveated Encoded Video Size** and **Preferred Foveated Sharpness** in SteamVR's **Streaming** settings.

## Download

- `azaharframe-1.4.0-linux-arm64.tar.gz`: Azahar for Frame, for `install.sh` and Frametanium.
- `azaharframe-1.4.0-framedrop-linux-arm64.zip` and `azaharframe.framedrop.json`: the same, for FrameDrop.
- `azahar-vr-1.4.0-windows-x86_64.zip`: Azahar VR for Windows.
- `azahar-vr-1.4.0-linux-x86_64.tar.gz`: Azahar VR for Linux (untested).

## Install

**Frame:** download the tarball and run `steam-frame/install.sh` from the repository with it as the argument. You can also update in Frametanium, or [install with FrameDrop](https://framedropvr.com/install?manifest=https%3A%2F%2Fgithub.com%2Fsimplysanatori%2FAzaharVR%2Freleases%2Flatest%2Fdownload%2Fazaharframe.framedrop.json).

**Windows:** unzip `azahar-vr-1.4.0-windows-x86_64.zip`, start SteamVR, then run `azahar.exe`.

## Reporting Problems

Use **Report a Problem**: in the in-game menu under **Settings** > **General** > **Help**, or under **Help** in Azahar's window. You can also open an issue with what you did, what you expected and what happened, and attach your log:
- Frame: `user/log/azahar_log.txt` from `~/.local/share/AzaharFrame`.
- Windows: `log\azahar_log.txt` from `%APPDATA%\AzaharVR`.

Logs hold the paths of your games, which include your user name. Read them first.
