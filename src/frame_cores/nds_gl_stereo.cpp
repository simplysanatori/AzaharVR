// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/nds_gl_stereo.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "GPU2D_OpenGL.h"
#include "GPU3D_OpenGL.h"
#include "NDS.h"

#include "common/logging/log.h"
#include "frame_cores/ds_gl/ds_gl_state.h"
#include "frame_cores/nds_gl_edge.h"
#include "frame_cores/nds_stereo_3d.h"

namespace FrameCores {
namespace {

using melonDS::s32;
using melonDS::u16;
using melonDS::u32;

constexpr int kLines = 192;

/// The private members of melonDS's renderer that the right eye needs. (An explicit instantiation
/// may name them.)
template <auto Member, typename Tag>
struct Opened {
    friend constexpr auto Open(Tag) {
        return Member;
    }
};
struct ScanlinesTag {};
struct LayersTag {};
struct OamTag {};
struct OamDirtyTag {};
struct SpriteDispCntTag {};
struct CompositorTag {};
struct ClearPlaneTag {};
struct ClearBitmapTag {};
struct Wide {};
struct Small {};
constexpr auto Open(ScanlinesTag);
constexpr auto Open(LayersTag);
constexpr auto Open(OamTag);
constexpr auto Open(OamDirtyTag);
constexpr auto Open(SpriteDispCntTag);
constexpr auto Open(CompositorTag);
constexpr auto Open(ClearPlaneTag);
constexpr auto Open(ClearBitmapTag);
constexpr auto Open(Wide);
constexpr auto Open(Small);
template struct Opened<&melonDS::GLRenderer2D::ScanlineConfig, ScanlinesTag>;
template struct Opened<&melonDS::GLRenderer2D::LayerConfig, LayersTag>;
template struct Opened<&melonDS::GLRenderer2D::OAM, OamTag>;
template struct Opened<&melonDS::GLRenderer2D::SpriteConfigDirty, OamDirtyTag>;
template struct Opened<&melonDS::GLRenderer2D::SpriteDispCnt, SpriteDispCntTag>;
template struct Opened<&melonDS::GLRenderer2D::CompositorShader, CompositorTag>;
template struct Opened<&melonDS::GLRenderer3D::ClearShaderPlain, ClearPlaneTag>;
template struct Opened<&melonDS::GLRenderer3D::ClearShaderBitmap, ClearBitmapTag>;
template struct Opened<&melonDS::GLRenderer::CaptureOutput256FB, Wide>;
template struct Opened<&melonDS::GLRenderer::CaptureOutput128FB, Small>;

struct InRightState {
    InRightState() {
        DsGl::UseState(1);
    }
    ~InRightState() {
        DsGl::UseState(0);
    }
    InRightState(const InRightState&) = delete;
    InRightState& operator=(const InRightState&) = delete;
};

void EndAtEdge(std::string& source) {
    if (!NdsGlEdge::Patch(source)) {
        LOG_ERROR(Core, "NdsCore: the right eye has what is beyond its edge: another shader");
    }
}

/// Where nothing is at the left edge, for the next draw with the program.
void SetEdge(GLuint program, const std::array<GLuint, 4>& edge) {
    glUseProgram(program);
    glUniform4ui(glGetUniformLocation(program, "uEdge"), edge[0], edge[1], edge[2], edge[3]);
}

/// Drawing a line moves the windows' state on: both eyes start from the same.
struct Windows {
    u32 a0, a1, b0, b1;
};

Windows ReadWindows(const melonDS::GPU& gpu) {
    return {gpu.GPU2D_A.Win0Active, gpu.GPU2D_A.Win1Active, gpu.GPU2D_B.Win0Active,
            gpu.GPU2D_B.Win1Active};
}

void WriteWindows(melonDS::GPU& gpu, const Windows& windows) {
    gpu.GPU2D_A.Win0Active = static_cast<melonDS::u8>(windows.a0);
    gpu.GPU2D_A.Win1Active = static_cast<melonDS::u8>(windows.a1);
    gpu.GPU2D_B.Win0Active = static_cast<melonDS::u8>(windows.b0);
    gpu.GPU2D_B.Win1Active = static_cast<melonDS::u8>(windows.b1);
}

} // namespace

/// An engine's layers for the right eye. What melonDS's renderer has taken down of the game's
/// registers and sprites is moved, not the game's own.
class StereoGl::RightEye2D final : public melonDS::GLRenderer2D {
public:
    using melonDS::GLRenderer2D::GLRenderer2D;

    /// Between frames.
    void Set(bool draw_, const NdsDepth::LayerShifts& shifts_) {
        draw = draw_;
        if (shifts != shifts_) {
            shifts = shifts_;
            const NdsDepth::LayerShifts ends = NdsDepth::EdgeLayers(shifts);
            std::array<GLuint, 4> edge{};
            for (std::size_t i = 0; i < edge.size(); ++i) {
                edge[i] = static_cast<GLuint>(std::max(0, ends[i]));
            }
            SetEdge(this->*Open(CompositorTag{}), edge);
            this->*Open(OamDirtyTag{}) = true;
        }
    }

    void DrawScanline(u32 line) override {
        if (draw) {
            GLRenderer2D::DrawScanline(line);
            MoveBackgrounds(line);
        }
    }

    void DrawSprites(u32 line) override {
        if (!draw) {
            return;
        }
        bool& oam_dirty = this->*Open(OamDirtyTag{});
        oam_dirty |= GPU2D.DispCnt != this->*Open(SpriteDispCntTag{}); // the sprites' captures
        const bool taken = oam_dirty || (GPU.OAMDirty & (1u << GPU2D.Num)) != 0;
        GLRenderer2D::DrawSprites(line);
        if (taken) {
            MoveSprites();
        }
    }

    void VBlank() override {
        if (draw) {
            GLRenderer2D::VBlank();
        }
    }

private:
    /// 3D in BG0 scrolls by GPU3D's RenderXPos, and its depth is in its vertices.
    void MoveBackgrounds(u32 line) {
        auto& scanline = (this->*Open(ScanlinesTag{})).uScanline[line];
        const auto& layers = this->*Open(LayersTag{});
        for (int bg = (GPU2D.DispCnt & 8) != 0 ? 1 : 0; bg < 4; ++bg) {
            const u32 type = layers.uBGConfig[bg].Type;
            if (type >= 7) {
                continue; // a capture
            }
            const int shift = NdsDepth::BackgroundShift(GPU2D.BGCnt[bg], shifts);
            s32(&offset)[4] = scanline.BGOffset[bg];
            if (bg >= 2 && type >= 2) { // rotated and scaled, as the compositor has it
                const auto ref = NdsDepth::ShiftAffine({offset[0], offset[1]}, GPU2D.BGRotA[bg - 2],
                                                       GPU2D.BGRotC[bg - 2], shift);
                offset[0] = ref.x;
                offset[1] = ref.y;
            } else {
                offset[0] = NdsDepth::ShiftScroll(static_cast<u16>(offset[0]), shift);
            }
        }
    }

    void MoveSprites() {
        u16(&oam)[512] = this->*Open(OamTag{});
        int captures[16];
        GPU2D.GetCaptureInfo_OBJ(captures);
        for (int i = 0; i < 512; i += 4) {
            if (!ShowsCapture(&oam[i], captures)) {
                oam[i + 1] = NdsDepth::ShiftSprite(oam[i], oam[i + 1], oam[i + 2], shifts);
            }
        }
    }

    /// As GLRenderer2D::UpdateOAM finds out.
    bool ShowsCapture(const u16* sprite, const int* captures) const {
        if (((sprite[0] >> 10) & 3) != 3 || (GPU2D.DispCnt & (1 << 6)) != 0) {
            return false;
        }
        static constexpr int kHeights[16] = {8,  8,  16, 8, 16, 8,  32, 8,
                                             32, 16, 32, 8, 64, 32, 64, 8};
        const int height = kHeights[(sprite[0] >> 14) | ((sprite[1] & 0xC000) >> 12)];
        const bool wide = (GPU2D.DispCnt & (1 << 5)) != 0;
        const int tile = sprite[2] & 0x3FF;
        const int start = wide ? ((tile & 0x01F) << 4) + ((tile & 0x3E0) << 7)
                               : ((tile & 0x00F) << 4) + ((tile & 0x3F0) << 7);
        const int end = start + height * (wide ? 512 : 256);
        const int mask = GPU2D.Num != 0 ? 0x7 : 0xF;
        for (int block = start >> 14; block < (end + 0x3FFF) >> 14; ++block) {
            if (captures[block & mask] != -1) {
                return true;
            }
        }
        return false;
    }

    bool draw = false;
    NdsDepth::LayerShifts shifts{};
};

/// The polygons, before the renderer among the bases, which reads them.
struct StereoGl::HasScene {
    explicit HasScene(melonDS::GPU3D& game) : scene{std::make_unique<NdsRight3D>(game)} {}
    std::unique_ptr<NdsRight3D> scene;
};

class StereoGl::RightEye final : private HasScene, public melonDS::GLRenderer {
public:
    explicit RightEye(melonDS::NDS& nds)
        : HasScene{nds.GPU.GPU3D}, melonDS::GLRenderer(nds, false) {
        Rend2D_A = std::make_unique<RightEye2D>(GPU.GPU2D_A, *this);
        Rend2D_B = std::make_unique<RightEye2D>(GPU.GPU2D_B, *this);
        Rend3D = std::make_unique<melonDS::GLRenderer3D>(scene->Scene(), *this);
    }

    NdsRight3D& Scene() {
        return *scene;
    }

    void Set(int engine, bool draw, const NdsDepth::LayerShifts& shifts) {
        static_cast<RightEye2D&>(engine == 0 ? *Rend2D_A : *Rend2D_B).Set(draw, shifts);
    }

    GLuint Output() {
        void* top = nullptr;
        void* bottom = nullptr;
        GetFramebuffers(&top, &bottom);
        return *static_cast<const GLuint*>(top);
    }

    /// The columns, of its own, that the 3D's clear plane has nothing in.
    void SetClearEdge(int columns) {
        const auto& scene3d = static_cast<melonDS::GLRenderer3D&>(*Rend3D);
        const auto edge = static_cast<GLuint>(columns);
        for (const GLuint program :
             {scene3d.*Open(ClearPlaneTag{}), scene3d.*Open(ClearBitmapTag{})}) {
            glUseProgram(program);
            glUniform1ui(glGetUniformLocation(program, "uEdge"), edge);
        }
    }

    /// Both renderers' pictures are swapped together.
    void SetBackBuffer(int to) {
        BackBuffer = to;
    }
};

void StereoGl::Unseen::All() {
    for (auto& bank : vram) {
        bank.fill(0); // what is mapped anew counts as changed
    }
    const auto none = [](auto& mapping) { mapping.fill(0x8000); };
    none(abg);
    none(aobj);
    none(bbg);
    none(bobj);
    none(abg_pal);
    none(bbg_pal);
    none(aobj_pal);
    none(bobj_pal);
    none(texture);
    none(tex_pal);
    oam = ~0u;
    palette = ~0u;
}

void StereoGl::Unseen::Note(const melonDS::GPU& gpu) {
    for (std::size_t bank = 0; bank < vram.size(); ++bank) {
        for (std::size_t i = 0; i < vram[bank].size(); ++i) {
            vram[bank][i] |= gpu.VRAMDirty[bank].Data[i];
        }
    }
    oam |= gpu.OAMDirty;
    palette |= gpu.PaletteDirty;
}

void StereoGl::Unseen::Swap(melonDS::GPU& gpu) {
    for (std::size_t bank = 0; bank < vram.size(); ++bank) {
        static_assert(sizeof(vram[0]) == sizeof(gpu.VRAMDirty[0].Data));
        std::swap_ranges(vram[bank].begin(), vram[bank].end(), gpu.VRAMDirty[bank].Data);
    }
    const auto swap = [](auto& mine, auto& tracking) {
        static_assert(sizeof(mine) == sizeof(tracking.Mapping));
        std::swap_ranges(mine.begin(), mine.end(), tracking.Mapping);
    };
    swap(abg, gpu.VRAMDirty_ABG);
    swap(aobj, gpu.VRAMDirty_AOBJ);
    swap(bbg, gpu.VRAMDirty_BBG);
    swap(bobj, gpu.VRAMDirty_BOBJ);
    swap(abg_pal, gpu.VRAMDirty_ABGExtPal);
    swap(bbg_pal, gpu.VRAMDirty_BBGExtPal);
    swap(aobj_pal, gpu.VRAMDirty_AOBJExtPal);
    swap(bobj_pal, gpu.VRAMDirty_BOBJExtPal);
    swap(texture, gpu.VRAMDirty_Texture);
    swap(tex_pal, gpu.VRAMDirty_TexPal);
    std::swap(oam, gpu.OAMDirty);
    std::swap(palette, gpu.PaletteDirty);
}

StereoGl::StereoGl(melonDS::NDS& nds) : melonDS::GLRenderer(nds, false) {}

StereoGl::~StereoGl() {
    DropRight();
}

/// What the right eye's renderer does after the left one did it: in its own state of OpenGL, with
/// what of the game's memory it has not seen. Everything is as before after it.
template <typename F>
void StereoGl::WithRight(F&& f) {
    const InRightState state;
    unseen.Swap(GPU);
    f(*right);
    unseen.Swap(GPU);
}

bool StereoGl::MakeRight() {
    bool ok = false;
    {
        const InRightState state;
        right = std::make_unique<RightEye>(GPU.NDS);
        DsGl::PatchShaders(&EndAtEdge);
        ok = right->Init();
        DsGl::PatchShaders(nullptr);
        if (ok) {
            right->Reset();
            right->SetRenderSettings(settings);
            right->SetBackBuffer(BackBuffer);
        }
    }
    if (!ok) {
        DropRight();
        return false;
    }
    unseen.All();
    drawn.reset();
    for (u32 bank = 0; bank < 4; ++bank) {
        CopyCaptures(*this, *right, bank, true);
        CopyCaptures(*this, *right, bank, false);
    }
    // The coming frame's 3D and its first line's sprites were drawn ahead
    watch = {};
    begun = false;
    StartScene();
    BeginFrame();
    WithRight([](RightEye& eye) { eye.DrawSprites(0); });
    return true;
}

void StereoGl::DropRight() {
    if (!right) {
        return;
    }
    {
        const InRightState state;
        right.reset();
    }
    DsGl::FreeState();
    shown_depth = {};
    ready = {};
    complete = {};
}

/// A bank of the captures of one renderer to another's.
void StereoGl::CopyCaptures(const melonDS::GLRenderer& from, melonDS::GLRenderer& to, u32 bank,
                            bool wide) {
    const InRightState state;
    glDisable(GL_SCISSOR_TEST);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    const int size = (wide ? 256 : 128) * settings.ScaleFactor;
    for (u32 part = 0; part < (wide ? 1u : 4u); ++part) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, wide ? (from.*Open(Wide{}))[bank]
                                                    : (from.*Open(Small{}))[bank << 2 | part]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                          wide ? (to.*Open(Wide{}))[bank] : (to.*Open(Small{}))[bank << 2 | part]);
        glBlitFramebuffer(0, 0, size, size, 0, 0, size, size, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool StereoGl::SetDepth(const NdsDepth::Depth& depth) {
    wanted = depth;
    if (!depth.on) {
        DropRight();
        return true;
    }
    return right || MakeRight();
}

GLuint StereoGl::Right(int screen) const {
    return right && ready[static_cast<std::size_t>(screen)] ? right->Output() : 0;
}

void StereoGl::Reset() {
    GLRenderer::Reset();
    if (right) {
        const InRightState state;
        right->Reset();
    }
    unseen.All();
    drawn.reset();
    if (!saving) {
        Forget();
    }
}

void StereoGl::Forget() {
    if (right) {
        right->Scene().Cut();
    }
    watch = {};
    shown_depth = {};
    begun = false;
    next_scene = scene = no_scene = false;
    lines = -1;
    complete = {};
    ready = {};
}

void StereoGl::PostSavestate() {
    // After a load the coming frame's 3D is not drawn; after a save it is there still. A load, and
    // only that, has reset the texture tracking.
    saving = GPU.VRAMDirty_Texture.Mapping[0] != 0x8000;
    GLRenderer::PostSavestate(); // Reset
    if (right) {
        const InRightState state;
        right->PostSavestate();
    }
    saving = false;
}

void StereoGl::SetRenderSettings(melonDS::RendererSettings& to) {
    settings = to;
    drawn.reset();
    GLRenderer::SetRenderSettings(to);
    if (right) {
        const InRightState state;
        right->SetRenderSettings(to);
    }
}

/// Line 215, for the frame that begins at 262.
void StereoGl::StartScene() {
    const bool needed = watch.SceneNeeded(GPU.ScreenSwap, wanted.screens);
    const int screen = watch.NextOnTop() ? 0 : 1;
    next_scene = false;
    const auto last = std::exchange(drawn, std::nullopt);
    if (!NdsDepth::ShowsScene(GPU.GPU2D_A.DispCnt)) {
        right->Scene().NoScene(screen);
    } else if (needed) {
        NdsRight3D& of = right->Scene();
        const NdsDepth::SceneShift shift = of.Prepare(wanted, screen);
        // Where the clear plane ends on the screen, in the scene, whose lines are scrolled
        const int ends = std::max(0, NdsDepth::EdgeLayers(wanted.layers)[3]);
        const int scroll = NdsDepth::ScrollOf(GPU.GPU3D.RenderXPos);
        const int edge = ends == 0 ? 0 : NdsDepth::SceneEdge(of.FarShift(), scroll) + scroll;
        drawn = Drawn{shift, edge};
        of.Scene().RenderFrameIdentical = last && *last == *drawn && GPU.GPU3D.RenderFrameIdentical;
        WithRight([&](RightEye& eye) {
            eye.SetClearEdge(std::max(0, edge) * settings.ScaleFactor);
            eye.Start3DRendering();
        });
        next_scene = true;
    }
}

void StereoGl::Start3DRendering() {
    unseen.Note(GPU);
    GLRenderer::Start3DRendering();
    if (right) {
        StartScene();
    }
}

void StereoGl::BeginFrame() {
    if (!begun) {
        watch.BeginFrame(GPU.ScreenSwap);
        scene = std::exchange(next_scene, false);
        ended_shift = next_shift;
    }
    begun = true;
    scene_offset = scene ? right->Scene().Offset() : 0;
    next_shift = wanted.layers[3];
    a_on_top = watch.a_on_top;
    lines = 0;
    no_scene = false;
    draws = shown_depth.Note(wanted, GPU, a_on_top, true);
    const InRightState state;
    for (int engine = 0; engine < 2; ++engine) {
        right->Set(engine, draws[static_cast<std::size_t>(engine)], wanted.layers);
    }
}

void StereoGl::DrawSprites(u32 line) {
    unseen.Note(GPU);
    GLRenderer::DrawSprites(line);
    if (!right) {
        return;
    }
    if (line == 0) {
        // At line 262, before SwapBuffers: sprites are drawn a line ahead, so a frame starts here
        if (!begun) {
            const bool all = lines == kLines;
            const bool a = all && draws[0] && !no_scene;
            const bool b = all && draws[1];
            complete = {a_on_top ? a : b, a_on_top ? b : a};
        }
        BeginFrame();
    }
    WithRight([line](RightEye& eye) { eye.DrawSprites(line); });
}

void StereoGl::DrawScanline(u32 line) {
    unseen.Note(GPU);
    const Windows windows = ReadWindows(GPU);
    GLRenderer::DrawScanline(line);
    if (!right) {
        return;
    }
    begun = false;
    const Windows after = ReadWindows(GPU);
    WriteWindows(GPU, windows);
    const u16 scroll = GPU.GPU3D.RenderXPos;
    GPU.GPU3D.RenderXPos = NdsDepth::ScrollScene(scroll, scene_offset);
    WithRight([line](RightEye& eye) { eye.DrawScanline(line); });
    GPU.GPU3D.RenderXPos = scroll;
    WriteWindows(GPU, after);

    const bool bg0_on = (GPU.GPU2D_A.LayerEnable & 1) != 0;
    no_scene |= draws[0] && !scene && NdsDepth::LineShowsScene(GPU.GPU2D_A.DispCnt, bg0_on);
    if (GPU.ScreenSwap != a_on_top || lines < 0) {
        lines = -1;
    } else {
        ++lines;
    }
}

void StereoGl::VBlank() {
    unseen.Note(GPU);
    GLRenderer::VBlank();
    if (!right) {
        return;
    }
    WithRight([](RightEye& eye) { eye.VBlank(); });
    if (GPU.CaptureEnable && (!draws[0] || no_scene || lines < 0)) {
        // The right eye has no picture of this frame's engine A
        const bool wide = ((GPU.CaptureCnt >> 20) & 3) != 0;
        CopyCaptures(*this, *right, (GPU.CaptureCnt >> 16) & 3, wide);
    }
}

void StereoGl::VBlankEnd() {
    GLRenderer::VBlankEnd();
    if (right) {
        WithRight([](RightEye& eye) { eye.VBlankEnd(); });
    }
}

void StereoGl::AllocCapture(u32 bank, u32 start, u32 len) {
    GLRenderer::AllocCapture(bank, start, len);
    if (right) {
        WithRight([&](RightEye& eye) { eye.AllocCapture(bank, start, len); });
    }
}

void StereoGl::SwapBuffers() {
    GLRenderer::SwapBuffers();
    if (right) {
        right->SwapBuffers();
        ready = std::exchange(complete, {});
        shown_shift = ended_shift;
    }
}

} // namespace FrameCores
