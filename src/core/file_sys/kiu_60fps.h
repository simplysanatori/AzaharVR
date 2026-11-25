// Copyright Azahar for Frame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiu60: Kid Icarus: Uprising at 60 pictures a second (vr_kiu_60fps, per game, off).
// The app makes an IPS patch from the game's own code as it loads, when every word is the game's,
// and applies it after any mod's code patch. Nothing is a game file; nothing is kept. Pure: the
// emulator's side is core/loader/ncch.cpp and frame_xr/kiu_hooks.cpp (a save state, below).
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
// These five changes, places and values, are the same as UprisingHack's 60 FPS patch for USA Rev 1
// (patricksmill/UprisingHack, GPL-3.0-or-later: kiu/scripts/make_60fps_ips.py and
// kiu/analysis/FINDINGS.md "Frame rate"), checked here against both games' code. The touch's words
// below, the context words and the rest of this file are our own.
//
// The touch (0x7DAC34; V): each update the game takes the stylus's speed over that update's touch
// samples (one a VBlank), keeps the last two, and uses the newest one that is not zero; the speed,
// eased at 30 a second times dt, times dt moves the reticle, and what passes the aim box turns
// the land camera. At 30 an update holds two samples, so a sample without movement (a slow
// stylus, whole pixels) is averaged with its neighbour; at 60 it is an update of its own, and the
// speed before it is used again: up to twice the turn. Ours, at 60:
//     +0x20 the last update's speed, +0x28 the speed in use, +0x30 updates without movement
//     the speed = the mean of the last two updates' (two VBlanks, as at 30); no movement in
//     either: the speed in use stays for two more updates (one of the game's at 30)
//     the easing 30 -> 60: at once, as 30 times 1/30 s is
// The words are in the function's own room (its old list of speeds), nothing outside it.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace FileSys::Kiu60 {

constexpr const char* kSwitch = "AZAHARFRAME_KIU_60FPS"; ///< =1 or =0, read when a game loads

constexpr uint32_t kCodeBase = 0x100000; ///< the address of code.bin's first byte

// The game's own data (both builds: the same addresses).
constexpr uint32_t kVsyncManager = 0xD63824; ///< pointer
constexpr uint32_t kManagerRate = 0x50, kManagerInterval = 0x54;
constexpr uint32_t kFrameCounter = 0xD635A0; ///< pointer
constexpr uint32_t kUpdateLength = 0x20;     ///< f32, 1/60 s: 2 at 30 pictures a second, 1 at 60

struct Word {
    uint32_t address;
    uint32_t game;
    uint32_t ours; ///< equal to `game`: context, only checked
};

// The five and their context.
constexpr uint32_t kRate30 = 0xE3A0001E, kRate60 = 0xE3A0003C;       // mov r0, #30 | #60
constexpr uint32_t kInterval2 = 0xE3A00002, kInterval1 = 0xE3A00001; // mov r0, #2 | #1
constexpr uint32_t kParity = 0xE2022001, kNoParity = 0xE3A02000; // and r2, r2, #1 | mov r2, #0
constexpr uint32_t kHalf = 0xE08160A0, kWhole = 0xE0816000; // add r6, r1, r0, lsr #1 | add r6, r1, r0
constexpr uint32_t kTwo = 0x40000000, kOne = 0x3F800000;    // 2.0f | 1.0f
constexpr uint32_t kStoreRate = 0xE5810050;     // str r0, [r1, #0x50]
constexpr uint32_t kStoreInterval = 0xE5810054; // str r0, [r1, #0x54]
constexpr uint32_t kLoadLength = 0xED9F0A06;    // vldr s0, [pc, #24]: the length's only reader

// The touch's words: {from 0x7DAD90, the game's, ours}. r4: the touch, s0 and s1: this update's
// speed, s19 and s18: the speed in use.
constexpr Word kTouch[] = {
    {0x00, 0xE5940030, 0xE2841020}, // add r1, r4, #32
    {0x04, 0xE3500002, 0xEC911A02}, // vldmia r1, {s2, s3}     the last update's
    {0x08, 0xAA00000F, 0xEC810A02}, // vstmia r1, {s0, s1}
    {0x0C, 0xE0840180, 0xEE300A01}, // vadd s0, s0, s2
    {0x10, 0xE2800020, 0xEE700AA1}, // vadd s1, s1, s3
    {0x14, 0xEC800A02, 0xED9F1AA6}, // vldr s2, [pc, #664]     0.5 (+0x2B4)
    {0x18, 0xE5940030, 0xEE200A01}, // vmul s0, s0, s2
    {0x1C, 0xE2800001, 0xEE600A81}, // vmul s1, s1, s2
    {0x20, 0xE5840030, 0xE5940030}, // ldr r0, [r4, #0x30]
    {0x24, 0xEA00000F, 0xEA000008}, // b +0x4C
    {0x4C, 0xE5942028, 0xEC532A10}, // vmov r2, r3, s0, s1
    {0x50, 0xE2840028, 0xE1822003}, // orr r2, r2, r3
    {0x54, 0xE5842020, 0xE1B02082}, // lsls r2, r2, #1         both zero?
    {0x58, 0xE594102C, 0x13A00000}, // movne r0, #0
    {0x5C, 0xE5841024, 0x02800001}, // addeq r0, r0, #1
    {0x60, 0xEC800A02, 0xE5840030}, // str r0, [r4, #0x30]
    {0x64, 0xE5940030, 0x1A000001}, // bne +0x70
    {0x68, 0xE2400001, 0xE3500003}, // cmp r0, #3
    {0x6C, 0xE3700001, 0xBA000001}, // blt +0x78
    {0x70, 0xDA00000F, 0xED840A0A}, // vstr s0, [r4, #40]
    {0x74, 0xE0841180, 0xEDC40A0B}, // vstr s1, [r4, #44]
    {0x78, 0xED910A08, 0xEDD49A0A}, // vldr s19, [r4, #40]
    {0x7C, 0xEEB40A6B, 0xED949A0B}, // vldr s18, [r4, #44]
    {0x80, 0xEEF1FA10, 0xEA00000B}, // b +0xB4
    {0x510, 0x41F00000, 0x42700000}, // the easing: 30.0f -> 60.0f
    // Context: the way in, the way out, 0.5, the easing's only reader.
    {-0x10u, 0x0A00000C, 0x0A00000C},
    {0xB4, 0xEE290AA9, 0xEE290AA9},
    {0x2B4, 0x3F000000, 0x3F000000},
    {0x3C0, 0xED9F8A52, 0xED9F8A52},
};
constexpr std::size_t kTouchWords = sizeof(kTouch) / sizeof(kTouch[0]);
constexpr std::size_t kChanged = 5 + 25; ///< words that differ

struct Build {
    uint64_t title;
    const char* name;
    std::array<Word, 13 + kTouchWords> words;
};

struct Places {
    uint32_t setup;   ///< the rate's mov
    uint32_t wait;    ///< the parity's and
    uint32_t pacer;   ///< the length's literal
    uint32_t touch;   ///< kTouch's first
    uint32_t sampler; ///< vadd s1, s5, s6: the touch's time, 1/60 s a sample
    uint32_t reticle; ///< the reticle's update: where the C-Stick mods have their hooks
    uint32_t hook_a, hook_b; ///< what is there (branches: each build its own)
};

constexpr Build MakeBuild(uint64_t title, const char* name, const Places& p) {
    Build b{title,
            name,
            {{{p.setup, kRate30, kRate60},
              {p.setup + 4, kStoreRate, kStoreRate},
              {p.setup + 8, kInterval2, kInterval1},
              {p.setup + 12, kStoreInterval, kStoreInterval},
              {p.wait, kParity, kNoParity},
              {p.pacer + 0x5C, kHalf, kWhole},
              {p.pacer - 0x20, kLoadLength, kLoadLength},
              {p.pacer, kTwo, kOne},
              {p.sampler, 0xEE720A83, 0xEE720A83},
              {p.sampler + 0x9C, 0x3C888889, 0x3C888889}, // 1/60
              {p.reticle, p.hook_a, p.hook_a},
              {p.reticle + 0x670, p.hook_b, p.hook_b},
              {p.reticle + 0xDE0, 0xE594008C, 0xE594008C}}}};
    for (std::size_t i = 0; i < kTouchWords; ++i) {
        b.words[13 + i] = {p.touch + kTouch[i].address, kTouch[i].game, kTouch[i].ours};
    }
    return b;
}

constexpr std::array<Build, 2> kBuilds{{
    MakeBuild(0x0004000000030200ull, "Europe",
              {0x48A87C, 0x9105B4, 0x828FC8, 0x7DAD90, 0x4DBAD0, 0x6D219C, 0xEB05512B, 0xEB10FF21}),
    MakeBuild(0x0004000000030100ull, "USA Rev 1",
              {0x48A7E4, 0x91041C, 0x828E30, 0x7DABF8, 0x4DBA00, 0x6D2050, 0xEB055118, 0xEB10FED4}),
}};

constexpr const Build* BuildFor(uint64_t title) {
    for (const Build& b : kBuilds) {
        if (b.title == title) {
            return &b;
        }
    }
    return nullptr;
}

/// What the code is.
enum class Code {
    Game,    ///< every word the game's
    Ours,    ///< every word that changes ours
    Differs, ///< a word is neither, or some of each (another build, a mod)
};

struct Look {
    Code code = Code::Differs;
    uint32_t address = 0; ///< with Differs: the first such place, and what it holds
    uint32_t found = 0;
};

/// `read(address)` -> std::optional<uint32_t>.
template <typename Read>
Look LookAt(const Build& build, Read&& read) {
    Look out;
    std::size_t ours = 0, game = 0;
    for (const Word& w : build.words) {
        const std::optional<uint32_t> v = read(w.address);
        const bool changes = w.game != w.ours;
        if (!v || (*v != w.game && *v != w.ours) ||
            (changes && (*v == w.ours ? game != 0 : ours != 0))) {
            out.address = w.address;
            out.found = v.value_or(0);
            return out;
        }
        if (changes) {
            ++(*v == w.ours ? ours : game);
        }
    }
    out.code = ours != 0 ? Code::Ours : Code::Game;
    return out;
}

enum class Outcome {
    Made,      ///< every word is the game's: the patch is in `ips`
    NotMapped, ///< another title
    Differs,   ///< a word is neither the game's nor ours (another build, a mod): nothing made
    Already,   ///< the words are ours already (a mod did it): nothing to do
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

/// The patch itself: every changed word of the table and nothing else (also the mod's code.ips,
/// made by the test [kiu60mod]).
inline std::vector<uint8_t> MakeIps(const Build& build) {
    // "PATCH", then {offset: 3 bytes, size: 2 bytes, data}, big endian, then "EOF".
    std::vector<uint8_t> ips{'P', 'A', 'T', 'C', 'H'};
    for (const Word& w : build.words) {
        if (w.game == w.ours) {
            continue;
        }
        const uint32_t offset = w.address - kCodeBase;
        for (const uint32_t b : {offset >> 16, offset >> 8, offset, 0u, 4u, w.ours, w.ours >> 8,
                                 w.ours >> 16, w.ours >> 24}) {
            ips.push_back(static_cast<uint8_t>(b));
        }
    }
    ips.insert(ips.end(), {'E', 'O', 'F'});
    return ips;
}

/// The patch for this game's code (code.bin as loaded, mods applied), all words or none.
inline Patch Make(uint64_t title, std::span<const uint8_t> code) {
    Patch out;
    out.build = BuildFor(title);
    if (!out.build) {
        return out;
    }
    const Look look = LookAt(*out.build, [&](uint32_t a) { return ReadWord(code, a); });
    if (look.code != Code::Game) {
        out.result = look.code == Code::Ours ? Outcome::Already : Outcome::Differs;
        out.address = look.address;
        out.found = look.found;
        return out;
    }
    out.ips = MakeIps(*out.build);
    out.result = Outcome::Made;
    return out;
}

/// Whether the game gets the patch: the test switch when it is "1" or "0", else the setting.
constexpr bool Wanted(std::optional<std::string_view> value, bool setting) {
    if (value && (*value == "1" || *value == "0")) {
        return *value == "1";
    }
    return setting;
}

/// What the running game was started at, after the loader's patch (the loader writes it, the
/// VBlank hook reads it): a save state made at the other rate is brought to it.
enum class Run {
    Leave, ///< the code is neither (a mod's, another version): never touched
    At30,
    At60, ///< ours, or a mod's that is the same
};
inline std::atomic<Run> g_run{Run::Leave};

/// Where the loader's code stands for the rest of the run: `code` as it looked before the patch,
/// `patched` true when it was put on.
constexpr Run RunFor(Code code, bool patched) {
    return code == Code::Differs ? Run::Leave
                                 : (patched || code == Code::Ours ? Run::At60 : Run::At30);
}

/// An update's share of one at 30, from the frame counter's length (its bits): 0.5 at 60.
constexpr float UpdateShareFor(uint32_t length_bits) {
    return length_bits == kOne ? 0.5f : 1.0f;
}

/// The game's memory made what `sixty` asks for (a save state brings the code and the rate it was
/// made with): `read(address)` -> std::optional<uint32_t>, `write_code` and `write_data`
/// (address, value). At a VBlank.
enum class Turned {
    No,      ///< nothing to do, or not yet
    Yes,     ///< the words and the manager's rate were written
    Differs, ///< the code is neither: left alone
};
template <typename Read, typename WriteCode, typename WriteData>
Turned TurnTo(const Build& build, bool sixty, Read&& read, WriteCode&& write_code,
              WriteData&& write_data) {
    // One read a VBlank: the update's length says which the code is.
    const Word& length = build.words[7];
    const std::optional<uint32_t> now = read(length.address);
    if (!now || *now == (sixty ? length.ours : length.game)) {
        return Turned::No;
    }
    const Look look = LookAt(build, read);
    if (look.code == Code::Differs) {
        return Turned::Differs;
    }
    const std::optional<uint32_t> manager = read(kVsyncManager);
    if (!manager) {
        return Turned::No;
    }
    const std::optional<uint32_t> rate = read(*manager + kManagerRate);
    const std::optional<uint32_t> interval = read(*manager + kManagerInterval);
    if (!rate || !interval || *rate != (sixty ? 30u : 60u) || *interval != (sixty ? 2u : 1u)) {
        return Turned::No; // not set up yet
    }
    for (const Word& w : build.words) {
        if (w.game != w.ours) {
            write_code(w.address, sixty ? w.ours : w.game);
        }
    }
    write_data(*manager + kManagerRate, sixty ? 60u : 30u);
    write_data(*manager + kManagerInterval, sixty ? 1u : 2u);
    return Turned::Yes;
}

/// AzaharFrame kiuhs3a: the toast that the setting, changed in a run, counts from the next start:
/// once a change, wherever it is noticed first (the menu, at once; else the next VBlank).
struct RestartToast {
    enum class Say {
        Nothing,
        For60,
        For30,
        TakeBack, ///< back at what the run started with: the one showing goes
    };
    bool said = false;
    /// `started`, `wanted`: 60 as the run started, and now.
    constexpr Say Next(bool started, bool wanted) {
        const bool other = started != wanted;
        if (other == said) {
            return Say::Nothing;
        }
        said = other;
        return !other ? Say::TakeBack : (wanted ? Say::For60 : Say::For30);
    }
};

} // namespace FileSys::Kiu60
