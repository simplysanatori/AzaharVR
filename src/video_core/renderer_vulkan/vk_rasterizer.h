// Copyright 2022-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include "video_core/kiu_clear.h" // AzaharFrame kiuhs3
#include "video_core/kiu_powers.h" // AzaharFrame kiupowers
#include "video_core/renderer_vulkan/vk_kiu_talk.h" // AzaharFrame kiutalk
#include "video_core/rasterizer_accelerated.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_stream_buffer.h"
#include "video_core/renderer_vulkan/vk_texture_runtime.h"

namespace Frontend {
class EmuWindow;
}

namespace VideoCore {
class CustomTexManager;
class RendererBase;
} // namespace VideoCore

namespace Pica {
struct DisplayTransferConfig;
struct MemoryFillConfig;
struct FramebufferConfig;
} // namespace Pica

namespace Vulkan {

struct ScreenInfo;

class Instance;
class Scheduler;
class RenderManager;

class RasterizerVulkan : public VideoCore::RasterizerAccelerated {
public:
    explicit RasterizerVulkan(Memory::MemorySystem& memory, Pica::PicaCore& pica,
                              VideoCore::CustomTexManager& custom_tex_manager,
                              VideoCore::RendererBase& renderer, Frontend::EmuWindow& emu_window,
                              const Instance& instance, Scheduler& scheduler,
                              RenderManager& renderpass_cache, DescriptorUpdateQueue& update_queue,
                              u32 image_count);
    ~RasterizerVulkan() override;

    void TickFrame();
    void LoadDefaultDiskResources(const std::atomic_bool& stop_loading,
                                  const VideoCore::DiskResourceLoadCallback& callback) override;

    void DrawTriangles() override;
    void FlushAll() override;
    void FlushRegion(PAddr addr, u32 size) override;
    void InvalidateRegion(PAddr addr, u32 size) override;
    void FlushAndInvalidateRegion(PAddr addr, u32 size) override;
    void ClearAll(bool flush) override;
    bool AccelerateDisplayTransfer(const Pica::DisplayTransferConfig& config) override;
    bool AccelerateTextureCopy(const Pica::DisplayTransferConfig& config) override;
    bool AccelerateFill(const Pica::MemoryFillConfig& config) override;
    bool AccelerateDisplay(const Pica::FramebufferConfig& config, PAddr framebuffer_addr,
                           u32 pixel_stride, ScreenInfo& screen_info);
    bool AccelerateDrawBatch(bool is_indexed) override;

    /// Switches the disk resources to the specified title
    void SwitchDiskResources(u64 title_id) override;

    /// Persists the driver pipeline cache to disk (see RendererBase::SaveDiskCaches)
    void SaveDiskCaches() {
        pipeline_cache.SaveDriverPipelineDiskCache();
    }
    bool DiskResourcesLoadCrashed(u64 title_id) override;

    /// AzaharFrame kiupowers: the power bar's sprites of the game's last bottom screen, and their
    /// textures, in the order the sprites name them (video_core/kiu_powers.h).
    bool KiuPowersFresh() const { // the game still draws them (it draws every other frame)
        return powers_tick - powers_drawn <= 6;
    }
    const std::vector<VideoCore::KiuPowers::Sprite>& KiuPowersSprites() const {
        return powers_sprites;
    }
    template <typename F>
    void KiuPowersTextures(F&& each) {
        if (!KiuPowersFresh()) {
            return;
        }
        for (const auto& texture : powers_textures) {
            Surface& surface = res_cache.GetTextureSurface(texture);
            each(surface.ImageView(), res_cache.GetSampler(texture.config).Handle());
        }
    }

    /// AzaharFrame kiutalk: the talk's portraits and illustrations, and their textures in the order
    /// the sprites name them.
    const KiuTalkDraws& KiuTalk() const {
        return kiu_talk;
    }
    template <typename F>
    void KiuTalkTextures(F&& each) {
        if (!kiu_talk.Fresh()) {
            return;
        }
        for (const auto& texture : kiu_talk.Textures()) {
            Surface& surface = res_cache.GetTextureSurface(texture);
            each(surface.ImageView(), res_cache.GetSampler(texture.config).Handle());
        }
    }

private:
    /// AzaharFrame kiupowers: keeps the bar's quads of a draw of the bottom screen.
    void NoteKiuPowersDraw();
    /// AzaharFrame kiuhs3: the picture's colour cleared before its first world draw (kiu_clear.h), in the
    /// render pass just begun.
    void ClearKiuPicture(bool world, const Common::Rectangle<u32>& rect);

    /// Syncs pipeline state from PICA registers
    void SyncDrawState();

    /// Syncs and uploads the lighting, fog and proctex LUTs
    void SyncAndUploadLUTs();
    void SyncAndUploadLUTsLF();

    /// Syncs all enabled PICA texture units
    void SyncTextureUnits(const Framebuffer* framebuffer);

    /// Syncs all utility textures in the fragment shader.
    void SyncUtilityTextures(const Framebuffer* framebuffer);

    /// Binds the PICA shadow cube required for shadow mapping
    void BindShadowCube(const Pica::TexturingRegs::FullTextureConfig& texture,
                        vk::DescriptorSet texture_set);

    /// Binds a texture cube to texture unit 0
    void BindTextureCube(const Pica::TexturingRegs::FullTextureConfig& texture,
                         vk::DescriptorSet texture_set);

    /// Upload the uniform blocks to the uniform buffer object
    void UploadUniforms(bool accelerate_draw);

    /// Generic draw function for DrawTriangles and AccelerateDrawBatch
    bool Draw(bool accelerate, bool is_indexed);

    /// Internal implementation for AccelerateDrawBatch
    bool AccelerateDrawBatchInternal(bool is_indexed);

    /// Setup index array for AccelerateDrawBatch
    void SetupIndexArray();

    /// Setup vertex array for AccelerateDrawBatch
    void SetupVertexArray();

    /// Setup the fixed attribute emulation in vulkan
    void SetupFixedAttribs();

    /// Setup vertex shader for AccelerateDrawBatch
    bool SetupVertexShader();

    /// Setup geometry shader for AccelerateDrawBatch
    bool SetupGeometryShader();

    /// Creates the vertex layout struct used for software shader pipelines
    void MakeSoftwareVertexLayout();

private:
    const Instance& instance;
    Scheduler& scheduler;
    RenderManager& renderpass_cache;
    DescriptorUpdateQueue& update_queue;
    PipelineCache pipeline_cache;
    TextureRuntime runtime;
    RasterizerCache res_cache;

    VertexLayout software_layout;
    std::array<u32, 16> binding_offsets{};
    std::array<bool, 16> enable_attributes{};
    std::array<vk::Buffer, 16> vertex_buffers;
    VertexArrayInfo vertex_info;
    PipelineInfo pipeline_info{};

    StreamBuffer stream_buffer;     ///< Vertex+Index buffer
    StreamBuffer uniform_buffer;    ///< Uniform buffer
    StreamBuffer texture_buffer;    ///< Texture buffer
    StreamBuffer texture_lf_buffer; ///< Texture Light-Fog buffer
    vk::UniqueBufferView texture_lf_view;
    vk::UniqueBufferView texture_rg_view;
    vk::UniqueBufferView texture_rgba_view;
    vk::DeviceSize uniform_buffer_alignment;
    u32 uniform_size_aligned_vs_pica;
    u32 uniform_size_aligned_vs;
    u32 uniform_size_aligned_fs;
    bool async_shaders{false};
    // AzaharFrame kiupowers
    std::vector<VideoCore::KiuPowers::Sprite> powers_sprites;
    std::vector<Pica::TexturingRegs::FullTextureConfig> powers_textures;
    u64 powers_tick = 0, powers_drawn = 0; ///< TickFrames; the one of the sprites' last draw
    u32 powers_offset = UINT32_MAX;        ///< the last draw's place in the batch's indices
    static constexpr std::size_t kMostPowersSprites = 64; ///< the bar has 25
    VideoCore::KiuClear::Pictures kiu_pictures; ///< AzaharFrame kiuhs3
    KiuTalkDraws kiu_talk; ///< AzaharFrame kiutalk
};

} // namespace Vulkan
