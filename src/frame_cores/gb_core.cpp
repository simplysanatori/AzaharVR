// Adapter for mGBA (endrift, MPL-2.0)
// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/gb_core.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include "common/logging/log.h"
#include "frame_cores/cheat_store.h"
#include "frame_cores/driver_runner.h"
#include "frame_cores/rom_info.h"
#include "frame_cores/shadow_eye.h"

extern "C" {
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>
#include <mgba/core/cheats.h>
#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba/gb/core.h>
#include <mgba/gb/interface.h>
#include <mgba/internal/gb/cheats.h>
#include <mgba/internal/gb/gb.h>
}

namespace FrameCores {
namespace {

constexpr uint32_t kGbWidth = 160;
constexpr uint32_t kGbHeight = 144;

bool EnsureDir(const std::string& dir) {
    if (dir.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return !ec;
}

bool IsZipPath(const std::string& path) {
    auto ext = std::filesystem::path(path).extension().string();
    for (char& ch : ext) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return ext == ".zip";
}

bool AtomicWriteBytes(const std::string& path, const uint8_t* data, std::size_t size) {
    EnsureDir(std::filesystem::path(path).parent_path().string());
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        out.flush();
        if (!out.good()) {
            return false;
        }
    }
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::rename(path, path + ".bak", ec);
        ec.clear();
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

void GbLoggerCallback(struct mLogger* /*logger*/, int /*category*/, enum mLogLevel level,
                      const char* format, va_list args) {
    if ((level & (mLOG_FATAL | mLOG_ERROR)) == 0) {
        return;
    }
    char buf[512];
    std::vsnprintf(buf, sizeof(buf), format, args);
    LOG_ERROR(Core, "mGBA(GB): {}", buf);
}

struct mLogger g_gb_logger{GbLoggerCallback, nullptr};

struct GbRumbleWrapper {
    mRumbleIntegrator integrator{};
    std::atomic<float> level{0.0f};
};

void OnGbRumbleSet(struct mRumbleIntegrator* integ, float value) {
    auto* wrap = reinterpret_cast<GbRumbleWrapper*>(integ);
    wrap->level.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_relaxed);
}

} // namespace

struct GbCore::Impl {
    mCore* core = nullptr;

    std::string rom_path;
    std::string save_path;
    CorePaths paths{};
    CoreBootOptions opts{};

    uint32_t width = kGbWidth;
    uint32_t height = kGbHeight;
    uint32_t stride = kGbWidth; ///< `fb`'s: what the driver wants
    uint32_t audio_rate = 32768;

    std::vector<mColor> fb;
    GbRumbleWrapper rumble{};
    Common::AltCore::InputState input{};
    DriverRunner driver;
    ShadowEye shadow;

    void StartDriver(DriverKind kind, const DriverRunner::Known& known) {
        DriverServices services;
        services.core = core;
        services.step = [this](uint32_t keys) {
            // Bits 0..7 match GB_KEY_A..GB_KEY_DOWN 1:1; the GBA's R and L are none of them.
            shadow.Run(keys & 0xFFu);
            core->setKeys(core, keys & 0xFFu);
            core->runFrame(core);
        };
        services.drain_audio = [this](int16_t* out, std::size_t max_frames) -> std::size_t {
            mAudioBuffer* buffer = core->getAudioBuffer(core);
            return buffer ? mAudioBufferRead(buffer, out, max_frames) : 0;
        };
        services.rumble = [this] { return rumble.level.load(std::memory_order_relaxed); };
        services.picture = [this] {
            return DriverPicture{fb.data(), width, height, stride}; // XBGR8888
        };
        services.rom_path = rom_path;
        services.game_dir = paths.game_dir;

        RomInfo info;
        info.kind = Common::AltCore::CoreKind::GB;
        driver.Start(kind, known, std::move(services), info, opts, save_path, caps);

        caps.layer_depth = ShadowEye::Fits(core);
        ApplyLayerDepth();
    }

    void ApplyLayerDepth() {
        shadow.SetDepth(opts.layer_depth);
        if (opts.layer_depth == 0 || !caps.layer_depth) {
            shadow.Stop();
        } else if (!shadow.Started()) {
            shadow.Start(core, width, height);
        }
    }

    std::vector<CheatEntry> custom_cheats; ///< the cheats the player added (kept beside the save)
    std::string custom_cheats_path;

    // Puts the switched-on cheats into mGBA's cheat device (all rebuilt).
    void ApplyCheats() {
        mCheatDevice* dev = core ? core->cheatDevice(core) : nullptr;
        if (!dev) {
            return;
        }
        // Clearing alone would leave a Game Genie code's change in the game's ROM.
        for (std::size_t i = 0; i < mCheatSetsSize(&dev->cheats); ++i) {
            mCheatSet* set = *mCheatSetsGetPointer(&dev->cheats, i);
            set->enabled = false;
            mCheatRefresh(dev, set);
        }
        mCheatDeviceClear(dev);
        for (const auto& entry : custom_cheats) {
            if (!entry.enabled) {
                continue;
            }
            mCheatSet* set = dev->createSet(dev, entry.name.c_str());
            for (const auto& line : SplitCheatCode(entry.code)) {
                mCheatAddLine(set, line.c_str(), GB_CHEAT_AUTODETECT);
            }
            mCheatAddSet(dev, set);
        }
    }

    // True when every line of the code is one mGBA reads.
    bool CodeIsValid(const std::string& code) const {
        mCheatDevice* dev = core ? core->cheatDevice(core) : nullptr;
        const auto lines = SplitCheatCode(code);
        if (!dev || lines.empty()) {
            return false;
        }
        mCheatSet* set = dev->createSet(dev, "check");
        bool ok = true;
        for (const auto& line : lines) {
            ok = ok && mCheatAddLine(set, line.c_str(), GB_CHEAT_AUTODETECT);
        }
        mCheatSetDeinit(set);
        return ok;
    }

    Common::AltCore::Capabilities caps = Common::AltCore::Capabilities::ForGB();
};

GbCore::GbCore() : impl(std::make_unique<Impl>()) {}

GbCore::~GbCore() {
    Unload();
}

bool GbCore::Load(const std::string& rom_path, const CorePaths& paths,
                  const CoreBootOptions& opts) {
    Unload();

    mLogSetDefaultLogger(&g_gb_logger);

    impl->rom_path = rom_path;
    impl->paths = paths;
    impl->opts = opts;
    impl->caps = Common::AltCore::Capabilities::ForGB();

    EnsureDir(paths.game_dir);

    VFile* rom_vf = nullptr;
    if (IsZipPath(rom_path)) {
        std::vector<uint8_t> rom_bytes;
        if (!ExtractRomFromZip(rom_path, rom_bytes) || rom_bytes.empty()) {
            LOG_ERROR(Core, "GbCore: failed to extract GB/GBC ROM from {}", rom_path);
            return false;
        }
        rom_vf = VFileMemChunk(rom_bytes.data(), rom_bytes.size());
    } else {
        rom_vf = VFileOpen(rom_path.c_str(), O_RDONLY);
    }

    if (!rom_vf) {
        LOG_ERROR(Core, "GbCore: failed to open {}", rom_path);
        return false;
    }

    mCore* core = GBCoreCreate();
    if (!core || !core->init(core)) {
        if (core) {
            core->deinit(core);
        }
        rom_vf->close(rom_vf);
        LOG_ERROR(Core, "GbCore: GBCoreCreate/init failed");
        return false;
    }

    mCoreInitConfig(core, nullptr);
    mCoreOptions core_opts{};
    core_opts.useBios = opts.use_bios;
    core_opts.skipBios = false;
    core_opts.volume = 0x100;
    core_opts.audioBuffers = 1024;
    mCoreConfigLoadDefaults(&core->config, &core_opts);
    mCoreConfigSetDefaultIntValue(&core->config, "gb.colors",
                                  opts.color_correction ? GB_COLORS_SGB_CGB_FALLBACK : GB_COLORS_NONE);
    mCoreConfigSetDefaultIntValue(&core->config, "useCgbColors",
                                  opts.color_correction ? GB_COLORS_SGB_CGB_FALLBACK : GB_COLORS_NONE);
    mCoreConfigSetDefaultIntValue(&core->config, "sgb.borders", 0);
    mCoreConfigSetDefaultIntValue(&core->config, "allowOpposingDirections", 0);
    mCoreLoadConfig(core);

    unsigned base_w = kGbWidth;
    unsigned base_h = kGbHeight;
    core->baseVideoSize(core, &base_w, &base_h);
    const auto known = DriverRunner::WhatKnows(rom_path, /*gbads=*/false);
    const DriverKind driver_kind = DriverRunner::Choose(opts, known);
    const uint32_t alloc_w =
        DriverBufferWidth(driver_kind, std::max<uint32_t>(base_w, kGbWidth));
    const uint32_t alloc_h = std::max<uint32_t>(base_h, kGbHeight);
    impl->width = kGbWidth;
    impl->height = kGbHeight;
    impl->fb.assign(static_cast<std::size_t>(alloc_w) * alloc_h, 0);

    core->setVideoBuffer(core, impl->fb.data(), alloc_w);
    core->setAudioBufferSize(core, 2048);

    mRumbleIntegratorInit(&impl->rumble.integrator);
    impl->rumble.integrator.setRumble = OnGbRumbleSet;
    impl->rumble.level.store(0.0f, std::memory_order_relaxed);
    core->setPeripheral(core, mPERIPH_RUMBLE, &impl->rumble.integrator.d);

    if (!core->loadROM(core, rom_vf)) {
        rom_vf->close(rom_vf);
        mCoreConfigDeinit(&core->config);
        core->deinit(core);
        LOG_ERROR(Core, "GbCore: loadROM failed for {}", rom_path);
        return false;
    }
    // `core->loadROM` takes ownership of `rom_vf` on success.

    // Optional GB/GBC BIOS if present in bios_dir
    if (opts.use_bios && !paths.bios_dir.empty()) {
        const std::array<std::string, 2> bios_names = {"gbc_bios.bin", "gb_bios.bin"};
        for (const auto& name : bios_names) {
            const auto bios_path = (std::filesystem::path(paths.bios_dir) / name).string();
            if (VFile* bvf = VFileOpen(bios_path.c_str(), O_RDONLY)) {
                if (!core->loadBIOS(core, bvf, 0)) {
                    bvf->close(bvf);
                } else {
                    LOG_INFO(Core, "GbCore: loaded BIOS {}", bios_path);
                    break;
                }
            }
        }
    }

    const std::string stem = std::filesystem::path(rom_path).stem().string();
    impl->save_path = (std::filesystem::path(paths.game_dir) / (stem + ".sav")).string();
    if (VFile* save_vf = VFileOpen(impl->save_path.c_str(), O_CREAT | O_RDWR)) {
        if (!core->loadSave(core, save_vf)) {
            save_vf->close(save_vf);
        }
    }

    core->reset(core);

    unsigned cur_w = kGbWidth;
    unsigned cur_h = kGbHeight;
    core->currentVideoSize(core, &cur_w, &cur_h);
    impl->width = cur_w > 0 ? cur_w : kGbWidth;
    impl->height = cur_h > 0 ? cur_h : kGbHeight;
    impl->stride = DriverBufferWidth(driver_kind, impl->width);
    core->setVideoBuffer(core, impl->fb.data(), impl->stride);

    impl->core = core;
    impl->input = {};
    impl->caps.has_alt_cheats = true;
    impl->custom_cheats_path =
        (std::filesystem::path(paths.game_dir) / (stem + ".custom.cheats")).string();
    impl->custom_cheats = LoadCustomCheats(impl->custom_cheats_path);
    impl->ApplyCheats();
    const unsigned rate = core->audioSampleRate(core);
    impl->audio_rate = rate > 0 ? rate : 32768u;

    impl->caps.screens.top_aspect =
        static_cast<float>(impl->width) / static_cast<float>(impl->height);
    impl->caps.screens.touch_width = impl->width;
    impl->caps.screens.touch_height = impl->height;
    impl->StartDriver(driver_kind, known);

    LOG_INFO(Core, "GbCore: loaded {} ({}x{}, rate={} Hz)", rom_path, impl->width, impl->height,
             impl->audio_rate);
    return true;
}

void GbCore::Unload() {
    if (!impl) {
        return;
    }
    impl->shadow.Stop();
    impl->driver.Stop();
    if (impl->core) {
        impl->core->unloadROM(impl->core);
        mCoreConfigDeinit(&impl->core->config);
        impl->core->deinit(impl->core);
        impl->core = nullptr;
    }
    impl->custom_cheats.clear();
    impl->fb.clear();
    impl->rumble.level.store(0.0f, std::memory_order_relaxed);
}

void GbCore::Reset() {
    if (!impl->core) {
        return;
    }
    impl->core->reset(impl->core);
    impl->rumble.level.store(0.0f, std::memory_order_relaxed);
    impl->ApplyCheats();
    impl->driver.Reset();
}

void GbCore::SetInput(const Common::AltCore::InputState& input) {
    impl->input = input;
}

void GbCore::RunFrame(FrameOutput& out) {
    impl->driver.RunFrame(impl->input, out, impl->caps);
    impl->driver.SetRightEye(impl->shadow.Picture(),
                             {impl->fb.data(), impl->width, impl->height, impl->stride},
                             impl->shadow.FarthestShift(), out);
}

uint32_t GbCore::AudioSampleRate() const {
    return impl->audio_rate;
}

Common::AltCore::Capabilities GbCore::GetCapabilities() const {
    return impl->caps;
}

bool GbCore::SaveStateToMemory(std::vector<uint8_t>& out) {
    if (!impl->core) {
        return false;
    }
    // Use mCoreSaveStateNamed into an in-memory VFile so SRAM + RTC extdata are included.
    constexpr int kStateFlags = SAVESTATE_SAVEDATA | SAVESTATE_RTC | SAVESTATE_METADATA;
    if (VFile* vf = VFileMemChunk(nullptr, 0)) {
        const bool ok = mCoreSaveStateNamed(impl->core, vf, kStateFlags);
        if (ok) {
            const ssize_t sz = vf->size(vf);
            if (sz > 0) {
                out.resize(static_cast<std::size_t>(sz));
                vf->seek(vf, 0, SEEK_SET);
                const ssize_t got = vf->read(vf, out.data(), static_cast<std::size_t>(sz));
                vf->close(vf);
                if (got == sz) {
                    return true;
                }
                out.clear();
                return false;
            }
        }
        vf->close(vf);
    }

    // Fallback to raw state buffer
    const size_t raw_sz = impl->core->stateSize(impl->core);
    if (raw_sz == 0) {
        return false;
    }
    out.resize(raw_sz);
    if (!impl->core->saveState(impl->core, out.data())) {
        out.clear();
        return false;
    }
    return true;
}

bool GbCore::LoadStateFromMemory(const uint8_t* data, std::size_t size) {
    if (!impl->core || !data || size == 0) {
        return false;
    }
    if (VFile* vf = VFileFromConstMemory(data, size)) {
        // Without SAVESTATE_SAVEDATA, as GBA states: the game sees the state's save data, and the
        // save file changes only when the game saves.
        const bool ok = mCoreLoadStateNamed(impl->core, vf, SAVESTATE_RTC);
        vf->close(vf);
        if (ok) {
            return true;
        }
    }
    const size_t raw_sz = impl->core->stateSize(impl->core);
    if (raw_sz > 0 && size >= raw_sz) {
        return impl->core->loadState(impl->core, data);
    }
    return false;
}

bool GbCore::StateSafeNow() const {
    return impl->driver.StateSafeNow();
}

void GbCore::AfterStateLoad() {
    impl->driver.AfterStateLoad();
}

void GbCore::SetEditing(bool on) {
    impl->driver.SetEditing(on);
}

void GbCore::SetLayerDepth(uint32_t level) {
    if (impl->opts.layer_depth != level) {
        impl->opts.layer_depth = level;
        impl->ApplyLayerDepth();
    }
}

void GbCore::SetMaxLayerShift(float share) {
    impl->shadow.SetMaxShift(share);
}

void GbCore::TurnPage() {
    impl->driver.TurnPage();
}

std::vector<CheatEntry> GbCore::ListCheats() const {
    return impl->custom_cheats;
}

bool GbCore::SetCheatEnabled(std::size_t idx, bool on) {
    if (!impl->core || idx >= impl->custom_cheats.size()) {
        return false;
    }
    impl->custom_cheats[idx].enabled = on;
    SaveCustomCheats(impl->custom_cheats_path, impl->custom_cheats);
    impl->ApplyCheats();
    return true;
}

bool GbCore::AddCheat(const std::string& name, const std::string& code) {
    if (!impl->core || !impl->CodeIsValid(code)) {
        return false;
    }
    impl->custom_cheats.push_back(CheatEntry{name.empty() ? "Custom Cheat" : name, code, true});
    SaveCustomCheats(impl->custom_cheats_path, impl->custom_cheats);
    impl->ApplyCheats();
    return true;
}

bool GbCore::ImportBatterySave(const std::string& sav_path) {
    if (!impl->core || sav_path.empty() || impl->save_path.empty()) {
        return false;
    }
    std::ifstream in(sav_path, std::ios::binary | std::ios::ate);
    if (!in) {
        return false;
    }
    const auto sz = static_cast<std::size_t>(in.tellg());
    std::vector<uint8_t> bytes(sz);
    in.seekg(0, std::ios::beg);
    if (sz > 0 && !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(sz))) {
        return false;
    }

    const std::string rom = impl->rom_path;
    const CorePaths paths = impl->paths;
    const CoreBootOptions opts = impl->opts;
    const std::string dst_save = impl->save_path;

    Unload();
    if (!AtomicWriteBytes(dst_save, bytes.data(), bytes.size())) {
        Load(rom, paths, opts);
        return false;
    }
    return Load(rom, paths, opts);
}

} // namespace FrameCores
