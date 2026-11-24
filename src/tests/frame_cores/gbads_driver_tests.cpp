// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "frame_cores/frame_driver.h"
#include "frame_cores/gbads_driver.h"
#include "gbads_game.h"
#include "gbads_logo.h"

extern "C" {
#include "emu.h"
#include "profiles.h"
#include <mgba/core/core.h>
}

using namespace FrameCores;
namespace fs = std::filesystem;

namespace {

constexpr std::size_t kPixels = std::size_t{GBA_W} * GBA_H;

/// A core that only counts.
struct FakeGame {
    ::Emu emu{};
    std::vector<uint32_t> fb = std::vector<uint32_t>(kPixels, 0x00102030);
    std::vector<uint32_t> keys;
    std::thread::id step_thread;
    std::vector<int16_t> sound;
    float rumble = 0.0f;

    void MakeSound(int16_t value, std::size_t frames) {
        sound.insert(sound.end(), frames * 2u, value);
    }

    DriverServices Services() {
        DriverServices s;
        s.emu = &emu;
        s.rom_path = "/game.gba";
        s.step = [this](uint32_t k) {
            keys.push_back(k);
            step_thread = std::this_thread::get_id();
            MakeSound(1, 5000);
            rumble = 0.5f;
        };
        s.drain_audio = [this](int16_t* out, std::size_t max_frames) -> std::size_t {
            const std::size_t n = std::min(max_frames, sound.size() / 2u);
            std::copy_n(sound.begin(), n * 2u, out);
            sound.erase(sound.begin(), sound.begin() + static_cast<std::ptrdiff_t>(n * 2u));
            return n;
        };
        s.rumble = [this] { return rumble; };
        s.picture = [this] { return DriverPicture{fb.data(), GBA_W, GBA_H, GBA_W}; };
        return s;
    }
};

/// The loop, as a test says it.
struct FakeLoop {
    std::function<void(const GbadsHost&)> body;
    bool has_output = true;
    std::array<GbadsPicture, 2> pictures{};
    int running = 0;
    int runs = 0;
    int events = 0;
    int turns = 0;
    int restarts = 0;
    int loads = 0;
    std::string rom;
    std::string logo_dir;
};
FakeLoop g_fake;

bool Top(const GbadsHost& h) {
    bool turn = false;
    if (!h.events(h.ctx, &turn)) {
        return false;
    }
    ++g_fake.events;
    g_fake.turns += turn ? 1 : 0;
    return true;
}

/// As game.c's: a frame and a picture in a pass.
void Passes(const GbadsHost& h) {
    while (Top(h)) {
        if (!h.step(h.ctx, h.keys(h.ctx)) || !h.present(h.ctx)) {
            return;
        }
    }
}

const GbadsLoop kFakeLoop{
    [](const GbadsHost* h) {
        ++g_fake.running;
        ++g_fake.runs;
        g_fake.rom = h->rom_path;
        g_fake.logo_dir = h->logo_dir ? h->logo_dir : "";
        g_fake.body(*h);
        --g_fake.running;
    },
    [](int* w, int* h) {
        *w = 480;
        *h = 320;
        return g_fake.has_output;
    },
    [](GbadsPicture out[2]) {
        out[0] = g_fake.pictures[0];
        out[1] = g_fake.pictures[1];
    },
    [] { ++g_fake.restarts; },
    [] { ++g_fake.loads; },
};

GbaDsLimits QuickLimits() {
    GbaDsLimits limits;
    limits.frames = 3;
    limits.wait = std::chrono::milliseconds{100};
    return limits;
}

std::unique_ptr<GbaDsDriver> MakeFake(FakeGame& game,
                                      std::function<void(const GbadsHost&)> body = Passes) {
    g_fake = FakeLoop{};
    g_fake.body = std::move(body);
    return GbaDsDriver::Make(game.Services(), kFakeLoop, QuickLimits());
}

DriverFrame Run(FrameDriver& driver, uint32_t keys = 0) {
    DriverInput in;
    in.keys = keys;
    DriverFrame out;
    driver.RunFrame(in, out);
    return out;
}

/// The driver is down to the game alone, and the thread is gone.
void CheckAlone(FakeGame& game, GbaDsDriver& driver) {
    CHECK(g_fake.running == 0);
    CHECK(driver.SecondarySize() == std::pair<uint32_t, uint32_t>{0, 0});
    const std::size_t before = game.keys.size();
    const DriverFrame out = Run(driver, 4);
    CHECK(game.keys.size() == before + 1);
    CHECK(game.keys.back() == 4);
    REQUIRE(out.primary.pixels);
    CHECK(out.primary.width == GBA_W);
    CHECK(out.primary.pixels[0] == 0xFF102030);
    CHECK(out.secondary.pixels == nullptr);
    CHECK(out.audio_pcm.size() == 5000 * 2);
    CHECK(driver.StateSafeNow());
    driver.Reset();
    driver.AfterStateLoad();
    CHECK(g_fake.restarts == 0);
    CHECK(g_fake.loads == 0);
}

} // namespace

TEST_CASE("GbaDsDriver: one frame in a RunFrame, on its own thread", "[frame_cores]") {
    FakeGame game;
    auto driver = MakeFake(game);
    REQUIRE(driver);
    CHECK(game.keys.empty()); // the set-up runs nothing
    CHECK(g_fake.rom == "/game.gba");
    CHECK(driver->SecondarySize() == std::pair<uint32_t, uint32_t>{480, 320});
    CHECK_FALSE(driver->HasTouch());
    CHECK_FALSE(driver->CanEdit());
    CHECK(driver->StateSafeNow());

    for (uint32_t i = 0; i < 20; ++i) {
        Run(*driver, 0xFC00u | i);
        REQUIRE(game.keys.size() == i + 1);
        CHECK(game.keys.back() == i); // the GBA's ten keys alone
        CHECK(game.step_thread != std::this_thread::get_id());
    }

    // Nothing runs while nothing is asked
    const int events = g_fake.events;
    std::this_thread::sleep_for(std::chrono::milliseconds{30});
    CHECK(g_fake.events == events);
    CHECK(game.keys.size() == 20);
}

TEST_CASE("GbaDsDriver: pictures", "[frame_cores]") {
    FakeGame game;
    auto driver = MakeFake(game);
    REQUIRE(driver);

    std::vector<uint32_t> primary(kPixels, 0x00030201);
    std::vector<uint32_t> secondary(6, 0x00C86432);
    g_fake.pictures[0] = {primary.data(), GBA_W, GBA_H, 0};
    g_fake.pictures[1] = {secondary.data(), 3, 2, 0};
    DriverFrame out = Run(*driver);
    REQUIRE(out.primary.pixels);
    CHECK(out.primary.pixels[0] == 0xFF030201);
    REQUIRE(out.secondary.pixels);
    CHECK(out.secondary.width == 3);
    CHECK(out.secondary.height == 2);
    CHECK(out.secondary.stride == 3);
    CHECK(out.secondary.pixels[5] == 0xFFC86432);
    CHECK(driver->SecondarySize() == std::pair<uint32_t, uint32_t>{3, 2});

    g_fake.pictures[1].shade = 255;
    CHECK(Run(*driver).secondary.pixels[0] == 0xFF000000);

    // No picture: none is shown, and the size stays
    g_fake.pictures[1] = {};
    CHECK(Run(*driver).secondary.pixels == nullptr);
    CHECK(driver->SecondarySize() == std::pair<uint32_t, uint32_t>{3, 2});

    // The Primary Screen never grows
    g_fake.pictures[0] = {primary.data(), 120, 160, 0};
    out = Run(*driver);
    CHECK(out.primary.width == GBA_W);
    CHECK(out.primary.height == GBA_H);
    CHECK(out.primary.pixels[0] == 0xFF102030);
}

TEST_CASE("GbaDsDriver: frames drawn again make no sound", "[frame_cores]") {
    FakeGame game;
    auto driver = MakeFake(game, [&game](const GbadsHost& h) {
        while (Top(h)) {
            if (!h.step(h.ctx, h.keys(h.ctx))) {
                return;
            }
            game.MakeSound(2, 9000);
            game.rumble = 1.0f;
            if (!h.present(h.ctx)) {
                return;
            }
        }
    });
    REQUIRE(driver);
    for (int i = 0; i < 3; ++i) {
        const DriverFrame out = Run(*driver);
        CHECK(out.audio_pcm == std::vector<int16_t>(5000 * 2, 1));
        CHECK(out.rumble == 0.5f);
        CHECK(game.sound.empty());
    }
    CHECK(game.keys.size() == 3);
}

TEST_CASE("GbaDsDriver: a loop that does not step", "[frame_cores]") {
    FakeGame game;
    auto driver = MakeFake(game, [](const GbadsHost& h) {
        while (Top(h) && h.present(h.ctx)) {
        }
    });
    REQUIRE(driver);
    for (std::size_t i = 1; i <= 2; ++i) {
        Run(*driver);
        CHECK(game.keys.size() == i);
        CHECK(g_fake.running == 1);
    }
    Run(*driver);
    CHECK(game.keys.size() == 3);
    CheckAlone(game, *driver);
}

TEST_CASE("GbaDsDriver: a loop that shows nothing", "[frame_cores]") {
    FakeGame game;
    auto driver = MakeFake(game, [](const GbadsHost& h) {
        while (Top(h) && h.step(h.ctx, h.keys(h.ctx))) {
        }
    });
    REQUIRE(driver);
    for (uint32_t i = 1; i <= 2; ++i) {
        Run(*driver, i);
        CHECK(game.keys.size() == i);
        CHECK(game.keys.back() == i); // not the keys it read in the frame before
        CHECK(g_fake.running == 1);
    }
    Run(*driver, 3);
    CHECK(game.keys.size() == 3);
    CheckAlone(game, *driver);
}

TEST_CASE("GbaDsDriver: a pass without a picture now and then is no reason", "[frame_cores]") {
    FakeGame game;
    auto driver = MakeFake(game, [](const GbadsHost& h) {
        for (int i = 0; Top(h); ++i) {
            if (!h.step(h.ctx, h.keys(h.ctx)) || (i % 2 == 0 && !h.present(h.ctx))) {
                return;
            }
        }
    });
    REQUIRE(driver);
    for (std::size_t i = 1; i <= 30; ++i) {
        Run(*driver);
        REQUIRE(game.keys.size() == i);
    }
    CHECK(g_fake.running == 1);
}

TEST_CASE("GbaDsDriver: a loop that leaves by itself", "[frame_cores]") {
    FakeGame game;
    auto driver = MakeFake(game, [](const GbadsHost& h) {
        for (int i = 0; i < 2 && Top(h); ++i) {
            if (!h.step(h.ctx, h.keys(h.ctx)) || !h.present(h.ctx)) {
                return;
            }
        }
    });
    REQUIRE(driver);
    Run(*driver);
    Run(*driver);
    Run(*driver);
    CHECK(game.keys.size() == 3);
    CheckAlone(game, *driver);
}

TEST_CASE("GbaDsDriver: a loop that does not come back is told to leave", "[frame_cores]") {
    FakeGame game;
    bool step_first = false;
    SECTION("before its frame") {}
    SECTION("after its frame") {
        step_first = true;
    }
    auto driver = MakeFake(game, [step_first](const GbadsHost& h) {
        if (!Top(h) || (step_first && !h.step(h.ctx, 0))) {
            return;
        }
        while (h.alive(h.ctx)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    });
    REQUIRE(driver);
    Run(*driver);
    CHECK(game.keys.size() == 1);
    CheckAlone(game, *driver);
}

TEST_CASE("GbaDsDriver: a slow core is not the loop's doing", "[frame_cores]") {
    FakeGame game;
    g_fake = FakeLoop{};
    g_fake.body = Passes;
    DriverServices services = game.Services();
    services.step = [&game](uint32_t k) {
        std::this_thread::sleep_for(std::chrono::milliseconds{250});
        game.keys.push_back(k);
    };
    auto driver = GbaDsDriver::Make(services, kFakeLoop, QuickLimits());
    REQUIRE(driver);
    Run(*driver);
    CHECK(game.keys.size() == 1);
    CHECK(g_fake.running == 1);
}

TEST_CASE("GbaDsDriver: teardown and another game", "[frame_cores]") {
    FakeGame game;
    for (const int frames : {0, 1, 5}) {
        auto driver = MakeFake(game);
        REQUIRE(driver);
        for (int i = 0; i < frames; ++i) {
            Run(*driver);
        }
        driver.reset();
        CHECK(g_fake.running == 0);
    }
    {
        // Parked in the middle of a pass
        auto driver = MakeFake(game, [](const GbadsHost& h) {
            while (Top(h) && h.step(h.ctx, 0)) {
            }
        });
        REQUIRE(driver);
        Run(*driver);
        driver.reset();
        CHECK(g_fake.running == 0);
    }
    {
        // Paying no attention to the top of its pass
        auto driver = MakeFake(game, [](const GbadsHost& h) {
            Top(h);
            while (h.alive(h.ctx)) {
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
        });
        REQUIRE(driver);
        driver.reset();
        CHECK(g_fake.running == 0);
    }

    auto first = MakeFake(game);
    REQUIRE(first);
    CHECK_FALSE(GbaDsDriver::Make(game.Services(), kFakeLoop, QuickLimits())); // one at a time
    CHECK(g_fake.runs == 1);
    Run(*first);
    first.reset();
    CHECK(MakeFake(game));
}

TEST_CASE("GbaDsDriver: games it does not start with", "[frame_cores]") {
    FakeGame game;
    CHECK_FALSE(MakeFake(game, [](const GbadsHost&) {})); // left in its set-up
    CHECK(g_fake.running == 0);

    g_fake = FakeLoop{};
    g_fake.body = Passes;
    g_fake.has_output = false;
    CHECK_FALSE(GbaDsDriver::Make(game.Services(), kFakeLoop, QuickLimits()));
    CHECK(g_fake.running == 0);

    DriverServices no_game = game.Services();
    no_game.emu = nullptr;
    CHECK_FALSE(GbaDsDriver::Make(no_game, kFakeLoop, QuickLimits()));

    CHECK(game.keys.empty());
    CHECK(MakeFake(game));
}

TEST_CASE("GbaDsDriver: reset, loaded states and pages", "[frame_cores]") {
    FakeGame game;
    auto driver = MakeFake(game);
    REQUIRE(driver);
    driver->Reset();
    driver->AfterStateLoad();
    driver->AfterStateLoad();
    CHECK(g_fake.restarts == 1);
    CHECK(g_fake.loads == 2);
    CHECK(game.keys.empty());

    Run(*driver);
    CHECK(g_fake.turns == 0);
    driver->TurnPage();
    Run(*driver);
    Run(*driver);
    CHECK(g_fake.turns == 1);
}

TEST_CASE("GbaDsDriver: logos are kept in the game's own folder", "[frame_cores]") {
    FakeGame game;
    CHECK(MakeFake(game));
    CHECK(g_fake.logo_dir.empty()); // no folder, no logos

    g_fake = FakeLoop{};
    g_fake.body = Passes;
    DriverServices services = game.Services();
    services.game_dir = "/games/1";
    CHECK(GbaDsDriver::Make(services, kFakeLoop, QuickLimits()));
    CHECK(g_fake.logo_dir == "/games/1/logo");
}

// ---------------------------------------------------------------- game.c itself, on ROMs made here

namespace {

/// An endless loop and a header with a game code, nothing of any game. The test draws the picture.
struct MadeGame {
    fs::path folder; ///< nothing but this game's
    fs::path path;
    std::string game_dir;
    ::Emu emu{};
    bool open = false;
    int steps = 0;
    std::vector<int16_t> scratch = std::vector<int16_t>(8192, 0);

    explicit MadeGame(const char* code, bool title = false) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        folder = fs::temp_directory_path() / ("azaharvr_gbads_test_" + std::to_string(stamp));
        fs::create_directories(folder);
        path = folder / "game.gba";
        std::vector<uint8_t> rom(std::size_t{1} << 20, 0xFF);
        std::fill_n(rom.begin(), 0x200, uint8_t{0});
        const uint32_t branch = 0xEA00002E; // to 0xC0
        const std::array<uint32_t, 4> loop{0xE2800001, 0xE0811000, 0xE0222001, 0xEAFFFFFB};
        std::memcpy(rom.data(), &branch, sizeof(branch));
        std::memcpy(rom.data() + 0xA0, "TEST", 4);
        std::memcpy(rom.data() + 0xAC, code, 4);
        std::memcpy(rom.data() + 0xB0, "00", 2);
        rom[0xB2] = 0x96;
        std::memcpy(rom.data() + 0xC0, loop.data(), sizeof(loop));
        if (title) {
            PutTitle(rom);
        }
        std::ofstream(path, std::ios::binary)
            .write(reinterpret_cast<const char*>(rom.data()),
                   static_cast<std::streamsize>(rom.size()));
        open = emu_open(&emu, path.string().c_str(), nullptr, true);
    }
    ~MadeGame() {
        if (open) {
            emu_close(&emu);
        }
        std::error_code ec;
        fs::remove_all(folder, ec);
    }
    MadeGame(const MadeGame&) = delete;
    MadeGame& operator=(const MadeGame&) = delete;

    /// A title screen the ROM draws by itself, in any core: on BG0 a box of 10x6 tiles at tile
    /// (5, 3), yellow, with a hole at its tile (3, 2) and its last one red.
    static void PutTitle(std::vector<uint8_t>& rom) {
        const std::array<uint32_t, 8> code{
            0xE28F0038, //       add r0, pc, #0x38 (the table, at 0x100)
            0xE4901004, // next: ldr r1, [r0], #4
            0xE3510000, //       cmp r1, #0
            0x0A000002, //       beq done
            0xE4902004, //       ldr r2, [r0], #4
            0xE1C120B0, //       strh r2, [r1]
            0xEAFFFFF9, //       b next
            0xEAFFFFFE, // done: b done
        };
        std::vector<uint32_t> table;
        const auto put = [&table](uint32_t address, uint32_t value) {
            table.push_back(address);
            table.push_back(value);
        };
        put(0x05000000, 0x2000);
        put(0x05000002, 0x039F); // (31, 28, 0)
        put(0x05000004, 0x109F); // (31, 4, 4)
        for (uint32_t k = 0; k < 32; k += 2) {
            put(0x06000020 + k, 0x1111);
            put(0x06000040 + k, 0x2222);
        }
        for (uint32_t y = 0; y < 6; ++y) {
            for (uint32_t x = 0; x < 10; ++x) {
                const bool hole = x == 3 && y == 2;
                const bool last = x == 9 && y == 5;
                put(0x06003800 + ((y + 3) * 32 + x + 5) * 2, hole ? 0 : last ? 2 : 1);
            }
        }
        put(0x04000008, 0x0700);
        put(0x04000000, 0x0100);
        table.push_back(0);
        std::memcpy(rom.data() + 0xC0, code.data(), sizeof(code));
        std::memcpy(rom.data() + 0x100, table.data(), table.size() * sizeof(uint32_t));
    }

    /// With a folder of the game's, for its logo.
    void MakeGameDir() {
        game_dir = (folder / "games").string();
        fs::create_directories(game_dir);
    }
    std::vector<std::string> Files() const {
        std::vector<std::string> files;
        for (const auto& entry : fs::recursive_directory_iterator(folder)) {
            if (!entry.is_directory()) {
                files.push_back(entry.path().lexically_relative(folder).generic_string());
            }
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    DriverServices Services() {
        DriverServices s;
        s.core = emu.core;
        s.emu = &emu;
        s.rom_path = path.string();
        s.game_dir = game_dir;
        s.step = [this](uint32_t keys) {
            ++steps;
            emu_frame(&emu, keys);
        };
        s.drain_audio = [this](int16_t* out, std::size_t max_frames) -> std::size_t {
            return emu_audio_read(&emu, out, max_frames);
        };
        s.rumble = [this] { return emu_rumble_level(&emu); };
        s.picture = [this] { return DriverPicture{emu.fb, GBA_W, GBA_H, GBA_W}; };
        return s;
    }
    std::unique_ptr<GbaDsDriver> Make() {
        return GbaDsDriver::Make(Services(), gbads_game_loop, GbaDsLimits{});
    }

    void Write16(uint32_t address, uint32_t value) {
        emu.core->busWrite16(emu.core, address, static_cast<uint16_t>(value));
    }
    void SolidTile(uint32_t base, uint32_t tile, uint32_t color) {
        for (uint32_t k = 0; k < 32; k += 2) {
            Write16(base + tile * 32 + k, color * 0x1111);
        }
    }

    /// Mario Kart's registers: BG0 is the HUD, with a yellow bar along the top.
    void DrawHud() {
        Write16(0x04000000, 0x1F41);
        Write16(0x04000008, 0x0700);
        Write16(0x0400000A, 0x1005);
        Write16(0x0400000C, 0x1106);
        Write16(0x0400000E, 0x1207);
        const std::array<std::array<uint32_t, 3>, 6> colors{
            {{0, 0, 8}, {31, 28, 0}, {31, 31, 31}, {4, 20, 4}, {8, 18, 31}, {31, 4, 4}}};
        for (uint32_t i = 0; i < colors.size(); ++i) {
            Write16(0x05000000 + i * 2,
                    colors[i][0] | (colors[i][1] << 5) | (colors[i][2] << 10));
        }
        for (uint32_t t = 1; t < 6; ++t) {
            SolidTile(0x06000000, t, t);
            SolidTile(0x06004000, t, t);
        }
        const auto tile = [this](uint32_t map, uint32_t x, uint32_t y, uint32_t t) {
            Write16(map + (y * 32 + x) * 2, t);
        };
        for (uint32_t y = 0; y < 20; ++y) {
            for (uint32_t x = 0; x < 30; ++x) {
                tile(0x06003800, x, y, 0);
                tile(0x06008000, x, y, (x + y) % 5 == 0 ? 3 : 4);
            }
        }
        for (uint32_t x = 1; x < 12; ++x) {
            tile(0x06003800, x, 0, 1);
        }
        for (uint32_t y = 16; y < 19; ++y) {
            for (uint32_t x = 1; x < 6; ++x) {
                tile(0x06003800, x, y, 5);
            }
        }
        for (uint32_t y = 12; y < 18; ++y) {
            for (uint32_t x = 23; x < 28; ++x) {
                tile(0x06003800, x, y, 2);
            }
        }
    }
    /// The scenery moves, as a game's does.
    DriverFrame RunMoving(FrameDriver& driver, int frames) {
        DriverFrame out;
        for (int i = 0; i < frames; ++i) {
            ++scroll;
            for (uint32_t bg = 1; bg < 4; ++bg) {
                Write16(0x04000010 + bg * 4, scroll);
            }
            out = Run(driver);
        }
        return out;
    }
    uint32_t scroll = 0;
};

constexpr uint32_t kBar = 0xFF00E7FF; // (31, 28, 0) as the core widens it

uint32_t At(const DriverPicture& picture, uint32_t x, uint32_t y) {
    return picture.pixels ? picture.pixels[y * picture.stride + x] : 0;
}

bool HudMoved(const DriverFrame& frame) {
    return frame.primary.pixels && frame.secondary.pixels && frame.secondary.width == 480 &&
           frame.secondary.height == 320 && At(frame.primary, 40, 3) != kBar &&
           At(frame.secondary, 80, 6) == kBar;
}

} // namespace

TEST_CASE("gba-ds's game.c: a HUD game", "[frame_cores]") {
    MadeGame game("AMKE");
    REQUIRE(game.open);
    const unsigned long opened = game.emu.frames;
    auto driver = game.Make();
    REQUIRE(driver);
    CHECK(game.steps == 0);
    CHECK(game.emu.frames == opened);
    CHECK(driver->SecondarySize() == std::pair<uint32_t, uint32_t>{480, 320});

    DriverFrame out;
    for (int i = 0; i < 10; ++i) {
        out = Run(*driver);
        REQUIRE(game.steps == i + 1);
        CHECK((i < 2 || out.audio_pcm.size() > 800));
    }
    CHECK(game.emu.frames == opened + 10);
    CHECK(out.secondary.pixels == nullptr); // no HUD up, and no logo

    game.DrawHud();
    out = game.RunMoving(*driver, 10);
    CHECK(HudMoved(out));
    CHECK(game.steps == 20);

    SECTION("a state, loaded as the core loads it") {
        std::vector<uint8_t> state(emu_state_size(&game.emu));
        REQUIRE(game.emu.core->saveState(game.emu.core, state.data()));
        game.Write16(0x04000000, 0);
        for (int i = 0; i < 30; ++i) {
            out = Run(*driver);
        }
        CHECK_FALSE(HudMoved(out));
        REQUIRE(game.emu.core->loadState(game.emu.core, state.data()));
        const unsigned long before = game.emu.frames;
        driver->AfterStateLoad();
        CHECK(game.emu.frames == before);
        CHECK(HudMoved(game.RunMoving(*driver, 4)));
        CHECK(game.steps == 54);
    }
    SECTION("reset") {
        emu_reset(&game.emu);
        driver->Reset();
        for (int i = 0; i < 30; ++i) {
            out = Run(*driver);
        }
        CHECK(game.steps == 50);
        CHECK(out.secondary.pixels == nullptr);
        game.DrawHud();
        CHECK(HudMoved(game.RunMoving(*driver, 10)));
    }
    SECTION("the game is drawn whole again after it") {
        driver.reset();
        for (int i = 0; i < 2; ++i) {
            ++game.scroll;
            for (uint32_t bg = 1; bg < 4; ++bg) {
                game.Write16(0x04000010 + bg * 4, game.scroll);
            }
            emu_frame(&game.emu, 0);
        }
        CHECK((game.emu.fb[3 * GBA_W + 40] | 0xFF000000u) == kBar);
    }
}

TEST_CASE("gba-ds's game.c: frames drawn again are not the game's", "[frame_cores]") {
    MadeGame game("ATHE");
    REQUIRE(game.open);
    auto driver = game.Make();
    REQUIRE(driver);
    for (int i = 0; i < 5; ++i) {
        Run(*driver);
    }
    // A picture with the goal sheet's eight probes in it
    game.Write16(0x04000000, 0x0403);
    struct Probe {
        int x, y;
        uint32_t color;
    };
    const std::array<Probe, 8> probes{{{16, 12, 0x392931},
                                       {52, 29, 0x7B7B7B},
                                       {5, 49, 0x294252},
                                       {214, 71, 0x943908},
                                       {7, 92, 0x29949C},
                                       {27, 112, 0x9C8410},
                                       {87, 130, 0xA5ADAD},
                                       {86, 146, 0xC6CED6}}};
    const auto plot = [&game](int x, int y, uint32_t color) {
        game.Write16(0x06000000 + static_cast<uint32_t>(y * GBA_W + x) * 2, color);
    };
    for (int y = 0; y < GBA_H; ++y) {
        for (int x = 0; x < GBA_W; ++x) {
            plot(x, y, static_cast<uint32_t>((x / 16) | (y / 16) << 5));
        }
    }
    for (const Probe& p : probes) {
        const uint32_t color = ((p.color >> 19) & 31) | (((p.color >> 11) & 31) << 5) |
                               (((p.color >> 3) & 31) << 10);
        for (int y = p.y - 3; y <= p.y + 3; ++y) {
            for (int x = std::max(0, p.x - 3); x <= p.x + 3; ++x) {
                plot(x, y, color);
            }
        }
    }
    const unsigned long before = game.emu.frames;
    std::size_t sound = 0;
    DriverFrame out;
    for (int i = 0; i < 8; ++i) {
        out = Run(*driver);
        sound += out.audio_pcm.size() / 2;
    }
    CHECK(game.steps == 13);
    CHECK(sound > 8 * 500);
    CHECK(sound < 8 * 600); // eight frames', at 32768 Hz
    REQUIRE(out.secondary.pixels);
    CHECK(out.secondary.width == 240);
    CHECK(out.secondary.height == 160);
    CHECK(game.emu.frames > before + 8); // the sheet's was drawn again without its sprites
}

TEST_CASE("gba-ds's game.c: one game after another", "[frame_cores]") {
    for (int i = 0; i < 2; ++i) {
        MadeGame hud("AMKE");
        REQUIRE(hud.open);
        {
            auto driver = hud.Make();
            REQUIRE(driver);
            Run(*driver);
            hud.DrawHud();
            CHECK(HudMoved(hud.RunMoving(*driver, 10)));
        }
        MadeGame none("ZZZE"); // no profile
        REQUIRE(none.open);
        CHECK_FALSE(none.Make());
        emu_frame(&none.emu, 0); // and the core is as it was
    }
    for (int i = 0; i < 2; ++i) {
        MadeGame map("B8KE");
        REQUIRE(map.open);
        auto driver = map.Make();
        REQUIRE(driver);
        CHECK(driver->SecondarySize().first > 0);
        for (int frame = 1; frame <= 30; ++frame) {
            Run(*driver);
            REQUIRE(map.steps == frame);
        }
    }
}

TEST_CASE("gba-ds's game.c: the table's two more cores", "[frame_cores]") {
    MadeGame game("BPPE");
    REQUIRE(game.open);
    auto driver = game.Make();
    REQUIRE(driver);
    for (int i = 1; i <= 10; ++i) {
        const DriverFrame out = Run(*driver);
        REQUIRE(game.steps == i);
        CHECK(out.primary.width == GBA_W);
        CHECK(out.primary.height == GBA_H);
    }
}

// ---------------------------------------------------------------- logo.c

namespace {

constexpr uint32_t kGrey = 0xFF303030;
constexpr uint32_t kBarOnGrey = 0xFF188B97; // kBar at 128 / 255 over kGrey

/// The queue has nothing of this game's any more.
bool LogoDone(const char* code) {
    for (int i = 0; i < 12000; ++i) {
        if (!gds_logo_busy(code)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}

/// The box of MadeGame's title screen, half as bright, in the middle of the GBA's grey screen.
bool HasLogo(const DriverPicture& picture) {
    return picture.pixels && picture.width == GBA_W && picture.height == GBA_H &&
           At(picture, 0, 0) == kGrey && At(picture, 79, 56) == kGrey &&
           At(picture, 80, 56) == kBarOnGrey && At(picture, 158, 56) == kBarOnGrey &&
           At(picture, 160, 56) == kGrey && At(picture, 80 + 28, 56 + 20) == kGrey &&
           At(picture, 80, 103) == kBarOnGrey && At(picture, 80, 104) == kGrey;
}

std::size_t Threads() {
    std::size_t n = 0;
    for (auto it = fs::directory_iterator("/proc/self/task"); it != fs::directory_iterator{};
         ++it) {
        ++n;
    }
    return n;
}

struct Pixels {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;
};

Pixels ReadLogo(const fs::path& path) {
    Pixels out;
    uint8_t* px = gds_logo_read(path.string().c_str(), &out.w, &out.h);
    if (!px) {
        return {};
    }
    out.rgba.assign(px, px + static_cast<std::size_t>(out.w) * out.h * 4);
    std::free(px);
    return out;
}

} // namespace

TEST_CASE("gba-ds's logo.c: a logo's file", "[frame_cores]") {
    const auto name = [](const std::string& dir, const char* code, uint32_t crc) {
        std::array<char, 1200> out{};
        const bool ok = gds_logo_path(dir.c_str(), code, crc, out.data(), out.size());
        CHECK(ok == (out[0] != 0));
        return std::string(out.data());
    };
    const auto rev = [](const char* code) {
        const MapProfile* p = profile_find(code);
        REQUIRE(p);
        return p->logo.rev;
    };
    REQUIRE(rev("AMKE") == 0);
    CHECK(name("/d/logo", "AMKE", 0x1234) == "/d/logo/AMKE.png");
    // Made again when its recipe changes
    REQUIRE(rev("AMTE") > 0);
    CHECK(name("/d", "AMTE", 0x1234) == "/d/AMTE-r" + std::to_string(rev("AMTE")) + ".png");
    // One for each ROM file where hacks share a code
    REQUIRE(profile_find("BPRE"));
    REQUIRE(profile_find("BPRE")->logoPerRom);
    CHECK(name("/d", "BPRE", 0x00A1B2C3) == "/d/BPRE-00A1B2C3.png");

    CHECK(name("/d", "ZZZE", 1).empty()); // no profile
    CHECK(name("", "AMKE", 1).empty());
    CHECK(name("/" + std::string(1010, 'd'), "AMKE", 1).size() == 1020);
    CHECK(name("/" + std::string(1020, 'd'), "AMKE", 1).empty()); // would be cut
    std::array<char, 8> small{};
    CHECK_FALSE(gds_logo_path("/d/logo", "AMKE", 1, small.data(), small.size()));
    CHECK_FALSE(gds_logo_path(nullptr, "AMKE", 1, small.data(), small.size()));
}

TEST_CASE("gba-ds's logo.c: a picture, written and read", "[frame_cores]") {
    MadeGame game("ZZZE");
    const fs::path file = game.folder / "a.png";
    const std::string path = file.string();
    const auto draw = [](int w, int h) {
        Pixels p{w, h, {}};
        for (int i = 0; i < w * h; ++i) {
            const auto value = static_cast<uint8_t>(i * 7 + i / w);
            p.rgba.insert(p.rgba.end(), {value, static_cast<uint8_t>(value ^ 0x5A),
                                         static_cast<uint8_t>(i),
                                         static_cast<uint8_t>(i % 3 ? 255 : 0)});
        }
        return p;
    };
    const std::array<std::array<int, 2>, 3> sizes{{{GBA_W, GBA_H}, {1, 1}, {113, 47}}};
    for (const auto& size : sizes) {
        const Pixels drawn = draw(size[0], size[1]);
        REQUIRE(gds_logo_write(path.c_str(), drawn.rgba.data(), drawn.w, drawn.h));
        const Pixels read = ReadLogo(file);
        CHECK(read.w == drawn.w);
        CHECK(read.h == drawn.h);
        CHECK(read.rgba == drawn.rgba);
    }
    CHECK(game.Files() == std::vector<std::string>{"a.png", "game.gba"}); // no .tmp

    // What is no logo of ours is not read, and not put in place
    const fs::path none = game.folder / "b.png";
    CHECK_FALSE(gds_logo_write(none.string().c_str(), draw(GBA_W + 1, 8).rgba.data(), GBA_W + 1,
                               8));
    CHECK_FALSE(gds_logo_write((game.folder / "no" / "b.png").string().c_str(),
                               draw(8, 8).rgba.data(), 8, 8));
    CHECK(ReadLogo(none).rgba.empty());
    CHECK(ReadLogo(game.path).rgba.empty());
    CHECK(game.Files() == std::vector<std::string>{"a.png", "game.gba"});
    // cut short
    fs::resize_file(file, fs::file_size(file) / 2);
    CHECK(ReadLogo(file).rgba.empty());
}

TEST_CASE("gba-ds's logo.c: a title screen's logo", "[frame_cores]") {
    MadeGame game("AMKE", true);
    REQUIRE(game.open);
    game.MakeGameDir();
    const unsigned long opened = game.emu.frames;
    const uint32_t counted = game.emu.core->frameCounter(game.emu.core);
    const std::vector<uint32_t> fb(game.emu.fb, game.emu.fb + kPixels);
    emu_audio_read(&game.emu, game.scratch.data(), game.scratch.size() / 2);

    auto driver = game.Make();
    REQUIRE(driver);
    bool playing = false;
    SECTION("made while the game waits") {}
    SECTION("made beside the game") {
        playing = true;
    }
    if (playing) {
        while (gds_logo_busy("AMKE")) {
            const int before = game.steps;
            const DriverFrame out = Run(*driver);
            REQUIRE(game.steps == before + 1);
            REQUIRE(out.audio_pcm.size() / 2 < 1100);
            REQUIRE(game.steps < 1000000);
        }
        CHECK(game.emu.frames == opened + static_cast<unsigned long>(game.steps));
    } else {
        REQUIRE(LogoDone("AMKE"));
        CHECK(game.steps == 0);
        CHECK(game.emu.frames == opened);
        CHECK(game.emu.core->frameCounter(game.emu.core) == counted);
        CHECK(std::equal(fb.begin(), fb.end(), game.emu.fb));
        CHECK(emu_audio_read(&game.emu, game.scratch.data(), game.scratch.size() / 2) == 0);
    }

    // As the ROM drew it, and nothing else on the disk
    const std::vector<std::string> files{"game.gba", "games/logo/AMKE.png"};
    REQUIRE(game.Files() == files);
    const Pixels logo = ReadLogo(game.folder / "games/logo/AMKE.png");
    REQUIRE(logo.w == 80);
    REQUIRE(logo.h == 48);
    const auto pixel = [&logo](std::size_t x, std::size_t y) {
        const uint8_t* p = logo.rgba.data() + (y * 80 + x) * 4;
        return std::array<int, 4>{p[0], p[1], p[2], p[3]};
    };
    CHECK(pixel(0, 0) == std::array<int, 4>{0xFF, 0xE7, 0x00, 0xFF});
    CHECK(pixel(79, 47) == std::array<int, 4>{0xFF, 0x21, 0x21, 0xFF});
    CHECK(pixel(24, 16)[3] == 0);
    CHECK(pixel(31, 23)[3] == 0);
    CHECK(pixel(32, 23)[3] == 0xFF);

    // On the Secondary Screen from the next frame, and at once the next time
    Run(*driver);
    CHECK(HasLogo(Run(*driver).secondary));
    CHECK(driver->SecondarySize() == std::pair<uint32_t, uint32_t>{GBA_W, GBA_H});
    driver.reset();
    const uint32_t generation = gds_logo_generation();
    driver = game.Make();
    REQUIRE(driver);
    CHECK_FALSE(gds_logo_busy("AMKE"));
    CHECK(HasLogo(Run(*driver).secondary));
    driver.reset();
    CHECK(gds_logo_generation() == generation);
    CHECK(game.Files() == files);
}

TEST_CASE("gba-ds's logo.c: no logo to be found", "[frame_cores]") {
    MadeGame game("AMKE"); // draws nothing
    REQUIRE(game.open);
    game.MakeGameDir();
    uint32_t generation = gds_logo_generation();
    {
        auto driver = game.Make();
        REQUIRE(driver);
        REQUIRE(LogoDone("AMKE"));
        CHECK(gds_logo_generation() == generation + 1);
        // Not tried again in each frame
        for (int i = 1; i <= 30; ++i) {
            CHECK(Run(*driver).secondary.pixels == nullptr); // as without logos
            REQUIRE(game.steps == i);
        }
        CHECK_FALSE(gds_logo_busy("AMKE"));
    }
    // Nor when the game is opened again
    generation = gds_logo_generation();
    auto driver = game.Make();
    REQUIRE(driver);
    CHECK_FALSE(gds_logo_busy("AMKE"));
    Run(*driver);
    CHECK(gds_logo_generation() == generation);
    CHECK(game.Files() == std::vector<std::string>{"game.gba"});
}

TEST_CASE("gba-ds's logo.c: a game closed meanwhile", "[frame_cores]") {
    {
        MadeGame warm("AMKE"); // (threads the first driver of the process may leave)
        REQUIRE(warm.open);
        REQUIRE(warm.Make());
    }
    MadeGame game("AMKE", true);
    REQUIRE(game.open);
    game.MakeGameDir();
    const std::size_t threads = Threads();
    for (const int frames : {0, 1, 5}) {
        auto driver = game.Make();
        REQUIRE(driver);
        for (int i = 0; i < frames; ++i) {
            Run(*driver);
        }
        driver.reset();
        CHECK_FALSE(gds_logo_busy(nullptr));
        // (a joined thread may be listed for a moment)
        for (int i = 0; i < 100 && Threads() > threads; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        CHECK(Threads() <= threads);
    }
    // Another game, right after one closed
    game.Make();
    MadeGame other("AMKE", true);
    REQUIRE(other.open);
    other.MakeGameDir();
    {
        auto driver = other.Make();
        REQUIRE(driver);
        REQUIRE(LogoDone("AMKE"));
        Run(*driver);
        CHECK(HasLogo(Run(*driver).secondary));
    }
    // What was cut short is not what failed: asked for again
    auto driver = game.Make();
    REQUIRE(driver);
    REQUIRE(LogoDone("AMKE"));
    CHECK(game.Files() == std::vector<std::string>{"game.gba", "games/logo/AMKE.png"});
}
