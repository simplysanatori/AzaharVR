// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame: RendererVulkan::RenderToXr, called from SwapBuffers after the
// window was drawn. Draws each of the three screens full size into its own XR
// swapchain image (top-left eye, top-right eye, bottom), or into spill slots while
// those are still in use, submits once, and hands the frame to the handoff thread
// (vk_xr_target.cpp, vk_xr_handoff.h), which releases the images to the runtime
// once the GPU is done. Nothing here waits for the XR side.

#include <algorithm>
#include <utility>

#include "common/debug_stall.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/kiu_powers.h" // AzaharFrame kiupowers
#include "video_core/vr_immersive.h" // AzaharFrame doubling
#include "video_core/vulkan_hooks.h"

namespace Vulkan {

namespace {

// Native sizes of the 3DS screens (landscape).
constexpr u32 kTopWidth = 400;
constexpr u32 kBottomWidth = 320;
constexpr u32 kScreenHeight = 240;
// SteamVR on the Frame downsamples layers wider than 3458 px: 8x (3200) at most.
constexpr u32 kMaxScale = 8;

std::array<f32, 4 * 4> Orthographic(u32 width, u32 height) {
    // clang-format off
    return {2.f / width, 0.f,          0.f, -1.f,
            0.f,         2.f / height, 0.f, -1.f,
            0.f,         0.f,          1.f,  0.f,
            0.f,         0.f,          0.f,  1.f};
    // clang-format on
}

} // Anonymous namespace

void RendererVulkan::DrawKiuPowers(u32 width, u32 height, float alpha) {
    namespace KP = VideoCore::KiuPowers;
    const KP::PixelRect strip = KP::StripIn(width, height);
    if (strip.width == 0) {
        return;
    }
    // Nothing behind them: the pass begins opaque black.
    scheduler.Record([strip](vk::CommandBuffer cmdbuf) {
        const vk::ClearAttachment clear = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .colorAttachment = 0,
            .clearValue = {.color = {.float32 = std::array{0.f, 0.f, 0.f, 0.f}}},
        };
        const vk::ClearRect rect = {
            .rect = {{0, static_cast<s32>(strip.y - KP::kGapPx)},
                     {strip.width, strip.height + KP::kGapPx}},
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        cmdbuf.clearAttachments(clear, rect);
    });
    const std::vector<KP::Sprite>& sprites = rasterizer.KiuPowersSprites();
    if (!(alpha > 0.0f) || sprites.empty()) {
        return;
    }
    // The strip's part of the bottom screen, in its pixels.
    scheduler.Record([strip, pipeline = xr_target->SpritePipeline()](vk::CommandBuffer cmdbuf) {
        const vk::Viewport viewport = {
            .x = 0.0f,
            .y = static_cast<float>(strip.y),
            .width = static_cast<float>(strip.width),
            .height = static_cast<float>(strip.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        const vk::Rect2D scissor = {{0, static_cast<s32>(strip.y)}, {strip.width, strip.height}};
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        cmdbuf.setViewport(0, viewport);
        cmdbuf.setScissor(0, scissor);
    });
    draw_info.modelview = Orthographic(KP::kWide, KP::kHigh);
    u32 bound = UINT32_MAX;
    for (const KP::Sprite& sprite : sprites) {
        if (sprite.texture >= xr_sprite_sets.size()) {
            continue;
        }
        std::array<float, 16> vertices; // position, texture coordinates
        for (u32 i = 0; i < 4; i++) {
            vertices[i * 4] = sprite.x[i] - static_cast<float>(KP::kLeft);
            vertices[i * 4 + 1] = sprite.y[i] - static_cast<float>(KP::kTop);
            vertices[i * 4 + 2] = sprite.u[i];
            vertices[i * 4 + 3] = sprite.v[i];
        }
        auto [data, offset, invalidate] = vertex_buffer.Map(sizeof(vertices), 16);
        std::memcpy(data, vertices.data(), sizeof(vertices));
        vertex_buffer.Commit(sizeof(vertices));
        draw_info.i_resolution = Common::MakeVec(sprite.color[0], sprite.color[1], sprite.color[2],
                                                 sprite.color[3] * alpha);
        const vk::DescriptorSet set =
            sprite.texture != bound ? xr_sprite_sets[sprite.texture] : vk::DescriptorSet{};
        bound = sprite.texture;
        scheduler.Record([this, offset = offset, info = draw_info, set](vk::CommandBuffer cmdbuf) {
            if (set) {
                cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                          *present_pipeline_layout, 0, set, {});
            }
            cmdbuf.pushConstants(*present_pipeline_layout,
                                 vk::ShaderStageFlagBits::eFragment |
                                     vk::ShaderStageFlagBits::eVertex,
                                 0, sizeof(info), &info);
            cmdbuf.bindVertexBuffers(0, vertex_buffer.Handle(), {0});
            cmdbuf.draw(4, 1, static_cast<u32>(offset) / (sizeof(float) * 4), 0);
        });
    }
}

bool RendererVulkan::RenderToXr() {
    VideoCore::XrScreenSink* sink = VideoCore::GetXrScreenSink();
    if (!sink || !sink->IsActive()) {
        return false;
    }
    if (!UsesXrDevice(instance)) {
        // The swapchain images belong to the runtime's device; never draw into them
        // from another one.
        static bool logged = false;
        if (!std::exchange(logged, true)) {
            LOG_ERROR(Render_Vulkan, "xr: the renderer is not on the XR device, no XR screens");
        }
        return false;
    }
    if (xr_target && xr_target_generation != sink->SwapchainGeneration()) {
        // The session ended and a new one made new swapchains (VR -> flat -> VR): what was
        // made of the old images goes (after its releases drained, in the destructor).
        xr_target.reset();
    }
    if (!xr_target) {
        xr_target_generation = sink->SwapchainGeneration();
        xr_target = std::make_unique<XrPresentTarget>(instance, scheduler, *sink,
                                                      present_vertex_shader,
                                                      *present_pipeline_layout);
    }

    const u32 scale = std::clamp<u32>(GetResolutionScaleFactor(), 1, kMaxScale);
    // AzaharFrame doubling: packed, both eyes in the TopLeft swapchain (left above, right below).
    const bool packed = xr_target->Packed();
    const bool powers_strip = // AzaharFrame kiupowers
        VideoCore::KiuPowers::g_live.strip.load(std::memory_order_relaxed);
    const float powers_alpha = VideoCore::KiuPowers::g_alpha.load(std::memory_order_relaxed);
    const std::array<vk::Extent2D, XrPresentTarget::kScreens> sizes = {
        vk::Extent2D{kTopWidth * scale,
                     packed ? 2 * kScreenHeight * scale + VideoCore::kXrPackedGap
                            : kScreenHeight * scale},
        packed ? vk::Extent2D{} : vk::Extent2D{kTopWidth * scale, kScreenHeight * scale},
        // AzaharFrame kiupowers: with the strip under the picture.
        vk::Extent2D{kBottomWidth * scale, VideoCore::KiuPowers::ImageHeight(scale, powers_strip)},
    };
    if (!xr_target->Prepare(sizes)) {
        return false;
    }

    const auto sampler = present_samplers[!Settings::values.filter_mode.GetValue()];
    const auto present_set = present_heap.Commit();
    for (u32 index = 0; index < screen_infos.size(); index++) {
        update_queue.AddImageSampler(present_set, 0, index, screen_infos[index].image_view,
                                     sampler);
    }
    // AzaharFrame kiupowers: the sprites' textures (they may be uploaded here: outside the pass).
    xr_sprite_sets.clear();
    if (powers_strip && powers_alpha > 0.0f) {
        rasterizer.KiuPowersTextures([&](vk::ImageView view, vk::Sampler texture_sampler) {
            const auto set = present_heap.Commit();
            for (u32 index = 0; index < screen_infos.size(); index++) {
                update_queue.AddImageSampler(set, 0, index, view, texture_sampler);
            }
            xr_sprite_sets.push_back(set);
        });
    }
    renderpass_cache.EndRendering();

    std::array<XrPresentTarget::Target, XrPresentTarget::kScreens> targets{};
    // AzaharFrame doubling: the head pose the picture on the screens was drawn with (immersive
    // modes 2 and 3), handed back with the images' release.
    if (!xr_target->BeginFrame(targets, VideoCore::Immersive::DrawnContentPose())) {
        return false;
    }
    for (u32 screen_id = 0; screen_id < XrPresentTarget::kScreens; screen_id++) {
        XrPresentTarget::Target& target = targets[screen_id];
        if (screen_id == static_cast<u32>(VideoCore::XrScreen::Bottom)) { // AzaharFrame kiupowers
            target.height = VideoCore::KiuPowers::PictureHeight(target.width, target.height);
        }
        scheduler.Record([target, present_set, renderpass = xr_target->RenderPass(),
                          pipeline = xr_target->Pipeline(),
                          layout = *present_pipeline_layout](vk::CommandBuffer cmdbuf) {
            const vk::Viewport viewport = {
                .x = 0.0f,
                .y = static_cast<float>(target.offset_y),
                .width = static_cast<float>(target.width),
                .height = static_cast<float>(target.height),
                .minDepth = 0.0f,
                .maxDepth = 1.0f,
            };
            const vk::Rect2D scissor = {
                .offset = {0, static_cast<s32>(target.offset_y)},
                .extent = {target.width, target.height},
            };
            if (target.begin_pass) {
                // The whole image: a packed top screen's gap between the eyes is cleared too.
                const vk::ClearValue clear{
                    .color = vk::ClearColorValue{.float32 = std::array{0.f, 0.f, 0.f, 1.f}}};
                const vk::RenderPassBeginInfo begin_info = {
                    .renderPass = renderpass,
                    .framebuffer = target.framebuffer,
                    .renderArea = {.offset = {0, 0}, .extent = {target.fb_width, target.fb_height}},
                    .clearValueCount = 1,
                    .pClearValues = &clear,
                };
                cmdbuf.beginRenderPass(begin_info, vk::SubpassContents::eInline);
                cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
                cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0,
                                          present_set, {});
            }
            cmdbuf.setViewport(0, viewport);
            cmdbuf.setScissor(0, scissor);
        });
        draw_info.modelview = Orthographic(target.width, target.height);
        // AzaharFrame fixa: the XR present shader takes `layer` (unused otherwise) as Match
        // Bottom Screen Brightness's dim in thousandths, 1 to 999 (0: none).
        const float dim =
            sink->ScreenDim(static_cast<VideoCore::XrScreen>(std::min<u32>(screen_id, 2)));
        // AzaharFrame kiu131: and the opacity in thousandths (1 to 999; 0: opaque) times 1000.
        const float alpha =
            sink->ScreenAlpha(static_cast<VideoCore::XrScreen>(std::min<u32>(screen_id, 2)));
        draw_info.layer =
            (dim < 1.0f ? std::clamp(static_cast<int>(dim * 1000.0f + 0.5f), 1, 999) : 0) +
            1000 * (alpha < 1.0f ? std::clamp(static_cast<int>(alpha * 1000.0f + 0.5f), 1, 999) : 0);
        DrawSingleScreen(screen_id, 0.0f, 0.0f, static_cast<float>(target.width),
                         static_cast<float>(target.height),
                         Layout::DisplayOrientation::Landscape);
        if (powers_strip && screen_id == static_cast<u32>(VideoCore::XrScreen::Bottom)) {
            DrawKiuPowers(target.fb_width, target.fb_height, powers_alpha);
        }
        if (target.end_pass) {
            scheduler.Record([](vk::CommandBuffer cmdbuf) { cmdbuf.endRenderPass(); });
        }
    }
    Common::DebugStall::Maybe(Common::DebugStall::Where::Acquire); // AzaharFrame: test only
    // The tick this submission signals (Scheduler::SubmitExecution).
    const u64 tick = scheduler.CurrentTick();
    scheduler.Flush();
    xr_target->EndFrame(tick);
    return true;
}

} // namespace Vulkan
