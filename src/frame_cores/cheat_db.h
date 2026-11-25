// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// The Cheats tab's Download for DS, GBA and Game Boy games. The cheats are libretro-database's
// (github.com/libretro/libretro-database, cht/, CC-BY-SA-4.0): never bundled or re-hosted, one file
// for the game fetched from a pinned commit when the player asks.
//
// Which file is the game's comes from an index the app downloads from the ds-gba-game-fixes repo
// (cheats/libretro-index.tsv, at a pinned commit; built offline by tools/build_cheats_index.py from
// libretro's No-Intro data): the game code and the CRC32 of the whole ROM file. A game that is not
// there by CRC may match by file name, and the result says so. A cheat is never switched on here.
//
// Everything read from the network is hostile: sizes and counts are limited, a file name from the
// index is checked and percent-encoded, the folder comes from a table in this file, and a cheat that
// is not a valid code for the system is dropped. No Qt; the network is a function the caller gives.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/cheats/online.h"
#include "frame_cores/cheat_merge.h"
#include "frame_cores/core_backend.h"

namespace FrameCores::CheatDb {

/// The pins. The index and the cheat files are only ever read at these commits.
inline constexpr const char* kLibretroCommit = "f89f09cbe13f9f7863ecf20d0ebf2c5ce53be029";
/// Of github.com/simplysanatori/ds-gba-game-fixes, the commit holding cheats/libretro-index.tsv
/// built for kLibretroCommit. Empty until that commit is pushed: Download then says no file list.
inline constexpr const char* kFixesCommit = "";

inline constexpr const char* kCreditText = "Cheats: libretro-database, CC-BY-SA-4.0";
inline constexpr const char* kCreditUrl = "https://github.com/libretro/libretro-database";

inline constexpr std::size_t kMaxIndexBytes = 4 * 1024 * 1024;
inline constexpr std::size_t kMaxIndexEntries = 30000;
inline constexpr std::size_t kMaxChtBytes = 1024 * 1024; // the largest DS file is about 150 KB
inline constexpr int kMaxCheats = 2000;                  // one DS file has 1537
inline constexpr std::size_t kMaxNameLength = 120;
inline constexpr std::size_t kMaxFileNameLength = 200;
inline constexpr std::uint64_t kMaxRomBytes = 1ull << 30;

enum class System { Nds, Gba, Gb, Gbc };

/// libretro's folder under cht/ ("Nintendo - Nintendo DS").
const char* SystemDir(System system);
/// The index's tag for it ("nds", "gba", "gb", "gbc").
const char* SystemTag(System system);
std::optional<System> SystemFromTag(std::string_view tag);
/// A DS game has a game code (DSi games too, in libretro's DS folder); a Game Boy game has none.
bool SystemHasCode(System system);

// ---- The index ----

struct IndexEntry {
    System system = System::Nds;
    std::string code; ///< "" when the system has none
    std::uint32_t crc = 0;
    std::string name; ///< the cht file's name without ".cht"
};

struct Index {
    std::string libretro_commit;
    std::vector<IndexEntry> entries;
};

struct IndexParse {
    Index index;
    int skipped = 0;   ///< lines that were not a valid entry
    std::string error; ///< non-empty: the file is not an index
};

/// Reads the index: "azahar-cheat-index 1", "libretro <40 hex>", then one line a game,
/// "<tag> TAB <code or -> TAB <8 hex CRC32> TAB <file name>" ('#' lines are notes).
IndexParse ParseIndex(std::string_view text);

struct Match {
    const IndexEntry* entry = nullptr;
    bool by_name = false;
};

/// The game's entry: the game code (where the system has one) and the CRC32 of the whole file;
/// else the ROM's file name (`rom_stem`, no folder or extension) against the entries' names, with
/// the same code. `crc` is empty when it could not be computed.
Match FindMatch(const Index& index, System system, std::string_view game_code,
                std::optional<std::uint32_t> crc, std::string_view rom_stem);

/// The request path of the index at `commit`.
std::string IndexUrlPath(std::string_view commit);
/// The request path of the entry's cht file at `commit`, percent-encoded.
std::string ChtUrlPath(std::string_view commit, System system, std::string_view file_name);

// ---- The cheat file ----

struct ChtParse {
    std::vector<CheatEntry> cheats; ///< all off, in file order
    int skipped = 0;                ///< with a code that is not valid for the system
    bool truncated = false;         ///< more than kMaxCheats, or the file was over kMaxChtBytes
};

/// libretro's .cht text: cheatN_desc, cheatN_code (words joined by '+'), cheatN_enable (ignored:
/// nothing is switched on). The code is kept as the cores read it, one line of two words each
/// (Nds, Gba; "XXXXXXXX YYYYYYYY", Gba also "XXXXXXXX YYYY") or one word each (Gb, Gbc).
/// An entry with no code is a heading and is left out.
ChtParse ParseCht(std::string_view text, System system);

/// A code the player typed (the Cheats tab's Add Cheat), as ParseCht's: "" and `code` set when it
/// is valid for the system, else why not, in the 3DS flow's words ("Enter a Code", "Line 2: Not a
/// Code"). Lines of one or two hex words (DS: 8+8 digits; GBA: 8+8, or 8+4 for Code Breaker; Game
/// Boy: one word, 8 digits or a Game Genie code "XXX-XXX"); a word may be joined as 16 (or 12) digits.
std::string NormalizeTyped(const std::string& text, System system, std::string& code);

// ---- The download ----

struct Pins {
    std::string fixes_commit = kFixesCommit;
    std::string libretro_commit = kLibretroCommit;
};

struct Result {
    std::vector<CheatEntry> cheats;
    bool found = false;   ///< a file for the game was found and read
    bool by_name = false; ///< ... by its name, not its CRC32
    int skipped = 0;
    std::string error; ///< non-empty: nothing could be fetched
};

/// Finds and fetches the game's cheat file with `get` (blocking; call it off the GUI thread).
Result Download(const Cheats::Online::HttpGet& get, System system, std::string_view game_code,
                std::optional<std::uint32_t> crc, std::string_view rom_stem,
                const Pins& pins = {});

/// The CRC32 of the whole ROM file (or of the first ROM inside a .zip). Empty when it cannot be
/// read or is over kMaxRomBytes. Blocking and slow for a big DS game: off the GUI thread.
std::optional<std::uint32_t> RomCrc32(const std::string& path);

} // namespace FrameCores::CheatDb
