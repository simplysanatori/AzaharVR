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

namespace FrameCores {

/**
 * Layer Depth over 1x: melonDS's hi-res renderer, with a second one for the right eye, which is
 * told of everything after the first, with the backgrounds, sprites and 3D moved as nds_depth.h
 * says. The game and the left picture never see it (the state of OpenGL is the first's own,
 * ds_gl_state.h).
 *
 * Display capture: the right eye has captures of its own, from its own pictures, and a background
 * or sprite that shows one is not moved, as the depth is in it. Where engine A is not drawn for the
 * right eye (it is on the bottom screen) the left eye's capture is taken over. What the CPU reads
 * of a capture is the left eye's.
 *
 * What is moved right leaves room at the right eye's left edge, which is black, as with the
 * software renderer (NdsDepth::EdgeLayers): its compositor and its 3D's clear plane end there
 * (nds_gl_edge.h).
 *
 * Only the engine on the top screen is drawn for the right eye, unless the bottom screen is to have
 * a right picture too. A frame without all of its right picture (the first one, or one whose
 * engines changed screens in it) has none.
 */
class StereoGl : public melonDS::GLRenderer {
public:
    explicit StereoGl(melonDS::NDS& nds);
    ~StereoGl() override;

    /// Between frames, for the next one. False: there is no second renderer to be had, and there
    /// will be none.
    bool SetDepth(const NdsDepth::Depth& depth, bool secondary_too = false);

    /// The last frame's right picture, as GetFramebuffers' of the left one. 0: none, the left one
    /// is for both eyes.
    GLuint Right(int screen) const;

    /// The farthest layer's shift in it, in pixels: a picture is drawn a frame after SetDepth.
    int RightShift() const {
        return shown_shift;
    }

    /// The second renderer is there
    bool HasRight() const {
        return right != nullptr;
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
    class RightEye;
    class RightEye2D;

    /// What of the game's memory has changed since a renderer looked. melonDS has this once, and
    /// whoever looks first takes it: this is the right eye's.
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

    bool MakeRight();
    void DropRight();
    template <typename F>
    void WithRight(F&& f);
    void CopyCaptures(const melonDS::GLRenderer& from, melonDS::GLRenderer& to, melonDS::u32 bank,
                      bool wide);
    void Forget();
    void StartScene();
    void BeginFrame();

    std::unique_ptr<RightEye> right; ///< none while off
    Unseen unseen{};
    melonDS::RendererSettings settings{};
    NdsDepth::Depth wanted{};
    bool wanted_secondary = false;
    NdsDepth::TopWatch watch{};
    bool begun = false;          ///< a frame, none of whose lines is drawn yet
    bool a_on_top = false;       ///< engine A, this frame
    std::array<bool, 2> draws{}; ///< engine A, engine B: for the right eye too, this frame
    /// The right eye's 3D that is there, if it is of the game's last frame of 3D: how it is moved,
    /// and where what is behind it ends.
    struct Drawn {
        NdsDepth::SceneShift shift{};
        int edge = 0;
        bool operator==(const Drawn&) const = default;
    };
    std::optional<Drawn> drawn{};
    int scene_offset = 0;    ///< this frame's 3D's
    int next_shift = 0;      ///< of this frame's farthest layer
    int ended_shift = 0;     ///< the one's before
    int shown_shift = 0;     ///< the last frame's
    bool next_scene = false; ///< the coming frame's 3D is drawn for the right eye
    bool scene = false;      ///< this frame's is
    int lines = -1;          ///< of the right eye's, this frame; < 0: not all will be
    bool complete = false;   ///< the frame that ends has all of them
    bool saving = false;
    std::array<bool, 2> ready{}; ///< the last frame's
};

} // namespace FrameCores
