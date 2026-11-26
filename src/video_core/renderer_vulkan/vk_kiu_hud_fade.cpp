// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuhs3a: vk_kiu_hud_fade.h.

#include <algorithm>
#include <string_view>
#include <boost/container/static_vector.hpp>

#include "common/assert.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_kiu_hud_fade.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/vk_texture_runtime.h"

namespace Vulkan {

namespace HF = VideoCore::KiuHudFade;

namespace {

constexpr std::string_view VERT = R"(
#version 450 core

void main() {
    float x = float((gl_VertexIndex & 1) << 2);
    float y = float((gl_VertexIndex & 2) << 1);
    gl_Position = vec4(x - 1.0, y - 1.0, 0.0, 1.0);
}
)";

constexpr std::string_view FRAG = R"(
#version 450 core

layout (location = 0) out vec4 color;
layout (set = 0, binding = 0) uniform sampler2D behind;
layout (push_constant, std140) uniform Info {
    float share;
};

void main() {
    color = vec4(texelFetch(behind, ivec2(gl_FragCoord.xy), 0).rgb, share);
}
)";

constexpr std::array<vk::DescriptorSetLayoutBinding, 1> BINDINGS = {{
    {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
}};

} // namespace

KiuHudFade::KiuHudFade(const Instance& instance_, Scheduler& scheduler_,
                       RenderManager& renderpass_cache_, DescriptorUpdateQueue& update_queue_)
    : instance{instance_}, scheduler{scheduler_}, renderpass_cache{renderpass_cache_},
      update_queue{update_queue_}, heap{instance, scheduler.GetMasterSemaphore(), BINDINGS, 16} {}

KiuHudFade::~KiuHudFade() {
    const vk::Device device = instance.GetDevice();
    for (const auto& [key, pipeline] : pipelines) {
        device.destroyPipeline(pipeline);
    }
    if (layout) {
        device.destroyPipelineLayout(layout);
        device.destroyShaderModule(vertex);
        device.destroyShaderModule(fragment);
        device.destroySampler(sampler);
    }
}

void KiuHudFade::Begin(const HF::Piece& piece, u32 color_address, const Framebuffer* target,
                       Surface& color, const Common::Rectangle<u32>& rect, s32 viewport_x,
                       s32 viewport_y, s32 viewport_width, s32 viewport_height) {
    const HF::Pixels within{static_cast<s32>(rect.left), static_cast<s32>(rect.bottom),
                            rect.GetWidth(), rect.GetHeight()};
    const auto pixels = [&](const HF::Box& b) {
        return HF::ToPixels(b, viewport_x, viewport_y, viewport_width, viewport_height, within);
    };
    framebuffer = target;
    draw_rect = rect;
    if (!active) {
        boost::container::static_vector<vk::Rect2D, 2> parts;
        kept = {pixels(piece.groups[0]), pixels(piece.groups[1])};
        drawn = {};
        if (!HF::Intersect(kept[0], kept[1]).Empty()) { // one part, for both
            kept.fill(HF::Union(kept[0], kept[1]));
        }
        for (std::size_t i = 0; i < kept.size(); ++i) {
            if (!kept[i].Empty() && (i == 0 || kept[i] != kept[0])) {
                parts.push_back({{kept[i].x, kept[i].y}, {kept[i].width, kept[i].height}});
            }
        }
        if (parts.empty()) {
            return;
        }
        behind = color.CopyImageView({parts.data(), parts.size()});
        active = true;
        address = color_address;
        opacity = piece.opacity;
    }
    const std::size_t group = piece.group == VideoCore::KiuHud::Group::Bar ? 0 : 1;
    drawn[group] = HF::Union(drawn[group], HF::Intersect(pixels(piece.box), kept[group]));
}

void KiuHudFade::Finish() {
    if (!active) {
        return;
    }
    active = false;
    const vk::Pipeline pipeline = PipelineFor(framebuffer);
    const vk::DescriptorSet set = heap.Commit();
    update_queue.AddImageSampler(set, 0, 0, behind, sampler);
    renderpass_cache.BeginRendering(framebuffer, draw_rect);
    scheduler.Record([pipeline, set, layout = layout, share = 1.0f - opacity, rect = draw_rect,
                      drawn = drawn](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, set, {});
        cmdbuf.pushConstants(layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(share), &share);
        cmdbuf.setViewport(0, vk::Viewport{0.0f, 0.0f, static_cast<float>(rect.right),
                                           static_cast<float>(std::max(rect.top, rect.bottom)),
                                           0.0f, 1.0f});
        // The two apart, or where they meet once: never what was behind twice.
        std::array<HF::Pixels, 2> parts = drawn;
        if (!HF::Intersect(parts[0], parts[1]).Empty()) {
            parts = {HF::Union(parts[0], parts[1]), HF::Pixels{}};
        }
        for (const HF::Pixels& p : parts) {
            if (!p.Empty()) {
                cmdbuf.setScissor(0, vk::Rect2D{{p.x, p.y}, {p.width, p.height}});
                cmdbuf.draw(3, 1, 0, 0);
            }
        }
    });
    scheduler.MakeDirty(StateFlags::Pipeline);
}

vk::Pipeline KiuHudFade::PipelineFor(const Framebuffer* target) {
    const vk::Device device = instance.GetDevice();
    if (!layout) {
        vertex = Compile(VERT, vk::ShaderStageFlagBits::eVertex, device);
        fragment = Compile(FRAG, vk::ShaderStageFlagBits::eFragment, device);
        const vk::PushConstantRange range{vk::ShaderStageFlagBits::eFragment, 0, sizeof(float)};
        layout = device.createPipelineLayout({.setLayoutCount = 1,
                                              .pSetLayouts = &heap.Layout(),
                                              .pushConstantRangeCount = 1,
                                              .pPushConstantRanges = &range});
        sampler = device.createSampler({.magFilter = vk::Filter::eNearest,
                                        .minFilter = vk::Filter::eNearest,
                                        .mipmapMode = vk::SamplerMipmapMode::eNearest,
                                        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                                        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                                        .addressModeW = vk::SamplerAddressMode::eClampToEdge});
    }
    const auto color = target->Format(VideoCore::SurfaceType::Color);
    const auto depth = target->Format(VideoCore::SurfaceType::Depth);
    vk::Pipeline& made = pipelines[{static_cast<u32>(color), static_cast<u32>(depth)}];
    if (made) {
        return made;
    }
    const std::array stages = {
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex, .module = vertex, .pName = "main"},
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment, .module = fragment, .pName = "main"},
    };
    const vk::PipelineVertexInputStateCreateInfo vertex_input{};
    const vk::PipelineInputAssemblyStateCreateInfo input_assembly = {
        .topology = vk::PrimitiveTopology::eTriangleList};
    const vk::PipelineViewportStateCreateInfo viewport = {.viewportCount = 1, .scissorCount = 1};
    const vk::PipelineRasterizationStateCreateInfo raster = {
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .lineWidth = 1.0f};
    const vk::PipelineMultisampleStateCreateInfo multisampling = {
        .rasterizationSamples = vk::SampleCountFlagBits::e1};
    const vk::PipelineDepthStencilStateCreateInfo depth_stencil = {
        .depthTestEnable = false,
        .depthWriteEnable = false,
        .depthCompareOp = vk::CompareOp::eAlways};
    const vk::PipelineColorBlendAttachmentState blend = {
        .blendEnable = true,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eZero,
        .dstAlphaBlendFactor = vk::BlendFactor::eOne,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB};
    const vk::PipelineColorBlendStateCreateInfo blending = {.attachmentCount = 1,
                                                            .pAttachments = &blend};
    const std::array dynamic_states = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamic = {
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data()};
    const vk::GraphicsPipelineCreateInfo info = {
        .stageCount = static_cast<u32>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &blending,
        .pDynamicState = &dynamic,
        .layout = layout,
        .renderPass = renderpass_cache.GetDrawRenderpass(color, depth), // AzaharVR foveation
    };
    const auto [result, pipeline] = device.createGraphicsPipeline({}, info);
    ASSERT_MSG(result == vk::Result::eSuccess, "Unable to build the HUD's pipeline");
    made = pipeline;
    return made;
}

} // namespace Vulkan
