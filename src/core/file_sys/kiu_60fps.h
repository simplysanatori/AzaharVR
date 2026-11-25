// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharFrame kiu60: Kid Icarus: Uprising at 60 pictures a second (a test, PC build only, off).
// The app makes an IPS patch of five words from the game's own code as it loads, when every word
// is the game's, and applies it after any mod's code patch. Nothing is a game file; nothing is
// kept. The frame rate is set once at the game's start: a save state keeps the rate it was made at.
//
// What the words are (European game's addresses; found by reading the code, V = seen running):
//   The vsync manager (singleton, pointer at 0xD63824) is set up once: +0x50 pictures a second,
//   +0x54 VBlanks a picture. Its wait (0x910524) holds each picture until that many VBlanks, plus
//   a remainder (+0xA8), passed since the one before, and notes how many did (+0xB0).
//   The pacer (0x828FCC) runs the game's update once per `remainder + VBlanks / 2`, and before each
//   update gives the frame counter (pointer at 0xD635A0, +0x20) the update's length in 1/60 s,
//   from which every actor gets its dt. So the game's speed comes from the length, not the rate.
//     rate      mov r0, #30         -> #60        30 -> 60 pictures a second
//     interval  mov r0, #2          -> #1         a picture every VBlank
//     parity    and r2, r2, #1      -> mov r2, #0 the wait's remainder is "VBlanks mod 2": with one
//                                                 VBlank a picture it would add a VBlank to every
//                                                 other wait (40 a second)
//     updates   add r6, r1, r0 lsr 1 -> add r6, r1, r0   one update per VBlank
//     length    2.0f                -> 1.0f       an update is 1/60 s of game time
// The five places are the ones named in UprisingHack's notes (patricksmill/UprisingHack,
// GPL-3.0-or-later, kiu/analysis/FINDINGS.md "Frame rate", USA Rev 1), checked here against both
// games' code; the words and this file are our own.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace FileSys::Kiu60 {

constexpr const char* kSwitch = "AZAHARFRAME_KIU_60FPS"; ///< =1, read when a game loads

constexpr uint32_t kCodeBase = 0x100000; ///< the address of code.bin's first byte

struct Word {
    uint32_t address;
    uint32_t game;
    uint32_t ours; ///< equal to `game`: context, only checked
};

struct Build {
    uint64_t title;
    const char* name;
    std::array<Word, 8> words;
};

constexpr uint32_t kRate30 = 0xE3A0001E, kRate60 = 0xE3A0003C;       // mov r0, #30 | #60
constexpr uint32_t kInterval2 = 0xE3A00002, kInterval1 = 0xE3A00001; // mov r0, #2 | #1
constexpr uint32_t kParity = 0xE2022001, kNoParity = 0xE3A02000; // and r2, r2, #1 | mov r2, #0
constexpr uint32_t kHalf = 0xE08160A0, kWhole = 0xE0816000; // add r6, r1, r0, lsr #1 | add r6, r1, r0
constexpr uint32_t kTwo = 0x40000000, kOne = 0x3F800000;    // 2.0f | 1.0f
constexpr uint32_t kStoreRate = 0xE5810050;     // str r0, [r1, #0x50]
constexpr uint32_t kStoreInterval = 0xE5810054; // str r0, [r1, #0x54]
constexpr uint32_t kLoadLength = 0xED9F0A06;    // vldr s0, [pc, #24]: the length's only reader

constexpr Build MakeBuild(uint64_t title, const char* name, uint32_t setup, uint32_t wait,
                          uint32_t pacer) {
    return {title,
            name,
            {{{setup, kRate30, kRate60},
              {setup + 4, kStoreRate, kStoreRate},
              {setup + 8, kInterval2, kInterval1},
              {setup + 12, kStoreInterval, kStoreInterval},
              {wait, kParity, kNoParity},
              {pacer + 0x5C, kHalf, kWhole},
              {pacer - 0x20, kLoadLength, kLoadLength},
              {pacer, kTwo, kOne}}}};
}

/// `setup`: the rate's mov; `wait`: the parity's and; `pacer`: the length's literal.
constexpr std::array<Build, 2> kBuilds{{
    MakeBuild(0x0004000000030200ull, "Europe", 0x48A87C, 0x9105B4, 0x828FC8),
    MakeBuild(0x0004000000030100ull, "USA Rev 1", 0x48A7E4, 0x91041C, 0x828E30),
}};

enum class Outcome {
    Made,      ///< every word is the game's: the patch is in `ips`
    NotMapped, ///< another title
    Differs,   ///< a word is neither the game's nor ours (another build, a mod): nothing made
    Already,   ///< the five words are ours already (a mod did it): nothing to do
};

struct Patch {
    Outcome result = Outcome::NotMapped;
    const Build* build = nullptr;
    uint32_t address = 0; ///< with Differs: the first such place, and what it holds
    uint32_t found = 0;
    std::vector<uint8_t> ips;
};

inline std::optional<uint32_t> ReadWord(std::span<const uint8_t> code, uint32_t address) {
    if (address < kCodeBase || address - kCodeBase + 4 > code.size()) {
        return std::nullopt;
    }
    const uint8_t* p = code.data() + (address - kCodeBase);
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
           static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

/// The patch for this game's code (code.bin as loaded, mods applied), all words or none.
inline Patch Make(uint64_t title, std::span<const uint8_t> code) {
    Patch out;
    for (const Build& build : kBuilds) {
        if (build.title != title) {
            continue;
        }
        out.build = &build;
        std::size_t ours = 0, changed = 0;
        for (const Word& w : build.words) {
            const std::optional<uint32_t> v = ReadWord(code, w.address);
            if (!v || (*v != w.game && *v != w.ours)) {
                out.result = Outcome::Differs;
                out.address = w.address;
                out.found = v.value_or(0);
                return out;
            }
            if (w.game != w.ours) {
                ++changed;
                ours += *v == w.ours ? 1 : 0;
            }
        }
        if (ours == changed) {
            out.result = Outcome::Already;
            return out;
        }
        if (ours != 0) { // some of ours, some of the game's: not a state this app makes
            out.result = Outcome::Differs;
            return out;
        }
        // IPS: "PATCH", then {offset: 3 bytes, size: 2 bytes, data}, big endian, then "EOF".
        out.ips = {'P', 'A', 'T', 'C', 'H'};
        for (const Word& w : build.words) {
            if (w.game == w.ours) {
                continue;
            }
            const uint32_t offset = w.address - kCodeBase;
            for (const uint32_t b : {offset >> 16, offset >> 8, offset, 0u, 4u, w.ours,
                                     w.ours >> 8, w.ours >> 16, w.ours >> 24}) {
                out.ips.push_back(static_cast<uint8_t>(b));
            }
        }
        out.ips.insert(out.ips.end(), {'E', 'O', 'F'});
        out.result = Outcome::Made;
        return out;
    }
    return out;
}

/// Whether the switch's value asks for the patch in this build of the app (the PC one only).
constexpr bool WantedFor(std::optional<std::string_view> value, bool pc_build) {
    return pc_build && value && *value == "1";
}

} // namespace FileSys::Kiu60
