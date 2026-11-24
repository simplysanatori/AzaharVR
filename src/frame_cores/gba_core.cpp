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

extern "C" {
#include "cheats.h"
#include "config.h"
#include "emu.h"
#include "mapcap.h"
#include "patch.h"
#include "profiles.h"
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

    uint32_t audio_rate = 32768;
    uint32_t keys = 0;
    uint64_t frame_count = 0;

    // Framebuffers with alpha guaranteed to 0xFF (RGBA8888, bytes R,G,B,A)
    std::vector<uint32_t> primary_rgba;
    std::vector<uint32_t> secondary_rgba;
    std::vector<int16_t> audio_scratch;

    // Map capture state (gba-ds mapcap)
    MapCapture* mapcap = nullptr;
    MapResult map_res{};
    uint32_t last_map_serial = 0;
    uint32_t last_room_key = 0;
    uint64_t last_map_req_frame = 0;
    uint64_t anim_frame_counter = 0;
    bool has_map_image = false;
    bool secondary_active = false;
    uint32_t sec_w = 0;
    uint32_t sec_h = 0;

    // Cheats state (gba-ds .cht loader + runtime user-added cheats)
    Cheats cheats{};
    std::vector<CheatEntry> custom_cheats;
    std::string custom_cheats_path; ///< where custom_cheats are kept

    Common::AltCore::Capabilities caps = Common::AltCore::Capabilities::ForGBA();

    void FreeMapResultBuffers() {
        for (int i = 0; i < MAP_MAX_FRAMES; ++i) {
            if (map_res.frames[i]) {
                std::free(map_res.frames[i]);
                map_res.frames[i] = nullptr;
            }
        }
        map_res = MapResult{};
        last_map_serial = 0;
        has_map_image = false;
        secondary_active = false;
        sec_w = 0;
        sec_h = 0;
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
    impl->frame_count = 0;
    impl->keys = 0;

    const unsigned rate = emu_audio_rate(&impl->emu);
    impl->audio_rate = rate > 0 ? rate : 32768u;
    impl->primary_rgba.assign(kGbaWidth * kGbaHeight, 0xFF000000u);
    impl->audio_scratch.resize(4096u * 2u, 0);

    impl->caps.has_solar_sensor = emu_has_solar(&impl->emu);

    // Shadow-instance map capture (gba-ds profiles)
    if (opts.enable_mapcap && impl->emu.gameCode[0] != '\0') {
        impl->mapcap = mapcap_create(rom_path.c_str(), impl->emu.gameCode, true);
        if (impl->mapcap) {
            const MapProfile* prof = mapcap_profile(impl->mapcap);
            int mw = 0;
            int mh = 0;
            mapcap_size(impl->mapcap, &mw, &mh);
            if (prof && !prof->logoOnly && mw > 0 && mh > 0) {
                impl->sec_w = static_cast<uint32_t>(mw);
                impl->sec_h = static_cast<uint32_t>(mh);
                impl->secondary_active = true;
                impl->caps.screens.has_bottom = true;
                impl->caps.screens.bottom_aspect =
                    static_cast<float>(mw) / static_cast<float>(mh);
                impl->secondary_rgba.assign(impl->sec_w * impl->sec_h, 0xFF181818u);
                LOG_INFO(Core, "GbaCore: map capture enabled for {} ({}x{})",
                         mapcap_title(impl->mapcap), mw, mh);
            } else {
                mapcap_destroy(impl->mapcap);
                impl->mapcap = nullptr;
            }
        }
    }

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
             impl->audio_rate);
    return true;
}

void GbaCore::Unload() {
    if (!impl) {
        return;
    }
    if (impl->mapcap) {
        mapcap_cancel(impl->mapcap);
        mapcap_destroy(impl->mapcap);
        impl->mapcap = nullptr;
    }
    impl->FreeMapResultBuffers();

    cheats_free(&impl->cheats);
    impl->custom_cheats.clear();

    if (impl->loaded) {
        emu_close(&impl->emu);
        impl->loaded = false;
    }
    impl->primary_rgba.clear();
    impl->secondary_rgba.clear();
    impl->frame_count = 0;
}

void GbaCore::Reset() {
    if (!impl->loaded) {
        return;
    }
    if (impl->mapcap) {
        mapcap_cancel(impl->mapcap);
    }
    emu_reset(&impl->emu);
    impl->frame_count = 0;
    impl->last_room_key = 0;
    impl->last_map_req_frame = 0;
    impl->has_map_image = false;
    impl->ApplyAllCheats();
}

void GbaCore::SetInput(const Common::AltCore::InputState& input) {
    // Common::AltCore::ButtonBit bits 0..9 match gba-ds K_A..K_L 1:1.
    impl->keys = input.buttons & 0x3FFu;
}

void GbaCore::RunFrame(FrameOutput& out) {
    out.primary = {};
    out.secondary = {};
    out.audio_pcm.clear();
    out.rumble = 0.0f;
    out.metrics_changed = false;

    if (!impl->loaded) {
        return;
    }

    emu_frame(&impl->emu, impl->keys);
    ++impl->frame_count;

    // Copy XBGR8888 (bytes R,G,B,X) to opaque RGBA8888 (bytes R,G,B,0xFF)
    const uint32_t* src_fb = impl->emu.fb;
    if (src_fb) {
        constexpr std::size_t kPixels = static_cast<std::size_t>(kGbaWidth) * kGbaHeight;
        for (std::size_t i = 0; i < kPixels; ++i) {
            impl->primary_rgba[i] = src_fb[i] | 0xFF000000u;
        }
        out.primary.pixels = impl->primary_rgba.data();
        out.primary.width = kGbaWidth;
        out.primary.height = kGbaHeight;
        out.primary.stride_pixels = kGbaWidth;
        out.primary.bgra = false;
    }

    // Drain audio buffer
    const size_t max_frames = impl->audio_scratch.size() / 2u;
    const size_t got = emu_audio_read(&impl->emu, impl->audio_scratch.data(), max_frames);
    if (got > 0) {
        out.audio_pcm.assign(impl->audio_scratch.data(), impl->audio_scratch.data() + got * 2u);
    }

    out.rumble = std::clamp(emu_rumble_level(&impl->emu), 0.0f, 1.0f);

    // Drive gba-ds shadow-instance map capture if a profile matched
    if (impl->mapcap) {
        if (mapcap_poll(impl->mapcap, impl->last_map_serial, &impl->map_res)) {
            impl->last_map_serial = impl->map_res.serial;
            impl->has_map_image = impl->map_res.ok && impl->map_res.w > 0 && impl->map_res.h > 0;
            impl->anim_frame_counter = 0;

            if (impl->has_map_image) {
                const auto mw = static_cast<uint32_t>(impl->map_res.w);
                const auto mh = static_cast<uint32_t>(impl->map_res.h);
                if (!impl->secondary_active || mw != impl->sec_w || mh != impl->sec_h) {
                    impl->sec_w = mw;
                    impl->sec_h = mh;
                    impl->secondary_active = true;
                    impl->caps.screens.has_bottom = true;
                    impl->caps.screens.bottom_aspect =
                        static_cast<float>(mw) / static_cast<float>(mh);
                    impl->secondary_rgba.resize(static_cast<std::size_t>(mw) * mh, 0xFF000000u);
                    out.metrics_changed = true;
                    out.updated_metrics = impl->caps.screens;
                }
            }
        }

        if (mapcap_in_menu(impl->mapcap, &impl->emu)) {
            impl->has_map_image = false;
        } else if (!mapcap_busy(impl->mapcap) && mapcap_live_ok(impl->mapcap, &impl->emu)) {
            const MapProfile* prof = mapcap_profile(impl->mapcap);
            const uint64_t interval = (prof && prof->interval > 0) ? static_cast<uint64_t>(prof->interval) : 120u;
            const uint32_t room_key = mapcap_room_key(impl->mapcap, &impl->emu);
            const bool room_changed = (room_key != 0 && room_key != impl->last_room_key);
            const uint64_t since_req = impl->frame_count - impl->last_map_req_frame;

            const bool should_request =
                (!impl->has_map_image && impl->frame_count >= 30 && since_req >= 30) ||
                (room_changed && since_req >= 15) || (since_req >= interval);

            if (should_request) {
                if (mapcap_request(impl->mapcap, &impl->emu)) {
                    impl->last_map_req_frame = impl->frame_count;
                    if (room_key != 0) {
                        impl->last_room_key = room_key;
                    }
                }
            }
        }

        if (impl->has_map_image && impl->map_res.ok && impl->map_res.nFrames > 0) {
            int idx = 0;
            if (impl->map_res.nFrames > 1) {
                const uint64_t step =
                    impl->map_res.pages
                        ? 180u
                        : static_cast<uint64_t>(std::max(1, impl->map_res.frameStep));
                idx = static_cast<int>((impl->anim_frame_counter / step) %
                                       static_cast<uint64_t>(impl->map_res.nFrames));
                ++impl->anim_frame_counter;
            }
            const uint32_t* map_px = impl->map_res.frames[idx];
            if (map_px) {
                const std::size_t n = static_cast<std::size_t>(impl->sec_w) * impl->sec_h;
                if (impl->secondary_rgba.size() != n) {
                    impl->secondary_rgba.resize(n);
                }
                for (std::size_t i = 0; i < n; ++i) {
                    impl->secondary_rgba[i] = map_px[i] | 0xFF000000u;
                }
                out.secondary.pixels = impl->secondary_rgba.data();
                out.secondary.width = impl->sec_w;
                out.secondary.height = impl->sec_h;
                out.secondary.stride_pixels = impl->sec_w;
                out.secondary.bgra = false;
            }
        }
    }
}

uint32_t GbaCore::AudioSampleRate() const {
    return impl->audio_rate;
}

Common::AltCore::Capabilities GbaCore::GetCapabilities() const {
    return impl->caps;
}

bool GbaCore::SaveStateToMemory(std::vector<uint8_t>& out) {
    if (!impl->loaded) {
        return false;
    }
    const size_t sz = emu_state_size(&impl->emu);
    if (sz == 0) {
        return false;
    }
    out.resize(sz);
    if (!emu_state_save(&impl->emu, out.data())) {
        out.clear();
        return false;
    }
    return true;
}

bool GbaCore::LoadStateFromMemory(const uint8_t* data, std::size_t size) {
    if (!impl->loaded || !data || size == 0) {
        return false;
    }
    const size_t expected = emu_state_size(&impl->emu);
    if (expected == 0 || size < expected) {
        return false;
    }
    if (impl->mapcap) {
        mapcap_cancel(impl->mapcap);
    }
    const bool ok = emu_state_load(&impl->emu, data);
    if (ok) {
        impl->last_room_key = 0;
        impl->last_map_req_frame = 0;
    }
    return ok;
}

bool GbaCore::SaveStateToFile(const std::string& path) {
    if (!impl->loaded || path.empty()) {
        return false;
    }
    EnsureDir(std::filesystem::path(path).parent_path().string());
    const std::string tmp = path + ".tmp";
    if (!emu_state_save_file(&impl->emu, tmp.c_str())) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return false;
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

bool GbaCore::LoadStateFromFile(const std::string& path) {
    if (!impl->loaded || path.empty()) {
        return false;
    }
    if (impl->mapcap) {
        mapcap_cancel(impl->mapcap);
    }
    const bool ok = emu_state_load_file(&impl->emu, path.c_str());
    if (ok) {
        impl->last_room_key = 0;
        impl->last_map_req_frame = 0;
    }
    return ok;
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
