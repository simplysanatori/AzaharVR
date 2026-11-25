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

namespace FrameCores {
namespace {

using melonDS::s32;
using melonDS::u16;
using melonDS::u32;
using melonDS::u8;

constexpr u32 kWidth = 256;
constexpr int kLines = 192;
constexpr int kCaptureHoldFrames = 60; // games that capture every few frames do not flicker

/// No 3D for the right eye, and no semaphores: SoftRenderer3D::GetLine takes one of the render
/// thread's lines per call, and the left eye has taken them all. The place for polygons per eye.
class No3D final : public melonDS::Renderer3D {
public:
    explicit No3D(melonDS::GPU3D& gpu3d) : melonDS::Renderer3D(gpu3d) {}
    void Reset() override {}
    void RenderFrame() override {}
    u32* GetLine(int) override {
        return empty.data();
    }

private:
    std::array<u32, kWidth> empty{};
};

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
void MoveBackgrounds(melonDS::GPU2D& engine, int level) {
    for (int bg = 0; bg < 4; ++bg) {
        const int shift = NdsDepth::LayerShift(level, engine.BGCnt[bg] & 3);
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

/// Engine A's line is the same in both eyes: 3D in BG0, or a display mode without layers.
bool IsFlatLineA(const melonDS::GPU2D& engine) {
    const bool has_3d = (engine.DispCnt & (1u << 3)) != 0 && (engine.LayerEnable & 1) != 0;
    const bool layers = ((engine.DispCnt >> 16) & 3) == 1;
    return has_3d || !layers;
}

} // namespace

class StereoSoft::RightEye final : public melonDS::SoftRenderer {
public:
    RightEye(melonDS::NDS& nds, int back_buffer) : melonDS::SoftRenderer(nds) {
        Rend3D = std::make_unique<No3D>(GPU.GPU3D);
        BackBuffer = back_buffer;
        Reset(); // its buffers start undefined
    }

    // The base class expects its own 3D renderer in these
    void SetRenderSettings(melonDS::RendererSettings&) override {}
    void PreSavestate() override {}
    void PostSavestate() override {}

private:
    friend class StereoSoft;
};

StereoSoft::StereoSoft(melonDS::NDS& nds) : melonDS::SoftRenderer(nds) {}

StereoSoft::~StereoSoft() = default;

void StereoSoft::SetDepth(int level_) {
    wanted_level = std::clamp(level_, 0, NdsDepth::kMaxLevel);
}

bool StereoSoft::GetRightFramebuffers(void** top, void** bottom) {
    if (right && right_ready) {
        return right->GetFramebuffers(top, bottom);
    }
    GetFramebuffers(top, bottom);
    return false;
}

/// The row being drawn: GetFramebuffers gives the other buffer, the finished one.
template <typename R>
u32* StereoSoft::BackRow(R& renderer, int screen, u32 line) {
    void* screens[2]{};
    renderer.BackBuffer ^= 1;
    renderer.GetFramebuffers(&screens[0], &screens[1]);
    renderer.BackBuffer ^= 1;
    return static_cast<u32*>(screens[screen]) + line * kWidth;
}

void StereoSoft::Reset() {
    SoftRenderer::Reset();
    if (right) {
        right->Reset();
    }
    capture_hold = 0;
    drawn_lines = 0;
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
    level = wanted_level;
    if (level == 0) {
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
        moved[i + 1] = NdsDepth::ShiftSprite(moved[i], moved[i + 1], moved[i + 2], level);
    }

    std::memcpy(GPU.OAM, moved.data(), sizeof(moved));
    right->DrawSprites(line);
    std::memcpy(GPU.OAM, saved.data(), sizeof(saved));
}

void StereoSoft::DrawScanline(u32 line) {
    if (right && GPU.CaptureEnable) {
        // On from a frame's first line to its last, so no frame is half moved
        capture_hold = kCaptureHoldFrames;
        drawing = false;
    }
    if (!drawing) {
        SoftRenderer::DrawScanline(line);
        return;
    }

    // Drawing a line moves the windows' state on: both eyes start from the same
    const Windows windows_a = ReadWindows(GPU.GPU2D_A);
    const Windows windows_b = ReadWindows(GPU.GPU2D_B);
    const bool flat_a = IsFlatLineA(GPU.GPU2D_A);

    SoftRenderer::DrawScanline(line);

    {
        const SavedEngine saved_a{GPU.GPU2D_A};
        const SavedEngine saved_b{GPU.GPU2D_B};
        WriteWindows(GPU.GPU2D_A, windows_a);
        WriteWindows(GPU.GPU2D_B, windows_b);
        MoveBackgrounds(GPU.GPU2D_A, level);
        MoveBackgrounds(GPU.GPU2D_B, level);
        if (flat_a) {
            GPU.GPU2D_A.Enabled = false; // its line is copied below: a fill instead of layers
        }

        const bool capture = GPU.CaptureEnable;
        GPU.CaptureEnable = false; // the game's VRAM is the left eye's
        right->DrawScanline(line);
        GPU.CaptureEnable = capture;
    }

    if (flat_a) {
        const int screen = GPU.ScreenSwap ? 0 : 1;
        std::memcpy(BackRow(*right, screen, line), BackRow(*this, screen, line),
                    kWidth * sizeof(u32));
    }
    ++drawn_lines;
}

void StereoSoft::SwapBuffers() {
    SoftRenderer::SwapBuffers();
    if (right) {
        right->SwapBuffers();
    }
    right_ready = right && drawn_lines == kLines;
    drawn_lines = 0;
}

} // namespace FrameCores
