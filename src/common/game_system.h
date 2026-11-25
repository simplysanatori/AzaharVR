// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

namespace Common {

/// What the game lists call a game's system (a DSi game is a DS game there).
enum class GameSystem { N3DS, DS, GBA, GB, GBC };

/// The lists' filter choices; Game Boy covers Game Boy Color too.
enum class SystemFilter { All = 0, N3DS, DS, GBA, GB };
inline constexpr int kSystemFilters = 5;

/// Whether a game of `system` shows under `filter`.
constexpr bool FilterMatches(SystemFilter filter, GameSystem system) {
    switch (filter) {
    case SystemFilter::N3DS:
        return system == GameSystem::N3DS;
    case SystemFilter::DS:
        return system == GameSystem::DS;
    case SystemFilter::GBA:
        return system == GameSystem::GBA;
    case SystemFilter::GB:
        return system == GameSystem::GB || system == GameSystem::GBC;
    default:
        return true;
    }
}

} // namespace Common
