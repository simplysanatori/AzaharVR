// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>

namespace FrameCores::NdsDepth {

constexpr int kMaxLevel = 3;

/// How many pixels to the right a layer is in the right eye. Priority 0 (text, HUD) stays at the
/// screen; nothing comes in front of it.
constexpr int LayerShift(int level, int priority) {
    return std::clamp(level, 0, kMaxLevel) * (priority & 3);
}

/// BGxHOFS of a text background moved right; the hardware wraps it.
constexpr uint16_t ShiftScroll(uint16_t scroll, int shift) {
    return static_cast<uint16_t>(scroll - shift);
}

struct AffinePoint {
    int32_t x;
    int32_t y;
    constexpr bool operator==(const AffinePoint&) const = default;
};

/// The line's reference point of a rotated or scaled background moved right on the screen. One
/// pixel to the right is (RotA, RotC) in the background, so both coordinates change.
constexpr AffinePoint ShiftAffine(AffinePoint ref, int16_t rot_a, int16_t rot_c, int shift) {
    return {ref.x - shift * rot_a, ref.y - shift * rot_c};
}

/// A sprite's second attribute with its x moved by its own priority. x is 9 bits: past the right
/// edge it wraps to -256 and up, and no sprite is wide enough (128 at most) to reach the screen
/// again from there. Window sprites stay, as the windows do.
constexpr uint16_t ShiftSprite(uint16_t attr0, uint16_t attr1, uint16_t attr2, int level) {
    const bool disabled = ((attr0 >> 8) & 3) == 2;
    const bool window = ((attr0 >> 10) & 3) == 2;
    if (disabled || window) {
        return attr1;
    }
    const int shift = LayerShift(level, (attr2 >> 10) & 3);
    return static_cast<uint16_t>((attr1 & ~0x1FF) | ((attr1 + shift) & 0x1FF));
}

} // namespace FrameCores::NdsDepth

// The tests use the functions above and have no melonDS headers
#if __has_include("GPU_Soft.h")

#include "GPU_Soft.h"

namespace FrameCores {

/**
 * Layer Depth for DS games: melonDS's software renderer, with a second one that draws every line
 * again with the backgrounds and sprites moved by their priority, for the right eye. The game and
 * the left picture never see it. At level 0 there is no second renderer.
 *
 * 3D is drawn again for the right eye from the game's polygons (nds_stereo_3d.h). Lines without
 * it (3D off, the frame after a change of level or a state load) are the left eye's. Frames with
 * display capture (3D on both screens), and the second after them, have no right picture: captures
 * would alternate between moved and not.
 *
 * Not wired in. That needs:
 * - NdsCore::Load: nds->SetRenderer(std::make_unique<StereoSoft>(*nds)) after making the NDS or
 *   DSi and before SetRenderSettings. The GPU owns it; NdsCore keeps a plain pointer for as long
 *   as `nds` lives.
 * - Threaded 3D stays as it is: the right eye's 3D is threaded when the left eye's is.
 * - CoreBootOptions::layer_depth and the live setting to SetDepth, under the core's mutex.
 * - RunFrame: GetRightFramebuffers after GetFramebuffers; the right top picture to
 *   FrameOutput::primary_right when it returns true.
 * - Nothing for capture: it is seen here, line by line.
 */
class StereoSoft final : public melonDS::SoftRenderer {
public:
    explicit StereoSoft(melonDS::NDS& nds);
    ~StereoSoft() override;

    /// 0 off, 1 to 3. From the next frame. Not while a frame runs.
    void SetDepth(int level);
    /// Off: 3D is flat, and no second 3D frame is drawn. As SetDepth.
    void SetDepth3D(bool on);

    /// The right eye's last frame, as GetFramebuffers gives the left one's: top and bottom
    /// (either engine, by the screen swap), 256x192, 32-bit BGRA with alpha set, until the next
    /// RunFrame. False: there is none, and these are the left pictures.
    bool GetRightFramebuffers(void** top, void** bottom);

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
    static melonDS::u32* BackRow(R& renderer, int screen, melonDS::u32 line);

    void BeginFrame();
    void DrawRightSprites(melonDS::u32 line);

    std::unique_ptr<RightEye> right; ///< none at level 0
    int wanted_level = 0;
    bool wanted_3d = true;
    bool threaded_3d = false; ///< the left eye's 3D
    int level = 0;
    int capture_hold = 0;     ///< frames left without a right picture
    int drawn_lines = 0;      ///< the right eye's, this frame
    bool drawing = false;     ///< the right eye's lines, this frame
    bool right_ready = false; ///< the last frame has all its lines
};

} // namespace FrameCores

#endif
