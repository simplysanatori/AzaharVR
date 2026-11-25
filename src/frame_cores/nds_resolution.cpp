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

// melonDS's marks of which blocks of VRAM are a display capture that is in the renderer's textures
// (GPU.cpp's CBFlag). They outlive a renderer (GPU::SetRenderer), and are private to it.
using CaptureMarks = melonDS::u16 (&)[16];
CaptureMarks Marks(melonDS::GPU& gpu);
template <auto Member>
struct MarksOf {
    friend CaptureMarks Marks(melonDS::GPU& gpu) {
        return gpu.*Member;
    }
};
template struct MarksOf<&melonDS::GPU::VRAMCaptureBlockFlags>;
constexpr melonDS::u16 kIsCapture = 1 << 15;
constexpr melonDS::u16 kSynced = 1 << 13; ///< also in VRAM

/// A frame's 3D is drawn before the frame, and its line 0's sprites: a renderer put in place
/// between two frames draws them itself.
void DrawAhead(melonDS::NDS& nds) {
    nds.GPU.GPU3D.RenderFrameIdentical = false; // to the renderer before
    nds.GetRenderer().Start3DRendering();
    nds.GetRenderer().DrawSprites(0);
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

void NdsResolution::FailDepth(const std::string& why) {
    LOG_ERROR(Core, "NdsCore: Layer Depth is at 1x: {}", why);
    depth_is_flat = true;
    leave = true;
}

/// The software renderer in place of another. Its 3D thread begins with the coming frame's 3D, once
/// more, when it is turned on: only after DrawAhead, which tells it there is something to draw.
void NdsResolution::PutSoftware(melonDS::NDS& nds, bool threaded) {
    nds.SetRenderer(soft ? soft(nds) : std::make_unique<melonDS::SoftRenderer>(nds));
    DrawAhead(nds);
    melonDS::RendererSettings settings{};
    settings.ScaleFactor = 1;
    settings.Threaded = threaded;
    nds.GetRenderer().SetRenderSettings(settings);
}

void NdsResolution::Apply(melonDS::NDS& nds, bool threaded, const NdsDepth::Depth& depth) {
    depth_on = depth.on;
    held = depth.on && depth_is_flat;
    const uint32_t to = Available() && !held ? wanted : 1;
    if (to == scale && !leave) {
        if (!hires || hires->SetDepth(depth)) {
            return;
        }
        FailDepth("no second eye");
    }
    if (to > 1 && !hires) {
        std::string why = "no reason given";
        auto renderer = maker(nds, why);
        if (!renderer) {
            Fail(why);
            return;
        }
        const auto* const made = renderer->renderer.get();
        nds.SetRenderer(std::move(renderer->renderer));
        if (&nds.GetRenderer() != made) { // melonDS put its own there, without its settings
            Fail("the renderer could not start");
            PutSoftware(nds, threaded);
            return;
        }
        hires = renderer->control;
        scale = 1;
        // Its textures hold no captures yet; melonDS leaves the marks of those it copied to VRAM
        // for the CPU before.
        for (auto& mark : Marks(nds.GPU)) {
            mark = 0;
        }
    }
    if (to > 1 && !leave && !hires->SetScale(to)) {
        const std::string why = "the renderer cannot draw at " + std::to_string(to) + "x";
        depth.on ? FailDepth(why + " for both eyes") : Fail(why);
        leave = true;
    }
    if (to > 1 && !leave && !hires->SetDepth(depth)) {
        FailDepth("no second eye at " + std::to_string(to) + "x");
    }
    if (to == 1 || leave) {
        hires = nullptr;
        leave = false;
        held = depth.on && depth_is_flat;
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
        constexpr const char* why = "the renderer stopped working";
        depth_on ? FailDepth(why) : Fail(why);
        leave = true;
        return false;
    }
    for (Vulkan::AltScreenImage* image : {&top, &bottom}) {
        image->scale = scale;
        image->max_scale = std::max(scale, ceiling);
    }
    return true;
}

bool NdsResolution::GetRight(int screen, Vulkan::AltScreenImage& right) {
    Vulkan::AltScreenImage image = right;
    if (!hires || !hires->GetRight(screen, image)) {
        return false;
    }
    image.scale = scale;
    image.max_scale = right.max_scale;
    right = image;
    return true;
}

bool NdsResolution::ShowsDepth(int screen) const {
    return hires && hires->ShowsDepth(screen);
}

int NdsResolution::RightShift() const {
    return hires ? hires->RightShift() : 0;
}

void NdsResolution::ReadScreens(Vulkan::AltScreenImage& top, Vulkan::AltScreenImage& bottom) {
    if (hires) {
        hires->ReadScreens(top, bottom);
    }
}

void NdsResolution::SetMaker(NdsHiresMaker to, melonDS::NDS* nds, bool threaded) {
    if (hires && nds) {
        PutSoftware(*nds, threaded);
    }
    Detach();
    maker = std::move(to);
    leave = false;
    failed = false;
    depth_is_flat = held = false;
}

NdsResolution::CapturesStay::CapturesStay(const NdsResolution& resolution, melonDS::NDS& nds_) {
    if (resolution.hires) {
        nds = &nds_;
        std::copy_n(Marks(nds->GPU), flags.size(), flags.begin());
    }
}

NdsResolution::CapturesStay::~CapturesStay() {
    if (!nds) {
        return;
    }
    for (std::size_t i = 0; i < flags.size(); i++) {
        if ((flags[i] & kIsCapture) != 0) {
            Marks(nds->GPU)[i] = flags[i] | kSynced;
        }
    }
    nds->GetRenderer().DrawSprites(0); // the coming frame's, which the renderer forgot
}

} // namespace FrameCores
