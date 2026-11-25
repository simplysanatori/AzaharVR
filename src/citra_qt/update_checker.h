// Copyright 2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <optional>
#include <string>
#include "core/mods/transfer.h"

namespace UpdateChecker {
/// AzaharVR updatecheck: the tag of the latest published release of simplysanatori/AzaharVR (never
/// a pre-release), one HTTPS GET. nullopt on any failure (offline, rate limit, bad reply); runs on
/// the calling thread, so call it off the UI thread.
std::optional<std::string> GetLatestRelease();
/// The same, stoppable: a stopped `stopper` (Stop(), any thread) ends the lookup, the connect and
/// the read at once, so quitting never waits for a dead DNS server or a stalled socket. A stopper
/// that is already stopped returns nullopt without any network use.
std::optional<std::string> GetLatestRelease(Mods::Transfer::Stopper& stopper);
/// Stops the check GetLatestRelease() runs (the window is closing).
void RequestStop();
} // namespace UpdateChecker
