// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <filesystem>
#include <string>

#include "frame_cores/core_backend.h"
#include "frame_cores/nds_depth.h"

namespace FrameCores {

/// Configures the directory used by melonDS::Platform::GetLocalFilePath / OpenLocalFile
/// for relative asset paths (e.g. <data_dir>/ds/). Thread-safe.
void SetMelonDSLocalDir(const std::string& dir);

/// The DSi storage games write to: a copy of the player's dsi_nand.bin, which stays as it is. A
/// changed original replaces the copy; the old copy is kept as .bak. Empty: no original, or the copy
/// failed.
std::string PrepareWorkingNand(const std::filesystem::path& bios_dir);

/// Whether Automatic (or the player's choice) starts a game as a DSi. A DSi-enhanced game is a DS
/// game too: it is one unless the player's DSi files are there, as most do not start without.
constexpr bool PicksDsi(bool dsi_only, bool dsi_enhanced, bool has_dsi_files,
                        const CoreBootOptions& opts) {
    if (opts.force_dsi || opts.force_nds) {
        return opts.force_dsi;
    }
    return dsi_only || (dsi_enhanced && has_dsi_files);
}

/**
 * Nintendo DS / DSi emulation backend wrapping melonDS master.
 * Supports FreeBIOS direct boot with zero external dumps required, optional
 * user BIOS/firmware/DSi NAND auto-discovery, ARM JIT, threaded software 3D,
 * touch + rotation (Book Mode), mic blow synthesis, lid close, Action Replay
 * cheats, GBA Slot-2 ROMs & Rumble Pak, and atomic battery/state persistence.
 */
class NdsCore final : public CoreBackend {
public:
    NdsCore();
    ~NdsCore() override;

    NdsCore(const NdsCore&) = delete;
    NdsCore& operator=(const NdsCore&) = delete;

    bool Load(const std::string& rom_path, const CorePaths& paths,
              const CoreBootOptions& opts) override;
    void Unload() override;
    void Reset() override;

    void SetInput(const Common::AltCore::InputState& input) override;
    void RunFrame(FrameOutput& out) override;

    double NominalFps() const override {
        return 59.8261;
    }
    uint32_t AudioSampleRate() const override {
        return 32768;
    }
    Common::AltCore::Capabilities GetCapabilities() const override;

    bool SaveStateToMemory(std::vector<uint8_t>& out) override;
    bool LoadStateFromMemory(const uint8_t* data, std::size_t size) override;

    bool ImportBatterySave(const std::string& sav_path) override;
    void FlushBatterySave() override;

    std::vector<CheatEntry> ListCheats() const override;
    bool SetCheatEnabled(std::size_t idx, bool on) override;
    bool AddCheat(const std::string& name, const std::string& code) override;
    int ImportCheats(const std::vector<CheatEntry>& cheats) override;

    void SetMicBlow(bool on) override;
    void SetMicrophone(MicrophoneFactory factory) override;
    void ReloadMicrophone() override;
    void CloseMicrophone() override;
    void SetLidClosed(bool closed) override;
    void SetTouchEnabled(bool on) override;
    void SetScreenRotation(int deg) override;
    bool SetGbaSlotRom(const std::string& path) override;
    void SetConsoleMode(bool dsi) override;
    void SetLayerDepth(uint32_t level) override;
    void SetMaxLayerShift(float share) override;
    void SetDepthOn(uint32_t screens, bool by_slot = false) override;
    void SetDepthOnSecondary(bool on) override;

    /// Layer Depth: the w of the 3D scene that stands on its layer, for a game that is to have a
    /// fixed one; 0 (as it starts): each scene's own view (NdsDepth::ViewTrack).
    void SetDepthConvergence(float w);

    /// Layer Depth's, of the last frame.
    NdsDepth::Lines DepthLines() const;
    void SetResolution(uint32_t factor, uint32_t ceiling) override;
    void SetGraphics(const Vulkan::AltGraphics* graphics) override;
    void ReadPictures(FrameOutput& frame) override;

    // Platform callback entrypoints routed from melonds_platform.cpp via userdata.
    void OnSignalStop(int reason);
    void OnWriteNDSSave(const uint8_t* data, uint32_t len, uint32_t offset, uint32_t write_len);
    void OnWriteGBASave(const uint8_t* data, uint32_t len, uint32_t offset, uint32_t write_len);
    void OnWriteFirmware(const uint8_t* data, uint32_t len, uint32_t offset, uint32_t write_len);
    void OnMicStart();
    void OnMicStop();
    int OnMicReadInput(int16_t* data, int max_length);
    void OnRumbleStart(uint32_t duration_ms);
    void OnRumbleStop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace FrameCores
