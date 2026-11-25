// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharVR homebrew: the curated list of DS, GBA, Game Boy and Game Boy Color homebrew the Homebrew tab
// offers (the same tab as the 3DS one). By link only: nothing is bundled or re-hosted; each entry
// names the author's own page and a direct download address on a host this file allows, with the
// file's size and SHA-256, which are checked before the file is used. Nothing here starts by itself:
// the callers parse a list and download one entry when the player asks. No Qt.
//
// The list (a JSON file, in the layout of ds-gba-game-fixes' catalog/ files):
//
//   { "schema": 1,
//     "homebrew": [
//       { "slug": "example-game",              // [a-z0-9-_], 1 to 64, unique
//         "title": "Example Game",
//         "author": "Example Author",
//         "licence": "MIT",                     // as the author states it; required
//         "system": "gba",                      // "ds", "gba", "gb" or "gbc"
//         "size": 131072,                       // bytes, 1 to kMaxFileSize
//         "sha256": "<64 hex>",
//         "page": "https://example.org/game",   // the author's own page (https)
//         "url": "https://github.com/.../game.gba", // direct download (https, an allowed host)
//         "filename": "game.gba" }              // optional: else the url's last part
//     ] }
//
// An entry that breaks a rule is left out (and named in `rejected`); a list that is not valid JSON,
// has no known "schema", is too big or too deeply nested is refused whole.

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include "common/common_types.h"

namespace Homebrew::Handheld {

enum class System : u8 { DS, GBA, GB, GBC };

constexpr int kSupportedSchema = 1;
constexpr std::size_t kMaxCatalogueBytes = 1024 * 1024;
constexpr std::size_t kMaxEntries = 500;
constexpr std::size_t kMaxJsonDepth = 32;
constexpr u64 kMaxFileSize = 64ULL * 1024 * 1024; ///< a bigger file is refused
constexpr std::size_t kMaxTextLength = 120;       ///< title, author, licence
constexpr std::size_t kMaxUrlLength = 1024;

struct Entry {
    std::string slug;
    std::string title;
    std::string author;
    std::string licence;
    System system = System::GBA;
    u64 size = 0;
    std::string sha256; ///< lower-case hex
    std::string page;   ///< the author's own page
    std::string url;    ///< the direct download
    std::string filename;
    bool operator==(const Entry&) const = default;
};

struct Catalogue {
    std::vector<Entry> entries;
    std::vector<std::string> rejected; ///< why each left-out entry was left out
    std::string error;                 ///< "" or why the whole list was refused
};

/// Reads a list. Never throws; hostile input gives an empty list and an error, or fewer entries.
Catalogue Parse(std::string_view json);

/// Hosts a download (and every redirect of it) may use: exact lower-case host names, no ports, no
/// wildcards. The list is compiled in: a list file cannot add one.
bool HostAllowed(std::string_view host);

bool ValidSlug(std::string_view slug);
/// A file name that is safe to create inside a folder: no separators, no "..", no control
/// characters, no leading dot, at most 100 bytes, and an extension that fits `system`.
bool ValidFileName(std::string_view name, System system);
/// "https://host/path": https only, a plain lower-case host (no user, no port), no spaces or
/// control characters, no ".." or encoded dots or slashes, no backslash, no fragment. `host`
/// receives the host.
bool ParseHttpsUrl(std::string_view url, std::string* host = nullptr);

std::string_view SystemName(System system); ///< "ds", "gba", "gb", "gbc"
bool ParseSystem(std::string_view name, System* system);

/// The folder (inside `root`) and the file an entry is installed to: root/<slug>/<filename>. Empty
/// when the entry's slug or filename is not safe.
std::string EntryDir(const std::string& root, const Entry& entry);
std::string EntryPath(const std::string& root, const Entry& entry);

/// "" when the file at `path` has exactly the entry's size and SHA-256, else why not (the file is
/// not touched).
std::string VerifyFile(const std::string& path, const Entry& entry);

/// Downloads the entry into `root`/<slug>/ (created), by its url over https from allowed hosts only,
/// at most entry.size bytes. The file is checked (size, SHA-256) under a temporary name and only a
/// whole, matching file is moved to its place; else it is deleted. "" on success, else why.
/// Blocking: call it off the GUI thread. `progress(done, total)` returns false to stop.
std::string Install(const std::string& root, const Entry& entry,
                    const std::function<bool(u64, u64)>& progress);

/// Removes the entry's folder inside `root` (the file and any leftovers). "" on success.
std::string Remove(const std::string& root, const Entry& entry);

} // namespace Homebrew::Handheld
