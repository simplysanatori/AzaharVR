// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiupowers: the layers of the powers and the dots (vis/power_menu.h). The powers are
// a part of the bottom screen's image (video_core/kiu_powers.h), the dots an image of their own.

#pragma once

#include <vector>

#include "frame_xr/frame_xr.h"
#include "frame_xr/vis/async_overlay.h"
#include "frame_xr/vis/power_menu.h"
#include "frame_xr/xr_includes.h"

namespace FrameXr {

class Runtime;

class PowerLayers {
public:
    /// XR thread, before the session ends.
    void Destroy();

    struct In {
        bool playing = false;      ///< the game's screens are live, first person's screen head-driven
        bool bottom_shown = false; ///< the Bottom Screen is on view
        bool bottom_persistent = false;
        XrPosef top_pose[2]{{{0, 0, 0, 1}, {0, 0, 0}}, {{0, 0, 0, 1}, {0, 0, 0}}};
        XrExtent2Df top_size{0.0f, 0.0f};
        bool top_in_view = false; ///< VIEW space, else LOCAL
        ViewPose head;
        XrTime time = 0;
        // The bottom screen's image on view (none: no handle).
        XrSwapchain bottom = XR_NULL_HANDLE;
        uint32_t bottom_width = 0, bottom_height = 0;
    };
    /// In LOCAL space. A quad with a swapchain and `visible` off only keeps its place in the list
    /// (layer_slots.h); without a swapchain there is none.
    struct Out {
        Quad powers, dots;
    };
    Out Update(Runtime& rt, const In& in);

private:
    Vis::AsyncOverlay dots_chain_;
    bool dots_failed_ = false;
    uint32_t dots_key_ = UINT32_MAX;
    std::vector<uint8_t> pixels_;
    Vis::Powers::Show show_;
    bool aimed_ = false;
    float powers_width_ = 0.0f, dots_width_ = 0.0f; ///< over the hand, kept while up
    XrTime time_ = 0;
    bool logged_powers_ = false, logged_dots_ = false;
};

} // namespace FrameXr
