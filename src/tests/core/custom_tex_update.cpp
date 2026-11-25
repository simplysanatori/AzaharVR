// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
//
// Updates of texture packs (core/custom_tex/update.h): when one is noticed, and the swap, on temp
// folders.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <system_error>
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
#include "core/custom_tex/parts.h"
#include "core/custom_tex/update.h"

namespace fs = std::filesystem;
using namespace CustomTex;
using Download::Entry;
using Download::Marker;
using Download::Source;

namespace {

constexpr u64 kTitle = 0x0004000000030200ULL;
const std::string kTid = "0004000000030200";
const std::string kId = "some-pack";
const std::string kA(64, 'a'), kB(64, 'b');

Entry MakeEntry(Source::Type type, const std::string& updated, const std::string& sha = {},
                u64 size = 0, const std::string& file_id = {}) {
    Entry e;
    e.id = kId;
    e.source.type = type;
    e.source.file_id = file_id;
    e.updated = updated;
    e.sha256 = sha;
    e.size = size;
    return e;
}

/// A marker from before updates were looked for
Marker Old(const std::string& source, const std::string& updated) {
    Marker m;
    m.id = kId;
    m.source = source;
    m.updated = updated;
    return m;
}

bool Newer(const Marker& m, const Entry& e) {
    return Update::Newer(m, e);
}

struct Temp {
    fs::path root;
    Paths paths;
    Temp() {
        static int n = 0;
        root = fs::weakly_canonical(fs::temp_directory_path()) /
               fmt::format("azf_texupdate_{}_{}", ::getpid(), n++);
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
    fs::path Pack() const {
        return Library() / "HD";
    }
    fs::path Copy() const {
        return Library() / "HD-astc4x4";
    }
    fs::path Staged() const {
        return Update::StagedPath(Pack().string());
    }
    fs::path OldOne() const {
        return Update::OldPath(Pack().string());
    }
};

void Put(const fs::path& file, const std::string& text = "x") {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string Text(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

Marker Version(const std::string& updated, const std::string& sha) {
    return Download::MarkerFor(MakeEntry(Source::Type::Gdrive, updated, sha, 1, "f"));
}

/// The pack with "old" or "new" in its textures, two parts
void PutPack(const fs::path& dir, const std::string& text, const Marker& marker) {
    Put(dir / "UI" / "tex1_64x64_0123456789ABCDEF_14.png", text);
    Put(dir / "Effects" / "tex1_32x32_0123456789ABCDE0_14.png", text);
    REQUIRE(Download::WriteMarker(dir.string(), marker));
}

void PutOld(const Temp& t) {
    PutPack(t.Pack(), "old", Version("2026-11-23", kA));
}
void PutStaged(const Temp& t) {
    PutPack(t.Staged(), "new", Version("2026-11-24", kB));
}
void PutCopy(const Temp& t, bool marker = true) {
    Put(t.Copy() / "UI" / "tex1_64x64_0123456789ABCDEF_14.ktx", "old");
    if (marker) {
        REQUIRE(Download::WriteMarker(t.Copy().string(), Version("2026-11-23", kA)));
    }
    Put(t.Copy() / Optimize::kMarker, R"({"format":"ASTC 4x4","source":"../HD"})");
}

/// 1x1, RGBA
constexpr unsigned char kPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
    0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x10,
    0x50, 0x30, 0xF8, 0x0F, 0x00, 0x02, 0x04, 0x01, 0x60, 0x8D, 0xBC, 0xBB, 0x71, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

void PutPngs(const fs::path& pack) {
    const std::string png(reinterpret_cast<const char*>(kPng), sizeof(kPng));
    Put(pack / "UI" / "tex1_64x64_0123456789ABCDEF_14.png", png);
    Put(pack / "Effects" / "tex1_32x32_0123456789ABCDE0_14.png", png);
}

Convert::Request FastRequest(const fs::path& pack) {
    Convert::Request request;
    request.pack = pack.string();
    request.quality = "fast";
    request.jobs = 1;
    request.low_priority = false;
    return request;
}

std::string Id(const fs::path& p) {
    return fs::weakly_canonical(p).generic_string();
}

std::string Version(const Temp& t) {
    return Text(t.Pack() / "UI" / "tex1_64x64_0123456789ABCDEF_14.png");
}

std::vector<std::string> On(const Temp& t) {
    return Layers::Packs(Layers::Current(t.paths, kTitle));
}

std::map<std::string, std::string> Tree(const fs::path& root) {
    std::map<std::string, std::string> out;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        const fs::file_status st = fs::symlink_status(e.path());
        out[e.path().lexically_relative(root).generic_string()] =
            Common::FS::IsLink(st)      ? "-> " + fs::read_symlink(e.path()).generic_string()
            : fs::is_regular_file(st) ? Text(e.path())
                                      : "";
    }
    return out;
}

std::vector<std::string> Listed(const Temp& t) {
    std::vector<std::string> out;
    for (const Pack& p : ListPacks(t.paths, kTitle)) {
        out.push_back(p.id);
    }
    return out;
}

} // namespace

TEST_CASE("texupdate: the version with the same or another SHA-256", "[texupdate]") {
    using T = Source::Type;
    const Entry e = MakeEntry(T::Gdrive, "2026-11-24", kB, 10, "f2");
    const auto marker = [](const std::string& updated, const std::string& sha) {
        return Download::MarkerFor(MakeEntry(T::Gdrive, updated, sha, 10, "f1"));
    };
    CHECK(Newer(marker("2026-11-23", kA), e));
    CHECK(Newer(marker("2026-11-24", kA), e));       // two releases on one day
    CHECK_FALSE(Newer(marker("2026-11-25", kA), e)); // an older list than the one it came from
    CHECK(Newer(marker("2026-10", kA), e));
    CHECK_FALSE(Newer(marker("2026-11", kA), e)); // which came first is not known
    CHECK_FALSE(Newer(marker("", kA), e));
    for (const char* date : {"2026-11-23", "2026-10", "2020-01-01", ""}) {
        CHECK_FALSE(Newer(marker(date, kB), e)); // the same file, whatever the dates
    }
    // The list that came with the app, older than the pack's: never
    const Entry built_in = MakeEntry(T::Gdrive, "2026-11", kA, 10, "f0");
    CHECK_FALSE(Newer(marker("2026-11-24", kB), built_in));
    CHECK_FALSE(Newer(marker("2026-12-01", kB), built_in));
    // Every source: the SHA-256 and the catalog's own date
    for (const T type : {T::GithubRelease, T::Https, T::Gdrive, T::GdriveFolder, T::Gamebanana}) {
        Marker m = Download::MarkerFor(MakeEntry(type, "2026-10", kA));
        m.updated = "2026-12-31"; // the source's own clock: not compared with the catalog's
        CHECK(Newer(m, MakeEntry(type, "2026-11", kB)));
        CHECK_FALSE(Newer(m, MakeEntry(type, "2026-10", kB)));
    }
}

TEST_CASE("texupdate: older markers, by date", "[texupdate]") {
    using T = Source::Type;
    const Entry e = MakeEntry(T::Gdrive, "2026-11-24", kB, 10, "f2");
    CHECK(Newer(Old("gdrive", "2026-11-23"), e));
    CHECK(Newer(Old("gdrive", "2026-11-22"), e));
    CHECK_FALSE(Newer(Old("gdrive", "2026-11-24"), e));
    CHECK_FALSE(Newer(Old("gdrive", "2026-11-30"), e)); // later than the catalog's
    CHECK_FALSE(Newer(Old("gdrive", "2026-11"), e));    // inside the month: not certain
    CHECK(Newer(Old("gdrive", "2026-10"), e));
    CHECK_FALSE(Newer(Old("gdrive", ""), e));
    CHECK_FALSE(Newer(Old("gdrive", "2026-10-05"), MakeEntry(T::Gdrive, "2026-10")));
    CHECK(Newer(Old("gdrive", "2026-10-05"), MakeEntry(T::Gdrive, "2026-11")));
    CHECK_FALSE(Newer(Old("gdrive", "2026-10"), MakeEntry(T::Gdrive, "")));
    CHECK(Newer(Old("https", "2026-10-05"), MakeEntry(T::Https, "2026-10-06")));
    // Another clock: the page's, the release's, the folder's
    CHECK_FALSE(Newer(Old("gamebanana", "2026-01-01"), MakeEntry(T::Gamebanana, "2026-11", "", 10)));
    CHECK_FALSE(Newer(Old("github", "2026-01-01"), MakeEntry(T::GithubRelease, "2026-11", "", 10)));
    CHECK_FALSE(Newer(Old("https", "2026-01-01"), MakeEntry(T::GdriveFolder, "2026-11", "", 10)));
    CHECK_FALSE(Newer(Old("github", "2026-01-01"), e)); // the entry has another source now
}

TEST_CASE("texupdate: newer markers without a SHA-256", "[texupdate]") {
    using T = Source::Type;
    const auto marker = [](T type, const std::string& updated, u64 size, const std::string& file = {}) {
        return Download::MarkerFor(MakeEntry(type, updated, "", size, file));
    };
    // A date and another size
    CHECK(Newer(marker(T::Gamebanana, "2026-10", 5), MakeEntry(T::Gamebanana, "2026-11", "", 6)));
    CHECK_FALSE(Newer(marker(T::Gamebanana, "2026-10", 5), MakeEntry(T::Gamebanana, "2026-11", "", 5)));
    CHECK_FALSE(Newer(marker(T::Gamebanana, "2026-10", 5), MakeEntry(T::Gamebanana, "2026-10", "", 6)));
    CHECK_FALSE(Newer(marker(T::Gamebanana, "2026-10", 0), MakeEntry(T::Gamebanana, "2026-11", "", 6)));
    CHECK(Newer(marker(T::GithubRelease, "2026-10", 5), MakeEntry(T::GithubRelease, "2026-11", "", 6)));
    CHECK_FALSE(Newer(marker(T::GdriveFolder, "2026-10", 5), MakeEntry(T::GdriveFolder, "2026-11", "", 6)));
    // The catalog's dates: a later one, unless it is plainly the same file
    CHECK(Newer(marker(T::Gdrive, "2026-11-01", 5, "f"), MakeEntry(T::Gdrive, "2026-11-02", "", 6, "f")));
    CHECK(Newer(marker(T::Gdrive, "2026-11-01", 5, "f"), MakeEntry(T::Gdrive, "2026-11-02", "", 5, "g")));
    CHECK_FALSE(Newer(marker(T::Gdrive, "2026-11-01", 5, "f"), MakeEntry(T::Gdrive, "2026-11-02", "", 5, "f")));
    CHECK_FALSE(Newer(marker(T::Https, "2026-11-01", 5), MakeEntry(T::Https, "2026-11-02", "", 5)));
    // One side has a SHA-256: the date
    CHECK(Newer(marker(T::Gdrive, "2026-11-01", 5, "f"), MakeEntry(T::Gdrive, "2026-11-02", kA, 6, "g")));
}

TEST_CASE("texupdate: other packs", "[texupdate]") {
    const Entry e = MakeEntry(Source::Type::Gdrive, "2026-11-24", kB, 10, "f2");
    Marker zip = Old("zip", "2020-01-01");
    zip.id = "zip:HD";
    CHECK_FALSE(Newer(zip, e));
    Marker other = Old("gdrive", "2020-01-01");
    other.id = "other-pack";
    CHECK_FALSE(Newer(other, e));
    CHECK_FALSE(Newer({}, {}));
    // Several copies: the newest decides
    CHECK_FALSE(Update::NewerThanAll({}, e));
    CHECK(Update::NewerThanAll({Old("gdrive", "2026-11-22"), Old("gdrive", "2026-11-23")}, e));
    CHECK_FALSE(Update::NewerThanAll({Old("gdrive", "2026-11-22"), Version("2026-11-24", kB)}, e));
}

TEST_CASE("texupdate: markers", "[texupdate]") {
    Temp t;
    const std::string dir = t.Library().string();
    CHECK_FALSE(Download::ReadMarker(dir));
    Entry e = MakeEntry(Source::Type::Gamebanana, "2026-11", kA, 5, "");
    e.live_updated = "2026-11-20";
    e.aspect = "16:9";
    REQUIRE(Download::WriteMarker(dir, Download::MarkerFor(e)));
    const auto m = Download::ReadMarker(dir);
    REQUIRE(m);
    CHECK(m->updated == "2026-11-20");
    CHECK(m->catalog_updated == "2026-11");
    CHECK(m->sha256 == kA);
    CHECK(m->size == 5);
    CHECK(m->source == "gamebanana");
    CHECK(Download::MarkerId(dir) == kId);
    CHECK(Download::MarkerAspect(dir) == "16:9");
    REQUIRE(Download::WriteMarker(dir, kId, "gdrive", "2026-11"));
    CHECK(Download::ReadMarker(dir) == Old("gdrive", "2026-11"));
    for (const char* bad : {"", "{", "[]", "1", R"({"id":5,"size":"x"})"}) {
        Put(t.Library() / ".azaharframe-download.json", bad);
        const auto r = Download::ReadMarker(dir);
        CHECK((!r || r->id.empty()));
    }
}

TEST_CASE("texupdate: the list notices it, with the packs that are there", "[texupdate]") {
    Temp t;
    Entry e = MakeEntry(Source::Type::Gdrive, "2026-11-24", kB, 1, "f");
    e.title_ids = {kTitle};
    e.name = "HD";
    Download::Catalog cat;
    cat.packs.push_back(e);
    const Download::Downloader dl(t.paths.load_dir, cat);
    const auto info = [&] { return dl.ListForTitle(kTitle).at(0); };
    CHECK_FALSE(info().update);
    PutOld(t);
    CHECK(info().update);
    CHECK(info().installed);
    CHECK(info().updated == "2026-11-24"); // the new one's date
    PutCopy(t);
    CHECK(info().update); // the copy has the marker too
    CHECK(info().folders == std::vector<std::string>{"HD", "HD-astc4x4"});

    // The update waits: it is the installed version, and no pack of its own
    PutStaged(t);
    CHECK_FALSE(info().update);
    CHECK(info().staged);
    CHECK(info().folders == std::vector<std::string>{"HD", "HD-astc4x4"});
    CHECK(Listed(t) == std::vector<std::string>{Id(t.Pack()), Id(t.Copy())});
    Put(t.Staged() / ".azaharframe-download.json", "");
    CHECK(info().update); // not complete

    fs::remove_all(t.Staged());
    fs::remove_all(t.Pack()); // the original was removed: the copy's marker
    CHECK(info().update);
    REQUIRE(Download::WriteMarker(t.Copy().string(), Version("2026-11-24", kB)));
    CHECK_FALSE(info().update);
    CHECK(info().updated == "2026-11-24");
}

TEST_CASE("texupdate: which folder is replaced", "[texupdate]") {
    Temp t;
    const auto target = [&] { return Update::TargetFolder(t.paths, kTitle, kId); };
    CHECK(target().empty());
    PutPack(t.Library() / "Mine", "mine", Old("gdrive", "2026-01-01"));
    fs::remove(t.Library() / "Mine" / ".azaharframe-download.json");
    CHECK(target().empty()); // the player's own
    PutOld(t);
    CHECK(target() == "HD");
    PutCopy(t);
    CHECK(target() == "HD");
    CHECK(Update::TargetFolder(t.paths, kTitle, "other").empty());
    CHECK(Update::TargetFolder(t.paths, kTitle, "zip:HD").empty());
    CHECK(Update::TargetFolder(t.paths, kTitle, "").empty());
    // An older one of the same download: the one that is on
    PutPack(t.Library() / "HD (2)", "old", Version("2026-11-23", kA));
    REQUIRE(Layers::TurnOn(t.paths, kTitle, Id(t.Library() / "HD (2)")).ok);
    CHECK(target() == "HD (2)");
    REQUIRE(Layers::TurnOn(t.paths, kTitle, Id(t.Copy())).ok);
    CHECK(target() == "HD");
    fs::remove_all(t.Library() / "HD (2)");
    // The original was removed: its name
    fs::remove_all(t.Pack());
    CHECK(target() == "HD");
    Put(t.Copy() / Optimize::kMarker, R"({"source":"../../../../elsewhere/HD"})");
    CHECK(target().empty());

    // Through a link: never
    const fs::path away = t.root / "away";
    fs::rename(t.Library(), away);
    std::error_code ec;
    Common::FS::CreateFolderLink(away, t.Library(), ec);
    REQUIRE_FALSE(ec);
    PutPack(away / "HD", "old", Version("2026-11-23", kA));
    CHECK(target().empty());
    Download::Catalog cat;
    cat.packs.push_back(MakeEntry(Source::Type::Gdrive, "2026-11-24", kB, 1, "f"));
    cat.packs[0].title_ids = {kTitle};
    Download::Downloader dl(t.paths.load_dir, cat);
    CHECK_FALSE(dl.Start(kId, kTitle, false, false, true)); // refused before any request
    CHECK_FALSE(dl.GetState().Busy());
}

TEST_CASE("texupdate: the new version takes the old one's place", "[texupdate]") {
    Temp t;
    PutOld(t);
    PutStaged(t);
    PutPack(t.Library() / "Under", "u", Old("gdrive", ""));
    PutPack(t.Library() / "Over", "o", Old("gdrive", ""));
    const std::string hd = Id(t.Pack()), under = Id(t.Library() / "Under"),
                      over = Id(t.Library() / "Over");

    SECTION("off") {
        const auto r = Update::Apply(t.paths, kTitle);
        CHECK(r.error.empty());
        CHECK(r.packs == std::vector<std::string>{hd});
        CHECK(Version(t) == "new");
        CHECK(On(t).empty());
        CHECK(fs::exists(t.OldOne())); // until Finish
        Update::Finish(t.paths, kTitle);
        CHECK_FALSE(fs::exists(t.OldOne()));
        CHECK_FALSE(fs::exists(t.Staged()));
        CHECK(Update::Apply(t.paths, kTitle).packs.empty());
        CHECK(Version(t) == "new");
    }
    SECTION("on, its place in the list") {
        for (const std::string& p : {under, hd, over}) {
            REQUIRE(Layers::TurnOn(t.paths, kTitle, p).ok);
        }
        const auto before = Layers::Current(t.paths, kTitle);
        REQUIRE(Layers::Packs(before) == std::vector<std::string>{over, hd, under});
        CHECK(Update::Waiting(t.paths) == std::vector<u64>{kTitle});
        CHECK(Update::Apply(t.paths, kTitle).error.empty());
        Update::Finish(t.paths, kTitle);
        CHECK(Layers::Current(t.paths, kTitle) == before);
        CHECK(Layers::Linked(t.paths, kTitle) == over);
        CHECK(Version(t) == "new");
        CHECK(Update::Waiting(t.paths).empty());
    }
    SECTION("the one the game's link points at") {
        REQUIRE(Layers::TurnOn(t.paths, kTitle, hd).ok);
        CHECK(Update::Apply(t.paths, kTitle).error.empty());
        CHECK(Layers::Linked(t.paths, kTitle) == hd);
        CHECK(Text(t.Load() / "textures" / kTid / "UI" / "tex1_64x64_0123456789ABCDEF_14.png") == "new");
    }
    SECTION("the Customize choices") {
        Parts::State state = Parts::ReadState(hd);
        const Parts::Layout layout = Parts::Scan(hd, state.hints);
        Parts::SetGroup(state, layout, *layout.FindGroup("Effects"), false);
        REQUIRE(Parts::WriteState(hd, state));
        Parts::Hints hints;
        hints.off = {"Extra*"};
        REQUIRE(Parts::WriteHints(t.Staged().string(), hints));
        CHECK(Update::Apply(t.paths, kTitle).error.empty());
        const Parts::State now = Parts::ReadState(hd);
        CHECK(now.hints == hints); // the new version's own
        CHECK(now.set == state.set);
        CHECK(Parts::GroupValue(layout, now, *layout.FindGroup("Effects")) == 0);
        CHECK(Parts::GroupValue(layout, now, *layout.FindGroup("UI")) == 2);
    }
}

TEST_CASE("texupdate: the optimized copy", "[texupdate]") {
    Temp t;
    const std::string hd = Id(t.Pack()), copy = Id(t.Copy());
    for (const bool marker : {true, false}) {
        DYNAMIC_SECTION((marker ? "with" : "without") << " a marker, on") {
            PutOld(t);
            PutCopy(t, marker);
            PutStaged(t);
            REQUIRE(Layers::TurnOn(t.paths, kTitle, copy).ok);
            Put(t.Copy() / Parts::kStateFile, R"({"set":true,"off":["Effects"]})");
            const auto r = Update::Apply(t.paths, kTitle);
            CHECK(r.error.empty());
            CHECK(r.stale == std::vector<std::string>{copy});
            CHECK(On(t) == std::vector<std::string>{hd}); // never the old copy
            CHECK(Optimize::IsStale(copy));
            CHECK(Version(t) == "new");
            CHECK(fs::exists(t.Copy() / "UI")); // nothing converts or goes by itself
            CHECK(Parts::ReadState(hd) == Parts::ReadState(copy)); // the choices of the one used
            Update::Finish(t.paths, kTitle);
            CHECK(fs::exists(t.Copy() / "UI"));
            // One row, the original's, on or off
            Layers::Game game = Layers::Game::Load(t.paths, kTitle);
            CHECK(game.stale.count(copy) == 1);
            REQUIRE(game.PackRows().size() == 1);
            CHECK(game.packs[game.PackRows()[0].shown].id == hd);
            game.SetOn(hd, false);
            REQUIRE(game.PackRows().size() == 1);
            CHECK(game.packs[game.PackRows()[0].shown].id == hd);
        }
    }
    SECTION("off") {
        PutOld(t);
        PutCopy(t);
        PutStaged(t);
        CHECK(Update::Apply(t.paths, kTitle).stale == std::vector<std::string>{copy});
        CHECK(On(t).empty());
    }
    SECTION("its original was removed: it comes back") {
        PutCopy(t);
        PutStaged(t);
        REQUIRE(Layers::TurnOn(t.paths, kTitle, copy).ok);
        const auto r = Update::Apply(t.paths, kTitle);
        CHECK(r.error.empty());
        CHECK(r.packs == std::vector<std::string>{hd});
        CHECK(On(t) == std::vector<std::string>{hd});
        CHECK(Optimize::IsStale(copy));
        CHECK(Version(t) == "new");
        Update::Finish(t.paths, kTitle);
        CHECK(Version(t) == "new");
    }
    SECTION("made again: from nothing") {
        PutOld(t);
        PutCopy(t);
        Put(t.Copy() / "Gone" / "tex1_8x8_0123456789ABCDE1_14.ktx");
        PutStaged(t);
        PutPngs(t.Staged());
        Update::Apply(t.paths, kTitle);
        REQUIRE(Optimize::IsStale(copy));
        std::string why;
        Convert::EstimatePack(FastRequest(hd), &why); // what the question is asked with: nothing changes
        CHECK(fs::exists(t.Copy() / "Gone"));
        REQUIRE(Optimize::IsStale(copy));
        const Convert::Result made = Convert::Converter(FastRequest(hd)).Run();
        CHECK(made.code == Convert::kOk);
        CHECK(made.output == copy);
        CHECK(fs::exists(t.Copy() / "UI" / "tex1_64x64_0123456789ABCDEF_14.ktx"));
        CHECK_FALSE(fs::exists(t.Copy() / "Gone"));
        CHECK_FALSE(Optimize::IsStale(copy));
        CHECK(Download::ReadMarker(copy) == Download::ReadMarker(hd));
    }
    SECTION("a copy that is not stale is carried on with") {
        PutOld(t);
        PutCopy(t);
        PutPngs(t.Pack());
        Put(t.Copy() / "Extra" / "note.txt");
        CHECK(Convert::Converter(FastRequest(hd)).Run().code == Convert::kOk);
        CHECK(fs::exists(t.Copy() / "Extra"));
    }
    SECTION("the stale mark") {
        PutCopy(t);
        CHECK_FALSE(Optimize::IsStale(copy));
        REQUIRE(Optimize::MarkStale(copy, true));
        CHECK(Optimize::IsStale(copy));
#ifdef _WIN32 // AzaharVR: the same folder, written with Windows' separator (as in 1.3.0)
        REQUIRE(Optimize::SourceOf(copy));
        CHECK(fs::path(*Optimize::SourceOf(copy)).lexically_normal() ==
              fs::path(hd).lexically_normal());
#else
        CHECK(Optimize::SourceOf(copy) == hd);
#endif
        REQUIRE(Optimize::MarkStale(copy, false));
        CHECK_FALSE(Optimize::IsStale(copy));
        CHECK_FALSE(Optimize::MarkStale(t.Library().string(), true));
        CHECK_FALSE(Optimize::IsStale(t.Library().string()));
    }
}

TEST_CASE("texupdate: what is not certain stays as it is", "[texupdate]") {
    Temp t;
    const auto unchanged = [&] {
        const auto before = Tree(t.root);
        CHECK(Update::Apply(t.paths, kTitle).packs.empty());
        Update::Finish(t.paths, kTitle);
        CHECK(Tree(t.root) == before);
    };
    SECTION("nothing waits") {
        PutOld(t);
        unchanged();
    }
    SECTION("not complete: no marker, a bad one, no textures, another download's, a link") {
        PutOld(t);
        PutStaged(t);
        const fs::path marker = t.Staged() / ".azaharframe-download.json";
        fs::remove(marker);
        unchanged();
        Put(marker, "{");
        unchanged();
        REQUIRE(Download::WriteMarker(t.Staged().string(), "other-pack", "gdrive", "2026-11-24"));
        unchanged();
        fs::remove_all(t.Staged());
        fs::create_directories(t.Staged());
        REQUIRE(Download::WriteMarker(t.Staged().string(), Version("2026-11-24", kB)));
        unchanged();
        fs::remove_all(t.Staged());
        PutPack(t.root / "away", "new", Version("2026-11-24", kB));
        std::error_code ec;
        Common::FS::CreateFolderLink(t.root / "away", t.Staged(), ec);
        REQUIRE_FALSE(ec);
        unchanged();
    }
    SECTION("older than the pack, or the same") {
        PutPack(t.Pack(), "newest", Version("2026-11-25", std::string(64, 'c')));
        PutStaged(t);
        unchanged();
        PutPack(t.Pack(), "same", Version("2026-11-24", kB));
        unchanged();
    }
    SECTION("beside the player's own pack") {
        PutOld(t);
        fs::remove(t.Pack() / ".azaharframe-download.json");
        PutStaged(t);
        unchanged();
    }
    SECTION("no pack and no copy of it") {
        PutStaged(t);
        unchanged();
    }
    SECTION("the pack is a link") {
        PutPack(t.root / "away", "old", Version("2026-11-23", kA));
        std::error_code ec;
        Common::FS::CreateFolderLink(t.root / "away", t.Pack(), ec);
        REQUIRE_FALSE(ec);
        PutStaged(t);
        unchanged();
    }
    SECTION("all three") {
        PutOld(t);
        PutStaged(t);
        PutPack(t.OldOne(), "older", Old("gdrive", "2026-11-23"));
        fs::remove_all(t.Pack());
        fs::create_directories(t.Pack()); // not a pack
        unchanged();
    }
    SECTION("the new one is no pack: the old one is not deleted") {
        PutPack(t.OldOne(), "old", Version("2026-11-23", kA));
        fs::create_directories(t.Pack());
        unchanged();
        REQUIRE(Download::WriteMarker(t.Pack().string(), Version("2026-11-24", kB)));
        unchanged(); // no textures
    }
    SECTION("the old one is of another download, or the same version") {
        PutPack(t.Pack(), "new", Version("2026-11-24", kB));
        Marker other = Version("2026-11-23", kA);
        other.id = "other-pack";
        PutPack(t.OldOne(), "old", other);
        unchanged();
        PutPack(t.OldOne(), "old", Version("2026-11-24", kB));
        unchanged();
    }
    SECTION("names that are not ours") {
        PutOld(t);
        for (const char* name : {".azaharvr-update", "..azaharvr-old", "HD.azaharvr-update", ".HD.update"}) {
            PutPack(t.Library() / name, "x", Version("2026-11-24", kB));
        }
        CHECK_FALSE(Update::IsStagedName(".azaharvr-update"));
        CHECK_FALSE(Update::IsStagedName("HD.azaharvr-update"));
        CHECK(Update::IsStagedName(".HD.azaharvr-update"));
        unchanged();
    }
}

TEST_CASE("texupdate: cut off by a crash", "[texupdate]") {
    Temp t;
    SECTION("between the renames: finished, with both leftovers there") {
        PutPack(t.OldOne(), "old", Version("2026-11-23", kA));
        PutStaged(t);
        CHECK(Update::Apply(t.paths, kTitle).packs == std::vector<std::string>{Id(t.Pack())});
        CHECK(Version(t) == "new");
        CHECK(fs::exists(t.OldOne()));
        Update::Finish(t.paths, kTitle);
        CHECK_FALSE(fs::exists(t.OldOne()));
        CHECK(Version(t) == "new");
    }
    SECTION("between the renames, the new one is not complete: nothing") {
        PutPack(t.OldOne(), "old", Version("2026-11-23", kA));
        PutStaged(t);
        fs::remove(t.Staged() / ".azaharframe-download.json");
        const auto before = Tree(t.root);
        CHECK_FALSE(Update::Apply(t.paths, kTitle).error.empty());
        Update::Finish(t.paths, kTitle);
        CHECK(Tree(t.root) == before);
    }
    SECTION("the new one is gone: the old one is put back") {
        PutPack(t.OldOne(), "old", Version("2026-11-23", kA));
        CHECK(Update::Apply(t.paths, kTitle).packs.empty());
        Update::Finish(t.paths, kTitle);
        CHECK(Version(t) == "old");
        CHECK_FALSE(fs::exists(t.OldOne()));
    }
    SECTION("after the renames: the old one goes") {
        PutPack(t.Pack(), "new", Version("2026-11-24", kB));
        PutPack(t.OldOne(), "old", Version("2026-11-23", kA));
        CHECK(Update::Apply(t.paths, kTitle).packs.empty());
        CHECK(fs::exists(t.OldOne()));
        Update::Finish(t.paths, kTitle);
        CHECK_FALSE(fs::exists(t.OldOne()));
        CHECK(Version(t) == "new");
    }
}

TEST_CASE("texupdate: a rename that fails leaves the old pack", "[texupdate]") {
#ifdef _WIN32
    SKIP("folder permissions are POSIX");
#else
    if (::geteuid() == 0) {
        SKIP("root may rename anything");
    }
    Temp t;
    PutOld(t);
    PutCopy(t);
    PutStaged(t);
    const std::string copy = Id(t.Copy());
    REQUIRE(Layers::TurnOn(t.paths, kTitle, copy).ok);
    const auto before = Layers::Current(t.paths, kTitle);
    // The pack's folder cannot be renamed (as on Windows while a file in it is open). The library
    // itself is not writable then, so the list cannot be saved either; the list's own file is made
    // first, for the first of the two
    fs::permissions(t.Library(), fs::perms::owner_read | fs::perms::owner_exec);
    const auto r = Update::Apply(t.paths, kTitle);
    fs::permissions(t.Library(), fs::perms::owner_all);
    CHECK_FALSE(r.error.empty());
    CHECK(r.packs.empty());
    CHECK(Version(t) == "old");
    CHECK(fs::exists(t.Staged()));
    CHECK(Layers::Current(t.paths, kTitle) == before);
    CHECK_FALSE(Optimize::IsStale(copy));
    Update::Finish(t.paths, kTitle);
    CHECK(fs::exists(t.Pack()));
    // Again, when it can be
    CHECK(Update::Apply(t.paths, kTitle).error.empty());
    CHECK(Version(t) == "new");
    CHECK(On(t) == std::vector<std::string>{Id(t.Pack())});
#endif
}

TEST_CASE("texupdate: something in the old one's hidden place", "[texupdate]") {
    Temp t;
    PutOld(t);
    PutStaged(t);
    Put(t.OldOne()); // a file: the second folder cannot go there
    Update::Apply(t.paths, kTitle);
    CHECK(Version(t) == "old");
    CHECK(fs::exists(t.Staged()));
}

TEST_CASE("texupdate: deleting a pack takes its waiting update along", "[texupdate]") {
    Temp t;
    PutOld(t);
    PutCopy(t);
    PutStaged(t);
    PutPack(t.Library() / "HD 2", "other", Old("gdrive", ""));
    PutPack(Update::StagedPath((t.Library() / "HD 2").string()), "other", Old("gdrive", ""));
    SECTION("the pair") {
        REQUIRE(Deletion::Delete(t.paths, kTitle, {Id(t.Pack()), Id(t.Copy())}).ok);
        CHECK_FALSE(fs::exists(t.Staged()));
    }
    SECTION("the copy, its original removed before") {
        fs::remove_all(t.Pack());
        REQUIRE(Deletion::Delete(t.paths, kTitle, {Id(t.Copy())}).ok);
        CHECK_FALSE(fs::exists(t.Staged()));
    }
    SECTION("the copy only: the original's stays") {
        REQUIRE(Deletion::Delete(t.paths, kTitle, {Id(t.Copy())}).ok);
        CHECK(fs::exists(t.Staged()));
    }
    CHECK(fs::exists(Update::StagedPath((t.Library() / "HD 2").string())));
}

TEST_CASE("texupdate: nothing is changed without the player", "[texupdate]") {
    Temp t;
    PutOld(t);
    PutCopy(t);
    PutPack(t.Library() / "Mine", "mine", Old("gdrive", "2020-01-01"));
    fs::remove(t.Library() / "Mine" / ".azaharframe-download.json");
    REQUIRE(Layers::TurnOn(t.paths, kTitle, Id(t.Copy())).ok);
    REQUIRE(Layers::TurnOn(t.paths, kTitle, Id(t.Library() / "Mine")).ok);
    const auto before = Tree(t.root);

    // Opening the pages, with an update in the list
    Entry e = MakeEntry(Source::Type::Gdrive, "2026-11-24", kB, 1, "f");
    e.title_ids = {kTitle};
    Download::Catalog cat;
    cat.packs.push_back(e);
    Download::Downloader dl(t.paths.load_dir, cat);
    REQUIRE(dl.ListForTitle(kTitle).at(0).update);
    const Layers::Game game = Layers::Game::Load(t.paths, kTitle);
    CHECK(game.PackRows().size() == 2);
    for (const Pack& p : game.packs) {
        CHECK(Deletion::Check(t.paths, kTitle, game.packs, p.id).Ok());
        CHECK(Deletion::OtherGames(t.paths, kTitle, p.id).empty());
        CHECK_FALSE(Optimize::IsStale(p.path));
    }
    CHECK(Tree(t.root) == before);
    // The list again: another one, an empty one, one that cannot be read
    dl.SetCatalog(cat);
    CHECK_FALSE(Download::ParseCatalog("<html>"));
    dl.SetCatalog({});
    CHECK(dl.ListForTitle(kTitle).empty());
    CHECK(Tree(t.root) == before);
    // Games starting and stopping
    for (int i = 0; i < 3; i++) {
        CHECK(Update::Waiting(t.paths).empty());
        CHECK(Update::Apply(t.paths, kTitle).packs.empty());
        Update::Finish(t.paths, kTitle);
    }
    CHECK(Tree(t.root) == before);
    // What is newer is not downloaded by being listed, and what is not newer not at all
    dl.SetCatalog(cat);
    Entry older = e;
    older.updated = "2026-11-01";
    cat.packs[0] = older;
    dl.SetCatalog(cat);
    CHECK_FALSE(dl.Start(kId, kTitle, false, false, true));
    CHECK(Tree(t.root) == before);
}
