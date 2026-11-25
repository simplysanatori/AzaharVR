// Copyright 2026 AzaharVR contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// The covers of the VR menu's Games tab. Nothing is bundled, and nothing is asked for before the
// player presses Download (in "Download Thumbnails?") or turns Download Game Covers on (off by
// default). Covers on disk show whatever the setting says.
//
// The one source: the libretro-thumbnails repositories on GitHub, whose files are named exactly as
// No-Intro names the game. A game is looked up by its file's name only, so a ROM hack (named
// otherwise) never gets its base game's cover. Another source would be another SourceFor and
// another folder beside kLibretro's.
//
// The covers folder, one file per game (Game::Id: the per-game key and the file's name):
//   libretro-thumbnails/<system>/<id>-<art>.png   downloaded
//   libretro-thumbnails/missing.txt               the names not found there, and when
//   own/<system>/<id>.png                         the player's own images
//   choices.txt                                   what the player chose for a game
//   known.txt                                     the games the list had, by id (NewGames)

#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "common/game_system.h"
#include "core/cheats/online.h"

namespace Core::Covers {

constexpr std::size_t kMaxBytes = 2 * 1024 * 1024; ///< a bigger reply is no cover
constexpr std::size_t kMaxOwnBytes = 8 * 1024 * 1024; ///< nor is a bigger image of the player's
constexpr std::time_t kRetryMissing = 30 * 24 * 3600; ///< a cover not found is asked for again

/// The pictures the source has of a game. Box is the cover shown unless the player chose another.
enum class Art { Box, Title, Snap };

/// What the player chose for a game.
enum class Choice { Automatic, None, Own, Title, Snap };
constexpr Choice ChoiceFor(Art art) {
    return art == Art::Title ? Choice::Title : art == Art::Snap ? Choice::Snap : Choice::Automatic;
}

struct Game {
    Common::GameSystem system = Common::GameSystem::N3DS;
    std::uint64_t key = 0; ///< the title id or the per-game key; 0: it can have no cover
    std::string stem;      ///< the file's name without extension, tags kept
    /// "<system>/<id>". The key alone does not tell a hack from its base game (it covers the
    /// file's start), so the name is part of it.
    std::string Id() const;
    bool operator==(const Game&) const = default;
};

/// Where a picture of a game would be.
struct Source {
    std::string host; ///< "https://..."
    std::string path; ///< percent-encoded
    bool operator==(const Source&) const = default;
};

/// The name as libretro-thumbnails spells it: &*/:`<>?\|" become _.
std::string ThumbnailName(std::string_view name);
/// A picture of a game of `system` whose file is named `file_stem` (without extension, tags kept);
/// none for a name that is no No-Intro name (no "(USA)" tag at its end), which is then never sent.
std::optional<Source> SourceFor(Common::GameSystem system, std::string_view file_stem,
                                Art art = Art::Box);

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

/// Whether a network is there at all (the interfaces only: nothing is sent).
bool NetworkUp();

/// The covers folder. Any thread; nothing here asks the network.
class Library {
public:
    explicit Library(std::string dir);

    /// The file the game's tile shows, by the player's choice ("" none): an own image, no cover,
    /// the picture chosen, else the box art.
    std::string Shown(const Game& game) const;
    Choice ChoiceOf(const Game& game) const;
    /// Reset Cover is Choice::Automatic. The files stay; an own image's goes with it.
    void Choose(const Game& game, Choice choice);
    /// Use My Own Image: its copy, and the choice. "" when done, else why not.
    std::string SetOwn(const Game& game, const std::string& bytes);

    bool Has(const Game& game, Art art) const;
    /// Whether a download could bring the game its cover: none shown, none chosen, a name the
    /// source can have, and not missing there lately.
    bool Wanted(const Game& game, std::time_t now) const;
    bool KnownMissing(const Game& game, Art art, std::time_t now) const;
    bool Store(const Game& game, Art art, const std::string& bytes);
    void MarkMissing(const Game& game, Art art, std::time_t now);

    /// The files the list had (NewGames). No scan was noted yet: HasKnown is false.
    bool HasKnown() const;
    bool Known(const Game& game) const;
    void AddKnown(const std::vector<Game>& games);

private:
    std::string FileOf(const Game& game, Art art) const;
    std::string OwnOf(const Game& game) const;
    void SaveChoices() const;
    void SaveMissing() const;

    std::string dir;
    mutable std::mutex mutex;
    std::map<std::string, Choice> choices;      ///< by Game::Id
    std::map<std::string, std::time_t> missing; ///< by Source::path, when
    std::set<std::string> known; ///< Game::Id
    bool has_known = false;
};

/// What came of one Download.
enum class Outcome { Got, NotThere, Failed, Stop }; ///< Stop: ask for no more
/// Asks the source for one picture of a game and keeps it. `is_image` decodes the reply.
Outcome Download(const Get& get, Library& library, const Game& game, Art art, std::time_t now,
                 const std::function<bool(const std::string& bytes)>& is_image);

/// "Download Thumbnails?" after a scan: once for the files that were not in the list before. A file
/// is known by Game::Id (its per-game key and its name), so a moved file is not new and a renamed
/// one is. The first scan ever only notes what is there.
class NewGames {
public:
    struct Listed {
        std::string path;
        Game game;
    };
    struct Now {
        bool automatic = false; ///< Download Game Covers is on: they come by themselves
        bool running = false;   ///< a game runs
        bool online = true;
        std::time_t time = 0;
    };
    /// A scan is done. The new files that could have a cover wait for Take; the rest is known.
    void Scanned(const std::vector<Listed>& games, Library& library, std::time_t now);
    /// The files to ask about now (none: no question), known from here on. While a game runs, or
    /// without a network, they wait.
    std::vector<Listed> Take(Library& library, const Now& now);

private:
    std::vector<Listed> waiting;
};

} // namespace Core::Covers
