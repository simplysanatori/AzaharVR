// Copyright 2026 AzaharVR contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// The Games tab's covers (core/frame_covers.h), against a fake server: no network.

#include <filesystem>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "core/frame_covers.h"

using namespace Core::Covers;
using Common::GameSystem;
using Cheats::Online::RawReply;

namespace {

const std::string kPng = std::string("\x89PNG\r\n\x1a\n", 8) + "rest of a png";
const std::string kJpeg = std::string("\xFF\xD8\xFF\xE0", 4) + "rest of a jpeg";

/// Answers with `reply`, and notes what it was asked.
struct FakeServer {
    RawReply reply;
    std::vector<std::string> asked;
    Get Getter() {
        return [this](const std::string& host, const std::string& path, std::size_t max) {
            asked.push_back(host + path);
            RawReply r = reply;
            if (r.body.size() > max) { // as HttpsGetRaw: the body is capped while it arrives
                r = {};
                r.error = "Download too large";
            }
            return r;
        };
    }
};

RawReply Reply(int status, std::string body = {}, std::string error = {}) {
    RawReply r;
    r.status = status;
    r.body = std::move(body);
    r.error = std::move(error);
    return r;
}

} // namespace

TEST_CASE("Covers: where a game's cover is", "[covers]") {
    CHECK_FALSE(SourceFor(GameSystem::GBA, ""));
    CHECK_FALSE(SourceFor(GameSystem::GB, ".."));
    CHECK_FALSE(SourceFor(GameSystem::GBA, "Pokemon Lazarus v1.2")); // no such cover: not asked
    CHECK_FALSE(SourceFor(GameSystem::GBA, "Zelda (USA) [!]"));
    const auto s = SourceFor(GameSystem::GBA, "Pokemon - Emerald Version (USA, Europe)");
    REQUIRE(s);
    CHECK(s->host == "https://raw.githubusercontent.com");
    CHECK(s->path == "/libretro-thumbnails/Nintendo_-_Game_Boy_Advance/master/Named_Boxarts/"
                     "Pokemon%20-%20Emerald%20Version%20%28USA%2C%20Europe%29.png");
    // libretro's names: those characters as _, nothing else of the name changed
    CHECK(ThumbnailName("A/B: C & D?") == "A_B_ C _ D_");
    const auto odd = SourceFor(GameSystem::DS, "50% #1 ../x (USA)");
    REQUIRE(odd);
    CHECK(odd->path.ends_with("/Named_Boxarts/50%25%20%231%20.._x%20%28USA%29.png"));
    CHECK(SourceFor(GameSystem::N3DS, "T (USA)")->path.find("Nintendo_-_Nintendo_3DS/") !=
          std::string::npos);
    CHECK(SourceFor(GameSystem::GB, "T (World)", Art::Title)->path.find("/Named_Titles/") !=
          std::string::npos);
    CHECK(SourceFor(GameSystem::GB, "T (World)", Art::Snap)->path.find("/Named_Snaps/") !=
          std::string::npos);
    CHECK(SourceFor(GameSystem::GB, "T (World)")->path.find("Nintendo_-_Game_Boy/") != std::string::npos);
    CHECK(SourceFor(GameSystem::GBC, "T (World)")->path.find("Nintendo_-_Game_Boy_Color/") !=
          std::string::npos);
}

TEST_CASE("Covers: what a reply is", "[covers]") {
    const Source s = *SourceFor(GameSystem::GBA, "Game (USA)");
    FakeServer server;
    const auto fetch = [&](RawReply reply) {
        server.reply = std::move(reply);
        return Fetch(server.Getter(), s);
    };
    using R = Fetched::Result;
    CHECK(fetch(Reply(200, kPng)).result == R::Got);
    CHECK(fetch(Reply(200, kJpeg)).bytes == kJpeg);
    CHECK(fetch(Reply(404)).result == R::NotThere);
    CHECK(fetch(Reply(200, "<html>not found</html>")).result == R::Failed); // wrong type
    CHECK(fetch(Reply(200, "")).result == R::Failed);
    CHECK(fetch(Reply(200, kPng + std::string(kMaxBytes, 'x'))).result == R::Failed); // too large
    CHECK(fetch(Reply(0, {}, "Could not establish connection")).result == R::Failed); // no network
    CHECK(fetch(Reply(500)).result == R::Failed);
    CHECK(fetch(Reply(0, {}, "Could not establish connection")).stop); // asks for no more
    CHECK(fetch(Reply(429, {}, "limited")).stop);
    CHECK_FALSE(fetch(Reply(500)).stop);
    CHECK_FALSE(fetch(Reply(404)).stop);
    CHECK_FALSE(fetch(Reply(200, kPng + std::string(kMaxBytes, 'x'))).stop);
    CHECK(fetch(Reply(301)).result == R::Failed); // no redirect followed
    CHECK(server.asked.back() == s.host + s.path);

    Source plain = s; // only https
    plain.host = "http://raw.githubusercontent.com";
    const auto before = server.asked.size();
    CHECK(Fetch(server.Getter(), plain).result == R::Failed);
    CHECK(server.asked.size() == before); // not even asked
}

namespace {

/// A covers folder of its own, gone at the end.
struct Folder {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("azaharvr-covers-test-" + std::to_string(std::time(nullptr)) + "-" +
         std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    Folder() {
        std::filesystem::remove_all(dir);
    }
    ~Folder() {
        std::filesystem::remove_all(dir);
    }
    std::string Path() const {
        return dir.string() + "/";
    }
};

const Game kA{GameSystem::GBA, 0x1111, "Game A (USA)"};

bool AnyImage(const std::string&) {
    return true;
}

} // namespace

TEST_CASE("Covers: downloads are kept and not asked for again", "[covers]") {
    Folder folder;
    const Game b{GameSystem::GB, 0x2222, "Game B (Japan)"};
    FakeServer server;
    server.reply = Reply(200, kPng);
    {
        Library library(folder.Path());
        CHECK(library.Shown(kA).empty());
        CHECK(library.Wanted(kA, 1000));
        CHECK(server.asked.empty()); // looking asks for nothing
        CHECK(Download(server.Getter(), library, kA, Art::Box, 1000, AnyImage) == Outcome::Got);
        CHECK(server.asked.size() == 1);
        CHECK_FALSE(library.Shown(kA).empty());
        CHECK_FALSE(library.Wanted(kA, 1000));
        CHECK(Download(server.Getter(), library, kA, Art::Box, 1000, AnyImage) == Outcome::Got);
        CHECK(server.asked.size() == 1); // there already

        server.reply = Reply(404);
        CHECK(Download(server.Getter(), library, b, Art::Box, 1000, AnyImage) == Outcome::NotThere);
        CHECK(Download(server.Getter(), library, b, Art::Box, 1001, AnyImage) == Outcome::NotThere);
        CHECK(server.asked.size() == 2); // known to be missing
    }
    Library again(folder.Path()); // the next start
    CHECK(!again.Shown(kA).empty());
    CHECK_FALSE(again.Wanted(b, 1000 + kRetryMissing - 1));
    CHECK(again.Wanted(b, 1000 + kRetryMissing)); // after a while
    server.reply = Reply(200, kJpeg);
    CHECK(Download(server.Getter(), again, b, Art::Box, 1000 + kRetryMissing, AnyImage) ==
          Outcome::Got);
    CHECK(Library(folder.Path()).Shown(b).ends_with("-box.png"));
}

TEST_CASE("Covers: no name of the source's, no key, no network, no image", "[covers]") {
    Folder folder;
    Library library(folder.Path());
    FakeServer server;
    server.reply = Reply(200, kPng);
    const Game hack{GameSystem::GBA, 0x1111, "Pokemon Lazarus"};
    const Game keyless{GameSystem::N3DS, 0, "Game A (USA)"};
    for (const Game& g : {hack, keyless}) {
        CHECK_FALSE(library.Wanted(g, 1000));
        CHECK(Download(server.Getter(), library, g, Art::Box, 1000, AnyImage) == Outcome::NotThere);
        CHECK(library.Shown(g).empty());
    }
    CHECK(server.asked.empty());

    server.reply = Reply(0, {}, "Could not establish connection");
    CHECK(Download(server.Getter(), library, kA, Art::Box, 1000, AnyImage) == Outcome::Stop);
    CHECK(library.Wanted(kA, 1000)); // tried again the next time

    server.reply = Reply(200, kPng); // a PNG's signature, but no image
    CHECK(Download(server.Getter(), library, kA, Art::Box, 1000,
                   [](const std::string&) { return false; }) == Outcome::NotThere);
    CHECK(library.Shown(kA).empty());
    CHECK_FALSE(library.Wanted(kA, 1001));
}

TEST_CASE("Covers: the player's choice wins and stays", "[covers]") {
    Folder folder;
    FakeServer server;
    server.reply = Reply(200, kPng);
    {
        Library library(folder.Path());
        library.Choose(kA, Choice::None); // No Cover, before any download
        CHECK_FALSE(library.Wanted(kA, 1000));
        library.Store(kA, Art::Box, kPng); // one that was under way
        CHECK(library.Shown(kA).empty());

        CHECK(library.SetOwn(kA, "GIF89a") == "not an image");
        CHECK(library.SetOwn(kA, kPng + std::string(kMaxOwnBytes, 'x')) == "too large");
        CHECK(library.ChoiceOf(kA) == Choice::None); // a refused image changes nothing
        CHECK(library.SetOwn(kA, kJpeg).empty());
    }
    {
        Library library(folder.Path());
        CHECK(library.ChoiceOf(kA) == Choice::Own);
        CHECK(library.Shown(kA).find("/own/gba/") != std::string::npos);
        CHECK_FALSE(library.Wanted(kA, 1000));
        library.Store(kA, Art::Box, kPng); // a later download
        CHECK(library.Shown(kA).find("/own/") != std::string::npos);

        library.Choose(kA, ChoiceFor(Art::Title));
        CHECK(library.Shown(kA).empty()); // not downloaded yet
        CHECK(Download(server.Getter(), library, kA, Art::Title, 1000, AnyImage) == Outcome::Got);
        CHECK(library.Shown(kA).ends_with("-title.png"));

        library.Choose(kA, Choice::Automatic); // Reset Cover
        CHECK(library.Shown(kA).ends_with("-box.png"));
    }
    CHECK(Library(folder.Path()).ChoiceOf(kA) == Choice::Automatic);
}

TEST_CASE("Covers: two games with one name, and two names of one key", "[covers]") {
    Folder folder;
    Library library(folder.Path());
    Game other = kA;
    other.key = 0x3333; // another game in a file of that name
    library.Store(kA, Art::Box, kPng);
    CHECK(library.Shown(other).empty());
    CHECK(library.SetOwn(other, kJpeg).empty());
    CHECK(library.Shown(kA).find("/libretro-thumbnails/") != std::string::npos);
    Game hack = kA; // the key covers the file's start: a hack can share its base game's
    hack.stem = "Game A Deluxe";
    CHECK(library.Shown(hack).empty());
    Game gb = kA;
    gb.system = GameSystem::GB;
    CHECK(gb.Id() != kA.Id());
}

TEST_CASE("Covers: image types", "[covers]") {
    CHECK(ImageOf(kPng) == Image::Png);
    CHECK(ImageOf(kJpeg) == Image::Jpeg);
    CHECK(ImageOf("GIF89a") == Image::None);
    CHECK(ImageOf("") == Image::None);
}
