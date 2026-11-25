// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// The games the VR menu's Games tab lists: what the desktop game list found, published as one
// shared list that nobody changes after (a new list replaces it). No Qt and no emulator types, so
// the menu draws it and the tests build it on any machine.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/game_system.h"

namespace FrameXr::Menu {

/// A game's own picture: straight RGBA, rows top down (48x48 for the 3DS, 32x32 for the others).
struct GameIcon {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
};

struct GameEntry {
    std::string path;      ///< what starts it
    std::string real_path; ///< the same for every way to reach one file ("": as `path`)
    std::string title;
    std::string file_name; ///< without its folder and extension
    std::string region;    ///< "" unknown or more than one
    uint64_t key = 0;      ///< the title id, or the per-game key of the other systems
    Common::GameSystem system = Common::GameSystem::N3DS;
    bool favorite = false;
    bool two_screens = false;
    bool pokedaisy = false;
    bool missing = false; ///< its file was not there when it was pressed
    std::shared_ptr<const GameIcon> cover; ///< its cover as a tile shows it (cover_art.h), null none
    uint64_t played_s = 0; ///< the time it was played, in seconds
    std::shared_ptr<const GameIcon> icon; ///< null: a drawn one
    bool operator==(const GameEntry&) const = default;
};

struct GamesSnapshot {
    std::vector<GameEntry> games; ///< in the order the tab shows them
    bool operator==(const GamesSnapshot&) const = default;
};
using GamesList = std::shared_ptr<const GamesSnapshot>;

/// The list as the tab shows it, from the game list's rows in any order: each file once, the
/// recently played games first (`recent`: their paths, newest first), then the rest by title. A
/// game whose title the menu's font cannot draw, or that shares its title with another game of its
/// system, has its file's name.
GamesSnapshot MakeSnapshot(std::vector<GameEntry> rows, const std::vector<std::string>& recent);

/// Whether the menu's font has every letter of a name (Latin only).
bool NameDrawable(const std::string& utf8);

/// Whether the tab shows at all: only once a game of another system than the 3DS is listed.
bool GamesTabApplies(const GamesList& list);

/// The chips that show, in order: none with one system, else All and each system that has games.
std::vector<Common::SystemFilter> GameChips(const GamesList& list);
/// The chip in effect: `wanted` while it shows, else All.
Common::SystemFilter GameChip(const GamesList& list, Common::SystemFilter wanted);

enum class GamesSort : int { RecentlyPlayed, FavouritesFirst, Title, PlayTime, Count };

/// The games under a chip whose title or file name has `search` in it (any case, accents aside),
/// sorted, as indices of `list->games`.
std::vector<int> GamesShown(const GamesList& list, Common::SystemFilter chip, GamesSort sort,
                            const std::string& search);

const char* ChipName(Common::SystemFilter chip);
const char* SystemName(Common::GameSystem system);
const char* SortName(GamesSort sort);
/// A tile's small line, the longest first: the one that fits shows.
std::vector<std::string> GameLines(const GameEntry& game, bool running);

} // namespace FrameXr::Menu
