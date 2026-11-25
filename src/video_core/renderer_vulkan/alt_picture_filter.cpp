// Copyright 2026 AzaharVR contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <string>
#include <string_view>

#include "common/logging/log.h"
#include "video_core/host_shaders/full_screen_triangle_vert.h"
#include "video_core/host_shaders/texture_filtering/bicubic_frag.h"
#include "video_core/host_shaders/texture_filtering/mmpx_frag.h"
#include "video_core/host_shaders/texture_filtering/refine_frag.h"
#include "video_core/host_shaders/texture_filtering/scale_force_frag.h"
#include "video_core/host_shaders/texture_filtering/xbrz_freescale_frag.h"
#include "video_core/renderer_vulkan/alt_picture_filter.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

namespace {

/// Anime4K's shader reads three; the others the first.
constexpr std::array<vk::DescriptorSetLayoutBinding, 3> kBindings = {{
    {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
    {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
    {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
}};

/// As the 3DS's filters' (vk_blit_helper.cpp): the vertex shader reads the first two, xBRZ the
/// scale.
struct PushConstants {
    std::array<float, 2> tex_scale;
    std::array<float, 2> tex_offset;
    float res_scale;
};

constexpr vk::ShaderStageFlags kPushStages =
    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

std::string_view Source(Settings::TextureFilter filter) {
    switch (filter) {
    case Settings::TextureFilter::Anime4K:
        return HostShaders::REFINE_FRAG;
    case Settings::TextureFilter::Bicubic:
        return HostShaders::BICUBIC_FRAG;
    case Settings::TextureFilter::ScaleForce:
        return HostShaders::SCALE_FORCE_FRAG;
    case Settings::TextureFilter::xBRZ:
        return HostShaders::XBRZ_FREESCALE_FRAG;
    case Settings::TextureFilter::MMPX:
        return HostShaders::MMPX_FRAG;
    case Settings::TextureFilter::NoFilter:
        break;
    }
    return {};
}

/// The shaders ask for medium precision, which is enough for a 3DS texture; a whole picture's
/// coordinates need more where the GPU takes the hint.
std::string HighPrecision(std::string_view source) {
    std::string code{source};
    constexpr std::string_view medium = "precision mediump float;";
    if (const auto at = code.find(medium); at != std::string::npos) {
        code.replace(at, medium.size(), "precision highp float;");
    }
    return code;
}

} // namespace

AltPictureFilter::AltPictureFilter(const Instance& instance_, Scheduler& scheduler_,
                                   DescriptorUpdateQueue& update_queue_)
    : instance{instance_}, scheduler{scheduler_}, update_queue{update_queue_} {
    const vk::Device device = instance.GetDevice();
    heap = std::make_unique<DescriptorHeap>(instance, scheduler.GetMasterSemaphore(), kBindings,
                                            32);
    const vk::PushConstantRange push_range = {
        .stageFlags = kPushStages,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    const vk::DescriptorSetLayout set_layout = heap->Layout();
    layout = device.createPipelineLayoutUnique({
        .setLayoutCount = 1,
        .pSetLayouts = &set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    });

    const vk::AttachmentDescription attachment = {
        .format = vk::Format::eR8G8B8A8Unorm,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eDontCare,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eUndefined,
        .finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::AttachmentReference color_ref = {
        .attachment = 0,
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
    };
    const vk::SubpassDescription subpass = {
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
    };
    // The image was sampled by the draw before, and is sampled by the next.
    const std::array dependencies = {
        vk::SubpassDependency{
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = vk::PipelineStageFlagBits::eFragmentShader |
                            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits::eShaderRead,
            .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
        },
        vk::SubpassDependency{
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
            .dstStageMask = vk::PipelineStageFlagBits::eFragmentShader,
            .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
            .dstAccessMask = vk::AccessFlagBits::eShaderRead,
        },
    };
    render_pass = device.createRenderPassUnique({
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = static_cast<u32>(dependencies.size()),
        .pDependencies = dependencies.data(),
    });

    sampler = device.createSamplerUnique({
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .maxAnisotropy = 1.0f,
        .minLod = 0.0f,
        .maxLod = 0.0f,
    });
    vertex_shader = vk::UniqueShaderModule{
        Compile(HostShaders::FULL_SCREEN_TRIANGLE_VERT, vk::ShaderStageFlagBits::eVertex, device),
        device};
}

AltPictureFilter::~AltPictureFilter() = default;

vk::Framebuffer AltPictureFilter::MakeFramebuffer(vk::ImageView view, u32 width, u32 height) {
    return instance.GetDevice().createFramebuffer({
        .renderPass = *render_pass,
        .attachmentCount = 1,
        .pAttachments = &view,
        .width = width,
        .height = height,
        .layers = 1,
    });
}

vk::Pipeline AltPictureFilter::Pipeline(Settings::TextureFilter filter) {
    const std::size_t index = static_cast<std::size_t>(filter);
    const std::string_view source = Source(filter);
    if (index >= kFilters || source.empty() || failed[index]) {
        return nullptr;
    }
    if (pipelines[index]) {
        return *pipelines[index];
    }
    const vk::Device device = instance.GetDevice();
    shaders[index] = vk::UniqueShaderModule{
        Compile(HighPrecision(source), vk::ShaderStageFlagBits::eFragment, device), device};
    const std::array stages = {
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = *vertex_shader,
            .pName = "main",
        },
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = *shaders[index],
            .pName = "main",
        },
    };
    const vk::PipelineVertexInputStateCreateInfo vertex_input{};
    const vk::PipelineInputAssemblyStateCreateInfo input_assembly = {
        .topology = vk::PrimitiveTopology::eTriangleList,
    };
    const vk::PipelineViewportStateCreateInfo viewport = {
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const vk::PipelineRasterizationStateCreateInfo raster = {
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eClockwise,
        .lineWidth = 1.0f,
    };
    const vk::PipelineMultisampleStateCreateInfo multisample = {
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
    };
    const vk::PipelineColorBlendAttachmentState blend = {
        .blendEnable = false,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    const vk::PipelineColorBlendStateCreateInfo color_blend = {
        .attachmentCount = 1,
        .pAttachments = &blend,
    };
    const std::array dynamic_states = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamic = {
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };
    const vk::GraphicsPipelineCreateInfo info = {
        .stageCount = static_cast<u32>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisample,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic,
        .layout = *layout,
        .renderPass = *render_pass,
    };
    const auto result = device.createGraphicsPipeline({}, info);
    if (result.result != vk::Result::eSuccess) {
        LOG_ERROR(Render_Vulkan, "alt_xr: no pipeline for texture filter {}", index);
        failed[index] = true;
        return nullptr;
    }
    pipelines[index] = vk::UniquePipeline{result.value, device};
    return *pipelines[index];
}

bool AltPictureFilter::Draw(Settings::TextureFilter filter, vk::ImageView source,
                            vk::Framebuffer target, u32 width, u32 height, u32 scale) {
    const vk::Pipeline pipeline = Pipeline(filter);
    if (!pipeline) {
        return false;
    }
    const vk::DescriptorSet set = heap->Commit();
    for (u8 binding = 0; binding < kBindings.size(); binding++) {
        update_queue.AddImageSampler(set, binding, 0, source, *sampler,
                                     vk::ImageLayout::eShaderReadOnlyOptimal);
    }
    update_queue.Flush();
    scheduler.Record([pipeline, set, target, width, height, scale, pass = *render_pass,
                      layout = *layout](vk::CommandBuffer cmdbuf) {
        const vk::Rect2D area{{0, 0}, {width, height}};
        cmdbuf.beginRenderPass(
            vk::RenderPassBeginInfo{
                .renderPass = pass,
                .framebuffer = target,
                .renderArea = area,
            },
            vk::SubpassContents::eInline);
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, set, {});
        const PushConstants constants{
            .tex_scale = {1.0f, 1.0f},
            .tex_offset = {0.0f, 0.0f},
            .res_scale = static_cast<float>(scale),
        };
        cmdbuf.pushConstants(layout, kPushStages, 0, sizeof(constants), &constants);
        cmdbuf.setViewport(0, vk::Viewport{0.0f, 0.0f, static_cast<float>(width),
                                           static_cast<float>(height), 0.0f, 1.0f});
        cmdbuf.setScissor(0, area);
        cmdbuf.draw(3, 1, 0, 0);
        cmdbuf.endRenderPass();
    });
    return true;
}

} // namespace Vulkan
