// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
//
// Deleting texture packs (core/custom_tex/delete.h), on temp folders.

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#include "common/folder_link.h"
#include "core/custom_tex/convert.h"
#include "core/custom_tex/delete.h"
#include "core/custom_tex/download.h"
#include "core/custom_tex/layers.h"
#include "core/custom_tex/optimize.h"
#include "core/custom_tex/packs.h"

namespace fs = std::filesystem;
using namespace CustomTex;
using Deletion::Place;

namespace {

constexpr u64 kTitle = 0x0004000000033500ULL;
const std::string kTid = "0004000000033500";
constexpr u64 kOther = 0x0004000000033600ULL; // another region of the game
const std::string kOtherTid = "0004000000033600";

struct Temp {
    fs::path root;
    Paths paths;
    Temp() {
        static int n = 0;
        root = fs::weakly_canonical(fs::temp_directory_path()) /
               fmt::format("azf_texdelete_{}_{}", ::getpid(), n++);
        fs::remove_all(root);
        fs::create_directories(Library());
        fs::create_directories(Load() / "textures");
        paths.load_dir = Load().generic_string();
    }
    ~Temp() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    fs::path Load() const {
        return root / "load";
    }
    fs::path Library() const {
        return Load() / "texture-packs" / kTid;
    }
    fs::path Place() const {
        return Load() / "textures" / kTid;
    }
};

void Put(const fs::path& file, const std::string& text = "x") {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

/// A PNG pack of two textures in a folder of its own
void PutPack(const fs::path& dir) {
    Put(dir / "tex1_64x64_0123456789ABCDEF_14.png");
    Put(dir / "UI" / "tex1_32x32_0123456789ABCDE0_14.png");
}

/// Its optimized copy
void PutCopy(const fs::path& dir, const std::string& source) {
    Put(dir / "tex1_64x64_0123456789ABCDEF_14.ktx");
    Put(dir / Optimize::kMarker, R"({"format":"ASTC 4x4","source":")" + source + "\"}");
}

void Link(const fs::path& target, const fs::path& at) {
    fs::create_directories(at.parent_path());
    std::error_code ec;
    Common::FS::CreateFolderLink(target, at, ec);
    REQUIRE_FALSE(ec);
}

std::string Id(const fs::path& p) {
    return fs::weakly_canonical(p).generic_string();
}

Deletion::Target Check(const Temp& t, const std::string& id) {
    return Deletion::Check(t.paths, kTitle, ListPacks(t.paths, kTitle), id);
}

/// Every path under the folder, links not followed, with the files' contents
std::map<std::string, std::string> Tree(const fs::path& root) {
    std::map<std::string, std::string> out;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        std::string& value = out[e.path().lexically_relative(root).generic_string()];
        const fs::file_status st = fs::symlink_status(e.path());
        if (Common::FS::IsLink(st)) {
            value = "-> " + fs::read_symlink(e.path()).generic_string();
        } else if (fs::is_regular_file(st)) {
            std::ifstream f(e.path(), std::ios::binary);
            value.assign(std::istreambuf_iterator<char>(f), {});
        }
    }
    return out;
}

std::vector<std::string> On(const Temp& t, u64 title = kTitle) {
    return Layers::Packs(Layers::Current(t.paths, title));
}

std::vector<std::string> Listed(const Temp& t, u64 title = kTitle) {
    std::vector<std::string> out;
    for (const Pack& p : ListPacks(t.paths, title)) {
        out.push_back(p.id);
    }
    return out;
}

/// Refused, and nothing on the disk changed
void CheckRefused(const Temp& t, const std::vector<std::string>& ids,
                  const Deletion::Options& options = {}) {
    const auto before = Tree(t.root);
    const Deletion::Result r = Deletion::Delete(t.paths, kTitle, ids, options);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
    CHECK_FALSE(r.changed);
    CHECK(Tree(t.root) == before);
}

} // namespace

TEST_CASE("texdelete: the player's own pack in the library", "[texdelete]") {
    Temp t;
    PutPack(t.Library() / "Mine");
    PutPack(t.Library() / "Other");
    const std::string id = Id(t.Library() / "Mine");
    REQUIRE(Check(t, id).Ok());
    CHECK(Check(t, id).place == Place::Library);
    CHECK_FALSE(Check(t, id).link);
    CHECK(Check(t, id).path == id);

    // Off: only its folder goes
    ComputeSize(id);
    auto r = Deletion::Delete(t.paths, kTitle, {id});
    REQUIRE(r.ok);
    CHECK(r.changed);
    CHECK_FALSE(fs::exists(t.Library() / "Mine"));
    CHECK_FALSE(CachedSize(id));
    CHECK(Listed(t) == std::vector<std::string>{Id(t.Library() / "Other")});
    CheckRefused(t, {id}); // gone

    // The last one: the library's folder stays
    REQUIRE(Deletion::Delete(t.paths, kTitle, {Id(t.Library() / "Other")}).ok);
    CHECK(fs::is_directory(t.Library()));
    CHECK(Listed(t).empty());
}

TEST_CASE("texdelete: the pack the game reads", "[texdelete]") {
    Temp t;
    PutPack(t.Library() / "A");
    PutPack(t.Library() / "B");
    const std::string a = Id(t.Library() / "A"), b = Id(t.Library() / "B");
    REQUIRE(Layers::TurnOn(t.paths, kTitle, b).ok);
    REQUIRE(Layers::TurnOn(t.paths, kTitle, a).ok);
    REQUIRE(On(t) == std::vector<std::string>{a, b});
    REQUIRE(Layers::Linked(t.paths, kTitle) == a);

    // The top one: the link moves to the next pack
    REQUIRE(Deletion::Delete(t.paths, kTitle, {a}).ok);
    CHECK(On(t) == std::vector<std::string>{b});
    CHECK(Layers::Linked(t.paths, kTitle) == b);
    CHECK_FALSE(fs::exists(t.Library() / "A"));

    // The only one: no link
    REQUIRE(Deletion::Delete(t.paths, kTitle, {b}).ok);
    CHECK(On(t).empty());
    CHECK_FALSE(fs::exists(fs::symlink_status(t.Place())));
    CHECK(fs::is_directory(t.Load() / "textures"));

    // A lower one: the top one stays linked
    PutPack(t.Library() / "A");
    PutPack(t.Library() / "B");
    REQUIRE(Layers::TurnOn(t.paths, kTitle, b).ok);
    REQUIRE(Layers::TurnOn(t.paths, kTitle, a).ok);
    REQUIRE(Deletion::Delete(t.paths, kTitle, {b}).ok);
    CHECK(On(t) == std::vector<std::string>{a});
    CHECK(Layers::Linked(t.paths, kTitle) == a);
}

TEST_CASE("texdelete: a real folder where the game reads", "[texdelete]") {
    Temp t;
    PutPack(t.Place());
    const std::string id = Id(t.Place());
    REQUIRE(Check(t, id).Ok());
    CHECK(Check(t, id).place == Place::Load);

    SECTION("the only pack") {
        const auto r = Deletion::Delete(t.paths, kTitle, {id});
        REQUIRE(r.ok);
        CHECK(Listed(t).empty());
        CHECK_FALSE(fs::exists(fs::symlink_status(t.Place())));
        CHECK(fs::is_directory(t.Load() / "textures"));
        CHECK_FALSE(fs::exists(t.Library() / "Original")); // not left in the library either
    }
    SECTION("a pack under it stays on") {
        PutPack(t.Library() / "Under");
        const std::string under = Id(t.Library() / "Under");
        REQUIRE(Layers::WriteSaved(t.paths.load_dir, kTitle, {{{id, ""}, {under, ""}}}));
        REQUIRE(On(t) == std::vector<std::string>{id, under});
        REQUIRE(Deletion::Delete(t.paths, kTitle, {id}).ok);
        CHECK(On(t) == std::vector<std::string>{under});
        CHECK(Layers::Linked(t.paths, kTitle) == under);
        CHECK(Listed(t) == std::vector<std::string>{under});
        CHECK(fs::exists(t.Library() / "Under" / "UI"));
    }
    SECTION("the library's own Original stays") {
        PutPack(t.Library() / "Original");
        REQUIRE(Deletion::Delete(t.paths, kTitle, {id}).ok);
        CHECK(Listed(t) == std::vector<std::string>{Id(t.Library() / "Original")});
    }
}

TEST_CASE("texdelete: a folder beside the place the game reads", "[texdelete]") {
    // Optimize Texture Pack in the Flat game list: the pack is <TID>-original, its copy <TID>-astc4x4
    Temp t;
    const fs::path copy = t.Load() / "textures" / (kTid + "-astc4x4");
    const fs::path original = t.Load() / "textures" / (kTid + "-original");
    PutPack(original);
    PutCopy(copy, "../" + original.filename().string());
    Link(copy, t.Place());
    REQUIRE(Listed(t) == std::vector<std::string>{Id(copy)});
    CHECK(Check(t, Id(copy)).place == Place::Load);
    CheckRefused(t, {Id(original)}); // not listed: never
    REQUIRE(Deletion::Delete(t.paths, kTitle, {Id(copy)}).ok);
    CHECK_FALSE(fs::exists(copy));
    CHECK_FALSE(fs::exists(fs::symlink_status(t.Place())));
    CHECK(fs::exists(original / "UI"));
    // Another game's folder beside it
    PutPack(t.Load() / "textures" / kOtherTid);
    CheckRefused(t, {Id(t.Load() / "textures" / kOtherTid)});
}

TEST_CASE("texdelete: Optimize for Frame pairs", "[texdelete]") {
    Temp t;
    const fs::path png = t.Library() / "Original", copy = t.Library() / "Original-astc4x4";
    PutPack(png);
    PutCopy(copy, "../Original");
    PutPack(t.Library() / "Keep");
    const std::string p = Id(png), c = Id(copy);
    REQUIRE(Layers::Partner(p) == c);

    SECTION("both, neither with a download marker") {
        REQUIRE(Layers::TurnOn(t.paths, kTitle, c).ok);
        REQUIRE(Deletion::Delete(t.paths, kTitle, {p, c}).ok);
        CHECK(Listed(t) == std::vector<std::string>{Id(t.Library() / "Keep")});
        CHECK(On(t).empty());
    }
    SECTION("the original from a download, the copy without a marker") {
        REQUIRE(Download::WriteMarker(p, "some-pack", "gdrive", "2026-11-24"));
        REQUIRE(Deletion::Delete(t.paths, kTitle, {p, c}).ok);
        CHECK(Listed(t) == std::vector<std::string>{Id(t.Library() / "Keep")});
    }
    SECTION("the copy alone: the original stays, off") {
        REQUIRE(Layers::TurnOn(t.paths, kTitle, c).ok);
        REQUIRE(Deletion::Delete(t.paths, kTitle, {c}).ok);
        CHECK(fs::exists(png / "UI"));
        CHECK(On(t).empty());
    }
    SECTION("one of them outside: nothing is deleted") {
        const fs::path out = t.root / "elsewhere" / "Pack";
        PutPack(out);
        CheckRefused(t, {Id(out), c});
        CheckRefused(t, {c, Id(out)});
    }
    SECTION("the same one twice") {
        REQUIRE(Deletion::Delete(t.paths, kTitle, {c, c}).ok);
        CHECK(fs::exists(png));
    }
}

TEST_CASE("texdelete: links", "[texdelete]") {
    Temp t;
    const fs::path outside = t.root / "outside" / "Pack";
    PutPack(outside);

    SECTION("a link in the library goes, what it points at stays") {
        Link(outside, t.Library() / "Linked");
        PutPack(t.Library() / "Keep");
        const std::string id = Id(outside);
        const auto target = Check(t, id);
        REQUIRE(target.Ok());
        CHECK(target.link);
        CHECK(target.path == Id(t.Library()) + "/Linked");
        REQUIRE(Layers::TurnOn(t.paths, kTitle, id).ok);
        REQUIRE(Deletion::Delete(t.paths, kTitle, {id}).ok);
        CHECK_FALSE(fs::exists(fs::symlink_status(t.Library() / "Linked")));
        CHECK(fs::exists(outside / "UI"));
        CHECK(On(t).empty());
        CHECK(Listed(t) == std::vector<std::string>{Id(t.Library() / "Keep")});
    }
    SECTION("a link in the library to another pack of the library") {
        PutPack(t.Library() / "Zed");
        Link(t.Library() / "Zed", t.Library() / "Alias");
        const std::string id = Id(t.Library() / "Zed");
        REQUIRE(Deletion::Delete(t.paths, kTitle, {id}).ok); // the folder or the link: never both
        CHECK(fs::exists(t.Library() / "Zed") !=
              fs::exists(fs::symlink_status(t.Library() / "Alias")));
    }
    SECTION("the game's link to a folder that is in no texture folder") {
        Link(outside, t.Place());
        REQUIRE(Listed(t) == std::vector<std::string>{Id(outside)});
        CHECK_FALSE(Check(t, Id(outside)).Ok());
        CheckRefused(t, {Id(outside)});
    }
    SECTION("a dangling link is no pack") {
        Link(outside, t.Library() / "Dangling");
        fs::remove_all(outside);
        CHECK(Listed(t).empty());
        CheckRefused(t, {(t.Library() / "Dangling").generic_string()});
        CheckRefused(t, {Id(outside)});
    }
    SECTION("links inside a pack are not followed") {
        PutPack(t.Library() / "Mine");
        Link(outside, t.Library() / "Mine" / "more");
        REQUIRE(Deletion::Delete(t.paths, kTitle, {Id(t.Library() / "Mine")}).ok);
        CHECK_FALSE(fs::exists(t.Library() / "Mine"));
        CHECK(fs::exists(outside / "UI"));
    }
    SECTION("load/textures is a link to another place") {
        const fs::path elsewhere = t.root / "azahar" / "load" / "textures";
        PutPack(elsewhere / kTid);
        fs::remove_all(t.Load() / "textures");
        Link(elsewhere, t.Load() / "textures");
        REQUIRE(Listed(t) == std::vector<std::string>{Id(elsewhere / kTid)});
        CheckRefused(t, {Id(elsewhere / kTid)});
    }
    SECTION("the library is a link to another place") {
        const fs::path elsewhere = t.root / "azahar" / "load" / "texture-packs";
        PutPack(elsewhere / kTid / "Mine");
        PutPack(elsewhere / kTid / "Down");
        REQUIRE(Download::WriteMarker((elsewhere / kTid / "Down").string(), "some-pack", "gdrive"));
        fs::remove_all(t.Load() / "texture-packs");
        Link(elsewhere, t.Load() / "texture-packs");
        REQUIRE(Listed(t).size() == 2);
        CheckRefused(t, {Id(elsewhere / kTid / "Mine")});
        CHECK(Deletion::Delete(t.paths, kTitle, {Id(elsewhere / kTid / "Down")}).ok); // as before
        CHECK(fs::exists(elsewhere / kTid / "Mine"));
    }
}

TEST_CASE("texdelete: packs outside the load folder", "[texdelete]") {
    Temp t;
    const fs::path home = t.root / "home";
    const fs::path azahar = home / ".local" / "share" / "azahar-emu" / "load";
    const fs::path big = azahar / "textures-4k" / kTid;
    const fs::path mine = home / "Packs" / (kTid + "-HD");
    PutPack(big);
    PutPack(mine);
    PutPack(t.Library() / "Keep");
    t.paths.home = home.generic_string();
    t.paths.extra_roots = {(azahar / "textures-*").generic_string(), (home / "Packs").generic_string()};
    t.paths.link_roots = {(azahar / "textures").generic_string()};
    const std::string keep = Id(t.Library() / "Keep");
    const Deletion::Options told{.outside = true};

    SECTION("another Azahar's and the player's own place") {
        CHECK(Check(t, Id(big)).place == Place::Outside);
        CHECK(Check(t, Id(big)).other_azahar);
        CHECK(Check(t, Id(mine)).place == Place::Outside);
        CHECK_FALSE(Check(t, Id(mine)).other_azahar);
        CheckRefused(t, {Id(big)}); // only when the player was told where it is
        CheckRefused(t, {Id(mine)}, {.shared = true});
        REQUIRE(Deletion::Delete(t.paths, kTitle, {Id(big)}, told).ok);
        CHECK_FALSE(fs::exists(big));
        CHECK(fs::is_directory(azahar / "textures-4k"));
        REQUIRE(Deletion::Delete(t.paths, kTitle, {Id(mine)}, told).ok);
        CHECK(fs::is_directory(home / "Packs"));
        CHECK(Listed(t) == std::vector<std::string>{keep});
    }
    SECTION("the one the game reads, through the game's link") {
        REQUIRE(Layers::TurnOn(t.paths, kTitle, keep).ok);
        REQUIRE(Layers::TurnOn(t.paths, kTitle, Id(big)).ok);
        REQUIRE(Layers::Linked(t.paths, kTitle) == Id(big));
        REQUIRE(Deletion::Delete(t.paths, kTitle, {Id(big)}, told).ok);
        CHECK_FALSE(fs::exists(big));
        CHECK(Layers::Linked(t.paths, kTitle) == keep); // the next pack
    }
    SECTION("another Azahar's load/textures: listed and deletable only through the game's link") {
        const fs::path read = azahar / "textures" / kTid;
        PutPack(read);
        CheckRefused(t, {Id(read)}, told);
        Link(read, t.Place());
        CHECK(Check(t, Id(read)).place == Place::Outside);
        REQUIRE(Deletion::Delete(t.paths, kTitle, {Id(read)}, told).ok);
        CHECK_FALSE(fs::exists(read));
        CHECK_FALSE(fs::exists(fs::symlink_status(t.Place())));
        CHECK(fs::is_directory(azahar / "textures"));
        CHECK(fs::exists(big / "UI"));
    }
    SECTION("the other Azahar's link to it stays, dangling") {
        Link(big, azahar / "textures" / kTid);
        REQUIRE(Deletion::Delete(t.paths, kTitle, {Id(big)}, told).ok);
        CHECK(fs::exists(fs::symlink_status(azahar / "textures" / kTid)));
        CHECK_FALSE(fs::exists(azahar / "textures" / kTid));
    }
    SECTION("a link in another place: never what it points at, when that is in no known place") {
        const fs::path far = home / "Documents" / kTid;
        PutPack(far);
        Link(far, azahar / "textures-4k" / (kTid + "-link"));
        const auto listed = Listed(t);
        REQUIRE(std::find(listed.begin(), listed.end(), Id(far)) != listed.end());
        CheckRefused(t, {Id(far)}, told);
    }
    SECTION("deeper in a known place, beside it, or the place itself") {
        const fs::path deep = azahar / "textures-4k" / "more" / kTid;
        PutPack(deep);
        Link(deep, t.Place());
        CheckRefused(t, {Id(deep)}, told);
        fs::remove(t.Place());
        for (const fs::path& p : {azahar, azahar / "textures-4k", home, home / "Packs", t.root}) {
            CheckRefused(t, {Id(p)}, told);
            CheckRefused(t, {Id(big), Id(p)}, told);
        }
    }
    SECTION("a pack outside the home folder") {
        const fs::path far = t.root / "srv" / "textures" / kTid;
        PutPack(far);
        t.paths.extra_roots.push_back((t.root / "srv" / "textures").generic_string());
        CheckRefused(t, {Id(far)}, told);
        t.paths.home.clear(); // not known: none
        CheckRefused(t, {Id(big)}, told);
        t.paths.home = Id(big); // never the home folder itself
        CheckRefused(t, {Id(big)}, told);
    }
    SECTION("a link in the home folder that leads out of it") {
        const fs::path far = t.root / "srv" / "textures";
        PutPack(far / kTid);
        Link(far, home / "far");
        t.paths.extra_roots = {(home / "far").generic_string()};
        REQUIRE(Listed(t).size() == 2);
        CheckRefused(t, {Id(far / kTid)}, told);
    }
    SECTION("a pair, and a pair of two places") {
        const fs::path copy = azahar / "textures-4k" / (kTid + "-astc4x4");
        PutCopy(copy, "../" + kTid);
        REQUIRE(Deletion::Delete(t.paths, kTitle, {Id(copy), Id(big)}, told).ok);
        CHECK_FALSE(fs::exists(copy));
        CHECK_FALSE(fs::exists(big));
    }
    SECTION("the roots file of the load folder") {
        // <load>/textures-4k through roots.txt: not another Azahar's
        const fs::path own = t.Load() / "textures-4k" / kTid;
        PutPack(own);
        t.paths.home = t.root.generic_string();
        t.paths.extra_roots = {(t.Load() / "textures-*").generic_string()};
        CHECK(Check(t, Id(own)).place == Place::Outside);
        CHECK_FALSE(Check(t, Id(own)).other_azahar);
    }
#ifndef _WIN32
    SECTION("its folder is not writable: nothing changes") {
        if (::geteuid() == 0) {
            SKIP("root may delete anything");
        }
        REQUIRE(Layers::TurnOn(t.paths, kTitle, Id(big)).ok);
        const auto before = Tree(big);
        const auto on = On(t);
        for (const fs::path& p : {azahar / "textures-4k", big}) {
            fs::permissions(p, fs::perms::owner_read | fs::perms::owner_exec);
            const auto r = Deletion::Delete(t.paths, kTitle, {Id(big)}, told);
            fs::permissions(p, fs::perms::owner_all);
            CHECK_FALSE(r.ok);
            CHECK_FALSE(r.changed);
            CHECK(On(t) == on);
            CHECK(Tree(big) == before);
        }
    }
    SECTION("a folder in it is not writable: not all deleted, listed, off") {
        if (::geteuid() == 0) {
            SKIP("root may delete anything");
        }
        REQUIRE(Layers::TurnOn(t.paths, kTitle, Id(big)).ok);
        fs::permissions(big / "UI", fs::perms::owner_read | fs::perms::owner_exec);
        const auto r = Deletion::Delete(t.paths, kTitle, {Id(big)}, told);
        fs::permissions(big / "UI", fs::perms::owner_all);
        CHECK_FALSE(r.ok);
        CHECK(r.changed);
        CHECK(On(t).empty());
        CHECK(Deletion::Delete(t.paths, kTitle, {Id(big)}, told).ok); // again
        CHECK_FALSE(fs::exists(big));
    }
#endif
}

TEST_CASE("texdelete: roots and odd names", "[texdelete]") {
    Temp t;
    PutPack(t.Library() / "Mine");
    const std::string id = Id(t.Library() / "Mine");

    CheckRefused(t, {});
    CheckRefused(t, {""});
    for (const std::string& bad :
         {id + "/", id + "/.", id + "/../Mine", id + "/UI", std::string("Mine"), std::string(".."),
          std::string("/"), Id(t.Library()), Id(t.Load()), Id(t.root),
          Id(t.Load() / "textures"), Id(t.Load() / "texture-packs"), Id(t.Place()),
          (t.Library() / ".." / kTid / "Mine").generic_string()}) {
        INFO(bad);
        CheckRefused(t, {bad});
        CheckRefused(t, {id, bad}); // one bad: none
    }
    std::string lower = id;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    CheckRefused(t, {lower}); // ids compare exactly

    SECTION("a root listed as a pack") {
        // an extra root that makes the scan list the library's own folder
        t.paths.extra_roots = {(t.Load() / "texture-packs").generic_string()};
        const auto listed = Listed(t);
        REQUIRE(std::find(listed.begin(), listed.end(), Id(t.Library())) != listed.end());
        CheckRefused(t, {Id(t.Library())});
        CHECK(Check(t, id).Ok());
    }
    SECTION("a pack that holds a root") {
        t.paths.extra_roots = {(t.Library() / "Mine" / "UI").generic_string()};
        CheckRefused(t, {id});
    }
    SECTION("a pack that is a root") {
        t.paths.extra_roots = {(t.Library() / "Mine").generic_string()};
        CheckRefused(t, {id});
    }
    SECTION("a pack that holds another pack") {
        PutPack(t.Library() / "Mine" / "inner" / kTid);
        Link(t.Library() / "Mine" / "inner" / kTid, t.Place());
        CheckRefused(t, {id});
        CheckRefused(t, {Id(t.Library() / "Mine" / "inner" / kTid)});
    }
    SECTION("a pack of another game") {
        const fs::path other = t.Load() / "texture-packs" / kOtherTid / "Theirs";
        PutPack(other);
        CheckRefused(t, {Id(other)});
        CHECK(Deletion::Delete(t.paths, kOther, {Id(other)}).ok);
    }
    SECTION("names with spaces, dots and other letters") {
        for (const char* name : {"My Pack 1.0", "..pack", "Pokémon", "a;b&c", "~"}) {
            const fs::path dir = t.Library() / fs::u8path(name);
            if (fs::u8path(name).generic_string().front() == '.') {
                PutPack(dir); // a hidden folder is not listed
                CheckRefused(t, {Id(dir)});
                continue;
            }
            PutPack(dir);
            CHECK(Deletion::Delete(t.paths, kTitle, {Id(dir)}).ok);
            CHECK_FALSE(fs::exists(dir));
        }
        CHECK(fs::exists(t.Library() / "Mine" / "UI"));
    }
}

TEST_CASE("texdelete: two games that share a pack", "[texdelete]") {
    Temp t;
    PutPack(t.Library() / "Mine");
    const std::string id = Id(t.Library() / "Mine");
    CHECK(Deletion::OtherGames(t.paths, kTitle, id).empty());

    SECTION("the other game's link") {
        Link(t.Library() / "Mine", t.Load() / "textures" / kOtherTid);
        CHECK(Deletion::OtherGames(t.paths, kTitle, id) == std::vector<u64>{kOther});
        CheckRefused(t, {id}); // only when the player was told
        REQUIRE(Deletion::Delete(t.paths, kTitle, {id}, {.shared = true}).ok);
        CHECK_FALSE(fs::exists(fs::symlink_status(t.Load() / "textures" / kOtherTid)));
    }
    SECTION("a lower pack of the other game's list") {
        const fs::path top = t.Load() / "texture-packs" / kOtherTid / "Top";
        PutPack(top);
        Link(top, t.Load() / "textures" / kOtherTid);
        REQUIRE(Layers::WriteSaved(t.paths.load_dir, kOther, {{{Id(top), ""}, {id, ""}}}));
        CHECK(Deletion::OtherGames(t.paths, kTitle, id) == std::vector<u64>{kOther});
        REQUIRE(Deletion::Delete(t.paths, kTitle, {id}, {.shared = true}).ok);
        CHECK(On(t, kOther) == std::vector<std::string>{Id(top)});
        CHECK(fs::exists(top));
    }
    SECTION("the other game's link to a folder in it") {
        Link(t.Library() / "Mine" / "UI", t.Load() / "textures" / kOtherTid);
        CheckRefused(t, {id});
        REQUIRE(Deletion::Delete(t.paths, kTitle, {id}, {.shared = true}).ok);
        CHECK_FALSE(fs::exists(fs::symlink_status(t.Load() / "textures" / kOtherTid)));
    }
    SECTION("the other game's link to the place this game reads") {
        fs::rename(t.Library() / "Mine", t.Place());
        Link(t.Place(), t.Load() / "textures" / kOtherTid);
        CheckRefused(t, {Id(t.Place())});
        REQUIRE(Deletion::Delete(t.paths, kTitle, {Id(t.Place())}, {.shared = true}).ok);
        CHECK_FALSE(fs::exists(fs::symlink_status(t.Load() / "textures" / kOtherTid)));
        CHECK_FALSE(fs::exists(fs::symlink_status(t.Place())));
    }
    SECTION("the other game's folder of packs is a link to this one's") {
        Link(t.Library(), t.Load() / "texture-packs" / kOtherTid);
        const auto before = Tree(t.root);
        CHECK_FALSE(Deletion::Delete(t.paths, kOther, {id}, {.shared = true}).ok);
        CHECK(Tree(t.root) == before);
        CHECK(Deletion::Delete(t.paths, kTitle, {id}).ok); // from its own game
    }
    SECTION("a link in the other game's folder of packs") {
        Link(t.Library() / "Mine", t.Load() / "texture-packs" / kOtherTid / "Shared");
        REQUIRE(Deletion::Delete(t.paths, kOther, {id}).ok);
        CHECK(fs::exists(t.Library() / "Mine" / "UI"));
    }
}

TEST_CASE("texdelete: a pack that cannot be deleted stays in the list", "[texdelete]") {
#ifdef _WIN32
    SKIP("folder permissions are POSIX");
#else
    if (::geteuid() == 0) {
        SKIP("root may delete anything");
    }
    Temp t;
    PutPack(t.Library() / "A");
    PutPack(t.Library() / "B");
    const std::string a = Id(t.Library() / "A"), b = Id(t.Library() / "B");
    REQUIRE(Layers::TurnOn(t.paths, kTitle, a).ok);

    SECTION("read-only") {
        fs::permissions(t.Library() / "A" / "UI", fs::perms::owner_read | fs::perms::owner_exec);
        fs::permissions(t.Library() / "A", fs::perms::owner_read | fs::perms::owner_exec);
        const auto r = Deletion::Delete(t.paths, kTitle, {a});
        CHECK_FALSE(r.ok);
        CHECK(r.changed); // off
        CHECK(On(t).empty());
        CHECK(Listed(t) == std::vector<std::string>{a, b});
        CHECK(fs::exists(t.Library() / "A" / "tex1_64x64_0123456789ABCDEF_14.png"));
    }
    SECTION("a read-only folder in it") {
        fs::permissions(t.Library() / "A" / "UI", fs::perms::owner_read | fs::perms::owner_exec);
        const auto r = Deletion::Delete(t.paths, kTitle, {a});
        CHECK_FALSE(r.ok);
        CHECK(Listed(t) == std::vector<std::string>{a, b}); // what is left of it
        CHECK(On(t).empty());
        fs::permissions(t.Library() / "A" / "UI", fs::perms::owner_all);
        CHECK(Deletion::Delete(t.paths, kTitle, {a}).ok); // again
        CHECK(Listed(t) == std::vector<std::string>{b});
    }
    SECTION("the second of a pair") {
        fs::permissions(t.Library() / "A" / "UI", fs::perms::owner_read | fs::perms::owner_exec);
        const auto r = Deletion::Delete(t.paths, kTitle, {b, a});
        CHECK_FALSE(r.ok);
        CHECK(r.changed);
        CHECK(Listed(t) == std::vector<std::string>{a});
    }
    SECTION("the game's link cannot change: nothing is deleted") {
        fs::permissions(t.Load() / "textures", fs::perms::owner_read | fs::perms::owner_exec);
        const auto r = Deletion::Delete(t.paths, kTitle, {a});
        fs::permissions(t.Load() / "textures", fs::perms::owner_all);
        CHECK_FALSE(r.ok);
        CHECK(fs::exists(t.Library() / "A" / "UI"));
        CHECK(On(t) == std::vector<std::string>{a});
    }
    for (const char* name : {"A", "A/UI"}) {
        std::error_code ec;
        fs::permissions(t.Library() / name, fs::perms::owner_all, ec);
    }
#endif
}

TEST_CASE("texdelete: not while the pack is converted", "[texdelete]") {
    Temp t;
    PutPack(t.Library() / "Mine"); // the textures are not PNGs: the converter keeps them
    PutPack(t.Library() / "Other");
    const std::string id = Id(t.Library() / "Mine");
    CHECK_FALSE(Convert::IsRunning(id));
    Convert::Request request;
    request.pack = id;
    request.jobs = 1;
    std::atomic_bool seen{false};
    Convert::Converter converter(request, [&](const Convert::Progress&) {
        if (seen.exchange(true)) {
            return;
        }
        CHECK(Convert::IsRunning(id));
        CHECK(Convert::IsRunning(Optimize::OptimizedPath(id)));
        const auto r = Deletion::Delete(t.paths, kTitle, {id});
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.changed);
        CHECK(Deletion::Delete(t.paths, kTitle, {Id(t.Library() / "Other")}).ok);
    });
    converter.Run();
    CHECK(seen);
    CHECK_FALSE(Convert::IsRunning(id));
    CHECK(fs::exists(t.Library() / "Mine" / "UI"));
    CHECK(Deletion::Delete(t.paths, kTitle, {id}).ok);
}

TEST_CASE("texdelete: listing and switching packs deletes nothing", "[texdelete]") {
    Temp t;
    PutPack(t.Library() / "A");
    PutCopy(t.Library() / "A-astc4x4", "../A");
    PutPack(t.Library() / "B");
    PutPack(t.Load() / "textures" / kOtherTid);
    REQUIRE(Download::WriteMarker((t.Library() / "B").string(), "some-pack", "gdrive", "2026-01-01"));
    const std::string a = Id(t.Library() / "A"), copy = Id(t.Library() / "A-astc4x4"),
                      b = Id(t.Library() / "B");
    const auto files = [&t] {
        std::map<std::string, std::string> out;
        for (auto& [path, text] : Tree(t.root)) {
            if (path.find("tex1_") != std::string::npos) {
                out[path.substr(path.find_last_of('/', path.find("tex1_")) + 1)] += text;
            }
        }
        return out;
    };
    const auto before = files();
    Download::Downloader dl(t.paths.load_dir, {});
    for (const std::string& id : {a, copy, b}) {
        REQUIRE(Layers::TurnOn(t.paths, kTitle, id).ok);
        ListPacks(t.paths, kTitle);
        for (const Pack& p : ListPacks(t.paths, kTitle)) {
            Check(t, p.id);
            Deletion::OtherGames(t.paths, kTitle, p.id);
        }
        dl.ListForTitle(kTitle);
        dl.ApplyFetchedCatalog(0, "offline", "");
        dl.ApplyFetchedCatalog(200, "", R"({"schema":1,"packs":[]})");
        Layers::LoaderFiles(t.paths.load_dir, kTitle);
        REQUIRE(Layers::TurnOff(t.paths, kTitle, id).ok);
    }
    REQUIRE(Layers::TurnOff(t.paths, kOther, Id(t.Load() / "textures" / kOtherTid)).ok);
    CHECK(files() == before);
    CHECK(fs::exists(t.Library() / "B" / ".azaharframe-download.json"));
}

TEST_CASE("texdelete: Downloader::Delete keeps to the downloaded ones", "[texdelete]") {
    Temp t;
    PutPack(t.Library() / "Mine");
    PutPack(t.Library() / "Down");
    REQUIRE(Download::WriteMarker((t.Library() / "Down").string(), "some-pack", "gdrive"));
    REQUIRE(Layers::TurnOn(t.paths, kTitle, Id(t.Library() / "Down")).ok);
    Download::Downloader dl(t.paths.load_dir, {});
    CHECK_FALSE(dl.Delete(kTitle, "Mine").ok);
    CHECK(dl.Delete(kTitle, "Down").ok);
    CHECK(On(t).empty());
    CHECK(Listed(t) == std::vector<std::string>{Id(t.Library() / "Mine")});
}
