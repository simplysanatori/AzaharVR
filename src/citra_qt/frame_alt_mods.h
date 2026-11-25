// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// The Mods tab's Mods segment for a DS game (the 3DS one is frame_mods.cpp): its one mod, the
// widescreen hack (frame_cores/widescreen.h). Every change applies from the next start of the game.
// GUI thread only.

#pragma once

#include <functional>

namespace FrameCores {
class AltCoreHost;
}
namespace FrameXr {
struct MenuRequest;
}

namespace FrameAltMods {

/// The running game's mods to the menu: as it starts and as the menu opens.
void Publish(FrameCores::AltCoreHost& host);

/// One of the Mods segment's commands (LoadModCatalog, InstallMod, SetModActive, RemoveMod,
/// OpenUrl; nothing for any other). `running` gives the host again when a download ends (null when
/// no game runs).
void Command(const FrameXr::MenuRequest& request, FrameCores::AltCoreHost& host,
             std::function<FrameCores::AltCoreHost*()> running);

} // namespace FrameAltMods
