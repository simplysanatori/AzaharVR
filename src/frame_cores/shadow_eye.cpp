// Adapter for mGBA (endrift, MPL-2.0)
// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/shadow_eye.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "common/logging/log.h"

extern "C" {
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>
#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/gb/interface.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gb/renderers/software.h>
}

namespace FrameCores {
namespace {

constexpr uint32_t kOpaque = 0xFF000000u;
/// The Game Boy has no priorities: its background is as far back, for its width, as the GBA's
/// farthest (3 of 240, 2 of 160).
constexpr uint32_t kBackgroundSteps = 2;
/// Of a pixel of mGBA's line: its palette entry, the sprites' after the background's.
constexpr uint32_t kPaletteMask = 0xFFu;
constexpr uint32_t kSpritePalettes = 0x20;
constexpr std::size_t kMaxRom = 32u * 1024 * 1024;

GB* Gb(mCore* core) {
    return static_cast<GB*>(core->board);
}

/// A copy of a file the game's core has open.
VFile* CopyFile(VFile* from) {
    const ssize_t size = from ? from->size(from) : 0;
    if (size <= 0 || static_cast<std::size_t>(size) > kMaxRom) {
        return nullptr;
    }
    std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
    if (from->seek(from, 0, SEEK_SET) != 0 ||
        from->read(from, bytes.data(), bytes.size()) != size) {
        return nullptr;
    }
    return VFileMemChunk(bytes.data(), bytes.size());
}

/// The ROM as the game's core got it (patched), never its own mapping.
VFile* CopyRom(mCore* game) {
    return CopyFile(Gb(game)->romVf);
}

/// Without the game's BIOS a state is refused while that runs.
VFile* CopyBios(mCore* game) {
    return CopyFile(Gb(game)->biosVf);
}

} // namespace

ShadowEye::~ShadowEye() {
    Stop();
}

bool ShadowEye::Fits(mCore* game) {
    if (game->platform(game) != mPLATFORM_GB) {
        return false;
    }
    VFile* rom = Gb(game)->romVf;
    const ssize_t size = rom ? rom->size(rom) : 0;
    return size > 0 && static_cast<std::size_t>(size) <= kMaxRom;
}

bool ShadowEye::Start(mCore* game_, uint32_t width_, uint32_t height_) {
    Stop();
    if (!Fits(game_) || width_ == 0 || height_ == 0) {
        return false;
    }
    VFile* rom = CopyRom(game_);
    if (!rom) {
        LOG_ERROR(Core, "ShadowEye: no ROM to copy");
        return false;
    }
    core = mCoreCreate(mPLATFORM_GB);
    if (!core || !core->init(core)) {
        LOG_ERROR(Core, "ShadowEye: no core");
        if (core) {
            core->deinit(core);
            core = nullptr;
        }
        rom->close(rom);
        return false;
    }
    game = game_;
    width = width_;
    height = height_;

    mCoreInitConfig(core, nullptr);
    mCoreLoadForeignConfig(core, &game->config);
    core->opts.useBios = false; // the game's own, below
    core->opts.skipBios = game->opts.skipBios;
    core->rtc.override = game->rtc.override;
    core->rtc.value = game->rtc.value;

    unsigned base_w = 0;
    unsigned base_h = 0;
    core->baseVideoSize(core, &base_w, &base_h);
    pixels.assign(static_cast<std::size_t>(std::max<uint32_t>(base_w, width)) *
                      std::max<uint32_t>(base_h, height),
                  0);
    core->setVideoBuffer(core, pixels.data(), width);

    if (!core->loadROM(core, rom)) {
        LOG_ERROR(Core, "ShadowEye: loadROM failed");
        rom->close(rom);
        Stop();
        return false;
    }
    if (VFile* bios = CopyBios(game); bios && !core->loadBIOS(core, bios, 0)) {
        bios->close(bios);
    }
    core->reset(core); // with the renderer; no save file
    GBVideoRenderer* const renderer = Gb(core)->video.renderer;
    draw_range = renderer->drawRange;
    renderer->drawRange = &ShadowEye::Hook;

    const std::size_t size = game->stateSize(game);
    if (size == 0 || size != core->stateSize(core)) {
        LOG_ERROR(Core, "ShadowEye: another kind of core");
        Stop();
        return false;
    }
    state.assign(size, 0);
    LOG_INFO(Core, "ShadowEye: {}x{}, depth {}", width, height, depth);
    return true;
}

void ShadowEye::Stop() {
    if (core) {
        core->unloadROM(core);
        mCoreConfigDeinit(&core->config);
        core->deinit(core);
        core = nullptr;
    }
    game = nullptr;
    pixels.clear();
    state.clear();
    drawn = false;
    failed = false;
}

void ShadowEye::SetDepth(uint32_t level) {
    depth = std::min(level, Common::LayerDepth::kMaxLevel);
    if (depth == 0) {
        drawn = false;
    }
}

void ShadowEye::SetMaxShift(float share) {
    max_shift = share;
    if (max_shift <= 0.0f) {
        drawn = false;
    }
}

void ShadowEye::Run(uint32_t keys) {
    if (!core || depth == 0 || max_shift <= 0.0f) {
        return;
    }
    drawn = false;
    unsigned now_w = 0;
    unsigned now_h = 0;
    game->currentVideoSize(game, &now_w, &now_h);
    if (now_w != width || now_h != height) {
        FailOnce("the game's picture has another size");
        return;
    }
    if (!CopyGame()) {
        FailOnce("the game's state was not taken");
        return;
    }
    background_shift = BackgroundShift();
    ShiftLayers();
    core->setKeys(core, keys);
    running = this;
    core->runFrame(core);
    running = nullptr;
    if (mAudioBuffer* sound = core->getAudioBuffer(core)) {
        mAudioBufferClear(sound);
    }
    const std::size_t count = static_cast<std::size_t>(width) * height;
    for (std::size_t i = 0; i < count; ++i) {
        pixels[i] |= kOpaque;
    }
    drawn = true;
}

DriverPicture ShadowEye::Picture() const {
    return drawn ? DriverPicture{pixels.data(), width, height, width} : DriverPicture{};
}

float ShadowEye::EdgeShift() const {
    return drawn ? Common::LayerDepth::EdgeShift(background_shift, width) : 0.0f;
}

int32_t ShadowEye::BackgroundShift() const {
    namespace LayerDepth = Common::LayerDepth;
    return LayerDepth::Directed(
        LayerDepth::LayerShift(depth, kBackgroundSteps, kBackgroundSteps, width, max_shift),
        LayerDepth::Get().inverted.load(std::memory_order_relaxed));
}

bool ShadowEye::CopyGame() {
    if (!game->saveState(game, state.data()) || !core->loadState(core, state.data())) {
        return false;
    }

    // A state does not hold the save data (some games keep more than saves there)
    GB* to = Gb(core);
    const GB* from = Gb(game);
    if (from->memory.sram && to->memory.sram) {
        std::memcpy(to->memory.sram, from->memory.sram, std::min(from->sramSize, to->sramSize));
    }

    // The lines the frame leaves as they are (after a reset, a frozen Super Game Boy picture)
    const void* shown = nullptr;
    std::size_t stride = 0;
    game->getPixels(game, &shown, &stride);
    if (shown) {
        const auto* lines = static_cast<const uint32_t*>(shown);
        for (uint32_t y = 0; y < height; ++y) {
            std::memcpy(pixels.data() + static_cast<std::size_t>(y) * width, lines + y * stride,
                        width * sizeof(uint32_t));
        }
    }
    return true;
}

// To the right is farther than the screen. Loading a state put every offset back to 0.
void ShadowEye::ShiftLayers() {
    core->adjustVideoLayer(core, GB_LAYER_BACKGROUND, background_shift, 0);
}

thread_local ShadowEye* ShadowEye::running = nullptr;

// The second core's renderer, which draws in Run alone.
void ShadowEye::Hook(GBVideoRenderer* renderer, int start, int end, int y) {
    if (running) {
        running->DrawLine(renderer, start, end, y);
    }
}

// The background ends where it ends in the game's own picture: the columns beyond are drawn again
// without it.
void ShadowEye::DrawLine(GBVideoRenderer* renderer, int start, int end, int y) const {
    draw_range(renderer, start, end, y);
    auto& software = *reinterpret_cast<GBVideoSoftwareRenderer*>(renderer);
    const int edge = std::min(Common::LayerDepth::EdgeColumns(background_shift), end);
    const bool has_background =
        GBRegisterLCDCIsBgEnable(software.lcdc) || software.model >= GB_MODEL_CGB;
    if (start >= edge || !has_background || renderer->sgbRenderMode != 0) {
        return;
    }
    renderer->disableBG = true;
    draw_range(renderer, start, edge, y);
    renderer->disableBG = false;
    software.lastX = end;

    const bool window = GBRegisterLCDCIsWindow(software.lcdc) && software.hasWindow;
    const int window_x = window ? software.wx + software.currentWx - 7 : edge;
    uint32_t* const line =
        software.outputBuffer + static_cast<std::ptrdiff_t>(software.outputBufferStride) * y;
    for (int x = start; x < std::min(window_x, edge); ++x) {
        if ((software.row[x] & kPaletteMask) < kSpritePalettes) {
            line[x] = Common::LayerDepth::kBeyond;
        }
    }
}

void ShadowEye::FailOnce(const char* what) {
    if (!failed) {
        failed = true;
        LOG_ERROR(Core, "ShadowEye: {}", what);
    }
}

} // namespace FrameCores
