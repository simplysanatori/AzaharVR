// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csetjmp>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "common/alt_core_types.h"
#include "common/layer_depth.h"
#include "common/xr_pacer.h"
#include "common/zstd_compression.h"
#include "core/autosave.h"
#include "core/savestate.h"
#include "frame_cores/alt_core_host.h"
#include "frame_cores/audio_resampler.h"
#include "frame_cores/cheat_store.h"
#include "frame_cores/driver_runner.h"
#include "frame_cores/frame_driver.h"
#include "frame_cores/gb_core.h"
#include "frame_cores/gba_core.h"
#include "frame_cores/nds_core.h"
#include "frame_cores/pokedaisy_driver.h"
#include "frame_cores/rom_info.h"

namespace {

namespace fs = std::filesystem;

struct TempDir {
    fs::path path;
    explicit TempDir(const char* name) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() /
               (std::string("azaharvr_alt_test_") + name + "_" + std::to_string(stamp));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void WriteFile(const fs::path& p, const std::vector<uint8_t>& data) {
    std::ofstream f(p, std::ios::binary);
    REQUIRE(f.good());
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    REQUIRE(f.good());
}

void PutLE16(std::vector<uint8_t>& buf, size_t off, uint16_t v) {
    buf[off + 0] = static_cast<uint8_t>(v & 0xFF);
    buf[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void PutLE32(std::vector<uint8_t>& buf, size_t off, uint32_t v) {
    buf[off + 0] = static_cast<uint8_t>(v & 0xFF);
    buf[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    buf[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    buf[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

/// Minimal valid homebrew GBA ROM: sets Mode 3 (bitmap) and fills VRAM with a solid colour.
std::vector<uint8_t> MakeHomebrewGbaRom() {
    std::vector<uint8_t> rom(0x4000, 0);
    // Entry at 0x00: branch to 0xC0 -> offset = (0xC0 - 0x08) / 4 = 0x2E
    PutLE32(rom, 0x00, 0xEA00002Eu);
    // GBA fixed value at 0xB2 = 0x96
    rom[0xB2] = 0x96;
    const char title[12] = {'H', 'B', 'G', 'B', 'A', 'T', 'E', 'S', 'T', ' ', ' ', ' '};
    std::memcpy(&rom[0xA0], title, 12);
    const char code[4] = {'H', 'B', 'T', 'E'};
    std::memcpy(&rom[0xAC], code, 4);
    rom[0xB0] = '0';
    rom[0xB1] = '1';
    // Header complement checksum over 0xA0..0xBC
    uint8_t chk = 0;
    for (size_t i = 0xA0; i <= 0xBC; ++i) {
        chk = static_cast<uint8_t>(chk - rom[i]);
    }
    rom[0xBD] = static_cast<uint8_t>(chk - 0x19);

    // ARM code at 0xC0:
    //   mov r0, #0x04000000     ; REG_DISPCNT
    //   mov r1, #0x0400         ; BG2 enable
    //   orr r1, r1, #3          ; Mode 3
    //   strh r1, [r0]
    //   mov r2, #0x06000000     ; VRAM
    //   mov r3, #0x001F         ; bright red (BGR555)
    //   mov r4, #0x9600         ; 240*160 = 38400 pixels
    // loop:
    //   strh r3, [r2], #2
    //   subs r4, r4, #1
    //   bne loop
    // halt:
    //   b halt
    size_t pc = 0xC0;
    PutLE32(rom, pc, 0xE3A00301u); pc += 4; // mov r0, #0x04000000
    PutLE32(rom, pc, 0xE3A01B01u); pc += 4; // mov r1, #0x0400
    PutLE32(rom, pc, 0xE3811003u); pc += 4; // orr r1, r1, #3
    PutLE32(rom, pc, 0xE1C010B0u); pc += 4; // strh r1, [r0]
    PutLE32(rom, pc, 0xE3A02406u); pc += 4; // mov r2, #0x06000000
    PutLE32(rom, pc, 0xE3A0301Fu); pc += 4; // mov r3, #0x001F
    PutLE32(rom, pc, 0xE3A04C96u); pc += 4; // mov r4, #0x9600
    PutLE32(rom, pc, 0xE0C230B2u); pc += 4; // strh r3, [r2], #2
    PutLE32(rom, pc, 0xE2544001u); pc += 4; // subs r4, r4, #1
    PutLE32(rom, pc, 0x1AFFFFFCu); pc += 4; // bne -16 (back to strh)
    PutLE32(rom, pc, 0xEAFFFFFEu); pc += 4; // b .
    return rom;
}

/// Minimal valid homebrew GB ROM (32 KiB) with valid Nintendo logo + checksum.
std::vector<uint8_t> MakeHomebrewGbRom() {
    std::vector<uint8_t> rom(0x8000, 0);
    // Entry at 0x100: nop; jp 0x150
    rom[0x100] = 0x00;
    rom[0x101] = 0xC3;
    rom[0x102] = 0x50;
    rom[0x103] = 0x01;
    // Standard GB logo bytes at 0x104..0x133
    static const uint8_t kLogo[48] = {
        0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83,
        0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
        0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99, 0xBB, 0xBB, 0x67, 0x63,
        0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
    };
    std::memcpy(&rom[0x104], kLogo, sizeof(kLogo));
    const char title[8] = {'H', 'B', 'G', 'B', 'T', 'E', 'S', 'T'};
    std::memcpy(&rom[0x134], title, sizeof(title));
    // Header checksum over 0x134..0x14C
    uint8_t chk = 0;
    for (size_t i = 0x134; i <= 0x14C; ++i) {
        chk = static_cast<uint8_t>(chk - rom[i] - 1);
    }
    rom[0x14D] = chk;
    // Code at 0x150: ld a, 0xE4; ldh (0x47), a; ld a, 0x91; ldh (0x40), a; jr -2
    rom[0x150] = 0x3E; rom[0x151] = 0xE4;
    rom[0x152] = 0xE0; rom[0x153] = 0x47;
    rom[0x154] = 0x3E; rom[0x155] = 0x91;
    rom[0x156] = 0xE0; rom[0x157] = 0x40;
    rom[0x158] = 0x18; rom[0x159] = 0xFE;
    return rom;
}

/// A Game Boy ROM with 8 KiB of battery-backed save data: A writes 0x11 to its first byte, B 0x22.
/// The byte is also the background palette, so the picture shows which one the game sees.
std::vector<uint8_t> MakeBatteryGbRom() {
    std::vector<uint8_t> rom = MakeHomebrewGbRom();
    rom[0x147] = 0x03; // MBC1 + RAM + battery
    rom[0x149] = 0x02; // 8 KiB
    uint8_t chk = 0;
    for (size_t i = 0x134; i <= 0x14C; ++i) {
        chk = static_cast<uint8_t>(chk - rom[i] - 1);
    }
    rom[0x14D] = chk;
    static const uint8_t kCode[] = {
        0x3E, 0x0A, 0xEA, 0x00, 0x00, // ld a, 0x0A; ld (0x0000), a     save data on
        0x3E, 0x91, 0xE0, 0x40,       // ld a, 0x91; ldh (0x40), a      screen on
        // loop (0x159):
        0x3E, 0x10, 0xE0, 0x00,       // ld a, 0x10; ldh (0x00), a      the buttons, not the D-pad
        0xF0, 0x00, 0xF0, 0x00,       // ldh a, (0x00), twice
        0xCB, 0x47, 0x20, 0x05,       // bit 0, a; jr nz, +5            A
        0x3E, 0x11, 0xEA, 0x00, 0xA0, // ld a, 0x11; ld (0xA000), a
        0xF0, 0x00,                   // ldh a, (0x00)
        0xCB, 0x4F, 0x20, 0x05,       // bit 1, a; jr nz, +5            B
        0x3E, 0x22, 0xEA, 0x00, 0xA0, // ld a, 0x22; ld (0xA000), a
        0xFA, 0x00, 0xA0, 0xE0, 0x47, // ld a, (0xA000); ldh (0x47), a  the palette
        0xC3, 0x59, 0x01,             // jp loop
    };
    std::memcpy(&rom[0x150], kCode, sizeof(kCode));
    return rom;
}

/// A GBA ROM with two text backgrounds of `priority0` and `priority1`, drawn `right0` and
/// `right1` pixels to the right of where they scroll. They scroll every frame and change in the
/// middle of it; sound channel 1 plays.
std::vector<uint8_t> MakeLayersGbaRom(uint32_t priority0, uint32_t priority1, int right0 = 0,
                                      int right1 = 0) {
    const auto sub = [](uint32_t reg, int value) { // sub r1, reg, #value
        return (value < 0 ? 0xE2801000u : 0xE2401000u) | reg << 16 |
               static_cast<uint32_t>(std::abs(value));
    };
    std::vector<uint8_t> rom = MakeHomebrewGbaRom();
    const uint32_t code[] = {
        0xE3A00301u,          // mov r0, #0x04000000
        0xE3A01080u,          // mov r1, #0x80
        0xE1C010B0u,          // strh r1, [r0]            forced blank
        0xE1C018B4u,          // strh r1, [r0, #0x84]     sound on
        0xE59F30F4u,          // ldr r3, =0x00021177
        0xE5803080u,          // str r3, [r0, #0x80]
        0xE59F30F0u,          // ldr r3, =0xF0800000
        0xE5803060u,          // str r3, [r0, #0x60]
        0xE3A02405u,          // mov r2, #0x05000000
        0xE59F30CCu,          // ldr r3, =0x03FF0000
        0xE5823000u,          // str r3, [r2]             colours 0 and 1
        0xE59F30C8u,          // ldr r3, =0x7C1F001F
        0xE5823004u,          // str r3, [r2, #4]         2 and 3
        0xE3A02406u,          // mov r2, #0x06000000
        0xE59F30C0u,          // ldr r3, =0x1234567B
        0xE59F60C0u,          // ldr r6, =0x33333333
        0xE3A04C02u,          // mov r4, #0x200
        0xE08333E3u,          // add r3, r3, r3, ror #7   16 tiles of noise
        0xE0035006u,          // and r5, r3, r6
        0xE4825004u,          // str r5, [r2], #4
        0xE2544004u,          // subs r4, r4, #4
        0x1AFFFFFAu,          // bne -4
        0xE3A02406u,          // mov r2, #0x06000000
        0xE2822901u,          // add r2, r2, #0x4000
        0xE3A04000u,          // mov r4, #0
        0xE0845104u,          // add r5, r4, r4, lsl #2   the maps, 0x4000 and 0x4800
        0xE0855324u,          // add r5, r5, r4, lsr #6
        0xE205500Fu,          // and r5, r5, #15
        0xE1C250B0u,          // strh r5, [r2]
        0xE2826B02u,          // add r6, r2, #0x800
        0xE02550A4u,          // eor r5, r5, r4, lsr #1
        0xE205500Fu,          // and r5, r5, #15
        0xE1C650B0u,          // strh r5, [r6]
        0xE2822002u,          // add r2, r2, #2
        0xE2844001u,          // add r4, r4, #1
        0xE3540B01u,          // cmp r4, #0x400
        0x3AFFFFF3u,          // blo -11
        0xE59F306Cu,          // ldr r3, =BG0CNT, BG1CNT
        0xE5803008u,          // str r3, [r0, #8]
        0xE3A01C03u,          // mov r1, #0x300
        0xE1C010B0u,          // strh r1, [r0]            mode 0
        0xE3A08000u,          // mov r8, #0
        0xE1D010B6u,          // ldrh r1, [r0, #6]        until line 160
        0xE35100A0u,          // cmp r1, #160
        0x1AFFFFFCu,          // bne -2
        0xE2888001u,          // add r8, r8, #1
        0xE1A070A8u,          // lsr r7, r8, #1
        sub(8, right0),
        0xE1C011B0u,          // strh r1, [r0, #0x10]     BG0HOFS
        sub(7, right1),
        0xE1C011B4u,          // strh r1, [r0, #0x14]     BG1HOFS
        0xE1C071B2u,          // strh r7, [r0, #0x12]     BG0VOFS
        0xE20810FFu,          // and r1, r8, #0xFF
        0xE3811B21u,          // orr r1, r1, #0x8400
        0xE1C016B4u,          // strh r1, [r0, #0x64]     the note
        0xE1D010B6u,          // ldrh r1, [r0, #6]        until line 80
        0xE3510050u,          // cmp r1, #80
        0x1AFFFFFCu,          // bne -2
        0xE1C081B6u,          // strh r8, [r0, #0x16]     BG1VOFS
        0xE2877003u,          // add r7, r7, #3
        0xE1C071B2u,          // strh r7, [r0, #0x12]
        0xEAFFFFEBu,          // b line 160
        0x03FF0000u,
        0x7C1F001Fu,
        0x1234567Bu,
        0x33333333u,
        0x09000800u | priority1 << 16 | priority0,
        0x00021177u,
        0xF0800000u,
    };
    size_t pc = 0xC0;
    for (const uint32_t word : code) {
        PutLE32(rom, pc, word);
        pc += 4;
    }
    return rom;
}

/// A Game Boy ROM whose background scrolls and changes its colours every frame, drawn `right`
/// pixels to the right of where it scrolls; `window`: the window covers it. Sound channel 1 plays.
std::vector<uint8_t> MakeLayersGbRom(bool window, int right = 0) {
    std::vector<uint8_t> rom = MakeHomebrewGbRom();
    const uint8_t lcdc = window ? 0xF1 : 0x91;
    const uint8_t code[] = {
        0xAF, 0xE0, 0x40,                         // xor a; ldh (0x40), a       screen off
        0x21, 0x10, 0x80, 0x06, 0xF0, 0x3E, 0x5B, // ld hl, 0x8010; ld b, 0xF0; ld a, 0x5B
        0x22, 0x07, 0xC6, 0x1D, 0x05, 0x20, 0xF9, // the tiles: ld (hl+), a; rlca; add a, 0x1D ...
        0x21, 0x00, 0x98, 0x01, 0x00, 0x08,       // ld hl, 0x9800; ld bc, 0x0800
        0x7D, 0x84, 0xE6, 0x0F, 0x22,             // both maps: ld a, l; add a, h; and 15; ...
        0x0B, 0x78, 0xB1, 0x20, 0xF6,             // dec bc; ld a, b; or c; jr nz
        0x3E, 0x07, 0xE0, 0x4B, 0xAF, 0xE0, 0x4A, // the window at 0, 0
        0x3E, 0x80, 0xE0, 0x26, 0x3E, 0x77, 0xE0, 0x24, // sound on
        0x3E, 0xFF, 0xE0, 0x25, 0x3E, 0xF0, 0xE0, 0x12,
        0x3E, lcdc, 0xE0, 0x40, 0x16, 0x00, // screen on; ld d, 0
        // loop (0x18E):
        0xF0, 0x44, 0xFE, 0x90, 0x20, 0xFA,             // until line 144
        0x14, 0x7A, 0xE0, 0x47, 0xE0, 0x42, 0xE0, 0x13, // inc d: the palette, SCY, the note
        0xD6, static_cast<uint8_t>(right), 0xE0, 0x43, 0x3E, 0x87, 0xE0, 0x14, // SCX
        0xF0, 0x44, 0xFE, 0x90, 0x28, 0xFA,             // until the next line
        0xC3, 0x8E, 0x01,                               // jp loop
    };
    std::memcpy(&rom[0x150], code, sizeof(code));
    return rom;
}

using Pixels = std::vector<uint32_t>;

Pixels Copy(const Vulkan::AltScreenImage& image) {
    if (!image.HasPicture()) {
        return {};
    }
    const uint32_t stride = image.stride_pixels ? image.stride_pixels : image.width;
    Pixels pixels;
    for (uint32_t y = 0; y < image.height; ++y) {
        const uint32_t* row = image.pixels + static_cast<size_t>(y) * stride;
        pixels.insert(pixels.end(), row, row + image.width);
    }
    return pixels;
}

/// A GBA or Game Boy game without its host.
struct DepthGame {
    TempDir tmp;
    std::unique_ptr<FrameCores::CoreBackend> core;
    FrameCores::FrameOutput out;

    DepthGame(bool gba, const std::vector<uint8_t>& rom, uint32_t layer_depth,
              FrameCores::CoreBootOptions opts = {})
        : tmp("depth") {
        const fs::path path = tmp.path / (gba ? "hb.gba" : "hb.gb");
        WriteFile(path, rom);
        FrameCores::CorePaths paths;
        paths.data_dir = tmp.path.string();
        paths.game_dir = (tmp.path / "game").string();
        paths.cheat_dir = (tmp.path / "cheats").string();
        opts.use_bios = false;
        opts.second_screen = 2; // off
        opts.layer_depth = layer_depth;
        if (gba) {
            core = std::make_unique<FrameCores::GbaCore>();
        } else {
            core = std::make_unique<FrameCores::GbCore>();
        }
        REQUIRE(core->Load(path.string(), paths, opts));
    }

    void Run(uint32_t buttons = 0) {
        Common::AltCore::InputState in{};
        in.buttons = buttons;
        core->SetInput(in);
        core->RunFrame(out);
    }
    Pixels Left() const {
        return Copy(out.primary);
    }
    Pixels Right() const {
        return Copy(out.primary_right);
    }
};

/// The setting, for a test.
struct InvertDepth {
    explicit InvertDepth(bool on) {
        Set(on);
    }
    ~InvertDepth() {
        Set(false);
    }
    void Set(bool on) {
        Common::LayerDepth::Get().inverted = on;
    }
};

/// Pictures where nothing is to be nearer or farther.
std::vector<uint8_t> MakeFlatRom(bool gba) {
    return gba ? MakeLayersGbaRom(0, 0) : MakeLayersGbRom(true);
}
std::vector<uint8_t> MakeDeepRom(bool gba) {
    return gba ? MakeLayersGbaRom(1, 3) : MakeLayersGbRom(false);
}

uint8_t FirstByte(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return static_cast<uint8_t>(f.get());
}

uint16_t NdsCrc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001)
                            : static_cast<uint16_t>(crc >> 1);
        }
    }
    return crc;
}

/// Minimal valid homebrew NDS ROM with ARM9 + ARM7 code, banner icon, and valid header CRC16.
std::vector<uint8_t> MakeHomebrewNdsRom(bool dsi_flag = false) {
    std::vector<uint8_t> rom(0x8000, 0);
    const char title[12] = {'H', 'B', 'N', 'D', 'S', 'T', 'E', 'S', 'T', 0, 0, 0};
    std::memcpy(&rom[0x00], title, 12);
    const char code[4] = {'H', 'N', 'D', 'E'};
    std::memcpy(&rom[0x0C], code, 4);
    rom[0x10] = '0';
    rom[0x11] = '1';
    rom[0x12] = dsi_flag ? 0x02 : 0x00; // unitcode

    // ARM9 binary at ROM 0x4000 -> RAM 0x02000000, size 0x100
    PutLE32(rom, 0x20, 0x4000);
    PutLE32(rom, 0x24, 0x02000000);
    PutLE32(rom, 0x28, 0x02000000);
    PutLE32(rom, 0x2C, 0x100);

    // ARM7 binary at ROM 0x4200 -> RAM 0x02380000, size 0x100
    PutLE32(rom, 0x30, 0x4200);
    PutLE32(rom, 0x34, 0x02380000);
    PutLE32(rom, 0x38, 0x02380000);
    PutLE32(rom, 0x3C, 0x100);

    // Banner at 0x5000
    PutLE32(rom, 0x68, 0x5000);
    PutLE32(rom, 0x80, 0x8000); // total used ROM size
    PutLE32(rom, 0x84, 0x4000); // header size

    // Banner: version 1, simple non-zero tile + palette + English title
    PutLE16(rom, 0x5000, 0x0001);
    for (size_t i = 0; i < 0x200; ++i) {
        rom[0x5020 + i] = 0x11; // palette index 1 everywhere
    }
    PutLE16(rom, 0x5220 + 2, 0x03E0); // green in BGR555
    const char16_t banner_title[] = u"Homebrew DS Test";
    for (size_t i = 0; i < std::char_traits<char16_t>::length(banner_title); ++i) {
        PutLE16(rom, 0x5340 + i * 2, static_cast<uint16_t>(banner_title[i]));
    }
    PutLE16(rom, 0x5002, NdsCrc16(&rom[0x5020], 0x820));

    // ARM9 code at 0x4000: enable VRAM A as LCDC, set DISPCNT = LCDC VRAM A, fill red, loop
    size_t pc = 0x4000;
    PutLE32(rom, pc, 0xE3A00301u); pc += 4; // mov r0, #0x04000000
    PutLE32(rom, pc, 0xE3A01802u); pc += 4; // mov r1, #0x00020000 (Mode LCDC VRAM A)
    PutLE32(rom, pc, 0xE5801000u); pc += 4; // str r1, [r0]
    PutLE32(rom, pc, 0xE3A01080u); pc += 4; // mov r1, #0x80 (VRAMCNT_A = enable LCDC)
    PutLE32(rom, pc, 0xE5C01240u); pc += 4; // strb r1, [r0, #0x240]
    PutLE32(rom, pc, 0xE3A0231Au); pc += 4; // mov r2, #0x68000000? wait: 0x06800000 = 0x1A << 22
    // Better: mov r2, #0x06800000 = 0x68 << 20 = ror 12 (rotate=6): 0xE3A02668
    pc -= 4;
    PutLE32(rom, pc, 0xE3A02668u); pc += 4; // mov r2, #0x06800000 (VRAM_A)
    PutLE32(rom, pc, 0xE3A0301Fu); pc += 4; // mov r3, #0x001F (red)
    PutLE32(rom, pc, 0xE3A04903u); pc += 4; // mov r4, #0xC000 (49152 pixels)
    PutLE32(rom, pc, 0xE0C230B2u); pc += 4; // strh r3, [r2], #2
    PutLE32(rom, pc, 0xE2544001u); pc += 4; // subs r4, r4, #1
    PutLE32(rom, pc, 0x1AFFFFFCu); pc += 4; // bne -16
    PutLE32(rom, pc, 0xEAFFFFFEu); pc += 4; // b .

    // ARM7 code at 0x4200: b .
    PutLE32(rom, 0x4200, 0xEAFFFFFEu);

    // Header CRC16 over 0x000..0x15D
    PutLE16(rom, 0x15E, NdsCrc16(rom.data(), 0x15E));
    return rom;
}

/// A DS ROM whose ARM7 copies the lid switch to main RAM; the ARM9 fills the picture by it.
std::vector<uint8_t> MakeLidNdsRom() {
    std::vector<uint8_t> rom = MakeHomebrewNdsRom(false);
    // The ARM9 code past the secure area (0x4000 to 0x7FFF), which a direct boot would not run as is
    rom.resize(0x10000, 0);
    std::copy_n(rom.begin() + 0x4000, 5 * 4, rom.begin() + 0x8000);
    PutLE32(rom, 0x20, 0x8000);
    PutLE32(rom, 0x80, 0x10000);
    PutLE16(rom, 0x15E, NdsCrc16(rom.data(), 0x15E));
    const auto put = [&rom](size_t pc, const std::vector<uint32_t>& code) {
        for (const uint32_t word : code) {
            PutLE32(rom, pc, word);
            pc += 4;
        }
    };
    // ARM9, after the display setup (five instructions)
    put(0x8000 + 5 * 4, {
        0xE3A05623u, // mov r5, #0x02300000
        // loop:
        0xE5956000u, // ldr r6, [r5]
        0xE3160080u, // tst r6, #0x80   closed
        0x13A0301Fu, // movne r3, #0x1F
        0x03A03B1Fu, // moveq r3, #0x7C00
        0xE3A02668u, // mov r2, #0x06800000
        0xE3A04903u, // mov r4, #0xC000
        0xE0C230B2u, // strh r3, [r2], #2
        0xE2544001u, // subs r4, r4, #1
        0x1AFFFFFCu, // bne -2
        0xEAFFFFF5u, // b loop
    });
    put(0x4200, {
        0xE59F000Cu, // ldr r0, =0x04000136 (EXTKEYIN)
        0xE59F200Cu, // ldr r2, =0x02300000
        0xE1D010B0u, // ldrh r1, [r0]
        0xE5821000u, // str r1, [r2]
        0xEAFFFFFCu, // b -2
        0x04000136u,
        0x02300000u,
    });
    return rom;
}

/// Holds a button for a moment, then long enough for the save file to be written.
void Press(FrameCores::AltCoreHost& host, uint32_t button) {
    Common::AltCore::InputState in{};
    in.buttons = button;
    host.PushInput(in);
    REQUIRE(host.StepFrames(10));
    host.PushInput({});
    REQUIRE(host.StepFrames(120));
}

/// What the game shows a moment later.
uint32_t PixelSoon(FrameCores::AltCoreHost& host) {
    REQUIRE(host.StepFrames(10));
    REQUIRE(host.LastFrame().primary.pixels != nullptr);
    return host.LastFrame().primary.pixels[0];
}

/// A host whose loads are kept: `loads.back()` is the last one.
void KeepLoads(FrameCores::AltCoreHost& host, std::vector<FrameCores::LoadDone>& loads) {
    FrameCores::HostFrontend frontend;
    frontend.on_loaded = [&loads](const FrameCores::LoadDone& done) { loads.push_back(done); };
    host.SetFrontend(std::move(frontend));
}

uint64_t NowSeconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

std::vector<uint8_t> ReadFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>{});
}

/// What the Plain driver gives a game with one screen.
void CheckPlain(const FrameCores::AltCoreHost& host, uint32_t width, uint32_t height) {
    const auto& frame = host.LastFrame();
    REQUIRE(frame.primary.pixels != nullptr);
    CHECK(frame.primary.width == width);
    CHECK(frame.primary.height == height);
    CHECK(frame.primary.stride_pixels == width);
    CHECK(!frame.primary.bgra);
    CHECK(std::all_of(frame.primary.pixels, frame.primary.pixels + width * height,
                      [](uint32_t p) { return (p >> 24) == 0xFFu; }));
    CHECK(frame.secondary.pixels == nullptr);
    CHECK(!frame.audio_pcm.empty());
    CHECK(frame.audio_pcm.size() % 2 == 0);
    CHECK(frame.rumble == 0.0f);
    CHECK(!frame.metrics_changed);
    const auto caps = host.GetCapabilities();
    CHECK(!caps.has_touch);
    CHECK(!caps.screens.has_bottom);
    CHECK(caps.screens.touch_width == width);
    CHECK(caps.screens.touch_height == height);
}

/// A driver that runs the core's own frame and is unsafe for states for some frames.
struct Job {
    int frames = 0; ///< left until a state is safe
    int made = 0;
    int resets = 0;
    int loads = 0;
    int pages = 0;
    bool held = false;
    int paint = -1; ///< a pixel of the Primary Screen the driver paints over
    FrameCores::DriverPicture picture;
};

class JobDriver final : public FrameCores::FrameDriver {
public:
    JobDriver(const FrameCores::DriverServices& services_, Job& job_)
        : services(services_), job(job_) {
        ++job.made;
    }
    void RunFrame(const FrameCores::DriverInput& in, FrameCores::DriverFrame& out) override {
        out.held = job.held;
        if (!job.held) {
            services.step(in.keys);
        }
        job.picture = services.picture();
        pixels.resize(static_cast<size_t>(job.picture.width) * job.picture.height);
        for (uint32_t y = 0; y < job.picture.height; ++y) {
            for (uint32_t x = 0; x < job.picture.width; ++x) {
                pixels[y * job.picture.width + x] =
                    job.picture.pixels[y * job.picture.stride + x] | 0xFF000000u;
            }
        }
        if (job.paint >= 0) {
            pixels[static_cast<size_t>(job.paint)] ^= 0x00FFFFFFu;
        }
        out.primary = {pixels.data(), job.picture.width, job.picture.height, job.picture.width};
        if (job.frames > 0) {
            --job.frames;
        }
    }
    void Reset() override {
        ++job.resets;
    }
    bool StateSafeNow() const override {
        return job.frames == 0;
    }
    void AfterStateLoad() override {
        ++job.loads;
    }
    void TurnPage() override {
        ++job.pages;
    }
    std::pair<uint32_t, uint32_t> SecondarySize() const override {
        return {};
    }
    bool HasTouch() const override {
        return false;
    }

private:
    const FrameCores::DriverServices services;
    Job& job;
    std::vector<uint32_t> pixels;
};

FrameCores::CoreBootOptions WithJob(Job& job) {
    FrameCores::CoreBootOptions opts;
    opts.driver.make = [&job](const FrameCores::DriverServices& services) {
        return std::make_unique<JobDriver>(services, job);
    };
    return opts;
}

#ifndef _WIN32
thread_local sigjmp_buf g_test_jmp;
thread_local std::atomic<bool> g_test_jmp_armed{false};
std::atomic<int> g_prior_handler_hits{0};
struct sigaction g_saved_prev_segv{};

void TestPriorSegvHandler(int sig, siginfo_t* info, void* ctx) {
    if (g_test_jmp_armed.load(std::memory_order_acquire)) {
        g_prior_handler_hits.fetch_add(1, std::memory_order_relaxed);
        g_test_jmp_armed.store(false, std::memory_order_release);
        siglongjmp(g_test_jmp, 1);
    }
    if ((g_saved_prev_segv.sa_flags & SA_SIGINFO) && g_saved_prev_segv.sa_sigaction) {
        g_saved_prev_segv.sa_sigaction(sig, info, ctx);
    } else if (g_saved_prev_segv.sa_handler && g_saved_prev_segv.sa_handler != SIG_DFL &&
               g_saved_prev_segv.sa_handler != SIG_IGN) {
        g_saved_prev_segv.sa_handler(sig);
    } else {
        signal(sig, SIG_DFL);
        raise(sig);
    }
}
#endif

} // namespace

TEST_CASE("AltCore RomInfo detection and reserved 64-bit keys", "[frame_cores]") {
    TempDir tmp("rominfo");
    const fs::path gba_path = tmp.path / "test.gba";
    const fs::path gb_path = tmp.path / "test.gb";
    const fs::path nds_path = tmp.path / "test.nds";
    const fs::path dsi_path = tmp.path / "test.dsi";

    WriteFile(gba_path, MakeHomebrewGbaRom());
    WriteFile(gb_path, MakeHomebrewGbRom());
    WriteFile(nds_path, MakeHomebrewNdsRom(false));
    WriteFile(dsi_path, MakeHomebrewNdsRom(true));

    CHECK(FrameCores::DetectCoreKind(gba_path.string()) == Common::AltCore::CoreKind::GBA);
    CHECK(FrameCores::DetectCoreKind(gb_path.string()) == Common::AltCore::CoreKind::GB);
    CHECK(FrameCores::DetectCoreKind(nds_path.string()) == Common::AltCore::CoreKind::NDS);
    CHECK(FrameCores::DetectCoreKind(dsi_path.string()) == Common::AltCore::CoreKind::DSi);
    CHECK(FrameCores::DetectCoreKind("game.3ds") == Common::AltCore::CoreKind::N3DS);

    const auto gba = FrameCores::ProbeRom(gba_path.string());
    REQUIRE(gba.has_value());
    CHECK(Common::AltCore::IsAltKey(gba->title_id));
    CHECK((gba->title_id >> 56) == (Common::AltCore::kTagGBA >> 56));
    CHECK(!gba->icon_rgba.empty());

    const auto gb = FrameCores::ProbeRom(gb_path.string());
    REQUIRE(gb.has_value());
    CHECK(Common::AltCore::IsAltKey(gb->title_id));
    CHECK((gb->title_id >> 56) == (Common::AltCore::kTagGB >> 56));

    const auto nds = FrameCores::ProbeRom(nds_path.string());
    REQUIRE(nds.has_value());
    CHECK(Common::AltCore::IsAltKey(nds->title_id));
    CHECK(nds->title == "Homebrew DS Test");
    CHECK(nds->icon_w == 32);
    CHECK(nds->icon_h == 32);
    CHECK(nds->icon_rgba.size() == 32u * 32u * 4u);

    const auto dsi = FrameCores::ProbeRom(dsi_path.string());
    REQUIRE(dsi.has_value());
    CHECK(dsi->dsi_enhanced);
}

TEST_CASE("AltCoreHost: Turn Secondary Screen Page goes by its presses", "[frame_cores]") {
    using namespace Common::AltCore;
    TempDir tmp("pages");
    const fs::path rom_path = tmp.path / "hb.gba";
    WriteFile(rom_path, MakeHomebrewGbaRom());
    Job job;
    FrameCores::AltCoreHost host;
    host.TurnPage(); // no game
    REQUIRE(host.Boot(rom_path.string(), (tmp.path / "data").string(),
                      /*headless_for_test=*/true, WithJob(job)));
    InputState in{};
    in.buttons = BtnTurnPage;
    host.PushInput(in);
    REQUIRE(host.StepFrames(3)); // held down: once
    CHECK(job.pages == 1);
    in.buttons = 0;
    host.PushInput(in);
    REQUIRE(host.StepFrames(1));
    in.buttons = BtnTurnPage | BtnA;
    host.PushInput(in);
    REQUIRE(host.StepFrames(2));
    CHECK(job.pages == 2);
    host.TurnPage();
    CHECK(job.pages == 3);
}

TEST_CASE("AltCoreHost: a held game is as a paused one", "[frame_cores]") {
    TempDir tmp("held");
    const fs::path rom_path = tmp.path / "hb.gba";
    WriteFile(rom_path, MakeHomebrewGbaRom());
    Job job;
    FrameCores::AltCoreHost host;
    REQUIRE(host.Boot(rom_path.string(), (tmp.path / "data").string(),
                      /*headless_for_test=*/true, WithJob(job)));
    REQUIRE(host.StepFrames(2));
    CHECK_FALSE(host.IsHeld());
    job.held = true;
    REQUIRE(host.StepFrames(1));
    CHECK(host.IsHeld());
    CHECK(host.LastFrame().held);
    CHECK(host.LastFrame().audio_pcm.empty());
    job.held = false;
    REQUIRE(host.StepFrames(1));
    CHECK_FALSE(host.IsHeld());
}

TEST_CASE("GBA sound: the rate is the game's own, as it changes it", "[frame_cores]") {
    TempDir tmp("gbarate");
    const fs::path rom_path = tmp.path / "hb.gba";
    std::vector<uint8_t> rom = MakeHomebrewGbaRom();
    PutLE32(rom, 0xE8, 0xE3A01C42u); // mov r1, #0x4200: SOUNDBIAS, 65536 Hz
    PutLE32(rom, 0xEC, 0xE1C018B8u); // strh r1, [r0, #0x88]
    PutLE32(rom, 0xF0, 0xEAFFFFFEu); // b .
    WriteFile(rom_path, rom);

    FrameCores::GbaCore core;
    CHECK(core.AudioSampleRate() == 32768);
    FrameCores::CorePaths paths;
    paths.game_dir = (tmp.path / "data").string();
    fs::create_directories(paths.game_dir);
    REQUIRE(core.Load(rom_path.string(), paths, {}));
    FrameCores::FrameOutput out;
    for (int i = 0; i < 10; ++i) {
        core.RunFrame(out);
    }
    CHECK(core.AudioSampleRate() == 65536);
    const std::size_t frames = out.audio_pcm.size() / 2;
    CHECK(frames > 1000); // 65536 / 59.73
    CHECK(frames < 1200);
    core.Unload();
}

#ifdef HAVE_POKEDAISY
TEST_CASE("PokeDaisy's ROM stays where it is when the game writes to its cartridge",
          "[frame_cores]") {
    using FrameCores::PokeDaisy::RomOf;
    using FrameCores::PokeDaisy::SettleRom;
    TempDir tmp("pdrom");
    FrameCores::CorePaths paths;
    paths.game_dir = (tmp.path / "data").string();
    fs::create_directories(paths.game_dir);

    // As FireRed: a 16 MiB game that sets up its debug print
    std::vector<uint8_t> rom = MakeHomebrewGbaRom();
    PutLE32(rom, 0xE8, 0xE59F0014u); // ldr r0, =0x08FE2FFE, the print area's lock
    PutLE32(rom, 0xEC, 0xE3A01020u); // mov r1, #0x20
    PutLE32(rom, 0xF0, 0xE1C010B0u); // strh r1, [r0]
    PutLE32(rom, 0xF4, 0xE59F000Cu); // ldr r0, =0x08FE20FA, its bank
    PutLE32(rom, 0xF8, 0xE3A010FDu); // mov r1, #0xFD
    PutLE32(rom, 0xFC, 0xE1C010B0u); // strh r1, [r0]
    PutLE32(rom, 0x100, 0xEAFFFFFEu); // b .
    PutLE32(rom, 0x104, 0x08FE2FFEu);
    PutLE32(rom, 0x108, 0x08FE20FAu);
    rom.resize(0x1000000);
    const fs::path rom_path = tmp.path / "hb.gba";
    WriteFile(rom_path, rom);
    for (const bool settled : {false, true}) {
        CAPTURE(settled);
        mCore* mgba = nullptr;
        FrameCores::CoreBootOptions opts;
        opts.driver.make = [&](const FrameCores::DriverServices& services) {
            mgba = services.core;
            if (settled) {
                SettleRom(mgba);
            }
            return std::unique_ptr<FrameCores::FrameDriver>{};
        };
        FrameCores::GbaCore core;
        REQUIRE(core.Load(rom_path.string(), paths, opts));
        REQUIRE(mgba);
        const auto before = RomOf(mgba);
        REQUIRE(before.first);
        REQUIRE(before.second == rom.size());
        CHECK(std::memcmp(before.first, rom.data(), rom.size()) == 0);
        FrameCores::FrameOutput out;
        for (int i = 0; i < 10; ++i) {
            core.RunFrame(out);
        }
        CHECK((RomOf(mgba) == before) == settled);
        core.Reset();
        for (int i = 0; i < 10; ++i) {
            core.RunFrame(out);
        }
        CHECK(RomOf(mgba).second == rom.size());
        if (settled) {
            CHECK(RomOf(mgba) == before);
        }
        core.Unload();
    }

    const std::vector<uint8_t> gb = MakeHomebrewGbRom();
    const fs::path gb_path = tmp.path / "hb.gb";
    WriteFile(gb_path, gb);
    FrameCores::CoreBootOptions opts;
    const uint8_t* first = nullptr;
    mCore* mgba = nullptr;
    opts.driver.make = [&](const FrameCores::DriverServices& services) {
        mgba = services.core;
        first = RomOf(mgba).first;
        SettleRom(mgba);
        return std::unique_ptr<FrameCores::FrameDriver>{};
    };
    FrameCores::GbCore core;
    REQUIRE(core.Load(gb_path.string(), paths, opts));
    const auto now = RomOf(mgba);
    CHECK(now.first != first); // the copy a cheat would have made
    REQUIRE(now.second == gb.size());
    CHECK(std::memcmp(now.first, gb.data(), gb.size()) == 0);
    SettleRom(mgba);
    CHECK(RomOf(mgba) == now);
    core.Unload();
}
#endif

TEST_CASE("AudioResampler: a new rate is read as that rate", "[frame_cores]") {
    FrameCores::AudioResampler resampler;
    REQUIRE(resampler.Init(32768, /*use_null_sink=*/true));
    const std::vector<int16_t> sound(1024 * 2, 1000);
    resampler.Push(sound.data(), 1024);
    CHECK(std::abs(resampler.BufferedMs() - 31.25) < 0.01);
    resampler.SetCoreSampleRate(65536);
    CHECK(std::abs(resampler.BufferedMs() - 15.625) < 0.01);
    resampler.SetCoreSampleRate(0); // not a rate
    CHECK(std::abs(resampler.BufferedMs() - 15.625) < 0.01);
    resampler.Shutdown();
}

TEST_CASE("GBA adapter boots homebrew ROM, renders, saves/loads state",
          "[frame_cores]") {
    TempDir tmp("gba");
    const fs::path rom_path = tmp.path / "hb.gba";
    WriteFile(rom_path, MakeHomebrewGbaRom());

    FrameCores::AltCoreHost host;
    REQUIRE(host.Boot(rom_path.string(), (tmp.path / "data").string(), /*headless_for_test=*/true));
    CHECK(host.IsRunning());
    CHECK(host.GetCapabilities().kind == Common::AltCore::CoreKind::GBA);
    CHECK(Common::XrPacer::Get().emu_frame_us.load() > 16730.0);

    Common::AltCore::InputState in{};
    in.buttons = Common::AltCore::BtnA;
    host.PushInput(in);
    REQUIRE(host.StepFrames(20));

    CheckPlain(host, 240, 160);
    const auto& frame = host.LastFrame();

    // Check that our Mode 3 red fill rendered non-zero pixels
    bool any_red = false;
    for (uint32_t i = 0; i < 240u * 160u; ++i) {
        if ((frame.primary.pixels[i] & 0x000000FFu) > 0x80u) {
            any_red = true;
            break;
        }
    }
    CHECK(any_red);

    // Solar level + save state round-trip + slot list
    host.SetSolarLevel(5);
    REQUIRE(host.QuickSave());
    REQUIRE(host.SaveSlot(1));
    CHECK(Core::ListSaveStatesIn(host.StatesDir(), host.GetRomInfo().title_id, 0).size() == 2);
    REQUIRE(host.StepFrames(5));
    REQUIRE(host.LoadSlot(1));
    REQUIRE(host.QuickLoad());

    host.Stop();
    CHECK(!host.IsRunning());
    // XrPacer frame period restored to 3DS default on Stop()
    CHECK(Common::XrPacer::Get().emu_frame_us.load() == Common::XrPacer::kEmuFrameUs);
}

TEST_CASE("GB adapter boots homebrew ROM, renders 160x144, and saves/loads state",
          "[frame_cores]") {
    TempDir tmp("gb");
    const fs::path rom_path = tmp.path / "hb.gb";
    WriteFile(rom_path, MakeHomebrewGbRom());

    FrameCores::AltCoreHost host;
    REQUIRE(host.Boot(rom_path.string(), (tmp.path / "data").string(), /*headless_for_test=*/true));
    CHECK(host.GetCapabilities().kind == Common::AltCore::CoreKind::GB);
    REQUIRE(host.StepFrames(10));
    CheckPlain(host, 160, 144);

    REQUIRE(host.SaveSlot(1));
    REQUIRE(host.StepFrames(5));
    REQUIRE(host.LoadSlot(1));

    host.Stop();
    CHECK(!host.IsRunning());
}

TEST_CASE("Each game has its own folder, whatever its file is called", "[frame_cores]") {
    TempDir tmp("gamefolder");
    const std::string data = (tmp.path / "data").string();
    std::vector<uint8_t> one = MakeBatteryGbRom();
    std::vector<uint8_t> two = one;
    two[0x134] ^= 0x01; // another title
    fs::create_directories(tmp.path / "a");
    fs::create_directories(tmp.path / "b");
    WriteFile(tmp.path / "a" / "game.gb", one);
    WriteFile(tmp.path / "b" / "game.gb", two);
    const auto first = FrameCores::ProbeRom((tmp.path / "a" / "game.gb").string());
    const auto second = FrameCores::ProbeRom((tmp.path / "b" / "game.gb").string());
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(FrameCores::GameFolder(data, *first) != FrameCores::GameFolder(data, *second));

    for (const auto& [folder, info] : {std::pair{"a", *first}, std::pair{"b", *second}}) {
        FrameCores::AltCoreHost host;
        REQUIRE(host.Boot((tmp.path / folder / "game.gb").string(), data,
                          /*headless_for_test=*/true));
        REQUIRE(host.SaveSlot(1));
        host.Stop();
        CHECK(fs::exists(Core::StatePathIn(FrameCores::GameFolder(data, info) + "/",
                                           info.title_id, 0, 1)));
    }
}

TEST_CASE("The DSi storage games write to is a copy", "[frame_cores]") {
    TempDir tmp("nand");
    const auto read = [](const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    const auto original = tmp.path / "dsi_nand.bin";
    CHECK(FrameCores::PrepareWorkingNand(tmp.path).empty()); // no original
    WriteFile(original, {1, 2, 3});
    const std::string copy = FrameCores::PrepareWorkingNand(tmp.path);
    REQUIRE(!copy.empty());
    CHECK(copy != original.string());
    CHECK(read(copy) == read(original));

    WriteFile(copy, {9, 9, 9}); // a game saved
    CHECK(FrameCores::PrepareWorkingNand(tmp.path) == copy);
    CHECK(read(copy) == std::string(3, '\x09'));
    CHECK(read(original) == std::string("\x01\x02\x03"));

    WriteFile(original, {4, 5, 6, 7}); // the player's file changed
    CHECK(FrameCores::PrepareWorkingNand(tmp.path) == copy);
    CHECK(read(copy) == read(original));
    CHECK(read(copy + ".bak") == std::string(3, '\x09'));
}

TEST_CASE("Loading a GB state leaves the battery save alone until the game saves", "[frame_cores]") {
    TempDir tmp("gbsave");
    const fs::path rom_path = tmp.path / "hb.gb";
    WriteFile(rom_path, MakeBatteryGbRom());
    const fs::path save = fs::path(FrameCores::GameFolder((tmp.path / "data").string(),
                                                          *FrameCores::ProbeRom(rom_path.string()))) /
                           "hb.sav";

    FrameCores::AltCoreHost host;
    REQUIRE(host.Boot(rom_path.string(), (tmp.path / "data").string(), /*headless_for_test=*/true));
    // Each press is followed by enough frames for the save file to be written
    const auto press = [&host](uint32_t button) {
        Common::AltCore::InputState in{};
        in.buttons = button;
        host.PushInput(in);
        REQUIRE(host.StepFrames(10));
        host.PushInput({});
        REQUIRE(host.StepFrames(120));
    };
    const auto pixel = [&host] { return host.LastFrame().primary.pixels[0]; };

    press(Common::AltCore::BtnA);
    CHECK(FirstByte(save) == 0x11);
    const uint32_t pixel_a = pixel();
    REQUIRE(host.SaveSlot(1));
    press(Common::AltCore::BtnB);
    CHECK(FirstByte(save) == 0x22);
    CHECK(pixel() != pixel_a);

    // The game sees the state's save data, the file keeps the newer one
    REQUIRE(host.LoadSlot(1));
    REQUIRE(host.StepFrames(120));
    CHECK(pixel() == pixel_a);
    CHECK(FirstByte(save) == 0x22);

    // What the game saves after the load is written
    press(Common::AltCore::BtnB);
    press(Common::AltCore::BtnA);
    CHECK(FirstByte(save) == 0x11);
    press(Common::AltCore::BtnB);
    REQUIRE(host.LoadSlot(1));
    REQUIRE(host.StepFrames(120));
    host.Stop();
    CHECK(FirstByte(save) == 0x22);
}

TEST_CASE("A game does not start without its renderer", "[frame_cores]") {
    TempDir tmp("norenderer");
    const fs::path rom_path = tmp.path / "hb.gba";
    WriteFile(rom_path, MakeHomebrewGbaRom());

    int calls = 0;
    FrameCores::HostCallbacks cb{};
    cb.on_capabilities = [&](const auto&) { ++calls; };
    cb.on_title_id = [&](uint64_t) { ++calls; };
    cb.on_input_sink = [&](Common::AltCore::InputSink) { ++calls; };
    cb.on_game_running = [&](bool) { ++calls; };
    FrameCores::AltCoreHost host;
    host.SetCallbacks(cb);
    // Not headless: there is no OpenXR here to draw with
    CHECK(!host.Boot(rom_path.string(), (tmp.path / "data").string()));
    CHECK(!host.IsRunning());
    CHECK(host.GetCapabilities().kind == Common::AltCore::CoreKind::N3DS);
    CHECK(Common::XrPacer::Get().emu_frame_us.load() == Common::XrPacer::kEmuFrameUs);
    CHECK(calls == 0);
    // The host is as new: the next start works
    REQUIRE(host.Boot(rom_path.string(), (tmp.path / "data").string(), /*headless_for_test=*/true));
    host.Stop();
}

TEST_CASE("Stopping a game gives the 3DS its state back and writes no state", "[frame_cores]") {
    TempDir tmp("stop");
    const fs::path rom_path = tmp.path / "hb.nds";
    WriteFile(rom_path, MakeHomebrewNdsRom(false));

    Common::AltCore::Capabilities caps = Common::AltCore::Capabilities::ForN3DS();
    bool sink = false, running = false;
    FrameCores::HostCallbacks cb{};
    cb.on_capabilities = [&](const Common::AltCore::Capabilities& c) { caps = c; };
    cb.on_input_sink = [&](Common::AltCore::InputSink s) { sink = static_cast<bool>(s); };
    cb.on_game_running = [&](bool on) { running = on; };
    FrameCores::AltCoreHost host;
    host.SetCallbacks(cb);
    REQUIRE(host.Boot(rom_path.string(), (tmp.path / "data").string(), /*headless_for_test=*/true));
    CHECK(caps.kind == Common::AltCore::CoreKind::NDS);
    CHECK(sink);
    CHECK(running);
    host.SetTouchEnabled(false);
    REQUIRE(host.StepFrames(5));
    const std::string states = host.StatesDir();
    const uint64_t key = host.GetRomInfo().title_id;
    host.Stop();

    CHECK(caps.kind == Common::AltCore::CoreKind::N3DS);
    CHECK(caps.screens.top_stereo);
    CHECK(!sink);
    CHECK(!running);
    CHECK(Common::XrPacer::Get().emu_frame_us.load() == Common::XrPacer::kEmuFrameUs);
    CHECK(Core::ListSaveStatesIn(states, key, 0).empty());
    CHECK(!fs::exists(Core::Autosave::Dir(states)));
}

TEST_CASE("Touch Screen Input off is the DS's only", "[frame_cores]") {
    using Common::AltCore::Capabilities;
    using Common::AltCore::TouchEnabled;
    CHECK(TouchEnabled(Capabilities::ForNDS(), true));
    CHECK(!TouchEnabled(Capabilities::ForNDS(), false));
    CHECK(!TouchEnabled(Capabilities::ForNDS(true), false));
    CHECK(!TouchEnabled(Capabilities::ForGBA(), true));
    CHECK(TouchEnabled(Capabilities::ForN3DS(), true));
    CHECK(TouchEnabled(Capabilities::ForN3DS(), false));
}

// The menu sends the new value, not "the other one": the same value twice changes nothing.
TEST_CASE("Microphone Blow and Close Lid take the value they get", "[frame_cores]") {
    {
        FrameCores::NdsCore core;
        const auto blows = [&core] {
            std::vector<int16_t> pcm(256, 1);
            const int n = core.OnMicReadInput(pcm.data(), static_cast<int>(pcm.size()));
            return std::any_of(pcm.begin(), pcm.begin() + n, [](int16_t s) { return s != 0; });
        };
        CHECK(!blows());
        for (const bool on : {true, true, false, false}) {
            core.SetMicBlow(on);
            CHECK(blows() == on);
        }
    }
    TempDir tmp("lid");
    const fs::path rom_path = tmp.path / "lid.nds";
    WriteFile(rom_path, MakeLidNdsRom());
    FrameCores::AltCoreHost host;
    REQUIRE(host.Boot(rom_path.string(), (tmp.path / "data").string(), /*headless_for_test=*/true));
    const auto pixel = [&host] {
        REQUIRE(host.StepFrames(20));
        return host.LastFrame().primary.pixels[0] & 0x00FFFFFFu;
    };
    const uint32_t open = pixel();
    host.SetLidClosed(true);
    const uint32_t closed = pixel();
    CHECK(open != closed);
    host.SetLidClosed(true);
    CHECK(pixel() == closed);
    host.SetLidClosed(false);
    CHECK(pixel() == open);
    host.SetLidClosed(false);
    CHECK(pixel() == open);
    // The menu's Microphone Blow: one press, over after its frames; SetMicBlow ends it
    constexpr int kFrames = FrameCores::AltCoreHost::kMicBlowFrames;
    CHECK(host.MicBlowFramesLeft() == 0);
    host.BlowMic();
    CHECK(host.MicBlowFramesLeft() == kFrames);
    REQUIRE(host.StepFrames(kFrames - 1));
    CHECK(host.MicBlowFramesLeft() == 1);
    host.BlowMic(); // pressed again: from the start
    CHECK(host.MicBlowFramesLeft() == kFrames);
    REQUIRE(host.StepFrames(kFrames));
    CHECK(host.MicBlowFramesLeft() == 0);
    host.BlowMic();
    host.SetMicBlow(false);
    CHECK(host.MicBlowFramesLeft() == 0);
    host.Stop();
}

TEST_CASE("DS and DSi adapters boot with FreeBIOS + JIT, touch, mic, lid, and save states",
          "[frame_cores]") {
    TempDir tmp("nds");
    const fs::path rom_path = tmp.path / "hb.nds";
    WriteFile(rom_path, MakeHomebrewNdsRom(false));

    FrameCores::AltCoreHost host;
    REQUIRE(host.Boot(rom_path.string(), (tmp.path / "data").string(), /*headless_for_test=*/true));
    CHECK(host.GetCapabilities().kind == Common::AltCore::CoreKind::NDS);

    Common::AltCore::InputState in{};
    in.buttons = Common::AltCore::BtnA | Common::AltCore::BtnX;
    in.touching = true;
    in.touch_x = 128;
    in.touch_y = 96;
    host.PushInput(in);
    host.SetMicBlow(true);
    host.SetScreenRotation(90);
    CHECK(host.GetCapabilities().screens.rotation_deg == 90);
    REQUIRE(host.StepFrames(10));

    const auto& frame = host.LastFrame();
    REQUIRE(frame.primary.pixels != nullptr);
    REQUIRE(frame.secondary.pixels != nullptr);
    CHECK(frame.primary.width == 256);
    CHECK(frame.primary.height == 192);
    CHECK(frame.secondary.width == 256);
    CHECK(frame.secondary.height == 192);

    host.SetLidClosed(true);
    REQUIRE(host.StepFrames(2));
    host.SetLidClosed(false);
    host.SetMicBlow(false);

    REQUIRE(host.SaveSlot(2));
    REQUIRE(host.StepFrames(4));
    REQUIRE(host.LoadSlot(2));

    host.Stop();
    CHECK(!host.IsRunning());

    // DSi mode boot & savestate check (D8)
    const fs::path dsi_path = tmp.path / "hb.dsi";
    WriteFile(dsi_path, MakeHomebrewNdsRom(true));
    FrameCores::CoreBootOptions dsi_opts{};
    dsi_opts.force_dsi = true;
    REQUIRE(host.Boot(dsi_path.string(), (tmp.path / "data").string(), /*headless_for_test=*/true,
                      dsi_opts));
    CHECK(host.GetCapabilities().kind == Common::AltCore::CoreKind::DSi);
    REQUIRE(host.StepFrames(6));
    REQUIRE(host.SaveSlot(3));
    REQUIRE(host.LoadSlot(3));
    host.Stop();

    // Automatic: a DSi-enhanced game without the player's DSi files is a DS game
    REQUIRE(host.Boot(dsi_path.string(), (tmp.path / "data").string(), /*headless_for_test=*/true));
    CHECK(host.GetCapabilities().kind == Common::AltCore::CoreKind::NDS);
    REQUIRE(host.StepFrames(2));
    host.Stop();
}

TEST_CASE("NDS adapter: the console a game starts on", "[frame_cores]") {
    using FrameCores::PicksDsi;
    const FrameCores::CoreBootOptions automatic{};
    CHECK_FALSE(PicksDsi(false, false, true, automatic));
    CHECK_FALSE(PicksDsi(false, true, false, automatic));
    CHECK(PicksDsi(false, true, true, automatic));
    CHECK(PicksDsi(true, true, false, automatic)); // made for the DSi only
    FrameCores::CoreBootOptions ds{};
    ds.force_nds = true;
    CHECK_FALSE(PicksDsi(true, true, true, ds));
    FrameCores::CoreBootOptions dsi{};
    dsi.force_dsi = true;
    CHECK(PicksDsi(false, false, false, dsi));
}

TEST_CASE("Autosaves: rotation, the menu's own, Keep lowered, loading", "[frame_cores]") {
    namespace A = Core::Autosave;
    TempDir tmp("autosave");
    const fs::path rom_path = tmp.path / "hb.gb";
    WriteFile(rom_path, MakeBatteryGbRom());
    std::vector<FrameCores::LoadDone> loads;
    FrameCores::AltCoreHost host;
    KeepLoads(host, loads);
    REQUIRE(host.Boot(rom_path.string(), (tmp.path / "data").string(), /*headless_for_test=*/true));
    const uint64_t key = host.GetRomInfo().title_id;
    const std::string dir = A::Dir(host.StatesDir());
    CHECK(A::List(dir, key, 0).empty());

    // Three older ones, and one of another game
    const uint64_t now = NowSeconds();
    const std::vector<uint8_t> body{1, 2, 3};
    fs::create_directories(dir);
    for (const uint64_t ago : {100, 200, 300}) {
        Core::WriteStateFile(A::PathFor(dir, key, 0, static_cast<s64>(now - ago)),
                             Core::CurrentStateHeader(key, now - ago), body);
    }
    const std::string other = A::PathFor(dir, key + 1, 0, static_cast<s64>(now - 400));
    Core::WriteStateFile(other, Core::CurrentStateHeader(key + 1, now - 400), body);

    Press(host, Common::AltCore::BtnA);
    const uint32_t pixel_a = PixelSoon(host);
    REQUIRE(host.MakeAutosave(A::Reason::Timer, 2));
    CHECK(!host.StatesBusy());
    auto list = A::List(dir, key, 0);
    REQUIRE(list.size() == 2);
    CHECK(list[0].time >= now);
    CHECK(!list[0].other);
    CHECK(fs::exists(list[0].thumb));
    CHECK(list[1].time == now - 100);
    CHECK(fs::exists(other));

    // The menu's has its own file and prunes nothing
    REQUIRE(host.MakeAutosave(A::Reason::MenuOpen, 1));
    REQUIRE(host.MakeAutosave(A::Reason::MenuOpen, 1));
    list = A::List(dir, key, 0);
    CHECK(list.size() == 3);
    CHECK(std::count_if(list.begin(), list.end(), [](const auto& e) { return e.menu; }) == 1);

    host.PruneAutosaves(1);
    list = A::List(dir, key, 0);
    std::erase_if(list, [](const auto& e) { return e.menu; });
    REQUIRE(list.size() == 1);
    CHECK(list[0].time >= now);

    // Loaded by the time the list showed, not another state in its file
    Press(host, Common::AltCore::BtnB);
    const uint32_t pixel_b = PixelSoon(host);
    CHECK(pixel_b != pixel_a);
    CHECK(!host.LoadAutosave(list[0].path, list[0].time + 1));
    CHECK(loads.back().result == FrameCores::LoadResult::Gone);
    CHECK(PixelSoon(host) == pixel_b);
    REQUIRE(host.LoadAutosave(list[0].path, list[0].time));
    CHECK(loads.back().what == FrameCores::LoadDone::What::Autosave);
    CHECK(loads.back().time == list[0].time);
    CHECK(PixelSoon(host) == pixel_a);
    REQUIRE(host.UndoLoad());
    CHECK(PixelSoon(host) == pixel_b);
    host.Stop();
    CHECK(!host.MakeAutosave(A::Reason::Timer, 2)); // no game
}

TEST_CASE("Undo Load goes back once, also after Import Save", "[frame_cores]") {
    TempDir tmp("undo");
    const std::string data = (tmp.path / "data").string();
    const fs::path rom_path = tmp.path / "hb.gb";
    WriteFile(rom_path, MakeBatteryGbRom());
    const fs::path save =
        fs::path(FrameCores::GameFolder(data, *FrameCores::ProbeRom(rom_path.string()))) / "hb.sav";
    std::vector<FrameCores::LoadDone> loads;
    FrameCores::AltCoreHost host;
    KeepLoads(host, loads);
    REQUIRE(host.Boot(rom_path.string(), data, /*headless_for_test=*/true));
    CHECK(!host.UndoLoad()); // nothing loaded yet

    Press(host, Common::AltCore::BtnA);
    const uint32_t pixel_a = PixelSoon(host);
    REQUIRE(host.SaveSlot(1));
    Press(host, Common::AltCore::BtnB);
    const uint32_t pixel_b = PixelSoon(host);
    REQUIRE(pixel_b != pixel_a);

    REQUIRE(host.LoadSlot(1));
    CHECK(PixelSoon(host) == pixel_a);
    REQUIRE(host.UndoLoad());
    CHECK(loads.back().what == FrameCores::LoadDone::What::Undo);
    CHECK(PixelSoon(host) == pixel_b);
    CHECK(!host.UndoLoad());
    CHECK(PixelSoon(host) == pixel_b);

    // A load that fails leaves the earlier load's Undo
    REQUIRE(host.LoadSlot(1));
    CHECK(!host.LoadSlot(2));
    REQUIRE(host.UndoLoad());
    CHECK(PixelSoon(host) == pixel_b);

    // A restart forgets it
    REQUIRE(host.LoadSlot(1));
    host.Reset();
    CHECK(!host.UndoLoad());

    // Import Save: the game and its save file both go back
    Press(host, Common::AltCore::BtnB);
    REQUIRE(FirstByte(save) == 0x22);
    std::vector<uint8_t> imported(0x2000, 0);
    imported[0] = 0x11;
    WriteFile(tmp.path / "import.sav", imported);
    CHECK(!host.ImportSave((tmp.path / "missing.sav").string()));
    CHECK(PixelSoon(host) == pixel_b);
    REQUIRE(host.ImportSave((tmp.path / "import.sav").string()));
    CHECK(loads.back().what == FrameCores::LoadDone::What::Import);
    REQUIRE(host.StepFrames(120));
    CHECK(PixelSoon(host) == pixel_a);
    REQUIRE(host.UndoLoad());
    REQUIRE(host.StepFrames(120));
    CHECK(PixelSoon(host) == pixel_b);
    host.Stop();
    CHECK(FirstByte(save) == 0x22);
}

TEST_CASE("A state of another game is refused", "[frame_cores]") {
    TempDir tmp("othergame");
    const std::string data = (tmp.path / "data").string();
    std::vector<uint8_t> one = MakeBatteryGbRom();
    std::vector<uint8_t> two = one;
    two[0x134] ^= 0x01; // another title
    WriteFile(tmp.path / "one.gb", one);
    WriteFile(tmp.path / "two.gb", two);

    FrameCores::AltCoreHost host;
    std::vector<FrameCores::LoadDone> loads;
    KeepLoads(host, loads);
    REQUIRE(host.Boot((tmp.path / "one.gb").string(), data, /*headless_for_test=*/true));
    REQUIRE(host.StepFrames(10));
    REQUIRE(host.SaveSlot(1));
    const std::string file =
        Core::StatePathIn(host.StatesDir(), host.GetRomInfo().title_id, 0, 1);

    REQUIRE(host.Boot((tmp.path / "two.gb").string(), data, /*headless_for_test=*/true));
    const uint64_t key = host.GetRomInfo().title_id;
    Press(host, Common::AltCore::BtnB);
    const uint32_t pixel = PixelSoon(host);
    fs::create_directories(host.StatesDir());
    fs::copy_file(file, Core::StatePathIn(host.StatesDir(), key, 0, 1));
    CHECK(Core::ListSaveStatesIn(host.StatesDir(), key, 0).empty());
    for (const bool anyway : {false, true}) {
        CHECK(!host.LoadSlot(1, anyway));
        CHECK(loads.back().result == FrameCores::LoadResult::OtherGame);
    }
    CHECK(PixelSoon(host) == pixel);
    CHECK(!host.UndoLoad());
    host.Stop();
}

TEST_CASE("A load that fails changes nothing", "[frame_cores]") {
    using FrameCores::LoadResult;
    TempDir tmp("failedload");
    const std::string data = (tmp.path / "data").string();
    const bool ds = GENERATE(false, true);
    const fs::path rom_path = tmp.path / (ds ? "hb.nds" : "hb.gb");
    WriteFile(rom_path, ds ? MakeLidNdsRom() : MakeBatteryGbRom());

    FrameCores::AltCoreHost host;
    std::vector<FrameCores::LoadDone> loads;
    KeepLoads(host, loads);
    REQUIRE(host.Boot(rom_path.string(), data, /*headless_for_test=*/true));
    REQUIRE(host.StepFrames(30));
    const uint64_t key = host.GetRomInfo().title_id;
    const auto slot = [&](u32 n) { return Core::StatePathIn(host.StatesDir(), key, 0, n); };
    const auto header = Core::CurrentStateHeader(key, NowSeconds());
    const uint32_t pixel = PixelSoon(host);
    REQUIRE(host.SaveSlot(1));
    const std::vector<uint8_t> good = ReadFile(slot(1));
    REQUIRE(good.size() > header.size());
    const std::vector<uint8_t> raw = Common::Compression::DecompressDataZSTD(
        std::span<const uint8_t>{good}.subspan(header.size()));
    REQUIRE(raw.size() > 1000);

    CHECK(!host.LoadSlot(2)); // none
    CHECK(loads.back().result == LoadResult::Gone);
    WriteFile(slot(3), {1, 2, 3}); // not a state
    CHECK(!host.LoadSlot(3));
    CHECK(loads.back().result == LoadResult::Failed);
    Core::WriteStateFile(slot(4), header, std::vector<uint8_t>{1, 2, 3}); // not compressed
    CHECK(!host.LoadSlot(4));
    CHECK(loads.back().result == LoadResult::Failed);
    Core::WriteStateFileFrom(slot(5), header, std::vector<uint8_t>(64, 0xAB)); // not the core's
    CHECK(!host.LoadSlot(5));
    CHECK(loads.back().result == LoadResult::Failed);
    Core::WriteStateFileFrom(slot(6), header,
                             std::span<const uint8_t>{raw}.first(raw.size() / 4)); // cut short
    CHECK(!host.LoadSlot(6));
    CHECK(loads.back().result == LoadResult::Failed);
    CHECK(PixelSoon(host) == pixel);
    CHECK(!host.UndoLoad());

    // Another build's: only with Load Anyway
    const auto old = Core::MakeStateHeader(key, std::string(40, '0'), "1.1.1", "AZF 1.1.1",
                                           NowSeconds());
    Core::WriteStateFile(slot(7), old, std::span<const uint8_t>{good}.subspan(header.size()));
    CHECK(!host.LoadSlot(7));
    CHECK(loads.back().result == LoadResult::OtherBuild);
    CHECK(loads.back().version == "1.1.1");
    CHECK(host.LoadSlot(7, /*anyway=*/true));
    CHECK(loads.back().result == LoadResult::Ok);
    CHECK(PixelSoon(host) == pixel);
    host.Stop();
}

TEST_CASE("A state waits for a driver's job, an autosave is dropped", "[frame_cores]") {
    namespace A = Core::Autosave;
    using FrameCores::LoadDone;
    using FrameCores::LoadResult;
    TempDir tmp("job");
    const fs::path rom_path = tmp.path / "hb.gb";
    WriteFile(rom_path, MakeBatteryGbRom());
    Job job;
    std::vector<LoadDone> loads;
    std::vector<A::Writer::Done> written;
    FrameCores::AltCoreHost host;
    FrameCores::HostFrontend frontend;
    frontend.on_loaded = [&loads](const LoadDone& done) { loads.push_back(done); };
    frontend.on_written = [&written](const A::Writer::Done& done) { written.push_back(done); };
    host.SetFrontend(std::move(frontend));
    REQUIRE(host.Boot(rom_path.string(), (tmp.path / "data").string(),
                      /*headless_for_test=*/true, WithJob(job)));
    CHECK(job.made == 1);
    const uint64_t key = host.GetRomInfo().title_id;
    const auto slot = [&](u32 n) { return Core::StatePathIn(host.StatesDir(), key, 0, n); };
    Press(host, Common::AltCore::BtnA);
    const uint32_t pixel_a = PixelSoon(host);

    // Save: waits, also behind one that waits; the autosaves are dropped
    job.frames = 5;
    REQUIRE(host.SaveSlot(1));
    for (const auto reason : {A::Reason::Timer, A::Reason::MenuOpen, A::Reason::Quit}) {
        CHECK(!host.MakeAutosave(reason, 2));
    }
    CHECK(host.StatesBusy());
    REQUIRE(host.StepFrames(5));
    CHECK(!fs::exists(slot(1)));
    CHECK(host.StatesBusy());
    CHECK(written.empty());
    REQUIRE(host.StepFrames(1));
    CHECK(fs::exists(slot(1)));
    CHECK(!host.StatesBusy());
    REQUIRE(written.size() == 1);
    CHECK(written[0].ok);
    CHECK(A::List(A::Dir(host.StatesDir()), key, 0).empty());
    REQUIRE(host.MakeAutosave(A::Reason::Timer, 2)); // its next turn

    // Load, then Undo Load: in their order
    Press(host, Common::AltCore::BtnB);
    const uint32_t pixel_b = PixelSoon(host);
    REQUIRE(pixel_b != pixel_a);
    job.frames = 3;
    REQUIRE(host.LoadSlot(1));
    REQUIRE(host.StepFrames(3));
    CHECK(loads.empty());
    CHECK(PixelSoon(host) == pixel_a);
    REQUIRE(loads.size() == 1);
    CHECK(loads[0].result == LoadResult::Ok);
    CHECK(job.loads == 1);
    job.frames = 3;
    REQUIRE(host.UndoLoad());
    REQUIRE(host.LoadSlot(1));
    REQUIRE(host.StepFrames(4));
    REQUIRE(loads.size() == 3);
    CHECK(loads[1].what == LoadDone::What::Undo);
    CHECK(loads[2].what == LoadDone::What::Slot);
    CHECK(job.loads == 3);
    CHECK(PixelSoon(host) == pixel_a);

    // A load that fails puts the moment before back: the driver hears of both
    Core::WriteStateFileFrom(slot(5), Core::CurrentStateHeader(key, NowSeconds()),
                             std::vector<uint8_t>(64, 0xAB));
    CHECK(!host.LoadSlot(5));
    CHECK(job.loads == 5);
    CHECK(PixelSoon(host) == pixel_a);

    // Import Save: the game starts again, with a driver of its own
    std::vector<uint8_t> imported(0x2000, 0);
    imported[0] = 0x22;
    WriteFile(tmp.path / "import.sav", imported);
    job.frames = 2;
    REQUIRE(host.ImportSave((tmp.path / "import.sav").string()));
    REQUIRE(host.StepFrames(2));
    CHECK(job.made == 1);
    REQUIRE(host.StepFrames(1));
    CHECK(job.made == 2);
    CHECK(job.loads == 6);
    CHECK(loads.back().what == LoadDone::What::Import);
    REQUIRE(host.StepFrames(120));
    CHECK(PixelSoon(host) == pixel_b);
    REQUIRE(host.UndoLoad());
    CHECK(job.made == 3);
    CHECK(job.loads == 7);
    CHECK(!host.ImportSave((tmp.path / "missing.sav").string()));
    CHECK(job.loads == 8);
    CHECK(PixelSoon(host) == pixel_a);

    // Restart: the save that waited would be of another moment
    written.clear();
    job.frames = 2;
    REQUIRE(host.SaveSlot(2));
    host.Reset();
    CHECK(job.resets == 1);
    REQUIRE(host.StepFrames(4));
    CHECK(!fs::exists(slot(2)));
    REQUIRE(written.size() == 1);
    CHECK(!written[0].ok);
    CHECK(written[0].slot == 2);
    CHECK(!host.StatesBusy());
    host.Stop();
}

TEST_CASE("A Game Boy driver's wider buffer shows the same picture", "[frame_cores]") {
    TempDir tmp("wide");
    const fs::path rom_path = tmp.path / "hb.gb";
    WriteFile(rom_path, MakeBatteryGbRom());
    const auto picture = [&](const char* data, const FrameCores::CoreBootOptions& opts) {
        FrameCores::AltCoreHost host;
        REQUIRE(host.Boot(rom_path.string(), (tmp.path / data).string(),
                          /*headless_for_test=*/true, opts));
        Press(host, Common::AltCore::BtnA);
        const auto& frame = host.LastFrame();
        REQUIRE(frame.primary.pixels != nullptr);
        CHECK(frame.primary.width == 160);
        CHECK(frame.primary.height == 144);
        const std::vector<uint32_t> pixels(frame.primary.pixels,
                                           frame.primary.pixels + 160 * 144);
        host.Stop();
        return pixels;
    };
    const auto plain = picture("plain", {});
    Job job;
    CHECK(picture("narrow", WithJob(job)) == plain);
    CHECK(job.picture.stride == 160);
    auto opts = WithJob(job);
    opts.driver.kind = FrameCores::DriverKind::PokeDaisy;
    CHECK(picture("wide", opts) == plain);
    CHECK(job.picture.width == 160);
    CHECK(job.picture.stride == 240);
}

TEST_CASE("Second Screen Off: no GBA map", "[frame_cores]") {
    TempDir tmp("mapoff");
    std::vector<uint8_t> rom = MakeHomebrewGbaRom();
    std::memcpy(&rom[0xAC], "B8KE", 4); // a map profile's
    const fs::path rom_path = tmp.path / "hb.gba";
    WriteFile(rom_path, rom);
    for (const uint32_t choice : {0u, 1u, 2u}) {
        FrameCores::CoreBootOptions opts;
        opts.second_screen = choice;
        FrameCores::AltCoreHost host;
        REQUIRE(host.Boot(rom_path.string(), (tmp.path / "data").string(),
                          /*headless_for_test=*/true, opts));
        CHECK(host.GetCapabilities().screens.has_bottom == (choice != 2));
        CHECK(!host.GetCapabilities().has_touch);
        REQUIRE(host.StepFrames(5));
        CHECK(host.LastFrame().secondary.pixels == nullptr); // none captured yet
        host.Stop();
    }
}

TEST_CASE("AudioResampler caps latency after pause and fast-forward bursts", "[frame_cores]") {
    FrameCores::AudioResampler resampler;
    REQUIRE(resampler.Init(32768, /*headless_null_sink=*/true));

    // Simulate 1 second of 400% fast-forward burst
    std::vector<int16_t> burst(32768 * 2, 1000);
    resampler.SetFastForward(true);
    resampler.Push(burst.data(), 32768);
    CHECK(resampler.BufferedMs() <= 55.0);

    // Return to normal speed and flush (like exiting in-game menu)
    resampler.SetFastForward(false);
    resampler.Flush();
    CHECK(resampler.BufferedMs() == 0.0);

    // Normal push stays bounded too
    resampler.Push(burst.data(), 32768);
    CHECK(resampler.BufferedMs() <= 105.0);
    resampler.Shutdown();
}

#ifndef _WIN32
TEST_CASE("S1 melonDS fault handler chains non-DS faults during and after DS run",
          "[frame_cores]") {
    TempDir tmp("s1crash");
    const fs::path rom_path = tmp.path / "hb.nds";
    WriteFile(rom_path, MakeHomebrewNdsRom(false));

    // Install a prior SIGSEGV handler (simulating Dynarmic / AzaharFrame crash reporter)
    struct sigaction sa{};
    sa.sa_sigaction = &TestPriorSegvHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    REQUIRE(sigaction(SIGSEGV, &sa, &g_saved_prev_segv) == 0);
    g_prior_handler_hits.store(0);

    FrameCores::AltCoreHost host;
    REQUIRE(host.Boot(rom_path.string(), (tmp.path / "data").string(), /*headless_for_test=*/true));
    REQUIRE(host.StepFrames(5));

    // 1. Deliberate fault on ANOTHER thread WHILE DS is actively running
    std::atomic<bool> worker_recovered{false};
    std::thread worker([&] {
        if (sigsetjmp(g_test_jmp, 1) == 0) {
            g_test_jmp_armed.store(true, std::memory_order_release);
            volatile int* bad = reinterpret_cast<volatile int*>(0x1000);
            (void)*bad;
        } else {
            worker_recovered.store(true, std::memory_order_release);
        }
    });
    worker.join();
    CHECK(worker_recovered.load());
    CHECK(g_prior_handler_hits.load() == 1);

    // 2. Stop DS, then trigger a fault on main thread (simulating 3DS fastmem / crash after DS)
    host.Stop();
    bool after_ds_recovered = false;
    if (sigsetjmp(g_test_jmp, 1) == 0) {
        g_test_jmp_armed.store(true, std::memory_order_release);
        volatile int* bad = reinterpret_cast<volatile int*>(0x2000);
        (void)*bad;
    } else {
        after_ds_recovered = true;
    }
    CHECK(after_ds_recovered);
    CHECK(g_prior_handler_hits.load() == 2);

    // Restore original SIGSEGV handler
    sigaction(SIGSEGV, &g_saved_prev_segv, nullptr);
}
#endif

namespace {

// A one-file zip with the file stored, not compressed.
std::vector<uint8_t> MakeStoredZip(const std::string& name, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> z;
    const auto put16 = [&](uint16_t v) {
        z.push_back(static_cast<uint8_t>(v));
        z.push_back(static_cast<uint8_t>(v >> 8));
    };
    const auto put32 = [&](uint32_t v) {
        put16(static_cast<uint16_t>(v));
        put16(static_cast<uint16_t>(v >> 16));
    };
    const uint32_t crc = FrameCores::ComputeCrc32(data.data(), data.size());
    const auto size = static_cast<uint32_t>(data.size());
    put32(0x04034B50u);
    put16(20);
    put16(0);
    put16(0);
    put32(0);
    put32(crc);
    put32(size);
    put32(size);
    put16(static_cast<uint16_t>(name.size()));
    put16(0);
    z.insert(z.end(), name.begin(), name.end());
    z.insert(z.end(), data.begin(), data.end());
    const auto cd_start = static_cast<uint32_t>(z.size());
    put32(0x02014B50u);
    put16(20);
    put16(20);
    put16(0);
    put16(0);
    put32(0);
    put32(crc);
    put32(size);
    put32(size);
    put16(static_cast<uint16_t>(name.size()));
    put16(0);
    put16(0);
    put16(0);
    put16(0);
    put32(0);
    put32(0);
    z.insert(z.end(), name.begin(), name.end());
    const auto cd_size = static_cast<uint32_t>(z.size()) - cd_start;
    put32(0x06054B50u);
    put16(0);
    put16(0);
    put16(1);
    put16(1);
    put32(cd_size);
    put32(cd_start);
    put16(0);
    return z;
}

} // namespace


TEST_CASE("Custom cheats are kept in a file and read back", "[frame_cores]") {
    TempDir tmp("cheatstore");
    const std::string path = (tmp.path / "g" / "x.custom.cheats").string();
    std::vector<FrameCores::CheatEntry> list = {
        {"Lives\tinfinite", "0123ABCD\n4567EF01", true},
        {"Off one", "ZZ", false},
    };
    REQUIRE(FrameCores::SaveCustomCheats(path, list));
    const auto back = FrameCores::LoadCustomCheats(path);
    REQUIRE(back.size() == 2);
    CHECK(back[0].name == "Lives\tinfinite");
    CHECK(back[0].code == "0123ABCD\n4567EF01");
    CHECK(back[0].enabled);
    CHECK(!back[1].enabled);

    // An empty list removes the file; a missing file is an empty list
    REQUIRE(FrameCores::SaveCustomCheats(path, {}));
    CHECK(!fs::exists(path));
    CHECK(FrameCores::LoadCustomCheats(path).empty());

    const auto lines = FrameCores::SplitCheatCode("AAA-BBB-CCC, 01FF5CD1\n\n02AA0000+x");
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == "AAA-BBB-CCC");
    CHECK(lines[3] == "x");
}

TEST_CASE("GBA custom cheats and DS cheat switches are kept when a game closes",
          "[frame_cores]") {
    TempDir tmp("cheatkeep");
    const auto data = (tmp.path / "data").string();

    const fs::path gba = tmp.path / "hb.gba";
    WriteFile(gba, MakeHomebrewGbaRom());
    {
        FrameCores::AltCoreHost host;
        REQUIRE(host.Boot(gba.string(), data, true));
        REQUIRE(host.AddCheat("Mine", "82003254 0014"));
        REQUIRE(host.SetCheatEnabled(host.ListCheats().size() - 1, false));
        host.Stop();
    }
    {
        FrameCores::AltCoreHost host;
        REQUIRE(host.Boot(gba.string(), data, true));
        const auto list = host.ListCheats();
        REQUIRE(!list.empty());
        CHECK(list.back().name == "Mine");
        CHECK(!list.back().enabled);
        host.Stop();
    }

    const fs::path nds = tmp.path / "hb.nds";
    WriteFile(nds, MakeHomebrewNdsRom(false));
    {
        FrameCores::AltCoreHost host;
        REQUIRE(host.Boot(nds.string(), data, true));
        REQUIRE(host.AddCheat("Ds one", "02000000 00000001"));
        REQUIRE(host.SetCheatEnabled(0, false));
        host.Stop();
    }
    {
        FrameCores::AltCoreHost host;
        REQUIRE(host.Boot(nds.string(), data, true));
        const auto list = host.ListCheats();
        REQUIRE(list.size() == 1);
        CHECK(!list[0].enabled);
        host.Stop();
    }
}

TEST_CASE("Game Boy cheats come from mGBA's cheat device and are kept", "[frame_cores]") {
    TempDir tmp("gbcheat");
    const auto data = (tmp.path / "data").string();
    const fs::path rom = tmp.path / "hb.gb";
    WriteFile(rom, MakeHomebrewGbRom());
    {
        FrameCores::AltCoreHost host;
        REQUIRE(host.Boot(rom.string(), data, true));
        CHECK(host.GetCapabilities().has_alt_cheats);
        CHECK(!host.AddCheat("Bad", "not a code"));
        REQUIRE(host.AddCheat("Lives", "01FF5CD1"));
        REQUIRE(host.StepFrames(3));
        REQUIRE(host.SetCheatEnabled(0, false));
        host.Stop();
    }
    {
        FrameCores::AltCoreHost host;
        REQUIRE(host.Boot(rom.string(), data, true));
        const auto list = host.ListCheats();
        REQUIRE(list.size() == 1);
        CHECK(list[0].name == "Lives");
        CHECK(!list[0].enabled);
        REQUIRE(host.StepFrames(3));
        host.Stop();
    }
}

TEST_CASE("Fast forward audio modes each do their own thing", "[frame_cores]") {
    using FrameCores::AudioResampler;
    using Mode = Settings::FastForwardAudio;
    CHECK(AudioResampler::ReadSpeed(true, Mode::SpedUp, 3.0) == 3.0);
    CHECK(AudioResampler::ReadSpeed(true, Mode::SpedUp, 100.0) == 8.0);
    CHECK(AudioResampler::ReadSpeed(true, Mode::SpedUp, 0.0) == 1.0);
    CHECK(AudioResampler::ReadSpeed(true, Mode::NormalPitch, 3.0) == 1.0);
    CHECK(AudioResampler::ReadSpeed(false, Mode::SpedUp, 3.0) == 1.0);
    CHECK(AudioResampler::LatencyCapMs(true, Mode::NormalPitch) == 45);
    CHECK(AudioResampler::LatencyCapMs(true, Mode::NormalSpeed) == 90);
    CHECK(AudioResampler::LatencyCapMs(false, Mode::Muted) == 90);

    std::vector<int16_t> burst(32768 * 2, 1000);
    const auto queued = [&](Mode mode) {
        AudioResampler r;
        REQUIRE(r.Init(32768, true));
        r.SetFastForward(true, mode, 4.0);
        r.Push(burst.data(), 32768);
        const double ms = r.BufferedMs();
        r.Shutdown();
        return ms;
    };
    CHECK(queued(Mode::Muted) == 0.0);
    CHECK(queued(Mode::NormalPitch) <= 55.0);
    CHECK(queued(Mode::SpedUp) <= 55.0);
    CHECK(queued(Mode::NormalSpeed) > queued(Mode::NormalPitch));
    CHECK(queued(Mode::NormalSpeed) <= 105.0);

    // Sped Up plays the queue faster: the same pull leaves less behind
    const auto left = [&](double speed) {
        AudioResampler r;
        REQUIRE(r.Init(32768, true));
        r.SetFastForward(true, Mode::SpedUp, speed);
        r.Push(burst.data(), 1400);
        std::vector<int16_t> out(400 * 2);
        r.Pull(out.data(), 400);
        const double ms = r.BufferedMs();
        r.Shutdown();
        return ms;
    };
    CHECK(left(4.0) < left(1.0));
}

TEST_CASE("A zipped ROM is probed once per file version and leaves nothing behind",
          "[frame_cores]") {
    TempDir tmp("zip");
    auto rom = MakeHomebrewGbaRom();
    const fs::path zip = tmp.path / "game.zip";
    WriteFile(zip, MakeStoredZip("inner.gba", rom));
    WriteFile(tmp.path / "mods.zip", MakeStoredZip("readme.txt", {'h', 'i'}));

    const auto first = FrameCores::ProbeRom(zip.string());
    REQUIRE(first.has_value());
    CHECK(first->kind == Common::AltCore::CoreKind::GBA);
    const auto second = FrameCores::ProbeRom(zip.string());
    REQUIRE(second.has_value());
    CHECK(second->title_id == first->title_id);
    CHECK(!FrameCores::ProbeRom((tmp.path / "mods.zip").string()).has_value());

    // A changed file is read again
    rom[0x10] ^= 0x55;
    WriteFile(zip, MakeStoredZip("inner.gba", rom));
    const auto third = FrameCores::ProbeRom(zip.string());
    REQUIRE(third.has_value());
    CHECK(third->title_id != first->title_id);

    // Only the files made here are in the folder
    size_t n = 0;
    for (const auto& e : fs::directory_iterator(tmp.path)) {
        (void)e;
        ++n;
    }
    CHECK(n == 2);
}

TEST_CASE("Stopping a game clears the running title id", "[frame_cores]") {
    TempDir tmp("titleid");
    const fs::path rom_path = tmp.path / "hb.gba";
    WriteFile(rom_path, MakeHomebrewGbaRom());
    uint64_t last = 0;
    FrameCores::HostCallbacks cb{};
    cb.on_title_id = [&](uint64_t tid) { last = tid; };
    FrameCores::AltCoreHost host;
    host.SetCallbacks(cb);
    REQUIRE(host.Boot(rom_path.string(), (tmp.path / "data").string(), true));
    CHECK(last != 0);
    host.Stop();
    CHECK(last == 0);
}

TEST_CASE("Layer Depth off: no right picture", "[frame_cores]") {
    const bool gba = GENERATE(true, false);
    DepthGame game(gba, MakeDeepRom(gba), 0);
    for (int i = 0; i < 30; ++i) {
        game.Run();
        REQUIRE(game.out.primary.HasPicture());
        REQUIRE(!game.out.primary_right.HasPicture());
    }
}

TEST_CASE("Layer Depth: with nothing moved both eyes see the same", "[frame_cores]") {
    const bool gba = GENERATE(true, false);
    DepthGame game(gba, MakeFlatRom(gba), 1);
    Pixels last;
    int changes = 0;
    for (int i = 0; i < 600; ++i) {
        INFO("frame " << i);
        game.Run(i % 7 == 0 ? Common::AltCore::BtnA : 0);
        const Pixels left = game.Left();
        REQUIRE(game.out.primary_right.width == game.out.primary.width);
        REQUIRE(game.out.primary_right.height == game.out.primary.height);
        REQUIRE(!game.out.primary_right.bgra);
        // Byte for byte, the first frame after the copy too
        REQUIRE((left == game.Right()));
        changes += left != last;
        last = left;
    }
    CHECK(changes > 500);
}

TEST_CASE("Layer Depth: no shift on a screen too wide for one", "[frame_cores]") {
    const bool gba = GENERATE(true, false);
    DepthGame game(gba, MakeDeepRom(gba), 3);
    game.core->SetMaxLayerShift(Common::LayerDepth::kOnScreen);
    for (int i = 0; i < 600; ++i) {
        INFO("frame " << i);
        game.Run(i % 7 == 0 ? Common::AltCore::BtnA : 0);
        REQUIRE((game.Left() == game.Right()));
        REQUIRE(game.out.layer_shift == 0.0f);
    }
    game.core->SetMaxLayerShift(0.0f); // no headset
    game.Run();
    CHECK(game.out.primary.HasPicture());
    CHECK(!game.out.primary_right.HasPicture());
}

TEST_CASE("Layer Depth: lines mGBA left undrawn at the start", "[frame_cores]") {
    // At line 0 forced blank goes off and no register was ever set to anything but 0. mGBA takes
    // the lines of its first, short frame as unchanged and leaves them blank, in both eyes. It ends
    // with the first register the game sets.
    std::vector<uint8_t> rom = MakeHomebrewGbaRom();
    const uint32_t code[] = {
        0xE3A00301u, // mov r0, #0x04000000
        0xE1D010B6u, // ldrh r1, [r0, #6]
        0xE3510000u, // cmp r1, #0
        0x1AFFFFFCu, // bne -2
        0xE1C010B0u, // strh r1, [r0]
        0xEAFFFFFEu, // b .
    };
    size_t pc = 0xC0;
    for (const uint32_t word : code) {
        PutLE32(rom, pc, word);
        pc += 4;
    }
    DepthGame game(true, rom, 3);
    const auto same = [&game] {
        for (int i = 0; i < 10; ++i) {
            INFO("frame " << i);
            game.Run();
            REQUIRE(game.out.primary_right.HasPicture());
            REQUIRE((game.Right() == game.Left()));
        }
        const Pixels left = game.Left();
        constexpr size_t kFirst = 126 * 240; // VCOUNT at the start
        CHECK(std::all_of(left.begin(), left.begin() + kFirst,
                          [](uint32_t p) { return p == 0xFF000000u; }));
        CHECK(std::none_of(left.begin() + kFirst, left.end(),
                           [](uint32_t p) { return p == 0xFF000000u; }));
    };
    same();
    game.core->Reset();
    same();
}

TEST_CASE("Layer Depth: each layer moves by its priority", "[frame_cores]") {
    const uint32_t level = GENERATE(1u, 2u, 3u);
    const bool gba = GENERATE(true, false);
    const bool inverted = GENERATE(false, true);
    const InvertDepth invert{inverted};
    // The expected right eye: the same game with its layers drawn there
    const int step = inverted ? -static_cast<int>(level) : static_cast<int>(level);
    const auto moved =
        gba ? MakeLayersGbaRom(1, 3, step, step * 3) : MakeLayersGbRom(false, step * 2);
    DepthGame game(gba, MakeDeepRom(gba), level);
    DepthGame expected(gba, moved, 0);
    int different = 0;
    for (int i = 0; i < 120; ++i) {
        INFO("frame " << i);
        game.Run();
        expected.Run();
        if (i < 20) {
            continue; // until the game has scrolled
        }
        REQUIRE((game.Right() == expected.Left()));
        REQUIRE(game.out.layer_shift ==
                Catch::Approx(gba ? step * 3 / 240.0 : step * 2 / 160.0));
        different += game.Right() != game.Left();
    }
    CHECK(different >= 98); // the Game Boy's palette is all one colour now and then
}

TEST_CASE("Layer Depth: the limit moves the layers less", "[frame_cores]") {
    const bool gba = GENERATE(true, false);
    const uint32_t width = gba ? 240 : 160;
    // Level 3's farthest layer would move 9 pixels of 240, 6 of 160
    const uint32_t most = GENERATE(1u, 2u, 4u, 5u);
    const float limit = (static_cast<float>(most) + 0.5f) / static_cast<float>(width);
    const uint32_t nearer = Common::LayerDepth::LayerShift(3, 1, 3, 240, limit);
    const auto moved =
        gba ? MakeLayersGbaRom(1, 3, static_cast<int>(nearer), static_cast<int>(most))
            : MakeLayersGbRom(false, static_cast<int>(most));
    DepthGame game(gba, MakeDeepRom(gba), 3);
    game.core->SetMaxLayerShift(limit);
    DepthGame expected(gba, moved, 0);
    for (int i = 0; i < 60; ++i) {
        INFO("frame " << i);
        game.Run();
        expected.Run();
        REQUIRE(game.out.layer_shift * static_cast<float>(width) ==
                Catch::Approx(static_cast<float>(most)));
        if (i >= 20) {
            REQUIRE((game.Right() == expected.Left()));
        }
    }
}

TEST_CASE("Layer Depth: inverted while the game runs", "[frame_cores]") {
    const bool gba = GENERATE(true, false);
    InvertDepth invert{false};
    DepthGame game(gba, MakeDeepRom(gba), 2);
    DepthGame plain(gba, MakeDeepRom(gba), 0);
    DepthGame same(gba, MakeDeepRom(gba), 2);
    DepthGame other(gba, MakeDeepRom(gba), 2);
    for (int i = 0; i < 120; ++i) {
        INFO("frame " << i);
        const bool on = i / 20 % 2 != 0;
        invert.Set(on);
        game.Run();
        plain.Run();
        invert.Set(false);
        same.Run();
        invert.Set(true);
        other.Run();
        REQUIRE((game.Right() == (on ? other : same).Right()));
        REQUIRE(game.out.layer_shift == (on ? other : same).out.layer_shift);
        REQUIRE((game.Left() == plain.Left())); // the game's own picture is never touched
        REQUIRE((plain.Right().empty()));       // level 0: none, either way
        REQUIRE(plain.out.layer_shift == 0.0f);
    }
}

TEST_CASE("Layer Depth: inverted, with nothing moved both eyes see the same", "[frame_cores]") {
    const bool gba = GENERATE(true, false);
    const bool on_screen = GENERATE(true, false);
    const InvertDepth invert{true};
    DepthGame game(gba, on_screen ? MakeDeepRom(gba) : MakeFlatRom(gba), 3);
    if (on_screen) {
        game.core->SetMaxLayerShift(Common::LayerDepth::kOnScreen);
    }
    for (int i = 0; i < 120; ++i) {
        INFO("frame " << i);
        game.Run();
        REQUIRE((game.Left() == game.Right()));
    }
}

TEST_CASE("Layer Depth: changed while the game runs", "[frame_cores]") {
    const bool gba = GENERATE(true, false);
    DepthGame game(gba, MakeDeepRom(gba), 0);
    DepthGame plain(gba, MakeDeepRom(gba), 0);
    CHECK(game.core->GetCapabilities().layer_depth);
    std::vector<std::unique_ptr<DepthGame>> expected;
    for (uint32_t level = 0; level <= 3; ++level) {
        expected.push_back(std::make_unique<DepthGame>(gba, MakeDeepRom(gba), level));
    }
    int frame = 0;
    const auto run = [&](uint32_t level, int frames) {
        game.core->SetLayerDepth(level);
        for (int i = 0; i < frames; ++i, ++frame) {
            INFO("frame " << frame << " level " << level);
            game.Run();
            plain.Run();
            for (auto& other : expected) {
                other->Run();
            }
            REQUIRE((game.Left() == plain.Left()));
            REQUIRE((game.out.audio_pcm == plain.out.audio_pcm));
            REQUIRE((game.Right() == expected[level]->Right()));
            REQUIRE(game.out.layer_shift == expected[level]->out.layer_shift);
        }
    };
    run(0, 30);
    for (const uint32_t level : {2u, 0u, 3u, 1u, 1u, 0u, 2u}) {
        run(level, 10);
    }
    // Import Save starts the core again, at the level it had
    const fs::path save = game.tmp.path / "import.sav";
    WriteFile(save, std::vector<uint8_t>(0x8000, 0x5A));
    REQUIRE(game.core->ImportBatterySave(save.string()));
    for (int i = 0; i < 30; ++i) {
        game.Run();
    }
    CHECK(game.out.primary_right.HasPicture());
    CHECK((game.Right() != game.Left()));
}

TEST_CASE("Layer Depth leaves the game's picture and sound alone", "[frame_cores]") {
    const bool gba = GENERATE(true, false);
    DepthGame with(gba, MakeDeepRom(gba), 3);
    DepthGame without(gba, MakeDeepRom(gba), 0);
    bool heard = false;
    for (int i = 0; i < 300; ++i) {
        INFO("frame " << i);
        const uint32_t buttons = i % 5 == 0 ? Common::AltCore::BtnA : 0;
        with.Run(buttons);
        without.Run(buttons);
        REQUIRE((with.out.audio_pcm == without.out.audio_pcm));
        REQUIRE((with.Left() == without.Left()));
        REQUIRE(with.out.rumble == without.out.rumble);
        const auto& pcm = with.out.audio_pcm;
        heard |= std::any_of(pcm.begin(), pcm.end(), [](int16_t v) { return v != 0; });
    }
    CHECK(heard);
}

TEST_CASE("Layer Depth after a reset, a state and the next game", "[frame_cores]") {
    const bool gba = GENERATE(true, false);
    DepthGame game(gba, MakeFlatRom(gba), 1);
    const auto same = [&game](int frames) {
        for (int i = 0; i < frames; ++i) {
            INFO("frame " << i);
            game.Run();
            REQUIRE((game.Right() == game.Left()));
            REQUIRE(game.out.primary_right.HasPicture());
        }
    };
    same(40);
    std::vector<uint8_t> state;
    REQUIRE(game.core->SaveStateToMemory(state));
    same(20);
    game.core->Reset();
    same(40);
    REQUIRE(game.core->LoadStateFromMemory(state.data(), state.size()));
    game.core->AfterStateLoad();
    same(20);

    const fs::path save = game.tmp.path / "import.sav";
    WriteFile(save, std::vector<uint8_t>(0x8000, 0x5A));
    REQUIRE(game.core->ImportBatterySave(save.string()));
    same(20);

    game.core->Unload();
    game.Run();
    CHECK(!game.out.primary.HasPicture());
    CHECK(!game.out.primary_right.HasPicture());
}

TEST_CASE("Layer Depth goes by the driver", "[frame_cores]") {
    using FrameCores::DriverKind;
    Job job;
    {
        // A held frame keeps the picture
        DepthGame game(true, MakeDeepRom(true), 2, WithJob(job));
        for (int i = 0; i < 30; ++i) {
            game.Run();
        }
        const Pixels right = game.Right();
        REQUIRE(!right.empty());
        CHECK((right != game.Left()));
        job.held = true;
        for (int i = 0; i < 3; ++i) {
            game.Run();
            CHECK(game.out.held);
            CHECK((game.Right() == right));
        }
        job.held = false;
        game.Run();
        CHECK((game.Right() != right));
    }
    for (const auto kind : {DriverKind::PokeDaisy, DriverKind::GbaDs}) {
        for (const bool gba : {true, false}) {
            if (!gba && kind == DriverKind::GbaDs) {
                continue;
            }
            INFO("driver " << static_cast<int>(kind) << " gba " << gba);
            auto opts = WithJob(job);
            opts.driver.kind = kind;
            DepthGame game(gba, MakeDeepRom(gba), 2, opts);
            DepthGame plain(gba, MakeDeepRom(gba), 2);
            CHECK(game.core->GetCapabilities().layer_depth);
            for (int i = 0; i < 30; ++i) {
                game.Run();
                plain.Run();
                REQUIRE((game.Left() == plain.Left()));
                REQUIRE((game.Right() == plain.Right()));
                REQUIRE(game.out.layer_shift == plain.out.layer_shift);
            }
            CHECK((game.Right() != game.Left()));
            // Something of the driver's on the game's picture: both eyes see the same
            job.paint = 5;
            for (int i = 0; i < 3; ++i) {
                game.Run();
                plain.Run();
                CHECK((game.Left() != plain.Left()));
                CHECK((game.Right() == game.Left()));
                CHECK(game.out.layer_shift == 0.0f);
            }
            job.paint = -1;
            game.Run();
            plain.Run();
            CHECK((game.Right() == plain.Right()));
            CHECK(game.out.layer_shift == plain.out.layer_shift);
        }
    }

    // The runner: one step a frame, and the picture only where it belongs
    uint32_t steps = 0;
    Pixels pixels(16 * 8, 0x00112233u);
    FrameCores::DriverServices services;
    services.step = [&steps](uint32_t) { ++steps; };
    services.picture = [&pixels] {
        return FrameCores::DriverPicture{pixels.data(), 16, 8, 16};
    };
    auto caps = Common::AltCore::Capabilities::ForGBA();
    FrameCores::DriverRunner runner;
    FrameCores::FrameOutput out;
    const Pixels moved(16 * 8, 0xFF332211u);
    const FrameCores::DriverPicture right{moved.data(), 16, 8, 16};
    const FrameCores::DriverPicture game{pixels.data(), 16, 8, 16};
    runner.Start(DriverKind::PokeDaisy, {}, services, {}, WithJob(job), {}, caps);
    const auto frame = [&] {
        runner.RunFrame({}, out, caps);
        CHECK(!out.primary_right.HasPicture());
        CHECK(out.layer_shift == 0.0f);
    };
    frame();
    runner.SetRightEye(right, game, 0.5f, out); // its alpha is none of the picture's
    CHECK(out.primary_right.pixels == moved.data());
    CHECK(out.layer_shift == 0.5f);
    frame();
    pixels.back() ^= 1; // the driver shows another picture
    runner.SetRightEye(right, game, 0.5f, out);
    CHECK(out.primary_right.pixels == out.primary.pixels);
    CHECK(out.layer_shift == 0.0f);
    frame();
    runner.SetRightEye({moved.data(), 8, 8, 16}, game, 0.5f, out); // another size
    CHECK(out.primary_right.pixels == out.primary.pixels);
    frame();
    runner.SetRightEye(right, {}, 0.5f, out);
    CHECK(out.primary_right.pixels == out.primary.pixels);
    frame();
    runner.SetRightEye({}, game, 0.5f, out);
    CHECK(!out.primary_right.HasPicture());
    CHECK(steps == 5);
    CHECK(runner.Missteps() == 0);
    runner.Stop();
}
