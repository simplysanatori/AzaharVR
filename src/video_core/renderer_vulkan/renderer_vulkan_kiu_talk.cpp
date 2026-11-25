// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiutalk: the talk's strip under the bottom screen's picture in its XR image
// (video_core/kiu_talk.h), drawn in the bottom screen's part of RendererVulkan::RenderToXr.

#include <algorithm>
#include <cmath>

#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/vulkan_hooks.h"

namespace Vulkan {

namespace KT = VideoCore::KiuTalk;

/// Outside the pass: the textures may be uploaded here.
void RendererVulkan::PrepareKiuTalk() {
    kiu_talk_sets.clear();
    if (!KT::g_live.strip.load(std::memory_order_relaxed)) {
        return;
    }
    const auto format = static_cast<vk::Format>(VideoCore::GetXrScreenSink()->SwapchainFormat());
    if (!kiu_talk_pass || kiu_talk_generation != xr_target_generation) {
        kiu_talk_generation = xr_target_generation;
        scheduler.Finish(); // the old ones may be in use
        kiu_talk_pass = std::make_unique<KiuTalkPass>(
            instance, xr_target->RenderPass(), *present_pipeline_layout,
            format == vk::Format::eR8G8B8A8Srgb || format == vk::Format::eB8G8R8A8Srgb);
    }
    kiu_talk_card =
        kiu_talk_choice.Card(KT::g_live.GetExpected(), rasterizer.KiuTalk().Seen());
    if (!KT::g_live.shown.load(std::memory_order_relaxed) || kiu_talk_card) {
        return;
    }
    rasterizer.KiuTalkTextures([&](vk::ImageView view, vk::Sampler sampler) {
        const auto set = present_heap.Commit();
        for (u32 index = 0; index < screen_infos.size(); index++) {
            update_queue.AddImageSampler(set, 0, index, view, sampler);
        }
        kiu_talk_sets.push_back(set);
    });
}

/// `width`, `height`: the image's. draw_info is the bottom screen's picture's.
void RendererVulkan::DrawKiuTalk(u32 width, u32 height, float dim, float alpha,
                                 vk::DescriptorSet picture) {
    const KT::PixelRect strip = KT::StripIn(width, height);
    if (strip.width == 0 || !kiu_talk_pass) {
        return;
    }
    // Nothing behind them: the pass begins opaque black.
    scheduler.Record([strip, width, height](vk::CommandBuffer cmdbuf) {
        const vk::ClearAttachment clear = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .colorAttachment = 0,
            .clearValue = {.color = {.float32 = std::array{0.f, 0.f, 0.f, 0.f}}},
        };
        const u32 top = strip.y - KT::kGapPx;
        const vk::ClearRect rect = {
            .rect = {{0, static_cast<s32>(top)}, {width, height - top}},
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
        cmdbuf.clearAttachments(clear, rect);
    });
    if (!KT::g_live.shown.load(std::memory_order_relaxed)) {
        return;
    }
    const u32 scale = strip.width / KT::kScreenWide;
    const vk::Viewport viewport = {
        .x = 0.0f,
        .y = static_cast<float>(strip.y),
        .width = static_cast<float>(strip.width),
        .height = static_cast<float>(strip.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    const auto rows = [&](u32 from, u32 to) {
        return vk::Rect2D{{0, static_cast<s32>(strip.y + from * scale)},
                          {strip.width, (to - from) * scale}};
    };
    if (kiu_talk_card) { // the picture's part of the card, as without the strip
        scheduler.Record([viewport, scissor = rows(KT::kCardTop, KT::kScreenHigh), picture,
                          pipeline = xr_target->Pipeline(),
                          layout = *present_pipeline_layout](vk::CommandBuffer cmdbuf) {
            cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, picture, {});
            cmdbuf.setViewport(0, viewport);
            cmdbuf.setScissor(0, scissor);
        });
        DrawSingleScreen(static_cast<u32>(VideoCore::XrScreen::Bottom), 0.0f, 0.0f,
                         static_cast<float>(strip.width), static_cast<float>(strip.height),
                         Layout::DisplayOrientation::Landscape);
        return;
    }
    const auto& sprites = rasterizer.KiuTalk().Sprites();
    if (kiu_talk_sets.empty() || sprites.empty()) {
        return;
    }
    // All the quads' vertices, and one over all of it for the opacity and the dim.
    const std::size_t count = sprites.size();
    constexpr u32 kStride = sizeof(float) * KiuTalkPass::kFloats;
    const u64 size = (count + 1) * 4 * kStride;
    auto [data, offset, invalidate] = vertex_buffer.Map(size, kStride);
    float* out = reinterpret_cast<float*>(data);
    for (const KT::Sprite& sprite : sprites) {
        for (u32 i = 0; i < 4; i++) {
            *out++ = sprite.quad.x[i];
            *out++ = sprite.quad.y[i];
            *out++ = sprite.quad.u[i];
            *out++ = sprite.quad.v[i];
            for (u32 c = 0; c < 4; c++) {
                *out++ = static_cast<float>(sprite.quad.color[i] >> (8 * c) & 0xFF) / 255.0f;
            }
        }
    }
    // Linear light, premultiplied: in an sRGB image the blend is in it, else as the picture's
    // shader (a power of the product).
    const float light = std::clamp(dim, 0.0f, 1.0f) * std::clamp(alpha, 0.0f, 1.0f);
    const float factor = kiu_talk_pass->Srgb() ? light : std::pow(light, 1.0f / 2.2f);
    for (u32 i = 0; i < 4; i++) {
        *out++ = i & 1 ? static_cast<float>(KT::kScreenWide) : 0.0f;
        *out++ = i & 2 ? static_cast<float>(KT::kScreenHigh) : 0.0f;
        *out++ = 0.0f;
        *out++ = 0.0f;
        for (u32 c = 0; c < 3; c++) {
            *out++ = factor;
        }
        *out++ = std::clamp(alpha, 0.0f, 1.0f);
    }
    vertex_buffer.Commit(size);

    PresentUniformData info = draw_info;
    // clang-format off
    info.modelview = {2.f / KT::kScreenWide, 0.f, 0.f, -1.f,
                      0.f, 2.f / KT::kScreenHigh, 0.f, -1.f,
                      0.f, 0.f, 1.f, 0.f,
                      0.f, 0.f, 0.f, 1.f};
    // clang-format on
    scheduler.Record([this, viewport, pipeline = kiu_talk_pass->Over()](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        cmdbuf.setViewport(0, viewport);
        cmdbuf.bindVertexBuffers(0, vertex_buffer.Handle(), {0});
    });
    const auto draw = [&](std::size_t n, vk::Rect2D scissor, vk::DescriptorSet set,
                          vk::Pipeline pipeline) {
        const u32 first = static_cast<u32>(offset / kStride + n * 4);
        scheduler.Record([this, first, info, scissor, set, pipeline](vk::CommandBuffer cmdbuf) {
            if (pipeline) {
                cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            }
            cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *present_pipeline_layout,
                                      0, set, {});
            cmdbuf.setScissor(0, scissor);
            cmdbuf.pushConstants(*present_pipeline_layout,
                                 vk::ShaderStageFlagBits::eFragment |
                                     vk::ShaderStageFlagBits::eVertex,
                                 0, sizeof(info), &info);
            cmdbuf.draw(4, 1, first, 0);
        });
    };
    for (std::size_t n = 0; n < count; n++) {
        const KT::Sprite& sprite = sprites[n];
        if (sprite.texture >= kiu_talk_sets.size()) {
            continue;
        }
        const auto constant = [&](u32 c) { // its stage's scale is 2
            return 2.0f * static_cast<float>(sprite.constant >> (8 * c) & 0xFF) / 255.0f;
        };
        info.i_resolution = Common::MakeVec(constant(0), constant(1), constant(2), 0.0f);
        info.screen_id_l = static_cast<int>(sprite.tev);
        draw(n,
             sprite.kind == KT::Kind::Illustration ? rows(KT::kWindowTop, KT::kWindowBottom)
                                                   : rows(0, KT::kScreenHigh),
             kiu_talk_sets[sprite.texture], {});
    }
    if (light < 1.0f || alpha < 1.0f) { // all of it at once
        info.screen_id_l = 0;
        draw(count, rows(0, KT::kScreenHigh), kiu_talk_sets[0], kiu_talk_pass->Scale());
    }
}

} // namespace Vulkan
