// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame gpupasses: the renderer's GPU time by kind of work (a test switch,
// AZAHARFRAME_GPU_PASSES=1; off: no query pool, nothing recorded). The RenderManager describes
// each segment as it is recorded (vk_render_manager.h) and records a mark after it; on the
// scheduler's worker thread the mark becomes a timestamp (bottom of the pipe, never inside a render
// pass: on a tiler a timestamp in a pass is one bin's), each command buffer gets one at its start
// and one at its end, and the timestamps are read back once its tick has signalled (never waited
// for), as GpuBusyTimer does. Every 10 s the sums go to the log (video_core/gpu_pass_stats.h,
// lines "gpu passes: ...").

#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <vector>
#include "common/common_types.h"
#include "video_core/gpu_pass_stats.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

class Instance;
class MasterSemaphore;

class GpuPassTimer {
public:
    explicit GpuPassTimer(const Instance& instance);
    ~GpuPassTimer();

    GpuPassTimer(const GpuPassTimer&) = delete;
    GpuPassTimer& operator=(const GpuPassTimer&) = delete;

    /// The renderer asks for it (once): true when the switch is on and the queue has
    /// timestamps; the query pool is made then (not for the schedulers of other cores).
    bool Enable();

    // On the worker thread:
    /// Right after `cmdbuf` began (outside a render pass): its first timestamp.
    void BeginBuffer(vk::CommandBuffer cmdbuf);
    /// A segment ends here (outside a render pass).
    void Mark(vk::CommandBuffer cmdbuf, const VideoCore::GpuPasses::Segment& segment);
    /// Before `cmdbuf` ends, with the tick its submit signals: its last timestamp.
    void EndBuffer(vk::CommandBuffer cmdbuf, u64 tick);
    /// The buffers whose tick has signalled, read back (no wait); the log lines every 10 s.
    void Collect(const MasterSemaphore& master);

    static constexpr u32 kSlots = 4096;  ///< timestamps in flight (the query pool's size)
    static constexpr u32 kKeepFree = 512; ///< a command buffer is timed only with this many free

private:
    struct Buffer {
        u32 first = 0; ///< its first query slot
        u32 count = 0; ///< timestamps written (slots first.. first + count - 1, wrapping)
        u64 tick = 0;
        bool closed = false;  ///< its last timestamp written
        bool partial = false; ///< the slots ran out in it: its later segments were not timed
        std::vector<VideoCore::GpuPasses::Segment> segments; ///< segment i ends at timestamp i+1
    };
    bool Write(vk::CommandBuffer cmdbuf, vk::PipelineStageFlagBits stage);

    vk::Device device;
    vk::QueryPool pool;
    std::atomic<bool> enabled{false};
    bool wanted = false;
    u32 valid_bits = 0;
    double ns_per_tick = 1.0;
    u64 mask = ~u64{0};
    u32 next = 0; ///< the next free slot
    u32 used = 0; ///< slots written and not read back yet
    bool open = false; ///< the command buffer being recorded is timed
    u64 last_frame = 0;
    std::deque<Buffer> pending;
    VideoCore::GpuPasses::Aggregator aggregator;
    std::chrono::steady_clock::time_point window_start{};
    u32 untimed = 0;
};

} // namespace Vulkan
