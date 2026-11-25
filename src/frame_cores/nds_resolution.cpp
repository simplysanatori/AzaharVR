// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/nds_resolution.h"

#include <algorithm>
#include <utility>

#include "GPU_Soft.h"
#include "NDS.h"

#include "common/logging/log.h"

namespace FrameCores {

namespace {

/// A frame's 3D is drawn before the frame, and its line 0's sprites: a renderer put in place
/// between two frames draws them itself.
void DrawAhead(melonDS::NDS& nds) {
    nds.GPU.GPU3D.RenderFrameIdentical = false; // to the renderer before
    nds.GetRenderer().Start3DRendering();
    nds.GetRenderer().DrawSprites(0);
}

/// The software renderer in place of another. Its 3D thread begins with the coming frame's 3D, once
/// more, when it is turned on: only after DrawAhead, which tells it there is something to draw.
void PutSoftware(melonDS::NDS& nds, bool threaded) {
    nds.SetRenderer(std::make_unique<melonDS::SoftRenderer>(nds));
    DrawAhead(nds);
    melonDS::RendererSettings settings{};
    settings.ScaleFactor = 1;
    settings.Threaded = threaded;
    nds.GetRenderer().SetRenderSettings(settings);
}

} // namespace

void NdsResolution::SetFactor(uint32_t factor, uint32_t max) {
    wanted = std::clamp(factor, 1u, kMaxNdsScale);
    ceiling = std::clamp(max, wanted, kMaxNdsScale);
}

void NdsResolution::Fail(const std::string& why) {
    LOG_ERROR(Core, "NdsCore: Internal Resolution stays at 1x: {}", why);
    failed = true;
}

void NdsResolution::Apply(melonDS::NDS& nds, bool threaded) {
    const uint32_t to = Available() ? wanted : 1;
    if (to == scale && !leave) {
        return;
    }
    if (to > 1 && !hires) {
        std::string why = "no reason given";
        auto renderer = maker(nds, why);
        if (!renderer) {
            Fail(why);
            return;
        }
        auto* const made = renderer.get();
        nds.SetRenderer(std::move(renderer));
        if (&nds.GetRenderer() != made) { // melonDS put its own there, without its settings
            Fail("the renderer could not start");
            PutSoftware(nds, threaded);
            return;
        }
        hires = made;
        scale = 1;
    }
    if (to > 1 && !hires->SetScale(to)) {
        Fail("the renderer cannot draw at " + std::to_string(to) + "x");
        leave = true;
    }
    if (to == 1 || leave) {
        hires = nullptr;
        leave = false;
        scale = 1;
        PutSoftware(nds, threaded);
        return;
    }
    scale = to;
    DrawAhead(nds);
}

bool NdsResolution::GetScreens(Vulkan::AltScreenImage& top, Vulkan::AltScreenImage& bottom) {
    if (!hires) {
        return false;
    }
    if (!hires->GetScreens(top, bottom)) {
        Fail("the renderer stopped working");
        leave = true;
        return false;
    }
    for (Vulkan::AltScreenImage* image : {&top, &bottom}) {
        image->scale = scale;
        image->max_scale = std::max(scale, ceiling);
    }
    return true;
}

NdsHiresMaker DefaultNdsHires() {
    return {};
}

} // namespace FrameCores
