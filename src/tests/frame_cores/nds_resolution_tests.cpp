// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
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

#include "common/layer_depth.h"
#include "core/autosave.h"
#include "core/frontend/alt_screens.h"
#include "frame_cores/alt_core_host.h"
#include "frame_cores/ds_gl/ds_gl_state.h"
#include "frame_cores/nds_core.h"
#include "frame_cores/nds_gl_edge.h"
#include "frame_cores/nds_resolution.h"
#include "frame_cores/nds_stereo_3d.h"
#include "video_core/renderer_vulkan/alt_present.h"
#include "video_core/vr_dynamic_resolution.h"

namespace {
#include "OpenGL_shaders/2DCompositorFS.h"
#include "OpenGL_shaders/2DCompositorVS.h"
#include "OpenGL_shaders/2DSpriteFS.h"
#include "OpenGL_shaders/3DClearBitmapFS.h"
#include "OpenGL_shaders/3DClearBitmapVS.h"
#include "OpenGL_shaders/3DClearFS.h"
#include "OpenGL_shaders/3DClearVS.h"
#include "OpenGL_shaders/3DFinalPassFogFS.h"
#include "OpenGL_shaders/3DRenderFS.h"
#include "OpenGL_shaders/FinalPassFS.h"
} // namespace

using FrameCores::NdsHires;
using FrameCores::NdsHiresRenderer;
using FrameCores::NdsResolution;
using Vulkan::AltScreenImage;
using Vulkan::AltTargetEven;
using Vulkan::AltTargetSize;

namespace {

namespace fs = std::filesystem;

struct InvertDepth {
    InvertDepth() {
        Common::LayerDepth::Get().inverted = true;
    }
    ~InvertDepth() {
        Common::LayerDepth::Get().inverted = false;
    }
};

struct SwapScreens {
    SwapScreens() {
        Common::LayerDepth::Get().swapped = true;
    }
    ~SwapScreens() {
        Common::LayerDepth::Get().swapped = false;
    }
};

struct Log {
    int made = 0;
    int gone = 0;
    std::vector<uint32_t> scales;
    bool can_make = true;
    bool can_init = true;
    uint32_t too_big = 99;
    bool works = true;
    bool second_eye = false;        ///< it can draw one
    uint32_t too_big_for_both = 99; ///< with the second eye
    std::vector<int> depths;        ///< the farthest layer's shift, at each change
    FrameCores::NdsDepth::Screens screens{};
    bool bottom_has_depth = false;
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
    bool SetDepth(const FrameCores::NdsDepth::Depth& to) override {
        if (depth != to) {
            log.depths.push_back(to.layers[3]);
        }
        depth = to;
        log.screens = to.screens;
        return !depth.on || (log.second_eye && scale < log.too_big_for_both);
    }
    bool ShowsDepth(int screen) const override {
        return screen == 0 || log.bottom_has_depth;
    }
    int EyesShift() const override {
        return depth.layers[3];
    }
    bool GetEyes(int screen, AltScreenImage& left, AltScreenImage& right) override {
        const auto& from = pixels[static_cast<std::size_t>(screen)];
        if (!depth.on || from.empty()) {
            return false;
        }
        left.pixels = from.data();
        right.pixels = from.data();
        return true;
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
    FrameCores::NdsDepth::Depth depth{};
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

TEST_CASE("DS resolution: Layer Depth over 1x", "[frame_cores]") {
    const auto depth = FrameCores::NdsDepth::MakeDepth(2, 1.0f);
    const auto inverted = FrameCores::NdsDepth::MakeDepth(2, 1.0f, true);
    Log log;
    log.second_eye = true;
    auto nds = Console();
    NdsResolution resolution{Maker(log)};
    AltScreenImage top, bottom, left, right;
    const auto has_eyes = [&] {
        REQUIRE(resolution.GetScreens(top, bottom));
        left = top;
        right = top;
        return resolution.GetEyes(0, left, right);
    };
    SECTION("both change as the game runs") {
        resolution.SetFactor(4, 8);
        resolution.Apply(*nds, true, depth);
        CHECK(resolution.Scale() == 4);
        CHECK(has_eyes());
        for (const AltScreenImage* eye : {&left, &right}) {
            CHECK(eye->scale == 4);
            CHECK(eye->max_scale == 8);
        }
        resolution.Apply(*nds, true, inverted);
        resolution.Apply(*nds, true, inverted);
        resolution.Apply(*nds, true);
        CHECK_FALSE(has_eyes());
        resolution.SetFactor(2, 8);
        resolution.Apply(*nds, true, depth);
        CHECK(has_eyes());
        CHECK(right.scale == 2);
        CHECK(log.depths == std::vector<int>{6, -6, 0, 6});
        CHECK(log.made == 1);
        resolution.SetFactor(1, 8);
        resolution.Apply(*nds, true, depth);
        CHECK(resolution.Scale() == 1);
        CHECK(resolution.Ceiling() == 8);
        CHECK(IsSoftware(*nds));
        CHECK_FALSE(resolution.GetEyes(0, left, right));
        resolution.SetFactor(3, 8);
        resolution.Apply(*nds, true, depth);
        CHECK(has_eyes());
        CHECK_FALSE(resolution.DepthIsFlat());
    }
    SECTION("no second eye, at any time: Layer Depth at 1x, the rest as before") {
        const bool live = GENERATE(false, true);
        resolution.SetFactor(4, 8);
        if (live) {
            resolution.Apply(*nds, true);
            CHECK(resolution.Scale() == 4);
        }
        log.second_eye = false;
        resolution.Apply(*nds, true, depth);
        CHECK(resolution.Scale() == 1);
        CHECK(resolution.Ceiling() == 1);
        CHECK(resolution.DepthIsFlat());
        CHECK(resolution.Available());
        CHECK(IsSoftware(*nds));
        log.second_eye = true; // not asked again
        resolution.Apply(*nds, true, depth);
        CHECK(resolution.Scale() == 1);
        CHECK(log.made == 1);
        resolution.Apply(*nds, true);
        CHECK(resolution.Scale() == 4);
        CHECK(resolution.Ceiling() == 8);
        resolution.Apply(*nds, true, depth);
        CHECK(resolution.Scale() == 1);
    }
    SECTION("too big for both eyes") {
        log.too_big_for_both = 6;
        resolution.SetFactor(4, 8);
        resolution.Apply(*nds, true, depth);
        CHECK(has_eyes());
        resolution.SetFactor(8, 8);
        resolution.Apply(*nds, true, depth);
        CHECK(resolution.Scale() == 1);
        CHECK(resolution.DepthIsFlat());
        resolution.Apply(*nds, true);
        CHECK(resolution.Scale() == 8);
    }
    SECTION("stops working with both eyes") {
        resolution.SetFactor(4, 4);
        resolution.Apply(*nds, true, depth);
        log.works = false;
        CHECK_FALSE(resolution.GetScreens(top, bottom));
        CHECK(resolution.DepthIsFlat());
        resolution.Apply(*nds, true, depth);
        CHECK(IsSoftware(*nds));
        log.works = true;
        resolution.Apply(*nds, true);
        CHECK(resolution.Scale() == 4);
        log.works = false; // and again without
        CHECK_FALSE(resolution.GetScreens(top, bottom));
        CHECK_FALSE(resolution.Available());
        resolution.Apply(*nds, true);
        CHECK(IsSoftware(*nds));
    }
    nds.reset();
    resolution.Detach();
    CHECK(log.made == log.gone);
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

// The submodule's: a melonDS whose shaders are written otherwise needs another nds_gl_edge.h
TEST_CASE("DS resolution: melonDS's shaders are those the eyes' edges are put in",
          "[frame_cores]") {
    namespace Edge = FrameCores::NdsGlEdge;
    struct Pair {
        const char* vertex;
        const char* fragment;
        int edges; ///< mentions of uEdge
    };
    const Pair pair =
        GENERATE(Pair{k2DCompositorVS, k2DCompositorFS, 4}, Pair{k3DClearVS, k3DClearFS, 4},
                 Pair{k3DClearBitmapVS, k3DClearBitmapFS, 4});
    const auto mentions = [](const std::string& text) {
        int count = 0;
        for (std::size_t at = text.find("uEdge"); at != std::string::npos;
             at = text.find("uEdge", at + 1)) {
            count++;
        }
        return count;
    };
    std::string log;
    std::string fragment = pair.fragment;
    CHECK(DsGl::Compiles(pair.vertex, fragment, log));
    REQUIRE(Edge::Patch(fragment));
    CHECK(mentions(fragment) == pair.edges);
    const bool compiles = DsGl::Compiles(pair.vertex, fragment, log);
    INFO(log);
    CHECK(compiles);
    // Written otherwise it stays as it is, and that is known
    std::string other = pair.fragment;
    other.replace(other.find("void main()"), 11, "void Main()");
    if (const std::size_t at = other.find("int effect = 0;"); at != std::string::npos) {
        other.replace(at, 15, "int how = 0;");
    }
    const std::string was = other;
    CHECK_FALSE(Edge::Patch(other));
    CHECK(other == was);
    // The others are left alone
    for (const char* source : {k2DSpriteFS, k3DRenderFS, k3DFinalPassFogFS, kFinalPassFS}) {
        std::string same = source;
        CHECK(Edge::Patch(same));
        CHECK(same == source);
    }
}

TEST_CASE("Layer Depth's cut of a DS game's pictures over 1x", "[frame_cores]") {
    for (u32 scale = 1; scale <= 8; scale++) {
        CAPTURE(scale);
        AltScreenImage left{};
        left.width = 256 * scale;
        left.height = 192 * scale;
        left.scale = scale;
        left.max_scale = 8;
        left.image = 1;
        AltScreenImage right = left;
        right.image = 2;
        const auto eyes = Vulkan::AltCutEyes(left, right, -6.0f / 256.0f); // Invert Depth
        CHECK(eyes.left.x == 6 * scale);
        CHECK(eyes.right.x == 0);
        CHECK(eyes.left.image == 1);
        CHECK(eyes.right.image == 2);
        CHECK(eyes.left.pixels == nullptr);
        CHECK(eyes.right.pixels == nullptr);
        CHECK(eyes.left.width == 250 * scale);
        CHECK(Vulkan::AltShowsStereo(eyes.left, eyes.right));
        // Dynamic Resolution remakes nothing here either
        CHECK(AltTargetSize(eyes.left.width, eyes.left.height, scale, 8) ==
              AltTargetSize(250 * 8, 192 * 8, 8, 8));
        CHECK(Vulkan::AltCutEyes(left, right, 0.0f).left.width == left.width);
    }
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

TEST_CASE("DS resolution: which screens' pictures are one for each eye", "[frame_cores]") {
    using Shows = std::array<bool, 2>;
    const auto nds = Console();
    auto& gpu = nds->GPU;
    const auto oam = [&gpu](int engine, uint16_t attr0) {
        const std::array<uint16_t, 4> sprite{attr0, 20, 0x0000, 0};
        for (std::size_t i = 0; i < 128; i++) {
            std::memcpy(&gpu.OAM[engine * 0x400 + i * 8], sprite.data(), sizeof(sprite));
        }
    };
    oam(0, 0x0200); // off
    oam(1, 0x0200);
    gpu.GPU2D_B.DispCnt = 0x11100; // a layer of another priority than the sprites'
    gpu.GPU2D_B.BGCnt[0] = 3;
    gpu.GPU2D_A.DispCnt = 0x10000;

    const auto depth = [](FrameCores::NdsDepth::Screens screens) {
        return FrameCores::NdsDepth::MakeDepth(3, 1.0f, false, 0, screens);
    };
    const auto both = depth({true, true});
    FrameCores::ShownDepth shown;
    CHECK(shown.Note(both, gpu, true, false) == Shows{true, false});
    CHECK_FALSE(shown[1]);
    oam(0, 100); // the other engine's sprites
    CHECK(shown.Note(both, gpu, true, false) == Shows{true, false});
    oam(1, 100);
    CHECK(shown.Note(both, gpu, true, false) == Shows{true, true});
    CHECK(shown[1]);
    // It stays, until it is off there; and is looked for again
    gpu.GPU2D_B.DispCnt = 0x10000;
    CHECK(shown.Note(both, gpu, true, false) == Shows{true, true});
    CHECK(shown.Note(depth({true, false}), gpu, true, false) == Shows{true, false});
    CHECK(shown.Note(both, gpu, true, false) == Shows{true, false});
    gpu.GPU2D_B.DispCnt = 0x11100;
    CHECK(shown.Note(depth({false, true}), gpu, true, false) == Shows{false, true});
    CHECK(shown.Note({}, gpu, true, false) == Shows{false, false});
    // Engine A on the bottom screen
    CHECK(shown.Note(both, gpu, false, false) == Shows{false, true});
    gpu.GPU2D_A.DispCnt = 0x10108;
    CHECK(shown.Note(both, gpu, false, false) == Shows{true, true});
}

TEST_CASE("DS resolution: Depth On over 1x", "[frame_cores]") {
    using FrameCores::NdsDepth::Screens;
    Dir tmp;
    const fs::path rom = tmp.path / "on.nds";
    const auto bytes = CountingRom();
    std::ofstream(rom, std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    Log log;
    log.second_eye = true;
    FrameCores::CorePaths paths;
    paths.data_dir = tmp.path.string();
    paths.game_dir = (tmp.path / "g").string();
    paths.cheat_dir = (tmp.path / "c").string();
    FrameCores::CoreBootOptions opts{};
    opts.ds_renderer = Maker(log);
    opts.layer_depth = 2;
    FrameCores::NdsCore core;
    REQUIRE(core.Load(rom.string(), paths, opts));
    core.SetResolution(4, 8);
    FrameCores::FrameOutput out;
    const auto run = [&] {
        core.RunFrame(out);
        core.RunFrame(out);
        CHECK(out.primary.scale == 4);
        return Screens{out.primary_right.HasPicture(), out.secondary_right.HasPicture()};
    };
    CHECK(run() == Screens{true, false});
    core.SetDepthOnSecondary(true);
    CHECK(run() == Screens{true, false}); // nothing to see on it
    CHECK(log.screens == Screens{true, true});
    log.bottom_has_depth = true;
    CHECK(run() == Screens{true, true});
    for (const AltScreenImage* eye : {&out.secondary_left, &out.secondary_right}) {
        CHECK(eye->width == 1024);
        CHECK(eye->scale == 4);
    }
    // The renderer of 1x, which has not seen any on it yet; over 1x again
    for (const uint32_t factor : {1u, 4u}) {
        log.bottom_has_depth = false;
        core.SetResolution(factor, 8);
        core.RunFrame(out);
        CHECK(out.secondary.scale == factor);
        CHECK(out.secondary_right.HasPicture());
        CHECK(out.secondary_right.scale == factor);
    }
    core.Reset();
    CHECK(run() == Screens{true, false});
    log.bottom_has_depth = true;
    CHECK(run() == Screens{true, true});
    core.SetDepthOn(1);
    CHECK(run() == Screens{true, false});
    CHECK(log.screens == Screens{true, false});
    core.SetDepthOn(2);
    CHECK(run() == Screens{false, true});
    CHECK(log.screens == Screens{false, true});
    // By slot, the one in the Secondary slot; swapped, the other, from the next frame. Both eyes
    // have pictures of either then, the same ones where it is off.
    core.SetDepthOn(2, true);
    const auto on = [&](Screens screens) {
        CHECK(run() == Screens{true, true});
        CHECK(log.screens == screens);
        for (const bool top : {true, false}) {
            const auto& own = top ? out.primary : out.secondary;
            for (const AltScreenImage* eye : {top ? &out.primary_left : &out.secondary_left,
                                              top ? &out.primary_right : &out.secondary_right}) {
                CHECK(eye->width == own.width);
                CHECK((screens[top ? 0 : 1] || eye->pixels == own.pixels));
            }
        }
    };
    on({false, true});
    {
        const SwapScreens swap;
        on({true, false});
        core.SetDepthOn(1, true);
        on({false, true});
        core.SetDepthOn(0, true);
        on({true, true});
        core.SetDepthOn(2, true);
        on({true, false});
        core.SetDepthOn(2); // the DS's own
        CHECK(run() == Screens{false, true});
        CHECK(log.screens == Screens{false, true});
        core.SetDepthOn(2, true);
        // The renderer of 1x: on the top screen, the bottom one's pictures are the game's own
        core.SetResolution(1, 8);
        core.RunFrame(out);
        core.RunFrame(out); // the first whole frame
        for (int i = 0; i < 3; i++) {
            core.RunFrame(out);
            CHECK(out.primary.scale == 1);
            CHECK(out.primary_left.pixels != out.primary.pixels);
            CHECK(out.primary_right.pixels != out.primary.pixels);
            CHECK(out.primary_left.pixels != out.primary_right.pixels);
            CHECK(out.secondary_left.pixels == out.secondary.pixels);
            CHECK(out.secondary_right.pixels == out.secondary.pixels);
        }
        core.SetResolution(4, 8);
        on({true, false});
    }
    core.SetResolution(1, 8);
    for (int i = 0; i < 3; i++) {
        core.RunFrame(out);
        CHECK(out.primary_left.pixels == out.primary.pixels);
        CHECK(out.primary_right.pixels == out.primary.pixels);
    }
    core.SetResolution(4, 8);
    on({false, true});
    core.SetDepthOnSecondary(false);
    CHECK_FALSE(run().bottom);
    core.SetDepthOnSecondary(true);
    on({false, true});
    core.SetDepthOn(2);
    CHECK(run() == Screens{false, true});
    const InvertDepth inverted;
    CHECK(run() == Screens{false, true});
    CHECK(out.layer_shift == 0.0f);
    CHECK(out.secondary_shift == -6.0f / 256.0f);
    core.SetDepthOnSecondary(false);
    CHECK(run() == Screens{false, false});
    CHECK(out.secondary_shift == 0.0f);
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
    // Layer Depth, of a renderer without a second eye: both eyes at 1x, no row; and back, whichever
    // changes first
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

    // With a second eye both are there at once
    log.second_eye = true;
    host.SetLayerDepth(1);
    REQUIRE(host.StepFrames(2));
    CHECK(picture.scale == 1); // not asked again in this game
    host.Stop();
    REQUIRE(host.Boot(rom.string(), data, /*headless_for_test=*/true, opts));
    REQUIRE(host.StepFrames(20));
    host.SetLayerDepth(2);
    for (const uint32_t factor : {3u, 8u, 1u, 2u, 3u}) {
        setting = {factor, 8};
        REQUIRE(host.StepFrames(2));
        CAPTURE(factor);
        CHECK(picture.scale == factor);
        CHECK(picture.max_scale == 8);
        CHECK(Small(picture) == small);
        CHECK(right());
        CHECK(host.LastFrame().primary_right.width == picture.width);
        CHECK(host.GetCapabilities().has_internal_resolution);
    }
    host.SetLayerDepth(0);
    REQUIRE(host.StepFrames(2));
    CHECK_FALSE(right());

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
