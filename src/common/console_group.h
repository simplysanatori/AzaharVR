// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

namespace Settings {

/// Which emulator's own copy of a graphics setting a game reads and writes (Settings::Graphics).
/// GBA is the group of GBA, Game Boy and Game Boy Color games.
enum class ConsoleGroup {
    N3DS,
    DS,
    GBA,
};

} // namespace Settings
