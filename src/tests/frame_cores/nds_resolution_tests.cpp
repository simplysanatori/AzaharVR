// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "Args.h"
#include "GPU.h"
#include "GPU_Soft.h"
#include "NDS.h"

#include "core/autosave.h"
#include "core/frontend/alt_screens.h"
#include "frame_cores/alt_core_host.h"
#include "frame_cores/nds_resolution.h"
#include "video_core/renderer_vulkan/alt_present.h"
#include "video_core/vr_dynamic_resolution.h"

using FrameCores::NdsHires;
using FrameCores::NdsHiresRenderer;
using FrameCores::NdsResolution;
using Vulkan::AltScreenImage;
using Vulkan::AltTargetEven;
using Vulkan::AltTargetSize;

namespace {

namespace fs = std::filesystem;

struct Log {
    int made = 0;
    int gone = 0;
    std::vector<uint32_t> scales;
    bool can_make = true;
    bool can_init = true;
    uint32_t too_big = 99;
    bool works = true;
};

/// melonDS's software renderer, its picture blown up
class Blown : public melonDS::Renderer, public NdsHires {
public:
    Blown(melonDS::NDS& nds, Log& log)
        : melonDS::Renderer(nds.GPU), log{log}, soft{std::make_unique<melonDS::SoftRenderer>(nds)} {
        log.made++;
    }
    ~Blown() override {
        log.gone++;
    }
    bool Init() override {
        return log.can_init && soft->Init();
    }
    void Reset() override {
        soft->Reset();
    }
    void Stop() override {
        soft->Stop();
    }
    void SetRenderSettings(melonDS::RendererSettings& settings) override {
        soft->SetRenderSettings(settings);
    }
    void DrawScanline(melonDS::u32 line) override {
        soft->DrawScanline(line);
    }
    void DrawSprites(melonDS::u32 line) override {
        soft->DrawSprites(line);
    }
    void Start3DRendering() override {
        soft->Start3DRendering();
    }
    void Finish3DRendering() override {
        soft->Finish3DRendering();
    }
    void Restart3DRendering() override {
        soft->Restart3DRendering();
    }
    void VBlank() override {
        soft->VBlank();
    }
    void VBlankEnd() override {
        soft->VBlankEnd();
    }
    void AllocCapture(melonDS::u32, melonDS::u32, melonDS::u32) override {}
    void SyncVRAMCapture(melonDS::u32, melonDS::u32, melonDS::u32, bool) override {}
    bool GetFramebuffers(void**, void**) override {
        return false;
    }
    void SwapBuffers() override {
        soft->SwapBuffers();
    }

    bool SetScale(uint32_t to) override {
        log.scales.push_back(to);
        scale = to;
        return to < log.too_big;
    }
    bool GetScreens(AltScreenImage& top, AltScreenImage& bottom) override {
        std::array<void*, 2> from{};
        if (!log.works || !soft->GetFramebuffers(&from[0], &from[1])) {
            return false;
        }
        for (std::size_t i = 0; i < 2; i++) {
            const uint32_t w = 256 * scale;
            const uint32_t h = 192 * scale;
            pixels[i].resize(std::size_t{w} * h);
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    pixels[i][std::size_t{y} * w + x] =
                        static_cast<uint32_t*>(from[i])[y / scale * 256 + x / scale];
                }
            }
            AltScreenImage& image = i == 0 ? top : bottom;
            image = {};
            image.pixels = pixels[i].data();
            image.width = w;
            image.height = h;
            image.stride_pixels = w;
            image.bgra = true;
        }
        return true;
    }

private:
    Log& log;
    std::unique_ptr<melonDS::SoftRenderer> soft;
    uint32_t scale = 1;
    std::array<std::vector<uint32_t>, 2> pixels;
};

FrameCores::NdsHiresMaker Maker(Log& log) {
    return [&log](melonDS::NDS& nds, std::string& why) -> std::unique_ptr<NdsHiresRenderer> {
        if (!log.can_make) {
            why = "no graphics here";
            return nullptr;
        }
        auto renderer = std::make_unique<Blown>(nds, log);
        auto made = std::make_unique<NdsHiresRenderer>();
        made->control = renderer.get();
        made->renderer = std::move(renderer);
        return made;
    };
}

/// A console whose picture is drawn by its own program: none here, so a plain backdrop.
std::unique_ptr<melonDS::NDS> Console() {
    auto nds = std::make_unique<melonDS::NDS>(melonDS::NDSArgs{}, nullptr);
    nds->Reset();
    return nds;
}

bool IsSoftware(melonDS::NDS& nds) {
    void* top = nullptr;
    void* bottom = nullptr;
    return nds.GPU.GetFramebuffers(&top, &bottom);
}

void Put32(std::vector<uint8_t>& rom, std::size_t at, uint32_t value) {
    std::memcpy(rom.data() + at, &value, 4);
}

/// Shows VRAM A, where the ARM9 counts down 16-bit colours.
std::vector<uint8_t> CountingRom() {
    std::vector<uint8_t> rom(0x10000, 0); // its code after the secure area
    std::memcpy(rom.data(), "RESTEST", 7);
    std::memcpy(rom.data() + 0x0C, "HRTE01", 6);
    const std::array<uint32_t, 14> header{0x8000,     0x02000000, 0x02000000, 0x100, 0x8200,
                                          0x02380000, 0x02380000, 0x100,      0,     0,
                                          0,          0,          0,          0};
    for (std::size_t i = 0; i < header.size(); i++) {
        Put32(rom, 0x20 + i * 4, header[i]);
    }
    Put32(rom, 0x80, 0x10000);
    Put32(rom, 0x84, 0x4000);
    const std::array<uint32_t, 11> arm9{
        0xE3A00301, // mov r0, #0x04000000
        0xE3A01802, // mov r1, #0x00020000
        0xE5801000, // str r1, [r0]
        0xE3A01080, // mov r1, #0x80
        0xE5C01240, // strb r1, [r0, #0x240]
        0xE3A02668, // mov r2, #0x06800000
        0xE3A04903, // mov r4, #0xC000
        0xE0C240B2, // strh r4, [r2], #2
        0xE2544001, // subs r4, r4, #1
        0x1AFFFFFC, // bne -16
        0xEAFFFFFE, // b .
    };
    for (std::size_t i = 0; i < arm9.size(); i++) {
        Put32(rom, 0x8000 + i * 4, arm9[i]);
    }
    Put32(rom, 0x8200, 0xEAFFFFFE);
    uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < 0x15E; i++) {
        crc ^= rom[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1) != 0 ? static_cast<uint16_t>((crc >> 1) ^ 0xA001)
                                 : static_cast<uint16_t>(crc >> 1);
        }
    }
    std::memcpy(rom.data() + 0x15E, &crc, 2);
    return rom;
}

/// The Primary Screen in the console's own pixels
std::vector<uint32_t> Small(const AltScreenImage& image) {
    std::vector<uint32_t> out;
    for (uint32_t y = 0; y < image.height; y += image.scale) {
        for (uint32_t x = 0; x < image.width; x += image.scale) {
            out.push_back(image.pixels[std::size_t{y} * image.stride_pixels + x]);
        }
    }
    return out;
}

struct Dir {
    fs::path path = fs::temp_directory_path() /
                    ("azaharvr_res_" +
                     std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    Dir() {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~Dir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

} // namespace

TEST_CASE("DS resolution: 1x is the software renderer", "[frame_cores]") {
    Log log;
    auto nds = Console();
    NdsResolution resolution{Maker(log)};
    CHECK(resolution.Available());
    for (int i = 0; i < 3; i++) {
        resolution.SetFactor(1, 1);
        resolution.Apply(*nds, false);
    }
    CHECK(log.made == 0);
    CHECK(resolution.Scale() == 1);
    AltScreenImage top, bottom;
    CHECK_FALSE(resolution.GetScreens(top, bottom));
    CHECK(top.pixels == nullptr);
}

TEST_CASE("DS resolution: the factor changes as the game runs", "[frame_cores]") {
    for (const bool threaded : {false, true}) {
        Log log;
        auto nds = Console();
        NdsResolution resolution{Maker(log)};
        AltScreenImage top, bottom;

        resolution.SetFactor(4, 6);
        CHECK(resolution.Scale() == 1); // not before the next frame
        resolution.Apply(*nds, threaded);
        CHECK(resolution.Scale() == 4);
        REQUIRE(resolution.GetScreens(top, bottom));
        CHECK(top.width == 1024);
        CHECK(bottom.height == 768);
        CHECK(top.scale == 4);
        CHECK(top.max_scale == 6);
        CHECK(bottom.max_scale == 6);

        resolution.Apply(*nds, threaded); // the same: nothing to do
        resolution.SetFactor(3, 6);
        resolution.Apply(*nds, threaded);
        CHECK(log.scales == std::vector<uint32_t>{4, 3});
        CHECK(log.made == 1); // the same renderer
        REQUIRE(resolution.GetScreens(top, bottom));
        CHECK(top.width == 768);

        resolution.SetFactor(1, 6);
        resolution.Apply(*nds, threaded);
        CHECK(log.gone == 1);
        CHECK(IsSoftware(*nds));
        CHECK_FALSE(resolution.GetScreens(top, bottom));
        CHECK(resolution.Ceiling() == 6);

        resolution.SetFactor(50, 1); // no more than there is, and no ceiling under the factor
        resolution.Apply(*nds, threaded);
        CHECK(resolution.Scale() == FrameCores::kMaxNdsScale);
        CHECK(resolution.Ceiling() == FrameCores::kMaxNdsScale);
        CHECK(log.made == 2);

        nds.reset();
        resolution.Detach();
        CHECK(log.gone == 2);
        CHECK(resolution.Scale() == 1);
        CHECK_FALSE(resolution.GetScreens(top, bottom));
    }
}

TEST_CASE("DS resolution: what fails leaves the game at 1x", "[frame_cores]") {
    AltScreenImage top, bottom;
    SECTION("no renderer in the build") {
        auto nds = Console();
        NdsResolution resolution;
        CHECK_FALSE(resolution.Available());
        resolution.SetFactor(4, 4);
        resolution.Apply(*nds, true);
        CHECK(resolution.Scale() == 1);
        CHECK(resolution.Ceiling() == 1);
        CHECK(IsSoftware(*nds));
    }
    SECTION("not made, and not asked again") {
        Log log;
        log.can_make = false;
        auto nds = Console();
        NdsResolution resolution{Maker(log)};
        resolution.SetFactor(4, 4);
        resolution.Apply(*nds, true);
        CHECK_FALSE(resolution.Available());
        log.can_make = true;
        resolution.Apply(*nds, true);
        CHECK(log.made == 0);
        CHECK(resolution.Scale() == 1);
        CHECK(IsSoftware(*nds));
    }
    SECTION("does not start") {
        Log log;
        log.can_init = false;
        auto nds = Console();
        NdsResolution resolution{Maker(log)};
        resolution.SetFactor(2, 2);
        resolution.Apply(*nds, true);
        resolution.Apply(*nds, true);
        CHECK_FALSE(resolution.Available());
        CHECK(log.made == 1);
        CHECK(log.gone == 1);
        CHECK(IsSoftware(*nds));
        CHECK_FALSE(resolution.GetScreens(top, bottom));
    }
    SECTION("cannot draw that big") {
        Log log;
        log.too_big = 6;
        auto nds = Console();
        NdsResolution resolution{Maker(log)};
        resolution.SetFactor(4, 4);
        resolution.Apply(*nds, true);
        CHECK(resolution.Scale() == 4);
        resolution.SetFactor(6, 6);
        resolution.Apply(*nds, true);
        CHECK(resolution.Scale() == 1);
        CHECK_FALSE(resolution.Available());
        CHECK(log.gone == 1);
        CHECK(IsSoftware(*nds));
    }
    SECTION("stops working") {
        Log log;
        auto nds = Console();
        NdsResolution resolution{Maker(log)};
        resolution.SetFactor(2, 2);
        resolution.Apply(*nds, true);
        REQUIRE(resolution.GetScreens(top, bottom));
        log.works = false;
        CHECK_FALSE(resolution.GetScreens(top, bottom));
        CHECK_FALSE(resolution.Available());
        resolution.Apply(*nds, true);
        CHECK(log.gone == 1);
        CHECK(IsSoftware(*nds));
    }
}

TEST_CASE("Alternate consoles' XR swapchains", "[frame_cores]") {
    using Size = std::array<u32, 2>;
    // A console's own pixels, as before
    CHECK(AltTargetSize(256, 192, 1, 1) == Size{1024, 768});
    CHECK(AltTargetSize(240, 160, 1, 1) == Size{960, 640});
    CHECK(AltTargetSize(160, 144, 1, 1) == Size{640, 576});
    CHECK(AltTargetSize(480, 320, 1, 1) == Size{1440, 960});
    CHECK(AltTargetSize(2000, 1200, 1, 1) == Size{2000, 1200});
    CHECK(AltTargetSize(0, 0, 1, 1) == Size{4, 4});
    for (u32 max = 2; max <= 8; max++) {
        const Size size = AltTargetSize(256 * max, 192 * max, max, max);
        CAPTURE(max);
        CHECK(size[0] >= 1024);
        CHECK(AltTargetEven(256 * max, 192 * max, size[0], size[1]));
        for (u32 scale = 1; scale <= max; scale++) { // Dynamic Resolution remakes nothing
            CHECK(AltTargetSize(256 * scale, 192 * scale, scale, max) == size);
        }
    }
    CHECK(AltTargetSize(1024, 768, 4, 4) == Size{1024, 768});
    CHECK(AltTargetSize(2048, 1536, 8, 8) == Size{2048, 1536});
    CHECK_FALSE(AltTargetEven(768, 576, 1024, 768));
    CHECK_FALSE(AltTargetEven(0, 0, 1024, 768));
}

TEST_CASE("Dynamic Resolution on a DS game's factor", "[frame_cores]") {
    namespace Dr = VideoCore::DynamicResolution;
    const u32 drop = Dr::g_drop.exchange(2);
    CHECK(Dr::Apply(4) == 2);
    CHECK(Dr::Apply(1) == 1);
    // On: from the setting up to two steps over it, at 1x nothing
    CHECK(Dr::AltBase(1, 0, true) == 1);
    CHECK(Dr::AltBase(2, 0, true) == 4);
    CHECK(Dr::AltBase(6, 0, true) == 8);
    CHECK(Dr::AltBase(7, 0, true) == 8);
    CHECK(Dr::AltBase(8, 0, true) == 8);
    CHECK(Dr::AltBase(10, 0, true) == 10);
    CHECK(Dr::Apply(Dr::AltBase(4, 0, true)) == 4); // two steps off
    CHECK(Dr::AltBase(4, 0, false) == 4);
    CHECK(Dr::AltBase(0, 3, true) == 3); // Auto
    CHECK(Dr::AltBase(0, 0, false) == 1);
    Dr::g_drop.store(0);
    CHECK(Dr::Apply(4) == 4);
    Dr::g_drop.store(drop);
}

TEST_CASE("DS resolution: a game's setting, its states and its sizes", "[frame_cores]") {
    namespace A = Core::Autosave;
    Dir tmp;
    const fs::path rom = tmp.path / "res.nds";
    const auto bytes = CountingRom();
    std::ofstream(rom, std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));

    Log log;
    FrameCores::HostResolution setting{4, 4};
    std::vector<Layout::AltScreens> sizes;
    FrameCores::HostFrontend frontend;
    frontend.resolution = [&] { return setting; };
    frontend.on_screens = [&](const Layout::AltScreens& screens) { sizes.push_back(screens); };
    FrameCores::AltCoreHost host;
    host.SetFrontend(std::move(frontend));
    FrameCores::CoreBootOptions opts{};
    opts.ds_renderer = Maker(log);

    const bool dsi = GENERATE(false, true);
    opts.force_dsi = dsi;
    const std::string data = (tmp.path / "data").string();
    REQUIRE(host.Boot(rom.string(), data, /*headless_for_test=*/true, opts));
    CHECK(host.GetCapabilities().has_internal_resolution);
    CHECK(host.GetCapabilities().has_dynamic_resolution);

    setting = {1, 1};
    REQUIRE(host.StepFrames(20));
    const AltScreenImage& picture = host.LastFrame().primary;
    const AltScreenImage& other = host.LastFrame().secondary;
    CHECK(picture.width == 256);
    CHECK(picture.scale == 1);
    CHECK(picture.max_scale == 1);
    const auto small = Small(picture);
    CHECK(*std::max_element(small.begin(), small.end()) !=
          *std::min_element(small.begin(), small.end()));
    const auto small_other = Small(other);

    // From the next frame on, the same picture
    for (const uint32_t factor : {4u, 2u, 1u, 8u, 1u, 3u}) {
        setting = {factor, 8};
        REQUIRE(host.StepFrames(1));
        CAPTURE(factor);
        CHECK(picture.width == 256 * factor);
        CHECK(other.height == 192 * factor);
        CHECK(picture.scale == factor);
        CHECK(picture.max_scale == 8);
        CHECK(Small(picture) == small);
        CHECK(Small(other) == small_other);
    }
    // The window, and with it the touch screen, stays in the console's own pixels
    for (const auto& screens : sizes) {
        CHECK(screens.top_width == 256);
        CHECK(screens.bottom_width == 256);
        CHECK(screens.bottom_height == 192);
    }
    // Layer Depth: both eyes at 1x, no row; and back, whichever changes first
    const auto right = [&host] { return host.LastFrame().primary_right.HasPicture(); };
    host.SetLayerDepth(2);
    REQUIRE(host.StepFrames(2));
    CHECK(picture.scale == 1);
    CHECK(picture.max_scale == 1);
    CHECK(Small(picture) == small);
    CHECK(right());
    CHECK_FALSE(host.GetCapabilities().has_internal_resolution);
    CHECK_FALSE(host.GetCapabilities().has_dynamic_resolution);
    setting = {2, 8};
    REQUIRE(host.StepFrames(2));
    CHECK(picture.scale == 1);
    host.SetLayerDepth(0);
    REQUIRE(host.StepFrames(3));
    CHECK(picture.scale == 2);
    CHECK_FALSE(right());
    CHECK(Small(picture) == small);
    CHECK(host.GetCapabilities().has_internal_resolution);
    setting = {1, 1};
    REQUIRE(host.StepFrames(2));
    host.SetLayerDepth(2);
    REQUIRE(host.StepFrames(2));
    CHECK(right());
    host.SetLayerDepth(0);
    setting = {3, 8};
    REQUIRE(host.StepFrames(3));
    CHECK(picture.scale == 3);
    CHECK_FALSE(right());
    CHECK(Small(picture) == small);

    // States, Restart and the other console keep the factor
    REQUIRE(host.MakeAutosave(A::Reason::Timer, 2));
    REQUIRE(host.SaveSlot(2));
    REQUIRE(host.StepFrames(2));
    REQUIRE(host.LoadSlot(2));
    REQUIRE(host.StepFrames(4));
    CHECK(picture.scale == 3);
    CHECK(Small(picture) == small);
    host.Reset();
    REQUIRE(host.StepFrames(20));
    CHECK(picture.scale == 3);
    CHECK(Small(picture) == small);
    host.SetConsoleMode(!dsi);
    REQUIRE(host.StepFrames(20));
    CHECK(picture.scale == 3);

    // It stops working: the row goes, the game stays
    log.works = false;
    REQUIRE(host.StepFrames(2));
    CHECK(picture.scale == 1);
    CHECK(picture.width == 256);
    CHECK(picture.max_scale == 1);
    CHECK_FALSE(host.GetCapabilities().has_internal_resolution);
    CHECK_FALSE(host.GetCapabilities().has_dynamic_resolution);
    host.Stop();
    CHECK(log.made == log.gone);

    // No graphics to draw with
    setting = {4, 4};
    REQUIRE(host.Boot(rom.string(), data, /*headless_for_test=*/true, {}));
    CHECK_FALSE(host.GetCapabilities().has_internal_resolution);
    REQUIRE(host.StepFrames(2));
    CHECK(host.LastFrame().primary.scale == 1);
    host.Stop();
}
