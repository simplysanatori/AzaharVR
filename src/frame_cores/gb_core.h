// Adapter for mGBA (endrift, MPL-2.0)
// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>

#include "frame_cores/core_backend.h"

namespace FrameCores {

/**
 * Game Boy / Game Boy Color emulation backend directly wrapping libmgba's mCore
 * (GBCoreCreate). Independent of gba-ds statics; provides 160x144 output, GBC/SGB
 * colour palettes, stereo audio, battery saves, save states, and cartridge
 * rumble.
 */
class GbCore final : public CoreBackend {
public:
    GbCore();
    ~GbCore() override;

    GbCore(const GbCore&) = delete;
    GbCore& operator=(const GbCore&) = delete;

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
    bool SaveStateToFile(const std::string& path) override;
    bool LoadStateFromFile(const std::string& path) override;

    bool ImportBatterySave(const std::string& sav_path) override;

    std::vector<CheatEntry> ListCheats() const override;
    bool SetCheatEnabled(std::size_t idx, bool on) override;
    bool AddCheat(const std::string& name, const std::string& code) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace FrameCores
