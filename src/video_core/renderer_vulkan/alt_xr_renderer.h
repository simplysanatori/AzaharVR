// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstdint>
#include <memory>

namespace Vulkan {

struct AltScreenImage {
    const uint32_t* pixels = nullptr; ///< RGBA8888 (bytes R, G, B, A) or BGRA8888 if bgra=true
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride_pixels = 0;       ///< 0 means tightly packed (= width)
    bool bgra = false;                ///< True for melonDS BGRA8888 buffers
};

/**
 * Lightweight Vulkan XR presenter for non-3DS cores (GBA, GB/GBC, DS/DSi).
 * Adopts the process's OpenXR Vulkan device via VideoCore::GetVulkanHooks(),
 * creates its own Vulkan::Instance, Scheduler, and XrPresentTarget, uploads CPU
 * screen buffers, presents into the XR swapchains (Primary on TopLeft, optional
 * Secondary on Bottom), and bumps Common::XrPacer::Get().game_swaps.
 */
class AltXrRenderer {
public:
    AltXrRenderer();
    ~AltXrRenderer();

    AltXrRenderer(const AltXrRenderer&) = delete;
    AltXrRenderer& operator=(const AltXrRenderer&) = delete;

    /// Initializes Vulkan resources against VideoCore::GetVulkanHooks() and
    /// VideoCore::GetXrScreenSink(). Returns false if XR is inactive/unavailable.
    bool Init();

    /// Tears down Vulkan resources, clears SetScreensLive(false) and SetQueueMutex(nullptr).
    void Shutdown();

    /// Uploads and submits one frame to the XR swapchains, bumping game_swaps.
    /// `secondary` may have pixels == nullptr when no secondary screen is active.
    bool Present(const AltScreenImage& primary, const AltScreenImage& secondary);

    bool IsReady() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Vulkan
