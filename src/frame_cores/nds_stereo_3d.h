// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "frame_cores/nds_stereo_renderer.h"

namespace FrameCores::NdsDepth {

/// Vertex::Position[3] is 20.12 fixed point, cut to 24 bits by the DS: 1.0 is 0x1000, and the most
/// is just under 4096.0.
constexpr int32_t kOneW = 0x1000;

/// x on the screen is 9 bits. Past 256 the rasteriser drops pixels; below 0 it would write into
/// the row above.
constexpr int32_t kMaxX = 0x1FF;

struct Depth3D {
    int strength;        ///< pixels to the right for what is far away; 0: no second 3D picture
    int32_t convergence; ///< the w that sits on the screen, 20.12
    int limit;           ///< the most a vertex moves, either way, in pixels
    constexpr bool operator==(const Depth3D&) const = default;
};

/// Per level until games have their own. What is far away is as deep as the backmost 2D layer, so
/// a 2D sky and 3D hills meet. Convergence 4.0 is near in the games measured (w 2.5 to 1024):
/// nearly all of the picture is behind the screen. Not tried in a headset.
///   level 1: 3 pixels, level 2: 6, level 3: 9
constexpr Depth3D Depth3DForLevel(int level) {
    const int far_pixels = LayerShift(level, 3);
    return {far_pixels, 4 * kOneW, far_pixels};
}

/// How many pixels to the right a vertex is in the right eye: strength * (1 - convergence / w),
/// rounded, within the limit. Never less for a larger w.
constexpr int VertexShift(const Depth3D& depth, int32_t w) {
    if (depth.strength == 0 || w <= 0) {
        return 0;
    }
    const int64_t pixels_w = int64_t{depth.strength} * (int64_t{w} - depth.convergence);
    const int64_t half = pixels_w < 0 ? -(w / 2) : w / 2;
    return static_cast<int>(
        std::clamp<int64_t>((pixels_w + half) / w, -depth.limit, depth.limit));
}

constexpr int32_t ShiftVertexX(int32_t x, int shift) {
    return std::clamp<int32_t>(x + shift, 0, kMaxX);
}

/// The whole polygon has w 1: orthographic, so drawn on the screen, not in the scene. Its vertices
/// do not move. One w for all of them is not enough: what faces the camera in the scene (a
/// billboard, a wall) has that too.
constexpr bool IsOnScreen(const int32_t* w, std::size_t count) {
    return std::all_of(w, w + count, [](int32_t value) { return value == kOneW; });
}

} // namespace FrameCores::NdsDepth

// As in nds_stereo_renderer.h
#if __has_include("GPU_Soft.h")

#include <array>
#include <memory>

#include "GPU3D.h"
#include "GPU3D_Soft.h"

namespace FrameCores {

/**
 * The right eye's 3D for the software renderer: the game's polygons of the frame, copied with
 * their vertices moved, in a GPU3D of ours (it registers nothing and runs no commands; it is the
 * storage and the registers a SoftRenderer3D reads), drawn by a SoftRenderer3D of its own.
 *
 * Only Start makes a frame. Until the first one, and after Stop, nothing is allocated and there
 * are no lines. All on the emulation thread.
 *
 * Threaded as the left eye's renderer is, so that a second 3D frame is not added to the emulation
 * thread. The render thread hands out 192 lines a frame by a semaphore: every line is taken or
 * given back before the next frame, and none is asked for that will not come.
 */
class NdsRight3D final : public melonDS::Renderer3D {
public:
    /// `game`: the game's GPU3D, only read.
    NdsRight3D(melonDS::GPU3D& game, melonDS::SoftRenderer& parent);
    ~NdsRight3D() override;

    /// Copies the frame and draws it, after the left eye's renderer has started on it: that one
    /// brings the texture memory up to date and uses up its dirty bits. The polygons, vertices and
    /// Render registers copied only change in GPU3D::VBlank (line 192, this same thread); the left
    /// eye's thread only reads them.
    void Start(const NdsDepth::Depth3D& depth, bool threaded);
    /// Until the frame is drawn. Before anything that writes texture memory.
    void Wait();
    /// Forgets the frame: no lines until the next Start.
    void Drop();
    /// Frees everything.
    void Stop();

    /// Whether GetLine has this frame's 3D.
    bool HasLine() const;

    void Reset() override;
    void RenderFrame() override {} // Start, with the depth
    /// Scrolled as the left eye's line is. Empty without HasLine.
    melonDS::u32* GetLine(int line) override;

private:
    static constexpr int kLines = 192;
    static constexpr std::size_t kVertices = 6144 * 2;
    static constexpr std::size_t kPolygons = 2048;
    static constexpr std::size_t kMaxPolygonVertices = 10;

    /// Where a vertex of the game is in the scene, if it was copied in this frame
    struct Slot {
        melonDS::u32 frame = 0;
        melonDS::u32 index = 0;
    };

    void Create(bool threaded);
    void FillScene(const NdsDepth::Depth3D& depth);
    bool CopyPolygon(const melonDS::Polygon& from, melonDS::Polygon& to,
                     const NdsDepth::Depth3D& depth);

    melonDS::SoftRenderer& owner;
    std::unique_ptr<melonDS::GPU3D> scene; ///< outlives `soft`, whose thread reads it
    std::unique_ptr<melonDS::SoftRenderer3D> soft;

    bool running = false; ///< a frame was started and not yet waited for
    int taken = kLines;   ///< lines of the frame

    /// Strips share vertices: each is copied once moved and, for polygons that stay, once not
    std::array<std::array<Slot, 2>, kVertices> slots{};
    melonDS::u32 frame = 0;
    std::size_t vertex_count = 0;

    std::array<melonDS::u32, 256> empty{};
};

} // namespace FrameCores

#endif
