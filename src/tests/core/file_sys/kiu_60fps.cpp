// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <map>
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
        REQUIRE(p.ips.size() == 5 + kChanged * 9 + 3);
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
        REQUIRE(changed == 4 * kChanged);
        REQUIRE(differ <= 4 * kChanged); // nothing but the words
        REQUIRE(differ >= kChanged);
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
    // The touch's words and the C-Stick mods' hook places.
    REQUIRE(eur.words[10].address == 0x6D219C);
    REQUIRE(eur.words[11].address == 0x6D280C);
    REQUIRE(eur.words[12].address == 0x6D2F7C);
    REQUIRE(eur.words[13].address == 0x7DAD90);
    REQUIRE(usa.words[12].address == 0x6D2E30);
    REQUIRE(usa.words[13].address == 0x7DABF8);
    for (const Build& b : kBuilds) { // each place once
        for (std::size_t i = 0; i < b.words.size(); ++i) {
            for (std::size_t j = 0; j < i; ++j) {
                REQUIRE(b.words[i].address != b.words[j].address);
            }
        }
    }
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

TEST_CASE("Kid Icarus 60: the setting, and the switch over it", "[kiu60]") {
    REQUIRE(Wanted(std::nullopt, true));
    REQUIRE_FALSE(Wanted(std::nullopt, false));
    REQUIRE(Wanted("1", false));
    REQUIRE_FALSE(Wanted("0", true));
    REQUIRE(Wanted("", true)); // neither: the setting
    REQUIRE_FALSE(Wanted("2", false));
}

TEST_CASE("Kid Icarus 60: the Turn Zone's push is the game's update long", "[kiu60]") {
    REQUIRE(UpdateShareFor(0x3F800000) == 0.5f); // 60
    REQUIRE(UpdateShareFor(0x40000000) == 1.0f); // 30
    REQUIRE(UpdateShareFor(0x40400000) == 1.0f); // anything else: as it was
    REQUIRE(UpdateShareFor(0) == 1.0f);
}

TEST_CASE("Kid Icarus 60: what the run was started at", "[kiu60]") {
    REQUIRE(RunFor(Code::Game, false) == Run::At30);
    REQUIRE(RunFor(Code::Game, true) == Run::At60);
    REQUIRE(RunFor(Code::Ours, false) == Run::At60); // a mod's code.ips: the app does nothing
    REQUIRE(RunFor(Code::Differs, false) == Run::Leave);
}

// The mod's files: each region's load/mods/<title>/exefs/code.ips, from the same table. Not run
// with the rest: KIU60_MOD_DIR=dir tests "[.kiu60mod]".
TEST_CASE("Kid Icarus 60: the mod's files", "[.][kiu60mod]") {
    const char* dir = std::getenv("KIU60_MOD_DIR");
    REQUIRE(dir != nullptr);
    for (const Build& b : kBuilds) {
        char title[17];
        std::snprintf(title, sizeof(title), "%016llX", static_cast<unsigned long long>(b.title));
        const std::filesystem::path out =
            std::filesystem::path(dir) / b.name / "load" / "mods" / title / "exefs";
        std::filesystem::create_directories(out);
        const std::vector<uint8_t> ips = MakeIps(b);
        std::ofstream file(out / "code.ips", std::ios::binary);
        file.write(reinterpret_cast<const char*>(ips.data()),
                   static_cast<std::streamsize>(ips.size()));
        REQUIRE(file.good());
    }
}

// The mod put on the game's code is the app's patch.
TEST_CASE("Kid Icarus 60: the mod is the option's patch", "[kiu60]") {
    for (const Build& b : kBuilds) {
        std::vector<uint8_t> code(0x900000, 0);
        for (const Word& w : b.words) {
            const std::size_t at = w.address - kCodeBase;
            for (int i = 0; i < 4; ++i) {
                code[at + i] = static_cast<uint8_t>(w.game >> (8 * i));
            }
        }
        const Patch p = Make(b.title, code);
        REQUIRE(p.result == Outcome::Made);
        REQUIRE(p.ips == MakeIps(b));
        REQUIRE(FileSys::Patch::ApplyIpsPatch(p.ips, code) == Loader::ResultStatus::Success);
        // and then the option finds its code there already and puts nothing on.
        const Patch again = Make(b.title, code);
        REQUIRE(again.result == Outcome::Already);
        REQUIRE(again.ips.empty());
    }
}

namespace {

/// A game's memory: the words, and the vsync manager once it is set up.
struct Game {
    std::map<uint32_t, uint32_t> m;
    int writes = 0;
    explicit Game(const Build& b, bool sixty, bool manager = true) {
        for (const Word& w : b.words) {
            m[w.address] = sixty ? w.ours : w.game;
        }
        if (manager) {
            m[kVsyncManager] = 0x14001000;
            m[0x14001000 + kManagerRate] = sixty ? 60 : 30;
            m[0x14001000 + kManagerInterval] = sixty ? 1 : 2;
        }
    }
    Turned To(const Build& b, bool sixty) {
        const auto write = [this](uint32_t a, uint32_t v) {
            m[a] = v;
            ++writes;
        };
        return TurnTo(
            b, sixty,
            [this](uint32_t a) {
                const auto i = m.find(a);
                return i == m.end() ? std::nullopt : std::optional<uint32_t>(i->second);
            },
            write, write);
    }
};

} // namespace

TEST_CASE("Kid Icarus 60: a save state made at the other rate", "[kiu60]") {
    for (const Build& b : kBuilds) {
        for (const bool sixty : {false, true}) {
            Game game(b, !sixty);
            REQUIRE(game.To(b, sixty) == Turned::Yes);
            REQUIRE(game.m == Game(b, sixty).m); // exactly a game started at it
            const int writes = game.writes;
            REQUIRE(game.To(b, sixty) == Turned::No); // and nothing more
            REQUIRE(game.writes == writes);
            Game same(b, sixty);
            REQUIRE(same.To(b, sixty) == Turned::No);
            REQUIRE(same.writes == 0);
            // Not set up yet, or a rate that is neither: not touched.
            Game early(b, !sixty, false);
            REQUIRE(early.To(b, sixty) == Turned::No);
            REQUIRE(early.writes == 0);
            Game odd(b, !sixty);
            odd.m[0x14001000 + kManagerRate] = 20;
            REQUIRE(odd.To(b, sixty) == Turned::No);
            REQUIRE(odd.writes == 0);
            // Another mod's 60 (the five words alone) or any word changed: left alone.
            Game mod(b, !sixty);
            mod.m[b.words[13].address] = 0xE1A00000;
            REQUIRE(mod.To(b, sixty) == Turned::Differs);
            REQUIRE(mod.writes == 0);
        }
    }
}

// The touch's rule, in C++: the words above do this (seen running, reports/102/rig60). One call an
// update at 60; the speed in use comes back.
namespace {
struct Rule {
    float last = 0, use = 0;
    int still = 0;
    float At60(float e) {
        const float mean = (e + last) * 0.5f;
        last = e;
        still = mean != 0 ? 0 : still + 1;
        if (mean != 0 || still >= 3) {
            use = mean;
        }
        return use;
    }
};
/// The game's at 30: an update holds two samples; the newest speed that is not zero of two.
struct Game30 {
    float before = 0;
    float At30(float first, float second) {
        const float e = (first + second) * 0.5f;
        const float out = e != 0 ? e : before;
        before = e;
        return out;
    }
};
} // namespace

TEST_CASE("Kid Icarus 60: the touch turns as far as at 30", "[kiu60]") {
    // A stylus that moves `step` every `every` VBlanks, 120 VBlanks, then rests.
    for (const int every : {1, 2, 4}) {
        for (const float step : {1.0f, 2.0f, 20.0f}) {
            Rule rule;
            Game30 game;
            float at60 = 0, at30 = 0;
            for (int vb = 0; vb < 132; vb += 2) {
                const auto d = [&](int v) { return v < 120 && v % every == 0 ? step : 0.0f; };
                at60 += rule.At60(d(vb)) + rule.At60(d(vb + 1));
                at30 += 2 * game.At30(d(vb), d(vb + 1));
            }
            INFO(every << " VBlanks, " << step << ": " << at60 << " and " << at30);
            REQUIRE(std::abs(at60 - at30) <= 0.03f * at30);
        }
    }
}
