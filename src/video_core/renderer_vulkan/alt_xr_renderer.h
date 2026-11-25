// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstdint>
#include <memory>

namespace Frontend {
class EmuWindow;
}

namespace Vulkan {

struct AltScreenImage {
    const uint32_t* pixels = nullptr; ///< RGBA8888 (bytes R, G, B, A) or BGRA8888 if bgra=true
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride_pixels = 0;       ///< 0 means tightly packed (= width)
    bool bgra = false;                ///< True for melonDS BGRA8888 buffers

    bool HasPicture() const {
        return pixels != nullptr && width > 0 && height > 0;
    }
};

/// The right eye has a picture of its own, which can be drawn just as the left eye's.
inline bool AltShowsStereo(const AltScreenImage& left, const AltScreenImage& right) {
    return right.HasPicture() && right.width == left.width && right.height == left.height;
}

/**
 * Lightweight Vulkan presenter for non-3DS cores (GBA, GB/GBC, DS/DSi), in VR and in the window.
 * Adopts the process's OpenXR Vulkan device via VideoCore::GetVulkanHooks() if there is one,
 * creates its own Vulkan::Instance and Scheduler, and uploads CPU screen buffers. While the
 * headset shows the screens it presents into the XR swapchains (Primary on TopLeft, optional
 * Secondary on Bottom, the Primary's right eye on TopRight while it has one) and bumps
 * Common::XrPacer::Get().game_swaps; otherwise into the window (AltWindowTarget). It picks per
 * frame, as RendererVulkan does, so a game goes on over a switch.
 */
class AltXrRenderer {
public:
    AltXrRenderer();
    ~AltXrRenderer();

    AltXrRenderer(const AltXrRenderer&) = delete;
    AltXrRenderer& operator=(const AltXrRenderer&) = delete;

    /// `window` (and `secondary_window`, for Separate Windows) must outlive this; without one the
    /// screens show in VR only. False if neither output can be used.
    bool Init(Frontend::EmuWindow* window = nullptr,
              Frontend::EmuWindow* secondary_window = nullptr);

    /// Tears down Vulkan resources, clears SetScreensLive(false) and SetQueueMutex(nullptr).
    void Shutdown();

    /// The output changed since the last Present (VR or flat, the window's size, the right eye's
    /// picture turned off from outside): a paused game is to be presented again.
    bool NeedsPresent() const;

    /// Uploads and submits one frame. `secondary` may have pixels == nullptr when no secondary
    /// screen is active. `overlay` (premultiplied alpha, RGBA) covers the whole Primary Screen as
    /// it is shown, whatever its size; without a picture nothing is drawn for it. `primary_right`
    /// (the size of `primary`, the overlay over it too) is the headset's right eye's; without a
    /// picture both eyes see `primary`. The window never shows it. With `skippable` (fast forward)
    /// the window may miss frames its display could not show anyway.
    bool Present(const AltScreenImage& primary, const AltScreenImage& secondary,
                 const AltScreenImage& overlay = {}, const AltScreenImage& primary_right = {},
                 bool skippable = false);

    bool IsReady() const;

    /// The headset shows the screens now, not the window.
    bool ShowsInVr() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Vulkan
