# Azahar VR 1.3.0

Azahar for Frame is now **Azahar VR**: the same app, now also for Windows and Linux PCs with SteamVR. On the Steam Frame it is still called Azahar for Frame. The repository is now `simplysanatori/AzaharVR`; old links redirect.

## Kid Icarus: Uprising: Angel's Aim

- New Immersive Mode **Full (6DoF): Kid Icarus: Uprising / Angel's Aim**, recommended for the game.
- New **Turning** options: **Smooth** and **Snap** (steps of 30°, 45°, 60° and 90°).
- New camera views: **First Person** and **Over the Shoulder**, with **Pit Opacity**.
- New **Bottom Screen** placement: **Off**, **Persistent** and **Wrist**.
- You can now change the **Reticle Size**.
- **Trigger Layout** on the **Controls** tab, with **Aiming** on:
  - **Fire and Power**: your aiming hand's trigger only fires. The other trigger uses your power, and the bumpers pick it.
  - **Original**: both triggers fire and also press the game's own buttons.
- Every Angel's Aim option is also under **Mods** > **Angel's Aim**, in groups. They show only while Kid Icarus: Uprising is running.
- Angel's Aim now supports the European version (Rev 0) and the USA version (Rev 1).

## Azahar VR for PC (New)

- Azahar VR is Azahar for Frame on a PC. It has the same in-game menu, immersive modes, **Mods** tab, texture packs and cheats, through SteamVR.
- **Windows** (64-bit).
- **Linux** (x86-64): untested. Bug reports are welcome.
- Live switching between Flat and VR.
- **Mods** and texture packs download and install as on the Frame.

### What You Need (Windows)

- SteamVR.
- [7-Zip](https://www.7-zip.org), for some mod archives. Windows' own tar handles zip, 7z and most RAR files.

## Emulator-Wide Improvements

- New **HUD Spacing: Horizontal** and **HUD Spacing: Vertical** (Experimental), in the Full modes: lower brings the game's HUD in from the edges, closer into view; higher spreads it out. 100% is the game's own. Mario Kart 7's race HUD comes into view with it.
- New Bottom Screen and Stats placement on your wrist: **Wrist Side**, **Back** or **Inside**.
- Improved **Black Frame Insertion**. Reminder: it only works at 120 Hz.
- Improved **Dynamic Resolution**: it remembers a separate level for when Black Frame Insertion is on, because of its extra processing and different performance.
- Logs keep the last 8 runs (`azahar_log.old.txt`, then `.old2` to `.old8`).

### Fixes

- After loading a save state, a game with mods reads its files from the running game again.
- A texture from a pack no longer shows up on a different surface.
- In packs that skip mipmap levels, a later level no longer hides the game's own.
- **Per-Game** settings keep their saved values after a game is started with other settings for a single run.
- **Full (6DoF): Other Games** keeps its view after the in-game menu saves.

## Known

- Kid Icarus: Uprising: **Over the Shoulder** still shows a controller in your free hand.
- PC: folders with characters outside English in their names aren't supported yet.
- PC, streaming to a Steam Frame from SteamVR: a reminder that SteamVR uses foveated streaming. If it bothers you or you notice issues, change **Foveated Encoded Video Size** and **Preferred Foveated Sharpness** in SteamVR's **Streaming** settings to improve the experience.

## Download

- `azaharframe-1.3.0-linux-arm64.tar.gz`: Azahar for Frame, for `install.sh` and Frametanium.
- `azaharframe-1.3.0-framedrop-linux-arm64.zip` and `azaharframe.framedrop.json`: the same, for FrameDrop.
- `azahar-vr-1.3.0-windows-x86_64.zip`: Azahar VR for Windows.
- `azahar-vr-1.3.0-linux-x86_64.tar.gz`: Azahar VR for Linux.

## Install

**Frame:** download the tarball and run `steam-frame/install.sh` from the repository with it as the argument. You can also update in Frametanium, or [install with FrameDrop](https://framedropvr.com/install?manifest=https%3A%2F%2Fgithub.com%2Fsimplysanatori%2FAzaharVR%2Freleases%2Flatest%2Fdownload%2Fazaharframe.framedrop.json).

**Windows:** unzip `azahar-vr-1.3.0-windows-x86_64.zip`, start SteamVR, then run `azahar.exe`.

## Reporting Problems

Use **Report a Problem**: in the in-game menu under **Settings** > **General** > **Help**, or under **Help** in Azahar's window. You can also open an issue with what you did, what you expected and what happened, and attach your log:
- Frame: `user/log/azahar_log.txt` from `~/.local/share/AzaharFrame`.
- Windows: `log\azahar_log.txt` from `%APPDATA%\AzaharVR`.

Logs hold the paths of your games, which include your user name. Read them first.
