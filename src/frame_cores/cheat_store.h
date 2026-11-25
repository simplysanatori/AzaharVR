// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "frame_cores/core_backend.h"
#include "frame_cores/utf8_path.h"

namespace FrameCores {

// Custom cheats a player typed in, kept beside the save: one per line,
// "<0|1><TAB><name><TAB><code>", with backslash, tab and newline escaped.

inline std::string EscapeCheatField(const std::string& in) {
    std::string out;
    for (const char c : in) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            break;
        default:
            out += c;
        }
    }
    return out;
}

inline std::string UnescapeCheatField(const std::string& in) {
    std::string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 1 < in.size()) {
            const char n = in[++i];
            out += n == 't' ? '\t' : n == 'n' ? '\n' : n;
        } else {
            out += in[i];
        }
    }
    return out;
}

/// Writes the list (an empty one removes the file). True when the file is as asked.
inline bool SaveCustomCheats(const std::string& path, const std::vector<CheatEntry>& list) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    if (list.empty()) {
        std::filesystem::remove(FsPath(path), ec);
        return true;
    }
    std::filesystem::create_directories(FsPath(path).parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(FsPath(tmp), std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << "# custom cheats: on(1)/off(0) TAB name TAB code\n";
        for (const auto& c : list) {
            out << (c.enabled ? '1' : '0') << '\t' << EscapeCheatField(c.name) << '\t'
                << EscapeCheatField(c.code) << '\n';
        }
        if (!out.good()) {
            return false;
        }
    }
    std::filesystem::rename(FsPath(tmp), FsPath(path), ec);
    if (ec) {
        std::filesystem::remove(FsPath(tmp), ec);
        return false;
    }
    return true;
}

inline std::vector<CheatEntry> LoadCustomCheats(const std::string& path) {
    std::vector<CheatEntry> list;
    std::ifstream in(FsPath(path), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() < 4 || line[0] == '#' || (line[0] != '0' && line[0] != '1') ||
            line[1] != '\t') {
            continue;
        }
        const auto tab = line.find('\t', 2);
        if (tab == std::string::npos) {
            continue;
        }
        CheatEntry entry;
        entry.enabled = line[0] == '1';
        entry.name = UnescapeCheatField(line.substr(2, tab - 2));
        entry.code = UnescapeCheatField(line.substr(tab + 1));
        if (!entry.code.empty()) {
            list.push_back(std::move(entry));
        }
    }
    return list;
}

/// The code split into the separate lines a cheat engine takes (newline, space, comma, plus).
inline std::vector<std::string> SplitCheatCode(const std::string& code) {
    std::vector<std::string> lines;
    std::string cur;
    for (const char c : code) {
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t' || c == ',' || c == '+') {
            if (!cur.empty()) {
                lines.push_back(std::move(cur));
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        lines.push_back(std::move(cur));
    }
    return lines;
}

} // namespace FrameCores
