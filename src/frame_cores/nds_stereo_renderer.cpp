// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/nds_stereo_renderer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>

#include "GPU.h"
#include "NDS.h"

#include "frame_cores/nds_stereo_3d.h"

namespace FrameCores {
namespace {

using melonDS::s32;
using melonDS::u16;
using melonDS::u32;
using melonDS::u8;

constexpr u32 kWidth = NdsDepth::kWidth;
constexpr int kLines = 192;
constexpr int kCaptureHoldFrames = 60; // games that capture every few frames do not flicker

struct Windows {
    u8 win0;
    u8 win1;
};

Windows ReadWindows(const melonDS::GPU2D& engine) {
    return {engine.Win0Active, engine.Win1Active};
}

void WriteWindows(melonDS::GPU2D& engine, Windows windows) {
    engine.Win0Active = windows.win0;
    engine.Win1Active = windows.win1;
}

/// What a right eye's line changes in an engine. Put back by value: the mosaic lines go on with
/// the reference point of an earlier line.
class SavedEngine {
public:
    explicit SavedEngine(melonDS::GPU2D& engine_)
        : engine{engine_}, enabled{engine_.Enabled}, windows{ReadWindows(engine_)} {
        std::memcpy(scroll, engine.BGXPos, sizeof(scroll));
        std::memcpy(ref_x, engine.BGXRefInternal, sizeof(ref_x));
        std::memcpy(ref_y, engine.BGYRefInternal, sizeof(ref_y));
    }
    ~SavedEngine() {
        engine.Enabled = enabled;
        WriteWindows(engine, windows);
        std::memcpy(engine.BGXPos, scroll, sizeof(scroll));
        std::memcpy(engine.BGXRefInternal, ref_x, sizeof(ref_x));
        std::memcpy(engine.BGYRefInternal, ref_y, sizeof(ref_y));
    }
    SavedEngine(const SavedEngine&) = delete;
    SavedEngine& operator=(const SavedEngine&) = delete;

private:
    melonDS::GPU2D& engine;
    bool enabled;
    Windows windows;
    u16 scroll[4];
    s32 ref_x[2];
    s32 ref_y[2];
};

/// Every background in both ways: what a mode does not use, it does not read. 3D in BG0 scrolls
/// by GPU3D's RenderXPos, so it does not move.
void MoveBackgrounds(melonDS::GPU2D& engine, const NdsDepth::LayerShifts& shifts) {
    for (int bg = 0; bg < 4; ++bg) {
        const int shift = shifts[engine.BGCnt[bg] & 3];
        engine.BGXPos[bg] = NdsDepth::ShiftScroll(engine.BGXPos[bg], shift);
        if (bg >= 2) {
            const int i = bg - 2;
            const auto ref =
                NdsDepth::ShiftAffine({engine.BGXRefInternal[i], engine.BGYRefInternal[i]},
                                      engine.BGRotA[i], engine.BGRotC[i], shift);
            engine.BGXRefInternal[i] = ref.x;
            engine.BGYRefInternal[i] = ref.y;
        }
    }
}

bool Shows3D(const melonDS::GPU2D& engine) {
    return (engine.DispCnt & (1u << 3)) != 0 && (engine.LayerEnable & 1) != 0;
}

/// Engine A's line is the same in both eyes: a display mode without layers, or 3D in BG0 that the
/// right eye does not have (its line is the left eye's as a whole, since that 3D is private there).
bool IsFlatLineA(const melonDS::GPU2D& engine, bool right_has_3d) {
    const bool layers = ((engine.DispCnt >> 16) & 3) == 1;
    return !layers || (Shows3D(engine) && !right_has_3d);
}

} // namespace

class StereoSoft::RightEye final : public melonDS::SoftRenderer {
public:
    RightEye(melonDS::NDS& nds, int back_buffer) : melonDS::SoftRenderer(nds) {
        // Never SoftRenderer's own: its GetLine would wait for lines of the left eye's thread
        Rend3D = std::make_unique<NdsRight3D>(GPU.GPU3D, *this);
        BackBuffer = back_buffer;
        Reset(); // its buffers start undefined
    }

    // The base class expects its own 3D renderer in these
    void SetRenderSettings(melonDS::RendererSettings&) override {}
    void PreSavestate() override {}
    void PostSavestate() override {}

    NdsRight3D& Scene() {
        return static_cast<NdsRight3D&>(*Rend3D);
    }

private:
    friend class StereoSoft;
};

StereoSoft::StereoSoft(melonDS::NDS& nds) : melonDS::SoftRenderer(nds) {}

StereoSoft::~StereoSoft() = default;

void StereoSoft::SetDepth(const NdsDepth::Depth& depth_) {
    wanted = depth_;
}

void StereoSoft::SetRenderSettings(melonDS::RendererSettings& settings) {
    SoftRenderer::SetRenderSettings(settings);
    threaded_3d = settings.Threaded;
}

void StereoSoft::PreSavestate() {
    SoftRenderer::PreSavestate();
    if (right) {
        right->Scene().Wait(); // a load clears the texture memory its thread reads
    }
}

void StereoSoft::PostSavestate() {
    SoftRenderer::PostSavestate();
    // After a load the left eye draws the loaded polygons; ours are the old ones. A save keeps
    // them. A load, and only that, has reset the texture tracking.
    const bool loaded = GPU.VRAMDirty_Texture.Mapping[0] == 0x8000;
    if (right && loaded) {
        right->Scene().Drop();
    }
}

void StereoSoft::Start3DRendering() {
    if (right) {
        right->Scene().Wait(); // the base class writes the texture memory its thread reads
    }
    SoftRenderer::Start3DRendering();
    if (!right) {
        return;
    }
    // Line 215, for the frame that begins at 262: by then the depth is the wanted one, and the
    // hold one frame shorter (BeginFrame)
    const u32 dispcnt = GPU.GPU2D_A.DispCnt;
    const bool in_bg0 = (dispcnt & (1u << 3)) != 0 && (dispcnt & (1u << 8)) != 0;
    const bool shown = in_bg0 && GPU.ScreenSwap;
    if (!wanted.on) {
        right->Scene().Stop();
    } else if (shown && capture_hold <= 1) {
        right->Scene().Start(wanted.scene, threaded_3d);
    } else {
        right->Scene().Drop();
    }
}

void StereoSoft::Restart3DRendering() {
    SoftRenderer::Restart3DRendering();
    if (right) {
        right->Scene().Drop();
    }
}

const u32* StereoSoft::RightTop() {
    if (!right || !right_ready) {
        return nullptr;
    }
    void* top = nullptr;
    void* bottom = nullptr;
    right->GetFramebuffers(&top, &bottom);
    return static_cast<const u32*>(top);
}

/// The row being drawn: GetFramebuffers gives the other buffer, the finished one.
template <typename R>
u32* StereoSoft::BackTopRow(R& renderer, u32 line) {
    void* top = nullptr;
    void* bottom = nullptr;
    renderer.BackBuffer ^= 1;
    renderer.GetFramebuffers(&top, &bottom);
    renderer.BackBuffer ^= 1;
    return static_cast<u32*>(top) + line * kWidth;
}

void StereoSoft::Reset() {
    SoftRenderer::Reset();
    if (right) {
        right->Reset();
    }
    capture_hold = 0;
    lines = {};
    drawing = false; // until the next frame's first sprites
    right_ready = false;
}

void StereoSoft::Stop() {
    SoftRenderer::Stop();
    if (right) {
        right->Stop();
    }
    right_ready = false;
}

void StereoSoft::BeginFrame() {
    depth = wanted;
    if (!depth.on) {
        right.reset();
        capture_hold = 0;
        drawing = false;
        return;
    }
    if (!right) {
        right = std::make_unique<RightEye>(GPU.NDS, BackBuffer);
    }
    if (capture_hold > 0) {
        --capture_hold;
    }
    drawing = capture_hold == 0;
    a_on_top = GPU.ScreenSwap;
}

void StereoSoft::DrawSprites(u32 line) {
    SoftRenderer::DrawSprites(line);
    if (line == 0) {
        // At line 262, before SwapBuffers: sprites are drawn a line ahead, so a frame starts here
        BeginFrame();
    }
    if (drawing) {
        DrawRightSprites(line);
    }
}

/// Moves the sprites in OAM itself, since the 2D renderer reads it there. Safe: this runs inside
/// the HBlank event, where neither CPU nor a DMA does, and the 3D thread never reads OAM. Not by
/// WriteOAM, as nothing changes for the game.
void StereoSoft::DrawRightSprites(u32 line) {
    std::array<u16, 1024> saved;
    static_assert(sizeof(saved) == sizeof(GPU.OAM));
    std::memcpy(saved.data(), GPU.OAM, sizeof(saved));

    std::array<u16, 1024> moved = saved;
    for (std::size_t i = 0; i < moved.size(); i += 4) {
        moved[i + 1] =
            NdsDepth::ShiftSprite(moved[i], moved[i + 1], moved[i + 2], depth.layers);
    }

    std::memcpy(GPU.OAM, moved.data(), sizeof(moved));
    (a_on_top ? right->Rend2D_A : right->Rend2D_B)->DrawSprites(line);
    std::memcpy(GPU.OAM, saved.data(), sizeof(saved));
}

void StereoSoft::DrawScanline(u32 line) {
    if (right && GPU.CaptureEnable) {
        // On from a frame's first line to its last, so no frame is half moved
        capture_hold = kCaptureHoldFrames;
        drawing = false;
    }
    if (GPU.ScreenSwap != a_on_top) {
        drawing = false; // the other engine's sprites were not drawn
    }
    if (!drawing) {
        SoftRenderer::DrawScanline(line);
        return;
    }

    // Drawing a line moves the windows' state on: both eyes start from the same
    const Windows windows_a = ReadWindows(GPU.GPU2D_A);
    const Windows windows_b = ReadWindows(GPU.GPU2D_B);
    const bool has_3d = right->Scene().HasLine();

    SoftRenderer::DrawScanline(line);

    const bool flat = a_on_top && IsFlatLineA(GPU.GPU2D_A, has_3d);
    {
        const SavedEngine saved_a{GPU.GPU2D_A};
        const SavedEngine saved_b{GPU.GPU2D_B};
        WriteWindows(GPU.GPU2D_A, windows_a);
        WriteWindows(GPU.GPU2D_B, windows_b);
        MoveBackgrounds(a_on_top ? GPU.GPU2D_A : GPU.GPU2D_B, depth.layers);
        // A disabled engine's line is a fill instead of layers
        if (flat || !a_on_top) {
            GPU.GPU2D_A.Enabled = false;
        }
        if (a_on_top) {
            GPU.GPU2D_B.Enabled = false;
        }

        const bool capture = GPU.CaptureEnable;
        GPU.CaptureEnable = false; // the game's VRAM is the left eye's
        right->DrawScanline(line);
        GPU.CaptureEnable = capture;
    }

    if (flat) {
        std::memcpy(BackTopRow(*right, line), BackTopRow(*this, line), kWidth * sizeof(u32));
    } else {
        ++lines.moved;
        lines.with_3d += a_on_top && has_3d && Shows3D(GPU.GPU2D_A);
    }
    ++lines.drawn;
}

void StereoSoft::SwapBuffers() {
    SoftRenderer::SwapBuffers();
    if (right) {
        right->SwapBuffers();
    }
    right_ready = right && lines.drawn == kLines;
    last_lines = lines;
    lines = {};
}

} // namespace FrameCores
