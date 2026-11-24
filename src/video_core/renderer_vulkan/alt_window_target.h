// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/frontend/framebuffer_layout.h"
#include "video_core/renderer_vulkan/vk_present_window.h"
#include "video_core/renderer_vulkan/vk_stream_buffer.h"

namespace Vulkan {

/// One desktop window of a DS, DSi, GBA or Game Boy game: the game's screens where the window's
/// layout has them, through the same PresentWindow and present shader as a 3DS game.
class AltWindowTarget {
public:
    AltWindowTarget(Frontend::EmuWindow& window, const Instance& instance, Scheduler& scheduler,
                    vk::ShaderModule vertex_shader, vk::PipelineLayout pipeline_layout);
    ~AltWindowTarget();

    AltWindowTarget(const AltWindowTarget&) = delete;
    AltWindowTarget& operator=(const AltWindowTarget&) = delete;

    /// Draws and queues one frame. `screens` has the Primary Screen at 0 and the Secondary Screen
    /// at 2, as the 3DS's have them.
    void Draw(StreamBuffer& vertex_buffer, vk::DescriptorSet screens);

    /// Queues an empty frame: the window while the headset shows the screens.
    void Clear(StreamBuffer& vertex_buffer, vk::DescriptorSet screens);

    /// The window's size changed since the last frame.
    bool Resized() const;

private:
    void DrawLayout(const Layout::FramebufferLayout& layout, StreamBuffer& vertex_buffer,
                    vk::DescriptorSet screens);
    void DrawScreen(const Layout::FramebufferLayout& layout, StreamBuffer& vertex_buffer,
                    int screen_id, const Common::Rectangle<u32>& rect, float opacity);
    void BuildPipeline(vk::ShaderModule vertex_shader);

    Frontend::EmuWindow& window;
    const Instance& instance;
    Scheduler& scheduler;
    vk::PipelineLayout pipeline_layout;
    PresentWindow present_window;
    vk::ShaderModule fragment_shader{};
    vk::Pipeline pipeline{};
    u32 drawn_width = 0;
    u32 drawn_height = 0;
};

} // namespace Vulkan
