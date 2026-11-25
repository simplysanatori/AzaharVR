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

/// Of the layer's states, the game's own renderer has the first.
struct InState {
    explicit InState(NdsDepth::Eye eye) {
        DsGl::UseState(1 + static_cast<int>(eye));
    }
    ~InState() {
        DsGl::UseState(0);
    }
    InState(const InState&) = delete;
    InState& operator=(const InState&) = delete;
};
static_assert(DsGl::kStates == 3);

/// The right eye's first: its scene's view is the left one's too.
constexpr std::array kEyes{NdsDepth::Eye::Right, NdsDepth::Eye::Left};

void EndAtEdge(std::string& source) {
    if (!NdsGlEdge::Patch(source)) {
        LOG_ERROR(Core, "NdsCore: an eye has what is beyond its edge: another shader");
    }
}

/// Where nothing is at the edge, for the next draw with the program.
void SetEdge(GLuint program, const std::array<GLuint, 4>& edge) {
    glUseProgram(program);
    glUniform4ui(glGetUniformLocation(program, "uEdge"), edge[0], edge[1], edge[2], edge[3]);
}

/// Drawing a line moves the windows' state on: all start from the same.
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

/// An engine's layers for an eye. What melonDS's renderer has taken down of the game's
/// registers and sprites is moved, not the game's own.
class StereoGl::Eye2D final : public melonDS::GLRenderer2D {
public:
    using melonDS::GLRenderer2D::GLRenderer2D;

    /// Between frames.
    void Set(bool draw_, NdsDepth::Eye eye, const NdsDepth::LayerShifts& depth) {
        draw = draw_;
        const NdsDepth::LayerShifts shifts_ = NdsDepth::EyeLayers(depth, eye);
        if (shifts != shifts_) {
            shifts = shifts_;
            const NdsDepth::LayerShifts ends = NdsDepth::EdgeLayers(depth, eye);
            std::array<GLuint, 4> edge{};
            for (std::size_t i = 0; i < edge.size(); ++i) {
                edge[i] = static_cast<GLuint>(ends[i]);
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
    HasScene(melonDS::GPU3D& game, NdsDepth::Eye eye)
        : scene{std::make_unique<NdsEye3D>(game, eye)} {}
    std::unique_ptr<NdsEye3D> scene;
};

class StereoGl::EyeRenderer final : private HasScene, public melonDS::GLRenderer {
public:
    EyeRenderer(melonDS::NDS& nds, NdsDepth::Eye eye_)
        : HasScene{nds.GPU.GPU3D, eye_}, melonDS::GLRenderer(nds, false), eye{eye_} {
        Rend2D_A = std::make_unique<Eye2D>(GPU.GPU2D_A, *this);
        Rend2D_B = std::make_unique<Eye2D>(GPU.GPU2D_B, *this);
        Rend3D = std::make_unique<melonDS::GLRenderer3D>(scene->Scene(), *this);
    }

    NdsEye3D& Scene() {
        return *scene;
    }

    void Set(int engine, bool draw, const NdsDepth::LayerShifts& depth) {
        static_cast<Eye2D&>(engine == 0 ? *Rend2D_A : *Rend2D_B).Set(draw, eye, depth);
    }

    GLuint Output() {
        void* top = nullptr;
        void* bottom = nullptr;
        GetFramebuffers(&top, &bottom);
        return *static_cast<const GLuint*>(top);
    }

    /// The columns, of its own, that the 3D's clear plane is in (nds_gl_edge.h).
    void SetClearEdge(int begin, int end) {
        const auto& scene3d = static_cast<melonDS::GLRenderer3D&>(*Rend3D);
        for (const GLuint program :
             {scene3d.*Open(ClearPlaneTag{}), scene3d.*Open(ClearBitmapTag{})}) {
            glUseProgram(program);
            glUniform4ui(glGetUniformLocation(program, "uEdge"), static_cast<GLuint>(begin),
                         static_cast<GLuint>(end), 0, 0);
        }
    }

    /// The renderers' pictures are swapped together.
    void SetBackBuffer(int to) {
        BackBuffer = to;
    }

private:
    const NdsDepth::Eye eye;
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
    DropEyes();
}

NdsEye3D& StereoGl::SceneOf(NdsDepth::Eye eye) {
    return Of(eye).renderer->Scene();
}

/// What an eye's renderer does after the game's did it: in its own state of OpenGL, with what of
/// the game's memory it has not seen. Everything is as before after it.
template <typename F>
void StereoGl::With(NdsDepth::Eye eye, F&& f) {
    EyeState& of = Of(eye);
    const InState state{eye};
    of.unseen.Swap(GPU);
    f(*of.renderer);
    of.unseen.Swap(GPU);
}

template <typename F>
void StereoGl::WithEyes(F&& f) {
    for (const NdsDepth::Eye eye : kEyes) {
        With(eye, f);
    }
}

void StereoGl::NoteUnseen() {
    for (EyeState& of : eyes) {
        of.unseen.Note(GPU);
    }
}

bool StereoGl::MakeEyes() {
    bool ok = true;
    for (const NdsDepth::Eye eye : kEyes) {
        EyeState& of = Of(eye);
        const InState state{eye};
        of.renderer = std::make_unique<EyeRenderer>(GPU.NDS, eye);
        DsGl::PatchShaders(&EndAtEdge);
        ok = of.renderer->Init();
        DsGl::PatchShaders(nullptr);
        if (!ok) {
            break;
        }
        of.renderer->Reset();
        of.renderer->SetRenderSettings(settings);
        of.renderer->SetBackBuffer(BackBuffer);
        of.unseen.All();
        of.drawn.reset();
    }
    if (!ok) {
        DropEyes();
        return false;
    }
    for (const EyeState& of : eyes) {
        for (u32 bank = 0; bank < 4; ++bank) {
            CopyCaptures(*this, *of.renderer, bank, true);
            CopyCaptures(*this, *of.renderer, bank, false);
        }
    }
    // The coming frame's 3D and its first line's sprites were drawn ahead
    watch = {};
    begun = false;
    StartScene();
    BeginFrame();
    WithEyes([](EyeRenderer& eye) { eye.DrawSprites(0); });
    return true;
}

void StereoGl::DropEyes() {
    if (!eyes[0].renderer && !eyes[1].renderer) {
        return;
    }
    for (const NdsDepth::Eye eye : kEyes) {
        const InState state{eye};
        Of(eye).renderer.reset();
    }
    DsGl::FreeState();
    shown_depth = {};
    ready = {};
    complete = {};
}

/// A bank of the captures of one renderer to another's.
void StereoGl::CopyCaptures(const melonDS::GLRenderer& from, melonDS::GLRenderer& to, u32 bank,
                            bool wide) {
    const InState state{NdsDepth::Eye::Right};
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
        DropEyes();
        return true;
    }
    return HasEyes() || MakeEyes();
}

GLuint StereoGl::Picture(NdsDepth::Eye eye, int screen) const {
    const EyeState& of = eyes[static_cast<std::size_t>(eye)];
    return of.renderer && ready[static_cast<std::size_t>(screen)] ? of.renderer->Output() : 0;
}

void StereoGl::Reset() {
    GLRenderer::Reset();
    for (const NdsDepth::Eye eye : kEyes) {
        EyeState& of = Of(eye);
        if (of.renderer) {
            const InState state{eye};
            of.renderer->Reset();
        }
        of.unseen.All();
        of.drawn.reset();
    }
    if (!saving) {
        Forget();
    }
}

void StereoGl::Forget() {
    for (const EyeState& of : eyes) {
        if (of.renderer) {
            of.renderer->Scene().Cut();
        }
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
    for (const NdsDepth::Eye eye : kEyes) {
        if (Of(eye).renderer) {
            const InState state{eye};
            Of(eye).renderer->PostSavestate();
        }
    }
    saving = false;
}

void StereoGl::SetRenderSettings(melonDS::RendererSettings& to) {
    settings = to;
    GLRenderer::SetRenderSettings(to);
    for (const NdsDepth::Eye eye : kEyes) {
        EyeState& of = Of(eye);
        of.drawn.reset();
        if (of.renderer) {
            const InState state{eye};
            of.renderer->SetRenderSettings(to);
        }
    }
}

/// Line 215, for the frame that begins at 262.
void StereoGl::StartScene() {
    const bool needed = watch.SceneNeeded(GPU.ScreenSwap, wanted.screens);
    const int screen = watch.NextOnTop() ? 0 : 1;
    next_scene = false;
    const bool shows = NdsDepth::ShowsScene(GPU.GPU2D_A.DispCnt);
    const NdsEye3D* first = nullptr;
    for (const NdsDepth::Eye eye : kEyes) {
        EyeState& of = Of(eye);
        NdsEye3D& eye_scene = of.renderer->Scene();
        const auto last = std::exchange(of.drawn, std::nullopt);
        if (!shows) {
            eye_scene.NoScene(screen);
        } else if (needed) {
            const NdsDepth::SceneShift shift = eye_scene.Prepare(wanted, screen, first);
            first = &eye_scene;
            // Where the clear plane ends on the screen, in the scene, whose lines are scrolled
            const bool ends = NdsDepth::EdgeLayers(wanted.layers, eye)[3] != 0;
            const int scroll = NdsDepth::ScrollOf(GPU.GPU3D.RenderXPos);
            const NdsDepth::SceneEnds at = eye_scene.Ends(scroll);
            const int moved = scroll + eye_scene.Offset();
            Drawn now{shift, eye_scene.Offset()};
            if (ends && at.begin > 0) {
                now.begin = std::max(0, at.begin + moved);
            }
            if (ends && at.end < NdsDepth::kWidth) {
                now.end = std::max(1, at.end + moved);
            }
            of.drawn = now;
            eye_scene.Scene().RenderFrameIdentical =
                last && *last == now && GPU.GPU3D.RenderFrameIdentical;
            With(eye, [&](EyeRenderer& renderer) {
                renderer.SetClearEdge(now.begin * settings.ScaleFactor,
                                      now.end * settings.ScaleFactor);
                renderer.Start3DRendering();
            });
            next_scene = true;
        }
    }
}

void StereoGl::Start3DRendering() {
    NoteUnseen();
    GLRenderer::Start3DRendering();
    if (HasEyes()) {
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
    for (EyeState& of : eyes) {
        of.scene_offset = scene ? of.renderer->Scene().Offset() : 0;
    }
    next_shift = wanted.layers[3];
    a_on_top = watch.a_on_top;
    lines = 0;
    no_scene = false;
    draws = shown_depth.Note(wanted, GPU, a_on_top, true);
    for (const NdsDepth::Eye eye : kEyes) {
        const InState state{eye};
        for (int engine = 0; engine < 2; ++engine) {
            Of(eye).renderer->Set(engine, draws[static_cast<std::size_t>(engine)], wanted.layers);
        }
    }
}

void StereoGl::DrawSprites(u32 line) {
    NoteUnseen();
    GLRenderer::DrawSprites(line);
    if (!HasEyes()) {
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
    WithEyes([line](EyeRenderer& eye) { eye.DrawSprites(line); });
}

void StereoGl::DrawScanline(u32 line) {
    NoteUnseen();
    const Windows windows = ReadWindows(GPU);
    GLRenderer::DrawScanline(line);
    if (!HasEyes()) {
        return;
    }
    begun = false;
    const Windows after = ReadWindows(GPU);
    const u16 scroll = GPU.GPU3D.RenderXPos;
    for (const NdsDepth::Eye eye : kEyes) {
        WriteWindows(GPU, windows);
        GPU.GPU3D.RenderXPos = NdsDepth::ScrollScene(scroll, Of(eye).scene_offset);
        With(eye, [line](EyeRenderer& renderer) { renderer.DrawScanline(line); });
    }
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
    NoteUnseen();
    GLRenderer::VBlank();
    if (!HasEyes()) {
        return;
    }
    WithEyes([](EyeRenderer& eye) { eye.VBlank(); });
    if (GPU.CaptureEnable && (!draws[0] || no_scene || lines < 0)) {
        // The eyes have no picture of this frame's engine A
        const bool wide = ((GPU.CaptureCnt >> 20) & 3) != 0;
        for (const EyeState& of : eyes) {
            CopyCaptures(*this, *of.renderer, (GPU.CaptureCnt >> 16) & 3, wide);
        }
    }
}

void StereoGl::VBlankEnd() {
    GLRenderer::VBlankEnd();
    if (HasEyes()) {
        WithEyes([](EyeRenderer& eye) { eye.VBlankEnd(); });
    }
}

void StereoGl::AllocCapture(u32 bank, u32 start, u32 len) {
    GLRenderer::AllocCapture(bank, start, len);
    if (HasEyes()) {
        WithEyes([&](EyeRenderer& eye) { eye.AllocCapture(bank, start, len); });
    }
}

void StereoGl::SwapBuffers() {
    GLRenderer::SwapBuffers();
    if (HasEyes()) {
        for (const EyeState& of : eyes) {
            of.renderer->SwapBuffers();
        }
        ready = std::exchange(complete, {});
        shown_shift = ended_shift;
    }
}

} // namespace FrameCores
