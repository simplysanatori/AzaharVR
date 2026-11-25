// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "core/homebrew/net.h"
#include "frame_cores/alt_core_host.h"
#include "frame_cores/rom_info.h"
#include "frame_cores/widescreen.h"

using namespace FrameCores;
using namespace FrameCores::Widescreen;
namespace fs = std::filesystem;

namespace {

const std::string kHead =
    "azahar-widescreen-index 1\n# a note\ntwilight " + std::string(40, 'c') + "\n";

std::string Row(const std::string& code, const std::string& crc, const std::string& file,
                const std::string& size = "24", const std::string& sha = std::string(64, 'b')) {
    return code + "\t" + crc + "\t" + file + "\t" + size + "\t" + std::string(40, 'a') + "\t" +
           sha + "\tA Game (USA)\n";
}

std::string File(const std::vector<std::uint32_t>& words) {
    std::string out;
    for (const std::uint32_t w : words) {
        for (int shift = 0; shift < 32; shift += 8) {
            out += static_cast<char>(w >> shift);
        }
    }
    return out;
}

bool Reads(const std::vector<std::uint32_t>& words) {
    const std::string f = File(words);
    return ReadCodes({reinterpret_cast<const std::uint8_t*>(f.data()), f.size()}) == words;
}

struct Dir {
    fs::path path;
    Dir() {
        std::random_device rd;
        path = fs::temp_directory_path() /
               ("wide_test_" + std::to_string(rd()) + "_" + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~Dir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void Write(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary)
        .write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// "if 16 bits there are 0, 1Fh; end all"
const std::vector<std::uint32_t> kRed{0x92300000, 0, 0x12300000, 0x1F, 0xD2000000, 0};

/// Shows the 16 bits at 02300000 as the Primary Screen's colour; its ARM7 takes VBlank interrupts,
/// where the cheats run.
std::string ColourRom(const char* code) {
    std::string rom(0x10000, '\0');
    const auto put = [&rom](std::size_t at, std::uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            rom[at + i] = static_cast<char>(value >> (i * 8));
        }
    };
    std::memcpy(rom.data(), "WIDETEST", 8);
    std::memcpy(rom.data() + 0x0C, code, 4);
    const std::array<std::uint32_t, 8> header{0x8000, 0x02000000, 0x02000000, 0x100,
                                              0x8200, 0x02380000, 0x02380000, 0x100};
    for (std::size_t i = 0; i < header.size(); ++i) {
        put(0x20 + i * 4, header[i]);
    }
    put(0x80, 0x10000);
    put(0x84, 0x4000);
    const std::uint32_t arm9[] = {0xE3A00301, 0xE3A01801, 0xE5801000, 0xE59F200C, 0xE3A03405,
                                  0xE5921000, 0xE1C310B0, 0xEAFFFFFC, 0x02300000};
    const std::uint32_t arm7[] = {0xE3A00301, 0xE3A01008, 0xE1C010B4, 0xE28F2018,
                                  0xE59F3024, 0xE5832000, 0xE3A01001, 0xE5801210,
                                  0xE5801208, 0xE321F01F, 0xEAFFFFFE, 0xE3A00301,
                                  0xE3E01000, 0xE5801214, 0xE12FFF1E, 0x0380FFFC};
    for (std::size_t i = 0; i < std::size(arm9); ++i) {
        put(0x8000 + i * 4, arm9[i]);
    }
    for (std::size_t i = 0; i < std::size(arm7); ++i) {
        put(0x8200 + i * 4, arm7[i]);
    }
    rom[0x15E] = 0x34;
    rom[0x15F] = 0x12;
    return rom;
}

} // namespace

TEST_CASE("Widescreen: the index", "[frame_cores][widescreen]") {
    const auto p = ParseIndex(kHead + Row("AAAE", "038D", "AAAE-38D.bin") +
                              Row("AAAE", "FFFF", "AAAE-FFFF.bin") +
                              Row("BBBE", "1234", "BBBE-1234.bin", "0"));
    REQUIRE(p.error.empty());
    CHECK(p.skipped == 0);
    CHECK(p.index.commit == std::string(40, 'c'));
    REQUIRE(p.index.entries.size() == 3);
    CHECK(p.index.entries[0].code == "AAAE");
    CHECK(p.index.entries[0].crc16 == 0x038D);
    CHECK(p.index.entries[0].file == "AAAE-38D.bin");
    CHECK(p.index.entries[0].size == 24);
    CHECK(p.index.entries[0].sha256 == std::string(64, 'b'));
    CHECK(UrlPath(p.index, p.index.entries[0]) == "/DS-Homebrew/TWiLightMenu/" +
                                                      std::string(40, 'c') +
                                                      "/resources/widescreen/AAAE-38D.bin");

    for (const std::string& row :
         {Row("AAAE", "038D", "../AAAE-38D.bin"), Row("AAAE", "038D", "AAAE-38D.bin/../x"),
          Row("AAAE", "038D", "AAAP-38D.bin"), Row("AAAE", "038E", "AAAE-38D.bin"),
          Row("AAAE", "38D", "AAAE-38D.bin"), Row("AAAE", "038d", "AAAE-38d.bin"),
          Row("aaae", "038D", "aaae-38D.bin"), Row("AAAE5", "038D", "AAAE5-38D.bin"),
          Row("AAAE", "038D", "AAAE-0038D.bin"), Row("AAAE", "038D", "AAAE-.bin"),
          Row("AAAE", "038D", "AAAE-38D.bin", "25"), Row("AAAE", "038D", "AAAE-38D.bin", "4104"),
          Row("AAAE", "038D", "AAAE-38D.bin", "-8"), Row("AAAE", "038D", "AAAE-38D.bin", ""),
          Row("AAAE", "038D", "AAAE-38D.bin", "24", std::string(64, 'B')),
          Row("AAAE", "038D", "AAAE-38D.bin", "24", std::string(63, 'b')), std::string("x\n"),
          std::string("\t\t\t\t\t\t\n"), std::string("AAAE\t038D\tAAAE-38D.bin\n")}) {
        CAPTURE(row);
        const auto bad = ParseIndex(kHead + row);
        CHECK(bad.error.empty());
        CHECK(bad.skipped == 1);
        CHECK(bad.index.entries.empty());
    }
    for (const std::string& text :
         {std::string(), std::string("x\n"), std::string("azahar-widescreen-index 2\n"),
          std::string("azahar-widescreen-index 1\n"),
          std::string("azahar-widescreen-index 1\ntwilight main\n"),
          std::string(kMaxIndexBytes + 1, '#')}) {
        const auto bad = ParseIndex(text);
        CHECK(!bad.error.empty());
        CHECK(bad.index.entries.empty());
    }
}

TEST_CASE("Widescreen: the game's hack, its header's first", "[frame_cores][widescreen]") {
    const auto p = ParseIndex(
        kHead + Row("AAAE", "FFFF", "AAAE-FFFF.bin") + Row("AAAE", "038D", "AAAE-38D.bin") +
        Row("AAAE", "1234", "AAAE-1234.bin", "0") + Row("BBBE", "0001", "BBBE-0001.bin"));
    const auto find = [&](const char* code, std::uint16_t crc) {
        const Entry* e = Find(p.index, code, crc);
        return e ? e->file : std::string();
    };
    CHECK(find("AAAE", 0x038D) == "AAAE-38D.bin");
    CHECK(find("AAAE", 0x0002) == "AAAE-FFFF.bin");
    CHECK(find("AAAE", 0xFFFF) == "AAAE-FFFF.bin");
    CHECK(find("AAAE", 0x1234).empty()); // a ROM that has none: not the one for any version
    CHECK(find("BBBE", 0x0001) == "BBBE-0001.bin");
    CHECK(find("BBBE", 0x0002).empty());
    CHECK(find("AAAP", 0x038D).empty());
    CHECK(find("", 0x038D).empty());
}

TEST_CASE("Widescreen: the program's own index", "[frame_cores][widescreen]") {
    const auto p = ParseIndex(BuiltInIndexText());
    REQUIRE(p.error.empty());
    CHECK(p.skipped == 0);
    CHECK(p.index.entries.size() == 546);
    CHECK(BuiltIn().entries.size() == 546);
    CHECK(std::count_if(p.index.entries.begin(), p.index.entries.end(),
                        [](const Entry& e) { return e.size == 0; }) == 4);
    const auto find = [](const char* code, std::uint16_t crc) {
        const Entry* e = Find(BuiltIn(), code, crc);
        return e ? e->file : std::string();
    };
    CHECK(find("IRAO", 0xF6BF) == "IRAO-F6BF.bin");
    CHECK(find("IRAO", 0xF6C0).empty());
    CHECK(find("CPUE", 0x5153) == "CPUE-FFFF.bin");
    CHECK(find("AFEP", 0x038D) == "AFEP-38D.bin");
    CHECK(find("AMCE", 0xDEF2) == "AMCE-DEF2.bin");
    CHECK(find("AMCE", 0x6600).empty()); // wide by a patch of its own
    CHECK(find("A2DE", 0x12CB).empty());
}

TEST_CASE("Widescreen: only the codes the hacks use", "[frame_cores][widescreen]") {
    CHECK(Reads(kRed));
    for (const std::uint32_t a : {0x0u, 0x1u, 0x2u, 0x5u, 0x6u, 0x9u, 0xBu}) {
        CHECK(Reads({a << 28, 7}));
    }
    for (const std::uint32_t a : {0xD0u, 0xD2u, 0xDCu}) {
        CHECK(Reads({a << 24, 7}));
    }
    CHECK(Reads({0xE2000000, 0x10, 1, 2, 3, 4, 0xD2000000, 0}));
    CHECK(Reads({0xE2000000, 0x0C, 1, 2, 3, 0}));
    CHECK(Reads({0xE2000000, 0x08, 0xC0000000, 0xFFFFFFFF})); // what it copies is no code
    for (const std::uint32_t a :
         {0x30000000u, 0x40000000u, 0x70000000u, 0x80000000u, 0xA0000000u, 0xC0000000u, 0xC4000000u,
          0xC5000000u, 0xC6000000u, 0xD1000000u, 0xD3000000u, 0xD4000000u, 0xD9000000u, 0xDD000000u,
          0xF0000000u}) {
        CAPTURE(a);
        CHECK(!Reads({a, 1}));
        CHECK(!Reads({0x12300000, 1, a, 1}));
    }
    for (const std::uint32_t size :
         {0x9u, 0x1000u, 0x1001u, 0x7FFFFFFFu, 0xFFFFFFF9u, 0xFFFFFFFFu}) {
        CAPTURE(size);
        CHECK(!Reads({0xE2000000, size, 1, 2}));
    }
    CHECK(!Reads({}));
    CHECK(!Reads({0x12300000}));
    CHECK(!Reads({0x12300000, 1, 2}));
    CHECK(Reads(std::vector<std::uint32_t>(kMaxFileBytes / 4)));
    CHECK(!Reads(std::vector<std::uint32_t>(kMaxFileBytes / 4 + 2)));
}

TEST_CASE("Widescreen: install, off, on, remove", "[frame_cores][widescreen]") {
    Dir dir;
    const std::string game = dir.path.string();
    const std::string hack = File(kRed);
    const Index index{std::string(40, 'c'), {}};
    Entry entry{"AAAE", 0x38D, "AAAE-38D.bin", static_cast<std::uint32_t>(hack.size()),
                Homebrew::Sha256Hex(hack)};
    Cheats::Online::HttpResult reply{200, hack, ""};
    std::vector<std::string> asked;
    const auto get = [&](const std::string& host, const std::string& path) {
        asked.push_back(host + path);
        return reply;
    };

    CHECK(StateOf(game, entry) == State::None);
    CHECK(CodesFor(game, entry).empty());
    CHECK(!SetOn(game, entry, true));
    CHECK(asked.empty()); // nothing is fetched before Install

    SECTION("what is not the file is refused") {
        for (const auto& [status, body] :
             std::vector<std::pair<int, std::string>>{{302, hack},
                                                      {404, hack},
                                                      {0, ""},
                                                      {200, ""},
                                                      {200, hack + "x"},
                                                      {200, hack.substr(8)},
                                                      {200, std::string(hack.size(), '\0')}}) {
            reply = {status, body, ""};
            CHECK(!Install(get, index, entry, game).empty());
            CHECK(StateOf(game, entry) == State::None);
        }
        reply = {200, hack, ""};
        Entry none = entry;
        none.size = 0;
        CHECK(!Install(get, index, none, game).empty());
        // The right checksum of what is not codes
        const std::string other = File({0xC0000000, 0xFFFFFFFF, 0xD1000000, 0, 0, 0});
        Entry bad = entry;
        bad.sha256 = Homebrew::Sha256Hex(other);
        reply.body = other;
        CHECK(!Install(get, index, bad, game).empty());
        std::error_code ec;
        CHECK((!fs::exists(dir.path / "mods", ec) || fs::is_empty(dir.path / "mods", ec)));
    }
    SECTION("the player's") {
        REQUIRE(Install(get, index, entry, game).empty());
        REQUIRE(asked.size() == 1);
        CHECK(asked[0] == "https://raw.githubusercontent.com/DS-Homebrew/TWiLightMenu/" +
                              index.commit + "/resources/widescreen/AAAE-38D.bin");
        CHECK(StateOf(game, entry) == State::On);
        CHECK(CodesFor(game, entry) == kRed);
        CHECK(Read(dir.path / "mods" / "AAAE-38D.bin") == hack);
        REQUIRE(SetOn(game, entry, false));
        CHECK(StateOf(game, entry) == State::Off);
        CHECK(CodesFor(game, entry).empty());
        CHECK(SetOn(game, entry, false));
        REQUIRE(SetOn(game, entry, true));
        CHECK(CodesFor(game, entry) == kRed);
        REQUIRE(SetOn(game, entry, false));
        REQUIRE(Install(get, index, entry, game).empty()); // again, over one that is off
        CHECK(StateOf(game, entry) == State::On);
        REQUIRE(Remove(game, entry));
        CHECK(StateOf(game, entry) == State::None);
        CHECK(fs::is_empty(dir.path / "mods"));
        // Another game's in the same folder is not this one's
        Write(dir.path / "mods" / "AAAP-38D.bin", hack);
        CHECK(CodesFor(game, entry).empty());
        // A file that was changed is not run
        Write(dir.path / "mods" / "AAAE-38D.bin", File({0xC0000000, 0xFFFFFFFF}));
        CHECK(CodesFor(game, entry).empty());
    }
}

TEST_CASE("Widescreen: a game with its mod on is 16:10, until it is off",
          "[frame_cores][widescreen]") {
    Dir dir;
    const std::string rom = (dir.path / "wide.nds").string();
    Write(rom, ColourRom("CPUE")); // the index has one hack for every version of that game
    const std::string data = (dir.path / "data").string();
    const auto info = ProbeRom(rom);
    REQUIRE(info);
    CHECK(info->header_crc16 == 0x1234);
    const fs::path mod = fs::path(GameFolder(data, *info)) / "mods" / "CPUE-FFFF.bin";
    const Entry* entry = Find(BuiltIn(), info->game_code, info->header_crc16);
    REQUIRE(entry);

    std::vector<Layout::AltScreens> sizes;
    HostFrontend frontend;
    frontend.on_screens = [&](const Layout::AltScreens& screens) { sizes.push_back(screens); };
    AltCoreHost host;
    host.SetFrontend(std::move(frontend));
    CoreBootOptions opts{};
    opts.force_dsi = GENERATE(false, true);
    opts.jit_enabled = GENERATE(false, true);
    const auto boot = [&] {
        sizes.clear();
        REQUIRE(host.Boot(rom, data, /*headless_for_test=*/true, opts));
        REQUIRE(host.StepFrames(20));
    };
    const auto colour = [&] { return host.LastFrame().primary.pixels[1000] & 0xFFFFFF; };

    boot();
    const auto plain = host.GetCapabilities();
    CHECK(plain.has_mods);
    CHECK(plain.screens.top_aspect == Catch::Approx(4.0f / 3.0f));
    CHECK(colour() == 0);
    CHECK(sizes == std::vector<Layout::AltScreens>{{256, 192, 256, 192}});

    Write(mod, File(kRed));
    boot();
    auto wide = plain;
    wide.screens.top_aspect = kAspect;
    CHECK(host.GetCapabilities() == wide); // nothing else is another, the Secondary Screen neither
    CHECK(colour() != 0);
    CHECK(sizes == std::vector<Layout::AltScreens>{{307, 192, 256, 192}});
    CHECK(host.LastFrame().primary.width == 256); // the picture is the game's own

    // In no list of the player's cheats, and never saved with them
    CHECK(host.ListCheats().empty());
    CHECK(!host.SetCheatEnabled(0, false));
    REQUIRE(host.AddCheat("Mine", "02310000 00000001"));
    CHECK(host.ImportCheats({{"Theirs", "02310004 00000001", false}}) == 1);
    CHECK(host.ImportCheats({{"Same", "92300000 00000000\n12300000 0000001F\nD2000000 00000000",
                              false}}) == 1); // the hack's own code is the player's to add
    REQUIRE(host.ListCheats().size() == 3);
    CHECK(host.ListCheats()[0].name == "Mine");
    CHECK(host.ListCheats()[1].name == "Theirs");
    CHECK(host.SetCheatEnabled(0, false));
    CHECK(!host.ListCheats()[0].enabled);
    CHECK(!host.SetCheatEnabled(3, false));
    host.Reset();
    REQUIRE(host.StepFrames(20));
    CHECK(colour() != 0);
    CHECK(host.ListCheats().size() == 3);

    // Off: at the next start (the menu's Restart is a reset)
    REQUIRE(SetOn(mod.parent_path().parent_path().string(), *entry, false));
    REQUIRE(host.StepFrames(2));
    CHECK(host.GetCapabilities().screens.top_aspect == kAspect);
    sizes.clear();
    host.Reset();
    REQUIRE(host.StepFrames(20));
    CHECK(host.GetCapabilities().screens == plain.screens);
    CHECK(host.GetCapabilities().has_mods);
    CHECK(colour() == 0);
    CHECK(sizes == std::vector<Layout::AltScreens>{{256, 192, 256, 192}});
    CHECK(host.ListCheats().size() == 3);

    // As they were saved, with the mod on
    REQUIRE(SetOn(mod.parent_path().parent_path().string(), *entry, true));
    boot();
    CHECK(host.GetCapabilities().screens.top_aspect == kAspect);
    CHECK(host.ListCheats().size() == 3);

    // A game with no hack in the index runs none, whatever is in its folder
    host.Stop();
    Write(rom, ColourRom("ZZZE"));
    Write(fs::path(GameFolder(data, *ProbeRom(rom))) / "mods" / "ZZZE-FFFF.bin", File(kRed));
    boot();
    CHECK(!host.GetCapabilities().has_mods);
    CHECK(host.GetCapabilities().screens == plain.screens);
    CHECK(colour() == 0);
}
