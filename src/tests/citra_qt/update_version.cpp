// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharVR updatecheck: parsing and comparing versions, and reading the release reply (all from
// strings; no network).

#include <atomic>
#include <chrono>
#include <catch2/catch_test_macros.hpp>
#include "citra_qt/update_checker.h"
#include "citra_qt/update_version.h"

using namespace UpdateChecker;

TEST_CASE("Update check: release tags", "[update]") {
    const auto v = ParseTag("1.3.0");
    REQUIRE(v);
    CHECK(v->major == 1);
    CHECK(v->minor == 3);
    CHECK(v->patch == 0);
    CHECK(v->ahead == 0);
    CHECK(ParseTag("v1.3.0"));
    CHECK(ParseTag("v10.20.30"));
    CHECK(ParseTag("0.0.1"));
    CHECK(ParseTag("V1.3.0") == std::nullopt);
    CHECK(ParseTag("") == std::nullopt);
    CHECK(ParseTag("v") == std::nullopt);
    CHECK(ParseTag("1.3") == std::nullopt);
    CHECK(ParseTag("2120.1") == std::nullopt); // an upstream tag
    CHECK(ParseTag("1.3.0.1") == std::nullopt);
    CHECK(ParseTag("1.3.0-rc1") == std::nullopt);
    CHECK(ParseTag("1.3.0-beta") == std::nullopt);
    CHECK(ParseTag("1.3.0+build5") == std::nullopt);
    CHECK(ParseTag("1.3.0-118-g204b92480") == std::nullopt); // a build, not a tag
    CHECK(ParseTag("1.3.0 ") == std::nullopt);
    CHECK(ParseTag(" 1.3.0") == std::nullopt);
    CHECK(ParseTag("1..0") == std::nullopt);
    CHECK(ParseTag("a.b.c") == std::nullopt);
    CHECK(ParseTag("01.3.0") == std::nullopt);
    CHECK(ParseTag("1.3.-1") == std::nullopt);
    CHECK(ParseTag("1.3.99999999999") == std::nullopt); // too long
    CHECK(ParseTag("latest") == std::nullopt);
}

TEST_CASE("Update check: builds", "[update]") {
    const auto r = ParseBuild("1.3.0");
    REQUIRE(r);
    CHECK(r->ahead == 0);
    const auto d = ParseBuild("v1.3.0-118-g204b92480");
    REQUIRE(d);
    CHECK(d->major == 1);
    CHECK(d->minor == 3);
    CHECK(d->patch == 0);
    CHECK(d->ahead == 118);
    CHECK(ParseBuild("1.3.0-0-gabcdef0"));
    CHECK(ParseBuild("v1.3.0-118-g204b92480-dirty") == std::nullopt);
    CHECK(ParseBuild("v1.3.0-dirty") == std::nullopt);
    CHECK(ParseBuild("v1.3.0-rc1") == std::nullopt);
    CHECK(ParseBuild("v1.4.0-rc1-3-gabc1234") == std::nullopt);
    CHECK(ParseBuild("51fe1f4bf") == std::nullopt); // a bare commit id
    CHECK(ParseBuild("51fe1f4bf-dirty") == std::nullopt);
    CHECK(ParseBuild("UNKNOWN") == std::nullopt);
    CHECK(ParseBuild("") == std::nullopt);
    CHECK(ParseBuild("v1.3.0-118-g") == std::nullopt);
    CHECK(ParseBuild("v1.3.0-118-gXYZ") == std::nullopt);
    CHECK(ParseBuild("v1.3.0-x-g204b92480") == std::nullopt);
    CHECK(ParseBuild("v1.3.0-118-204b92480") == std::nullopt);
    CHECK(ParseBuild("2120.1-300-gabc1234") == std::nullopt);
}

TEST_CASE("Update check: which version string of the build", "[update]") {
    CHECK(PickBuildVersion("1.3.0", "51fe1f4bf") == "1.3.0");
    CHECK(PickBuildVersion("51fe1f4", "v1.3.0-118-g204b92480") == "v1.3.0-118-g204b92480");
    CHECK(PickBuildVersion("51fe1f4", "v1.3.0-118-g204b92480-dirty") == std::nullopt);
    CHECK(PickBuildVersion("51fe1f4", "51fe1f4bf") == std::nullopt);
    CHECK(PickBuildVersion("", "") == std::nullopt);
}

TEST_CASE("Update check: when to alert", "[update]") {
    // newer release
    CHECK(IsUpdateAvailable("1.3.0", "1.3.1"));
    CHECK(IsUpdateAvailable("1.3.0", "v1.3.1"));
    CHECK(IsUpdateAvailable("v1.3.0", "1.4.0"));
    CHECK(IsUpdateAvailable("1.3.0", "2.0.0"));
    CHECK(IsUpdateAvailable("1.2.9", "1.3.0"));
    CHECK(IsUpdateAvailable("1.9.0", "1.10.0")); // numbers, not text
    CHECK(IsUpdateAvailable("v1.3.0-118-g204b92480", "1.3.1"));
    CHECK(IsUpdateAvailable("v1.3.0-118-g204b92480", "v1.4.0"));
    // same or older
    CHECK_FALSE(IsUpdateAvailable("1.3.0", "1.3.0"));
    CHECK_FALSE(IsUpdateAvailable("1.3.0", "v1.3.0"));
    CHECK_FALSE(IsUpdateAvailable("v1.3.0", "1.3.0"));
    CHECK_FALSE(IsUpdateAvailable("1.3.1", "1.3.0"));
    CHECK_FALSE(IsUpdateAvailable("1.10.0", "1.9.0"));
    CHECK_FALSE(IsUpdateAvailable("2.0.0", "1.99.99"));
    // a build ahead of the tag is not behind that tag
    CHECK_FALSE(IsUpdateAvailable("v1.3.0-118-g204b92480", "1.3.0"));
    CHECK_FALSE(IsUpdateAvailable("v1.3.0-118-g204b92480", "v1.3.0"));
    CHECK_FALSE(IsUpdateAvailable("v1.3.0-1-gabcdef0", "1.2.1"));
    CHECK_FALSE(IsUpdateAvailable("v1.3.0-0-gabcdef0", "1.3.0"));
    // unparsable or dirty: never
    CHECK_FALSE(IsUpdateAvailable("v1.3.0-118-g204b92480-dirty", "9.9.9"));
    CHECK_FALSE(IsUpdateAvailable("51fe1f4bf", "9.9.9"));
    CHECK_FALSE(IsUpdateAvailable("", "9.9.9"));
    CHECK_FALSE(IsUpdateAvailable("1.3.0-rc1", "9.9.9"));
    CHECK_FALSE(IsUpdateAvailable("1.3.0", ""));
    CHECK_FALSE(IsUpdateAvailable("1.3.0", "latest"));
    CHECK_FALSE(IsUpdateAvailable("1.3.0", "1.4.0-rc1"));
    CHECK_FALSE(IsUpdateAvailable("1.3.0", "1.4"));
    CHECK_FALSE(IsUpdateAvailable("1.3.0", "2120.1"));
    CHECK_FALSE(IsUpdateAvailable("1.3.0", "1.4.0 "));
}

TEST_CASE("Update check: the release reply", "[update]") {
    CHECK(TagFromReleaseJson(R"({"tag_name":"v1.4.0","draft":false,"prerelease":false})") ==
          "v1.4.0");
    CHECK(TagFromReleaseJson(R"({"tag_name":"1.4.0"})") == "1.4.0");
    CHECK(TagFromReleaseJson(R"({"name":"x","tag_name":"1.4.0","body":"notes","assets":[]})") ==
          "1.4.0");
    CHECK(TagFromReleaseJson(R"({"tag_name":"1.4.0","prerelease":true})") == std::nullopt);
    CHECK(TagFromReleaseJson(R"({"tag_name":"1.4.0","draft":true})") == std::nullopt);
    CHECK(TagFromReleaseJson(R"({"tag_name":"1.4.0-rc1"})") == std::nullopt);
    CHECK(TagFromReleaseJson(R"({"tag_name":"../evil"})") == std::nullopt);
    CHECK(TagFromReleaseJson(R"({"tag_name":14})") == std::nullopt);
    CHECK(TagFromReleaseJson(R"({"tag_name":null})") == std::nullopt);
    CHECK(TagFromReleaseJson(R"({"message":"Not Found"})") == std::nullopt);
    CHECK(TagFromReleaseJson(R"({"message":"API rate limit exceeded","documentation_url":"x"})") ==
          std::nullopt);
    CHECK(TagFromReleaseJson(R"([{"tag_name":"1.4.0"}])") == std::nullopt);
    CHECK(TagFromReleaseJson("") == std::nullopt);
    CHECK(TagFromReleaseJson("<html>rate limited</html>") == std::nullopt);
    CHECK(TagFromReleaseJson(R"({"tag_name":"1.4.0")") == std::nullopt); // cut off
}

TEST_CASE("Update check: the release page", "[update]") {
    CHECK(ReleasePageUrl("v1.4.0") ==
          "https://github.com/simplysanatori/AzaharVR/releases/tag/v1.4.0");
    CHECK(ReleasePageUrl("1.4.0") ==
          "https://github.com/simplysanatori/AzaharVR/releases/tag/1.4.0");
    CHECK(ReleasePageUrl("../x?y#z") ==
          "https://github.com/simplysanatori/AzaharVR/releases/latest");
}

TEST_CASE("Update check: a stopped check uses no network", "[update]") {
    std::atomic_bool flag{false};
    Mods::Transfer::Stopper stopper{flag};
    stopper.Stop();
    const auto start = std::chrono::steady_clock::now();
    CHECK(UpdateChecker::GetLatestRelease(stopper) == std::nullopt);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
}
