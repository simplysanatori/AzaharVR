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
#include <vector>

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
        const int shift = NdsDepth::BackgroundShift(engine.BGCnt[bg], shifts);
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

} // namespace

class StereoSoft::RightEye final : public melonDS::SoftRenderer {
public:
    explicit RightEye(melonDS::NDS& nds) : melonDS::SoftRenderer(nds) {
        // Never SoftRenderer's own: its GetLine would wait for lines of the left eye's thread
        Rend3D = std::make_unique<NdsRight3D>(GPU.GPU3D, *this);
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

void StereoSoft::SetDepth(const NdsDepth::Depth& depth_, bool secondary_too) {
    wanted = depth_;
    wanted_secondary = secondary_too;
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
        right->Scene().Cut();
        for (Screen& screen : screens) {
            screen.scene_left.clear(); // a loaded game's pictures are others
            screen.kept = false;
        }
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
    // Line 215, for the frame that begins at 262: by then the depth is the wanted one. The scene
    // is drawn if the screen engine A is on then has a right picture.
    const bool top = watch.SceneNeeded(GPU.ScreenSwap);
    const int screen = watch.NextOnTop() ? 0 : 1;
    if (!wanted.on) {
        right->Scene().Stop();
    } else if (!NdsDepth::ShowsScene(GPU.GPU2D_A.DispCnt)) {
        right->Scene().NoScene(screen);
    } else if (top || wanted_secondary) {
        right->Scene().Start(wanted, screen, threaded_3d);
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

const u32* StereoSoft::Right(int screen) const {
    const Screen& of = screens[screen];
    return right && of.ready ? of.picture.data() : nullptr;
}

NdsDepth::Lines StereoSoft::LastLines() const {
    return right && (screens[0].ready || screens[1].ready) ? last_lines : NdsDepth::Lines{};
}

/// A row of the picture being drawn (GetFramebuffers gives the other buffer, the finished one):
/// `screen` 0 is the top one.
template <typename R>
u32* StereoSoft::BackRow(R& renderer, int screen, u32 line) {
    void* top = nullptr;
    void* bottom = nullptr;
    renderer.BackBuffer ^= 1;
    renderer.GetFramebuffers(&top, &bottom);
    renderer.BackBuffer ^= 1;
    return static_cast<u32*>(screen == 0 ? top : bottom) + line * kWidth;
}

void StereoSoft::Forget() {
    for (Screen& screen : screens) {
        screen.ready = false;
        screen.kept = false;
        screen.scene_left.clear();
    }
    watch = {};
    complete = false;
    lines = {};
    drawing = false; // until the next frame's first sprites
}

void StereoSoft::Reset() {
    SoftRenderer::Reset();
    if (right) {
        right->Reset();
    }
    Forget();
}

void StereoSoft::Stop() {
    SoftRenderer::Stop();
    if (right) {
        right->Stop();
    }
    for (Screen& screen : screens) {
        screen.ready = false;
    }
}

/// Line 262: the frame's lines are all drawn, its buffers not swapped yet. The right eye's
/// pictures are taken out of its buffer, which it never swaps.
void StereoSoft::EndFrame() {
    complete = right && lines.drawn == kLines;
    if (!right) {
        return;
    }
    constexpr std::size_t kPixels = kWidth * kLines;
    for (int at = 0; at < 2; ++at) {
        Screen& screen = screens[at];
        const bool engine_a = (at == 0) == a_on_top;
        const bool drawn = complete && draws[engine_a ? 0 : 1];
        const u32* left = BackRow(*this, at, 0);
        const u32* mine = BackRow(*right, at, 0);
        const bool same = !engine_a && !screen.scene_left.empty() &&
                          NdsDepth::SamePicture(left, screen.scene_left.data(), kPixels);
        if (NdsDepth::KeepsRightPicture(engine_a, !screen.scene_left.empty(), same)) {
            screen.kept = true; // its picture stays
            continue;
        }
        if (!engine_a) {
            screen.kept = false;
        }
        screen.ready = drawn;
        if (drawn) {
            screen.picture.assign(mine, mine + kPixels);
        }
        if (drawn && engine_a) {
            screen.scene_left.assign(left, left + kPixels);
        } else {
            screen.scene_left.clear(); // the right eye's picture of that frame is gone
        }
    }
    if (complete) {
        lines.convergence = right->Scene().Convergence();
        last_lines = lines;
    }
}

void StereoSoft::BeginFrame() {
    depth = wanted;
    secondary = wanted_secondary;
    lines = {};
    if (!depth.on) {
        right.reset();
        Forget();
        return;
    }
    if (!right) {
        right = std::make_unique<RightEye>(GPU.NDS);
    }
    watch.BeginFrame(GPU.ScreenSwap);
    a_on_top = watch.a_on_top;
    // An engine is drawn again if its screen has a right picture. Not engine B where the game has
    // 3D on both screens: its screen keeps the right picture of the frame before (EndFrame).
    const int screen_a = a_on_top ? 0 : 1;
    const int screen_b = 1 - screen_a;
    draws[0] = screen_a == 0 || secondary;
    draws[1] = (screen_b == 0 || secondary) && !screens[screen_b].kept;
    drawing = draws[0] || draws[1];
}

void StereoSoft::DrawSprites(u32 line) {
    SoftRenderer::DrawSprites(line);
    if (line == 0) {
        // At line 262, before SwapBuffers: sprites are drawn a line ahead, so a frame starts here
        EndFrame();
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
        moved[i + 1] = NdsDepth::ShiftSprite(moved[i], moved[i + 1], moved[i + 2], depth.layers);
    }

    std::memcpy(GPU.OAM, moved.data(), sizeof(moved));
    if (draws[0]) {
        right->Rend2D_A->DrawSprites(line);
    }
    if (draws[1]) {
        right->Rend2D_B->DrawSprites(line);
    }
    std::memcpy(GPU.OAM, saved.data(), sizeof(saved));
}

void StereoSoft::DrawScanline(u32 line) {
    if (GPU.ScreenSwap != a_on_top) {
        drawing = false; // the engines' sprites were drawn for the other screens
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

    const bool bg0_on = (GPU.GPU2D_A.LayerEnable & 1) != 0;
    const bool flat = draws[0] && NdsDepth::IsFlatLine(GPU.GPU2D_A.DispCnt, bg0_on, has_3d);
    {
        const SavedEngine saved_a{GPU.GPU2D_A};
        const SavedEngine saved_b{GPU.GPU2D_B};
        WriteWindows(GPU.GPU2D_A, windows_a);
        WriteWindows(GPU.GPU2D_B, windows_b);
        // A disabled engine's line is a fill instead of layers
        if (draws[0] && !flat) {
            MoveBackgrounds(GPU.GPU2D_A, depth.layers);
        } else {
            GPU.GPU2D_A.Enabled = false;
        }
        if (draws[1]) {
            MoveBackgrounds(GPU.GPU2D_B, depth.layers);
        } else {
            GPU.GPU2D_B.Enabled = false;
        }

        const bool capture = GPU.CaptureEnable;
        GPU.CaptureEnable = false; // the game's VRAM is the left eye's
        right->DrawScanline(line);
        GPU.CaptureEnable = capture;
    }

    if (flat) {
        const int screen = a_on_top ? 0 : 1;
        std::memcpy(BackRow(*right, screen, line), BackRow(*this, screen, line),
                    kWidth * sizeof(u32));
    }
    if (!flat || draws[1]) {
        ++lines.moved;
    }
    lines.with_3d += draws[0] && has_3d && NdsDepth::LineShowsScene(GPU.GPU2D_A.DispCnt, bg0_on);
    ++lines.drawn;
}

void StereoSoft::SwapBuffers() {
    SoftRenderer::SwapBuffers();
    complete = false;
}

} // namespace FrameCores
