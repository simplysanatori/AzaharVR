// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "common/alt_core_types.h"
#include "frame_cores/core_backend.h"
#include "frame_cores/frame_driver.h"
#include "frame_cores/rom_info.h"

namespace FrameCores {

/// Copies a battery save to "<save>.before-edit-1"; the two copies before it become -2 and -3.
/// True: copied, or there is no save yet. On false the earlier copies are all there.
bool BackUpSave(const std::string& save_path);

/// What GbaCore and GbCore share: their driver, the count of its frames, and what the core's
/// capabilities and output take from it.
class DriverRunner {
public:
    /// `gbads_known`: gba-ds has a Secondary Screen for this game.
    static DriverKind Choose(const CoreBootOptions& opts, const std::string& rom_path,
                             bool gbads_known);

    /// Makes the driver. `services` has the core's own functions; the count, backup_save and
    /// map_capture are added here. Sets what `caps` has from the driver.
    void Start(DriverKind kind, DriverServices services, const RomInfo& info,
               const CoreBootOptions& opts, const std::string& save_path,
               Common::AltCore::Capabilities& caps);
    /// Before the core goes away.
    void Stop();

    /// One frame. A new size of the Secondary Screen goes to `caps` and `out`.
    void RunFrame(const Common::AltCore::InputState& input, FrameOutput& out,
                  Common::AltCore::Capabilities& caps);
    void Reset();
    bool StateSafeNow() const;
    void AfterStateLoad();

    /// Calls of RunFrame that did not step exactly once (a step outside RunFrame is one too).
    uint64_t Missteps() const {
        return missteps;
    }

private:
    bool UpdateSecondary(Common::AltCore::Capabilities& caps);
    void NoteMisstep(uint32_t stepped, const char* where);

    std::unique_ptr<FrameDriver> driver;
    DriverFrame frame;
    std::pair<uint32_t, uint32_t> secondary_size{};
    std::atomic<uint32_t> steps{0}; ///< a driver may step on a thread of its own
    uint64_t missteps = 0;
    bool backed_up = false; ///< this session
};

} // namespace FrameCores
