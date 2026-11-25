// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <cstdint>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "core/file_sys/kiu_60fps.h"
#include "core/file_sys/patch.h"

// AzaharFrame kiu60: the 60 patch's maker over invented code (only the words it names).
using namespace FileSys::Kiu60;

namespace {

void Put(std::vector<uint8_t>& code, uint32_t address, uint32_t v) {
    for (uint32_t i = 0; i < 4; ++i) {
        code[address - kCodeBase + i] = static_cast<uint8_t>(v >> (8 * i));
    }
}

std::vector<uint8_t> GameCode(const Build& b) {
    std::vector<uint8_t> code(0x900000, 0);
    for (const Word& w : b.words) {
        Put(code, w.address, w.game);
    }
    return code;
}

} // namespace

TEST_CASE("Kid Icarus 60: the patch is made from the game's own words", "[kiu60]") {
    for (const Build& b : kBuilds) {
        std::vector<uint8_t> code = GameCode(b);
        const Patch p = Make(b.title, code);
        REQUIRE(p.result == Outcome::Made);
        REQUIRE(p.build == &b);
        REQUIRE(p.ips.size() == 5 + 5 * 9 + 3);
        const std::vector<uint8_t> before = code;
        REQUIRE(FileSys::Patch::ApplyIpsPatch(p.ips, code) == Loader::ResultStatus::Success);
        std::size_t changed = 0;
        for (const Word& w : b.words) {
            REQUIRE(ReadWord(code, w.address) == w.ours);
            changed += w.game != w.ours ? 4 : 0;
        }
        std::size_t differ = 0;
        for (std::size_t i = 0; i < code.size(); ++i) {
            differ += code[i] != before[i] ? 1 : 0;
        }
        REQUIRE(changed == 20);
        REQUIRE(differ <= 20); // nothing but the five words
        REQUIRE(differ >= 5);
        REQUIRE(Make(b.title, code).result == Outcome::Already);
    }
}

TEST_CASE("Kid Icarus 60: the words", "[kiu60]") {
    const Build& eur = kBuilds[0];
    REQUIRE(eur.words[0].address == 0x48A87C);
    REQUIRE(eur.words[2].address == 0x48A884);
    REQUIRE(eur.words[4].address == 0x9105B4);
    REQUIRE(eur.words[5].address == 0x829024);
    REQUIRE(eur.words[7].address == 0x828FC8);
    const Build& usa = kBuilds[1];
    REQUIRE(usa.words[0].address == 0x48A7E4);
    REQUIRE(usa.words[2].address == 0x48A7EC);
    REQUIRE(usa.words[4].address == 0x91041C);
    REQUIRE(usa.words[5].address == 0x828E8C);
    REQUIRE(usa.words[7].address == 0x828E30);
}

TEST_CASE("Kid Icarus 60: nothing for other code", "[kiu60]") {
    const Build& b = kBuilds[0];
    REQUIRE(Make(0x0004000000030000ull, GameCode(b)).result == Outcome::NotMapped);
    // the other build's code under this title: off
    REQUIRE(Make(b.title, GameCode(kBuilds[1])).result == Outcome::Differs);
    for (const Word& w : b.words) { // any one word changed, a context word too
        std::vector<uint8_t> code = GameCode(b);
        Put(code, w.address, 0xE1A00000);
        const Patch p = Make(b.title, code);
        REQUIRE(p.result == Outcome::Differs);
        REQUIRE(p.address == w.address);
        REQUIRE(p.found == 0xE1A00000);
        REQUIRE(p.ips.empty());
    }
    // one of ours among the game's: not made
    std::vector<uint8_t> code = GameCode(b);
    Put(code, b.words[0].address, b.words[0].ours);
    REQUIRE(Make(b.title, code).result == Outcome::Differs);
    // code too short
    REQUIRE(Make(b.title, std::vector<uint8_t>(0x1000, 0)).result == Outcome::Differs);
}

TEST_CASE("Kid Icarus 60: the switch, on the PC build only", "[kiu60]") {
    REQUIRE(WantedFor("1", true));
    REQUIRE_FALSE(WantedFor("1", false));
    REQUIRE_FALSE(WantedFor("0", true));
    REQUIRE_FALSE(WantedFor(std::nullopt, true));
}
