// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/cheat_db.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>

#include "frame_cores/rom_info.h"

namespace FrameCores::CheatDb {

namespace {

constexpr const char* kRawHost = "https://raw.githubusercontent.com";
constexpr std::string_view kIndexMagic = "azahar-cheat-index 1";
constexpr std::size_t kMaxLine = 1024;
constexpr std::size_t kMaxValue = 64 * 1024;
constexpr std::size_t kMaxWordLength = 24;

bool IsHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool AllHex(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), IsHex);
}

bool IsCommit(std::string_view s) {
    return s.size() == 40 && std::all_of(s.begin(), s.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

char Upper(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

/// The next line of `text` (no newline), moving `text` past it.
std::string_view NextLine(std::string_view& text) {
    const std::size_t nl = text.find('\n');
    const std::string_view line = text.substr(0, nl);
    text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
    return line;
}

std::string PctEncode(std::string_view s) {
    std::string out;
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            c == '-' || c == '.' || c == '_' || c == '~') {
            out += ch;
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

/// A name from the index that can be a file's: no folder part, no control characters.
bool ValidFileName(std::string_view name) {
    if (name.empty() || name.size() > kMaxFileNameLength || name.front() == '.') {
        return false;
    }
    return std::none_of(name.begin(), name.end(), [](char ch) {
        const auto c = static_cast<unsigned char>(ch);
        return c < 0x20 || c == 0x7F || ch == '/' || ch == '\\';
    });
}

/// Lower case letters and digits only: "Pokemon - FireRed (USA)" and "pokemon fire red usa" differ
/// by the spaces only.
std::string Squash(std::string_view s) {
    std::string out;
    for (const char ch : s) {
        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z')) {
            out += ch;
        } else if (ch >= 'A' && ch <= 'Z') {
            out += static_cast<char>(ch - 'A' + 'a');
        } else if (static_cast<unsigned char>(ch) >= 0x80) {
            out += ch;
        }
    }
    return out;
}

std::vector<std::string_view> Words(std::string_view code) {
    std::vector<std::string_view> words;
    std::size_t i = 0;
    while (i < code.size()) {
        while (i < code.size() &&
               (code[i] == '+' || code[i] == ' ' || code[i] == '\t' || code[i] == ',' ||
                code[i] == '\r' || code[i] == '\n')) {
            ++i;
        }
        const std::size_t start = i;
        while (i < code.size() && code[i] != '+' && code[i] != ' ' && code[i] != '\t' &&
               code[i] != ',' && code[i] != '\r' && code[i] != '\n') {
            ++i;
        }
        if (i > start) {
            words.push_back(code.substr(start, i - start));
        }
    }
    return words;
}

std::string UpperWord(std::string_view w) {
    std::string out(w);
    for (char& c : out) {
        c = Upper(c);
    }
    return out;
}

/// A Game Boy Game Genie word: "XXX-XXX" or "XXX-XXX-XXX".
bool IsGameGenie(std::string_view w) {
    if (w.size() != 7 && w.size() != 11) {
        return false;
    }
    for (std::size_t i = 0; i < w.size(); ++i) {
        if (i % 4 == 3 ? w[i] != '-' : !IsHex(w[i])) {
            return false;
        }
    }
    return true;
}

/// The code as the cores read it (see ParseCht); "" when it is not a valid code for the system.
std::string NormalizeCode(std::string_view raw, System system) {
    if (raw.size() > kMaxValue) {
        return {};
    }
    const auto words = Words(raw);
    // The gba-ds loader reads at most 64 words of one code
    const std::size_t max_words = system == System::Nds ? 4096 : system == System::Gba ? 64 : 128;
    if (words.empty() || words.size() > max_words) {
        return {};
    }
    for (const auto w : words) {
        if (w.size() > kMaxWordLength) {
            return {};
        }
    }
    std::string out;
    if (system == System::Gb || system == System::Gbc) {
        for (const auto w : words) {
            if (!(w.size() == 8 && AllHex(w)) && !IsGameGenie(w)) {
                return {};
            }
            out += out.empty() ? "" : "\n";
            out += UpperWord(w);
        }
        return out;
    }
    if (words.size() % 2 != 0) {
        return {};
    }
    for (std::size_t i = 0; i < words.size(); i += 2) {
        const auto a = words[i];
        const auto b = words[i + 1];
        const bool ok = a.size() == 8 && AllHex(a) && AllHex(b) &&
                        (b.size() == 8 || (system == System::Gba && b.size() == 4));
        if (!ok) {
            return {};
        }
        out += out.empty() ? "" : "\n";
        out += UpperWord(a) + ' ' + UpperWord(b);
    }
    return out;
}

/// A cht value: the text inside the quotes (\" and \\ undone), or the rest of the line; control
/// characters become spaces.
std::string Value(std::string_view v) {
    v = Trim(v);
    std::string out;
    if (!v.empty() && v.front() == '"') {
        for (std::size_t i = 1; i < v.size() && out.size() < kMaxValue; ++i) {
            if (v[i] == '"') {
                break;
            }
            if (v[i] == '\\' && i + 1 < v.size()) {
                ++i;
            }
            out += v[i];
        }
    } else {
        out.assign(v.substr(0, kMaxValue));
    }
    for (char& c : out) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
            c = ' ';
        }
    }
    return out;
}

} // namespace

const char* SystemDir(System system) {
    switch (system) {
    case System::Nds:
        return "Nintendo - Nintendo DS";
    case System::Gba:
        return "Nintendo - Game Boy Advance";
    case System::Gb:
        return "Nintendo - Game Boy";
    case System::Gbc:
        break;
    }
    return "Nintendo - Game Boy Color";
}

const char* SystemTag(System system) {
    switch (system) {
    case System::Nds:
        return "nds";
    case System::Gba:
        return "gba";
    case System::Gb:
        return "gb";
    case System::Gbc:
        break;
    }
    return "gbc";
}

std::optional<System> SystemFromTag(std::string_view tag) {
    for (const System s : {System::Nds, System::Gba, System::Gb, System::Gbc}) {
        if (tag == SystemTag(s)) {
            return s;
        }
    }
    return std::nullopt;
}

bool SystemHasCode(System system) {
    return system == System::Nds || system == System::Gba;
}

IndexParse ParseIndex(std::string_view text) {
    IndexParse out;
    if (text.size() > kMaxIndexBytes) {
        out.error = "Reply too large";
        return out;
    }
    int stage = 0; // 0: magic, 1: libretro, 2: entries
    while (!text.empty()) {
        std::string_view line = NextLine(text);
        if (line.size() > kMaxLine) {
            out.error = "Not a cheat index";
            return out;
        }
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (stage == 0) {
            if (line != kIndexMagic) {
                out.error = "Not a cheat index";
                return out;
            }
            stage = 1;
            continue;
        }
        if (stage == 1) {
            constexpr std::string_view prefix = "libretro ";
            if (line.substr(0, prefix.size()) != prefix || !IsCommit(line.substr(prefix.size()))) {
                out.error = "Not a cheat index";
                return out;
            }
            out.index.libretro_commit = std::string(line.substr(prefix.size()));
            stage = 2;
            continue;
        }
        // <tag> TAB <code or -> TAB <crc> TAB <name>
        std::string_view f[4];
        std::string_view rest = line;
        bool ok = true;
        for (int i = 0; i < 3; ++i) {
            const std::size_t tab = rest.find('\t');
            if (tab == std::string_view::npos) {
                ok = false;
                break;
            }
            f[i] = rest.substr(0, tab);
            rest.remove_prefix(tab + 1);
        }
        f[3] = rest;
        const auto system = ok ? SystemFromTag(f[0]) : std::nullopt;
        const bool code_ok =
            f[1] == "-" || (f[1].size() == 4 &&
                            std::all_of(f[1].begin(), f[1].end(), [](char c) {
                                return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                                       (c >= 'a' && c <= 'z');
                            }));
        if (!ok || !system || !code_ok || f[2].size() != 8 || !AllHex(f[2]) ||
            !ValidFileName(f[3])) {
            ++out.skipped;
            continue;
        }
        if (out.index.entries.size() >= kMaxIndexEntries) {
            out.error = "Not a cheat index";
            out.index.entries.clear();
            return out;
        }
        IndexEntry e;
        e.system = *system;
        e.code = f[1] == "-" ? std::string{} : std::string(f[1]);
        e.crc = static_cast<std::uint32_t>(std::stoul(std::string(f[2]), nullptr, 16));
        e.name = std::string(f[3]);
        out.index.entries.push_back(std::move(e));
    }
    if (stage < 2) {
        out.error = "Not a cheat index";
    }
    return out;
}

Match FindMatch(const Index& index, System system, std::string_view game_code,
                std::optional<std::uint32_t> crc, std::string_view rom_stem) {
    const bool want_code = SystemHasCode(system);
    const auto code_fits = [&](const IndexEntry& e) {
        return !want_code || e.code.empty() || game_code.empty() || e.code == game_code;
    };
    if (crc) {
        for (const auto& e : index.entries) {
            // The code and the CRC32 both, where the game has a code
            if (e.system == system && e.crc == *crc &&
                (!want_code || e.code.empty() || e.code == game_code)) {
                return {&e, false};
            }
        }
    }
    const std::string stem = Squash(rom_stem);
    if (!stem.empty()) {
        for (const auto& e : index.entries) {
            if (e.system == system && code_fits(e) && Squash(e.name) == stem) {
                return {&e, true};
            }
        }
    }
    return {};
}

std::string IndexUrlPath(std::string_view commit) {
    return "/simplysanatori/ds-gba-game-fixes/" + std::string(commit) +
           "/cheats/libretro-index.tsv";
}

std::string ChtUrlPath(std::string_view commit, System system, std::string_view file_name) {
    return "/libretro/libretro-database/" + std::string(commit) + "/cht/" +
           PctEncode(SystemDir(system)) + "/" + PctEncode(file_name) + ".cht";
}

ChtParse ParseCht(std::string_view text, System system) {
    ChtParse out;
    if (text.size() > kMaxChtBytes) {
        out.truncated = true;
        return out;
    }
    struct Slot {
        std::string desc;
        std::string code;
        bool has_code = false;
    };
    std::map<int, Slot> slots;
    while (!text.empty()) {
        const std::string_view line = Trim(NextLine(text));
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos || line.substr(0, 5) != "cheat") {
            continue;
        }
        const std::string_view key = Trim(line.substr(0, eq));
        std::size_t i = 5, n = 0, digits = 0;
        while (i < key.size() && key[i] >= '0' && key[i] <= '9' && digits < 5) {
            n = n * 10 + static_cast<std::size_t>(key[i] - '0');
            ++i;
            ++digits;
        }
        const std::string_view tail = key.substr(i);
        if (digits == 0 || digits >= 5 || (tail != "_desc" && tail != "_code")) {
            continue;
        }
        if (n >= static_cast<std::size_t>(kMaxCheats)) {
            out.truncated = true;
            continue;
        }
        Slot& slot = slots[static_cast<int>(n)];
        const std::string value = Value(line.substr(eq + 1));
        if (tail == "_desc") {
            slot.desc = value.substr(0, kMaxNameLength);
        } else {
            slot.code = value;
            slot.has_code = true;
        }
    }
    for (const auto& [n, slot] : slots) {
        if (!slot.has_code || Trim(slot.code).empty()) {
            continue; // a heading
        }
        std::string code = NormalizeCode(slot.code, system);
        if (code.empty()) {
            ++out.skipped;
            continue;
        }
        CheatEntry entry;
        entry.name = std::string(Trim(slot.desc));
        if (entry.name.empty()) {
            entry.name = "Custom Cheat";
        }
        entry.code = std::move(code);
        entry.enabled = false;
        out.cheats.push_back(std::move(entry));
    }
    return out;
}

int MergeImported(std::vector<CheatEntry>& list, const std::vector<CheatEntry>& incoming) {
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

Result Download(const Cheats::Online::HttpGet& get, System system, std::string_view game_code,
                std::optional<std::uint32_t> crc, std::string_view rom_stem, const Pins& pins) {
    using Cheats::Online::HttpResult;
    Result out;
    if (!IsCommit(pins.fixes_commit) || !IsCommit(pins.libretro_commit)) {
        out.error = "No file list";
        return out;
    }
    const HttpResult idx = get(kRawHost, IndexUrlPath(pins.fixes_commit));
    if (idx.status != 200) {
        out.error = idx.error.empty() ? "No file list" : idx.error;
        return out;
    }
    const IndexParse parsed = ParseIndex(idx.body);
    if (!parsed.error.empty() || parsed.index.libretro_commit != pins.libretro_commit) {
        out.error = "No file list";
        return out;
    }
    const Match match = FindMatch(parsed.index, system, game_code, crc, rom_stem);
    if (!match.entry) {
        return out;
    }
    const HttpResult file = get(kRawHost, ChtUrlPath(pins.libretro_commit, system, match.entry->name));
    if (file.status != 200 || file.body.size() > kMaxChtBytes) {
        out.error = file.error.empty() ? "No reply" : file.error;
        return out;
    }
    ChtParse cht = ParseCht(file.body, system);
    out.found = true;
    out.by_name = match.by_name;
    out.skipped = cht.skipped;
    out.cheats = std::move(cht.cheats);
    return out;
}

std::optional<std::uint32_t> RomCrc32(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> buf(1 << 20);
    in.read(reinterpret_cast<char*>(buf.data()), 4);
    if (in.gcount() < 4) {
        return std::nullopt;
    }
    if (buf[0] == 'P' && buf[1] == 'K' && buf[2] == 3 && buf[3] == 4) {
        std::vector<std::uint8_t> rom;
        if (!ExtractRomFromZip(path, rom, nullptr, kMaxRomBytes + 1) || rom.empty() ||
            rom.size() > kMaxRomBytes) {
            return std::nullopt;
        }
        return ComputeCrc32(rom.data(), rom.size());
    }
    in.clear();
    in.seekg(0);
    std::uint32_t crc = 0;
    std::uint64_t total = 0;
    while (in) {
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        const auto got = static_cast<std::size_t>(in.gcount());
        if (got == 0) {
            break;
        }
        total += got;
        if (total > kMaxRomBytes) {
            return std::nullopt;
        }
        crc = ComputeCrc32(buf.data(), got, crc);
    }
    return total == 0 ? std::nullopt : std::optional<std::uint32_t>(crc);
}

} // namespace FrameCores::CheatDb
