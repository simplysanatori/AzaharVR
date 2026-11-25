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
#include "video_core/renderer_vulkan/alt_picture_filter.h"
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
#include "video_core/xr_screen_cover.h"

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

struct Image2D {
    vk::Image image{};
    vk::ImageView view{};
    VmaAllocation allocation{};
    vk::Framebuffer framebuffer{}; ///< made when a texture filter first draws into it
};

struct ScreenTexture {
    u32 width = 0; ///< the picture's own size
    u32 height = 0;
    Image2D picture{};
    Image2D scaled{}; ///< the picture `scale` times as big, if that is drawn
    u32 scale = 1;
    vk::ImageView view{}; ///< what is shown: either's
};

/// A picture's scale, for one screen.
struct PictureScale {
    u32 scale = 0;        ///< of what the headset's image or the window's texture is (0: none yet)
    bool texture = false; ///< the picture's texture is scaled (else the headset's image alone)
    bool filtered = false; ///< by Texture Filter
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
    std::unique_ptr<AltPictureFilter> picture_filter; ///< made with the first filtered picture
    vk::UniquePipelineLayout present_pipeline_layout;
    vk::ShaderModule present_vertex_shader{};
    std::array<vk::Sampler, 2> samplers{};
    u64 upload_size = kUploadBufferSize;
    ScreenTexture primary_tex{};
    ScreenTexture right_tex{}; ///< the Primary Screen for the right eye
    ScreenTexture secondary_tex{};
    ScreenTexture secondary_right_tex{}; ///< the Secondary Screen for the right eye
    bool secondary_stereo_failed = false;
    ScreenTexture overlay_tex{};
    ScreenTexture dummy_tex{};
    AltScreenImage secondary_shape{}; ///< the last secondary picture's size and scale alone
    std::unique_ptr<XrPresentTarget> xr_target;
    u64 xr_target_generation = 0;
    bool xr_target_stereo = false; ///< xr_target was made for a right eye's picture
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
    bool sharp_pixels = false;
    u32 presented_setting = 0; ///< the last Present's Internal Resolution
    Settings::TextureFilter presented_filter{}; ///< and Texture Filter
    PictureScale primary_scale{};
    PictureScale secondary_scale{};

    bool XrShowsScreens() const {
        return sink && sink->IsActive() && UsesXrDevice(*instance);
    }

    /// Both screens' newest pictures, for the present shader. A screen that has none is black; one
    /// that stops being sent keeps its last. `overlay`: the overlay is at kAltOverlaySlot, always
    /// smooth (its pixels are no multiple of the screen's). `right`: for the right eye, its picture
    /// in the Primary Screen's place. `uneven`: the screens' pixels are no multiple of the
    /// headset's images' either.
    vk::DescriptorSet CommitScreens(vk::ImageView secondary_view, bool overlay, bool right = false,
                                    bool uneven = false) {
        // Sharp pixels are drawn whole times in the headset, where no filter applies.
        const vk::Sampler sampler =
            samplers[(sharp_pixels && presented_vr) ||
                     (!uneven && !Settings::values.filter_mode.GetValue())];
        const vk::DescriptorSet set = present_heap->Commit();
        const vk::ImageView primary_view = right ? right_tex.view : primary_tex.view;
        const std::array views = {primary_view, primary_view, secondary_view};
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
        DestroyImage(tex.picture);
        DestroyImage(tex.scaled);
        tex = {};
    }

    void DestroyImage(Image2D& img) {
        if (img.framebuffer) {
            instance->GetDevice().destroyFramebuffer(img.framebuffer);
        }
        if (img.view) {
            instance->GetDevice().destroyImageView(img.view);
        }
        if (img.image) {
            vmaDestroyImage(instance->GetAllocator(), static_cast<VkImage>(img.image),
                            img.allocation);
        }
        img = {};
    }

    /// The picture's texture, and one `scale` times as big if that is more than once.
    bool EnsureTexture(ScreenTexture& tex, u32 width, u32 height, u32 scale) {
        const bool same = tex.picture.image && tex.width == width && tex.height == height;
        if (same && tex.scale == scale) {
            return true;
        }
        if (tex.picture.image && scheduler) {
            scheduler->Finish();
        }
        if (!same) {
            DestroyTexture(tex);
            if (!CreateImage(tex.picture, width, height)) {
                return false;
            }
            tex.width = width;
            tex.height = height;
        }
        DestroyImage(tex.scaled);
        tex.scale = scale;
        tex.view = tex.picture.view;
        if (scale > 1 && CreateImage(tex.scaled, width * scale, height * scale, true)) {
            tex.view = tex.scaled.view;
        }
        return true;
    }

    /// `target`: a texture filter may draw into it.
    bool CreateImage(Image2D& img, u32 width, u32 height, bool target = false) {
        const vk::ImageCreateInfo image_info = {
            .imageType = vk::ImageType::e2D,
            .format = vk::Format::eR8G8B8A8Unorm,
            .extent = {width, height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc |
                     vk::ImageUsageFlagBits::eSampled |
                     (target ? vk::ImageUsageFlagBits::eColorAttachment
                             : vk::ImageUsageFlags{}),
            .initialLayout = vk::ImageLayout::eUndefined,
        };
        const VmaAllocationCreateInfo alloc_info = {
            .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        };
        VkImage raw_image{};
        const VkImageCreateInfo raw_info = static_cast<VkImageCreateInfo>(image_info);
        const VkResult res = vmaCreateImage(instance->GetAllocator(), &raw_info, &alloc_info,
                                            &raw_image, &img.allocation, nullptr);
        if (res != VK_SUCCESS) {
            LOG_ERROR(Render_Vulkan, "alt_xr: failed to allocate {}x{} screen texture ({})", width,
                      height, static_cast<int>(res));
            img.allocation = {};
            return false;
        }
        img.image = vk::Image{raw_image};
        img.view = instance->GetDevice().createImageView({
            .image = img.image,
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
        return true;
    }

    /// The whole picture into its bigger image through Texture Filter. False: not drawn.
    bool DrawFiltered(ScreenTexture& tex) {
        const PictureScale& pick = ScaleOf(tex);
        if (!pick.filtered) {
            return false;
        }
        try {
            if (!picture_filter) {
                picture_filter =
                    std::make_unique<AltPictureFilter>(*instance, *scheduler, *update_queue);
            }
            const u32 width = tex.width * tex.scale;
            const u32 height = tex.height * tex.scale;
            if (!tex.scaled.framebuffer) {
                tex.scaled.framebuffer =
                    picture_filter->MakeFramebuffer(tex.scaled.view, width, height);
            }
            return picture_filter->Draw(presented_filter, tex.picture.view,
                                        tex.scaled.framebuffer, width, height, tex.scale);
        } catch (const std::exception& e) {
            LOG_ERROR(Render_Vulkan, "alt_xr: texture filter not drawn: {}", e.what());
            return false;
        }
    }

    /// The whole picture into its bigger image, each pixel a block.
    void DrawScaled(const ScreenTexture& tex) {
        scheduler->Record([src = tex.picture.image, dst = tex.scaled.image, w = tex.width,
                           h = tex.height, scale = tex.scale](vk::CommandBuffer cmdbuf) {
            const auto barrier = [](vk::Image image, vk::AccessFlags from, vk::AccessFlags to,
                                    vk::ImageLayout before, vk::ImageLayout after) {
                return vk::ImageMemoryBarrier{
                    .srcAccessMask = from,
                    .dstAccessMask = to,
                    .oldLayout = before,
                    .newLayout = after,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = image,
                    .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
                };
            };
            using Access = vk::AccessFlagBits;
            using Layout = vk::ImageLayout;
            using Stage = vk::PipelineStageFlagBits;
            const std::array before = {
                barrier(src, {}, Access::eTransferRead, Layout::eShaderReadOnlyOptimal,
                        Layout::eTransferSrcOptimal),
                barrier(dst, {}, Access::eTransferWrite, Layout::eUndefined,
                        Layout::eTransferDstOptimal),
            };
            cmdbuf.pipelineBarrier(Stage::eFragmentShader, Stage::eTransfer, {}, {}, {}, before);
            const vk::ImageSubresourceLayers layers{vk::ImageAspectFlagBits::eColor, 0, 0, 1};
            const auto end = [w, h](u32 times) {
                return vk::Offset3D{static_cast<s32>(w * times), static_cast<s32>(h * times), 1};
            };
            const vk::ImageBlit region = {
                .srcSubresource = layers,
                .srcOffsets = std::array{vk::Offset3D{}, end(1)},
                .dstSubresource = layers,
                .dstOffsets = std::array{vk::Offset3D{}, end(scale)},
            };
            cmdbuf.blitImage(src, Layout::eTransferSrcOptimal, dst, Layout::eTransferDstOptimal,
                             region, vk::Filter::eNearest);
            const std::array after = {
                barrier(src, {}, Access::eShaderRead, Layout::eTransferSrcOptimal,
                        Layout::eShaderReadOnlyOptimal),
                barrier(dst, Access::eTransferWrite, Access::eShaderRead,
                        Layout::eTransferDstOptimal, Layout::eShaderReadOnlyOptimal),
            };
            cmdbuf.pipelineBarrier(Stage::eTransfer, Stage::eFragmentShader, {}, {}, {}, after);
        });
    }

    const PictureScale& ScaleOf(const ScreenTexture& tex) const {
        return &tex == &secondary_tex || &tex == &secondary_right_tex ? secondary_scale
                                                                      : primary_scale;
    }

    /// The scales of both screens for the output `presented_vr` says.
    void PickScales(const AltScreenImage& primary, const AltScreenImage& secondary,
                    bool has_overlay) {
        presented_setting = Settings::RunningGraphics().resolution_factor.GetValue();
        presented_filter = Settings::RunningGraphics().texture_filter.GetValue();
        if (!sharp_pixels) {
            return;
        }
        u32 setting = presented_setting;
        if (has_overlay && setting > 0) {
            setting = std::max(setting, kAltOverlayScale);
        }
        PickScale(primary_scale, setting, primary.width, primary.height, false);
        if (secondary.HasPicture()) {
            PickScale(secondary_scale, presented_setting, secondary.width, secondary.height, true);
        }
    }

    void PickScale(PictureScale& pick, u32 setting, u32 width, u32 height, bool bottom) {
        const float shown =
            presented_vr ? kAltXrOversample *
                               VideoCore::XrScreenCover::Get(bottom
                                                                 ? VideoCore::XrScreen::Bottom
                                                                 : VideoCore::XrScreen::TopLeft) /
                               static_cast<float>(width)
                         : ShownInWindows(width, height, bottom);
        const u32 scale = AltPictureScale(setting, width, height, shown, pick.scale);
        if (scale != pick.scale) {
            LOG_INFO(Render_Vulkan,
                     "alt_xr: the {} Screen's {}x{} picture at {}x (Internal Resolution {}, a pixel "
                     "shown {:.2f} wide)",
                     bottom ? "Secondary" : "Primary", width, height, scale, setting, shown);
            pick.scale = scale;
        }
        // The window's scale is no whole number: it samples the scaled texture smoothly. A filter
        // draws the scaled texture in the headset too.
        pick.filtered = AltPictureFiltered(presented_filter, scale);
        pick.texture = scale > 1 && (!presented_vr || pick.filtered);
    }

    /// Window pixels one of the picture's covers, the most of the places it is shown at.
    float ShownInWindows(u32 width, u32 height, bool bottom) const {
        float most = 0.0f;
        const auto add = [&](const Common::Rectangle<u32>& rect) {
            const u32 length = std::max(rect.GetWidth(), rect.GetHeight()); // either way up
            most = std::max(most, static_cast<float>(length) /
                                      static_cast<float>(std::max(width, height)));
        };
        for (const Frontend::EmuWindow* const w :
             {window, secondary_target ? secondary_window : nullptr}) {
            if (!w) {
                continue;
            }
            const Layout::FramebufferLayout& layout = w->GetFramebufferLayout();
            if (bottom ? layout.bottom_screen_enabled : layout.top_screen_enabled) {
                add(bottom ? layout.bottom_screen : layout.top_screen);
            }
            if (layout.additional_screen_enabled && layout.additional_screen_is_bottom == bottom) {
                add(layout.additional_screen);
            }
        }
        return most;
    }

    /// Of a screen's (not the overlay's) texture.
    u32 TextureScale(const ScreenTexture& tex) const {
        const PictureScale& pick = ScaleOf(tex);
        return sharp_pixels && pick.texture ? std::max(pick.scale, 1u) : 1;
    }

    /// The headset's image for a picture.
    vk::Extent2D XrExtent(const AltScreenImage& image, const PictureScale& pick) const {
        if (!sharp_pixels) {
            return TargetExtent(image);
        }
        const u32 scale = std::max(pick.scale, 1u);
        return vk::Extent2D{image.width * scale, image.height * scale};
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
        if (!EnsureTexture(tex, img.width, img.height, see_through ? 1 : TextureScale(tex))) {
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
        const vk::Image dst_image = tex.picture.image;
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
        if (tex.scaled.image && !DrawFiltered(tex)) {
            DrawScaled(tex);
        }
        return true;
    }

    /// A screen's newest picture, from the GPU or the CPU.
    bool TakeImage(ScreenTexture& tex, const AltScreenImage& img) {
        if (&tex == &secondary_tex) {
            secondary_shape = {.width = img.width,
                               .height = img.height,
                               .scale = img.scale,
                               .max_scale = img.max_scale};
        }
        if (img.image == 0) {
            return UploadImage(tex, img);
        }
        if (img.alive.expired()) { // what drew it is gone (a paused game): its last stays
            return static_cast<bool>(tex.picture.image);
        }
        if (!EnsureTexture(tex, img.width, img.height, TextureScale(tex))) {
            return false;
        }
        copy_tick = scheduler->CurrentTick();
        scheduler->Record([src = vk::Image{reinterpret_cast<VkImage>(img.image)},
                           dst = tex.picture.image, layer = img.layer, x = img.x, w = img.width,
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
                .srcOffset = {static_cast<s32>(x), 0, 0},
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

    /// The sink was told otherwise behind xr_target's back (the core's new screen sizes say mono).
    bool StereoTurnedOff() const {
        return xr_target && xr_target_stereo && !sink->TopStereo();
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

    /// The packed right eye's follows the left one's in the same render pass.
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
            if (target.begin_pass) {
                cmdbuf.beginRenderPass(begin_info, vk::SubpassContents::eInline);
            }
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
            if (target.end_pass) {
                cmdbuf.endRenderPass();
            }
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
        DestroyTexture(right_tex);
        DestroyTexture(secondary_tex);
        DestroyTexture(secondary_right_tex);
        DestroyTexture(overlay_tex);
        DestroyTexture(dummy_tex);
        picture_filter.reset();
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
        secondary_shape = {};
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
        sharp_pixels = false;
        secondary_stereo_failed = false;
        primary_scale = {};
        secondary_scale = {};
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
    if (impl->sharp_pixels &&
        (impl->presented_setting != Settings::RunningGraphics().resolution_factor.GetValue() ||
         impl->presented_filter != Settings::RunningGraphics().texture_filter.GetValue())) {
        return true;
    }
    return vr != impl->presented_vr ||
           (!vr && impl->window_target && impl->window_target->Resized()) ||
           (vr && impl->StereoTurnedOff());
}

bool AltXrRenderer::Present(const AltScreenImage& whole, const AltScreenImage& whole_secondary,
                            const AltScreenImage& overlay, const AltEyes& primary_eyes,
                            const AltEyes& secondary_eyes, bool skippable) {
    if (!impl || !impl->ready || !whole.HasPicture()) {
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
    const AltScreenImage& whole_right = primary_eyes.right;
    const AltScreenImage& whole_secondary_right = secondary_eyes.right;
    impl->EnsureUploads({&whole, &whole_secondary, &overlay, &whole_right, &whole_secondary_right});
    impl->presented_vr = impl->XrShowsScreens();
    if (!impl->presented_vr) {
        if (!impl->window_target) {
            return false;
        }
        if (skippable &&
            std::chrono::steady_clock::now() - impl->window_presented < kSkippableInterval) {
            return true;
        }
        impl->PickScales(whole, whole_secondary, overlay.HasPicture() || impl->overlay_tex.view);
        if (!impl->TakeImage(impl->primary_tex, whole) ||
            (whole_secondary.HasPicture() &&
             !impl->TakeImage(impl->secondary_tex, whole_secondary))) {
            return false;
        }
        impl->PresentToWindow(impl->UploadOverlay(overlay));
        return true;
    }
    VideoCore::XrScreenSink* const sink = impl->sink;
    const bool stereo = AltShowsStereo(whole, whole_right);
    const float layer_shift = stereo ? primary_eyes.shift : 0.0f;
    const auto [primary, primary_right] =
        AltCutEyes(AltLeft(whole, primary_eyes), whole_right, layer_shift);
    // The Secondary Screen's eyes are both in its one swapchain (kXrPackedGap). What is in it goes
    // with the picture (XrContentPose), so it is only as big again, with nothing else to change.
    bool secondary_stereo = !impl->secondary_stereo_failed && whole_secondary.HasPicture() &&
                            AltShowsStereo(whole_secondary, whole_secondary_right);
    const float secondary_shift = secondary_stereo ? secondary_eyes.shift : 0.0f;
    const auto [secondary, secondary_right] =
        AltCutEyes(secondary_stereo ? AltLeft(whole_secondary, secondary_eyes) : whole_secondary,
                   whole_secondary_right, secondary_shift);
    if (impl->StereoTurnedOff() && stereo) {
        sink->SetTopStereo(true);
    }
    // The layout of the eyes is the target's for its life: made again when it changes, with the
    // screens off in between, so no refresh shows one layout as the other.
    if (impl->xr_target &&
        (impl->xr_target_generation != sink->SwapchainGeneration() ||
         impl->xr_target_stereo != stereo ||
         (stereo && impl->xr_target->Packed() != sink->PackedTop()))) {
        impl->DropXrTarget();
    }
    if (!impl->xr_target) {
        sink->SetTopStereo(stereo);
        impl->xr_target_stereo = stereo;
        impl->xr_target_generation = sink->SwapchainGeneration();
        impl->xr_target = std::make_unique<XrPresentTarget>(
            *impl->instance, *impl->scheduler, *sink, impl->present_vertex_shader,
            *impl->present_pipeline_layout);
    }

    impl->PickScales(primary, secondary, overlay.HasPicture() || impl->overlay_tex.view);
    const bool has_secondary = secondary.HasPicture();
    vk::Extent2D bottom_size{kDummySwapchainExtent, kDummySwapchainExtent};
    if (has_secondary) {
        bottom_size = impl->XrExtent(secondary, impl->secondary_scale);
    } else if (impl->secondary_tex.view) { // hidden (the GBA map in a menu): its size stays
        bottom_size = impl->XrExtent(impl->secondary_shape, impl->secondary_scale);
    }
    const vk::Extent2D eye = impl->XrExtent(primary, impl->primary_scale);
    const auto eyes = AltEyeSwapchains({eye.width, eye.height},
                                       {kDummySwapchainExtent, kDummySwapchainExtent}, stereo,
                                       impl->xr_target->Packed());
    const auto prepare = [&](bool both_bottom) {
        const AltEyeSize bottom =
            AltEyeSwapchains({bottom_size.width, bottom_size.height}, {}, both_bottom, true)[0];
        return impl->xr_target->Prepare({
            vk::Extent2D{eyes[0].width, eyes[0].height},
            vk::Extent2D{eyes[1].width, eyes[1].height},
            vk::Extent2D{bottom.width, bottom.height},
        });
    };
    const bool uneven =
        !AltTargetEven(primary.width, primary.height, eye.width, eye.height);
    if (!prepare(secondary_stereo)) {
        // Only the taller images fail, not the session: the game goes on without them
        if (!secondary_stereo || !prepare(false)) {
            return false;
        }
        LOG_ERROR(Render_Vulkan, "alt_xr: no images for both eyes of the Secondary Screen: both "
                                 "see one picture until the game starts again");
        impl->secondary_stereo_failed = true;
        secondary_stereo = false;
    }

    if (!impl->TakeImage(impl->primary_tex, primary)) {
        return false;
    }
    if (has_secondary && !impl->TakeImage(impl->secondary_tex, secondary)) {
        return false;
    }

    // Failed: the right eye gets the left one's picture. All uploads before the sets: a texture made
    // again submits.
    const bool has_right = stereo && impl->TakeImage(impl->right_tex, primary_right);
    const bool has_secondary_right =
        secondary_stereo && impl->TakeImage(impl->secondary_right_tex, secondary_right);
    const bool has_overlay = impl->UploadOverlay(overlay);
    const vk::Pipeline plain = impl->xr_target->Pipeline();
    const vk::Pipeline top = has_overlay ? impl->OverlayPipeline() : plain;
    const vk::ImageView secondary_view =
        has_secondary ? impl->secondary_tex.view : vk::ImageView{};
    const vk::DescriptorSet present_set =
        impl->CommitScreens(secondary_view, has_overlay, false, uneven);
    const vk::DescriptorSet right_set =
        has_right ? impl->CommitScreens(secondary_view, has_overlay, true, uneven) : present_set;
    const vk::DescriptorSet secondary_right_set =
        has_secondary_right
            ? impl->CommitScreens(impl->secondary_right_tex.view, has_overlay, false, uneven)
            : present_set;

    Common::XrPacer::Get().game_swaps.fetch_add(1, std::memory_order_relaxed);

    std::array<XrPresentTarget::Target, XrPresentTarget::kScreens> targets{};
    VideoCore::XrContentPose content{};
    content.layer_shift = layer_shift;
    content.bottom_stereo = secondary_stereo;
    content.bottom_shift = secondary_stereo ? secondary_shift : 0.0f;
    if (!impl->xr_target->BeginFrame(targets, content)) {
        impl->scheduler->Flush();
        return false;
    }

    impl->DrawQuad(targets[static_cast<std::size_t>(VideoCore::XrScreen::TopLeft)], present_set,
                   top, 0, primary.width, primary.height, 0);
    const auto& right_target = targets[static_cast<std::size_t>(VideoCore::XrScreen::TopRight)];
    if (stereo) {
        impl->DrawQuad(right_target, right_set, top, 0, primary.width, primary.height, 0);
    } else {
        impl->ClearTarget(right_target);
    }
    if (has_secondary) {
        const float dim = sink->ScreenDim(VideoCore::XrScreen::Bottom);
        const float alpha = sink->ScreenAlpha(VideoCore::XrScreen::Bottom);
        const int layer_flags =
            (dim < 1.0f ? std::clamp(static_cast<int>(dim * 1000.0f + 0.5f), 1, 999) : 0) +
            1000 * (alpha < 1.0f ? std::clamp(static_cast<int>(alpha * 1000.0f + 0.5f), 1, 999) : 0);
        const auto& bottom = targets[static_cast<std::size_t>(VideoCore::XrScreen::Bottom)];
        if (secondary_stereo) {
            auto [left, right] = AltEyeHalves(bottom);
            impl->DrawQuad(left, present_set, plain, 2, secondary.width, secondary.height,
                           layer_flags);
            impl->DrawQuad(right, secondary_right_set, plain, 2, secondary.width, secondary.height,
                           layer_flags);
        } else {
            impl->DrawQuad(bottom, present_set, plain, 2, secondary.width, secondary.height,
                           layer_flags);
        }
    } else {
        impl->ClearTarget(targets[static_cast<std::size_t>(VideoCore::XrScreen::Bottom)]);
    }

    const u64 tick = impl->scheduler->CurrentTick();
    impl->scheduler->Flush();
    impl->xr_target->EndFrame(tick);
    impl->KeepWindowAlive();
    return true;
}

void AltXrRenderer::SetSharpPixels(bool on) {
    if (impl) {
        impl->sharp_pixels = on;
    }
}

bool AltXrRenderer::ShowsSecondaryStereo() const {
    return ShowsInVr() && !impl->secondary_stereo_failed;
}

bool AltXrRenderer::IsReady() const {
    return impl && impl->ready;
}

bool AltXrRenderer::ShowsInVr() const {
    return IsReady() && impl->XrShowsScreens();
}

AltGraphics AltXrRenderer::Graphics() const {
    const Instance& instance = *impl->instance;
    return {instance.GetPhysicalDevice(), instance.GetDevice(), instance.GetGraphicsQueue(),
            instance.GetGraphicsQueueFamilyIndex(), &impl->scheduler->submit_mutex};
}

} // namespace Vulkan
