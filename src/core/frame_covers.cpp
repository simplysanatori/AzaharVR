// Copyright 2026 AzaharVR contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include "core/frame_covers.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <utility>
#include <fmt/format.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

#include "common/file_util.h"
#include "common/logging/log.h"

namespace Core::Covers {

namespace {

constexpr std::string_view kHost = "https://raw.githubusercontent.com";

/// The repository of a system's covers.
std::string_view RepositoryOf(Common::GameSystem system) {
    switch (system) {
    case Common::GameSystem::N3DS:
        return "Nintendo_-_Nintendo_3DS";
    case Common::GameSystem::DS:
        return "Nintendo_-_Nintendo_DS";
    case Common::GameSystem::GBA:
        return "Nintendo_-_Game_Boy_Advance";
    case Common::GameSystem::GB:
        return "Nintendo_-_Game_Boy";
    case Common::GameSystem::GBC:
        return "Nintendo_-_Game_Boy_Color";
    default:
        return {};
    }
}

/// Every byte but letters, digits and -._~ as %XX.
std::string Encoded(std::string_view text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '.' || c == '_' || c == '~') {
            out += ch;
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0xF];
        }
    }
    return out;
}

} // namespace

std::string ThumbnailName(std::string_view name) {
    std::string out(name);
    for (char& c : out) {
        if (std::string_view("&*/:`<>?\\|\"").find(c) != std::string_view::npos) {
            c = '_';
        }
    }
    return out;
}

std::optional<Source> SourceFor(Common::GameSystem system, std::string_view file_stem, Art art) {
    const std::string_view repository = RepositoryOf(system);
    const std::string name = ThumbnailName(file_stem);
    // Every No-Intro name ends with a "(USA)" tag: a name without one cannot match, so it is not
    // sent either
    if (repository.empty() || !name.ends_with(')') || name.find(" (") == std::string::npos) {
        return std::nullopt;
    }
    const char* folder = art == Art::Title  ? "Named_Titles"
                         : art == Art::Snap ? "Named_Snaps"
                                            : "Named_Boxarts";
    return Source{std::string(kHost), "/libretro-thumbnails/" + std::string(repository) +
                                          "/master/" + folder + "/" + Encoded(name + ".png")};
}

Image ImageOf(std::string_view bytes) {
    if (bytes.starts_with("\x89PNG\r\n\x1a\n")) {
        return Image::Png;
    }
    if (bytes.starts_with("\xFF\xD8\xFF")) {
        return Image::Jpeg;
    }
    return Image::None;
}

Cheats::Online::RawReply HttpsGet(const std::string& host, const std::string& path,
                                  std::size_t max_bytes, Mods::Transfer::Stopper* stopper) {
    return Cheats::Online::HttpsGetRaw(host, path, max_bytes, stopper);
}

Fetched Fetch(const Get& get, const Source& source) {
    Fetched out;
    if (!source.host.starts_with("https://")) {
        out.why = "not https";
        return out;
    }
    const auto reply = get(source.host, source.path, kMaxBytes);
    if (reply.status == 404) {
        out.result = Fetched::Result::NotThere;
    } else if (!reply.error.empty() || reply.status != 200) {
        out.why = !reply.error.empty() ? reply.error : "status " + std::to_string(reply.status);
        out.stop = (reply.status == 0 && reply.error != "Download too large") ||
                   reply.status == 403 || reply.status == 429;
    } else if (reply.body.size() > kMaxBytes) {
        out.why = "too large";
    } else if (ImageOf(reply.body) == Image::None) {
        out.why = "not an image";
    } else {
        out.result = Fetched::Result::Got;
        out.bytes = reply.body;
    }
    return out;
}

bool NetworkUp() {
#ifdef _WIN32
    // wininet's own answer, without linking it
    const HMODULE wininet = LoadLibraryW(L"wininet.dll");
    if (wininet == nullptr) {
        return true;
    }
    using Connected = BOOL(WINAPI*)(LPDWORD, DWORD);
    const auto connected = reinterpret_cast<Connected>(
        reinterpret_cast<void*>(GetProcAddress(wininet, "InternetGetConnectedState")));
    DWORD flags = 0;
    const bool up = connected == nullptr || connected(&flags, 0) != FALSE;
    FreeLibrary(wininet);
    return up;
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) {
        return true;
    }
    bool up = false;
    for (const ifaddrs* i = list; i != nullptr && !up; i = i->ifa_next) {
        if (i->ifa_addr == nullptr || (i->ifa_flags & IFF_LOOPBACK) != 0 ||
            (i->ifa_flags & IFF_RUNNING) == 0) {
            continue;
        }
        if (i->ifa_addr->sa_family == AF_INET) {
            up = true;
        } else if (i->ifa_addr->sa_family == AF_INET6) {
            up = !IN6_IS_ADDR_LINKLOCAL(
                &reinterpret_cast<const sockaddr_in6*>(i->ifa_addr)->sin6_addr);
        }
    }
    freeifaddrs(list);
    return up;
#endif
}

// ---- The covers folder ----

namespace {

constexpr const char* kLibretro = "libretro-thumbnails/";

/// A system's folder.
const char* FolderOf(Common::GameSystem system) {
    switch (system) {
    case Common::GameSystem::DS:
        return "ds/";
    case Common::GameSystem::GBA:
        return "gba/";
    case Common::GameSystem::GB:
        return "gb/";
    case Common::GameSystem::GBC:
        return "gbc/";
    default:
        return "3ds/";
    }
}

constexpr std::array<std::pair<Choice, const char*>, 4> kChoiceNames{{
    {Choice::None, "none"},
    {Choice::Own, "own"},
    {Choice::Title, "title"},
    {Choice::Snap, "snap"},
}};

std::string MissingKey(const Game& game, Art art) {
    const auto source = SourceFor(game.system, game.stem, art);
    return source ? source->path : std::string{};
}

/// A whole file or none: written beside it, then renamed.
bool WriteFile(const std::string& path, const std::string& bytes) {
    const std::string part = path + ".part";
    if (!FileUtil::CreateFullPath(path) ||
        FileUtil::WriteStringToFile(false, part, bytes) != bytes.size()) {
        FileUtil::Delete(part);
        return false;
    }
#ifdef _WIN32
    FileUtil::Delete(path); // it renames over nothing
#endif
    if (!FileUtil::Rename(part, path)) {
        FileUtil::Delete(part);
        return false;
    }
    return true;
}

template <typename Line>
void EachLine(const std::string& path, Line&& line) {
    std::ifstream in(path);
    std::string text;
    while (std::getline(in, text)) {
        if (!text.empty()) {
            line(text);
        }
    }
}

} // namespace

std::string Game::Id() const {
    std::uint32_t hash = 2166136261u; // FNV-1a
    for (const char c : stem) {
        hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
    }
    return fmt::format("{}{:016x}-{:08x}", FolderOf(system), key, hash);
}

Library::Library(std::string dir_) : dir{std::move(dir_)} {
    EachLine(dir + "choices.txt", [this](const std::string& line) {
        const std::size_t tab = line.find('\t');
        for (const auto& [choice, name] : kChoiceNames) {
            if (tab != std::string::npos && line.substr(0, tab) == name) {
                choices[line.substr(tab + 1)] = choice;
            }
        }
    });
    EachLine(dir + kLibretro + "missing.txt", [this](const std::string& line) {
        const std::size_t tab = line.find('\t');
        if (tab != std::string::npos) {
            missing[line.substr(tab + 1)] =
                static_cast<std::time_t>(std::strtoll(line.c_str(), nullptr, 10));
        }
    });
    has_known = FileUtil::Exists(dir + "known.txt");
    EachLine(dir + "known.txt", [this](const std::string& line) { known.insert(line); });
}

std::string Library::FileOf(const Game& game, Art art) const {
    const char* name = art == Art::Title ? "-title.png" : art == Art::Snap ? "-snap.png" : "-box.png";
    return dir + kLibretro + game.Id() + name;
}

std::string Library::OwnOf(const Game& game) const {
    return dir + "own/" + game.Id() + ".png";
}

std::string Library::Shown(const Game& game) const {
    if (game.key == 0) {
        return {};
    }
    std::string file;
    switch (ChoiceOf(game)) {
    case Choice::None:
        return {};
    case Choice::Own:
        file = OwnOf(game);
        break;
    case Choice::Title:
        file = FileOf(game, Art::Title);
        break;
    case Choice::Snap:
        file = FileOf(game, Art::Snap);
        break;
    default:
        file = FileOf(game, Art::Box);
        break;
    }
    return FileUtil::Exists(file) ? file : std::string{};
}

Choice Library::ChoiceOf(const Game& game) const {
    std::scoped_lock lock{mutex};
    const auto it = choices.find(game.Id());
    return it != choices.end() ? it->second : Choice::Automatic;
}

void Library::Choose(const Game& game, Choice choice) {
    if (game.key == 0) {
        return;
    }
    std::scoped_lock lock{mutex};
    if (choice == Choice::Automatic) {
        choices.erase(game.Id());
    } else {
        choices[game.Id()] = choice;
    }
    if (choice != Choice::Own) {
        FileUtil::Delete(OwnOf(game));
    }
    SaveChoices();
}

std::string Library::SetOwn(const Game& game, const std::string& bytes) {
    if (bytes.size() > kMaxOwnBytes) {
        return "too large";
    }
    if (game.key == 0 || ImageOf(bytes) == Image::None) {
        return "not an image";
    }
    if (!WriteFile(OwnOf(game), bytes)) {
        return "not written";
    }
    Choose(game, Choice::Own);
    return {};
}

bool Library::Has(const Game& game, Art art) const {
    return game.key != 0 && FileUtil::Exists(FileOf(game, art));
}

bool Library::Wanted(const Game& game, std::time_t now) const {
    return game.key != 0 && ChoiceOf(game) == Choice::Automatic && !Has(game, Art::Box) &&
           SourceFor(game.system, game.stem) && !KnownMissing(game, Art::Box, now);
}

bool Library::KnownMissing(const Game& game, Art art, std::time_t now) const {
    std::scoped_lock lock{mutex};
    const auto it = missing.find(MissingKey(game, art));
    return it != missing.end() && now - it->second < kRetryMissing;
}

bool Library::Store(const Game& game, Art art, const std::string& bytes) {
    if (game.key == 0 || !WriteFile(FileOf(game, art), bytes)) {
        return false;
    }
    std::scoped_lock lock{mutex};
    if (missing.erase(MissingKey(game, art)) > 0) {
        SaveMissing();
    }
    return true;
}

void Library::MarkMissing(const Game& game, Art art, std::time_t now) {
    std::scoped_lock lock{mutex};
    missing[MissingKey(game, art)] = now;
    SaveMissing();
}

bool Library::HasKnown() const {
    std::scoped_lock lock{mutex};
    return has_known;
}

bool Library::Known(const Game& game) const {
    std::scoped_lock lock{mutex};
    return known.contains(game.Id());
}

void Library::AddKnown(const std::vector<Game>& games) {
    std::scoped_lock lock{mutex};
    bool changed = !has_known;
    for (const Game& game : games) {
        changed |= known.insert(game.Id()).second;
    }
    has_known = true;
    if (!changed) {
        return;
    }
    std::string text;
    for (const std::string& id : known) {
        text += id + '\n';
    }
    WriteFile(dir + "known.txt", text);
}

void Library::SaveChoices() const {
    std::string text;
    for (const auto& [id, choice] : choices) {
        for (const auto& [c, name] : kChoiceNames) {
            if (c == choice) {
                text += std::string(name) + '\t' + id + '\n';
            }
        }
    }
    WriteFile(dir + "choices.txt", text);
}

void Library::SaveMissing() const {
    std::string text;
    for (const auto& [path, when] : missing) {
        text += std::to_string(static_cast<long long>(when)) + '\t' + path + '\n';
    }
    WriteFile(dir + kLibretro + "missing.txt", text);
}

Outcome Download(const Get& get, Library& library, const Game& game, Art art, std::time_t now,
                 const std::function<bool(const std::string& bytes)>& is_image) {
    const auto source = SourceFor(game.system, game.stem, art);
    if (!source || game.key == 0 || library.KnownMissing(game, art, now)) {
        return Outcome::NotThere; // not asked for
    }
    if (library.Has(game, art)) {
        return Outcome::Got;
    }
    const Fetched fetched = Fetch(get, *source);
    if (fetched.result == Fetched::Result::Failed) {
        LOG_INFO(Frontend, "Covers: {}: {}", game.stem, fetched.why);
        return fetched.stop ? Outcome::Stop : Outcome::Failed;
    }
    // A PNG's or JPEG's signature, but no image: as if not there
    if (fetched.result == Fetched::Result::NotThere || !is_image(fetched.bytes)) {
        library.MarkMissing(game, art, now);
        return Outcome::NotThere;
    }
    return library.Store(game, art, fetched.bytes) ? Outcome::Got : Outcome::Failed;
}

// ---- "Download Thumbnails?" ----

void NewGames::Scanned(const std::vector<Listed>& games, Library& library, std::time_t now) {
    const bool only_note = !library.HasKnown() ||
                           std::all_of(games.begin(), games.end(), [](const Listed& g) {
                               return g.game.system == Common::GameSystem::N3DS;
                           });
    std::vector<Game> seen;
    std::set<std::string> ids;
    waiting.clear(); // the ones still listed are found again
    for (const Listed& g : games) {
        if (library.Known(g.game) || !ids.insert(g.game.Id()).second) {
            continue;
        }
        if (only_note || !library.Wanted(g.game, now)) {
            seen.push_back(g.game);
        } else {
            waiting.push_back(g);
        }
    }
    library.AddKnown(seen);
}

std::vector<NewGames::Listed> NewGames::Take(Library& library, const Now& now) {
    if (waiting.empty() || now.running || (!now.automatic && !now.online)) {
        return {};
    }
    const std::vector<Listed> out = std::exchange(waiting, {});
    std::vector<Game> games;
    for (const Listed& g : out) {
        games.push_back(g.game);
    }
    library.AddKnown(games);
    return now.automatic ? std::vector<Listed>{} : out;
}

} // namespace Core::Covers
