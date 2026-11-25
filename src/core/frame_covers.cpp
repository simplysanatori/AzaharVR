// Copyright 2026 AzaharVR contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include "core/frame_covers.h"

#include <array>
#include <fstream>
#include <sstream>
#include <utility>

#include "common/file_util.h"
#include "common/logging/log.h"

namespace Core::Covers {

namespace {

constexpr std::string_view kHost = "https://raw.githubusercontent.com";

/// The repository of a system's covers.
std::string_view RepositoryOf(Common::GameSystem system) {
    switch (system) {
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

std::optional<Source> SourceFor(Common::GameSystem system, std::string_view file_stem) {
    const std::string_view repository = RepositoryOf(system);
    const std::string name = ThumbnailName(file_stem);
    // Every No-Intro name ends with a "(USA)" tag: a name without one cannot match, so it is not
    // sent either
    if (repository.empty() || !name.ends_with(')') || name.find(" (") == std::string::npos) {
        return std::nullopt;
    }
    Source s;
    s.host = std::string(kHost);
    s.path = "/libretro-thumbnails/" + std::string(repository) + "/master/Named_Boxarts/" +
             Encoded(name + ".png");
    s.key = std::string(repository) + "/" + name + ".png";
    return s;
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

Cache::Cache(std::string dir_) : dir{std::move(dir_)} {
    LoadMissing();
}

std::string Cache::FileOf(const Source& source) const {
    const std::string path = dir + source.key;
    return FileUtil::Exists(path) ? path : std::string{};
}

bool Cache::KnownMissing(const Source& source, std::time_t now) const {
    const auto it = missing.find(source.key);
    return it != missing.end() && now - it->second < kRetryMissing;
}

bool Cache::Store(const Source& source, const std::string& bytes) {
    const std::string path = dir + source.key;
    const std::string part = path + ".part";
    if (!FileUtil::CreateFullPath(path) ||
        FileUtil::WriteStringToFile(false, part, bytes) != bytes.size() ||
        !FileUtil::Rename(part, path)) {
        FileUtil::Delete(part);
        return false;
    }
    if (missing.erase(source.key) > 0) {
        SaveMissing();
    }
    return true;
}

void Cache::MarkMissing(const Source& source, std::time_t now) {
    missing[source.key] = now;
    SaveMissing();
}

void Cache::LoadMissing() {
    std::ifstream in(dir + "missing.txt");
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        try {
            missing[line.substr(tab + 1)] = static_cast<std::time_t>(std::stoll(line.substr(0, tab)));
        } catch (const std::exception&) {
        }
    }
}

void Cache::SaveMissing() const {
    if (!FileUtil::CreateFullPath(dir)) {
        return;
    }
    std::ostringstream text;
    for (const auto& [key, when] : missing) {
        text << static_cast<long long>(when) << '\t' << key << '\n';
    }
    FileUtil::WriteStringToFile(true, dir + "missing.txt", text.str());
}

} // namespace Core::Covers
