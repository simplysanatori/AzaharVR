// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "video_core/renderer_vulkan/vk_common.h"

/**
 * The OpenGL calls melonDS's hi-res renderer makes (ds_gl_decl.h, 79 of them), carried out on a
 * Vulkan device. Not an OpenGL implementation: only what that renderer uses, in the way it uses
 * it. One context at a time, used by one thread at a time (the host's lock).
 */
namespace DsGl {

struct Device {
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    std::mutex* queue_mutex = nullptr; ///< held around every use of `queue`
};

struct Stats {
    uint64_t draws = 0;
    uint64_t passes = 0;      ///< render passes begun
    uint64_t pass_splits = 0; ///< of them, begun again to read what was just drawn
    uint64_t clears = 0;
    uint64_t uploads = 0;
    uint64_t upload_bytes = 0;
    uint64_t readbacks = 0; ///< each one waits for the GPU
    uint64_t submits = 0;
    uint64_t sets = 0;         ///< descriptor sets written
    uint64_t pipelines = 0;    ///< made so far
    uint64_t stream_bytes = 0; ///< vertex, index and uniform data copied
};

struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE; ///< all layers, as the renderer samples it
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t layers = 0;
};

class Context;

/// The one the gl* calls go to from now on. Null with `why` when the device cannot do it, or there
/// is one already.
Context* Create(const Device& device, std::string& why);
/// Waits for the GPU, then frees everything.
void Destroy(Context* context);

/// Hands what was recorded to the GPU. Images are in VK_IMAGE_LAYOUT_GENERAL, always.
void Flush(bool wait);
/// False once the device failed (out of memory, device lost): the renderer is to be dropped.
bool Healthy();
const Stats& GetStats();
bool GetTexture(uint32_t name, Texture& out);
/// One layer's pixels as bytes R, G, B, A (tests, screenshots). Waits for the GPU.
bool ReadTexture(uint32_t name, uint32_t layer, std::vector<uint32_t>& rgba);
/// Messages (level 0 error, 1 warning, 2 note); default: stderr.
void SetLog(void (*log)(int level, const char* text));

} // namespace DsGl
