// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// The one place that says which parts of the in-game menu a console has. A 3DS game has them all
// but the DS, GBA and Game Boy rows; another console has a row only if the table lists it and the
// console can do what it names, so a new 3DS row stays out of the other consoles' menus until it is
// added here.

#pragma once

#include <optional>

#include "common/alt_core_types.h"

namespace FrameXr::Menu {

enum class Id : int;
enum class Tab : int;

using CoreCaps = Common::AltCore::Capabilities;

// What no DS, GBA or Game Boy core does yet.
inline constexpr bool kAltAutosave = true;            ///< true shows Autosave for these cores
inline constexpr bool kAltInternalResolution = false; ///< they draw at the console's own size
inline constexpr bool kAltTextureFilter = false;      ///< their renderer does not read it

constexpr bool Is3ds(const CoreCaps& core) {
    return core.kind == Common::AltCore::CoreKind::N3DS;
}

/// The driver option a row is; none for any other row.
std::optional<Common::AltCore::DriverOption> OptionOf(Id id);

/// Whether the running game's console has this row of the Position, Settings and Controls tabs
/// (also Swap Screens, Switch to Flat and Load State's Autosave card).
bool RowApplies(Id id, const CoreCaps& core);
bool TabApplies(Tab tab, const CoreCaps& core); ///< Games: see TabShown (tab_menu.h)

} // namespace FrameXr::Menu
