// Copyright 2026 AzaharVR contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// Cover art for the VR menu's Games tab, downloaded on demand when the player turns Download
// Cover Art on (off by default). Nothing is bundled.
//
// The source: the libretro-thumbnails repositories on GitHub (box art named exactly as No-Intro
// names the game), for DS, GBA, Game Boy and Game Boy Color games. A game is looked up by its
// file's name only, so a ROM hack (named otherwise) never gets its base game's cover, and nothing
// else about the player is sent. 3DS games keep their own icons.
//
// Covers are kept in the user folder's cache (covers/), with the names not found there, so each is
// asked for once; a failed connection is tried again at the next start.

#pragma once

#include <cstddef>
#include <ctime>
#include <functional>
#include <optional>
#include <map>
#include <string>
#include <string_view>

#include "common/game_system.h"
#include "core/cheats/online.h"

namespace Core::Covers {

constexpr std::size_t kMaxBytes = 2 * 1024 * 1024; ///< a bigger reply is no cover
constexpr std::time_t kRetryMissing = 30 * 24 * 3600; ///< a cover not found is asked for again

/// Where a game's cover would be.
struct Source {
    std::string host; ///< "https://..."
    std::string path; ///< percent-encoded
    std::string key;  ///< its file in the cache: "<system>/<name>.png"
    bool operator==(const Source&) const = default;
};

/// The name as libretro-thumbnails spells it: &*/:`<>?\|" become _.
std::string ThumbnailName(std::string_view name);
/// The cover of a game of `system` whose file is named `file_stem` (without extension, tags
/// kept); none for 3DS games and for a name that is no No-Intro name (no "(USA)" tag at its end),
/// which is then never sent.
std::optional<Source> SourceFor(Common::GameSystem system, std::string_view file_stem);

enum class Image { None, Png, Jpeg };
/// What the bytes are, by their signature.
Image ImageOf(std::string_view bytes);

/// One HTTPS GET of `host` `path`, the reply capped at `max_bytes`.
using Get = std::function<Cheats::Online::RawReply(const std::string& host, const std::string& path,
                                                   std::size_t max_bytes)>;
Cheats::Online::RawReply HttpsGet(const std::string& host, const std::string& path,
                                  std::size_t max_bytes, Mods::Transfer::Stopper* stopper);

struct Fetched {
    enum class Result { Got, NotThere, Failed } result = Result::Failed;
    std::string bytes; ///< Got: a PNG or JPEG of at most kMaxBytes
    std::string why;   ///< Failed: for the log
    bool stop = false; ///< Failed: no reply or a limit, so ask for no more covers this run
};
/// Asks for the cover (an https source only) and checks the reply.
Fetched Fetch(const Get& get, const Source& source);

/// The cache folder: the covers found and the names not found.
class Cache {
public:
    explicit Cache(std::string dir);
    /// The cover's file when it is there ("" not).
    std::string FileOf(const Source& source) const;
    /// It was not found less than kRetryMissing ago.
    bool KnownMissing(const Source& source, std::time_t now) const;
    /// Keeps a cover found; false when it could not be written.
    bool Store(const Source& source, const std::string& bytes);
    void MarkMissing(const Source& source, std::time_t now);

private:
    void LoadMissing();
    void SaveMissing() const;
    std::string dir;
    std::map<std::string, std::time_t> missing; ///< key, when
};

} // namespace Core::Covers
