// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <memory>

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/math_util.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "common/xr_pacer.h"
#include "core/frontend/alt_screens.h"
#include "core/frontend/emu_window.h"
#include "video_core/host_shaders/vulkan_present_vert.h"
#include "video_core/renderer_vulkan/alt_graphics.h"
#include "video_core/renderer_vulkan/alt_pipeline.h"
#include "video_core/renderer_vulkan/alt_present.h"
#include "video_core/renderer_vulkan/alt_window_target.h"
#include "video_core/renderer_vulkan/alt_xr_renderer.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/vk_stream_buffer.h"
#include "video_core/renderer_vulkan/vk_xr_target.h"
#include "video_core/vulkan_hooks.h"

#include <vk_mem_alloc.h>

namespace Vulkan {

namespace {

class DummyWindow final : public Frontend::EmuWindow {
public:
    DummyWindow() {
        window_info.type = Frontend::WindowSystemType::Headless;
    }
    void PollEvents() override {}
};

constexpr std::array<vk::DescriptorSetLayoutBinding, 1> kPresentBindings = {{
    {0, vk::DescriptorType::eCombinedImageSampler, 3, vk::ShaderStageFlagBits::eFragment},
}};

bool EncodesSrgb(vk::Format format) {
    return format == vk::Format::eR8G8B8A8Srgb || format == vk::Format::eB8G8R8A8Srgb;
}

constexpr u32 kVertexBufferSize = sizeof(AltScreenVertex) * 1024;
constexpr u64 kUploadBufferSize = 16ULL * 1024 * 1024;
constexpr u32 kDummySwapchainExtent = 16;
/// Fast forward: the window gets at most about 80 pictures a second (with vsync each one waits
/// for the display, which would hold the game back).
constexpr auto kSkippableInterval = std::chrono::milliseconds{12};

vk::Extent2D TargetExtent(const AltScreenImage& image) {
    const auto size = AltTargetSize(image.width, image.height, image.scale, image.max_scale);
    return vk::Extent2D{size[0], size[1]};
}

struct ScreenTexture {
    u32 width = 0;
    u32 height = 0;
    vk::Image image{};
    vk::ImageView view{};
    VmaAllocation allocation{};
};

} // namespace

struct AltXrRenderer::Impl {
    VideoCore::XrScreenSink* sink = nullptr;
    std::unique_ptr<Instance> instance;
    std::unique_ptr<Scheduler> scheduler;
    std::unique_ptr<XrQueueRegistration> queue_reg;
    std::unique_ptr<StreamBuffer> vertex_buffer;
    std::unique_ptr<StreamBuffer> upload_buffer;
    std::unique_ptr<DescriptorUpdateQueue> update_queue;
    std::unique_ptr<DescriptorHeap> present_heap;
    vk::UniquePipelineLayout present_pipeline_layout;
    vk::ShaderModule present_vertex_shader{};
    std::array<vk::Sampler, 2> samplers{};
    u64 upload_size = kUploadBufferSize;
    ScreenTexture primary_tex{};
    ScreenTexture secondary_tex{};
    ScreenTexture overlay_tex{};
    ScreenTexture dummy_tex{};
    vk::Extent2D secondary_extent{kDummySwapchainExtent, kDummySwapchainExtent}; ///< the last's
    std::unique_ptr<XrPresentTarget> xr_target;
    u64 xr_target_generation = 0;
    vk::ShaderModule overlay_shader{}; ///< both made for xr_target with the first overlay
    vk::Pipeline overlay_pipeline{};
    Frontend::EmuWindow* window = nullptr;
    Frontend::EmuWindow* secondary_window = nullptr;
    std::unique_ptr<AltWindowTarget> window_target;
    std::unique_ptr<AltWindowTarget> secondary_target; ///< Separate Windows, made when first used
    std::chrono::steady_clock::time_point window_presented{};
    u64 previous_frame_tick = 0;
    u64 copy_tick = 0; ///< when a core's picture was last copied in this Present (0: not)
    bool presented_vr = false; ///< the last Present's output
    bool ready = false;

    bool XrShowsScreens() const {
        return sink && sink->IsActive() && UsesXrDevice(*instance);
    }

    /// Both screens' newest pictures, for the present shader. A screen that has none is black; one
    /// that stops being sent keeps its last. `overlay`: the overlay is at kAltOverlaySlot, always
    /// smooth (its pixels are no multiple of the screen's). `uneven`: so are the screens'.
    vk::DescriptorSet CommitScreens(vk::ImageView secondary_view, bool overlay,
                                    bool uneven = false) {
        const vk::Sampler sampler = samplers[!uneven && !Settings::values.filter_mode.GetValue()];
        const vk::DescriptorSet set = present_heap->Commit();
        const std::array views = {primary_tex.view, primary_tex.view, secondary_view};
        for (u32 i = 0; i < views.size(); i++) {
            const bool over = overlay && i == kAltOverlaySlot;
            const vk::ImageView view = over ? overlay_tex.view : views[i];
            update_queue->AddImageSampler(set, 0, static_cast<u8>(i),
                                          view ? view : dummy_tex.view,
                                          over ? samplers[0] : sampler,
                                          vk::ImageLayout::eShaderReadOnlyOptimal);
        }
        update_queue->Flush();
        return set;
    }

    void PresentToWindow(bool overlay) {
        const auto start = std::chrono::steady_clock::now();
        const vk::DescriptorSet screens = CommitScreens(secondary_tex.view, overlay);
        window_target->Draw(*vertex_buffer, screens, overlay);
        const auto game = Layout::GetAltScreens();
        if (secondary_window && !sink && game && game->HasBottom() &&
            Settings::values.layout_option.GetValue() == Settings::LayoutOption::SeparateWindows) {
            if (!secondary_target) {
                secondary_target = std::make_unique<AltWindowTarget>(
                    *secondary_window, *instance, *scheduler, present_vertex_shader,
                    *present_pipeline_layout);
            }
            secondary_target->Draw(*vertex_buffer, screens, overlay);
        }
        window_presented = std::chrono::steady_clock::now();
        if (sink) {
            sink->NoteWindowPresent(static_cast<u32>(
                std::chrono::duration_cast<std::chrono::microseconds>(window_presented - start)
                    .count()));
        }
        LimitFramesAheadOfGpu();
    }

    /// The window while the headset shows the screens: an empty picture about once a second, a
    /// sign of life for the window system.
    void KeepWindowAlive() {
        const auto now = std::chrono::steady_clock::now();
        if (!window_target || now - window_presented < std::chrono::seconds{1}) {
            return;
        }
        window_presented = now;
        window_target->Clear(*vertex_buffer, CommitScreens(secondary_tex.view, false));
    }

    void LimitFramesAheadOfGpu() {
        if (previous_frame_tick != 0) {
            scheduler->Wait(previous_frame_tick);
        }
        previous_frame_tick = scheduler->CurrentTick() - 1;
    }

    void DestroyTexture(ScreenTexture& tex) {
        if (!instance) {
            tex = {};
            return;
        }
        const vk::Device device = instance->GetDevice();
        if (tex.view) {
            device.destroyImageView(tex.view);
        }
        if (tex.image) {
            vmaDestroyImage(instance->GetAllocator(), static_cast<VkImage>(tex.image),
                            tex.allocation);
        }
        tex = {};
    }

    bool EnsureTexture(ScreenTexture& tex, u32 width, u32 height) {
        if (tex.image && tex.width == width && tex.height == height) {
            return true;
        }
        if (tex.image && scheduler) {
            scheduler->Finish();
        }
        DestroyTexture(tex);

        const vk::ImageCreateInfo image_info = {
            .imageType = vk::ImageType::e2D,
            .format = vk::Format::eR8G8B8A8Unorm,
            .extent = {width, height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            .initialLayout = vk::ImageLayout::eUndefined,
        };
        const VmaAllocationCreateInfo alloc_info = {
            .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        };
        VkImage raw_image{};
        const VkImageCreateInfo raw_info = static_cast<VkImageCreateInfo>(image_info);
        const VkResult res = vmaCreateImage(instance->GetAllocator(), &raw_info, &alloc_info,
                                            &raw_image, &tex.allocation, nullptr);
        if (res != VK_SUCCESS) {
            LOG_ERROR(Render_Vulkan, "alt_xr: failed to allocate {}x{} screen texture ({})", width,
                      height, static_cast<int>(res));
            tex.allocation = {};
            return false;
        }
        tex.image = vk::Image{raw_image};
        tex.view = instance->GetDevice().createImageView({
            .image = tex.image,
            .viewType = vk::ImageViewType::e2D,
            .format = vk::Format::eR8G8B8A8Unorm,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        });
        tex.width = width;
        tex.height = height;
        return true;
    }

    /// Room for three frames of these pictures, so that none waits for the GPU to take the last.
    void EnsureUploads(std::initializer_list<const AltScreenImage*> images) {
        u64 frame = 0;
        for (const AltScreenImage* image : images) {
            if (image->image == 0) {
                frame += u64{image->width} * image->height * sizeof(u32);
            }
        }
        if (frame * 3 <= upload_size) {
            return;
        }
        scheduler->Finish();
        upload_buffer.reset();
        upload_size = frame * 3;
        upload_buffer =
            std::make_unique<StreamBuffer>(*instance, *scheduler,
                                           vk::BufferUsageFlagBits::eTransferSrc, upload_size,
                                           BufferType::Upload);
    }

    /// `see_through`: with the picture's own alpha (RGBA), else opaque.
    bool UploadImage(ScreenTexture& tex, const AltScreenImage& img, bool see_through = false) {
        if (!EnsureTexture(tex, img.width, img.height)) {
            return false;
        }
        const u32 byte_size = img.width * img.height * sizeof(u32);
        if (byte_size > upload_size) {
            LOG_ERROR(Render_Vulkan, "alt_xr: frame {}x{} exceeds staging buffer", img.width,
                      img.height);
            return false;
        }
        auto [dst_bytes, buf_offset, invalidate] = upload_buffer->Map(byte_size, 16);
        static_cast<void>(invalidate);
        const u32 stride = img.stride_pixels ? img.stride_pixels : img.width;
        u32* dst = reinterpret_cast<u32*>(dst_bytes);
        for (u32 y = 0; y < img.height; ++y) {
            const u32* src = img.pixels + static_cast<std::size_t>(y) * stride;
            u32* row = dst + static_cast<std::size_t>(y) * img.width;
            if (img.bgra) {
                for (u32 x = 0; x < img.width; ++x) {
                    const u32 p = src[x];
                    row[x] = ((p & 0x00FF0000u) >> 16) | (p & 0x0000FF00u) |
                             ((p & 0x000000FFu) << 16) | 0xFF000000u;
                }
            } else if (see_through) {
                std::memcpy(row, src, img.width * sizeof(u32));
            } else {
                for (u32 x = 0; x < img.width; ++x) {
                    row[x] = src[x] | 0xFF000000u;
                }
            }
        }
        upload_buffer->Commit(byte_size);

        const vk::Buffer src_buf = upload_buffer->Handle();
        const vk::Image dst_image = tex.image;
        const u32 w = img.width;
        const u32 h = img.height;
        scheduler->Record([src_buf, buf_offset, dst_image, w, h](vk::CommandBuffer cmdbuf) {
            constexpr vk::ImageSubresourceRange range = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };
            const vk::ImageMemoryBarrier to_dst = {
                .srcAccessMask = {},
                .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eTransferDstOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = dst_image,
                .subresourceRange = range,
            };
            cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader |
                                       vk::PipelineStageFlagBits::eTopOfPipe,
                                   vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, to_dst);

            const vk::BufferImageCopy region = {
                .bufferOffset = buf_offset,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                .imageOffset = {0, 0, 0},
                .imageExtent = {w, h, 1},
            };
            cmdbuf.copyBufferToImage(src_buf, dst_image, vk::ImageLayout::eTransferDstOptimal,
                                     region);

            const vk::ImageMemoryBarrier to_read = {
                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = dst_image,
                .subresourceRange = range,
            };
            cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                   vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, to_read);
        });
        return true;
    }

    /// A screen's newest picture, from the GPU or the CPU.
    bool TakeImage(ScreenTexture& tex, const AltScreenImage& img) {
        if (img.image == 0) {
            return UploadImage(tex, img);
        }
        if (img.alive.expired()) { // what drew it is gone (a paused game): its last stays
            return static_cast<bool>(tex.image);
        }
        if (!EnsureTexture(tex, img.width, img.height)) {
            return false;
        }
        copy_tick = scheduler->CurrentTick();
        scheduler->Record([src = vk::Image{reinterpret_cast<VkImage>(img.image)}, dst = tex.image,
                           layer = img.layer, w = img.width,
                           h = img.height](vk::CommandBuffer cmdbuf) {
            constexpr vk::ImageSubresourceRange range = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };
            const vk::MemoryBarrier drawn = {
                .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
                .dstAccessMask = vk::AccessFlagBits::eTransferRead,
            };
            const vk::ImageMemoryBarrier to_dst = {
                .srcAccessMask = {},
                .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eTransferDstOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = dst,
                .subresourceRange = range,
            };
            cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                       vk::PipelineStageFlagBits::eTransfer |
                                       vk::PipelineStageFlagBits::eFragmentShader,
                                   vk::PipelineStageFlagBits::eTransfer, {}, drawn, {}, to_dst);
            const vk::ImageCopy region = {
                .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, layer, 1},
                .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                .extent = {w, h, 1},
            };
            cmdbuf.copyImage(src, vk::ImageLayout::eGeneral, dst,
                             vk::ImageLayout::eTransferDstOptimal, region);
            const vk::ImageMemoryBarrier to_read = {
                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = dst,
                .subresourceRange = range,
            };
            cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                   vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, to_read);
        });
        return true;
    }

    bool UploadOverlay(const AltScreenImage& overlay) {
        return overlay.HasPicture() && UploadImage(overlay_tex, overlay, true);
    }

    /// The overlay's pipeline for xr_target's images.
    vk::Pipeline OverlayPipeline() {
        if (!overlay_pipeline) {
            const vk::Device device = instance->GetDevice();
            const bool srgb = EncodesSrgb(static_cast<vk::Format>(sink->SwapchainFormat()));
            overlay_shader = Compile(kAltOverlayFrag, vk::ShaderStageFlagBits::eFragment, device,
                                     srgb ? "#define OVERLAY_SRGB_TARGET\n" : "");
            overlay_pipeline =
                BuildAltPipeline(device, present_vertex_shader, overlay_shader,
                                 *present_pipeline_layout, xr_target->RenderPass(), false);
        }
        return overlay_pipeline;
    }

    /// With what was made for it; its destructor waits for the GPU.
    void DropXrTarget() {
        xr_target.reset();
        if (overlay_pipeline) {
            const vk::Device device = instance->GetDevice();
            device.destroyPipeline(overlay_pipeline);
            device.destroyShaderModule(overlay_shader);
            overlay_pipeline = nullptr;
            overlay_shader = nullptr;
        }
    }

    void DrawQuad(const XrPresentTarget::Target& target, vk::DescriptorSet present_set,
                  vk::Pipeline pipe, u32 screen_id, u32 src_w, u32 src_h, int layer_flags) {
        const float w = static_cast<float>(target.width);
        const float h = static_cast<float>(target.height);
        const auto vertices = AltScreenQuad(0.0f, 0.0f, w, h, true);
        const u32 size = static_cast<u32>(sizeof(AltScreenVertex) * vertices.size());
        auto [data, offset, invalidate] = vertex_buffer->Map(size, 16);
        static_cast<void>(invalidate);
        std::memcpy(data, vertices.data(), size);
        vertex_buffer->Commit(size);

        AltPresentUniformData info{};
        info.modelview = Orthographic(target.width, target.height);
        info.i_resolution = Common::MakeVec(static_cast<f32>(src_w), static_cast<f32>(src_h),
                                            1.0f / static_cast<f32>(std::max<u32>(src_w, 1)),
                                            1.0f / static_cast<f32>(std::max<u32>(src_h, 1)));
        info.o_resolution = Common::MakeVec(w, h, 1.0f / std::max(w, 1.0f),
                                            1.0f / std::max(h, 1.0f));
        info.screen_id_l = static_cast<int>(screen_id);
        info.screen_id_r = static_cast<int>(screen_id);
        info.layer = layer_flags;

        const vk::RenderPass rp = xr_target->RenderPass();
        const vk::PipelineLayout layout = *present_pipeline_layout;
        const vk::Buffer vbuf = vertex_buffer->Handle();

        scheduler->Record([target, present_set, rp, pipe, layout, vbuf, offset,
                           info](vk::CommandBuffer cmdbuf) {
            const vk::ClearValue clear{
                .color = vk::ClearColorValue{.float32 = std::array{0.f, 0.f, 0.f, 1.f}}};
            const vk::RenderPassBeginInfo begin_info = {
                .renderPass = rp,
                .framebuffer = target.framebuffer,
                .renderArea = {.offset = {0, 0}, .extent = {target.fb_width, target.fb_height}},
                .clearValueCount = 1,
                .pClearValues = &clear,
            };
            cmdbuf.beginRenderPass(begin_info, vk::SubpassContents::eInline);
            cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipe);
            cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, present_set, {});
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
            cmdbuf.setViewport(0, viewport);
            cmdbuf.setScissor(0, scissor);
            cmdbuf.pushConstants(
                layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
                sizeof(info), &info);
            const u32 first_vertex = offset / sizeof(AltScreenVertex);
            cmdbuf.bindVertexBuffers(0, vbuf, {0});
            cmdbuf.draw(4, 1, first_vertex, 0);
            cmdbuf.endRenderPass();
        });
    }

    void ClearTarget(const XrPresentTarget::Target& target) {
        if (!target.framebuffer) {
            return;
        }
        const vk::RenderPass rp = xr_target->RenderPass();
        scheduler->Record([target, rp](vk::CommandBuffer cmdbuf) {
            const vk::ClearValue clear{
                .color = vk::ClearColorValue{.float32 = std::array{0.f, 0.f, 0.f, 1.f}}};
            const vk::RenderPassBeginInfo begin_info = {
                .renderPass = rp,
                .framebuffer = target.framebuffer,
                .renderArea = {.offset = {0, 0}, .extent = {target.fb_width, target.fb_height}},
                .clearValueCount = 1,
                .pClearValues = &clear,
            };
            cmdbuf.beginRenderPass(begin_info, vk::SubpassContents::eInline);
            cmdbuf.endRenderPass();
        });
    }

    void Shutdown() {
        ready = false;
        DropXrTarget();
        window_target.reset();
        secondary_target.reset();
        previous_frame_tick = 0;
        if (scheduler) {
            scheduler->Finish();
        }
        if (instance && scheduler) {
            std::scoped_lock lock{scheduler->submit_mutex};
            instance->GetDevice().waitIdle();
        }
        DestroyTexture(primary_tex);
        DestroyTexture(secondary_tex);
        DestroyTexture(overlay_tex);
        DestroyTexture(dummy_tex);
        if (instance) {
            const vk::Device device = instance->GetDevice();
            for (auto& sampler : samplers) {
                if (sampler) {
                    device.destroySampler(sampler);
                    sampler = nullptr;
                }
            }
            if (present_vertex_shader) {
                device.destroyShaderModule(present_vertex_shader);
                present_vertex_shader = nullptr;
            }
        }
        present_pipeline_layout.reset();
        present_heap.reset();
        update_queue.reset();
        upload_buffer.reset();
        upload_size = kUploadBufferSize;
        secondary_extent = vk::Extent2D{kDummySwapchainExtent, kDummySwapchainExtent};
        vertex_buffer.reset();
        if (sink) {
            sink->SetTopStereo(true);
        }
        queue_reg.reset();
        scheduler.reset();
        instance.reset();
        sink = nullptr;
        window = nullptr;
        secondary_window = nullptr;
        xr_target_generation = 0;
    }
};

AltXrRenderer::AltXrRenderer() : impl{std::make_unique<Impl>()} {}

AltXrRenderer::~AltXrRenderer() {
    Shutdown();
}

bool AltXrRenderer::Init(Frontend::EmuWindow* window, Frontend::EmuWindow* secondary_window) {
    Shutdown();

    VideoCore::VulkanHooks* hooks = VideoCore::GetVulkanHooks();
    VideoCore::XrScreenSink* sink = hooks ? VideoCore::GetXrScreenSink() : nullptr;
    if (!window && !sink) { // the session itself may start later
        LOG_ERROR(Render_Vulkan, "alt_xr: no XR and no window");
        return false;
    }

    try {
        impl->sink = sink;
        impl->window = window;
        impl->secondary_window = secondary_window;
        DummyWindow dummy_window;
        impl->instance = std::make_unique<Instance>(
            window ? *window : dummy_window, Settings::values.physical_device.GetValue());
        if (sink && !UsesXrDevice(*impl->instance)) {
            LOG_ERROR(Render_Vulkan, "alt_xr: adopted Vulkan device is not the XR device");
            if (!window) {
                Shutdown();
                return false;
            }
        }
        impl->scheduler = std::make_unique<Scheduler>(*impl->instance);
        // The scheduler calls both on every submission, and an empty std::function throws. The 3DS
        // path sets them in its rasterizer and pipeline cache, which are not made here.
        impl->scheduler->RegisterOnSubmit([] {});
        impl->scheduler->RegisterOnDispatch([i = impl.get()] {
            if (i->update_queue) {
                i->update_queue->Flush();
            }
        });
        impl->queue_reg = std::make_unique<XrQueueRegistration>(*impl->instance, *impl->scheduler);
        if (sink) {
            sink->SetTopStereo(false);
        }

        impl->vertex_buffer = std::make_unique<StreamBuffer>(
            *impl->instance, *impl->scheduler, vk::BufferUsageFlagBits::eVertexBuffer,
            kVertexBufferSize, BufferType::Stream);
        impl->upload_buffer = std::make_unique<StreamBuffer>(
            *impl->instance, *impl->scheduler, vk::BufferUsageFlagBits::eTransferSrc,
            kUploadBufferSize, BufferType::Upload);
        impl->update_queue = std::make_unique<DescriptorUpdateQueue>(*impl->instance);
        impl->present_heap = std::make_unique<DescriptorHeap>(
            *impl->instance, impl->scheduler->GetMasterSemaphore(), kPresentBindings, 32);

        const vk::Device device = impl->instance->GetDevice();
        impl->present_vertex_shader =
            Compile(HostShaders::VULKAN_PRESENT_VERT, vk::ShaderStageFlagBits::eVertex, device);

        const vk::PushConstantRange push_range = {
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .offset = 0,
            .size = sizeof(AltPresentUniformData),
        };
        const vk::DescriptorSetLayout set_layout = impl->present_heap->Layout();
        impl->present_pipeline_layout = device.createPipelineLayoutUnique({
            .setLayoutCount = 1,
            .pSetLayouts = &set_layout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_range,
        });

        for (std::size_t i = 0; i < impl->samplers.size(); ++i) {
            const bool linear = i == 0;
            const vk::Filter filter = linear ? vk::Filter::eLinear : vk::Filter::eNearest;
            const vk::SamplerMipmapMode mipmap =
                linear ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest;
            impl->samplers[i] = device.createSampler({
                .magFilter = filter,
                .minFilter = filter,
                .mipmapMode = mipmap,
                .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                .anisotropyEnable = VK_FALSE,
                .maxAnisotropy = 1.0f,
                .compareEnable = false,
                .compareOp = vk::CompareOp::eAlways,
                .minLod = 0.0f,
                .maxLod = 0.0f,
                .borderColor = vk::BorderColor::eIntOpaqueBlack,
                .unnormalizedCoordinates = false,
            });
        }

        const u32 black_pixel = 0xFF000000u;
        const AltScreenImage dummy_img{&black_pixel, 1, 1, 1, false};
        if (!impl->UploadImage(impl->dummy_tex, dummy_img)) {
            Shutdown();
            return false;
        }
        impl->scheduler->Finish();
        // Present makes the XrPresentTarget: the session may not run yet (a game started with the
        // app), and its swapchains come and go.
        if (window) {
            impl->window_target = std::make_unique<AltWindowTarget>(
                *window, *impl->instance, *impl->scheduler, impl->present_vertex_shader,
                *impl->present_pipeline_layout);
        }
        impl->ready = true;
        LOG_INFO(Render_Vulkan, "alt_xr: presenter initialized ({})",
                 impl->XrShowsScreens() ? "VR" : "window");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(Render_Vulkan, "alt_xr: initialization failed: {}", e.what());
        Shutdown();
        return false;
    }
}

void AltXrRenderer::Shutdown() {
    if (impl) {
        impl->Shutdown();
    }
}

bool AltXrRenderer::NeedsPresent() const {
    if (!impl || !impl->ready) {
        return false;
    }
    const bool vr = impl->XrShowsScreens();
    return vr != impl->presented_vr || (!vr && impl->window_target && impl->window_target->Resized());
}

bool AltXrRenderer::Present(const AltScreenImage& primary, const AltScreenImage& secondary,
                            const AltScreenImage& overlay, bool skippable) {
    if (!impl || !impl->ready || !primary.HasPicture()) {
        return false;
    }
    // A copy of the core's picture goes to the GPU before the core's next work there does.
    impl->copy_tick = 0;
    SCOPE_EXIT({
        if (impl->copy_tick != 0 && impl->scheduler) {
            if (impl->copy_tick == impl->scheduler->CurrentTick()) {
                impl->scheduler->Flush();
            }
            impl->scheduler->WaitWorker();
        }
    });
    impl->EnsureUploads({&primary, &secondary, &overlay});
    impl->presented_vr = impl->XrShowsScreens();
    if (!impl->presented_vr) {
        if (!impl->window_target) {
            return false;
        }
        if (skippable &&
            std::chrono::steady_clock::now() - impl->window_presented < kSkippableInterval) {
            return true;
        }
        if (!impl->TakeImage(impl->primary_tex, primary) ||
            (secondary.HasPicture() && !impl->TakeImage(impl->secondary_tex, secondary))) {
            return false;
        }
        impl->PresentToWindow(impl->UploadOverlay(overlay));
        return true;
    }
    VideoCore::XrScreenSink* const sink = impl->sink;
    if (impl->xr_target && impl->xr_target_generation != sink->SwapchainGeneration()) {
        impl->DropXrTarget();
    }
    if (!impl->xr_target) {
        sink->SetTopStereo(false);
        impl->xr_target_generation = sink->SwapchainGeneration();
        impl->xr_target = std::make_unique<XrPresentTarget>(
            *impl->instance, *impl->scheduler, *sink, impl->present_vertex_shader,
            *impl->present_pipeline_layout);
    }

    const bool has_secondary = secondary.HasPicture();
    if (has_secondary) {
        impl->secondary_extent = TargetExtent(secondary);
    } // else hidden (the GBA map in a menu): its size stays
    const vk::Extent2D top_size = TargetExtent(primary);
    const vk::Extent2D bottom_size = impl->secondary_extent;
    const std::array<vk::Extent2D, XrPresentTarget::kScreens> sizes = {
        top_size,
        vk::Extent2D{kDummySwapchainExtent, kDummySwapchainExtent},
        bottom_size,
    };
    const bool uneven =
        !AltTargetEven(primary.width, primary.height, top_size.width, top_size.height);
    if (!impl->xr_target->Prepare(sizes)) {
        return false;
    }

    if (!impl->TakeImage(impl->primary_tex, primary)) {
        return false;
    }
    if (has_secondary && !impl->TakeImage(impl->secondary_tex, secondary)) {
        return false;
    }

    const bool has_overlay = impl->UploadOverlay(overlay);
    const vk::Pipeline plain = impl->xr_target->Pipeline();
    const vk::Pipeline top = has_overlay ? impl->OverlayPipeline() : plain;
    const vk::DescriptorSet present_set = impl->CommitScreens(
        has_secondary ? impl->secondary_tex.view : vk::ImageView{}, has_overlay, uneven);

    Common::XrPacer::Get().game_swaps.fetch_add(1, std::memory_order_relaxed);

    std::array<XrPresentTarget::Target, XrPresentTarget::kScreens> targets{};
    if (!impl->xr_target->BeginFrame(targets, VideoCore::XrContentPose{})) {
        impl->scheduler->Flush();
        return false;
    }

    impl->DrawQuad(targets[static_cast<std::size_t>(VideoCore::XrScreen::TopLeft)], present_set,
                   top, 0, primary.width, primary.height, 0);
    impl->ClearTarget(targets[static_cast<std::size_t>(VideoCore::XrScreen::TopRight)]);
    if (has_secondary) {
        const float dim = sink->ScreenDim(VideoCore::XrScreen::Bottom);
        const float alpha = sink->ScreenAlpha(VideoCore::XrScreen::Bottom);
        const int layer_flags =
            (dim < 1.0f ? std::clamp(static_cast<int>(dim * 1000.0f + 0.5f), 1, 999) : 0) +
            1000 * (alpha < 1.0f ? std::clamp(static_cast<int>(alpha * 1000.0f + 0.5f), 1, 999) : 0);
        impl->DrawQuad(targets[static_cast<std::size_t>(VideoCore::XrScreen::Bottom)], present_set,
                       plain, 2, secondary.width, secondary.height, layer_flags);
    } else {
        impl->ClearTarget(targets[static_cast<std::size_t>(VideoCore::XrScreen::Bottom)]);
    }

    const u64 tick = impl->scheduler->CurrentTick();
    impl->scheduler->Flush();
    impl->xr_target->EndFrame(tick);
    impl->KeepWindowAlive();
    return true;
}

bool AltXrRenderer::IsReady() const {
    return impl && impl->ready;
}

AltGraphics AltXrRenderer::Graphics() const {
    const Instance& instance = *impl->instance;
    return {instance.GetPhysicalDevice(), instance.GetDevice(), instance.GetGraphicsQueue(),
            instance.GetGraphicsQueueFamilyIndex(), &impl->scheduler->submit_mutex};
}

} // namespace Vulkan
