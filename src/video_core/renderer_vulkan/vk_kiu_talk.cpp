// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiutalk: vk_kiu_talk.h.

#include <array>
#include <cstring>
#include <string_view>
#include <fmt/format.h>

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/memory.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_kiu_talk.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

namespace KT = VideoCore::KiuTalk;

void KiuTalkDraws::NewPicture() {
    if (KT::g_live.trace.load(std::memory_order_relaxed)) {
        std::string now;
        for (const KT::Sprite& s : sprites) {
            now += fmt::format(" {}{:08X}..{:08X} x {:.0f}..{:.0f} y {:.0f}..{:.0f}",
                               s.kind == KT::Kind::PortraitLeft    ? "L "
                               : s.kind == KT::Kind::PortraitRight ? "R "
                                                                   : "I ",
                               s.quad.color[0], s.quad.color[2], s.quad.x[0], s.quad.x[1],
                               s.quad.y[0], s.quad.y[2]);
        }
        if (now != traced) {
            const KT::Expected e = KT::g_live.GetExpected();
            LOG_INFO(Render_Vulkan, "kiu talk: {} portraits of {}, {} pieces{}{}:{}",
                     seen.portraits, e.portraits, seen.pieces,
                     e.illustration ? " (an illustration)" : "", Seen().wrong ? ", WRONG" : "",
                     now);
            traced = std::move(now);
        }
    }
    sprites.clear();
    textures.clear();
    seen = {};
    in_illustration = false;
}

KT::Seen KiuTalkDraws::Seen() const {
    if (!Fresh()) {
        return {};
    }
    KT::Seen s = seen;
    s.wrong |= in_illustration; // never closed
    return s;
}

void KiuTalkDraws::Note(const Pica::RegsInternal& regs, Memory::MemorySystem& memory) {
    const VideoCore::KiuHud::Batch batch = KT::g_live.Get();
    if (batch.quads == 0) {
        NewPicture();
        offset = UINT32_MAX;
        return;
    }
    const auto& va = regs.pipeline.vertex_attributes;
    const auto& loader = va.attribute_loaders[0];
    const auto& index_array = regs.pipeline.index_array;
    const u32 base = va.GetPhysicalBaseAddress();
    // The bottom screen's, of the batch, its indices 16 bit and its vertices 24 bytes.
    if (Pica::f24::FromRaw(regs.rasterizer.viewport_size_y).ToFloat32() * 2.0f != 320.0f ||
        index_array.format.Value() == 0 ||
        !VideoCore::KiuHud::InBatch(batch, base, index_array.offset, regs.pipeline.num_vertices) ||
        loader.byte_count != 24) {
        return;
    }
    // A picture's draws go up through the batch's indices.
    if (index_array.offset <= offset) {
        NewPicture();
    }
    offset = index_array.offset;
    drawn = tick;

    const u8* const index = memory.GetPhysicalPointer(base + index_array.offset);
    if (!index) {
        return;
    }
    const auto read = [&](u32 q, KT::Quad& out) {
        u16 first = 0xFFFF;
        for (u32 k = 0; k < 6; ++k) {
            u16 i;
            std::memcpy(&i, index + (q * 6 + k) * 2, 2);
            first = std::min(first, i);
        }
        // Four vertices in a row: top left, top right, bottom left, bottom right.
        for (u32 k = 0; k < 4; ++k) {
            const u8* const v =
                memory.GetPhysicalPointer(base + loader.data_offset + (first + k) * 24u);
            if (!v) {
                return false;
            }
            float f[6];
            std::memcpy(f, v, sizeof f);
            std::memcpy(&out.color[k], v + 12, 4);
            out.x[k] = f[0];
            out.y[k] = f[1];
            out.u[k] = f[4];
            out.v[k] = f[5];
        }
        return true;
    };

    using Factor = Pica::FramebufferRegs::BlendFactor;
    using Equation = Pica::FramebufferRegs::BlendEquation;
    const auto& om = regs.framebuffer.output_merger;
    const auto& tex = regs.texturing;
    KT::DrawIn d;
    d.textured = tex.main_config.texture0_enable != 0;
    d.width = tex.texture0.width;
    d.height = tex.texture0.height;
    d.format = static_cast<u32>(tex.texture0_format.Value());
    d.texture = tex.texture0.GetPhysicalAddress();
    d.over = om.alphablend_enable != 0 &&
             om.alpha_blending.factor_source_rgb == Factor::SourceAlpha &&
             om.alpha_blending.factor_dest_rgb == Factor::OneMinusSourceAlpha &&
             om.alpha_blending.blend_equation_rgb == Equation::Add &&
             om.alpha_test.enable == 0;
    d.quads = regs.pipeline.num_vertices / 6;
    if (d.quads > 4 && !in_illustration) {
        return;
    }
    const auto stages = tex.GetTevStages();
    std::array<KT::Stage, 6> tev;
    for (std::size_t i = 0; i < 6; ++i) {
        tev[i] = {stages[i].sources_raw, stages[i].modifiers_raw, stages[i].ops_raw,
                  stages[i].scales_raw, stages[i].const_color};
    }
    u32 constant = 0;
    d.tev = KT::TevOf(tev, &constant);
    if (!read(0, d.first)) {
        return;
    }
    const KT::Part part = KT::PartOf(d, in_illustration);
    switch (part) {
    case KT::Part::Other:
        return;
    case KT::Part::Wrong:
        seen.wrong = true;
        return;
    case KT::Part::IllustrationOpens:
        in_illustration = true;
        return;
    case KT::Part::IllustrationCloses:
        in_illustration = false;
        return;
    default:
        break;
    }
    if (sprites.size() + d.quads > kMost) {
        seen.wrong = true;
        return;
    }
    KT::Sprite s;
    s.kind = part == KT::Part::Portrait ? KT::SideOf(d.first) : KT::Kind::Illustration;
    s.tev = d.tev;
    s.constant = constant;
    s.texture = static_cast<u32>(textures.size());
    const std::size_t before = sprites.size();
    for (u32 q = 0; q < d.quads; ++q) {
        if (!read(q, s.quad) || !KT::Finite(s.quad)) {
            sprites.resize(before);
            seen.wrong = true;
            return;
        }
        sprites.push_back(s);
    }
    textures.push_back(tex.GetTextures()[0]);
    ++(part == KT::Part::Portrait ? seen.portraits : seen.pieces);
}

namespace {

constexpr std::string_view VERT = R"(
#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 position;
layout (location = 1) in vec2 tex_coord;
layout (location = 2) in vec4 tint;
layout (location = 0) out vec2 frag_tex_coord;
layout (location = 1) out vec4 frag_tint;

layout (push_constant, std140) uniform DrawInfo {
    mat4 modelview_matrix;
    vec4 constant; // the TEV's, times its stage's scale
    vec4 unused;
    int tev;
};

void main() {
    vec4 p = vec4(position, 0.0, 1.0) * modelview_matrix;
    gl_Position = vec4(p.xy, 0.0, 1.0);
    frag_tex_coord = tex_coord;
    frag_tint = tint;
}
)";

// KT::Tev 1 the texture times the colour, 2 the portraits'; 0: the colour as it is (the Scale
// pipeline's factor). Premultiplied; in an sRGB image made linear as a whole, as the powers'
// (vk_xr_target.cpp XR_SPRITE_FRAG).
constexpr std::string_view FRAG = R"(
#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 frag_tex_coord;
layout (location = 1) in vec4 frag_tint;
layout (location = 0) out vec4 color;

layout (push_constant, std140) uniform DrawInfo {
    mat4 modelview_matrix;
    vec4 constant;
    vec4 unused;
    int tev;
};

layout (set = 0, binding = 0) uniform sampler2D textures[3];

vec3 ToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}

void main() {
    vec4 t = texture(textures[0], frag_tex_coord);
    vec4 c = frag_tint;
    if (tev == 1) {
        c *= t;
    } else if (tev == 2) {
        c.rgb *= 1.0 - clamp((1.0 - t.rgb) * constant.rgb, 0.0, 1.0);
        // A texture pack's black may be a step or two off (4 of 255 in the HD pack: a faint square
        // over the world, unseen over the game's backdrop).
        float a = min(2.0 * min(t.r + t.g, 1.0), 1.0);
        c.a *= max(a - 0.04, 0.0) / 0.96;
    }
    c = clamp(c, 0.0, 1.0);
    if (tev != 0) {
        c.rgb *= c.a;
#ifdef XR_SRGB_TARGET
        c.rgb = ToLinear(c.rgb);
#endif
    }
    color = c;
}
)";

} // Anonymous namespace

KiuTalkPass::KiuTalkPass(const Instance& instance, vk::RenderPass render_pass,
                         vk::PipelineLayout layout, bool srgb_)
    : device{instance.GetDevice()}, srgb{srgb_} {
    vertex = Compile(VERT, vk::ShaderStageFlagBits::eVertex, device);
    fragment = Compile(FRAG, vk::ShaderStageFlagBits::eFragment, device,
                       srgb ? "#define XR_SRGB_TARGET\n" : "");
    over = Create(render_pass, layout, false);
    scale = Create(render_pass, layout, true);
}

KiuTalkPass::~KiuTalkPass() {
    device.destroyPipeline(over);
    device.destroyPipeline(scale);
    device.destroyShaderModule(vertex);
    device.destroyShaderModule(fragment);
}

vk::Pipeline KiuTalkPass::Create(vk::RenderPass render_pass, vk::PipelineLayout layout,
                                 bool multiply) {
    const vk::VertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(float) * kFloats,
        .inputRate = vk::VertexInputRate::eVertex,
    };
    const std::array attributes = {
        vk::VertexInputAttributeDescription{
            .location = 0, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = 0},
        vk::VertexInputAttributeDescription{.location = 1,
                                            .binding = 0,
                                            .format = vk::Format::eR32G32Sfloat,
                                            .offset = sizeof(float) * 2},
        vk::VertexInputAttributeDescription{.location = 2,
                                            .binding = 0,
                                            .format = vk::Format::eR32G32B32A32Sfloat,
                                            .offset = sizeof(float) * 4},
    };
    const vk::PipelineVertexInputStateCreateInfo vertex_input = {
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<u32>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };
    const vk::PipelineInputAssemblyStateCreateInfo input_assembly = {
        .topology = vk::PrimitiveTopology::eTriangleStrip,
        .primitiveRestartEnable = false,
    };
    const vk::PipelineRasterizationStateCreateInfo raster = {
        .depthClampEnable = false,
        .rasterizerDiscardEnable = false,
        .cullMode = vk::CullModeFlagBits::eNone, // a portrait may be mirrored
        .frontFace = vk::FrontFace::eClockwise,
        .depthBiasEnable = false,
        .lineWidth = 1.0f,
    };
    const vk::PipelineMultisampleStateCreateInfo multisampling = {
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = false,
    };
    using Factor = vk::BlendFactor;
    const vk::PipelineColorBlendAttachmentState blend = {
        .blendEnable = true,
        .srcColorBlendFactor = multiply ? Factor::eZero : Factor::eOne,
        .dstColorBlendFactor = multiply ? Factor::eSrcColor : Factor::eOneMinusSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = multiply ? Factor::eZero : Factor::eOne,
        .dstAlphaBlendFactor = multiply ? Factor::eSrcAlpha : Factor::eOneMinusSrcAlpha,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    const vk::PipelineColorBlendStateCreateInfo blending = {
        .logicOpEnable = false,
        .attachmentCount = 1,
        .pAttachments = &blend,
    };
    const vk::Viewport placeholder_viewport = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const vk::Rect2D placeholder_scissor = {{0, 0}, {1, 1}};
    const vk::PipelineViewportStateCreateInfo viewport = {
        .viewportCount = 1,
        .pViewports = &placeholder_viewport,
        .scissorCount = 1,
        .pScissors = &placeholder_scissor,
    };
    const std::array dynamic_states = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamic = {
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };
    const vk::PipelineDepthStencilStateCreateInfo depth = {
        .depthTestEnable = false,
        .depthWriteEnable = false,
        .depthCompareOp = vk::CompareOp::eAlways,
        .depthBoundsTestEnable = false,
        .stencilTestEnable = false,
    };
    const std::array stages = {
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex, .module = vertex, .pName = "main"},
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment, .module = fragment, .pName = "main"},
    };
    const vk::GraphicsPipelineCreateInfo info = {
        .stageCount = static_cast<u32>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &raster,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth,
        .pColorBlendState = &blending,
        .pDynamicState = &dynamic,
        .layout = layout,
        .renderPass = render_pass,
    };
    const auto [result, made] = device.createGraphicsPipeline({}, info);
    ASSERT_MSG(result == vk::Result::eSuccess, "Unable to build the talk's pipeline");
    return made;
}

} // namespace Vulkan
