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
#include "frame_cores/rom_info.h"

extern "C" {
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>
#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/rewind.h>
#include <mgba/core/serialize.h>
#include <mgba/gb/core.h>
#include <mgba/gb/interface.h>
#include <mgba/internal/gb/gb.h>
}

namespace FrameCores {
namespace {

constexpr uint32_t kGbWidth = 160;
constexpr uint32_t kGbHeight = 144;
constexpr uint32_t kRewindAppendInterval = 6;

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
    mCoreRewindContext rewind_ctx{};
    bool rewind_inited = false;

    std::string rom_path;
    std::string save_path;
    CorePaths paths{};
    CoreBootOptions opts{};

    uint32_t width = kGbWidth;
    uint32_t height = kGbHeight;
    uint32_t audio_rate = 32768;
    uint32_t keys = 0;
    uint64_t frame_count = 0;

    std::vector<mColor> fb;
    std::vector<uint32_t> rgba_fb;
    std::vector<int16_t> audio_scratch;
    GbRumbleWrapper rumble{};

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

    EnsureDir(paths.save_dir);
    EnsureDir(paths.state_dir);

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
    const uint32_t alloc_w = std::max<uint32_t>(base_w, kGbWidth);
    const uint32_t alloc_h = std::max<uint32_t>(base_h, kGbHeight);
    impl->width = kGbWidth;
    impl->height = kGbHeight;
    impl->fb.assign(static_cast<std::size_t>(alloc_w) * alloc_h, 0);
    impl->rgba_fb.assign(static_cast<std::size_t>(alloc_w) * alloc_h, 0xFF000000u);
    impl->audio_scratch.resize(4096u * 2u, 0);

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
    impl->save_path = (std::filesystem::path(paths.save_dir) / (stem + ".sav")).string();
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
    core->setVideoBuffer(core, impl->fb.data(), impl->width);

    impl->core = core;
    impl->frame_count = 0;
    impl->keys = 0;
    const unsigned rate = core->audioSampleRate(core);
    impl->audio_rate = rate > 0 ? rate : 32768u;

    impl->caps.has_rewind = opts.enable_rewind;
    impl->caps.screens.top_aspect =
        static_cast<float>(impl->width) / static_cast<float>(impl->height);
    impl->caps.screens.touch_width = impl->width;
    impl->caps.screens.touch_height = impl->height;

    if (opts.enable_rewind && opts.rewind_entries > 0) {
        mCoreRewindContextInit(&impl->rewind_ctx, opts.rewind_entries, false);
        impl->rewind_inited = true;
    }

    LOG_INFO(Core, "GbCore: loaded {} ({}x{}, rate={} Hz)", rom_path, impl->width, impl->height,
             impl->audio_rate);
    return true;
}

void GbCore::Unload() {
    if (!impl) {
        return;
    }
    if (impl->rewind_inited) {
        mCoreRewindContextDeinit(&impl->rewind_ctx);
        impl->rewind_inited = false;
    }
    if (impl->core) {
        impl->core->unloadROM(impl->core);
        mCoreConfigDeinit(&impl->core->config);
        impl->core->deinit(impl->core);
        impl->core = nullptr;
    }
    impl->fb.clear();
    impl->rgba_fb.clear();
    impl->frame_count = 0;
    impl->rumble.level.store(0.0f, std::memory_order_relaxed);
}

void GbCore::Reset() {
    if (!impl->core) {
        return;
    }
    impl->core->reset(impl->core);
    impl->frame_count = 0;
    impl->rumble.level.store(0.0f, std::memory_order_relaxed);
}

void GbCore::SetInput(const Common::AltCore::InputState& input) {
    // Common::AltCore::ButtonBit bits 0..7 match GB_KEY_A..GB_KEY_DOWN 1:1.
    impl->keys = input.buttons & 0xFFu;
}

void GbCore::RunFrame(FrameOutput& out) {
    out.primary = {};
    out.secondary = {};
    out.audio_pcm.clear();
    out.rumble = 0.0f;
    out.metrics_changed = false;

    if (!impl->core) {
        return;
    }

    impl->core->setKeys(impl->core, impl->keys);
    impl->core->runFrame(impl->core);
    ++impl->frame_count;

    if (impl->rewind_inited && (impl->frame_count % kRewindAppendInterval) == 0) {
        mCoreRewindAppend(&impl->rewind_ctx, impl->core);
    }

    const std::size_t n_pixels = static_cast<std::size_t>(impl->width) * impl->height;
    for (std::size_t i = 0; i < n_pixels; ++i) {
        impl->rgba_fb[i] = static_cast<uint32_t>(impl->fb[i]) | 0xFF000000u;
    }

    out.primary.pixels = impl->rgba_fb.data();
    out.primary.width = impl->width;
    out.primary.height = impl->height;
    out.primary.stride_pixels = impl->width;
    out.primary.bgra = false;

    if (mAudioBuffer* ab = impl->core->getAudioBuffer(impl->core)) {
        const std::size_t max_frames = impl->audio_scratch.size() / 2u;
        const std::size_t got = mAudioBufferRead(ab, impl->audio_scratch.data(), max_frames);
        if (got > 0) {
            out.audio_pcm.assign(impl->audio_scratch.data(),
                                 impl->audio_scratch.data() + got * 2u);
        }
    }

    out.rumble = impl->rumble.level.load(std::memory_order_relaxed);
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

bool GbCore::SaveStateToFile(const std::string& path) {
    std::vector<uint8_t> buf;
    if (!SaveStateToMemory(buf)) {
        return false;
    }
    return AtomicWriteBytes(path, buf.data(), buf.size());
}

bool GbCore::LoadStateFromFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return false;
    }
    const auto sz = static_cast<std::size_t>(in.tellg());
    if (sz == 0) {
        return false;
    }
    std::vector<uint8_t> buf(sz);
    in.seekg(0, std::ios::beg);
    if (!in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz))) {
        return false;
    }
    return LoadStateFromMemory(buf.data(), buf.size());
}

bool GbCore::RewindStep() {
    if (!impl->core || !impl->rewind_inited) {
        return false;
    }
    return mCoreRewindRestore(&impl->rewind_ctx, impl->core, 1);
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
