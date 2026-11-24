// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <cstring>
#include <string_view>

#include "common/frame_flat.h"
#include "common/settings.h"
#include "core/frontend/emu_window.h"
#include "video_core/host_shaders/vulkan_present_frag.h"
#include "video_core/renderer_vulkan/alt_pipeline.h"
#include "video_core/renderer_vulkan/alt_present.h"
#include "video_core/renderer_vulkan/alt_window_target.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

namespace {

constexpr int kPrimary = 0;
constexpr int kSecondary = 2;

} // Anonymous namespace

AltWindowTarget::AltWindowTarget(Frontend::EmuWindow& window_, const Instance& instance_,
                                 Scheduler& scheduler_, vk::ShaderModule vertex_shader_,
                                 vk::PipelineLayout pipeline_layout_)
    : window{window_}, instance{instance_}, scheduler{scheduler_}, vertex_shader{vertex_shader_},
      pipeline_layout{pipeline_layout_}, present_window{window, instance, scheduler,
                                                        IsLowRefreshRate()} {
    BuildPipeline();
}

AltWindowTarget::~AltWindowTarget() {
    scheduler.Finish();
    present_window.WaitPresent();
    const vk::Device device = instance.GetDevice();
    {
        // The XR thread may use the queue (xrEndFrame) under this mutex.
        std::scoped_lock lock{scheduler.submit_mutex};
        device.waitIdle();
    }
    device.destroyPipeline(pipeline);
    device.destroyShaderModule(fragment_shader);
    device.destroyPipeline(overlay_pipeline); // null: none was made
    device.destroyShaderModule(overlay_shader);
}

void AltWindowTarget::BuildPipeline() {
    const vk::Device device = instance.GetDevice();
    const std::string_view preamble =
        instance.IsImageArrayDynamicIndexSupported() ? "#define ARRAY_DYNAMIC_INDEX" : "";
    fragment_shader = Compile(HostShaders::VULKAN_PRESENT_FRAG, vk::ShaderStageFlagBits::eFragment,
                              device, preamble);

    pipeline = BuildAltPipeline(device, vertex_shader, fragment_shader, pipeline_layout,
                                present_window.Renderpass(), true);
}

void AltWindowTarget::BuildOverlayPipeline() {
    const vk::Device device = instance.GetDevice();
    overlay_shader = Compile(kAltOverlayFrag, vk::ShaderStageFlagBits::eFragment, device);
    overlay_pipeline = BuildAltPipeline(device, vertex_shader, overlay_shader, pipeline_layout,
                                        present_window.Renderpass(), true);
}

void AltWindowTarget::Draw(StreamBuffer& vertex_buffer, vk::DescriptorSet screens,
                           bool overlay) {
    if (overlay && !overlay_pipeline) {
        BuildOverlayPipeline();
    }
    DrawLayout(window.GetFramebufferLayout(), vertex_buffer, screens, overlay);
}

void AltWindowTarget::Clear(StreamBuffer& vertex_buffer, vk::DescriptorSet screens) {
    Layout::FramebufferLayout layout = window.GetFramebufferLayout();
    layout.top_screen_enabled = false;
    layout.bottom_screen_enabled = false;
    layout.additional_screen_enabled = false;
    DrawLayout(layout, vertex_buffer, screens, false);
}

bool AltWindowTarget::Resized() const {
    const auto& layout = window.GetFramebufferLayout();
    return layout.width != drawn_width || layout.height != drawn_height;
}

void AltWindowTarget::DrawLayout(const Layout::FramebufferLayout& layout,
                                 StreamBuffer& vertex_buffer, vk::DescriptorSet screens,
                                 bool overlay) {
    drawn_width = layout.width;
    drawn_height = layout.height;
    Frame* const frame = present_window.GetRenderFrame();
    if (layout.width != frame->width || layout.height != frame->height) {
        present_window.WaitPresent();
        scheduler.Finish();
        present_window.RecreateFrame(frame, layout.width, layout.height);
    }

    const vk::ClearColorValue background{.float32 = std::array{
                                             Settings::values.bg_red.GetValue(),
                                             Settings::values.bg_green.GetValue(),
                                             Settings::values.bg_blue.GetValue(),
                                             1.0f,
                                         }};
    scheduler.Record([frame, screens, background, width = layout.width, height = layout.height,
                      renderpass = present_window.Renderpass(), pipe = pipeline,
                      pipe_layout = pipeline_layout](vk::CommandBuffer cmdbuf) {
        const vk::ClearValue clear{.color = background};
        const vk::Rect2D area = {.offset = {0, 0}, .extent = {width, height}};
        const vk::RenderPassBeginInfo begin_info = {
            .renderPass = renderpass,
            .framebuffer = frame->framebuffer,
            .renderArea = area,
            .clearValueCount = 1,
            .pClearValues = &clear,
        };
        const vk::Viewport viewport = {
            .width = static_cast<float>(width),
            .height = static_cast<float>(height),
            .maxDepth = 1.0f,
        };
        cmdbuf.setViewport(0, viewport);
        cmdbuf.setScissor(0, area);
        cmdbuf.beginRenderPass(begin_info, vk::SubpassContents::eInline);
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipe);
        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipe_layout, 0, screens, {});
    });

    const auto top = [&](const Common::Rectangle<u32>& rect) {
        if (layout.top_screen_enabled) {
            const auto bind = [this](vk::Pipeline to) {
                scheduler.Record([to](vk::CommandBuffer cmdbuf) {
                    cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, to);
                });
            };
            if (overlay) {
                bind(overlay_pipeline);
            }
            DrawScreen(layout, vertex_buffer, kPrimary, rect, layout.top_opacity);
            if (overlay) {
                bind(pipeline);
            }
        }
    };
    const auto bottom = [&](const Common::Rectangle<u32>& rect) {
        if (layout.bottom_screen_enabled) {
            DrawScreen(layout, vertex_buffer, kSecondary, rect, layout.bottom_opacity);
        }
    };
    // The 3DS's order: the second layer, which may be see-through, over the first.
    if (!Settings::WindowSwapScreen()) {
        top(layout.top_screen);
        bottom(layout.bottom_screen);
    } else {
        bottom(layout.bottom_screen);
        top(layout.top_screen);
    }
    if (layout.additional_screen_enabled) {
        layout.additional_screen_is_bottom ? bottom(layout.additional_screen)
                                           : top(layout.additional_screen);
    }

    scheduler.Record([](vk::CommandBuffer cmdbuf) { cmdbuf.endRenderPass(); });
    scheduler.Flush(frame->render_ready);
    present_window.Present(frame);
    scheduler.DispatchWork(); // Present only records the frame's hand-over
}

void AltWindowTarget::DrawScreen(const Layout::FramebufferLayout& layout,
                                 StreamBuffer& vertex_buffer, int screen_id,
                                 const Common::Rectangle<u32>& rect, float opacity) {
    float x = static_cast<float>(rect.left);
    float w = static_cast<float>(rect.GetWidth());
    const float y = static_cast<float>(rect.top);
    const float h = static_cast<float>(rect.GetHeight());

    // Side by side 3D shows each eye the same picture.
    const float half = static_cast<float>(layout.width / 2);
    const bool side_by_side = layout.render_3d_mode == Settings::StereoRenderOption::SideBySide;
    const bool both_halves =
        side_by_side || layout.render_3d_mode == Settings::StereoRenderOption::SideBySideFull;
    if (side_by_side) {
        x /= 2;
        w /= 2;
    }

    AltPresentUniformData info{};
    info.modelview = Orthographic(layout.width, layout.height);
    info.o_resolution = Common::MakeVec(w, h, 1.0f / w, 1.0f / h);
    info.screen_id_l = screen_id;
    info.screen_id_r = screen_id;

    for (int eye = 0; eye < (both_halves ? 2 : 1); eye++, x += half) {
        const auto vertices = AltScreenQuad(x, y, w, h, layout.is_rotated);
        const u32 size = static_cast<u32>(sizeof(vertices));
        auto [data, offset, invalidate] = vertex_buffer.Map(size, 16);
        static_cast<void>(invalidate);
        std::memcpy(data, vertices.data(), size);
        vertex_buffer.Commit(size);

        scheduler.Record([info, opacity, pipe_layout = pipeline_layout,
                          buffer = vertex_buffer.Handle(),
                          first = static_cast<u32>(offset / sizeof(AltScreenVertex))](
                             vk::CommandBuffer cmdbuf) {
            const std::array<float, 4> blend_constants = {0.0f, 0.0f, 0.0f, opacity};
            cmdbuf.setBlendConstants(blend_constants.data());
            cmdbuf.pushConstants(pipe_layout,
                                 vk::ShaderStageFlagBits::eVertex |
                                     vk::ShaderStageFlagBits::eFragment,
                                 0, sizeof(info), &info);
            cmdbuf.bindVertexBuffers(0, buffer, {0});
            cmdbuf.draw(4, 1, first, 0);
        });
    }
}

} // namespace Vulkan
