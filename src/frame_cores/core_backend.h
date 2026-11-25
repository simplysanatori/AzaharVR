// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "common/alt_core_types.h"
#include "frame_cores/frame_driver.h"
#include "frame_cores/microphone.h"
#include "video_core/renderer_vulkan/alt_xr_renderer.h"

namespace melonDS {
class NDS;
}
namespace Vulkan {
struct AltGraphics;
}

namespace FrameCores {

struct NdsHiresRenderer;

struct CorePaths {
    std::string data_dir;  ///< Root user data folder
    std::string game_dir;  ///< The running game's own folder (GameFolder): save, states, cheats
    std::string bios_dir;  ///< BIOS / firmware / DSi NAND directory
    std::string cheat_dir; ///< Shared cheat files the player adds (.cht, .mch)
};

/// Tests: the driver of a GBA or Game Boy game, instead of the one chosen for it.
struct DriverOverride {
    DriverKind kind = DriverKind::Plain;
    std::function<std::unique_ptr<FrameDriver>(const DriverServices&)> make;
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
    std::string gba_slot_rom;      ///< Optional Slot-2 GBA ROM path (DS mode)
    bool rumble_pak = false;       ///< Insert DS Rumble Pak in Slot-2 when no GBA ROM
    uint32_t second_screen = 0;    ///< SecondScreen (GBA and Game Boy games)
    uint32_t layer_depth = 0;      ///< 0 off, 1 to 3: how far back the layers go
    DriverOverride driver;
    /// Tests: what draws a DS game over 1x (NdsHiresMaker)
    std::function<std::unique_ptr<NdsHiresRenderer>(melonDS::NDS&, std::string&)> ds_renderer;
};

struct CheatEntry {
    std::string name;
    std::string code;
    bool enabled = false;
};

struct FrameOutput {
    Vulkan::AltScreenImage primary{};
    Vulkan::AltScreenImage secondary{};
    /// The Primary Screen for the right eye (Layer Depth); none: both eyes see `primary`. The same
    /// size as `primary`.
    Vulkan::AltScreenImage primary_right{};
    float layer_shift = 0.0f; ///< its farthest layer's, a share of the width (layer_depth.h)
    /// The same for the Secondary Screen, from a core asked for it (NdsCore::SetDepthOnSecondary).
    /// No host shows it yet: the headset has no right eye's picture of the Secondary Screen.
    Vulkan::AltScreenImage secondary_right{};
    Vulkan::AltScreenImage overlay{}; ///< over the whole Primary Screen, alpha premultiplied
    std::vector<int16_t> audio_pcm; ///< Interleaved stereo PCM16 (L, R, L, R, ...)
    float rumble = 0.0f;            ///< Haptic motor intensity [0.0, 1.0]
    bool held = false;              ///< No frame ran: the driver holds the game (DriverFrame)
    bool metrics_changed = false;
    Common::AltCore::ScreenMetrics updated_metrics{};
    bool options_changed = false; ///< Capabilities::driver_options, driver_options_on
    uint32_t updated_options = 0;
    uint32_t updated_options_on = 0;
    bool caps_changed = false; ///< GetCapabilities gives others than before the frame
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
    /// Of the last frame's sound: a game may change it at any time.
    virtual uint32_t AudioSampleRate() const = 0;
    virtual Common::AltCore::Capabilities GetCapabilities() const = 0;

    virtual bool SaveStateToMemory(std::vector<uint8_t>& out) = 0;
    virtual bool LoadStateFromMemory(const uint8_t* data, std::size_t size) = 0;

    /// False: a state made or loaded now would catch the game half changed.
    virtual bool StateSafeNow() const {
        return true;
    }
    /// After LoadStateFromMemory, whether it could load it or not.
    virtual void AfterStateLoad() {}

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
    /// Downloaded cheats (cheat_db.h): added off, those already there left out, saved with the
    /// game's own. Returns how many were added.
    virtual int ImportCheats(const std::vector<CheatEntry>& /*cheats*/) {
        return 0;
    }

    // Core-specific controls
    virtual void SetSolarLevel(int /*level*/) {}
    /// Allow Game Editing (GBA and Game Boy games).
    virtual void SetEditing(bool /*on*/) {}
    /// Layer Depth (0 to 3), from the next frame.
    virtual void SetLayerDepth(uint32_t /*level*/) {}
    /// The farthest a layer may move from the next frame on (layer_depth.h). 0: no right picture.
    virtual void SetMaxLayerShift(float /*share*/) {}
    /// Internal Resolution from the next frame on, and the highest it may become by Dynamic
    /// Resolution (has_internal_resolution).
    virtual void SetResolution(uint32_t /*factor*/, uint32_t /*ceiling*/) {}
    /// The presenter's graphics, to draw with over 1x; null: they go now.
    virtual void SetGraphics(const Vulkan::AltGraphics* /*graphics*/) {}
    /// The pixels of pictures that are the GPU's (AltScreenImage::image) into `frame`.
    virtual void ReadPictures(FrameOutput& /*frame*/) {}
    /// The Secondary Screen's next page (GBA games), from the next frame.
    virtual void TurnPage() {}
    /// An option of the driver's (GBA and Game Boy games), between frames.
    virtual bool SetDriverOption(Common::AltCore::DriverOption /*option*/, uint32_t /*value*/) {
        return false;
    }
    virtual void SetMicBlow(bool /*on*/) {}
    /// DS/DSi: the microphone's input, before Load.
    virtual void SetMicrophone(MicrophoneFactory /*factory*/) {}
    /// Its settings changed (any thread).
    virtual void ReloadMicrophone() {}
    /// The game is paused: nothing listens until it goes on.
    virtual void CloseMicrophone() {}
    virtual void SetLidClosed(bool /*closed*/) {}
    virtual void SetTouchEnabled(bool /*on*/) {}
    virtual void SetScreenRotation(int /*deg*/) {}
    virtual bool SetGbaSlotRom(const std::string& /*path*/) {
        return false;
    }
    virtual void SetConsoleMode(bool /*dsi*/) {}
};

} // namespace FrameCores
