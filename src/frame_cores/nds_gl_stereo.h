// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <optional>

#include "GPU_OpenGL.h"

#include "frame_cores/nds_depth.h"
#include "frame_cores/nds_stereo_3d.h"

namespace FrameCores {

/**
 * Layer Depth over 1x: melonDS's hi-res renderer, with one more for each eye, which are told of
 * everything after the first, with the backgrounds, sprites and 3D moved as nds_depth.h says. The
 * game and its own picture never see them (the state of OpenGL is the first's own, ds_gl_state.h).
 *
 * Display capture: an eye has captures of its own, from its own pictures, and a background or
 * sprite that shows one is not moved, as the depth is in it. Where engine A is not drawn for the
 * eyes (it is on the bottom screen) the game's capture is taken over. What the CPU reads of a
 * capture is the game's.
 *
 * What is moved leaves room at the right eye's left edge and the left eye's right one, which is
 * black, as with the software renderer (NdsDepth::EdgeLayers): the compositor and the 3D's clear
 * plane end there (nds_gl_edge.h).
 *
 * Only an engine whose screen has depth (NdsDepth::Screens, ShownDepth) is drawn for the eyes. A
 * frame without all of their pictures (the first one, or one whose engines changed screens in it)
 * has none.
 */
class StereoGl : public melonDS::GLRenderer {
public:
    explicit StereoGl(melonDS::NDS& nds);
    ~StereoGl() override;

    /// Between frames, for the next one. False: the eyes' renderers are not to be had, and there
    /// will be none.
    bool SetDepth(const NdsDepth::Depth& depth);

    /// The last frame's picture of an eye, as GetFramebuffers' of the game's own. 0, for either
    /// eye: none, the game's is for both.
    GLuint Picture(NdsDepth::Eye eye, int screen) const;

    /// The screen's picture is one for each eye, also in a frame without them (ShownDepth).
    bool ShowsDepth(int screen) const {
        return shown_depth[screen];
    }

    /// The farthest layer's shift between them, in pixels: they are drawn a frame after SetDepth.
    int Shift() const {
        return shown_shift;
    }

    /// The eyes' renderers are there
    bool HasEyes() const {
        return eyes[0].renderer != nullptr;
    }

    void Reset() override;
    void PostSavestate() override;
    void SetRenderSettings(melonDS::RendererSettings& settings) override;
    void DrawScanline(melonDS::u32 line) override;
    void DrawSprites(melonDS::u32 line) override;
    void Start3DRendering() override;
    void VBlank() override;
    void VBlankEnd() override;
    void AllocCapture(melonDS::u32 bank, melonDS::u32 start, melonDS::u32 len) override;
    void SwapBuffers() override;

private:
    struct HasScene;
    class EyeRenderer;
    class Eye2D;

    /// What of the game's memory has changed since a renderer looked. melonDS has this once, and
    /// whoever looks first takes it: this is one eye's.
    struct Unseen {
        std::array<std::array<melonDS::u64, 4>, 9> vram{};
        std::array<melonDS::u16, 32> abg{};
        std::array<melonDS::u16, 16> aobj{};
        std::array<melonDS::u16, 8> bbg{};
        std::array<melonDS::u16, 8> bobj{};
        std::array<melonDS::u16, 4> abg_pal{};
        std::array<melonDS::u16, 4> bbg_pal{};
        std::array<melonDS::u16, 1> aobj_pal{};
        std::array<melonDS::u16, 1> bobj_pal{};
        std::array<melonDS::u16, 4> texture{};
        std::array<melonDS::u16, 8> tex_pal{};
        melonDS::u32 oam = 0;
        melonDS::u32 palette = 0;

        void All();
        void Note(const melonDS::GPU& gpu);
        void Swap(melonDS::GPU& gpu);
    };

    /// How an eye's 3D is moved, and the columns of it that what is behind it is in.
    struct Drawn {
        NdsDepth::SceneShift shift{};
        int offset = 0;
        int begin = 0;
        int end = 0;
        bool operator==(const Drawn&) const = default;
    };

    struct EyeState {
        std::unique_ptr<EyeRenderer> renderer; ///< none while off
        Unseen unseen{};
        /// Of the 3D that is there, if it is of the game's last frame of 3D
        std::optional<Drawn> drawn{};
        int scene_offset = 0; ///< this frame's 3D's
    };

    bool MakeEyes();
    void DropEyes();
    EyeState& Of(NdsDepth::Eye eye) {
        return eyes[static_cast<std::size_t>(eye)];
    }
    NdsEye3D& SceneOf(NdsDepth::Eye eye);
    template <typename F>
    void With(NdsDepth::Eye eye, F&& f);
    template <typename F>
    void WithEyes(F&& f);
    void NoteUnseen();
    void CopyCaptures(const melonDS::GLRenderer& from, melonDS::GLRenderer& to, melonDS::u32 bank,
                      bool wide);
    void Forget();
    void StartScene();
    void BeginFrame();

    std::array<EyeState, 2> eyes; ///< by Eye
    melonDS::RendererSettings settings{};
    NdsDepth::Depth wanted{};
    NdsDepth::TopWatch watch{};
    ShownDepth shown_depth{};    ///< this frame's
    bool begun = false;          ///< a frame, none of whose lines is drawn yet
    bool a_on_top = false;       ///< engine A, this frame
    std::array<bool, 2> draws{}; ///< engine A, engine B: for the eyes too, this frame
    int next_shift = 0;          ///< of this frame's farthest layer
    int ended_shift = 0;         ///< the one's before
    int shown_shift = 0;         ///< the last frame's
    bool next_scene = false;     ///< the coming frame's 3D is drawn for the eyes
    bool scene = false;          ///< this frame's is
    bool no_scene = false;       ///< a line shows 3D, and it is not
    int lines = -1;              ///< of the eyes', this frame; < 0: not all will be
    /// The frame that ends has all of them, of a screen that has the eyes' pictures
    std::array<bool, 2> complete{};
    bool saving = false;
    std::array<bool, 2> ready{}; ///< the last frame's
};

} // namespace FrameCores
