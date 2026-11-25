// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <array>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/alt_core_types.h"
#include "frame_cores/rom_info.h"
#include "frame_cores/second_screen.h"

using Common::AltCore::CoreKind;
using namespace FrameCores;

namespace {

// Every gba-ds profile, by its first code
constexpr std::array kMapGames{
    "A2CE", "A3AE", "A3UJ", "A4ND", "A7KE", "A88E", "AA2E", "AAME", "AAUJ", "AB2E", "AB4E", "ABFE",
    "ACHE", "AE2E", "AE7E", "AFEJ", "AFXE", "AGFD", "AGSD", "ALDE", "ALFE", "ALGE", "AMAE", "AMTE",
    "AREE", "ASEE", "AT3D", "ATOE", "AVSE", "AW2E", "AWAE", "AWRE", "AX4E", "AXPD", "AXPE", "AXPI",
    "AY5E", "AZ2E", "AZLE", "B8CE", "B8KE", "BD6E", "BDTE", "BE8E", "BENE", "BFFE", "BG3E", "BGDE",
    "BGW1", "BGW2", "BHTE", "BKZE", "BM5E", "BMXE", "BPED", "BPEI", "BPGD", "BPGE", "BPGI", "BRLE",
    "BSKE", "BY3J", "BY6E", "BY7E", "BYIJ", "BZ4E", "BZ5E", "BZ6E", "BZME", "U3IE",
};
constexpr std::array kGamesWithoutMap{ // the HUD, a goal sheet, a table or a compilation
    "A2GE", "A2NE", "A3VE", "AFZC", "AMKE", "AMWE", "ANXE", "ASOE", "ATHD", "AVRE", "B3SE", "BCNE",
    "BFTJ", "BFZE", "BGWJ", "BPPE",
};

} // namespace

TEST_CASE("MapScreenAvailable follows the gba-ds rule", "[frame_cores]") {
    MapScreenFacts f;
    CHECK_FALSE(MapScreenAvailable(f)); // no profile

    f.profile_found = true;
    f.crop_w = 240;
    f.crop_h = 160;
    CHECK(MapScreenAvailable(f));

    f.logo_only = true;
    CHECK_FALSE(MapScreenAvailable(f));
    f.logo_only = false;

    f.crop_h = 0;
    CHECK_FALSE(MapScreenAvailable(f)); // empty crop

    f.tight_w = 100; // the tight size wins when it is set
    f.tight_h = 80;
    CHECK(MapScreenAvailable(f));
    f.tight_h = 0;
    CHECK_FALSE(MapScreenAvailable(f));
}

TEST_CASE("HasTwoScreens by system and game code", "[frame_cores]") {
    CHECK_FALSE(HasTwoScreens(CoreKind::NDS, "B8KE"));
    CHECK_FALSE(HasTwoScreens(CoreKind::DSi, "B8KE"));
    CHECK_FALSE(HasTwoScreens(CoreKind::N3DS, "B8KE"));
    CHECK_FALSE(HasTwoScreens(CoreKind::GB, "B8KE"));
    CHECK_FALSE(HasTwoScreens(CoreKind::GBA, ""));
    CHECK_FALSE(HasTwoScreens(CoreKind::GBA, "ZZZZ")); // no profile
    CHECK(HasTwoScreens(CoreKind::GBA, "B8KE"));       // a map profile
    CHECK(HasTwoScreens(CoreKind::GBA, "B8KP"));       // a variant of it
}

TEST_CASE("HasTwoScreens for every gba-ds game", "[frame_cores]") {
    CHECK(kMapGames.size() + kGamesWithoutMap.size() == 86);
    for (const char* code : kMapGames) {
        INFO(code);
        CHECK(HasTwoScreens(CoreKind::GBA, code));
    }
    for (const char* code : kGamesWithoutMap) {
        INFO(code);
#ifdef HAVE_GBADS_GAME
        CHECK(HasTwoScreens(CoreKind::GBA, code));
#else
        CHECK_FALSE(HasTwoScreens(CoreKind::GBA, code));
#endif
    }
}

TEST_CASE("GbaDsScreenAvailable: a map, or what game.c draws", "[frame_cores]") {
    MapScreenFacts f;
    f.hud = f.sheet = f.table = f.pick = true;
    CHECK_FALSE(GbaDsScreenAvailable(f, true)); // no profile

    f = {};
    f.profile_found = true;
    f.logo_only = true;
    CHECK_FALSE(GbaDsScreenAvailable(f, true)); // the logo alone
    for (bool MapScreenFacts::*what : {&MapScreenFacts::hud, &MapScreenFacts::sheet,
                                       &MapScreenFacts::table, &MapScreenFacts::pick}) {
        MapScreenFacts one = f;
        one.*what = true;
        CHECK(GbaDsScreenAvailable(one, true));
        CHECK_FALSE(GbaDsScreenAvailable(one, false));
    }

    f.logo_only = false;
    f.crop_w = 240;
    f.crop_h = 160;
    CHECK(GbaDsScreenAvailable(f, true));
    CHECK(GbaDsScreenAvailable(f, false));
}

TEST_CASE("SystemOf and FilterMatches", "[frame_cores]") {
    RomInfo info;
    CHECK(SystemOf(info) == GameSystem::N3DS);
    info.kind = CoreKind::NDS;
    CHECK(SystemOf(info) == GameSystem::DS);
    info.kind = CoreKind::DSi;
    CHECK(SystemOf(info) == GameSystem::DS);
    info.kind = CoreKind::GBA;
    CHECK(SystemOf(info) == GameSystem::GBA);
    info.kind = CoreKind::GB;
    CHECK(SystemOf(info) == GameSystem::GB);
    info.is_gbc = true;
    CHECK(SystemOf(info) == GameSystem::GBC);

    CHECK(FilterMatches(SystemFilter::All, GameSystem::GBA));
    CHECK(FilterMatches(SystemFilter::N3DS, GameSystem::N3DS));
    CHECK_FALSE(FilterMatches(SystemFilter::N3DS, GameSystem::DS));
    CHECK(FilterMatches(SystemFilter::DS, GameSystem::DS));
    CHECK_FALSE(FilterMatches(SystemFilter::GBA, GameSystem::GB));
    CHECK(FilterMatches(SystemFilter::GB, GameSystem::GB));
    CHECK(FilterMatches(SystemFilter::GB, GameSystem::GBC));
    CHECK_FALSE(FilterMatches(SystemFilter::GB, GameSystem::GBA));
}

TEST_CASE("BadgeRule: a companion or gba-ds gives Two Screens, GBA and Game Boy only", "[frame_cores]") {
    for (const CoreKind kind : {CoreKind::GBA, CoreKind::GB}) {
        CHECK(BadgeRule(kind, false, false) == GameBadges{false, false});
        CHECK(BadgeRule(kind, true, false) == GameBadges{true, true});
        CHECK(BadgeRule(kind, false, true) == GameBadges{true, false});
        CHECK(BadgeRule(kind, true, true) == GameBadges{true, true});
    }
    for (const CoreKind kind : {CoreKind::N3DS, CoreKind::NDS, CoreKind::DSi}) {
        CHECK(BadgeRule(kind, true, true) == GameBadges{});
    }
}

TEST_CASE("MayBePokeDaisy: which games are read whole", "[frame_cores]") {
    RomInfo info{};
    info.kind = CoreKind::GBA;
    for (const char* code : {"BPRE", "BPGJ", "BPED", "AXVE", "AXPS"}) {
        info.game_code = code;
        CHECK(MayBePokeDaisy(info));
    }
    info.game_code = "AGSE"; // Golden Sun
    CHECK_FALSE(MayBePokeDaisy(info));
    info.title = "POKEMON SOME"; // a hack with a code of its own
    CHECK(MayBePokeDaisy(info));
    info = {};
    info.kind = CoreKind::GBA; // homebrew: no code, no title
    CHECK_FALSE(MayBePokeDaisy(info));
    info.kind = CoreKind::GB;
    CHECK(MayBePokeDaisy(info));
    info.kind = CoreKind::NDS;
    info.title = "POKEMON D";
    CHECK_FALSE(MayBePokeDaisy(info));
}

TEST_CASE("BadgesOf: gba-ds's rule without PokeDaisy", "[frame_cores]") {
    RomInfo info{};
    info.kind = CoreKind::GBA;
    info.game_code = "B8KE";
    CHECK(BadgesOf(info, "/nonexistent.gba") == GameBadges{true, false});
    info.game_code = "BPRE"; // asked, but the file is not there
    CHECK_FALSE(BadgesOf(info, "/nonexistent.gba").pokedaisy);
    info.kind = CoreKind::NDS;
    info.game_code = "B8KE";
    CHECK(BadgesOf(info, "/nonexistent.nds") == GameBadges{});
}

TEST_CASE("RegionOf: a DS game's code, a file's tag", "[frame_cores]") {
    RomInfo info{};
    info.kind = CoreKind::NDS;
    const std::array<std::pair<const char*, const char*>, 6> cases{{{"AMCE", "North America"},
                                                                    {"AMCJ", "Japan"},
                                                                    {"AMCP", "Europe"},
                                                                    {"AMCD", "Europe"},
                                                                    {"AMCU", "Australia"},
                                                                    {"AMCQ", ""}}};
    for (const auto& [code, region] : cases) {
        info.game_code = code;
        CHECK(std::string(RegionOf(info)) == region);
    }
    info.kind = CoreKind::GBA; // its file's name says
    CHECK(std::string(RegionOf(info)).empty());

    using R = std::vector<std::string>;
    const auto of = [](const std::string& name) {
        R out;
        for (const char* r : RegionsOfName(name)) {
            out.emplace_back(r);
        }
        return out;
    };
    CHECK(of("Golden Sun (USA, Europe)") == R{"North America", "Europe"});
    CHECK(of("Pokemon - Emerald Version (Germany) (Rev 1)") == R{"Europe"});
    CHECK(of("Tetris (World) (Rev A)") == R{"Region free"});
    CHECK(of("Zelda (J) [!]") == R{"Japan"});
    CHECK(of("Zelda (UE)") == R{"North America", "Europe"});
    CHECK(of("Pokemon Lazarus (v1.2) (Hack)").empty());
    CHECK(of("Game (En,Fr,De) (Europe)") == R{"Europe"}); // the languages are no region
    CHECK(of("Game (USA, Beta)").empty());
    CHECK(of("No Tags").empty());
    CHECK(of("Broken (USA").empty());
}

TEST_CASE("CleanName and DisplayTitle", "[frame_cores]") {
    CHECK(CleanName("Golden Sun (USA, Europe)") == "Golden Sun");
    CHECK(CleanName("Legend of Zelda, The - The Minish Cap (USA) [!]") ==
          "The Legend of Zelda - The Minish Cap");
    CHECK(CleanName("[Hack] Some_Game (v1.2)") == "Some_Game"); // the name as it is
    CHECK(CleanName("Legend, The") == "The Legend");
    CHECK(CleanName("(USA)").empty());
    CHECK(CleanName("Odd ) Name") == "Odd ) Name");

    CHECK(CleanName("Legend of Zelda, The - A Link to the Past & Four Swords (USA)") ==
          "The Legend of Zelda - A Link to the Past & Four Swords");
    CHECK(CleanName("Final Fantasy I & II - Dawn of Souls (USA)  (Rev 1)") ==
          "Final Fantasy I & II - Dawn of Souls");
    CHECK(CleanName("Pokemon Emerald, The Hack") == "Pokemon Emerald, The Hack"); // not at the end

    RomInfo info{};
    info.kind = CoreKind::GB;
    info.title = "ZELDA";
    CHECK(DisplayTitle(info, "/roms/My Game (Japan).gb") == "My Game");
    CHECK(DisplayTitle(info, "/roms/(USA).gb") == "ZELDA"); // nothing left of the name
    info.kind = CoreKind::GBA;
    info.title = "POKEMON EMER";
    info.game_code = "BPEE"; // a hack keeps its base game's code: its own name shows
    CHECK(DisplayTitle(info, "/roms/Pokemon Lazarus v1.2.gba") == "Pokemon Lazarus v1.2");
    CHECK(DisplayTitle(info, "/roms/Pokemon Emerald (USA, Europe).gba.zip") == "Pokemon Emerald");
    CHECK(DisplayTitle(info, "/roms/Zipped (USA).zip") == "Zipped");
    CHECK(DisplayTitle(info, "/roms/Game.v2.gba") == "Game.v2");
    info.kind = CoreKind::NDS;
    info.title = "A DS Title";
    CHECK(DisplayTitle(info, "/roms/file.nds") == "A DS Title");
}
