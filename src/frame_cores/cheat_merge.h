// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// What the cores need of downloaded cheats (cheat_db.h), kept apart from it: cheat_db.h reaches
// core/cheats/online.h, whose namespace Cheats collides with gba-ds's `Cheats` type in gba_core.cpp.

#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "frame_cores/core_backend.h"

namespace FrameCores::CheatDb {

inline constexpr std::size_t kMaxStoredCheats = 4000; // a game's list, own and downloaded

/// Puts the entries `incoming` into `list` after its own, off, leaving out one with the same name
/// and code, up to kMaxStoredCheats. Returns how many were added.
inline int MergeImported(std::vector<CheatEntry>& list, const std::vector<CheatEntry>& incoming) {
    int added = 0;
    for (const auto& in : incoming) {
        if (list.size() >= kMaxStoredCheats) {
            break;
        }
        const bool have = std::any_of(list.begin(), list.end(), [&](const CheatEntry& e) {
            return e.name == in.name && e.code == in.code;
        });
        if (!have) {
            list.push_back(CheatEntry{in.name, in.code, false});
            ++added;
        }
    }
    return added;
}

} // namespace FrameCores::CheatDb
