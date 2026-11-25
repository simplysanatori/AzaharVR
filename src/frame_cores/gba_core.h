// Adapter for gba-ds by kyannon (GPL-3.0-only)
// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>

#include "frame_cores/core_backend.h"

namespace FrameCores {

/**
 * GBA emulation backend wrapping gba-ds (kyannon) on top of libmgba, including
 * shadow-instance map capture (mapcap), solar sensor, cartridge rumble,
 * ROM auto-patching, and RetroArch .cht cheats.
 */
class GbaCore final : public CoreBackend {
public:
    GbaCore();
    ~GbaCore() override;

    GbaCore(const GbaCore&) = delete;
    GbaCore& operator=(const GbaCore&) = delete;

    bool Load(const std::string& rom_path, const CorePaths& paths,
              const CoreBootOptions& opts) override;
    void Unload() override;
    void Reset() override;

    void SetInput(const Common::AltCore::InputState& input) override;
    void RunFrame(FrameOutput& out) override;

    double NominalFps() const override {
        return 59.7275;
    }
    uint32_t AudioSampleRate() const override;
    Common::AltCore::Capabilities GetCapabilities() const override;

    bool SaveStateToMemory(std::vector<uint8_t>& out) override;
    bool LoadStateFromMemory(const uint8_t* data, std::size_t size) override;
    bool StateSafeNow() const override;
    void AfterStateLoad() override;
    void SetEditing(bool on) override;
    void TurnPage() override;
    bool SetDriverOption(Common::AltCore::DriverOption option, uint32_t value) override;

    bool ImportBatterySave(const std::string& sav_path) override;

    std::vector<CheatEntry> ListCheats() const override;
    bool SetCheatEnabled(std::size_t idx, bool on) override;
    bool AddCheat(const std::string& name, const std::string& code) override;

    void SetSolarLevel(int level) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace FrameCores
