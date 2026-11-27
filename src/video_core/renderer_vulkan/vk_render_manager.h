// Copyright 2024-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <mutex>
#include <vector>

#include "common/math_util.h"
#include "video_core/gpu_pass_stats.h" // AzaharFrame gpupasses
#include "video_core/renderer_vulkan/vk_common.h"

VK_DEFINE_HANDLE(VmaAllocation) // AzaharVR foveation

namespace VideoCore {
enum class PixelFormat : u32;
}

namespace Vulkan {

class Instance;
class Scheduler;
class Framebuffer;

struct RenderPass {
    vk::Framebuffer framebuffer;
    vk::RenderPass render_pass;
    vk::Rect2D render_area;
    vk::ClearValue clear;
    u32 do_clear;

    bool operator==(const RenderPass& other) const noexcept {
        return std::tie(framebuffer, render_pass, render_area, do_clear) ==
                   std::tie(other.framebuffer, other.render_pass, other.render_area,
                            other.do_clear) &&
               std::memcmp(&clear, &other.clear, sizeof(vk::ClearValue)) == 0;
    }
};

class RenderManager {
    static constexpr u32 NumColorFormats = static_cast<u32>(VideoCore::PixelFormat::NumColorFormat);
    static constexpr u32 NumDepthFormats = static_cast<u32>(VideoCore::PixelFormat::NumDepthFormat);

public:
    explicit RenderManager(const Instance& instance, Scheduler& scheduler);
    ~RenderManager();

    /// Begins a new renderpass with the provided framebuffer as render target.
    void BeginRendering(const Framebuffer* framebuffer, Common::Rectangle<u32> draw_rect);

    /// Begins a new renderpass with the provided render state.
    void BeginRendering(const RenderPass& new_pass);

    /// Exits from any currently active renderpass instance. AzaharFrame gpupasses: `why` is
    /// what comes next (the reason the pass ends, and the kind of the work outside passes after
    /// it); it is only bookkeeping for AZAHARFRAME_GPU_PASSES=1 and changes nothing else.
    void EndRendering(VideoCore::GpuPasses::Why why = VideoCore::GpuPasses::Why::None);

    /// True when a non-shadow render pass with a depth attachment is currently open.
    [[nodiscard]] bool HasActiveDepth() const noexcept {
        return pass.render_pass && images[1] && !shadow_rendering;
    }

    /// AzaharFrame gpupasses: whether the passes are timed (AZAHARFRAME_GPU_PASSES=1).
    [[nodiscard]] bool Timing() const noexcept {
        return timing;
    }

    /// AzaharFrame gpupasses: work of kind `why` comes next, maybe through a helper that ends the
    /// pass itself (a conversion by shader). Bookkeeping only.
    void Expect(VideoCore::GpuPasses::Why why) {
        if (timing) {
            ExpectWork(why);
        }
    }

    /// AzaharFrame gpupasses: one of the game's draws in the pass begun last. `world`: through
    /// the world camera or depth tested (else 2D); the viewport in the 3DS's pixels. Call only
    /// when Timing().
    void NoteDraw(bool world, u32 viewport_width, u32 viewport_height);

    /// AzaharFrame gpupasses: a game frame ends (the renderer's swap).
    void NoteFrame() {
        if (timing) {
            ++timed.frame;
            timed.targets.clear();
        }
    }

    /// Returns the renderpass associated with the color-depth format pair
    vk::RenderPass GetRenderpass(VideoCore::PixelFormat color, VideoCore::PixelFormat depth,
                                 bool is_clear);

    /// AzaharVR foveation: the same render pass with a shading rate attachment as its last
    /// attachment (the one view of GetShadingRateView).
    vk::RenderPass GetRenderpassFsr(VideoCore::PixelFormat color, VideoCore::PixelFormat depth);

    /// AzaharVR foveation: the render pass the game's framebuffers and pipelines use: with the
    /// switch on, GetRenderpassFsr (a pass with a shading rate attachment is only compatible
    /// with pipelines made for one), else GetRenderpass(color, depth, false). Shadow passes (no
    /// attachment) keep the plain pass.
    vk::RenderPass GetDrawRenderpass(VideoCore::PixelFormat color, VideoCore::PixelFormat depth);

    /// AzaharVR foveation: the rate image for a width x height picture (made once, kept).
    vk::ImageView GetShadingRateView(u32 width, u32 height);

    /// AzaharVR foveation: a rate image of 1x1 everywhere, as large as the largest framebuffer:
    /// for the passes of the switch's render pass that are not the eye picture.
    vk::ImageView GetFullRateView();

    /// AzaharVR foveation: moves the full rate area to the gaze (or straight ahead) when it moved
    /// by a texel or more, or when the active picture sub-rectangle within a padded surface
    /// changes. Ends the pass in progress when it does; call before beginning one.
    void RefreshShadingRate(u32 width, u32 height, Common::Rectangle<u32> draw_rect);

private:
    // AzaharFrame gpupasses
    void StartPass(const RenderPass& new_pass, const Framebuffer* framebuffer);
    VideoCore::GpuPasses::Why EndPass(VideoCore::GpuPasses::Why why);
    void ExpectWork(VideoCore::GpuPasses::Why why);
    void SwitchWork(VideoCore::GpuPasses::Bucket bucket);
    void CloseWork();
    void Mark(const VideoCore::GpuPasses::Segment& segment);

    /// Creates a renderpass configured appropriately and stores it in cached_renderpasses
    vk::UniqueRenderPass CreateRenderPass(vk::Format color, vk::Format depth,
                                          vk::AttachmentLoadOp load_op) const;

private:
    const Instance& instance;
    Scheduler& scheduler;
    vk::UniqueRenderPass cached_renderpasses[NumColorFormats + 1][NumDepthFormats + 1][2];
    // AzaharVR foveation
    vk::UniqueRenderPass cached_fsr_renderpasses[NumColorFormats + 1][NumDepthFormats + 1];
    struct RateImage {
        u32 width{};
        u32 height{};
        vk::Image image;
        vk::ImageView view;
        VmaAllocation allocation{};
        vk::Buffer staging;
        VmaAllocation staging_allocation{};
        u32 centre_key{~0u}; ///< the centre (in texels) of the rates it holds
        int level{-1};       ///< foveation level (0..3) of the rates it holds
        u32 rect_x{};        ///< active eye picture sub-rectangle within width x height
        u32 rect_y{};
        u32 rect_w{};
        u32 rect_h{};
    };
    /// Records the upload of a rate image's rates (first: its layout is undefined).
    void RecordRateUpload(const RateImage& r, std::vector<u8> data, bool first);
    std::vector<RateImage> rate_images;
    RateImage full_rate; ///< AzaharVR foveation: GetFullRateView's (no staging buffer)
    std::mutex cache_mutex;
    std::array<vk::Image, 2> images;
    std::array<vk::ImageAspectFlags, 2> aspects;
    bool shadow_rendering{};
    RenderPass pass{};
    u32 num_draws{};

    // AzaharFrame gpupasses: the segment being recorded (on the thread that records).
    bool timing{};
    const Framebuffer* next_framebuffer{}; ///< the game's framebuffer of the pass about to begin
    struct Timed {
        u64 frame{};
        VideoCore::GpuPasses::Segment pass;     ///< the render pass in progress
        bool game{};                            ///< a game pass (drawn by the rasterizer)
        bool shadow{};
        VideoCore::GpuPasses::Screen screen{};  ///< from its draws' viewport
        VideoCore::GpuPasses::Screen target_screen{}; ///< from its target's unscaled size
        u32 world_draws{};
        VideoCore::GpuPasses::Why pending{};    ///< announced (Expect) while the pass was open
        VideoCore::GpuPasses::Bucket work{VideoCore::GpuPasses::Bucket::Count}; ///< outside passes
        bool work_open{};                       ///< work of `work` recorded since the last mark
        u32 work_ops{};
        struct Target {
            u64 id;
            u8 passes;
            VideoCore::GpuPasses::Why last_end;
        };
        std::vector<Target> targets; ///< this frame's
    } timed;
};

} // namespace Vulkan
