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
    CHECK_FALSE(SourceFor(GameSystem::N3DS, "Game (USA)"));
    CHECK_FALSE(SourceFor(GameSystem::GBA, ""));
    CHECK_FALSE(SourceFor(GameSystem::GB, ".."));
    CHECK_FALSE(SourceFor(GameSystem::GBA, "Pokemon Lazarus v1.2")); // no such cover: not asked
    CHECK_FALSE(SourceFor(GameSystem::GBA, "Zelda (USA) [!]"));
    const auto s = SourceFor(GameSystem::GBA, "Pokemon - Emerald Version (USA, Europe)");
    REQUIRE(s);
    CHECK(s->host == "https://raw.githubusercontent.com");
    CHECK(s->path == "/libretro-thumbnails/Nintendo_-_Game_Boy_Advance/master/Named_Boxarts/"
                     "Pokemon%20-%20Emerald%20Version%20%28USA%2C%20Europe%29.png");
    CHECK(s->key == "Nintendo_-_Game_Boy_Advance/Pokemon - Emerald Version (USA, Europe).png");
    // libretro's names: those characters as _, nothing else of the name changed
    CHECK(ThumbnailName("A/B: C & D?") == "A_B_ C _ D_");
    const auto odd = SourceFor(GameSystem::DS, "50% #1 ../x (USA)");
    REQUIRE(odd);
    CHECK(odd->path.ends_with("/Named_Boxarts/50%25%20%231%20.._x%20%28USA%29.png"));
    CHECK(odd->key.find('/') == odd->key.rfind('/')); // never outside the system's folder
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

TEST_CASE("Covers: the cache remembers", "[covers]") {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("azaharvr-covers-test-" + std::to_string(std::time(nullptr)));
    std::filesystem::remove_all(dir);
    const std::string root = dir.string() + "/";
    const Source a = *SourceFor(GameSystem::GBA, "Game A (USA)");
    const Source b = *SourceFor(GameSystem::GB, "Game B (Japan)");
    {
        Cache cache(root);
        CHECK(cache.FileOf(a).empty());
        CHECK(cache.Store(a, kPng));
        CHECK_FALSE(cache.FileOf(a).empty());
        CHECK_FALSE(cache.KnownMissing(b, 1000));
        cache.MarkMissing(b, 1000);
        CHECK(cache.KnownMissing(b, 1000 + kRetryMissing - 1));
        CHECK_FALSE(cache.KnownMissing(b, 1000 + kRetryMissing)); // asked for again after a while
    }
    {
        Cache again(root); // the next start
        CHECK(again.KnownMissing(b, 2000));
        CHECK(!again.FileOf(a).empty());
        CHECK(again.Store(b, kJpeg)); // found after all
        CHECK_FALSE(again.KnownMissing(b, 2000));
    }
    CHECK_FALSE(Cache(root).KnownMissing(b, 2000));
    std::filesystem::remove_all(dir);
}

TEST_CASE("Covers: image types", "[covers]") {
    CHECK(ImageOf(kPng) == Image::Png);
    CHECK(ImageOf(kJpeg) == Image::Jpeg);
    CHECK(ImageOf("GIF89a") == Image::None);
    CHECK(ImageOf("") == Image::None);
}
