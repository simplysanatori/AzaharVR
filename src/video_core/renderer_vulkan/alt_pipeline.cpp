// Copyright 2026 AzaharVR contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <cstddef>

#include "common/assert.h"
#include "video_core/renderer_vulkan/alt_pipeline.h"
#include "video_core/renderer_vulkan/alt_present.h"

namespace Vulkan {

const std::string_view kAltOverlayFrag = R"(
#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 frag_tex_coord;
layout (location = 0) out vec4 color;

layout (set = 0, binding = 0) uniform sampler2D screen_textures[3];

#ifdef OVERLAY_SRGB_TARGET
vec3 ToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}
#endif

void main() {
    const vec4 overlay = texture(screen_textures[1], frag_tex_coord);
    vec3 c = texture(screen_textures[0], frag_tex_coord).rgb * (1.0 - overlay.a) + overlay.rgb;
#ifdef OVERLAY_SRGB_TARGET
    c = ToLinear(c);
#endif
    color = vec4(c, 1.0);
}
)";

vk::Pipeline BuildAltPipeline(vk::Device device, vk::ShaderModule vertex_shader,
                              vk::ShaderModule fragment_shader, vk::PipelineLayout layout,
                              vk::RenderPass render_pass, bool constant_alpha) {
    const vk::VertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(AltScreenVertex),
        .inputRate = vk::VertexInputRate::eVertex,
    };
    const std::array attributes = {
        vk::VertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = vk::Format::eR32G32Sfloat,
            .offset = offsetof(AltScreenVertex, x),
        },
        vk::VertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = vk::Format::eR32G32Sfloat,
            .offset = offsetof(AltScreenVertex, u),
        },
    };
    const vk::PipelineVertexInputStateCreateInfo vertex_input = {
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<u32>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };
    const vk::PipelineInputAssemblyStateCreateInfo input_assembly = {
        .topology = vk::PrimitiveTopology::eTriangleStrip,
    };
    const vk::PipelineRasterizationStateCreateInfo raster_state = {
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eClockwise,
        .lineWidth = 1.0f,
    };
    const vk::PipelineMultisampleStateCreateInfo multisampling = {
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
    };
    const vk::PipelineColorBlendAttachmentState blend = {
        .blendEnable = constant_alpha,
        .srcColorBlendFactor = vk::BlendFactor::eConstantAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusConstantAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eConstantAlpha,
        .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusConstantAlpha,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    const vk::PipelineColorBlendStateCreateInfo color_blending = {
        .attachmentCount = 1,
        .pAttachments = &blend,
    };
    const vk::PipelineViewportStateCreateInfo viewport_info = {
        .viewportCount = 1,
        .scissorCount = 1,
    };
    constexpr std::array dynamic_states = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
        vk::DynamicState::eBlendConstants, // last: only with constant_alpha
    };
    const vk::PipelineDynamicStateCreateInfo dynamic_info = {
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()) - (constant_alpha ? 0u : 1u),
        .pDynamicStates = dynamic_states.data(),
    };
    const vk::PipelineDepthStencilStateCreateInfo depth_info = {
        .depthCompareOp = vk::CompareOp::eAlways,
    };
    const std::array stages = {
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = vertex_shader,
            .pName = "main",
        },
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = fragment_shader,
            .pName = "main",
        },
    };
    const vk::GraphicsPipelineCreateInfo info = {
        .stageCount = static_cast<u32>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_info,
        .pRasterizationState = &raster_state,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_info,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_info,
        .layout = layout,
        .renderPass = render_pass,
    };
    const auto [result, pipeline] = device.createGraphicsPipeline({}, info);
    ASSERT_MSG(result == vk::Result::eSuccess, "Unable to build a presenter's pipeline");
    return pipeline;
}

} // namespace Vulkan
