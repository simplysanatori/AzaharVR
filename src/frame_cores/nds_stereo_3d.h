// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <memory>

#include "GPU3D.h"
#include "GPU3D_Soft.h"

#include "frame_cores/nds_depth.h"

namespace FrameCores {

/// Which screens' pictures are one for each eye, for the renderers of both. The top one's always is
/// where it has depth at all. The bottom one's is from the first frame on with something to see:
/// the headset's pictures change their layout with it, so it stays.
class ShownDepth {
public:
    /// At the start of a frame. Whether engine A's and engine B's screens are. `captures`: as
    /// NdsDepth::HasDepth.
    std::array<bool, 2> Note(const NdsDepth::Depth& depth, const melonDS::GPU& gpu, bool a_on_top,
                             bool captures);
    bool operator[](int screen) const {
        return shows[static_cast<std::size_t>(screen)];
    }

private:
    std::array<bool, 2> shows{};
};

/**
 * An eye's 3D for the software renderer: the game's polygons of the frame, copied with
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
class NdsEye3D final : public melonDS::Renderer3D {
public:
    /// `game`: the game's GPU3D, only read.
    NdsEye3D(melonDS::GPU3D& game, melonDS::SoftRenderer& parent, NdsDepth::Eye eye);
    /// Only Prepare and Scene, for a renderer of another kind; of the rest, Cut and NoScene.
    NdsEye3D(melonDS::GPU3D& game, NdsDepth::Eye eye);
    ~NdsEye3D() override;

    /// Copies the frame and draws it, after the left eye's renderer has started on it: that one
    /// brings the texture memory up to date and uses up its dirty bits. The polygons, vertices and
    /// Render registers copied only change in GPU3D::VBlank (line 192, this same thread); the left
    /// eye's thread only reads them.
    /// `screen`: the one the scene is for, whose view it follows (NdsDepth::ViewTrack), unless
    /// `depth` has a fixed convergence.
    /// `same`: the other eye's, which has started on the same frame. Its view is this one's.
    void Start(const NdsDepth::Depth& depth, int screen, bool threaded,
               const NdsEye3D* same = nullptr);
    /// The frame copied, moved by the eye's part of what it returns, into Scene. Start does it by
    /// itself.
    NdsDepth::SceneShift Prepare(const NdsDepth::Depth& depth, int screen,
                                 const NdsEye3D* same = nullptr);
    /// The eye's GPU3D.
    melonDS::GPU3D& Scene();
    /// How far right of their places the last one's polygons are (NdsDepth::SceneOffset): its
    /// lines are scrolled by as much.
    int Offset() const {
        return offset;
    }
    /// The columns of a line of it that what is behind it is in, scrolled by `scroll`
    /// (ScrollOf).
    NdsDepth::SceneEnds Ends(int scroll) const {
        return NdsDepth::MakeSceneEnds(far_shift, scroll, left_end);
    }
    /// The game's next scene has nothing to do with the last one.
    void Cut();
    /// Engine A shows no 3D in the frame, which is for this screen.
    void NoScene(int screen);
    /// Of the last Start; 0 before any scene.
    melonDS::s32 Convergence() const {
        return convergence;
    }
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

    /// Where a vertex of the game is in the scene, if it was copied in this frame
    struct Slot {
        melonDS::u32 frame = 0;
        melonDS::u32 index = 0;
        int shift = 0;
        int fine = 0;
    };

    void Create(bool threaded);
    NdsDepth::SceneView Measure();
    NdsDepth::SceneView Follow(const NdsDepth::Depth& depth, int screen);
    melonDS::u32 ClearPixel(int x, int line) const;
    void EndAtEdge(melonDS::u32* row, int line) const;
    NdsDepth::PolygonPlace Place(const melonDS::Polygon& polygon) const;
    void FillScene(const NdsDepth::SceneShift& shift);
    bool CopyPolygon(const melonDS::Polygon& from, melonDS::Polygon& to,
                     const NdsDepth::SceneShift& shift);

    melonDS::SoftRenderer* owner = nullptr;
    const NdsDepth::Eye eye;
    std::unique_ptr<melonDS::GPU3D> scene; ///< outlives `soft`, whose thread reads it
    std::unique_ptr<melonDS::SoftRenderer3D> soft;

    std::array<NdsDepth::ViewTrack, 2> tracks{}; ///< of the top and the bottom screen
    NdsDepth::DepthLine line{};                  ///< this frame's
    NdsDepth::SceneView view{};                  ///< and the view it is drawn with
    melonDS::s32 convergence = 0;

    bool running = false; ///< a frame was started and not yet waited for
    int taken = kLines;   ///< lines of the frame
    int offset = 0;       ///< how far right of their places they are drawn (NdsDepth::SceneOffset)
    int far_shift = 0;    ///< of what is behind the scene
    bool left_end = true; ///< where that ends (NdsDepth::MakeSceneEnds)

    /// Strips share vertices: each is copied once by its own w and once by its polygon's place
    std::array<std::array<Slot, 2>, kVertices> slots{};
    melonDS::u32 frame = 0;
    std::size_t vertex_count = 0;

    std::array<melonDS::u32, 256> empty{};
};

} // namespace FrameCores
