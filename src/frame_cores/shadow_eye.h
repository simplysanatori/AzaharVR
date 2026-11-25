// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/layer_depth.h"
#include "frame_cores/frame_driver.h"

struct mCore;

namespace FrameCores {

/// The right eye's picture of a Game Boy game (Layer Depth): a second core that gets the game's
/// whole state every frame and draws the same frame with its layers moved. The game's core is only
/// read.
class ShadowEye {
public:
    ShadowEye() = default;
    ~ShadowEye();

    ShadowEye(const ShadowEye&) = delete;
    ShadowEye& operator=(const ShadowEye&) = delete;

    /// The game's state and ROM can be copied.
    static bool Fits(mCore* game);

    /// `game` has its ROM loaded and was reset; its picture is `width` by `height`. False: stays
    /// off.
    bool Start(mCore* game, uint32_t width, uint32_t height);
    /// Before the game's core goes away.
    void Stop();
    bool Started() const {
        return core != nullptr;
    }

    /// 0: nothing runs. Up to 3. Kept over Start and Stop, as the next.
    void SetDepth(uint32_t level);
    /// Common::LayerDepth::MaxShift; 0: nothing runs.
    void SetMaxShift(float share);

    /// The frame the game is about to run, with the keys it will get. Before the game's step.
    void Run(uint32_t keys);

    /// The frame of the last Run, until the next. None while off.
    DriverPicture Picture() const;
    /// Of that picture's farthest layer, a share of its width.
    float FarthestShift() const;

private:
    bool CopyGame();
    void ShiftLayers();
    int32_t BackgroundShift() const;
    void FailOnce(const char* what);

    mCore* game = nullptr;
    mCore* core = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 0;
    float max_shift = Common::LayerDepth::kNoLimit;
    float farthest_shift = 0.0f; ///< in `pixels`
    std::vector<uint32_t> pixels; ///< XBGR8888, alpha set after each frame
    std::vector<uint8_t> state;
    bool drawn = false;
    bool failed = false; ///< said so
};

} // namespace FrameCores
