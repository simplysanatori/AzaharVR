// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "frame_cores/cheat_db.h"

using namespace FrameCores;
using namespace FrameCores::CheatDb;

namespace {

const std::string kFixes(40, 'a');
const std::string kLibretro(40, 'b');

std::string Index(const std::string& commit, const std::string& lines) {
    return "# test\nazahar-cheat-index 1\nlibretro " + commit + "\n" + lines;
}

const std::string kLines =
    "nds\tBJBF\t1FCCF595\t007 - Blood Stone (France)\n"
    "nds\tBJBE\t0000ABCD\t007 - Blood Stone (USA)\n"
    "gba\tA7OE\t56C83C16\t007 - NightFire (USA, Europe) (Code Breaker)\n"
    "gbc\t-\tE038E666\t007 - The World Is Not Enough (USA, Europe)\n"
    "gb\t-\t5449CAC0\t1942\n";

constexpr const char* kDsCht =
    "cheats = 3\n\n"
    "cheat0_desc = \"Widescreen Codes\"\n\n"
    "cheat1_desc = \"Infinite Health\"\n"
    "cheat1_code = \"020C35DC+E5950170+94808004+FFFE0000+D2000000+00000000\"\n"
    "cheat1_enable = true\n\n"
    "cheat2_desc = \"All Levels Available\"\n"
    "cheat2_code = \"221A02BC+00000001\"\n"
    "cheat2_enable = false\n";

const Pins kPins{kFixes, kLibretro};

} // namespace

TEST_CASE("ParseIndex reads entries and drops bad lines", "[frame_cores][cheat_db]") {
    const auto p = ParseIndex(Index(kLibretro, kLines));
    REQUIRE(p.error.empty());
    CHECK(p.index.libretro_commit == kLibretro);
    REQUIRE(p.index.entries.size() == 5);
    CHECK(p.index.entries[0].system == System::Nds);
    CHECK(p.index.entries[0].code == "BJBF");
    CHECK(p.index.entries[0].crc == 0x1FCCF595u);
    CHECK(p.index.entries[3].code.empty());
    CHECK(p.index.entries[2].name == "007 - NightFire (USA, Europe) (Code Breaker)");

    const auto bad = ParseIndex(Index(
        kLibretro, "nds\tBJBF\t1FCCF595\t../../etc/passwd\n"
                   "nds\tBJBF\t1FCCF595\ta/b\n"
                   "nds\tBJBF\t1FCCF595\ta\\b\n"
                   "nds\tBJBF\t1FCCF595\t.hidden\n"
                   "nds\tBJBF\t1FCCF595\tctrl\x01name\n"
                   "nds\tBJBF\t1FCCF59\tshort crc\n"
                   "nds\tBJBF\tZZZZZZZZ\tnot hex\n"
                   "nds\tBJ\t1FCCF595\tshort code\n"
                   "n64\tBJBF\t1FCCF595\tunknown system\n"
                   "nds\tBJBF\t1FCCF595\n"
                   "nds\tBJBF\t1FCCF595\t\n"
                   "nds\tBJBF\t1FCCF595\tfine\n"));
    REQUIRE(bad.error.empty());
    CHECK(bad.skipped == 11);
    REQUIRE(bad.index.entries.size() == 1);
    CHECK(bad.index.entries[0].name == "fine");
}

TEST_CASE("ParseIndex refuses what is not an index", "[frame_cores][cheat_db]") {
    CHECK_FALSE(ParseIndex("").error.empty());
    CHECK_FALSE(ParseIndex("<html>404</html>").error.empty());
    CHECK_FALSE(ParseIndex("azahar-cheat-index 2\nlibretro " + kLibretro + "\n").error.empty());
    CHECK_FALSE(ParseIndex("azahar-cheat-index 1\n").error.empty());
    CHECK_FALSE(ParseIndex("azahar-cheat-index 1\nlibretro main\n").error.empty());
    CHECK_FALSE(ParseIndex("azahar-cheat-index 1\nlibretro " + std::string(40, 'B') + "\n")
                    .error.empty());
    // A line over the limit, and a file over it
    CHECK_FALSE(ParseIndex(Index(kLibretro, std::string(5000, 'x') + "\n")).error.empty());
    CHECK_FALSE(ParseIndex(std::string(kMaxIndexBytes + 1, '\n')).error.empty());
    // Too many entries
    std::string many;
    for (std::size_t i = 0; i <= kMaxIndexEntries; ++i) {
        many += "gb\t-\t00000001\tx\n";
    }
    CHECK_FALSE(ParseIndex(Index(kLibretro, many)).error.empty());
    // Binary junk never throws
    std::mt19937 rng(7);
    for (int i = 0; i < 200; ++i) {
        std::string junk(rng() % 600, '\0');
        for (auto& c : junk) {
            c = static_cast<char>(rng());
        }
        (void)ParseIndex(junk);
        (void)ParseIndex(Index(kLibretro, junk));
    }
}

TEST_CASE("FindMatch: code and CRC, then the name", "[frame_cores][cheat_db]") {
    const auto p = ParseIndex(Index(kLibretro, kLines));
    const Index& idx = p.index;

    auto m = FindMatch(idx, System::Nds, "BJBF", 0x1FCCF595u, "whatever");
    REQUIRE(m.entry);
    CHECK_FALSE(m.by_name);
    CHECK(m.entry->name == "007 - Blood Stone (France)");

    // The CRC of another game, or the right CRC with another game code: no match by CRC
    CHECK(FindMatch(idx, System::Nds, "BJBF", 0x12345678u, "nothing").entry == nullptr);
    CHECK(FindMatch(idx, System::Nds, "XXXX", 0x1FCCF595u, "nothing").entry == nullptr);
    // Another system's entry is never taken
    CHECK(FindMatch(idx, System::Gba, "BJBF", 0x1FCCF595u, "nothing").entry == nullptr);

    // Game Boy games have no code: the CRC alone, in the system's own list
    m = FindMatch(idx, System::Gbc, "", 0xE038E666u, "x");
    REQUIRE(m.entry);
    CHECK_FALSE(m.by_name);
    CHECK(FindMatch(idx, System::Gb, "", 0xE038E666u, "x").entry == nullptr);

    // By name: letters and digits, any case and spacing; the code must still fit
    m = FindMatch(idx, System::Nds, "BJBF", 0x1u, "007_-_blood stone (france)");
    REQUIRE(m.entry);
    CHECK(m.by_name);
    CHECK(FindMatch(idx, System::Nds, "BJBE", 0x1u, "007 - Blood Stone (France)").entry == nullptr);
    // No CRC at all (the file could not be read): the name still works
    m = FindMatch(idx, System::Gb, "", std::nullopt, "1942");
    REQUIRE(m.entry);
    CHECK(m.by_name);
    CHECK(FindMatch(idx, System::Gb, "", std::nullopt, "").entry == nullptr);
    CHECK(FindMatch(idx, System::Gb, "", std::nullopt, "(!!)").entry == nullptr);
}

TEST_CASE("URL paths come from tables and are percent-encoded", "[frame_cores][cheat_db]") {
    CHECK(IndexUrlPath(kFixes) ==
          "/simplysanatori/ds-gba-game-fixes/" + kFixes + "/cheats/libretro-index.tsv");
    CHECK(ChtUrlPath(kLibretro, System::Nds, "'Chou' Kowai (Japan) & more") ==
          "/libretro/libretro-database/" + kLibretro +
              "/cht/Nintendo%20-%20Nintendo%20DS/%27Chou%27%20Kowai%20%28Japan%29%20%26%20more.cht");
    CHECK(ChtUrlPath(kLibretro, System::Gbc, "a").find("Game%20Boy%20Color") != std::string::npos);
    // Whatever the name holds, the path stays under one folder
    const auto path = ChtUrlPath(kLibretro, System::Gb, "../../x?y#z\n");
    CHECK(path.find("..") == std::string::npos);
    CHECK(path.find('?') == std::string::npos);
    CHECK(path.find('#') == std::string::npos);
    CHECK(path.find('\n') == std::string::npos);
}

TEST_CASE("ParseCht: DS entries become Action Replay lines, off", "[frame_cores][cheat_db]") {
    const auto r = ParseCht(kDsCht, System::Nds);
    REQUIRE(r.cheats.size() == 2); // the heading is left out
    CHECK(r.skipped == 0);
    CHECK(r.cheats[0].name == "Infinite Health");
    CHECK(r.cheats[0].code ==
          "020C35DC E5950170\n94808004 FFFE0000\nD2000000 00000000");
    CHECK_FALSE(r.cheats[0].enabled); // cheatN_enable = true changes nothing
    CHECK_FALSE(r.cheats[1].enabled);
    CHECK(r.cheats[1].code == "221A02BC 00000001");
}

TEST_CASE("ParseCht: GBA and Game Boy codes", "[frame_cores][cheat_db]") {
    const auto gba = ParseCht("cheat0_desc = \"Max\"\ncheat0_code = \"3300397f+00ff+8201A454+07B7\"\n"
                              "cheat1_desc = \"GS\"\ncheat1_code = \"0A0B0C0D+0E0F1011\"\n"
                              "cheat2_desc = \"odd\"\ncheat2_code = \"3300397F+00FF+8201A454\"\n"
                              "cheat3_desc = \"long word\"\ncheat3_code = \"3300397F00+00FF\"\n"
                              "cheat4_desc = \"nds length\"\ncheat4_code = \"3300397F+000\"\n",
                              System::Gba);
    REQUIRE(gba.cheats.size() == 2);
    CHECK(gba.skipped == 3);
    CHECK(gba.cheats[0].code == "3300397F 00FF\n8201A454 07B7");
    CHECK(gba.cheats[1].code == "0A0B0C0D 0E0F1011");

    // DS words are all 8 digits
    CHECK(ParseCht("cheat0_desc=\"x\"\ncheat0_code=\"3300397F+00FF\"\n", System::Nds)
              .cheats.empty());

    const auto gb = ParseCht("cheat0_desc = \"GG\"\ncheat0_code = \"007-e48\"\n"
                             "cheat1_desc = \"GS\"\ncheat1_code = \"916412cb+910598CB\"\n"
                             "cheat2_desc = \"GG9\"\ncheat2_code = \"007-E48-A1B\"\n"
                             "cheat3_desc = \"bad\"\ncheat3_code = \"hello\"\n"
                             "cheat4_desc = \"bad\"\ncheat4_code = \"00-E48\"\n",
                             System::Gbc);
    REQUIRE(gb.cheats.size() == 3);
    CHECK(gb.skipped == 2);
    CHECK(gb.cheats[0].code == "007-E48");
    CHECK(gb.cheats[1].code == "916412CB\n910598CB");
    CHECK(gb.cheats[2].code == "007-E48-A1B");
}

TEST_CASE("ParseCht: hostile files", "[frame_cores][cheat_db]") {
    // Over the size limit: nothing is read
    const auto big = ParseCht(std::string(kMaxChtBytes + 1, 'x'), System::Nds);
    CHECK(big.truncated);
    CHECK(big.cheats.empty());

    // More entries than the limit: the first ones, and truncated
    std::string many;
    for (int i = 0; i < kMaxCheats + 500; ++i) {
        many += "cheat" + std::to_string(i) + "_desc = \"c\"\ncheat" + std::to_string(i) +
                "_code = \"221A02BC+00000001\"\n";
    }
    const auto lots = ParseCht(many, System::Nds);
    CHECK(lots.truncated);
    CHECK(lots.cheats.size() == static_cast<std::size_t>(kMaxCheats));

    // Huge indexes and odd keys
    const auto odd = ParseCht("cheat99999999999_code = \"221A02BC+00000001\"\n"
                              "cheat_code = \"221A02BC+00000001\"\n"
                              "cheat-1_code = \"221A02BC+00000001\"\n"
                              "cheat1_codex = \"221A02BC+00000001\"\n"
                              "= \"221A02BC+00000001\"\n"
                              "cheat\n\n\n=\n",
                              System::Nds);
    CHECK(odd.cheats.empty());

    // A name that is long, with control characters and an unterminated quote
    const auto name = ParseCht("cheat0_desc = \"" + std::string(5000, 'n') +
                                   "\x01\x02\ncheat0_code = \"221A02BC+00000001\"\n",
                               System::Nds);
    REQUIRE(name.cheats.size() == 1);
    CHECK(name.cheats[0].name.size() <= kMaxNameLength);
    for (const char c : name.cheats[0].name) {
        CHECK(static_cast<unsigned char>(c) >= 0x20);
    }

    // A code with hundreds of thousands of words, and one with 65 GBA word pairs
    std::string words;
    for (int i = 0; i < 200000; ++i) {
        words += "221A02BC+";
    }
    CHECK(ParseCht("cheat0_desc=\"a\"\ncheat0_code=\"" + words + "00000001\"\n", System::Nds)
              .cheats.empty());
    std::string pairs;
    for (int i = 0; i < 33; ++i) {
        pairs += (i ? "+" : "") + std::string("3300397F+00FF");
    }
    CHECK(ParseCht("cheat0_desc=\"a\"\ncheat0_code=\"" + pairs + "\"\n", System::Gba)
              .cheats.empty()); // gba-ds reads 64 words of a code
    // Escapes and a name that holds a path stay text
    const auto path = ParseCht("cheat0_desc = \"../../x\\\"y\"\ncheat0_code = \"221A02BC+00000001\"\n",
                               System::Nds);
    REQUIRE(path.cheats.size() == 1);
    CHECK(path.cheats[0].name == "../../x\"y");

    // Random bytes, every system: never a crash, and every code kept is hex words
    std::mt19937 rng(11);
    for (int i = 0; i < 300; ++i) {
        std::string junk(rng() % 2000, '\0');
        for (auto& c : junk) {
            c = (rng() % 4 == 0) ? static_cast<char>(rng()) : "cheat0123_desc=\"+ \n"[rng() % 19];
        }
        for (const System s : {System::Nds, System::Gba, System::Gb, System::Gbc}) {
            for (const auto& c : ParseCht(junk, s).cheats) {
                CHECK_FALSE(c.enabled);
                CHECK_FALSE(c.code.empty());
                for (const char ch : c.code) {
                    CHECK(((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || ch == ' ' ||
                           ch == '\n' || ch == '-'));
                }
            }
        }
    }
}

TEST_CASE("MergeImported leaves out what is there and what is over the limit",
          "[frame_cores][cheat_db]") {
    std::vector<CheatEntry> list{{"Mine", "221A02BC 00000001", true}};
    std::vector<CheatEntry> in{{"Mine", "221A02BC 00000001", false},   // the same
                               {"Mine", "221A02BC 00000002", false},   // another code
                               {"Other", "221A02BC 00000001", true}};  // enabled is not kept
    CHECK(MergeImported(list, in) == 2);
    REQUIRE(list.size() == 3);
    CHECK(list[0].enabled); // the player's own is untouched
    CHECK_FALSE(list[1].enabled);
    CHECK_FALSE(list[2].enabled);
    CHECK(MergeImported(list, in) == 0); // a second download adds nothing

    std::vector<CheatEntry> full;
    std::vector<CheatEntry> lots;
    for (std::size_t i = 0; i < kMaxStoredCheats + 10; ++i) {
        lots.push_back({"c" + std::to_string(i), "221A02BC 00000001", false});
    }
    CHECK(MergeImported(full, lots) == static_cast<int>(kMaxStoredCheats));
}

TEST_CASE("Download: by CRC, by name, and the failures", "[frame_cores][cheat_db]") {
    std::vector<std::string> asked;
    std::string index = Index(kLibretro, kLines);
    std::string cht = kDsCht;
    int cht_status = 200;
    int index_status = 200;
    const Cheats::Online::HttpGet get = [&](const std::string& host, const std::string& path) {
        asked.push_back(host + path);
        Cheats::Online::HttpResult r;
        if (path.find("libretro-index.tsv") != std::string::npos) {
            r.status = index_status;
            r.body = index;
        } else {
            r.status = cht_status;
            r.body = cht;
        }
        if (r.status != 200) {
            r.body.clear();
        }
        return r;
    };

    auto r = Download(get, System::Nds, "BJBF", 0x1FCCF595u, "x", kPins);
    CHECK(r.error.empty());
    CHECK(r.found);
    CHECK_FALSE(r.by_name);
    CHECK(r.cheats.size() == 2);
    REQUIRE(asked.size() == 2);
    CHECK(asked[0] == "https://raw.githubusercontent.com/simplysanatori/ds-gba-game-fixes/" + kFixes +
                          "/cheats/libretro-index.tsv");
    CHECK(asked[1].rfind("https://raw.githubusercontent.com/libretro/libretro-database/" + kLibretro +
                             "/cht/Nintendo%20-%20Nintendo%20DS/007%20-%20Blood%20Stone%20",
                         0) == 0);

    r = Download(get, System::Nds, "BJBF", 0x1u, "007 - Blood Stone (France)", kPins);
    CHECK(r.found);
    CHECK(r.by_name);

    asked.clear();
    r = Download(get, System::Nds, "AAAA", 0x1u, "unknown game", kPins);
    CHECK(r.error.empty());
    CHECK_FALSE(r.found);
    CHECK(asked.size() == 1); // no file asked for

    // A file that is gone, an index of another libretro commit, no pin, a refused request
    cht_status = 404;
    CHECK_FALSE(Download(get, System::Nds, "BJBF", 0x1FCCF595u, "x", kPins).error.empty());
    cht_status = 200;
    index = Index(std::string(40, 'c'), kLines);
    CHECK_FALSE(Download(get, System::Nds, "BJBF", 0x1FCCF595u, "x", kPins).error.empty());
    index = Index(kLibretro, kLines);
    asked.clear();
    CHECK_FALSE(Download(get, System::Nds, "BJBF", 0x1FCCF595u, "x", Pins{"", kLibretro})
                    .error.empty());
    CHECK_FALSE(Download(get, System::Nds, "BJBF", 0x1FCCF595u, "x", Pins{kFixes, "main"})
                    .error.empty());
    CHECK(asked.empty()); // nothing is asked without both pins
    CHECK_FALSE(Download(get, System::Nds, "BJBF", 0x1FCCF595u, "x").error.empty()); // as built
    index_status = 500;
    CHECK_FALSE(Download(get, System::Nds, "BJBF", 0x1FCCF595u, "x", kPins).error.empty());
    index_status = 200;
    index = "<html>";
    CHECK_FALSE(Download(get, System::Nds, "BJBF", 0x1FCCF595u, "x", kPins).error.empty());
    // A cheat file over the limit
    index = Index(kLibretro, kLines);
    cht = std::string(kMaxChtBytes + 1, 'x');
    CHECK_FALSE(Download(get, System::Nds, "BJBF", 0x1FCCF595u, "x", kPins).error.empty());
}

TEST_CASE("RomCrc32 reads the whole file", "[frame_cores][cheat_db]") {
    const auto path = std::filesystem::temp_directory_path() / "azahar_cheat_db_crc.bin";
    {
        std::ofstream out(path, std::ios::binary);
        out << "123456789";
    }
    CHECK(RomCrc32(path.string()) == std::optional<std::uint32_t>(0xCBF43926u));
    {
        // Across the read buffer's end
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        const std::string block(1 << 20, 'a');
        out << block << "123456789";
    }
    const auto big = RomCrc32(path.string());
    REQUIRE(big);
    CHECK(*big != 0xCBF43926u);
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
    }
    CHECK_FALSE(RomCrc32(path.string()));
    std::filesystem::remove(path);
    CHECK_FALSE(RomCrc32(path.string()));
}
