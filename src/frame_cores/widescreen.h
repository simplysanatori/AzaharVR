// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// The Mods tab's widescreen mod for a DS game: one of TWiLight Menu++'s hacks
// (github.com/DS-Homebrew/ TWiLightMenu, resources/widescreen, GPL-3.0). None is bundled or
// re-hosted; the one for the game is fetched from a pinned commit when the player installs it, and
// kept in the game's own folder.
//
// Which hack is the game's comes from an index (a copy of ds-gba-game-fixes'
// widescreen/twilight-index.tsv, widescreen_index.cpp): the game code and the CRC16 in the ROM's
// header, or FFFF for any version. A hack is Action Replay code that writes what the game needs for
// a 16:10 picture, so while one is on the Primary Screen is shown at 16:10.
//
// Both the file and the index are hostile until checked: a size and a SHA-256 for what is
// downloaded, and only the kinds of code the hacks use, whole, for what is run (AREngine checks
// none of it). No Qt; the network is a function the caller gives.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/cheats/online.h"

namespace FrameCores::Widescreen {

/// The Primary Screen's shape with a hack on: the 3DS's widescreen mode, which they are made for,
/// shows it at 384x240.
inline constexpr float kAspect = 16.0f / 10.0f;

inline constexpr std::size_t kMaxIndexBytes = 1024 * 1024;
inline constexpr std::size_t kMaxEntries = 5000;
inline constexpr std::size_t kMaxFileBytes = 4096;
inline constexpr std::uint16_t kAnyVersion = 0xFFFF;

inline constexpr const char* kSourceUrl = "https://github.com/DS-Homebrew/TWiLightMenu";

// ---- The index ----

struct Entry {
    std::string code;                  ///< game code
    std::uint16_t crc16 = kAnyVersion; ///< the header's
    std::string file;                  ///< "<code>-<crc16>.bin", as TWiLight Menu++ names it
    std::uint32_t size = 0;            ///< 0: a ROM that has no hack
    std::string sha256;                ///< lower-case hex
};

struct Index {
    std::string commit; ///< TWiLight Menu++'s
    std::vector<Entry> entries;
};

struct IndexParse {
    Index index;
    int skipped = 0;   ///< lines that were not a valid entry
    std::string error; ///< non-empty: the text is not an index
};

/// Reads "azahar-widescreen-index 1", "twilight <40 hex>", then one line a hack: game code, CRC16,
/// file name, size, git blob SHA-1, SHA-256 and name, with tabs between ('#' lines are notes).
IndexParse ParseIndex(std::string_view text);

/// The program's own (widescreen_index.cpp).
std::string BuiltInIndexText();
const Index& BuiltIn();

/// The game's hack: the one for its header, else the one for any version. Null when it has none,
/// also for a header the index lists with no hack.
const Entry* Find(const Index& index, std::string_view game_code, std::uint16_t header_crc16);

/// The request path of the hack.
std::string UrlPath(const Index& index, const Entry& entry);

// ---- The hack ----

/// The Action Replay words of a hack's file; nothing when any of it is not a code they use.
std::optional<std::vector<std::uint32_t>> ReadCodes(std::span<const std::uint8_t> file);

/// "" when it is the entry's file and whole, else why not.
std::string Verify(const Entry& entry, std::string_view file);

// ---- The player's, in the game's folder ----

enum class State { None, Off, On };

State StateOf(const std::string& game_dir, const Entry& entry);
/// Turns an installed hack on or off, from the next start of the game.
bool SetOn(const std::string& game_dir, const Entry& entry, bool on);
bool Remove(const std::string& game_dir, const Entry& entry);
/// Fetches the hack with `get` (blocking: off the GUI thread), checks it and puts it in the game's
/// folder, on. "" on success, else why.
std::string Install(const Cheats::Online::HttpGet& get, const Index& index, const Entry& entry,
                    const std::string& game_dir);

/// What the game that starts now runs: the codes of its hack when that is on; else none.
std::vector<std::uint32_t> CodesFor(const std::string& game_dir, const Entry& entry);

} // namespace FrameCores::Widescreen
