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

/// The private members of melonDS's renderers that RightEye2D needs. (An explicit instantiation may
/// name them.)
template <auto Member, typename Tag>
struct Opened {
    friend constexpr auto Open(Tag) {
        return Member;
    }
};
struct LineTag {};
struct SpritesTag {};
struct OutputTag {};
struct CompositeTag {};
constexpr auto Open(LineTag);
constexpr auto Open(SpritesTag);
constexpr auto Open(OutputTag);
constexpr auto Open(CompositeTag);
template struct Opened<&melonDS::SoftRenderer2D::BGOBJLine, LineTag>;
template struct Opened<&melonDS::SoftRenderer2D::OBJLine, SpritesTag>;
template struct Opened<&melonDS::SoftRenderer::Output2D, OutputTag>;
template struct Opened<&melonDS::SoftRenderer2D::ColorComposite, CompositeTag>;

/// An eye's 2D, without what the game has beyond its picture's edge (nds_depth.h, kCutsEdge).
class Eye2D final : public melonDS::SoftRenderer2D {
public:
    Eye2D(melonDS::GPU2D& engine, melonDS::SoftRenderer& parent_)
        : melonDS::SoftRenderer2D(engine, parent_), parent{parent_} {}

    /// Of the eye's layers; zeros: none end.
    void SetEnds(const NdsDepth::LayerShifts& ends) {
        shifts = ends;
        columns = NdsDepth::MakeEdgeColumns(ends);
    }

    void DrawSprites(u32 line) override {
        SoftRenderer2D::DrawSprites(line);
        u32* const sprites = this->*Open(SpritesTag{});
        for (int x = columns.begin; x < columns.end; ++x) {
            if (NdsDepth::IsBeyond(x, shifts[(sprites[x] >> 16) & 3])) { // OBJ_BGPrioMask
                sprites[x] = 0;
            }
        }
    }

    void DrawScanline(u32 line) override {
        SoftRenderer2D::DrawScanline(line);
        if (!GPU2D.Enabled || GPU2D.ForcedBlank) {
            return; // no layers
        }
        const u32* const drawn = this->*Open(LineTag{});
        u32* const out = (parent.*Open(OutputTag{}))[GPU2D.Num];
        for (int x = columns.begin; x < columns.end; ++x) {
            const u32 top = drawn[x];
            const u32 under = drawn[x + kWidth];
            if (IsBeyond(top, x)) {
                out[x] = 0xFF000000;
            } else if (IsBeyond(under, x)) {
                out[x] = (this->*Open(CompositeTag{}))(x, top, under & 0xFF000000);
            }
        }
    }

private:
    bool IsBeyond(u32 pixel, int x) const {
        return NdsDepth::IsBeyond(x, NdsDepth::PixelShift(pixel >> 24, GPU2D.BGCnt, shifts));
    }

    melonDS::SoftRenderer& parent;
    NdsDepth::LayerShifts shifts{};
    NdsDepth::EdgeColumns columns{};
};

} // namespace

class StereoSoft::EyeRenderer final : public melonDS::SoftRenderer {
public:
    EyeRenderer(melonDS::NDS& nds, NdsDepth::Eye eye_) : melonDS::SoftRenderer(nds), eye{eye_} {
        // Never SoftRenderer's own: its GetLine would wait for lines of the game's thread
        Rend3D = std::make_unique<NdsEye3D>(GPU.GPU3D, *this, eye);
        Rend2D_A = std::make_unique<Eye2D>(GPU.GPU2D_A, *this);
        Rend2D_B = std::make_unique<Eye2D>(GPU.GPU2D_B, *this);
        Reset(); // its buffers start undefined
    }

    /// Of the depth.
    void SetLayers(const NdsDepth::LayerShifts& depth) {
        layers = NdsDepth::EyeLayers(depth, eye);
        const NdsDepth::LayerShifts ends = NdsDepth::EdgeLayers(depth, eye);
        static_cast<Eye2D&>(*Rend2D_A).SetEnds(ends);
        static_cast<Eye2D&>(*Rend2D_B).SetEnds(ends);
    }
    const NdsDepth::LayerShifts& Layers() const {
        return layers;
    }

    // The base class expects its own 3D renderer in these
    void SetRenderSettings(melonDS::RendererSettings&) override {}
    void PreSavestate() override {}
    void PostSavestate() override {}

    NdsEye3D& Scene() {
        return static_cast<NdsEye3D&>(*Rend3D);
    }

private:
    friend class StereoSoft;
    const NdsDepth::Eye eye;
    NdsDepth::LayerShifts layers{};
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
    for (const auto& eye : eyes) {
        if (eye) {
            eye->Scene().Wait(); // a load clears the texture memory its thread reads
        }
    }
}

void StereoSoft::PostSavestate() {
    SoftRenderer::PostSavestate();
    // After a load the game's are the loaded polygons; ours are the old ones. A save keeps
    // them. A load, and only that, has reset the texture tracking.
    const bool loaded = GPU.VRAMDirty_Texture.Mapping[0] == 0x8000;
    if (HasEyes() && loaded) {
        for (const auto& eye : eyes) {
            eye->Scene().Cut();
        }
        for (Screen& screen : screens) {
            screen.scene_own.clear(); // a loaded game's pictures are others
            screen.kept = false;
        }
    }
}

void StereoSoft::Start3DRendering() {
    for (const auto& eye : eyes) {
        if (eye) {
            eye->Scene().Wait(); // the base class writes the texture memory its thread reads
        }
    }
    SoftRenderer::Start3DRendering();
    if (!HasEyes()) {
        return;
    }
    // Line 215, for the frame that begins at 262: by then the depth is the wanted one. The scene
    // is drawn if the screen engine A is on then has the eyes' pictures.
    const bool needed = watch.SceneNeeded(GPU.ScreenSwap, wanted.screens);
    const int screen = watch.NextOnTop() ? 0 : 1;
    const bool shows = NdsDepth::ShowsScene(GPU.GPU2D_A.DispCnt);
    const NdsEye3D* first = nullptr;
    for (const NdsDepth::Eye eye : {NdsDepth::Eye::Right, NdsDepth::Eye::Left}) {
        NdsEye3D& scene = Of(eye).Scene();
        if (!wanted.on) {
            scene.Stop();
        } else if (!shows) {
            scene.NoScene(screen);
        } else if (needed) {
            scene.Start(wanted, screen, threaded_3d, first);
            first = &scene;
        } else {
            scene.Drop();
        }
    }
}

void StereoSoft::Restart3DRendering() {
    SoftRenderer::Restart3DRendering();
    for (const auto& eye : eyes) {
        if (eye) {
            eye->Scene().Drop();
        }
    }
}

const u32* StereoSoft::Picture(NdsDepth::Eye eye, int screen) const {
    const Screen& of = screens[screen];
    return HasEyes() && of.ready ? of.pictures[static_cast<std::size_t>(eye)].data() : nullptr;
}

bool StereoSoft::ShowsDepth(int screen) const {
    return shown_depth[screen];
}

int StereoSoft::Shift(int screen) const {
    return screens[screen].shift;
}

NdsDepth::Lines StereoSoft::LastLines() const {
    return HasEyes() && (screens[0].ready || screens[1].ready) ? last_lines : NdsDepth::Lines{};
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
        screen.scene_own.clear();
    }
    watch = {};
    shown_depth = {};
    complete = false;
    lines = {};
    drawing = false; // until the next frame's first sprites
}

void StereoSoft::Reset() {
    SoftRenderer::Reset();
    for (const auto& eye : eyes) {
        if (eye) {
            eye->Reset();
        }
    }
    Forget();
}

void StereoSoft::Stop() {
    SoftRenderer::Stop();
    for (const auto& eye : eyes) {
        if (eye) {
            eye->Stop();
        }
    }
    for (Screen& screen : screens) {
        screen.ready = false;
    }
}

/// Line 262: the frame's lines are all drawn, its buffers not swapped yet. The eyes' pictures are
/// taken out of their buffers, which they never swap.
void StereoSoft::EndFrame() {
    complete = HasEyes() && lines.drawn == kLines;
    if (!HasEyes()) {
        return;
    }
    constexpr std::size_t kPixels = kWidth * kLines;
    for (int at = 0; at < 2; ++at) {
        Screen& screen = screens[at];
        const bool engine_a = (at == 0) == a_on_top;
        const bool drawn = complete && draws[engine_a ? 0 : 1];
        const u32* own = BackRow(*this, at, 0);
        const bool same = !engine_a && !screen.scene_own.empty() &&
                          NdsDepth::SamePicture(own, screen.scene_own.data(), kPixels);
        if (NdsDepth::KeepsRightPicture(engine_a, !screen.scene_own.empty(), same)) {
            screen.kept = true; // its pictures stay
            continue;
        }
        if (!engine_a) {
            screen.kept = false;
        }
        screen.ready = drawn;
        if (drawn) {
            for (std::size_t i = 0; i < eyes.size(); ++i) {
                const u32* mine = BackRow(*eyes[i], at, 0);
                screen.pictures[i].assign(mine, mine + kPixels);
            }
            screen.shift = depth.layers[3];
        }
        if (drawn && engine_a) {
            screen.scene_own.assign(own, own + kPixels);
        } else {
            screen.scene_own.clear(); // the eyes' pictures of that frame are gone
        }
    }
    if (complete) {
        lines.convergence = Of(NdsDepth::Eye::Right).Scene().Convergence();
        last_lines = lines;
    }
}

void StereoSoft::BeginFrame() {
    depth = wanted;
    lines = {};
    if (!depth.on) {
        eyes = {};
        Forget();
        return;
    }
    for (const NdsDepth::Eye eye : {NdsDepth::Eye::Left, NdsDepth::Eye::Right}) {
        auto& renderer = eyes[static_cast<std::size_t>(eye)];
        if (!renderer) {
            renderer = std::make_unique<EyeRenderer>(GPU.NDS, eye);
        }
        renderer->SetLayers(depth.layers);
    }
    watch.BeginFrame(GPU.ScreenSwap);
    a_on_top = watch.a_on_top;
    // An engine is drawn again if its screen has the eyes' pictures. Not engine B where the game
    // has 3D on both screens: its screen keeps those of the frame before (EndFrame).
    draws = shown_depth.Note(depth, GPU, a_on_top, false);
    draws[1] = draws[1] && !screens[a_on_top ? 1 : 0].kept;
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
        DrawEyesSprites(line);
    }
}

/// Moves the sprites in OAM itself, since the 2D renderer reads it there. Safe: this runs inside
/// the HBlank event, where neither CPU nor a DMA does, and the 3D thread never reads OAM. Not by
/// WriteOAM, as nothing changes for the game.
void StereoSoft::DrawEyesSprites(u32 line) {
    std::array<u16, 1024> saved;
    static_assert(sizeof(saved) == sizeof(GPU.OAM));
    std::memcpy(saved.data(), GPU.OAM, sizeof(saved));

    for (const auto& eye : eyes) {
        std::array<u16, 1024> moved = saved;
        for (std::size_t i = 0; i < moved.size(); i += 4) {
            moved[i + 1] =
                NdsDepth::ShiftSprite(moved[i], moved[i + 1], moved[i + 2], eye->Layers());
        }
        std::memcpy(GPU.OAM, moved.data(), sizeof(moved));
        if (draws[0]) {
            eye->Rend2D_A->DrawSprites(line);
        }
        if (draws[1]) {
            eye->Rend2D_B->DrawSprites(line);
        }
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

    // Drawing a line moves the windows' state on: all start from the same
    const Windows windows_a = ReadWindows(GPU.GPU2D_A);
    const Windows windows_b = ReadWindows(GPU.GPU2D_B);
    const bool has_3d = eyes[0]->Scene().HasLine() && eyes[1]->Scene().HasLine();

    SoftRenderer::DrawScanline(line);

    const bool bg0_on = (GPU.GPU2D_A.LayerEnable & 1) != 0;
    const bool flat = draws[0] && NdsDepth::IsFlatLine(GPU.GPU2D_A.DispCnt, bg0_on, has_3d);
    for (const auto& eye : eyes) {
        const SavedEngine saved_a{GPU.GPU2D_A};
        const SavedEngine saved_b{GPU.GPU2D_B};
        WriteWindows(GPU.GPU2D_A, windows_a);
        WriteWindows(GPU.GPU2D_B, windows_b);
        // A disabled engine's line is a fill instead of layers
        if (draws[0] && !flat) {
            MoveBackgrounds(GPU.GPU2D_A, eye->Layers());
        } else {
            GPU.GPU2D_A.Enabled = false;
        }
        if (draws[1]) {
            MoveBackgrounds(GPU.GPU2D_B, eye->Layers());
        } else {
            GPU.GPU2D_B.Enabled = false;
        }

        const bool capture = GPU.CaptureEnable;
        GPU.CaptureEnable = false; // the game's VRAM is of its own picture
        eye->DrawScanline(line);
        GPU.CaptureEnable = capture;
    }

    if (flat) {
        const int screen = a_on_top ? 0 : 1;
        for (const auto& eye : eyes) {
            std::memcpy(BackRow(*eye, screen, line), BackRow(*this, screen, line),
                        kWidth * sizeof(u32));
        }
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
