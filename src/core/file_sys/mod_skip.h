// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharFrame kiu132: files of a game's mods folder (<LoadDir>/mods/<TID>/) the loader leaves out
// of one run, as if they were not there: a Kid Icarus: Uprising C-Stick mod in a run that starts
// in Angel's Aim (frame_xr/kiu_first_person.h LeaveOutCStickModFor). The frontend sets them
// before the game loads and clears them before the next one; the mod library's own state
// (which mods are on) is never touched. Read by ncch_container.cpp (exheader.bin, the ExeFS
// files, the code patches) and layered_fs.cpp (romfs/ and romfs_ext/ files).

#pragma once

#include <algorithm>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace FileSys::ModSkip {

namespace Detail {
inline std::mutex g_mutex;
inline std::string g_dir;           ///< the mods folder, '/' separators, ending in '/'
inline std::set<std::string> g_rel; ///< files relative to it, '/' separators

inline std::string Normal(std::string_view path) {
    std::string s(path);
    std::replace(s.begin(), s.end(), '\\', '/');
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        if (c == '/' && !out.empty() && out.back() == '/') {
            continue; // "a//b" is "a/b"
        }
        out += c;
    }
    return out;
}
} // namespace Detail

/// Leaves out these files (relative to `mods_dir`) from now on, in place of any before.
inline void Set(std::string_view mods_dir, const std::vector<std::string>& files) {
    std::scoped_lock lock{Detail::g_mutex};
    Detail::g_dir = Detail::Normal(mods_dir);
    if (!Detail::g_dir.empty() && Detail::g_dir.back() != '/') {
        Detail::g_dir += '/';
    }
    Detail::g_rel.clear();
    for (const std::string& f : files) {
        std::string rel = Detail::Normal(f);
        while (!rel.empty() && rel.front() == '/') {
            rel.erase(rel.begin());
        }
        if (!rel.empty()) {
            Detail::g_rel.insert(rel);
        }
    }
}

inline void Clear() {
    Set({}, {});
}

/// Whether the loader leaves this file out (`path` as the loader builds it).
inline bool Skipped(std::string_view path) {
    std::scoped_lock lock{Detail::g_mutex};
    if (Detail::g_rel.empty()) {
        return false;
    }
    const std::string p = Detail::Normal(path);
    if (p.size() <= Detail::g_dir.size() || p.compare(0, Detail::g_dir.size(), Detail::g_dir) != 0) {
        return false;
    }
    return Detail::g_rel.count(p.substr(Detail::g_dir.size())) != 0;
}

/// How many files are left out now.
inline std::size_t Count() {
    std::scoped_lock lock{Detail::g_mutex};
    return Detail::g_rel.size();
}

} // namespace FileSys::ModSkip
