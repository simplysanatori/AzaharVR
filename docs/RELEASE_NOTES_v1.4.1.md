# Azahar VR 1.4.1

**Foveated Rendering** (Experimental): eye-tracked foveated rendering for 3DS games in **Full**, **Full with Follow** and **Angel's Aim** Immersive Modes. The game is drawn at full resolution where you look and lower further out. On the Steam Frame it is on **Low** by default.

## Foveated Rendering (New)

- **Off**, **Low**, **Medium** or **High**, per game: in the in-game menu under **Settings** > **Immersive**, and for Kid Icarus: Uprising under **Mods** > **Angel's Aim** > **View**.
- Fine-tune it with **Inner Ring Size**, **Middle Ring Size**, **Middle Ring Resolution** and **Outer Ring Resolution**.
- Kid Icarus: Uprising at 5x **Internal Resolution** on the Steam Frame: 42 FPS with it off, 58 FPS on **High**.
- Needs a headset with eye tracking; otherwise it shows **Unavailable**. On PC it is off by default (untested).

## Fixes

- Angel's Aim: a possible crash around **Reticle Size** in the per-game settings is fixed.

## Download

- `azaharframe-1.4.1-linux-arm64.tar.gz`: Azahar for Frame, for `install.sh` and Frametanium.
- `azaharframe-1.4.1-framedrop-linux-arm64.zip` and `azaharframe.framedrop.json`: the same, for FrameDrop.
- `azahar-vr-1.4.1-windows-x86_64.zip`: Azahar VR for Windows.
- `azahar-vr-1.4.1-linux-x86_64.tar.gz`: Azahar VR for Linux (untested).

## Install

**Frame:** download the tarball and run `steam-frame/install.sh` from the repository with it as the argument. You can also update in Frametanium, or [install with FrameDrop](https://framedropvr.com/install?manifest=https%3A%2F%2Fgithub.com%2Fsimplysanatori%2FAzaharVR%2Freleases%2Flatest%2Fdownload%2Fazaharframe.framedrop.json).

**Windows:** unzip `azahar-vr-1.4.1-windows-x86_64.zip`, start SteamVR, then run `azahar.exe`.

## Reporting Problems

Use **Report a Problem**: in the in-game menu under **Settings** > **General** > **Help**, or under **Help** in Azahar's window. You can also open an issue with what you did, what you expected and what happened, and attach your log:
- Frame: `user/log/azahar_log.txt` from `~/.local/share/AzaharFrame`.
- Windows: `log\azahar_log.txt` from `%APPDATA%\AzaharVR`.

Logs hold the paths of your games, which include your user name. Read them first.
