// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharVR homebrew: the DS / GBA / Game Boy list (core/homebrew/handheld_catalogue.h) with no
// network: what a list may say, hostile input, and the checks before a file is used. The entries
// below are made up for these tests; no real homebrew is named.

#ifdef _WIN32 // AzaharVR: getpid
#include <process.h>
#else
#include <unistd.h>
#endif
#include <filesystem>
#include <fstream>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "core/homebrew/handheld_catalogue.h"
#include "core/homebrew/net.h"

namespace H = Homebrew::Handheld;

namespace {

const std::string kSha(64, 'a');

std::string EntryJson(const std::string& slug = "test-one", const std::string& system = "gba",
                      const std::string& file = "test.gba", const std::string& url = "",
                      const std::string& size = "1000", const std::string& sha = "") {
    return R"({"slug":")" + slug + R"(","title":"Test One","author":"A. Author","licence":"MIT",)" +
           R"("system":")" + system + R"(","size":)" + size + R"(,"sha256":")" +
           (sha.empty() ? kSha : sha) + R"(","page":"https://example.org/test",)" + R"("url":")" +
           (url.empty() ? "https://github.com/author/test/releases/download/v1/" + file : url) +
           R"(","filename":")" + file + R"("})";
}

std::string ListJson(const std::string& entries, const std::string& schema = "1") {
    return R"({"schema":)" + schema + R"(,"homebrew":[)" + entries + "]}";
}

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("azaharvr-handheld-test-" + std::to_string(::getpid()));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

H::Entry GoodEntry() {
    const H::Catalogue c = H::Parse(ListJson(EntryJson()));
    REQUIRE(c.entries.size() == 1);
    return c.entries[0];
}

} // namespace

TEST_CASE("Handheld homebrew: a good list is read", "[homebrew][handheld]") {
    const H::Catalogue c = H::Parse(ListJson(
        EntryJson() + "," +
        EntryJson("test-two", "ds", "two.nds", "https://hh3.gbdev.io/static/x/two.nds") + "," +
        EntryJson("test-three", "gbc", "three.gbc")));
    CHECK(c.error.empty());
    CHECK(c.rejected.empty());
    REQUIRE(c.entries.size() == 3);
    const H::Entry& e = c.entries[0];
    CHECK(e.slug == "test-one");
    CHECK(e.title == "Test One");
    CHECK(e.author == "A. Author");
    CHECK(e.licence == "MIT");
    CHECK(e.system == H::System::GBA);
    CHECK(e.size == 1000);
    CHECK(e.sha256 == kSha);
    CHECK(e.page == "https://example.org/test");
    CHECK(c.entries[1].system == H::System::DS);
    CHECK(c.entries[2].system == H::System::GBC);
    // an empty list is a good list; the file name may come from the url
    CHECK(H::Parse(ListJson("")).error.empty());
    std::string no_name = EntryJson();
    no_name.erase(no_name.find(R"(,"filename")"));
    no_name += "}";
    const H::Catalogue d = H::Parse(ListJson(no_name));
    REQUIRE(d.entries.size() == 1);
    CHECK(d.entries[0].filename == "test.gba");
    // an upper-case checksum is read as lower case
    const H::Catalogue up = H::Parse(ListJson(EntryJson("u", "gba", "u.gba", "", "5", std::string(64, 'A'))));
    REQUIRE(up.entries.size() == 1);
    CHECK(up.entries[0].sha256 == kSha);
}

TEST_CASE("Handheld homebrew: a list that cannot be read is refused whole", "[homebrew][handheld]") {
    CHECK_FALSE(H::Parse("").error.empty());
    CHECK_FALSE(H::Parse("not json").error.empty());
    CHECK_FALSE(H::Parse("[]").error.empty());
    CHECK_FALSE(H::Parse(R"({"homebrew":[]})").error.empty());          // no schema
    CHECK_FALSE(H::Parse(R"({"schema":"1","homebrew":[]})").error.empty());
    CHECK_FALSE(H::Parse(R"({"schema":1.5,"homebrew":[]})").error.empty());
    CHECK_FALSE(H::Parse(ListJson("", "2")).error.empty()); // newer than this reads
    CHECK_FALSE(H::Parse(ListJson("", "0")).error.empty());
    CHECK_FALSE(H::Parse(R"({"schema":1})").error.empty());
    CHECK_FALSE(H::Parse(R"({"schema":1,"homebrew":{}})").error.empty());
    CHECK(H::Parse(R"({"schema":2,"homebrew":[)" + EntryJson() + "]}").entries.empty());
    // too big, too deep
    CHECK_FALSE(H::Parse(std::string(H::kMaxCatalogueBytes + 1, ' ')).error.empty());
    CHECK_FALSE(H::Parse(std::string(100000, '[') + std::string(100000, ']')).error.empty());
    std::string deep = R"({"schema":1,"homebrew":[)";
    for (int i = 0; i < 200; i++) {
        deep += R"({"a":)";
    }
    CHECK_FALSE(H::Parse(deep).error.empty());
    std::string many;
    for (std::size_t i = 0; i <= H::kMaxEntries; i++) {
        many += (i ? "," : "") + std::string("{}");
    }
    CHECK_FALSE(H::Parse(ListJson(many)).error.empty());
    // invalid UTF-8 and a truncated list
    CHECK(H::Parse(std::string("{\"schema\":1,\"homebrew\":[\"\xff\xfe\"]}")).entries.empty());
    CHECK_FALSE(H::Parse(ListJson(EntryJson()).substr(0, 60)).error.empty());
}

TEST_CASE("Handheld homebrew: a bad entry is left out, the others stay", "[homebrew][handheld]") {
    const H::Catalogue c = H::Parse(ListJson(EntryJson("bad slug") + "," + EntryJson("good")));
    REQUIRE(c.entries.size() == 1);
    CHECK(c.entries[0].slug == "good");
    CHECK(c.rejected.size() == 1);
    // a duplicate slug: the first stays
    const H::Catalogue d = H::Parse(ListJson(EntryJson() + "," + EntryJson()));
    CHECK(d.entries.size() == 1);
    CHECK(d.rejected.size() == 1);
    // not objects
    const H::Catalogue e = H::Parse(ListJson(R"(1,"x",null,[],{})"));
    CHECK(e.entries.empty());
    CHECK(e.rejected.size() == 5);
}

TEST_CASE("Handheld homebrew: hostile fields", "[homebrew][handheld]") {
    const auto rejected = [](const std::string& entry) {
        return H::Parse(ListJson(entry)).entries.empty();
    };
    const auto replace = [](std::string s, const std::string& from, const std::string& to) {
        const auto at = s.find(from);
        REQUIRE(at != std::string::npos);
        return s.replace(at, from.size(), to);
    };
    const std::string good = EntryJson();
    CHECK_FALSE(rejected(good));
    // slugs
    for (const char* slug : {"", "../x", "a/b", "a\\\\b", ".x", "-x", "UPPER", "a b", "a.b", "é",
                             "x%2e"}) {
        CHECK(rejected(EntryJson(slug)));
    }
    CHECK(rejected(EntryJson(std::string(65, 'a'))));
    CHECK_FALSE(rejected(EntryJson(std::string(64, 'a'))));
    // file names
    for (const char* name : {"../evil.gba", "a/b.gba", "a\\\\b.gba", ".hidden.gba", "x..gba.gba",
                             "evil.exe", "evil.gba.sh", "gba", ".gba", "a.gba ", "a.gba.", "a%2e.gba",
                             "a\\u0000.gba", "a\\n.gba", "é.gba", "-rf.gba", "a:b.gba"}) {
        CHECK(rejected(replace(good, R"("filename":"test.gba")", std::string(R"("filename":")") + name + "\"")));
    }
    CHECK(rejected(replace(good, R"("filename":"test.gba")", R"("filename":7)")));
    CHECK(rejected(EntryJson("a", "ds", "a.gba")));  // the extension must fit the system
    CHECK(rejected(EntryJson("a", "gba", "a.nds")));
    CHECK_FALSE(rejected(EntryJson("a", "ds", "A.NDS")));
    CHECK_FALSE(rejected(EntryJson("a", "gb", "a.gbc")));
    CHECK(rejected(EntryJson("a", "nes", "a.nes")));
    CHECK(rejected(EntryJson("a", "3ds", "a.3dsx")));
    CHECK(rejected(EntryJson("a", "GBA", "a.gba")));
    // sizes
    for (const char* size : {"0", "-1", "1.5", "\"1000\"", "null", "true", "[]",
                             "18446744073709551615", "67108865", "1e3"}) {
        CHECK(rejected(EntryJson("a", "gba", "a.gba", "", size)));
    }
    CHECK_FALSE(rejected(EntryJson("a", "gba", "a.gba", "", "67108864")));
    // checksums
    for (const char* sha : {"abc", "zz", "g"}) {
        CHECK(rejected(EntryJson("a", "gba", "a.gba", "", "5", sha)));
    }
    CHECK(rejected(EntryJson("a", "gba", "a.gba", "", "5", std::string(63, 'a'))));
    CHECK(rejected(EntryJson("a", "gba", "a.gba", "", "5", std::string(65, 'a'))));
    CHECK(rejected(EntryJson("a", "gba", "a.gba", "", "5", std::string(63, 'a') + "g")));
    CHECK(rejected(replace(good, "\"sha256\":\"" + kSha + "\"", R"("sha256":12)")));
    // missing fields, wrong types
    for (const char* key : {"slug", "title", "author", "licence", "system", "size", "sha256", "page",
                            "url"}) {
        const std::string quoted = std::string("\"") + key + "\":";
        const auto at = good.find(quoted);
        REQUIRE(at != std::string::npos);
        std::string renamed = good;
        renamed.replace(at, quoted.size(), std::string("\"x") + key + "\":");
        CHECK(rejected(renamed));
    }
    // text: empty, control characters, direction controls, too long, bad UTF-8
    for (const char* t : {"", " ", " x", "x ", "a\\nb", "a\\u0000b", "a\\u0007b", "a\\u202eb", "a\\u2066b",
                          "a\\u200fb", "a\\u2028b", "a\\u0085b", "a\\u007fb"}) {
        CHECK(rejected(replace(good, R"("title":"Test One")", std::string(R"("title":")") + t + "\"")));
        CHECK(rejected(replace(good, R"("licence":"MIT")", std::string(R"("licence":")") + t + "\"")));
    }
    CHECK(rejected(replace(good, R"("author":"A. Author")",
                           R"("author":")" + std::string(H::kMaxTextLength + 1, 'x') + "\"")));
    CHECK_FALSE(rejected(replace(good, R"("author":"A. Author")",
                                 R"("author":")" + std::string(H::kMaxTextLength, 'x') + "\"")));
    CHECK_FALSE(rejected(replace(good, R"("author":"A. Author")", "\"author\":\"Zo\xc3\xab\"")));
    CHECK(rejected(replace(good, R"("author":"A. Author")", "\"author\":\"Zo\xc3\"")));
    CHECK(rejected(replace(good, R"("author":"A. Author")", "\"author\":\"\xed\xa0\x80\"")));
    CHECK(rejected(replace(good, R"("author":"A. Author")", "\"author\":\"\xc0\xaf\"")));
}

TEST_CASE("Handheld homebrew: only https, only allowed hosts, no tricks in an address",
          "[homebrew][handheld]") {
    const auto url_rejected = [](const std::string& url) {
        return H::Parse(ListJson(EntryJson("a", "gba", "a.gba", url))).entries.empty();
    };
    CHECK_FALSE(url_rejected("https://github.com/a/b/releases/download/v1/a.gba"));
    CHECK_FALSE(url_rejected("https://raw.githubusercontent.com/a/b/main/a.gba"));
    CHECK_FALSE(url_rejected("https://hh3.gbdev.io/static/database-gba/x/a.gba"));
    CHECK(url_rejected("http://github.com/a/a.gba"));
    CHECK(url_rejected("ftp://github.com/a/a.gba"));
    CHECK(url_rejected("file:///etc/passwd"));
    CHECK(url_rejected("//github.com/a/a.gba"));
    CHECK(url_rejected("github.com/a/a.gba"));
    CHECK(url_rejected("HTTPS://github.com/a/a.gba"));
    CHECK(url_rejected("https://GitHub.com/a/a.gba"));          // not lower case
    CHECK(url_rejected("https://example.org/a.gba"));           // not allowed
    CHECK(url_rejected("https://github.com.evil.org/a.gba"));   // look-alike
    CHECK(url_rejected("https://evilgithub.com/a.gba"));
    CHECK(url_rejected("https://sub.github.com/a.gba"));        // no wildcards
    CHECK(url_rejected("https://github.com@evil.org/a.gba"));   // userinfo
    CHECK(url_rejected("https://evil.org@github.com/a.gba"));
    CHECK(url_rejected("https://github.com:8443/a.gba"));       // a port
    CHECK(url_rejected("https://github.com:80@evil.org/a.gba"));
    CHECK(url_rejected("https://github.com\\@evil.org/a.gba"));
    CHECK(url_rejected("https://github.com"));                  // no path
    CHECK(url_rejected("https://github.com?x=/a.gba"));
    CHECK(url_rejected("https://github.com/a/../../b/a.gba"));  // traversal
    CHECK(url_rejected("https://github.com/a/%2e%2e/b/a.gba"));
    CHECK(url_rejected("https://github.com/a/%2E%2E/b/a.gba"));
    CHECK(url_rejected("https://github.com/a/..%2fb/a.gba"));
    CHECK(url_rejected("https://github.com/a/..%5cb/a.gba"));
    CHECK(url_rejected("https://github.com/a\\b/a.gba"));
    CHECK(url_rejected("https://github.com/a b/a.gba"));        // space, controls, non-ASCII
    CHECK(url_rejected("https://github.com/a\tb/a.gba"));
    CHECK(url_rejected("https://github.com/a\nb/a.gba"));
    CHECK(url_rejected("https://github.com/\xc3\xa9/a.gba"));
    CHECK(url_rejected("https://github.com/a.gba#x"));
    CHECK(url_rejected("https://127.0.0.1/a.gba"));             // not on the list
    CHECK(url_rejected("https://[::1]/a.gba"));
    CHECK(url_rejected("https://github.com./a.gba"));
    CHECK(url_rejected("https://github..com/a.gba"));
    CHECK(url_rejected("https:///github.com/a.gba"));
    CHECK(url_rejected("https://github.com/" + std::string(H::kMaxUrlLength, 'a')));
    // a list file cannot add a host
    CHECK(H::Parse(R"({"schema":1,"hosts":["example.org"],"homebrew":[)" +
                   EntryJson("a", "gba", "a.gba", "https://example.org/a.gba") + "]}")
              .entries.empty());
    // the author's own page is only a link, but it is https and well formed
    const std::string good = EntryJson();
    for (const char* page : {"http://example.org/x", "javascript:alert(1)", "https://user@example.org/x",
                             "https://example.org/../x", "example.org", ""}) {
        std::string s = good;
        const std::string from = R"("page":"https://example.org/test")";
        s.replace(s.find(from), from.size(), std::string(R"("page":")") + page + "\"");
        CHECK(H::Parse(ListJson(s)).entries.empty());
    }
    // the host check the downloads use
    CHECK(H::HostAllowed("github.com"));
    CHECK_FALSE(H::HostAllowed("api.github.com"));
    CHECK_FALSE(H::HostAllowed(""));
    CHECK_FALSE(H::HostAllowed("github.com:443"));
    CHECK_FALSE(H::HostAllowed("https://github.com"));
}

TEST_CASE("Handheld homebrew: paths stay inside the folder", "[homebrew][handheld]") {
    H::Entry e = GoodEntry();
    CHECK(H::EntryPath("/root", e) == "/root/test-one/test.gba");
    CHECK(H::EntryDir("/root", e) == "/root/test-one");
    e.slug = "../x";
    CHECK(H::EntryPath("/root", e).empty());
    CHECK(H::EntryDir("/root", e).empty());
    e = GoodEntry();
    e.filename = "../../x.gba";
    CHECK(H::EntryPath("/root", e).empty());
    e.filename = "/etc/x.gba";
    CHECK(H::EntryPath("/root", e).empty());
    e = GoodEntry();
    CHECK(H::EntryPath("", e).empty());
    // an entry made by hand with a bad address is not downloaded
    e.url = "http://github.com/a.gba";
    TempDir t;
    CHECK_FALSE(H::Install(t.path.string(), e, [](u64, u64) { return true; }).empty());
    e.url = "https://example.org/a.gba";
    CHECK_FALSE(H::Install(t.path.string(), e, [](u64, u64) { return true; }).empty());
    e = GoodEntry();
    e.size = H::kMaxFileSize + 1;
    CHECK_FALSE(H::Install(t.path.string(), e, [](u64, u64) { return true; }).empty());
    CHECK(std::filesystem::is_empty(t.path)); // nothing was made
    // an uncatalogued file name is not reached through Remove either
    e = GoodEntry();
    e.slug = "..";
    CHECK_FALSE(H::Remove(t.path.string(), e).empty());
    CHECK(std::filesystem::exists(t.path));
}

TEST_CASE("Handheld homebrew: a file is checked by size and SHA-256 before it is used",
          "[homebrew][handheld]") {
    TempDir t;
    const std::string body = "not a real game, only test bytes";
    const std::string path = (t.path / "f.gba").string();
    {
        std::ofstream(path, std::ios::binary) << body;
    }
    H::Entry e = GoodEntry();
    e.size = body.size();
    e.sha256 = Homebrew::Sha256Hex(body);
    CHECK(H::VerifyFile(path, e).empty());
    H::Entry wrong_size = e;
    wrong_size.size++;
    CHECK_FALSE(H::VerifyFile(path, wrong_size).empty());
    H::Entry wrong_hash = e;
    wrong_hash.sha256[0] = wrong_hash.sha256[0] == 'a' ? 'b' : 'a';
    CHECK_FALSE(H::VerifyFile(path, wrong_hash).empty());
    CHECK_FALSE(H::VerifyFile((t.path / "missing.gba").string(), e).empty());
    CHECK_FALSE(H::VerifyFile(t.path.string(), e).empty()); // a folder
    // one byte changed, same size
    {
        std::ofstream(path, std::ios::binary) << "Not a real game, only test bytes";
    }
    CHECK_FALSE(H::VerifyFile(path, e).empty());
    // Remove takes the entry's folder and nothing else
    std::filesystem::create_directories(t.path / "test-one");
    std::ofstream(t.path / "test-one" / "test.gba") << "x";
    std::ofstream(t.path / "other.gba") << "y";
    CHECK(H::Remove(t.path.string(), GoodEntry()).empty());
    CHECK_FALSE(std::filesystem::exists(t.path / "test-one"));
    CHECK(std::filesystem::exists(t.path / "other.gba"));
}
