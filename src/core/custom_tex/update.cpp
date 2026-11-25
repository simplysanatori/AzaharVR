// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
//
// See update.h.

#include "core/custom_tex/update.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <set>
#include <string_view>
#include <system_error>
#include "common/folder_link.h"
#include "common/logging/log.h"
#include "core/custom_tex/convert.h"
#include "core/custom_tex/download.h"
#include "core/custom_tex/layers.h"
#include "core/custom_tex/optimize.h"
#include "core/custom_tex/parts.h"

namespace fs = std::filesystem;

namespace CustomTex::Update {

namespace {

constexpr std::string_view kStaged = ".azaharvr-update";
constexpr std::string_view kOld = ".azaharvr-old";

std::string Month(const std::string& date) {
    return date.substr(0, 7);
}

/// Certainly later: by the day when both have one, else by the month.
bool Later(const std::string& date, const std::string& than) {
    if (date.empty() || than.empty()) {
        return false;
    }
    return date.size() == 10 && than.size() == 10 ? date > than : Month(date) > Month(than);
}

/// Not certainly on or after: `date` is before `than`, or in one month with a day missing.
bool MaybeBefore(const std::string& date, const std::string& than) {
    if (date.empty() || than.empty()) {
        return true;
    }
    return date.size() == 10 && than.size() == 10 ? date < than : Month(date) <= Month(than);
}

fs::path Canon(const fs::path& p) {
    std::error_code ec;
    const fs::path c = fs::weakly_canonical(p, ec);
    return fs::path((ec ? p : c).generic_string());
}

/// A real folder (not a link)
bool RealFolder(const fs::path& p) {
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(p, ec);
    return !ec && fs::is_directory(st) && !Common::FS::IsLink(st);
}

bool Exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(p, ec));
}

/// The download's catalog id of a folder that is a pack by the scan's rule: textures in it, and the
/// app's marker. "" it is not.
std::string GoodPack(const fs::path& p) {
    if (!RealFolder(p) || DetectFormat(p.generic_string()) == Format::Unknown) {
        return {};
    }
    const auto m = Download::ReadMarker(p.string());
    return m && m->id.rfind("zip:", 0) != 0 ? m->id : std::string{};
}

/// The title's library when no link is on the way from the load folder to it.
fs::path Library(const Paths& paths, u64 title_id) {
    const fs::path library = Canon(paths.load_dir) / "texture-packs" / TitleFolder(title_id);
    return RealFolder(library) && Canon(library) == library ? library : fs::path{};
}

std::string FolderOf(const std::string& name, std::string_view suffix) {
    if (name.size() < suffix.size() + 2 || name.front() != '.' ||
        std::string_view(name).substr(name.size() - suffix.size()) != suffix) {
        return {};
    }
    return name.substr(1, name.size() - 1 - suffix.size());
}

/// The folder names in the library that have a staged update or an old version beside them.
std::set<std::string> Folders(const fs::path& library) {
    std::set<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(library, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        for (const std::string_view suffix : {kStaged, kOld}) {
            if (std::string folder = FolderOf(name, suffix); !folder.empty()) {
                out.insert(std::move(folder));
            }
        }
    }
    return out;
}

/// The optimized copies in the library whose original is (or was) `pack`.
std::vector<std::string> CopiesOf(const fs::path& library, const fs::path& pack) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(library, ec), end; !ec && it != end; it.increment(ec)) {
        if (!RealFolder(it->path()) || it->path().filename().string().starts_with('.')) {
            continue;
        }
        const auto source = Optimize::SourceOf(it->path().string());
        if (source && Canon(*source) == pack) {
            out.push_back(Canon(it->path()).generic_string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool Rename(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (Exists(to)) {
        return false;
    }
    fs::rename(from, to, ec);
    if (ec) {
        LOG_WARNING(Render, "texture packs: {} cannot be renamed: {}", from.generic_string(),
                    ec.message());
    }
    return !ec;
}

/// The Customize choices for the new version: the ones of the pack that was used, with the new
/// version's own hints.
void CarryParts(const fs::path& staged, const fs::path& from) {
    Parts::State state = Parts::ReadState(from.string());
    const Parts::Hints hints = Parts::ReadState(staged.string()).hints;
    if (state == Parts::State{}) {
        return; // none
    }
    state.hints = hints;
    if (!Parts::WriteState(staged.string(), state)) {
        LOG_WARNING(Render, "texture packs: the Customize choices of {} did not carry over",
                    from.generic_string());
    }
}

/// What the catalog said of the version the marker is of.
Download::Entry EntryOf(const Download::Marker& marker) {
    using Type = Download::Source::Type;
    Download::Entry e;
    e.id = marker.id;
    for (const Type type : {Type::GithubRelease, Type::Gdrive, Type::GdriveFolder, Type::Gamebanana,
                            Type::Https}) {
        if (Download::SourceName(type) == marker.source) {
            e.source.type = type;
        }
    }
    e.source.file_id = marker.file_id;
    e.updated = marker.catalog_updated;
    e.sha256 = marker.sha256;
    e.size = marker.size;
    return e;
}

/// The game's list has `live` in the place of its optimized copies.
bool UseOriginal(const Paths& paths, u64 title_id, const fs::path& live,
                 const std::vector<std::string>& copies, std::string& error) {
    for (const std::string& copy : copies) {
        Layers::State list = Layers::Current(paths, title_id);
        if (!Layers::Has(list, copy)) {
            continue;
        }
        Layers::ApplySwitch(list, {copy, live.generic_string()});
        if (const auto r = Layers::Apply(paths, title_id, list); !r.ok) {
            error = r.error;
            return false;
        }
    }
    return true;
}

} // namespace

bool Newer(const Download::Marker& installed, const Download::Entry& entry) {
    using Type = Download::Source::Type;
    if (installed.id.empty() || installed.id != entry.id) {
        return false;
    }
    const bool catalog_dates =
        entry.source.type == Type::Gdrive || entry.source.type == Type::Https;
    // What the catalog said at the download; older markers of these sources have it as the date
    const std::string then = !installed.catalog_updated.empty() ? installed.catalog_updated
                             : catalog_dates && installed.source == Download::SourceName(entry.source.type)
                                 ? installed.updated
                                 : std::string{};
    if (!installed.sha256.empty() && !entry.sha256.empty()) {
        return installed.sha256 != entry.sha256 && !MaybeBefore(entry.updated, then);
    }
    if (!Later(entry.updated, then)) {
        return false;
    }
    const bool sizes = installed.size > 0 && entry.size > 0;
    const bool other_size = sizes && installed.size != entry.size;
    if (catalog_dates) {
        // Same file under a new date?
        return !sizes || other_size || installed.file_id != entry.source.file_id;
    }
    return entry.source.type != Type::GdriveFolder && other_size;
}

bool NewerThanAll(const std::vector<Download::Marker>& installed, const Download::Entry& entry) {
    return !installed.empty() &&
           std::all_of(installed.begin(), installed.end(),
                       [&](const Download::Marker& m) { return Newer(m, entry); });
}

std::string StagedPath(const std::string& pack) {
    const fs::path p(pack);
    return (p.parent_path() / ("." + p.filename().string() + std::string(kStaged))).string();
}

std::string OldPath(const std::string& pack) {
    const fs::path p(pack);
    return (p.parent_path() / ("." + p.filename().string() + std::string(kOld))).string();
}

bool IsStagedName(const std::string& name) {
    return !FolderOf(name, kStaged).empty();
}

std::string TargetFolder(const Paths& paths, u64 title_id, const std::string& catalog_id) {
    const fs::path library = Library(paths, title_id);
    if (library.empty() || catalog_id.empty() || catalog_id.rfind("zip:", 0) == 0) {
        return {};
    }
    std::vector<std::string> originals, gone;
    std::error_code ec;
    for (fs::directory_iterator it(library, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.starts_with('.') || !RealFolder(it->path())) {
            continue;
        }
        const auto m = Download::ReadMarker(it->path().string());
        if (!m || m->id != catalog_id) {
            continue;
        }
        if (!Optimize::IsOptimized(it->path().string())) {
            originals.push_back(name);
        } else if (const auto source = Optimize::SourceOf(it->path().string())) {
            const fs::path original(*source);
            if (Canon(original.parent_path()) == library && !Exists(original) &&
                !original.filename().empty() && !original.filename().string().starts_with('.')) {
                gone.push_back(original.filename().string());
            }
        }
    }
    std::sort(originals.begin(), originals.end());
    std::sort(gone.begin(), gone.end());
    const Layers::State list = Layers::Current(paths, title_id);
    for (const std::string& name : originals) {
        const std::string id = (library / name).generic_string();
        const auto copies = CopiesOf(library, id);
        if (Layers::Has(list, id) || std::any_of(copies.begin(), copies.end(), [&](const auto& c) {
                return Layers::Has(list, c);
            })) {
            return name;
        }
    }
    return !originals.empty() ? originals.front() : !gone.empty() ? gone.front() : std::string{};
}

Applied Apply(const Paths& paths, u64 title_id, const std::string& only) {
    Applied out;
    const fs::path library = Library(paths, title_id);
    if (library.empty()) {
        return out;
    }
    for (const std::string& folder : Folders(library)) {
        if (!only.empty() && folder != only) {
            continue;
        }
        const fs::path live = library / folder;
        const fs::path staged = StagedPath(live.string()), old = OldPath(live.string());
        const bool has_live = Exists(live), has_old = Exists(old);
        const std::string new_id = GoodPack(staged);
        if (!has_live && has_old) {
            // Cut off between the renames
            const std::string old_id = GoodPack(old);
            if (!new_id.empty() && new_id == old_id) {
                if (!Rename(staged, live)) {
                    out.error = "Cannot put the update of " + folder + " in place";
                    continue;
                }
            } else if (!Exists(staged) && !old_id.empty()) {
                if (!Rename(old, live)) {
                    out.error = "Cannot put " + folder + " back";
                }
                continue;
            } else {
                out.error = "Not certain which one is good: " + folder;
                continue;
            }
        } else {
            const std::vector<std::string> copies = CopiesOf(library, live);
            if (new_id.empty() || has_old ||
                // a new original for a copy whose original was removed, or a newer version of a pack
                (has_live ? GoodPack(live) != new_id ||
                                !Newer(*Download::ReadMarker(live.string()),
                                       EntryOf(*Download::ReadMarker(staged.string())))
                          : copies.empty())) {
                continue;
            }
            if (Convert::IsRunning(live.generic_string())) {
                out.error = "Being converted: " + folder;
                continue;
            }
            // The choices of the one that is used
            const Layers::State before = Layers::Current(paths, title_id);
            const auto used = std::find_if(copies.begin(), copies.end(), [&](const std::string& c) {
                return Layers::Has(before, c);
            });
            CarryParts(staged, used != copies.end() ? fs::path(*used)
                               : has_live           ? live
                                                    : fs::path(copies.front()));
            // The optimized copies are of the old version, and no longer used: first, so that a
            // crash in here never leaves one in the new original's place
            std::vector<std::string> marked;
            for (const std::string& copy : copies) {
                if (!Optimize::IsStale(copy) && Optimize::MarkStale(copy, true)) {
                    marked.push_back(copy);
                }
            }
            bool ok = !has_live || UseOriginal(paths, title_id, live, copies, out.error);
            if (ok && has_live && !Rename(live, old)) {
                ok = false;
                out.error = "Cannot move " + folder + " aside";
            }
            if (ok && (!Rename(staged, live) || GoodPack(live) != new_id)) {
                ok = false;
                out.error = "Cannot put the update of " + folder + " in place";
                if (Exists(live)) {
                    Rename(live, staged);
                }
                if (has_live) {
                    Rename(old, live);
                }
            }
            if (!ok) { // as it was; the new one waits
                for (const std::string& copy : marked) {
                    Optimize::MarkStale(copy, false);
                }
                if (Layers::Current(paths, title_id) != before) {
                    Layers::Apply(paths, title_id, before);
                }
                continue;
            }
            if (!has_live) {
                UseOriginal(paths, title_id, live, copies, out.error);
            }
        }
        LOG_INFO(Render, "texture packs: {} updated", live.generic_string());
        ForgetSize(live.generic_string());
        out.packs.push_back(live.generic_string());
        for (const std::string& copy : CopiesOf(library, live)) {
            if (Optimize::IsStale(copy)) {
                out.stale.push_back(copy);
            }
        }
    }
    if (!out.error.empty()) {
        LOG_WARNING(Render, "texture packs: update: {}", out.error);
    }
    return out;
}

void Finish(const Paths& paths, u64 title_id) {
    const fs::path library = Library(paths, title_id);
    if (library.empty()) {
        return;
    }
    for (const std::string& folder : Folders(library)) {
        const fs::path live = library / folder, old = OldPath(live.string());
        const std::string id = GoodPack(live);
        const auto mine = Download::ReadMarker(live.string());
        const auto theirs = Download::ReadMarker(old.string());
        if (!RealFolder(old) || id.empty() || !theirs || !Newer(*theirs, EntryOf(*mine))) {
            continue; // the new one is not in place, or it is not certain that it is
        }
        std::error_code ec;
        fs::remove_all(old, ec);
        if (ec) {
            LOG_WARNING(Render, "texture packs: the old {} cannot be deleted: {}", folder,
                        ec.message());
        }
    }
}

std::vector<u64> Waiting(const Paths& paths) {
    std::vector<u64> out;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(paths.load_dir) / "texture-packs", ec), end;
         !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        u64 title = 0;
        const auto r = std::from_chars(name.data(), name.data() + name.size(), title, 16);
        if (name.size() == 16 && r.ec == std::errc{} && r.ptr == name.data() + name.size() &&
            !Folders(it->path()).empty()) {
            out.push_back(title);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void RemoveLeftovers(const std::string& pack) {
    for (const std::string& path : {StagedPath(pack), OldPath(pack)}) {
        std::error_code ec;
        if (RealFolder(path)) {
            fs::remove_all(path, ec);
        }
    }
}

} // namespace CustomTex::Update
