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

namespace FrameCores {

/**
 * Layer Depth for DS games: melonDS's software renderer, with a second one that draws every line
 * again with the backgrounds and sprites moved by their priority, for the right eye. The game and
 * the left picture never see it. Only the engine on the top screen is drawn again, unless the
 * bottom screen is to have a right picture too.
 *
 * 3D is drawn again for the right eye from the game's polygons (nds_stereo_3d.h). Lines without
 * it (the frame after Layer Depth is turned on or a state is loaded) are the left eye's. Display
 * capture is the left eye's alone. A game with 3D on both screens (engine A on another screen
 * every frame, the captured picture on the other) keeps the right picture of engine A's frame for
 * the frame after it (nds_depth.h, KeepsRightPicture).
 */
class StereoSoft final : public melonDS::SoftRenderer {
public:
    explicit StereoSoft(melonDS::NDS& nds);
    ~StereoSoft() override;

    /// From the next frame. Not while a frame runs. `secondary_too`: the bottom screen has a right
    /// picture as well (for a host that can show one; twice the 2D drawing otherwise saved).
    void SetDepth(const NdsDepth::Depth& depth, bool secondary_too = false);

    /// The right eye's last frame of a screen (0 the top one, 1 the bottom one), as
    /// GetFramebuffers gives the left one's: 256x192, 32-bit BGRA with alpha set, until the next
    /// RunFrame. None: the left eye's is for both.
    const melonDS::u32* Right(int screen) const;

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
    class RightEye;

    /// A screen's right picture
    struct Screen {
        std::vector<melonDS::u32> picture;
        bool ready = false;
        /// The left picture of the frame `picture` is of, if engine A was on this screen in it
        std::vector<melonDS::u32> scene_left;
        /// The last frame with engine B on this screen kept the picture (3D on both screens)
        bool kept = false;
    };

    template <typename R>
    static melonDS::u32* BackRow(R& renderer, int screen, melonDS::u32 line);

    void Forget();
    void EndFrame();
    void BeginFrame();
    void DrawRightSprites(melonDS::u32 line);

    std::unique_ptr<RightEye> right; ///< none while off
    NdsDepth::Depth wanted{};
    bool wanted_secondary = false;
    NdsDepth::Depth depth{};  ///< this frame's
    bool secondary = false;   ///< this frame's
    bool threaded_3d = false; ///< the left eye's 3D
    NdsDepth::TopWatch watch{};
    std::array<Screen, 2> screens{};
    NdsDepth::Lines lines{}; ///< this frame's
    NdsDepth::Lines last_lines{};
    bool drawing = false;        ///< the right eye's lines, this frame
    std::array<bool, 2> draws{}; ///< engine A, engine B: drawn again this frame
    bool a_on_top = false;       ///< engine A, this frame
    bool complete = false;       ///< the frame that ends has all the right eye's lines
};

} // namespace FrameCores
