// Copyright 2026 AzaharVR contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string_view>

#include "video_core/renderer_vulkan/vk_common.h"

// The pipelines of AltXrRenderer's outputs that are not the 3DS's own (alt_present.h has the data).
namespace Vulkan {

/// The present fragment shader's interface: the Primary Screen with the overlay over it, mixed in
/// the pictures' own values as a console mixes them. OVERLAY_SRGB_TARGET: written as linear light,
/// which the target encodes back.
extern const std::string_view kAltOverlayFrag;

/// The pipeline of one output for these shaders, viewport and scissor dynamic. `constant_alpha`:
/// blended by the (dynamic) blend constant, Custom Layout's opacity, else not at all.
vk::Pipeline BuildAltPipeline(vk::Device device, vk::ShaderModule vertex_shader,
                              vk::ShaderModule fragment_shader, vk::PipelineLayout layout,
                              vk::RenderPass render_pass, bool constant_alpha);

} // namespace Vulkan
