// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <vector>

#include "GPU_Soft.h"

#include "frame_cores/nds_depth.h"
#include "frame_cores/nds_stereo_3d.h"

namespace FrameCores {

/**
 * Layer Depth for DS games: melonDS's software renderer, with one more for each eye that draws
 * every line again with the backgrounds and sprites moved by their priority, half for the one eye
 * and half for the other (NdsDepth::EyeShift). The game and its own picture never see it. Only an
 * engine whose screen has depth (NdsDepth::Screens, ShownDepth) is drawn again.
 *
 * 3D is drawn again for the eyes from the game's polygons (nds_stereo_3d.h). Lines without it (the
 * frame after Layer Depth is turned on or a state is loaded) are the game's. Display capture is of
 * the game's own picture alone. A game with 3D on both screens (engine A on another screen every
 * frame, the captured picture on the other) keeps the pictures of engine A's frame for the frame
 * after it (nds_depth.h, KeepsRightPicture).
 */
class StereoSoft final : public melonDS::SoftRenderer {
public:
    explicit StereoSoft(melonDS::NDS& nds);
    ~StereoSoft() override;

    /// From the next frame. Not while a frame runs.
    void SetDepth(const NdsDepth::Depth& depth);

    /// The eye's last frame of a screen (0 the top one, 1 the bottom one), as GetFramebuffers gives
    /// the game's: 256x192, 32-bit BGRA with alpha set, until the next RunFrame. None, of either
    /// eye: the game's is for both.
    const melonDS::u32* Picture(NdsDepth::Eye eye, int screen) const;
    /// The screen's picture is one for each eye, also in a frame without them: in the depth's
    /// `screens`, and the bottom one has had something to see.
    bool ShowsDepth(int screen) const;
    /// The farthest layer's shift between the Pictures, in pixels: they are drawn a frame after
    /// SetDepth, or kept for one more.
    int Shift(int screen) const;

    NdsDepth::Lines LastLines() const;

    void Reset() override;
    void Stop() override;
    void SetRenderSettings(melonDS::RendererSettings& settings) override;
    void PreSavestate() override;
    void PostSavestate() override;
    void Start3DRendering() override;
    void Restart3DRendering() override;
    void DrawScanline(melonDS::u32 line) override;
    void DrawSprites(melonDS::u32 line) override;
    void SwapBuffers() override;

private:
    class EyeRenderer;

    /// A screen's two pictures
    struct Screen {
        std::array<std::vector<melonDS::u32>, 2> pictures; ///< by Eye
        int shift = 0;                                     ///< the farthest layer's
        bool ready = false;
        /// The game's picture of the frame `pictures` are of, if engine A was on this screen in it
        std::vector<melonDS::u32> scene_own;
        /// The last frame with engine B on this screen kept the picture (3D on both screens)
        bool kept = false;
    };

    template <typename R>
    static melonDS::u32* BackRow(R& renderer, int screen, melonDS::u32 line);

    void Forget();
    void EndFrame();
    void BeginFrame();
    void DrawEyesSprites(melonDS::u32 line);
    EyeRenderer& Of(NdsDepth::Eye eye) const {
        return *eyes[static_cast<std::size_t>(eye)];
    }
    /// The eyes' renderers are there
    bool HasEyes() const {
        return eyes[0] != nullptr;
    }

    std::array<std::unique_ptr<EyeRenderer>, 2> eyes; ///< by Eye; none while off
    NdsDepth::Depth wanted{};
    NdsDepth::Depth depth{};  ///< this frame's
    bool threaded_3d = false; ///< the game's 3D
    NdsDepth::TopWatch watch{};
    std::array<Screen, 2> screens{};
    ShownDepth shown_depth{}; ///< this frame's
    NdsDepth::Lines lines{};  ///< this frame's
    NdsDepth::Lines last_lines{};
    bool drawing = false;        ///< the eyes' lines, this frame
    std::array<bool, 2> draws{}; ///< engine A, engine B: drawn again this frame
    bool a_on_top = false;       ///< engine A, this frame
    bool complete = false;       ///< the frame that ends has all the eyes' lines
};

} // namespace FrameCores
