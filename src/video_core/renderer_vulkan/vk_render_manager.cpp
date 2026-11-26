// Copyright 2024-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include "common/assert.h"
#include "video_core/rasterizer_cache/pixel_format.h"
#include "common/frame_test_switches.h" // AzaharVR foveation
#include "video_core/renderer_vulkan/vk_foveation.h"
#include "video_core/vr_gaze.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_texture_runtime.h"

#include <vk_mem_alloc.h> // AzaharVR foveation

namespace Vulkan {

constexpr u32 MinDrawsToFlush = 20;

using VideoCore::PixelFormat;
using VideoCore::SurfaceType;

RenderManager::RenderManager(const Instance& instance, Scheduler& scheduler)
    : instance{instance}, scheduler{scheduler} {}

RenderManager::~RenderManager() {
    // AzaharVR foveation
    for (RateImage& r : rate_images) {
        instance.GetDevice().destroyImageView(r.view);
        vmaDestroyImage(instance.GetAllocator(), static_cast<VkImage>(r.image), r.allocation);
        vmaDestroyBuffer(instance.GetAllocator(), static_cast<VkBuffer>(r.staging),
                         r.staging_allocation);
    }
}

void RenderManager::BeginRendering(const Framebuffer* framebuffer,
                                   Common::Rectangle<u32> draw_rect) {
    const vk::Rect2D render_area = {
        .offset{
            .x = static_cast<s32>(draw_rect.left),
            .y = static_cast<s32>(draw_rect.bottom),
        },
        .extent{
            .width = draw_rect.GetWidth(),
            .height = draw_rect.GetHeight(),
        },
    };
    if (framebuffer->fsr_picture) { // AzaharVR foveation: the gaze moves the full rate area
        RefreshShadingRate(framebuffer->RateWidth(), framebuffer->RateHeight());
    }
    const RenderPass new_pass = {
        .framebuffer = framebuffer->Handle(),
        .render_pass = framebuffer->RenderPass(),
        .render_area = render_area,
        .clear = {},
        .do_clear = false,
    };
    images = framebuffer->Images();
    aspects = framebuffer->Aspects();
    shadow_rendering = framebuffer->shadow_rendering;
    BeginRendering(new_pass);
}

void RenderManager::BeginRendering(const RenderPass& new_pass) {
    if (pass == new_pass) [[likely]] {
        num_draws++;
        return;
    }

    EndRendering();
    scheduler.Record([info = new_pass](vk::CommandBuffer cmdbuf) {
        const vk::RenderPassBeginInfo renderpass_begin_info = {
            .renderPass = info.render_pass,
            .framebuffer = info.framebuffer,
            .renderArea = info.render_area,
            .clearValueCount = info.do_clear ? 1u : 0u,
            .pClearValues = &info.clear,
        };
        cmdbuf.beginRenderPass(renderpass_begin_info, vk::SubpassContents::eInline);
    });

    pass = new_pass;
}

void RenderManager::EndRendering() {
    if (!pass.render_pass) {
        return;
    }

    scheduler.Record([images = images, aspects = aspects,
                      shadow_rendering = shadow_rendering](vk::CommandBuffer cmdbuf) {
        u32 num_barriers = 0;
        vk::PipelineStageFlags pipeline_flags{};
        vk::AccessFlags src_access_flags{};
        std::array<vk::ImageMemoryBarrier, 2> barriers;
        for (u32 i = 0; i < images.size(); i++) {
            if (!images[i]) {
                continue;
            }
            const bool is_color = static_cast<bool>(aspects[i] & vk::ImageAspectFlagBits::eColor);
            if (is_color) {
                pipeline_flags |= shadow_rendering
                                      ? vk::PipelineStageFlagBits::eFragmentShader
                                      : vk::PipelineStageFlagBits::eColorAttachmentOutput;
                src_access_flags = shadow_rendering ? vk::AccessFlagBits::eShaderWrite
                                                    : vk::AccessFlagBits::eColorAttachmentWrite;
            } else {
                pipeline_flags |= vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                  vk::PipelineStageFlagBits::eLateFragmentTests;
                src_access_flags = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            }
            barriers[num_barriers++] = vk::ImageMemoryBarrier{
                .srcAccessMask = src_access_flags,
                .dstAccessMask =
                    vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead,
                .oldLayout = vk::ImageLayout::eGeneral,
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = images[i],
                .subresourceRange{
                    .aspectMask = aspects[i],
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            };
        }
        cmdbuf.endRenderPass();
        if (num_barriers == 0) {
            return;
        }
        cmdbuf.pipelineBarrier(pipeline_flags,
                               vk::PipelineStageFlagBits::eFragmentShader |
                                   vk::PipelineStageFlagBits::eTransfer,
                               vk::DependencyFlagBits::eByRegion, 0, nullptr, 0, nullptr,
                               num_barriers, barriers.data());
    });

    // Reset state.
    pass.render_pass = VK_NULL_HANDLE;
    images = {};
    aspects = {};
    shadow_rendering = false;

    // The Mali guide recommends flushing at the end of each major renderpass
    // Testing has shown this has a significant effect on rendering performance
    if (num_draws > MinDrawsToFlush && instance.ShouldFlush()) {
        scheduler.Flush();
        num_draws = 0;
    }
}

vk::RenderPass RenderManager::GetRenderpass(VideoCore::PixelFormat color,
                                            VideoCore::PixelFormat depth, bool is_clear) {
    std::scoped_lock lock{cache_mutex};

    const u32 color_index =
        color == VideoCore::PixelFormat::Invalid ? NumColorFormats : static_cast<u32>(color);
    const u32 depth_index =
        depth == VideoCore::PixelFormat::Invalid
            ? NumDepthFormats
            : (static_cast<u32>(depth - VideoCore::PixelFormat::NumColorFormat));

    ASSERT_MSG(color_index <= NumColorFormats && depth_index <= NumDepthFormats,
               "Invalid color index {} and/or depth_index {}", color_index, depth_index);

    vk::UniqueRenderPass& renderpass = cached_renderpasses[color_index][depth_index][is_clear];
    if (!renderpass) {
        const vk::Format color_format = instance.GetTraits(color).native;
        const vk::Format depth_format = instance.GetTraits(depth).native;
        const vk::AttachmentLoadOp load_op =
            is_clear ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
        renderpass = CreateRenderPass(color_format, depth_format, load_op);
    }

    return *renderpass;
}

namespace {
// AzaharVR foveation: the full rate centre in the picture's rate image (0..1 along its columns and
// rows), from the gaze or straight ahead, and it in texels (a change under one texel is none).
void CentreFor(u32 width, u32 height, float& cx, float& cy) {
    static int flip = -1;
    static u32 flip_checked = 0;
    if (flip < 0 || (++flip_checked % 120) == 0) { // the switch is read live, now and then
        const auto v = Common::FrameTestSwitches::Value("AZAHARFRAME_XR_FOVEATION_FLIP");
        flip = v ? std::atoi(v->c_str()) & 3 : 0;
    }
    float u, v;
    VideoCore::Gaze::Centre(u, v);
    VideoCore::Gaze::PictureToImage(u, v, width, height, flip, cx, cy);
}

u32 CentreKey(float cx, float cy, u32 w, u32 h) {
    const u32 x = static_cast<u32>(std::clamp(cx, -0.5f, 1.5f) * static_cast<float>(w) + 64.0f);
    const u32 y = static_cast<u32>(std::clamp(cy, -0.5f, 1.5f) * static_cast<float>(h) + 64.0f);
    return (x << 16) | (y & 0xFFFF);
}
} // namespace

// AzaharVR foveation: the render pass of GetRenderpass(color, depth, false) plus a shading rate
// attachment (read only, kept in its layout). Pipelines made for the plain pass are used with it:
// the attachment is not part of a render pass's compatibility.
vk::RenderPass RenderManager::GetRenderpassFsr(VideoCore::PixelFormat color,
                                               VideoCore::PixelFormat depth) {
    std::scoped_lock lock{cache_mutex};
    const u32 color_index =
        color == VideoCore::PixelFormat::Invalid ? NumColorFormats : static_cast<u32>(color);
    const u32 depth_index =
        depth == VideoCore::PixelFormat::Invalid
            ? NumDepthFormats
            : (static_cast<u32>(depth - VideoCore::PixelFormat::NumColorFormat));
    vk::UniqueRenderPass& renderpass = cached_fsr_renderpasses[color_index][depth_index];
    if (renderpass) {
        return *renderpass;
    }
    const vk::Format color_format = instance.GetTraits(color).native;
    const vk::Format depth_format = instance.GetTraits(depth).native;
    u32 count = 0;
    std::array<vk::AttachmentDescription2, 3> attachments;
    vk::AttachmentReference2 color_ref{};
    vk::AttachmentReference2 depth_ref{};
    bool use_color = false;
    bool use_depth = false;
    if (color_format != vk::Format::eUndefined) {
        attachments[count] = vk::AttachmentDescription2{
            .format = color_format,
            .loadOp = vk::AttachmentLoadOp::eLoad,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = vk::ImageLayout::eGeneral,
            .finalLayout = vk::ImageLayout::eGeneral,
        };
        color_ref = vk::AttachmentReference2{.attachment = count++,
                                             .layout = vk::ImageLayout::eGeneral};
        use_color = true;
    }
    if (depth_format != vk::Format::eUndefined) {
        attachments[count] = vk::AttachmentDescription2{
            .format = depth_format,
            .loadOp = vk::AttachmentLoadOp::eLoad,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .stencilLoadOp = vk::AttachmentLoadOp::eLoad,
            .stencilStoreOp = vk::AttachmentStoreOp::eStore,
            .initialLayout = vk::ImageLayout::eGeneral,
            .finalLayout = vk::ImageLayout::eGeneral,
        };
        depth_ref = vk::AttachmentReference2{.attachment = count++,
                                             .layout = vk::ImageLayout::eGeneral};
        use_depth = true;
    }
    attachments[count] = vk::AttachmentDescription2{
        .format = vk::Format::eR8Uint,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eFragmentShadingRateAttachmentOptimalKHR,
        .finalLayout = vk::ImageLayout::eFragmentShadingRateAttachmentOptimalKHR,
    };
    const vk::AttachmentReference2 rate_ref{
        .attachment = count++,
        .layout = vk::ImageLayout::eFragmentShadingRateAttachmentOptimalKHR,
    };
    const u32 texel = instance.FragmentShadingRateTexelSize();
    const vk::FragmentShadingRateAttachmentInfoKHR rate_info{
        .pFragmentShadingRateAttachment = &rate_ref,
        .shadingRateAttachmentTexelSize = {texel, texel},
    };
    const vk::SubpassDescription2 subpass{
        .pNext = &rate_info,
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = use_color ? 1u : 0u,
        .pColorAttachments = &color_ref,
        .pDepthStencilAttachment = use_depth ? &depth_ref : nullptr,
    };
    const vk::RenderPassCreateInfo2 info{
        .attachmentCount = count,
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    renderpass = instance.GetDevice().createRenderPass2KHRUnique(info);
    return *renderpass;
}

// AzaharVR foveation: the picture's rate image, uploaded the first time it is asked for.
vk::ImageView RenderManager::GetShadingRateView(u32 width, u32 height) {
    {
        std::scoped_lock lock{cache_mutex};
        for (const RateImage& r : rate_images) {
            if (r.width == width && r.height == height) {
                return r.view;
            }
        }
    }
    const u32 texel = instance.FragmentShadingRateTexelSize();
    const u32 w = (width + texel - 1) / texel;
    const u32 h = (height + texel - 1) / texel;
    float cx, cy;
    CentreFor(width, height, cx, cy);
    std::vector<u8> data = VideoCore::Foveation::MakeRateImage(instance.FoveationLevel(), width,
                                                               height, texel, cy, cx);
    data.resize((data.size() + 3) & ~std::size_t{3}); // a buffer update is in words

    RateImage r;
    r.width = width;
    r.height = height;
    r.centre_key = CentreKey(cx, cy, w, h);
    const VkImageCreateInfo image_info = static_cast<VkImageCreateInfo>(vk::ImageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR8Uint,
        .extent = {w, h, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eFragmentShadingRateAttachmentKHR |
                 vk::ImageUsageFlagBits::eTransferDst,
        .initialLayout = vk::ImageLayout::eUndefined,
    });
    const VmaAllocationCreateInfo alloc_info = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    VkImage raw_image{};
    const VkResult image_result = vmaCreateImage(instance.GetAllocator(), &image_info, &alloc_info,
                                                 &raw_image, &r.allocation, nullptr);
    ASSERT_MSG(image_result == VK_SUCCESS, "foveation: the rate image could not be made");
    r.image = vk::Image{raw_image};
    r.view = instance.GetDevice().createImageView({
        .image = r.image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR8Uint,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    });
    // The rates reach the image through this buffer, written in the command stream
    // (vkCmdUpdateBuffer), so a new set never overwrites one a pass before it still reads.
    const VkBufferCreateInfo buffer_info = static_cast<VkBufferCreateInfo>(vk::BufferCreateInfo{
        .size = data.size(),
        .usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst,
    });
    VkBuffer raw_buffer{};
    const VkResult buffer_result =
        vmaCreateBuffer(instance.GetAllocator(), &buffer_info, &alloc_info, &raw_buffer,
                        &r.staging_allocation, nullptr);
    ASSERT_MSG(buffer_result == VK_SUCCESS, "foveation: the rate image's buffer was not made");
    r.staging = vk::Buffer{raw_buffer};

    EndRendering(); // a copy is not recorded inside a render pass
    RecordRateUpload(r, std::move(data), true);
    std::scoped_lock lock{cache_mutex};
    rate_images.push_back(r);
    return r.view;
}

void RenderManager::RecordRateUpload(const RateImage& r, std::vector<u8> data, bool first) {
    const u32 texel = instance.FragmentShadingRateTexelSize();
    const u32 w = (r.width + texel - 1) / texel;
    const u32 h = (r.height + texel - 1) / texel;
    scheduler.Record([image = r.image, staging = r.staging, w, h, first,
                      data = std::move(data)](vk::CommandBuffer cmdbuf) {
        const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        const vk::MemoryBarrier transfer_barrier{
            .srcAccessMask = vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite,
        };
        const vk::ImageMemoryBarrier to_copy{
            .srcAccessMask = first ? vk::AccessFlags{}
                                   : vk::AccessFlagBits::eFragmentShadingRateAttachmentReadKHR,
            .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
            .oldLayout = first ? vk::ImageLayout::eUndefined
                               : vk::ImageLayout::eFragmentShadingRateAttachmentOptimalKHR,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = range,
        };
        cmdbuf.pipelineBarrier(
            first ? vk::PipelineStageFlags{vk::PipelineStageFlagBits::eTopOfPipe}
                  : (vk::PipelineStageFlagBits::eFragmentShadingRateAttachmentKHR |
                     vk::PipelineStageFlagBits::eTransfer),
            vk::PipelineStageFlagBits::eTransfer, {}, transfer_barrier, {}, to_copy);
        cmdbuf.updateBuffer(staging, 0, data.size(), data.data());
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                               vk::PipelineStageFlagBits::eTransfer, {}, transfer_barrier, {}, {});
        const vk::BufferImageCopy region{
            .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .imageExtent = {w, h, 1},
        };
        cmdbuf.copyBufferToImage(staging, image, vk::ImageLayout::eTransferDstOptimal, region);
        const vk::ImageMemoryBarrier to_rate{
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eFragmentShadingRateAttachmentReadKHR,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eFragmentShadingRateAttachmentOptimalKHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = range,
        };
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                               vk::PipelineStageFlagBits::eFragmentShadingRateAttachmentKHR, {},
                               {}, {}, to_rate);
    });
}

void RenderManager::RefreshShadingRate(u32 width, u32 height) {
    if (!VideoCore::Gaze::g_wanted.load(std::memory_order_relaxed)) {
        return; // the fixed centre was uploaded with the image
    }
    const u32 texel = instance.FragmentShadingRateTexelSize();
    const u32 w = (width + texel - 1) / texel;
    const u32 h = (height + texel - 1) / texel;
    float cx, cy;
    CentreFor(width, height, cx, cy);
    const u32 key = CentreKey(cx, cy, w, h);
    RateImage found;
    {
        std::scoped_lock lock{cache_mutex};
        RateImage* hit = nullptr;
        for (RateImage& r : rate_images) {
            if (r.width == width && r.height == height) {
                hit = &r;
                break;
            }
        }
        if (!hit || hit->centre_key == key) {
            return;
        }
        hit->centre_key = key;
        found = *hit;
    }
    std::vector<u8> data = VideoCore::Foveation::MakeRateImage(instance.FoveationLevel(), width,
                                                               height, texel, cy, cx);
    data.resize((data.size() + 3) & ~std::size_t{3});
    EndRendering();
    RecordRateUpload(found, std::move(data), false);
}

vk::UniqueRenderPass RenderManager::CreateRenderPass(vk::Format color, vk::Format depth,
                                                     vk::AttachmentLoadOp load_op) const {
    u32 attachment_count = 0;
    std::array<vk::AttachmentDescription, 2> attachments;

    bool use_color = false;
    vk::AttachmentReference color_attachment_ref{};
    bool use_depth = false;
    vk::AttachmentReference depth_attachment_ref{};

    if (color != vk::Format::eUndefined) {
        attachments[attachment_count] = vk::AttachmentDescription{
            .format = color,
            .loadOp = load_op,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = vk::ImageLayout::eGeneral,
            .finalLayout = vk::ImageLayout::eGeneral,
        };

        color_attachment_ref = vk::AttachmentReference{
            .attachment = attachment_count++,
            .layout = vk::ImageLayout::eGeneral,
        };

        use_color = true;
    }

    if (depth != vk::Format::eUndefined) {
        attachments[attachment_count] = vk::AttachmentDescription{
            .format = depth,
            .loadOp = load_op,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .stencilLoadOp = load_op,
            .stencilStoreOp = vk::AttachmentStoreOp::eStore,
            .initialLayout = vk::ImageLayout::eGeneral,
            .finalLayout = vk::ImageLayout::eGeneral,
        };

        depth_attachment_ref = vk::AttachmentReference{
            .attachment = attachment_count++,
            .layout = vk::ImageLayout::eGeneral,
        };

        use_depth = true;
    }

    const vk::SubpassDescription subpass = {
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = use_color ? 1u : 0u,
        .pColorAttachments = &color_attachment_ref,
        .pResolveAttachments = 0,
        .pDepthStencilAttachment = use_depth ? &depth_attachment_ref : nullptr,
    };

    const vk::RenderPassCreateInfo renderpass_info = {
        .attachmentCount = attachment_count,
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 0,
        .pDependencies = nullptr,
    };

    return instance.GetDevice().createRenderPassUnique(renderpass_info);
}

} // namespace Vulkan
