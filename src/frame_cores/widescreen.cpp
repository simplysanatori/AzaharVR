// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/widescreen.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>

#include "common/logging/log.h"
#include "core/homebrew/net.h"

namespace FrameCores::Widescreen {

namespace {

constexpr const char* kRawHost = "https://raw.githubusercontent.com";
constexpr std::string_view kMagic = "azahar-widescreen-index 1";
constexpr std::string_view kCommit = "twilight ";

bool AllOf(std::string_view s, std::string_view digits) {
    return s.find_first_not_of(digits) == std::string_view::npos;
}

bool LowerHex(std::string_view s, std::size_t size) {
    return s.size() == size && AllOf(s, "0123456789abcdef");
}

template <typename T>
std::optional<T> Number(std::string_view s, int base) {
    T value{};
    const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value, base);
    if (s.empty() || error != std::errc{} || end != s.data() + s.size()) {
        return std::nullopt;
    }
    return value;
}

/// "<code>-<1 to 4 upper-case hex>.bin", and no other: it is a part of a path and of an address.
std::optional<std::uint16_t> CrcOfName(std::string_view file, std::string_view code) {
    constexpr std::string_view kEnd = ".bin";
    if (file.size() < code.size() + 2 + kEnd.size() || file.substr(0, code.size()) != code ||
        file[code.size()] != '-' || file.substr(file.size() - kEnd.size()) != kEnd) {
        return std::nullopt;
    }
    file.remove_prefix(code.size() + 1);
    file.remove_suffix(kEnd.size());
    if (file.size() > 4 || !AllOf(file, "0123456789ABCDEF")) {
        return std::nullopt;
    }
    return Number<std::uint16_t>(file, 16);
}

std::optional<Entry> ParseEntry(std::string_view line) {
    std::string_view f[7];
    for (std::size_t i = 0; i < std::size(f); ++i) {
        const std::size_t tab = i + 1 < std::size(f) ? line.find('\t') : line.size();
        if (tab == std::string_view::npos) {
            return std::nullopt;
        }
        f[i] = line.substr(0, tab);
        line.remove_prefix(std::min(tab + 1, line.size()));
    }
    const auto crc = Number<std::uint16_t>(f[1], 16);
    const auto size = Number<std::uint32_t>(f[3], 10);
    if (f[0].size() != 4 || !AllOf(f[0], "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ") ||
        f[1].size() != 4 || !crc || CrcOfName(f[2], f[0]) != crc || !size ||
        *size > kMaxFileBytes || *size % 8 != 0 || !LowerHex(f[4], 40) || !LowerHex(f[5], 64) ||
        f[6].find('\t') != std::string_view::npos) {
        return std::nullopt;
    }
    return Entry{std::string(f[0]), *crc, std::string(f[2]), *size, std::string(f[5])};
}

/// The hack while it is on; off it has ".off" after that.
std::filesystem::path PathOf(const std::string& game_dir, const Entry& entry) {
    return std::filesystem::path(game_dir) / "mods" / entry.file;
}

std::filesystem::path OffPathOf(const std::string& game_dir, const Entry& entry) {
    return PathOf(game_dir, entry) += ".off";
}

bool IsFile(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

} // namespace

IndexParse ParseIndex(std::string_view text) {
    IndexParse out;
    if (text.size() > kMaxIndexBytes) {
        out.error = "Too large";
        return out;
    }
    int stage = 0; // 0: the magic, 1: the commit, 2: entries
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        const std::string_view line = text.substr(0, nl);
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (stage == 0 && line == kMagic) {
            stage = 1;
        } else if (stage == 1 && line.substr(0, kCommit.size()) == kCommit &&
                   LowerHex(line.substr(kCommit.size()), 40)) {
            out.index.commit = std::string(line.substr(kCommit.size()));
            stage = 2;
        } else if (stage != 2 || out.index.entries.size() >= kMaxEntries) {
            stage = 0;
            break;
        } else if (auto entry = ParseEntry(line)) {
            out.index.entries.push_back(std::move(*entry));
        } else {
            ++out.skipped;
        }
    }
    if (stage != 2) {
        out.index = {};
        out.error = "Not a widescreen index";
    }
    return out;
}

const Index& BuiltIn() {
    static const Index index = ParseIndex(BuiltInIndexText()).index;
    return index;
}

const Entry* Find(const Index& index, std::string_view game_code, std::uint16_t header_crc16) {
    const Entry* any = nullptr;
    for (const Entry& e : index.entries) {
        if (e.code != game_code) {
            continue;
        }
        if (e.crc16 == header_crc16) {
            return e.size > 0 ? &e : nullptr;
        }
        if (e.crc16 == kAnyVersion && e.size > 0) {
            any = &e;
        }
    }
    return any;
}

std::string UrlPath(const Index& index, const Entry& entry) {
    return "/DS-Homebrew/TWiLightMenu/" + index.commit + "/resources/widescreen/" + entry.file;
}

std::optional<std::vector<std::uint32_t>> ReadCodes(std::span<const std::uint8_t> file) {
    if (file.empty() || file.size() % 8 != 0 || file.size() > kMaxFileBytes) {
        return std::nullopt;
    }
    std::vector<std::uint32_t> words(file.size() / 4);
    for (std::size_t i = 0; i < words.size(); ++i) {
        words[i] = file[i * 4] | file[i * 4 + 1] << 8 | file[i * 4 + 2] << 16 |
                   static_cast<std::uint32_t>(file[i * 4 + 3]) << 24;
    }
    for (std::size_t i = 0; i < words.size();) {
        const std::uint32_t a = words[i], b = words[i + 1];
        i += 2;
        switch (a >> 28) {
        case 0x0: // writes of 32, 16 and 8 bits
        case 0x1:
        case 0x2:
        case 0x5: // if equal, if not, if equal on 16 bits
        case 0x6:
        case 0x9:
        case 0xB: // an offset from memory
            continue;
        case 0xD: // end if, end all, add to the offset
            if (const std::uint32_t op = a >> 24; op == 0xD0 || op == 0xD2 || op == 0xDC) {
                continue;
            }
            return std::nullopt;
        case 0xE: // a copy of what follows
            if (b > kMaxFileBytes || (b + 7) / 8 * 2 > words.size() - i) {
                return std::nullopt;
            }
            i += (b + 7) / 8 * 2;
            continue;
        default:
            return std::nullopt;
        }
    }
    return words;
}

std::string Verify(const Entry& entry, std::string_view file) {
    if (file.size() != entry.size) {
        return "Wrong size";
    }
    if (Homebrew::Sha256Hex(file) != entry.sha256) {
        return "Wrong checksum";
    }
    if (!ReadCodes({reinterpret_cast<const std::uint8_t*>(file.data()), file.size()})) {
        return "Not codes";
    }
    return {};
}

State StateOf(const std::string& game_dir, const Entry& entry) {
    return IsFile(PathOf(game_dir, entry))      ? State::On
           : IsFile(OffPathOf(game_dir, entry)) ? State::Off
                                                : State::None;
}

bool SetOn(const std::string& game_dir, const Entry& entry, bool on) {
    const State now = StateOf(game_dir, entry);
    if (now == State::None || (now == State::On) == on) {
        return now != State::None;
    }
    std::error_code ec;
    const auto at = PathOf(game_dir, entry), off = OffPathOf(game_dir, entry);
    std::filesystem::rename(on ? off : at, on ? at : off, ec);
    return !ec;
}

bool Remove(const std::string& game_dir, const Entry& entry) {
    std::error_code ec, ec2;
    std::filesystem::remove(PathOf(game_dir, entry), ec);
    std::filesystem::remove(OffPathOf(game_dir, entry), ec2);
    return StateOf(game_dir, entry) == State::None;
}

std::string Install(const Cheats::Online::HttpGet& get, const Index& index, const Entry& entry,
                    const std::string& game_dir) {
    if (entry.size == 0 || !LowerHex(index.commit, 40)) {
        return "Not available";
    }
    const auto reply = get(kRawHost, UrlPath(index, entry));
    if (reply.status != 200) { // a redirect too
        return reply.error.empty() ? "No reply" : reply.error;
    }
    if (std::string why = Verify(entry, reply.body); !why.empty()) {
        LOG_WARNING(Frontend, "widescreen: {}: {}", entry.file, why);
        return why;
    }
    // Under another name until it is whole
    std::error_code ec;
    const auto path = PathOf(game_dir, entry);
    const auto part = std::filesystem::path(path) += ".part";
    std::filesystem::create_directories(path.parent_path(), ec);
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        out.write(reply.body.data(), static_cast<std::streamsize>(reply.body.size()));
        out.flush();
        if (!out) {
            ec = std::make_error_code(std::errc::io_error);
        }
    }
    if (!ec) {
        std::filesystem::rename(part, path, ec);
    }
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(part, ignored);
        return "Cannot write the file";
    }
    std::filesystem::remove(OffPathOf(game_dir, entry), ec);
    return {};
}

std::vector<std::uint32_t> CodesFor(const std::string& game_dir, const Entry& entry) {
    if (StateOf(game_dir, entry) != State::On) {
        return {};
    }
    std::ifstream in(PathOf(game_dir, entry), std::ios::binary);
    std::vector<std::uint8_t> file(kMaxFileBytes + 1);
    in.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(file.size()));
    file.resize(static_cast<std::size_t>(in.gcount()));
    auto codes = ReadCodes(file);
    if (!codes) {
        LOG_ERROR(Core, "widescreen: {} is not a hack, left out", entry.file);
        return {};
    }
    return std::move(*codes);
}

} // namespace FrameCores::Widescreen
