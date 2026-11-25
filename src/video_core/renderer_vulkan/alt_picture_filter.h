// Copyright 2026 AzaharVR contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>

#include "common/common_types.h"
#include "common/settings.h"
#include "video_core/renderer_vulkan/alt_present.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class DescriptorHeap;
class DescriptorUpdateQueue;
class Instance;
class Scheduler;

/// The 3DS's texture filters over a whole GBA or Game Boy picture: the same shaders as the 3DS's
/// textures get (vk_blit_helper.cpp), drawn from the picture into its bigger image.
class AltPictureFilter {
public:
    AltPictureFilter(const Instance& instance, Scheduler& scheduler,
                     DescriptorUpdateQueue& update_queue);
    ~AltPictureFilter();

    /// For an image the filters draw into (R8G8B8A8Unorm, a colour attachment that is sampled).
    vk::Framebuffer MakeFramebuffer(vk::ImageView view, u32 width, u32 height);

    /// `source` (ready to be sampled) through `filter` into `target`, `width` x `height`, `scale`
    /// times the source's size; the target is left ready to be sampled. False: not drawn (no such
    /// filter, or its pipeline failed).
    bool Draw(Settings::TextureFilter filter, vk::ImageView source, vk::Framebuffer target,
              u32 width, u32 height, u32 scale);

private:
    vk::Pipeline Pipeline(Settings::TextureFilter filter);

    static constexpr std::size_t kFilters = 6;

    const Instance& instance;
    Scheduler& scheduler;
    DescriptorUpdateQueue& update_queue;
    std::unique_ptr<DescriptorHeap> heap;
    vk::UniquePipelineLayout layout;
    vk::UniqueRenderPass render_pass;
    vk::UniqueSampler sampler;
    vk::UniqueShaderModule vertex_shader;
    std::array<vk::UniqueShaderModule, kFilters> shaders;
    std::array<vk::UniquePipeline, kFilters> pipelines;
    std::array<bool, kFilters> failed{};
};

} // namespace Vulkan
