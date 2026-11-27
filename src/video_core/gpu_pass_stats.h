// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame gpupasses: where the renderer's GPU time goes, by kind of work (the test switch
// AZAHARFRAME_GPU_PASSES=1, renderer_vulkan/vk_gpu_passes.h). The renderer's command buffers are
// cut into segments by timestamps written only outside render passes (inside one, a tiler such as
// Adreno runs the pass once per bin and a timestamp there is one bin's): before a render pass when
// other work came before it, after each render pass, and where the kind of work outside passes
// changes (copies, uploads, a readback, presentation). A segment's time is from the timestamp
// before it to the one after it (both at the bottom of the pipe: all earlier work done), so the
// segments of a command buffer add up to its time from its first timestamp to its last, the same
// span AZAHARFRAME_GPU_BUSY measures. A render pass's segment holds all of the pass on a tiler:
// binning, every bin's loads (LOAD), draws and stores (STORE), and the barrier after it.
//
// This file is the bookkeeping only (no Vulkan): the segment's description, recorded with its
// timestamp, and the sums over game frames that become the log lines every 10 s.

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>
#include "common/common_types.h"

namespace VideoCore::GpuPasses {

/// What a segment's time is counted as.
enum class Bucket : u8 {
    Top3D,     ///< the game's render passes into the top screen with world (3D) draws in them
    Top2D,     ///< the game's render passes into the top screen with 2D draws only
    Bottom,    ///< the game's render passes into the bottom screen
    Offscreen, ///< the game's render passes into anything else (render to texture, shadows)
    Copy,      ///< rasterizer cache copies, blits, clears, scales and format conversions
    Upload,    ///< texture uploads and decodes (their upscale filter and mip levels too)
    Readback,  ///< a surface read back to the CPU (the CPU waits for the GPU there)
    Present,   ///< the screens drawn into the XR swapchains or the window
    Other,     ///< anything not marked: barriers, the foveation rate image, a command buffer's end
    Count,
};
constexpr std::size_t kBuckets = static_cast<std::size_t>(Bucket::Count);

/// Why a render pass ended, or what kind of work comes next outside passes.
enum class Why : u8 {
    None,
    Target,    ///< the next draw has another framebuffer
    Area,      ///< the same framebuffer, another render area (or a clear)
    Copy,      ///< a copy between surfaces
    Blit,      ///< a blit (scaled copy)
    Clear,     ///< a surface cleared outside a pass
    Convert,   ///< a format conversion (depth to colour, a depth blit by shader)
    Scale,     ///< a surface's scaled copy made or refreshed
    Upload,    ///< a texture upload
    Mipmap,    ///< mip levels made
    Readback,  ///< a surface read back to the CPU
    Present,   ///< the screens drawn to the XR swapchains or the window
    Submit,    ///< the command buffer was submitted
    Foveation, ///< the foveation rate image updated
    Other,
    Count,
};
constexpr std::size_t kWhys = static_cast<std::size_t>(Why::Count);

/// The bucket of the work that `why` announces outside passes; Bucket::Count when `why` is not
/// work of its own (a target change, a submit).
constexpr Bucket BucketOf(Why why) {
    switch (why) {
    case Why::Copy:
    case Why::Blit:
    case Why::Clear:
    case Why::Convert:
    case Why::Scale:
        return Bucket::Copy;
    case Why::Upload:
    case Why::Mipmap:
        return Bucket::Upload;
    case Why::Readback:
        return Bucket::Readback;
    case Why::Present:
        return Bucket::Present;
    case Why::Foveation:
    case Why::Other:
        return Bucket::Other;
    default:
        return Bucket::Count;
    }
}

constexpr bool IsPassBucket(Bucket b) {
    return b == Bucket::Top3D || b == Bucket::Top2D || b == Bucket::Bottom ||
           b == Bucket::Offscreen;
}

const char* Name(Bucket bucket);
const char* Name(Why why);

/// Which screen a game draw goes to, from its viewport (the 3DS's own size, unscaled).
enum class Screen : u8 { None, Top, Bottom };
Screen ScreenOf(u32 viewport_width, u32 viewport_height);

/// The bucket of one of the game's render passes, from what was drawn in it. `screen` is the
/// screen of its draws, or of its target's size when it had none.
Bucket GameBucket(bool shadow, Screen screen, u32 world_draws);

constexpr u8 kNoFormat = 0xFF;

/// One segment: a render pass, or a run of work outside passes.
struct Segment {
    u64 frame = 0;        ///< the game frame it was recorded in
    Bucket bucket = Bucket::Other;
    bool pass = false;    ///< a render pass (else work outside passes)
    bool scaled = false;  ///< pass: its target is at the internal resolution (scale above 1)
    bool foveated = false;///< pass: it has the eye picture's shading rate image
    bool load = false;    ///< pass: its attachments are loaded (LOAD) at the start
    bool clear = false;   ///< pass: its attachments are cleared (CLEAR) at the start
    bool store = false;   ///< pass: its attachments are stored (STORE) at the end
    Why end = Why::None;  ///< pass: why it ended
    Why reopen = Why::None; ///< pass: it loads a target an earlier pass this frame stored and
                            ///< ended (why that pass ended); None: the target's first pass
    u8 color = kNoFormat; ///< pass: its colour attachment's PixelFormat
    u8 depth = kNoFormat; ///< pass: its depth attachment's PixelFormat
    u8 index = 0;         ///< pass: its number among this frame's passes on its target (0: first)
    u8 attachments = 0;   ///< pass: colour and depth attachments (0..2)
    u32 width = 0;        ///< pass: the target's size (scaled pixels)
    u32 height = 0;
    u32 area = 0;         ///< pass: pixels in its render area
    u32 draws = 0;        ///< pass: draws
    u32 flat = 0;         ///< pass: of them 2D (not through the world camera, not depth tested)
    u32 ops = 0;          ///< outside passes: operations in it
    u64 target = 0;       ///< pass: its target's identity (for the targets a frame)
};

/// The sums of the segments over game frames, and the log lines. Segments come in the order
/// they were recorded (frame numbers never go down); a frame is complete when one of a later
/// frame comes.
class Aggregator {
public:
    /// One segment's measured time.
    void Add(const Segment& s, double ms);
    /// Command buffers (or their ends) not timed: the query slots were all in use.
    void Untimed(u32 buffers) {
        untimed += buffers;
    }
    /// Complete frames in this window.
    u32 Frames() const {
        return static_cast<u32>(totals.size());
    }
    /// The window's lines (without a log prefix), then a new window. `seconds`: its length.
    std::vector<std::string> Report(double seconds);

    static constexpr std::size_t kMostKeys = 256; ///< distinct passes kept for the top 5

private:
    struct Sums {
        std::array<double, kBuckets> ms{};
        std::array<double, kBuckets> passes{};
        std::array<double, kBuckets> draws{};
        std::array<double, kBuckets> flat{};
        std::array<double, kBuckets> ops{};
        std::array<double, kBuckets> loads{};
        std::array<double, kBuckets> clears{};
        std::array<double, kBuckets> stores{};
        std::array<double, kBuckets> mpx{};
        std::array<double, kBuckets> mbytes{};
        std::array<double, kBuckets> foveated{};
        std::array<double, kBuckets> targets{};
        std::array<double, kWhys> ended{};
        std::array<double, kWhys> reopened{};
        double scaled_ms = 0, scaled_mpx = 0, unscaled_ms = 0, unscaled_mpx = 0, outside_ms = 0;
        double marks = 0;
        void AddTo(Sums& into) const;
    };
    void CloseFrame();

    using Key = std::tuple<u8, u32, u32, u8, u8, u8>; ///< bucket, size, formats, index
    struct Top {
        double ms = 0;
        double n = 0;
        double draws = 0;
    };

    bool open = false;
    u64 frame = 0;
    Sums cur;
    std::array<std::vector<u64>, kBuckets> cur_targets;
    Sums window;
    std::array<std::vector<float>, kBuckets> per_frame;
    std::vector<float> totals;
    std::map<Key, Top> top;
    u32 top_dropped = 0;
    u32 untimed = 0;
};

/// The traffic estimate of a pass's loads and stores, in bytes: its render area times the
/// bytes a pixel of its attachments (the 3DS formats' sizes; the host format may be larger).
double LoadStoreBytes(const Segment& s);

} // namespace VideoCore::GpuPasses
