// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstdint>
#include <map>
#include <catch2/catch_test_macros.hpp>
#include <vector>
#include "frame_xr/input/kiu_model.h"
#include "frame_xr/kiu_first_person.h"

// AzaharFrame kiu133: the player's model node found by its holder's shape, wherever the holder
// sits in the actor (frame_xr/input/kiu_model.h).
using namespace FrameXr::Input::KiuModel;

namespace {

class Mem final : public Reader {
public:
    std::map<uint32_t, uint32_t> words;
    bool Read32(uint32_t addr, uint32_t& v) const override {
        const auto it = words.find(addr);
        if (it == words.end()) {
            return false;
        }
        v = it->second;
        return true;
    }
    bool Read8(uint32_t, uint8_t&) const override {
        return false;
    }
};

constexpr uint32_t kVt = 0xD1E990;
constexpr uint32_t kActor = 0x141DFE90;
constexpr uint32_t kModel = 0x141DF5B8;

/// An actor with its holder's model pointer at `off` (and the actor word 8 before it).
Mem ActorWithModelAt(uint32_t off) {
    Mem m;
    for (uint32_t o = 0; o < 0x800; o += 4) {
        m.words[kActor + o] = 1; // filler
    }
    m.words[kActor + off - 0xC] = 0xD760B8;
    m.words[kActor + off - 8] = kActor;
    m.words[kActor + off - 4] = 0xFFFF0001;
    m.words[kActor + off] = kModel;
    m.words[kModel] = kVt;
    return m;
}

} // namespace

TEST_CASE("Kid Icarus: the player's model node by its holder", "[frame_xr][kiu133]") {
    for (const uint32_t off : {0x344u, 0x34Cu, 0x3CCu}) { // the three places seen on the desktop
        const Mem m = ActorWithModelAt(off);
        const Found f = FindModel(m, kActor, kVt, 0);
        CHECK(f.off == off);
        CHECK(f.model == kModel);
        CHECK(FindModel(m, kActor, kVt, off).model == kModel); // the hint
    }
    SECTION("the model pointer without the actor before it is not taken") {
        Mem m = ActorWithModelAt(0x344);
        m.words[kActor + 0x344 - 8] = 0x141E0000;
        CHECK(FindModel(m, kActor, kVt, 0).model == 0);
    }
    SECTION("another class is not taken") {
        Mem m = ActorWithModelAt(0x34C);
        m.words[kModel] = 0xD1E880; // the child node's class
        CHECK(FindModel(m, kActor, kVt, 0).model == 0);
    }
    SECTION("the actor's link wins, also off the actor (on the heap)") {
        Mem m = ActorWithModelAt(0x3CC);
        const uint32_t link = 0x14000000, other = 0x141E0000;
        m.words[kActor + kActorLink] = link;
        m.words[link + kLinkActor] = kActor;
        m.words[link + kLinkModel] = other;
        m.words[other] = kVt;
        const Found f = FindModel(m, kActor, kVt, 0);
        CHECK(f.off == kByLink);
        CHECK(f.model == other);
        m.words[link + kLinkActor] = 0x1; // not this actor's: the holder's shape
        CHECK(FindModel(m, kActor, kVt, kByLink).off == 0x3CCu);
    }
    SECTION("a stale hint falls back to the search") {
        const Mem m = ActorWithModelAt(0x3CC);
        CHECK(FindModel(m, kActor, kVt, 0x344).off == 0x3CCu);
    }
}

TEST_CASE("Kid Icarus: Pit Opacity's draws", "[frame_xr][kiu133]") {
    // Seen with him drawn (twice each eye) and hidden: his are the ones only seen drawn.
    const std::vector<uint32_t> drawn{0x181E824C, 0x181EAAA8, 0x18442B9C, 0x181E824C, 0x18442B9C,
                                      0x181F08FC};
    const std::vector<uint32_t> hidden{0x18442B9C, 0x18442B9C};
    CHECK(PitDrawsFrom(drawn, hidden) ==
          std::vector<uint32_t>{0x181E824C, 0x181EAAA8, 0x181F08FC});
    CHECK(PitDrawsFrom(drawn, {}).empty()); // no frame looked at hidden: nothing known
}

TEST_CASE("Kid Icarus: Camera's two keys", "[frame_xr][kiu133]") {
    using namespace FrameXr::Kiu::FirstPerson;
    // A file from before keeps its meaning: Over the Shoulder alone is 1, nothing is 0.
    CHECK(ViewFor(false, false) == 0);
    CHECK(ViewFor(true, false) == 1);
    CHECK(ViewFor(false, true) == 2);
    CHECK(ViewFor(true, true) == 2); // the Game's Camera wins
    for (const int v : {0, 1, 2}) {
        CHECK(ViewFor(ViewOverShoulder(v), ViewGameCamera(v)) == v);
    }
    CHECK(GameCameraFor(nullptr, true));
    CHECK_FALSE(GameCameraFor(nullptr, false));
    CHECK(GameCameraFor("1", false));
    CHECK_FALSE(GameCameraFor("0", true)); // the switch wins
}
