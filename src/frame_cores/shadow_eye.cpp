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
#include <mgba/gba/interface.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/io.h>
// The last one's second guard opens where it should close: this block is its guard.
#undef CXX_GUARD_START
#undef CXX_GUARD_END
#define CXX_GUARD_START
#define CXX_GUARD_END
#include <mgba/internal/gba/renderers/video-software.h>
}

namespace FrameCores {
namespace {

constexpr uint32_t kOpaque = 0xFF000000u;
constexpr uint32_t kGbaFarthest = 3;    ///< the last of the background priorities
constexpr uint32_t kGbaObjPriority = 1; ///< the sprites are one layer, at this priority's depth
/// The Game Boy has no priorities: its background is as far back, for its width, as the GBA's
/// farthest (3 of 240, 2 of 160).
constexpr uint32_t kGbBackgroundPriority = 2;
constexpr std::size_t kMaxRom = 32u * 1024 * 1024; ///< larger ones page the ROM (GBA Video)

GBA* Gba(mCore* core) {
    return static_cast<GBA*>(core->board);
}

GB* Gb(mCore* core) {
    return static_cast<GB*>(core->board);
}

/// The renderer runs behind another. A Game Boy's is always its own (it never sets videoLogger).
bool HasProxy(const mCore* core) {
    return core->platform(core) == mPLATFORM_GBA && core->videoLogger;
}

/// mGBA's software renderer, where the core has no proxy of it.
GBAVideoSoftwareRenderer* GbaRenderer(mCore* core) {
    return reinterpret_cast<GBAVideoSoftwareRenderer*>(Gba(core)->video.renderer);
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

/// The ROM as the game's core got it (patched), never its own mapping: the shadow writes to its
/// cartridge's pins and may change its ROM.
VFile* CopyRom(mCore* game) {
    return CopyFile(game->platform(game) == mPLATFORM_GBA ? Gba(game)->romVf : Gb(game)->romVf);
}

/// Without the game's BIOS a state is refused while that runs.
VFile* CopyBios(mCore* game) {
    if (game->platform(game) == mPLATFORM_GBA) {
        const GBA* gba = Gba(game);
        return gba->memory.fullBios ? VFileMemChunk(gba->memory.bios, GBA_SIZE_BIOS) : nullptr;
    }
    return CopyFile(Gb(game)->biosVf);
}

} // namespace

ShadowEye::~ShadowEye() {
    Stop();
}

bool ShadowEye::Fits(mCore* game) {
    const mPlatform platform = game->platform(game);
    if ((platform != mPLATFORM_GBA && platform != mPLATFORM_GB) || HasProxy(game)) {
        return false;
    }
    VFile* rom = platform == mPLATFORM_GBA ? Gba(game)->romVf : Gb(game)->romVf;
    const ssize_t size = rom ? rom->size(rom) : 0;
    return size > 0 && static_cast<std::size_t>(size) <= kMaxRom;
}

bool ShadowEye::Start(mCore* game_, uint32_t width_, uint32_t height_) {
    Stop();
    if (!Fits(game_) || width_ == 0 || height_ == 0) {
        return false;
    }
    const mPlatform platform = game_->platform(game_);
    VFile* rom = CopyRom(game_);
    if (!rom) {
        LOG_ERROR(Core, "ShadowEye: no ROM to copy");
        return false;
    }
    core = mCoreCreate(platform);
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
    if (platform == mPLATFORM_GBA) {
        // Sunlight; no rumble and no link
        core->setPeripheral(core, mPERIPH_GBA_LUMINANCE,
                            game->getPeripheral(game, mPERIPH_GBA_LUMINANCE));
    }

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

    const std::size_t size = game->stateSize(game);
    if (size == 0 || size != core->stateSize(core) || HasProxy(core)) {
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
    ShiftLayers();
    farthest_shift = static_cast<float>(Shift(Farthest())) / static_cast<float>(width);
    core->setKeys(core, keys);
    core->runFrame(core);
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

float ShadowEye::FarthestShift() const {
    return drawn ? farthest_shift : 0.0f;
}

uint32_t ShadowEye::Farthest() const {
    return game->platform(game) == mPLATFORM_GBA ? kGbaFarthest : kGbBackgroundPriority;
}

int32_t ShadowEye::Shift(uint32_t step) const {
    return static_cast<int32_t>(
        Common::LayerDepth::LayerShift(depth, step, Farthest(), width, max_shift));
}

bool ShadowEye::CopyGame() {
    const bool is_gba = game->platform(game) == mPLATFORM_GBA;
    // Reading the registers for a state ends mGBA's idle loop detection: the game's goes on as if
    // none was made.
    const bool halt_pending = is_gba && Gba(game)->haltPending;
    const bool saved = game->saveState(game, state.data());
    if (is_gba) {
        Gba(game)->haltPending = halt_pending;
    }
    if (!saved || !core->loadState(core, state.data())) {
        return false;
    }

    // What a state does not hold: the save data (some games keep more than saves there), and what
    // the renderers carry from one frame to the next.
    if (is_gba) {
        GBA* to = Gba(core);
        const GBA* from = Gba(game);
        to->haltPending = halt_pending;
        const std::size_t size = GBASavedataSize(&from->memory.savedata);
        if (size > 0 && size == GBASavedataSize(&to->memory.savedata) &&
            from->memory.savedata.data && to->memory.savedata.data) {
            std::memcpy(to->memory.savedata.data, from->memory.savedata.data, size);
        }
        for (int i = 0; i < 2; ++i) {
            GbaRenderer(core)->winN[i].on = GbaRenderer(game)->winN[i].on;
        }
        for (int i = 0; i < 4; ++i) {
            GbaRenderer(core)->bg[i].enabled = GbaRenderer(game)->bg[i].enabled;
        }
    } else {
        GB* to = Gb(core);
        const GB* from = Gb(game);
        if (from->memory.sram && to->memory.sram) {
            std::memcpy(to->memory.sram, from->memory.sram,
                        std::min(from->sramSize, to->sramSize));
        }
    }

    // The lines the frame leaves as they are (after a reset, a frozen Super Game Boy picture)
    const void* shown = nullptr;
    std::size_t stride = 0;
    game->getPixels(game, &shown, &stride);
    if (shown) {
        const auto* from = static_cast<const uint32_t*>(shown);
        for (uint32_t y = 0; y < height; ++y) {
            std::memcpy(pixels.data() + static_cast<std::size_t>(y) * width, from + y * stride,
                        width * sizeof(uint32_t));
        }
    }
    return true;
}

// To the right: farther than the screen. Loading a state put every offset back to 0.
void ShadowEye::ShiftLayers() {
    if (game->platform(game) == mPLATFORM_GBA) {
        const uint16_t* io = Gba(game)->memory.io; // busRead16 is a read by the game
        for (uint32_t bg = 0; bg < 4; ++bg) {
            const uint32_t priority = io[GBA_REG(BG0CNT) + bg] & 3u;
            core->adjustVideoLayer(core, GBA_LAYER_BG0 + bg, Shift(priority), 0);
        }
        core->adjustVideoLayer(core, GBA_LAYER_OBJ, Shift(kGbaObjPriority), 0);
    } else {
        core->adjustVideoLayer(core, GB_LAYER_BACKGROUND, Shift(kGbBackgroundPriority), 0);
    }
}

void ShadowEye::FailOnce(const char* what) {
    if (!failed) {
        failed = true;
        LOG_ERROR(Core, "ShadowEye: {}", what);
    }
}

} // namespace FrameCores
