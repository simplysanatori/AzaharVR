// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiutalk: Kid Icarus's talk portraits and illustrations (video_core/kiu_talk.h): the
// draws of the game's bottom screen that are theirs (the rasterizer's side), and their pipelines
// in the XR present pass (RendererVulkan::DrawKiuTalk).

#pragma once

#include <string>
#include <vector>

#include "video_core/kiu_talk.h"
#include "video_core/pica/regs_internal.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Memory {
class MemorySystem;
}

namespace Vulkan {

class Instance;

/// The emulation thread.
class KiuTalkDraws {
public:
    /// Once a frame of the emulator.
    void Tick() {
        ++tick;
    }
    /// A draw from the vertex buffers, about to be drawn: kept when it is one of theirs.
    void Note(const Pica::RegsInternal& regs, Memory::MemorySystem& memory);

    /// The game's last bottom screen's (none when it draws no more: it draws every other frame).
    bool Fresh() const {
        return tick - drawn <= 6;
    }
    const std::vector<VideoCore::KiuTalk::Sprite>& Sprites() const {
        return sprites;
    }
    /// In the order the sprites name them.
    const std::vector<Pica::TexturingRegs::FullTextureConfig>& Textures() const {
        return textures;
    }
    VideoCore::KiuTalk::Seen Seen() const;

private:
    void NewPicture();

    std::vector<VideoCore::KiuTalk::Sprite> sprites;
    std::vector<Pica::TexturingRegs::FullTextureConfig> textures;
    VideoCore::KiuTalk::Seen seen;
    bool in_illustration = false;
    bool trace_waits = false;
    std::string traced;
    u64 tick = 0, drawn = 0;
    u32 offset = UINT32_MAX; ///< the last draw's place in the batch's indices
    static constexpr std::size_t kMost = 16; ///< four places of two quads, four pieces
};

/// The quads' pipelines, for one render pass of the XR screens.
class KiuTalkPass {
public:
    /// A vertex: position, texture coordinates, colour (straight alpha).
    static constexpr u32 kFloats = 8;

    KiuTalkPass(const Instance& instance, vk::RenderPass render_pass, vk::PipelineLayout layout,
                bool srgb);
    ~KiuTalkPass();
    KiuTalkPass(const KiuTalkPass&) = delete;
    KiuTalkPass& operator=(const KiuTalkPass&) = delete;

    vk::Pipeline Over() const { ///< drawn over what the pass has, premultiplied
        return over;
    }
    vk::Pipeline Scale() const { ///< multiplies what the pass has by the vertex colour
        return scale;
    }
    bool Srgb() const {
        return srgb;
    }

private:
    vk::Pipeline Create(vk::RenderPass render_pass, vk::PipelineLayout layout, bool multiply);

    vk::Device device;
    bool srgb;
    vk::ShaderModule vertex, fragment;
    vk::Pipeline over, scale;
};

} // namespace Vulkan
