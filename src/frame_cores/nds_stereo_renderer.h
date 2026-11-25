// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>

#include "GPU_Soft.h"

#include "frame_cores/nds_depth.h"

namespace FrameCores {

/**
 * Layer Depth for DS games: melonDS's software renderer, with a second one that draws every line
 * again with the backgrounds and sprites moved by their priority, for the right eye. The game and
 * the left picture never see it. Only the engine on the top screen is drawn again.
 *
 * 3D is drawn again for the right eye from the game's polygons (nds_stereo_3d.h). Lines without
 * it (the frame after Layer Depth is turned on or a state is loaded) are the left eye's. Frames with
 * display capture (3D on both screens), and the second after them, have no right picture: captures
 * would alternate between moved and not.
 */
class StereoSoft final : public melonDS::SoftRenderer {
public:
    explicit StereoSoft(melonDS::NDS& nds);
    ~StereoSoft() override;

    /// From the next frame. Not while a frame runs.
    void SetDepth(const NdsDepth::Depth& depth);

    /// The right eye's last frame of the top screen, as GetFramebuffers gives the left one's:
    /// 256x192, 32-bit BGRA with alpha set, until the next RunFrame. None: the left eye's is for
    /// both.
    const melonDS::u32* RightTop();

    NdsDepth::Lines LastLines() const {
        return right_ready ? last_lines : NdsDepth::Lines{};
    }

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

    template <typename R>
    static melonDS::u32* BackTopRow(R& renderer, melonDS::u32 line);

    void BeginFrame();
    void DrawRightSprites(melonDS::u32 line);

    std::unique_ptr<RightEye> right; ///< none while off
    NdsDepth::Depth wanted{};
    NdsDepth::Depth depth{};  ///< this frame's
    bool threaded_3d = false; ///< the left eye's 3D
    int capture_hold = 0;     ///< frames left without a right picture
    NdsDepth::Lines lines{};  ///< this frame's
    NdsDepth::Lines last_lines{};
    bool drawing = false;     ///< the right eye's lines, this frame
    bool a_on_top = false;    ///< engine A, this frame
    bool right_ready = false; ///< the last frame has all its lines
};

} // namespace FrameCores
