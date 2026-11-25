// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
//
// See delete.h.

#include "core/custom_tex/delete.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <set>
#include <system_error>
#include <utility>
#include "common/folder_link.h"
#include "common/logging/log.h"
#include "core/custom_tex/convert.h"
#include "core/custom_tex/download.h"
#include "core/custom_tex/layers.h"

namespace fs = std::filesystem;

namespace CustomTex::Deletion {

namespace {

/// As Pack::id.
fs::path Canon(const fs::path& p) {
    std::error_code ec;
    const fs::path c = fs::weakly_canonical(p, ec);
    return fs::path((ec ? p : c).generic_string());
}

/// `inner` is `outer` or inside it (both canonical).
bool Within(const fs::path& inner, const fs::path& outer) {
    auto i = inner.begin(), o = outer.begin();
    for (; o != outer.end(); ++i, ++o) {
        if (i == inner.end() || *i != *o) {
            return false;
        }
    }
    return true;
}

/// Also when the paths differ: a file system that ignores case, a bind mount.
bool SameFolder(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return a == b || fs::equivalent(a, b, ec);
}

fs::path LibraryRoot(const Paths& paths, u64 title_id) {
    return fs::path(paths.load_dir) / "texture-packs" / TitleFolder(title_id);
}

/// The folders that are never a pack, nor inside one.
std::vector<fs::path> Roots(const Paths& paths, u64 title_id) {
    const fs::path load = paths.load_dir;
    std::vector<fs::path> roots{Canon(load), Canon(load / "textures"), Canon(load / "texture-packs"),
                                Canon(LibraryRoot(paths, title_id))};
    for (const std::string& root : ExtraRootFolders(paths)) {
        roots.push_back(Canon(root));
    }
    return roots;
}

/// The titles with a folder in <load>/textures or <load>/texture-packs.
std::set<u64> Titles(const Paths& paths) {
    std::set<u64> out;
    for (const char* sub : {"textures", "texture-packs"}) {
        std::error_code ec;
        for (fs::directory_iterator it(fs::path(paths.load_dir) / sub, ec), end; !ec && it != end;
             it.increment(ec)) {
            const std::string name = it->path().filename().generic_string();
            u64 title = 0;
            const auto r = std::from_chars(name.data(), name.data() + name.size(), title, 16);
            if (name.size() == 16 && r.ec == std::errc{} && r.ptr == name.data() + name.size()) {
                out.insert(title);
            }
        }
    }
    return out;
}

/// The title's entries that are the pack or inside it.
std::set<std::string> Uses(const Paths& paths, u64 title_id, const std::string& id) {
    std::set<std::string> out;
    for (const Layers::Entry& e : Layers::Current(paths, title_id).entries) {
        if (Within(Canon(e.pack), id)) {
            out.insert(e.pack);
        }
    }
    return out;
}

/// "" when it is gone.
std::string Remove(const fs::path& path, bool link) {
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(path, ec);
    if (link) {
        if (!Common::FS::IsLink(st)) {
            return "Not a link any more";
        }
        fs::remove(path, ec); // the link only
    } else {
        if (Common::FS::IsLink(st) || !fs::is_directory(st) || Canon(path) != path) {
            return "Not a folder any more";
        }
        fs::remove_all(path, ec); // links inside are removed, never followed
    }
    if (ec) {
        return "Cannot remove the folder: " + ec.message();
    }
    return fs::exists(fs::symlink_status(path, ec)) ? "Cannot remove the folder" : "";
}

} // namespace

Target Check(const Paths& paths, u64 title_id, const std::vector<Pack>& packs,
             const std::string& id) {
    Target t;
    t.id = id;
    const auto refuse = [&t](const char* why) {
        t.place = Place::None;
        t.error = why;
        return t;
    };
    const auto pack = std::find_if(packs.begin(), packs.end(),
                                   [&](const Pack& p) { return !id.empty() && p.id == id; });
    if (pack == packs.end()) {
        return refuse("Not a listed pack");
    }
    const std::string tid = TitleFolder(title_id);
    const fs::path load = Canon(paths.load_dir);
    const fs::path library = Canon(LibraryRoot(paths, title_id));
    std::error_code ec;

    // A link in the library: the link goes
    const fs::path listed = fs::path(pack->path);
    const fs::path name = listed.filename();
    if (name.empty() || name == "." || name == "..") {
        return refuse("Bad folder name");
    }
    if (Common::FS::IsLink(listed, ec)) {
        const fs::path parent = Canon(listed.parent_path());
        if (parent == library && library == load / "texture-packs" / tid) {
            t.path = (parent / name).generic_string();
            t.link = true;
            t.place = Place::Library;
            return t;
        }
    }

    // Else the real folder the pack is (its id has no links in it)
    const fs::path real = Canon(id);
    const fs::file_status st = fs::symlink_status(real, ec);
    if (real.generic_string() != id || Common::FS::IsLink(st) || !fs::is_directory(st)) {
        return refuse("Not a folder");
    }
    for (const fs::path& root : Roots(paths, title_id)) {
        if (Within(root, real) || SameFolder(root, real)) {
            return refuse("Holds a texture folder");
        }
    }
    for (const Pack& p : packs) {
        if (p.id != id && Within(Canon(p.id), real)) {
            return refuse("Holds another pack");
        }
    }
    if (!pack->managed || Common::FS::IsLink(listed, ec)) {
        // not through the library
    } else if (real.parent_path() == library) {
        // The library itself reached through a link (texture-packs or the title's folder in it):
        // only what the app put there
        if (library != load / "texture-packs" / tid && !Download::MarkerId(id)) {
            return refuse("The library is a link");
        }
        t.place = Place::Library;
    }
    if (t.place == Place::None && real.parent_path() == load / "textures" &&
        IsTitleFolder(real.filename().string(), tid)) {
        t.place = Place::Load;
    }
    if (t.place == Place::None) {
        return refuse("Not in the texture folders");
    }
    t.path = id;
    return t;
}

std::vector<u64> OtherGames(const Paths& paths, u64 title_id, const std::string& id) {
    std::vector<u64> out;
    for (const u64 title : Titles(paths)) {
        if (title != title_id && !Uses(paths, title, id).empty()) {
            out.push_back(title);
        }
    }
    return out;
}

Result Delete(const Paths& paths, u64 title_id, const std::vector<std::string>& ids,
              const Options& options) {
    Result result;
    const auto fail = [&result](std::string why) {
        result.ok = false;
        result.error = std::move(why);
        return result;
    };
    if (ids.empty()) {
        return fail("No pack");
    }

    // Everything is checked before anything changes
    const std::vector<Pack> packs = ListPacks(paths, title_id);
    std::vector<Target> targets;
    for (const std::string& id : ids) {
        const Target t = Check(paths, title_id, packs, id);
        if (!t.Ok()) {
            return fail(t.error);
        }
        if (Convert::IsRunning(id)) {
            return fail("Being converted");
        }
        if (!t.link && !options.shared && !OtherGames(paths, title_id, id).empty()) {
            return fail("Another game uses it");
        }
        if (std::none_of(targets.begin(), targets.end(),
                         [&](const Target& o) { return o.path == t.path; })) {
            targets.push_back(t);
        }
    }

    // Off first, in the other games too: the link moves to the next pack (layers.h)
    const auto off = [&](u64 title, const std::string& pack) {
        const bool on = Layers::Has(Layers::Current(paths, title), pack);
        CustomTex::Result r = Layers::TurnOff(paths, title, pack);
        result.changed |= !r.backed_up.empty() ||
                          (on && !Layers::Has(Layers::Current(paths, title), pack));
        return r;
    };
    for (const Target& t : targets) {
        if (t.link) {
            continue; // the pack stays for them
        }
        for (const u64 title : OtherGames(paths, title_id, t.id)) {
            for (const std::string& used : Uses(paths, title, t.id)) {
                if (const CustomTex::Result r = off(title, used); !r.ok) {
                    return fail(r.error);
                }
            }
        }
    }
    const fs::path place = Canon(paths.load_dir) / "textures" / TitleFolder(title_id);
    for (Target& t : targets) {
        const CustomTex::Result r = off(title_id, t.id);
        if (!r.ok) {
            return fail(r.error);
        }
        if (!t.link && fs::path(t.path) == place) {
            // A real folder where the game reads was moved into the library
            const fs::path moved = Canon(r.backed_up);
            if (r.backed_up.empty() ||
                moved.parent_path() != Canon(LibraryRoot(paths, title_id))) {
                return fail("Still in use");
            }
            t.path = moved.generic_string();
        }
    }

    for (const Target& t : targets) {
        const std::string error = Remove(t.path, t.link);
        ForgetSize(t.path);
        ForgetSize(t.id);
        result.changed = true;
        if (!error.empty()) {
            return fail(error);
        }
        LOG_INFO(Render, "texture packs: deleted {}{}", t.link ? "the link " : "", t.path);
    }
    result.ok = true;
    return result;
}

} // namespace CustomTex::Deletion
