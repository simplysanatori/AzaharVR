// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csetjmp>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/alt_core_types.h"
#include "common/xr_pacer.h"
#include "frame_cores/alt_core_host.h"
#include "frame_cores/audio_resampler.h"
#include "frame_cores/nds_core.h"
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

    const auto& frame = host.LastFrame();
    REQUIRE(frame.primary.pixels != nullptr);
    CHECK(frame.primary.width == 240);
    CHECK(frame.primary.height == 160);

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
    CHECK(!host.ListSaveSlots().empty());
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

    const auto& frame = host.LastFrame();
    REQUIRE(frame.primary.pixels != nullptr);
    CHECK(frame.primary.width == 160);
    CHECK(frame.primary.height == 144);

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
        CHECK(fs::exists(fs::path(FrameCores::GameFolder(data, info)) / "game.slot1.state"));
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
    host.Stop();

    CHECK(caps.kind == Common::AltCore::CoreKind::N3DS);
    CHECK(caps.screens.top_stereo);
    CHECK(!sink);
    CHECK(!running);
    CHECK(Common::XrPacer::Get().emu_frame_us.load() == Common::XrPacer::kEmuFrameUs);
    CHECK(fs::is_empty(tmp.path / "data" / "ds" / "states"));
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
