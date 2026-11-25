// Copyright 2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <optional>
#include <string>

namespace UpdateChecker {
/// AzaharVR updatecheck: the tag of the latest published release of simplysanatori/AzaharVR (never
/// a pre-release), one HTTPS GET. nullopt on any failure (offline, rate limit, bad reply); runs on
/// the calling thread, so call it off the UI thread.
std::optional<std::string> GetLatestRelease();
} // namespace UpdateChecker
