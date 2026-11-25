// Adapter for gba-ds by kyannon (GPL-3.0-only)
// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/gba_core.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "common/logging/log.h"
#include "frame_cores/cheat_store.h"
#include "frame_cores/driver_runner.h"
#include "frame_cores/second_screen.h"

extern "C" {
#include "cheats.h"
#include "config.h"
#include "emu.h"
#include "patch.h"
#include <mgba-util/vfs.h>
#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
}

namespace FrameCores {
namespace {

constexpr uint32_t kGbaWidth = GBA_W;
constexpr uint32_t kGbaHeight = GBA_H;

bool EnsureDir(const std::string& dir) {
    if (dir.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return !ec;
}

bool AtomicCopyFile(const std::string& src, const std::string& dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in) {
        return false;
    }
    const std::string tmp = dst + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << in.rdbuf();
        out.flush();
        if (!out.good()) {
            return false;
        }
    }
    std::error_code ec;
    if (std::filesystem::exists(dst, ec)) {
        std::filesystem::rename(dst, dst + ".bak", ec);
        ec.clear();
    }
    std::filesystem::rename(tmp, dst, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace

struct GbaCore::Impl {
    Emu emu{};
    bool loaded = false;

    std::string rom_path;
    std::string save_path;
    CorePaths paths{};
    CoreBootOptions opts{};

    Common::AltCore::InputState input{};
    DriverRunner driver;

    // Cheats state (gba-ds .cht loader + runtime user-added cheats)
    Cheats cheats{};
    std::vector<CheatEntry> custom_cheats;
    std::string custom_cheats_path; ///< where custom_cheats are kept

    Common::AltCore::Capabilities caps = Common::AltCore::Capabilities::ForGBA();

    void StartDriver() {
        DriverServices services;
        services.core = emu.core;
        services.emu = &emu;
        services.step = [this](uint32_t keys) { emu_frame(&emu, keys & 0x3FFu); };
        services.drain_audio = [this](int16_t* out, std::size_t max_frames) -> std::size_t {
            return emu_audio_read(&emu, out, max_frames);
        };
        services.rumble = [this] { return std::clamp(emu_rumble_level(&emu), 0.0f, 1.0f); };
        services.picture = [this] {
            return DriverPicture{emu.fb, kGbaWidth, kGbaHeight, kGbaWidth}; // XBGR8888
        };
        services.rom_path = rom_path;
        services.game_dir = paths.game_dir;

        RomInfo info;
        info.kind = Common::AltCore::CoreKind::GBA;
        info.game_code = emu.gameCode;
        const bool gbads_known = opts.enable_mapcap && HasTwoScreens(info.kind, info.game_code);
        const auto known = DriverRunner::WhatKnows(rom_path, gbads_known);
        driver.Start(DriverRunner::Choose(opts, known), known, std::move(services), info, opts,
                     save_path, caps);
    }

    void ApplyAllCheats() {
        if (!loaded) {
            return;
        }
        cheats_apply(&cheats, &emu);
        if (!custom_cheats.empty()) {
            std::vector<const char*> active_codes;
            active_codes.reserve(custom_cheats.size());
            for (const auto& entry : custom_cheats) {
                if (entry.enabled && !entry.code.empty()) {
                    active_codes.push_back(entry.code.c_str());
                }
            }
            if (!active_codes.empty()) {
                emu_cheats_add(&emu, "Custom", active_codes.data(),
                               static_cast<int>(active_codes.size()), EMU_CHEAT_DETECT);
            }
        }
    }
};

GbaCore::GbaCore() : impl(std::make_unique<Impl>()) {}

GbaCore::~GbaCore() {
    Unload();
}

bool GbaCore::Load(const std::string& rom_path, const CorePaths& paths,
                   const CoreBootOptions& opts) {
    Unload();

    impl->rom_path = rom_path;
    impl->paths = paths;
    impl->opts = opts;
    impl->caps = Common::AltCore::Capabilities::ForGBA();

    EnsureDir(paths.game_dir);
    EnsureDir(paths.cheat_dir);

    const std::string stem = std::filesystem::path(rom_path).stem().string();
    impl->save_path = (std::filesystem::path(paths.game_dir) / (stem + ".sav")).string();

    // Optional GBA BIOS discovery in <bios_dir>/gba_bios.bin (else mGBA HLE BIOS).
    emu_bios_path[0] = '\0';
    if (opts.use_bios && !paths.bios_dir.empty()) {
        const std::array<std::filesystem::path, 2> candidates = {
            std::filesystem::path(paths.bios_dir) / "gba_bios.bin",
            std::filesystem::path(paths.bios_dir) / "bios" / "gba_bios.bin",
        };
        for (const auto& candidate : candidates) {
            const std::string s = candidate.string();
            if (emu_bios_check(s.c_str()) == BIOS_FILE_OK) {
                std::snprintf(emu_bios_path, sizeof(emu_bios_path), "%s", s.c_str());
                LOG_INFO(Core, "GbaCore: using GBA BIOS at {}", s);
                break;
            }
        }
    }

    if (!emu_open(&impl->emu, rom_path.c_str(), impl->save_path.c_str(), true)) {
        LOG_ERROR(Core, "GbaCore: emu_open failed for {}", rom_path);
        return false;
    }

    impl->loaded = true;
    impl->input = {};

    impl->caps.has_solar_sensor = emu_has_solar(&impl->emu);

    impl->StartDriver();

    // Load .cht cheats from <cheat_dir> and beside the ROM
    Config cheat_cfg{};
    const std::string rom_dir = std::filesystem::path(rom_path).parent_path().string();
    std::string dirs = paths.cheat_dir;
    if (!rom_dir.empty()) {
        if (!dirs.empty()) {
            dirs += ':';
        }
        dirs += rom_dir;
    }
    std::snprintf(cheat_cfg.cheatDir, sizeof(cheat_cfg.cheatDir), "%s", dirs.c_str());
    std::snprintf(cheat_cfg.assetDir, sizeof(cheat_cfg.assetDir), "%s", paths.bios_dir.c_str());
    const std::string prefs_path =
        (std::filesystem::path(paths.game_dir) / (stem + ".cheats")).string();
    cheats_load(&impl->cheats, &cheat_cfg, rom_path.c_str(), impl->emu.gameCode,
                prefs_path.c_str());
    impl->custom_cheats_path =
        (std::filesystem::path(paths.game_dir) / (stem + ".custom.cheats")).string();
    impl->custom_cheats = LoadCustomCheats(impl->custom_cheats_path);
    impl->ApplyAllCheats();

    LOG_INFO(Core, "GbaCore: loaded {} (code={}, rate={} Hz)", rom_path, impl->emu.gameCode,
             AudioSampleRate());
    return true;
}

void GbaCore::Unload() {
    if (!impl) {
        return;
    }
    impl->driver.Stop();

    cheats_free(&impl->cheats);
    impl->custom_cheats.clear();

    if (impl->loaded) {
        emu_close(&impl->emu);
        impl->loaded = false;
    }
}

void GbaCore::Reset() {
    if (!impl->loaded) {
        return;
    }
    emu_reset(&impl->emu);
    impl->ApplyAllCheats();
    impl->driver.Reset();
}

void GbaCore::SetInput(const Common::AltCore::InputState& input) {
    impl->input = input;
}

void GbaCore::RunFrame(FrameOutput& out) {
    impl->driver.RunFrame(impl->input, out, impl->caps);
}

uint32_t GbaCore::AudioSampleRate() const {
    // Games change it (SOUNDBIAS), most soon after the start
    const unsigned rate = impl->loaded ? emu_audio_rate(&impl->emu) : 0;
    return rate > 0 ? rate : 32768u;
}

Common::AltCore::Capabilities GbaCore::GetCapabilities() const {
    return impl->caps;
}

bool GbaCore::SaveStateToMemory(std::vector<uint8_t>& out) {
    if (!impl->loaded) {
        return false;
    }
    // With the save data and the clock, as emu_state_save_file: emu_state_save leaves them out.
    VFile* vf = VFileMemChunk(nullptr, 0);
    if (!vf) {
        return false;
    }
    bool ok = mCoreSaveStateNamed(impl->emu.core, vf,
                                  SAVESTATE_SAVEDATA | SAVESTATE_RTC | SAVESTATE_METADATA);
    const ssize_t size = ok ? vf->size(vf) : 0;
    out.resize(size > 0 ? static_cast<std::size_t>(size) : 0);
    ok = size > 0 && vf->seek(vf, 0, SEEK_SET) == 0 && vf->read(vf, out.data(), out.size()) == size;
    vf->close(vf);
    if (!ok) {
        out.clear();
    }
    return ok;
}

bool GbaCore::LoadStateFromMemory(const uint8_t* data, std::size_t size) {
    if (!impl->loaded || !data || size == 0) {
        return false;
    }
    const size_t expected = emu_state_size(&impl->emu);
    if (expected == 0 || size < expected) {
        return false;
    }
    VFile* vf = VFileFromConstMemory(data, size);
    if (!vf) {
        return false;
    }
    // As emu_state_load_file: the save file changes only when the game saves.
    const bool ok = mCoreLoadStateNamed(impl->emu.core, vf, SAVESTATE_RTC);
    vf->close(vf);
    return ok;
}

bool GbaCore::StateSafeNow() const {
    return impl->driver.StateSafeNow();
}

void GbaCore::AfterStateLoad() {
    impl->driver.AfterStateLoad();
}

void GbaCore::SetEditing(bool on) {
    impl->driver.SetEditing(on);
}

void GbaCore::TurnPage() {
    impl->driver.TurnPage();
}

bool GbaCore::SetDriverOption(Common::AltCore::DriverOption option, uint32_t value) {
    return impl->driver.SetOption(option, value, impl->caps);
}

bool GbaCore::ImportBatterySave(const std::string& sav_path) {
    if (!impl->loaded || sav_path.empty() || impl->save_path.empty()) {
        return false;
    }
    const std::string rom = impl->rom_path;
    const CorePaths paths = impl->paths;
    const CoreBootOptions opts = impl->opts;
    const std::string dst_save = impl->save_path;

    // Close core first so async save writer flushes and releases the save file.
    Unload();
    if (!AtomicCopyFile(sav_path, dst_save)) {
        LOG_ERROR(Core, "GbaCore: failed to copy imported save from {} to {}", sav_path, dst_save);
        Load(rom, paths, opts);
        return false;
    }
    return Load(rom, paths, opts);
}

std::vector<CheatEntry> GbaCore::ListCheats() const {
    std::vector<CheatEntry> list;
    list.reserve(static_cast<std::size_t>(impl->cheats.n) + impl->custom_cheats.size());
    for (int i = 0; i < impl->cheats.n; ++i) {
        const Cheat& ch = impl->cheats.items[i];
        list.push_back(CheatEntry{
            ch.desc,
            ch.code ? std::string(ch.code) : std::string{},
            ch.on,
        });
    }
    for (const auto& entry : impl->custom_cheats) {
        list.push_back(entry);
    }
    return list;
}

bool GbaCore::SetCheatEnabled(std::size_t idx, bool on) {
    const auto file_count = static_cast<std::size_t>(impl->cheats.n);
    if (idx < file_count) {
        impl->cheats.items[idx].on = on;
        cheats_save(&impl->cheats);
        impl->ApplyAllCheats();
        return true;
    }
    const std::size_t custom_idx = idx - file_count;
    if (custom_idx < impl->custom_cheats.size()) {
        impl->custom_cheats[custom_idx].enabled = on;
        SaveCustomCheats(impl->custom_cheats_path, impl->custom_cheats);
        impl->ApplyAllCheats();
        return true;
    }
    return false;
}

bool GbaCore::AddCheat(const std::string& name, const std::string& code) {
    if (!impl->loaded || code.empty()) {
        return false;
    }
    impl->custom_cheats.push_back(CheatEntry{
        name.empty() ? "Custom Cheat" : name,
        code,
        true,
    });
    SaveCustomCheats(impl->custom_cheats_path, impl->custom_cheats);
    impl->ApplyAllCheats();
    return true;
}

void GbaCore::SetSolarLevel(int level) {
    emu_solar_level = std::clamp(level, 0, 10);
}

} // namespace FrameCores
