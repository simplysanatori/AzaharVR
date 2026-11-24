// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/alt_core_types.h"
#include "video_core/renderer_vulkan/alt_xr_renderer.h"

namespace FrameCores {

struct CorePaths {
    std::string data_dir;  ///< Root user data folder
    std::string save_dir;  ///< Battery saves directory (e.g. <data_dir>/gba/saves)
    std::string state_dir; ///< Save states directory (e.g. <data_dir>/gba/states)
    std::string bios_dir;  ///< BIOS / firmware / DSi NAND directory
    std::string cheat_dir; ///< Cheats directory
};

struct CoreBootOptions {
    bool force_dsi = false;        ///< Force DSi console mode (DS titles)
    bool force_nds = false;        ///< Force classic DS mode even for DSi-enhanced titles
    bool jit_enabled = true;       ///< Enable ARM JIT (melonDS)
    bool jit_fastmem = true;       ///< Enable JIT fast memory (melonDS)
    bool threaded_3d = true;       ///< Enable threaded 3D software renderer (melonDS)
    bool use_bios = true;          ///< Auto-load optional user BIOS/firmware if present
    bool color_correction = true;  ///< Enable GB/GBC/GBA colour correction
    bool enable_mapcap = true;     ///< Enable gba-ds secondary-screen map capture
    bool enable_rewind = true;     ///< Enable rewind ring buffer (GBA / GB / GBC)
    std::size_t rewind_entries = 300;
    std::string gba_slot_rom;      ///< Optional Slot-2 GBA ROM path (DS mode)
    bool rumble_pak = false;       ///< Insert DS Rumble Pak in Slot-2 when no GBA ROM
};

struct CheatEntry {
    std::string name;
    std::string code;
    bool enabled = false;
};

struct FrameOutput {
    Vulkan::AltScreenImage primary{};
    Vulkan::AltScreenImage secondary{};
    std::vector<int16_t> audio_pcm; ///< Interleaved stereo PCM16 (L, R, L, R, ...)
    float rumble = 0.0f;            ///< Haptic motor intensity [0.0, 1.0]
    bool metrics_changed = false;
    Common::AltCore::ScreenMetrics updated_metrics{};
};

/// Internal emulation backend interface implemented by GbaCore, GbCore, and NdsCore.
class CoreBackend {
public:
    virtual ~CoreBackend() = default;

    virtual bool Load(const std::string& rom_path, const CorePaths& paths,
                      const CoreBootOptions& opts) = 0;
    virtual void Unload() = 0;
    virtual void Reset() = 0;

    virtual void SetInput(const Common::AltCore::InputState& input) = 0;
    virtual void RunFrame(FrameOutput& out) = 0;

    virtual double NominalFps() const = 0;
    virtual uint32_t AudioSampleRate() const = 0;
    virtual Common::AltCore::Capabilities GetCapabilities() const = 0;

    virtual bool SaveStateToMemory(std::vector<uint8_t>& out) = 0;
    virtual bool LoadStateFromMemory(const uint8_t* data, std::size_t size) = 0;
    virtual bool SaveStateToFile(const std::string& path) = 0;
    virtual bool LoadStateFromFile(const std::string& path) = 0;

    virtual bool RewindStep() {
        return false;
    }
    virtual bool ImportBatterySave(const std::string& /*sav_path*/) {
        return false;
    }
    virtual void FlushBatterySave() {}

    // Cheats API
    virtual std::vector<CheatEntry> ListCheats() const {
        return {};
    }
    virtual bool SetCheatEnabled(std::size_t /*idx*/, bool /*on*/) {
        return false;
    }
    virtual bool AddCheat(const std::string& /*name*/, const std::string& /*code*/) {
        return false;
    }

    // Core-specific controls
    virtual void SetSolarLevel(int /*level*/) {}
    virtual void SetMicBlow(bool /*on*/) {}
    virtual void SetLidClosed(bool /*closed*/) {}
    virtual void SetTouchEnabled(bool /*on*/) {}
    virtual void SetScreenRotation(int /*deg*/) {}
    virtual bool SetGbaSlotRom(const std::string& /*path*/) {
        return false;
    }
    virtual void SetConsoleMode(bool /*dsi*/) {}
};

} // namespace FrameCores
