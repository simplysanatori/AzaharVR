// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstdint>
#include <memory>

#include "common/layer_depth.h"
#include "frame_cores/frame_driver.h"

struct mCore;

namespace FrameCores {

/// The two eyes' pictures of a GBA game (Layer Depth): a software renderer for each that draws each
/// line from what the game's has as it draws it, with the layers moved. They sit below whatever a
/// driver hooks above them, so a layer or sprite the driver leaves out is left out here too.
class GbaEyes {
public:
    GbaEyes();
    ~GbaEyes();

    GbaEyes(const GbaEyes&) = delete;
    GbaEyes& operator=(const GbaEyes&) = delete;

    /// Before a driver is made: one that hooks the renderer keeps what it finds there. False: the
    /// game is not drawn by mGBA's software renderer.
    bool Attach(mCore* game);
    /// After the driver is gone, before the core is.
    void Detach();

    /// 0: nothing is drawn. Up to 3.
    void SetDepth(uint32_t level);
    /// Common::LayerDepth::MaxShift; 0: nothing is drawn.
    void SetMaxShift(float share);

    /// Around the game's own frame. Frames a driver runs to draw one again are not drawn here.
    void BeginFrame();
    void EndFrame();

    /// Of the last frame, until the next. None while off.
    DriverPicture Picture(Common::LayerDepth::Eye eye) const;
    /// The game's, as its renderer drew it: without what a driver painted on it since.
    DriverPicture GamePicture() const;
    /// FrameOutput::layer_shift for the two.
    float EdgeShift() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace FrameCores
