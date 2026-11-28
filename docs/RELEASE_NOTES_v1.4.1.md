# Azahar VR 1.4.1

Eye-tracked **Foveated Rendering**, and fixes for Vive XR Elite, Touch and Index controllers.

## Foveated Rendering (New, Experimental)

- Draws the game at full resolution where you look and lower further out, in **Full**, **Full with Follow** and **Angel's Aim** Immersive Modes.
- **Off**, **Low**, **Medium** or **High**, per game: in the in-game menu under **Settings** > **Immersive**, and for Kid Icarus: Uprising under **Mods** > **Angel's Aim** > **View**.
- Fine-tune it with **Inner Ring Size**, **Middle Ring Size**, **Middle Ring Resolution** and **Outer Ring Resolution**.
- On the Steam Frame it is on **Low** by default. Kid Icarus: Uprising at 5x **Internal Resolution**: 42 FPS with it off, 58 FPS on **High**.
- Needs a headset with eye tracking; otherwise it shows **Unavailable**. On PC it is off by default.

## PC Controllers

- **Edit Controls**: when you choose a 3DS button and press the control you want for it, a grip or a push of the left stick now counts. Before, only the face buttons and Menu did, so L, R and the D-pad could not be moved.
- **Move Screens**, on the in-game menu's **Position** tab: the menu closes and the game waits while you grip a screen to move it. That hand's stick moves it nearer or farther and makes it bigger or smaller. The other hand's stick does what the D-pad does on the Steam Frame. Press **B** to finish. In a game the grips stay **L** and **R**.
- The controls help and the in-game menu's **Controls** tab show your controllers, with drawings of Meta Quest, Valve Index and Vive XR Elite controllers made from SteamVR's own models, and what each button does on them.
- **Controllers**, on the **Controls** tab: **Automatic** uses the controllers SteamVR reports. Choose **Meta Quest**, **Valve Index**, **Vive XR Elite / Focus 3** or **Steam Frame** if it picks the wrong ones.
- **Edit Controls** names your controllers instead of calling them Frame Controllers.
- Valve Index: a tap of the left stick click hides or shows the Secondary Screen, as on the Steam Frame.

## Fixes

- Angel's Aim: a possible crash around **Reticle Size** in the per-game settings is fixed.
- The first start after updating deletes the Vulkan shader cache once, since a cache from an earlier version could crash a game at start or when it resumed. Shaders build again as you play.

## Download

- `azaharframe-1.4.1-linux-arm64.tar.gz`: Azahar for Frame, for `install.sh` and Frametanium.
- `azaharframe-1.4.1-framedrop-linux-arm64.zip` and `azaharframe.framedrop.json`: the same, for FrameDrop.
- `azahar-vr-1.4.1-windows-x86_64.zip`: Azahar VR for Windows.
- `azahar-vr-1.4.1-linux-x86_64.tar.gz`: Azahar VR for Linux (untested).

## Install

**Frame:** download the tarball and run `steam-frame/install.sh` from the repository with it as the argument. You can also [install it with FrameDrop](https://framedropvr.com/install?manifest=https%3A%2F%2Fgithub.com%2Fsimplysanatori%2FAzaharVR%2Freleases%2Fdownload%2Fv1.4.1%2Fazaharframe.framedrop.json).

**Windows:** unzip `azahar-vr-1.4.1-windows-x86_64.zip`, start SteamVR, then run `azahar.exe`.

## Reporting Problems

Use **Report a Problem**: in the in-game menu under **Settings** > **General** > **Help**, or under **Help** in Azahar's window. You can also open an issue with what you did, what you expected and what happened, and attach your log:
- Frame: `user/log/azahar_log.txt` from `~/.local/share/AzaharFrame`.
- Windows: `log\azahar_log.txt` from `%APPDATA%\AzaharVR`.

Logs hold the paths of your games, which include your user name. Read them first.
