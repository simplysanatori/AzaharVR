// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include "common/logging/log.h"
#include "video_core/renderer_vulkan/vk_gpu_passes.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_master_semaphore.h"

namespace Vulkan {

namespace GP = VideoCore::GpuPasses;

GpuPassTimer::GpuPassTimer(const Instance& instance) {
    // Off by default: AZAHARFRAME_GPU_PASSES=1 turns it on (read once, at the start; the
    // test-switch file's value is in the environment by then).
    if (const char* env = std::getenv("AZAHARFRAME_GPU_PASSES"); !env || std::strcmp(env, "1") != 0) {
        return;
    }
    wanted = true;
    const vk::PhysicalDevice physical = instance.GetPhysicalDevice();
    const auto families = physical.getQueueFamilyProperties();
    const u32 family = instance.GetGraphicsQueueFamilyIndex();
    valid_bits = family < families.size() ? families[family].timestampValidBits : 0;
    const float period = physical.getProperties().limits.timestampPeriod;
    if (valid_bits == 0 || period <= 0.0f) {
        LOG_INFO(Render_Vulkan, "gpu passes: the queue has no timestamps, not measured");
        wanted = false;
        return;
    }
    device = instance.GetDevice();
    ns_per_tick = period;
    mask = valid_bits >= 64 ? ~u64{0} : (u64{1} << valid_bits) - 1;
}

GpuPassTimer::~GpuPassTimer() {
    if (pool) {
        device.destroyQueryPool(pool);
    }
}

bool GpuPassTimer::Enable() {
    if (!wanted) {
        return false;
    }
    if (enabled.load(std::memory_order_acquire)) {
        return true;
    }
    try {
        pool = device.createQueryPool({
            .queryType = vk::QueryType::eTimestamp,
            .queryCount = kSlots,
        });
    } catch (const vk::SystemError&) {
        pool = vk::QueryPool{};
        wanted = false;
        LOG_INFO(Render_Vulkan, "gpu passes: no query pool, not measured");
        return false;
    }
    LOG_INFO(Render_Vulkan,
             "gpu passes: on (AZAHARFRAME_GPU_PASSES=1): timestamps of {} valid bits, {:.3f} ns a "
             "tick, {} query slots; written outside render passes only, at the bottom of the "
             "pipe; the lines every 10 s",
             valid_bits, ns_per_tick, kSlots);
    enabled.store(true, std::memory_order_release);
    return true;
}

bool GpuPassTimer::Write(vk::CommandBuffer cmdbuf, vk::PipelineStageFlagBits stage) {
    if (used >= kSlots) {
        return false;
    }
    const u32 slot = next;
    next = (next + 1) % kSlots;
    ++used;
    cmdbuf.resetQueryPool(pool, slot, 1);
    cmdbuf.writeTimestamp(stage, pool, slot);
    return true;
}

void GpuPassTimer::BeginBuffer(vk::CommandBuffer cmdbuf) {
    open = false;
    if (!enabled.load(std::memory_order_acquire)) {
        return;
    }
    if (kSlots - used < kKeepFree) {
        ++untimed; // every slot near the end still on the GPU: this one goes untimed
        return;
    }
    Buffer buffer;
    buffer.first = next;
    Write(cmdbuf, vk::PipelineStageFlagBits::eTopOfPipe);
    buffer.count = 1;
    buffer.segments.reserve(64);
    pending.push_back(std::move(buffer));
    open = true;
}

void GpuPassTimer::Mark(vk::CommandBuffer cmdbuf, const GP::Segment& segment) {
    last_frame = segment.frame;
    if (!open) {
        return;
    }
    Buffer& buffer = pending.back();
    if (buffer.partial || !Write(cmdbuf, vk::PipelineStageFlagBits::eBottomOfPipe)) {
        buffer.partial = true;
        return;
    }
    ++buffer.count;
    buffer.segments.push_back(segment);
}

void GpuPassTimer::EndBuffer(vk::CommandBuffer cmdbuf, u64 tick) {
    if (!open) {
        return;
    }
    Buffer& buffer = pending.back();
    if (!buffer.partial && Write(cmdbuf, vk::PipelineStageFlagBits::eBottomOfPipe)) {
        GP::Segment tail; // what came after the last mark: barriers, the end of the buffer
        tail.frame = last_frame;
        tail.bucket = GP::Bucket::Other;
        ++buffer.count;
        buffer.segments.push_back(tail);
    } else {
        buffer.partial = true;
    }
    buffer.tick = tick;
    buffer.closed = true;
    open = false;
}

void GpuPassTimer::Collect(const MasterSemaphore& master) {
    if (!enabled.load(std::memory_order_acquire)) {
        return;
    }
    std::vector<u64> t;
    while (!pending.empty() && pending.front().closed && master.IsFree(pending.front().tick)) {
        Buffer& buffer = pending.front();
        t.assign(buffer.count, 0);
        const u32 first_run = std::min(buffer.count, kSlots - buffer.first);
        bool ready = device.getQueryPoolResults(pool, buffer.first, first_run,
                                                first_run * sizeof(u64), t.data(), sizeof(u64),
                                                vk::QueryResultFlagBits::e64) ==
                     vk::Result::eSuccess;
        if (ready && first_run < buffer.count) {
            const u32 rest = buffer.count - first_run;
            ready = device.getQueryPoolResults(pool, 0, rest, rest * sizeof(u64),
                                               t.data() + first_run, sizeof(u64),
                                               vk::QueryResultFlagBits::e64) ==
                    vk::Result::eSuccess;
        }
        if (!ready) {
            break; // not written yet: next time
        }
        for (u32 i = 1; i < buffer.count; ++i) {
            const u64 ticks = ((t[i] & mask) - (t[i - 1] & mask)) & mask;
            const double ns = static_cast<double>(ticks) * ns_per_tick;
            if (ns >= 0.0 && ns < 1e9) {
                aggregator.Add(buffer.segments[i - 1], ns / 1e6);
            }
        }
        if (buffer.partial) {
            ++untimed;
        }
        used -= buffer.count;
        pending.pop_front();
    }
    const auto now = std::chrono::steady_clock::now();
    if (window_start == std::chrono::steady_clock::time_point{}) {
        window_start = now;
        return;
    }
    const double seconds = std::chrono::duration<double>(now - window_start).count();
    if (seconds < 10.0 || aggregator.Frames() == 0) {
        return;
    }
    aggregator.Untimed(untimed);
    untimed = 0;
    for (const std::string& line : aggregator.Report(seconds)) {
        LOG_INFO(Render_Vulkan, "{}", line);
    }
    window_start = now;
}

} // namespace Vulkan
