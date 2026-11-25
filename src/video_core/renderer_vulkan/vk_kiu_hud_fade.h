// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuhs3a: HUD Opacity's copy of what is behind, and its draw over the HUD
// (video_core/kiu_hud_fade.h).

#pragma once

#include <array>
#include <map>
#include <utility>

#include "common/math_util.h"
#include "video_core/kiu_hud_fade.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"

namespace Vulkan {

class DescriptorUpdateQueue;
class Framebuffer;
class Instance;
class RenderManager;
class Scheduler;
class Surface;

/// The emulation thread.
class KiuHudFade {
public:
    KiuHudFade(const Instance& instance, Scheduler& scheduler, RenderManager& renderpass_cache,
               DescriptorUpdateQueue& update_queue);
    ~KiuHudFade();
    KiuHudFade(const KiuHudFade&) = delete;
    KiuHudFade& operator=(const KiuHudFade&) = delete;

    /// Pieces are drawn whose back is kept.
    bool Active() const {
        return active;
    }
    /// The pieces being faded are this target's.
    bool On(u32 color_address) const {
        return active && color_address == address;
    }
    /// A piece is about to be drawn into `framebuffer` (its colour `color`), not in a pass yet.
    void Begin(const VideoCore::KiuHudFade::Piece& piece, u32 color_address,
               const Framebuffer* framebuffer, Surface& color,
               const Common::Rectangle<u32>& draw_rect, s32 viewport_x, s32 viewport_y,
               s32 viewport_width, s32 viewport_height);
    /// Before anything else is drawn, copied or read: what was behind goes over the pieces.
    void Finish();

private:
    vk::Pipeline PipelineFor(const Framebuffer* framebuffer);

    const Instance& instance;
    Scheduler& scheduler;
    RenderManager& renderpass_cache;
    DescriptorUpdateQueue& update_queue;
    DescriptorHeap heap;
    vk::PipelineLayout layout;
    vk::ShaderModule vertex, fragment;
    vk::Sampler sampler;
    std::map<std::pair<u32, u32>, vk::Pipeline> pipelines; ///< by the colour's and depth's format

    bool active = false;
    u32 address = 0;
    float opacity = 1.0f;
    const Framebuffer* framebuffer = nullptr;
    Common::Rectangle<u32> draw_rect;
    vk::ImageView behind;
    std::array<VideoCore::KiuHudFade::Pixels, 2> kept;  ///< the bar's part, the score's
    std::array<VideoCore::KiuHudFade::Pixels, 2> drawn; ///< what their pieces covered
};

} // namespace Vulkan
