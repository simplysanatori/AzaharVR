// Copyright 2026 AzaharVR contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <mutex>

#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

/// What AltXrRenderer draws with, for a core that draws on the GPU itself.
struct AltGraphics {
    vk::PhysicalDevice physical;
    vk::Device device;
    vk::Queue queue;
    u32 queue_family = 0;
    std::mutex* queue_mutex = nullptr; ///< held around every use of `queue`
};

} // namespace Vulkan
