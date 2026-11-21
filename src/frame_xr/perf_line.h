// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame bfidr: the "xr: perf" line, every ChangeGate::kSummaryMs (10 s) while a game is
// live in VR (the 1.2.x plan's Phase 1: budgets from data, not from GPU behind, which is the
// time from a submit to its fence):
//   - the emulator's GPU work per game frame (vk_gpu_busy.h: AZAHARFRAME_GPU_BUSY=1 only, off by
//     default; "not measured" when off), and the XR thread's pass (the controllers, or the
//     projection screens) per refresh;
//   - the late releases of the top screen, the late pictures Black Frame Insertion lit again,
//     and the longest xrEndFrame call;
//   - CPU per thread as a share of one core, from /proc/self/task/*/stat (utime + stime), by
//     thread name (Group), plus the busiest others by name, so names this build does not know
//     show up in the log.
// The XR thread adds up its seconds (Window) and hands them over; the /proc read and the line
// are on a short thread of their own (app.cpp), never on the frame loop. Pure here
// (unit-tested: src/tests/frame_xr/perf_line.cpp).

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace FrameXr::PerfLine {

/// One thread's /proc/<pid>/task/<tid>/stat: its name and the CPU it used (utime + stime, in
/// clock ticks).
struct TaskTimes {
    std::string name;
    uint64_t ticks = 0;
};

/// "tid (comm) state ppid ... utime stime ...": the name may hold spaces and parentheses, so the
/// fields are counted from the last ')'. Nothing for a line that is not one.
inline std::optional<TaskTimes> ParseTaskStat(std::string_view stat) {
    const auto open = stat.find('(');
    const auto close = stat.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open) {
        return std::nullopt;
    }
    TaskTimes t;
    t.name = std::string(stat.substr(open + 1, close - open - 1));
    // After ") ": field 3 (state) is the first; utime and stime are fields 14 and 15.
    std::string_view rest = stat.substr(close + 1);
    int field = 2;
    uint64_t utime = 0, stime = 0;
    bool have_u = false, have_s = false;
    std::size_t i = 0;
    while (i < rest.size() && !(have_u && have_s)) {
        while (i < rest.size() && rest[i] == ' ') {
            i++;
        }
        if (i >= rest.size()) {
            break;
        }
        const std::size_t start = i;
        while (i < rest.size() && rest[i] != ' ' && rest[i] != '\n') {
            i++;
        }
        field++;
        if (field == 14 || field == 15) {
            uint64_t v = 0;
            for (std::size_t k = start; k < i; k++) {
                if (rest[k] < '0' || rest[k] > '9') {
                    return std::nullopt;
                }
                v = v * 10 + static_cast<uint64_t>(rest[k] - '0');
            }
            (field == 14 ? utime : stime) = v;
            (field == 14 ? have_u : have_s) = true;
        }
        if (i < rest.size() && rest[i] == '\n') {
            break;
        }
    }
    if (!have_u || !have_s) {
        return std::nullopt;
    }
    t.ticks = utime + stime;
    return t;
}

/// The groups the line names, in its order: the emulation thread (Qt names it after its class),
/// the renderer's worker (VulkanWorker, the GPU's submits) and the desktop window's presenter,
/// the frame loop, the handoff thread (XrRelease: the renderer's releases to the XR swapchains)
/// and audio. The audio threads are the libraries' own: libpulse's mainloop under cubeb
/// ("threaded-ml"), SDL's ("SDLAudioP<n>"), OpenAL Soft's ("alsoft-mixer"), a PipeWire loop
/// ("data-loop.<n>", "pw-..."); which one runs depends on the sink and the system (not verified
/// on the headset: the busiest others are named in the line for that). The kernel keeps 15
/// characters of a name.
inline constexpr const char* kGroups[] = {"emulation", "video", "present", "FrameXR", "handoff",
                                          "audio"};

/// The group of a thread name, or nullptr for the others.
inline const char* Group(std::string_view name) {
    const auto starts = [&](std::string_view p) { return name.substr(0, p.size()) == p; };
    if (name == "EmuThread") {
        return "emulation";
    }
    if (name == "VulkanWorker") {
        return "video";
    }
    if (name == "VulkanPresent" || starts("Present") || starts("SecondaryPrese")) {
        return "present";
    }
    if (name == "FrameXR") {
        return "FrameXR";
    }
    if (name == "XrRelease") {
        return "handoff";
    }
    if (name == "threaded-ml" || starts("SDLAudio") || starts("alsoft-") || starts("cubeb") ||
        starts("data-loop") || starts("pw-") || starts("PulseHotplug")) {
        return "audio";
    }
    return nullptr;
}

/// The CPU per thread between two reads, by tid, as percent of one core over `seconds` at `hz`
/// clock ticks a second. Threads that started after the first read count from 0; threads that
/// ended in between are not in the second read and count nothing.
struct CpuShares {
    std::map<std::string, double> groups; ///< kGroups' (0 when none ran)
    std::vector<std::pair<std::string, double>> others; ///< by name, busiest first
    double others_total = 0.0;
    double process = 0.0;
};

inline CpuShares Shares(const std::map<int, TaskTimes>& before, const std::map<int, TaskTimes>& after,
                        double seconds, long hz) {
    CpuShares out;
    for (const char* g : kGroups) {
        out.groups[g] = 0.0;
    }
    if (seconds <= 0.0 || hz <= 0) {
        return out;
    }
    const double per_tick = 100.0 / (seconds * static_cast<double>(hz));
    std::map<std::string, double> others;
    for (const auto& [tid, now] : after) {
        // By tid, whatever the name then (a thread may be named after it starts).
        const auto it = before.find(tid);
        const uint64_t was = it != before.end() ? std::min(it->second.ticks, now.ticks) : 0;
        const double pct = static_cast<double>(now.ticks - was) * per_tick;
        out.process += pct;
        if (const char* g = Group(now.name)) {
            out.groups[g] += pct;
        } else {
            others[now.name] += pct;
            out.others_total += pct;
        }
    }
    out.others.assign(others.begin(), others.end());
    std::stable_sort(out.others.begin(), out.others.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });
    return out;
}

/// The XR thread's seconds between two lines (from LogSummary's taken values: never taken
/// twice).
struct Window {
    double seconds = 0.0;
    uint64_t display_frames = 0, game_frames = 0;
    uint64_t emu_gpu_ns = 0, emu_buffers = 0;
    uint64_t pass_gpu_ns = 0, pass_draws = 0;
    uint64_t late_releases = 0, late_lit = 0;
    double end_frame_max_ms = 0.0;
    uint32_t bfi_seconds = 0;
    int factor = 0;        ///< the factor drawn at the end (0: not known)
    int screen_factor = 0; ///< the most the screen can show at the end (0: not known)
    /// AzaharFrame bfikit: the top screen swaps, those with the game's world uniforms read (the
    /// immersive modes), and of them the ones with the same uniforms as the swap before (all of
    /// them; without the camera's rows), each with how many came right after a new picture
    /// (video_core/vr_content_repeat.h).
    uint32_t swaps = 0, swaps_read = 0, swaps_same = 0, swaps_same_alt = 0;
    uint32_t swaps_scene_same = 0, swaps_scene_same_alt = 0;
};

/// The line (without "xr: "). `gpu_busy_on`: the emulator's timer runs (AZAHARFRAME_GPU_BUSY=1).
inline std::string Format(const Window& w, bool gpu_busy_on, const CpuShares& cpu,
                          std::size_t top_others = 3) {
    char buf[384];
    std::string out;
    std::snprintf(buf, sizeof(buf), "perf %.1f s, %llu game frames, %llu refreshes: ", w.seconds,
                  static_cast<unsigned long long>(w.game_frames),
                  static_cast<unsigned long long>(w.display_frames));
    out += buf;
    if (!gpu_busy_on) {
        out += "emulator GPU not measured (AZAHARFRAME_GPU_BUSY=1)";
    } else if (w.game_frames == 0 || w.emu_buffers == 0) {
        out += "emulator GPU -";
    } else {
        std::snprintf(buf, sizeof(buf), "emulator GPU %.2f ms a game frame (%.2f a refresh, %llu "
                                        "command buffers)",
                      static_cast<double>(w.emu_gpu_ns) / 1e6 / static_cast<double>(w.game_frames),
                      w.display_frames ? static_cast<double>(w.emu_gpu_ns) / 1e6 /
                                             static_cast<double>(w.display_frames)
                                       : 0.0,
                      static_cast<unsigned long long>(w.emu_buffers));
        out += buf;
    }
    if (w.pass_draws > 0) {
        std::snprintf(buf, sizeof(buf), ", XR pass %.2f ms a draw (%llu draws)",
                      static_cast<double>(w.pass_gpu_ns) / 1e6 / static_cast<double>(w.pass_draws),
                      static_cast<unsigned long long>(w.pass_draws));
        out += buf;
    } else {
        out += ", XR pass -";
    }
    std::snprintf(buf, sizeof(buf),
                  "; late releases %llu, late pictures lit again %llu, longest xrEndFrame %.1f ms; "
                  "Black Frame Insertion %u s",
                  static_cast<unsigned long long>(w.late_releases),
                  static_cast<unsigned long long>(w.late_lit), w.end_frame_max_ms, w.bfi_seconds);
    out += buf;
    if (w.factor > 0) {
        std::snprintf(buf, sizeof(buf), ", drawn at %dx", w.factor);
        out += buf;
    }
    if (w.screen_factor > 0) {
        std::snprintf(buf, sizeof(buf), ", the screen shows up to %dx", w.screen_factor);
        out += buf;
    }
    // AzaharFrame bfikit: new pictures against swaps (only where the uniforms were read).
    if (w.swaps_read > 0) {
        std::snprintf(buf, sizeof(buf),
                      "; game pictures: %u top screen swaps, %u with world uniforms read, %u new, "
                      "%u the same as the swap before (%u right after a new one), without the "
                      "camera %u new, %u the same (%u right after a new one)",
                      w.swaps, w.swaps_read, w.swaps_read - w.swaps_same, w.swaps_same,
                      w.swaps_same_alt, w.swaps_read - w.swaps_scene_same, w.swaps_scene_same,
                      w.swaps_scene_same_alt);
        out += buf;
    }
    out += "; CPU % of a core:";
    bool first = true;
    for (const char* g : kGroups) {
        const auto it = cpu.groups.find(g);
        std::snprintf(buf, sizeof(buf), "%s %s %.0f", first ? "" : ",", g,
                      it != cpu.groups.end() ? it->second : 0.0);
        out += buf;
        first = false;
    }
    std::snprintf(buf, sizeof(buf), ", others %.0f", cpu.others_total);
    out += buf;
    const std::size_t named = std::min(top_others, cpu.others.size());
    for (std::size_t i = 0; i < named; i++) {
        std::snprintf(buf, sizeof(buf), "%s%s %.0f%s", i ? ", " : " (", cpu.others[i].first.c_str(),
                      cpu.others[i].second, i + 1 == named ? ")" : "");
        out += buf;
    }
    std::snprintf(buf, sizeof(buf), ", process %.0f", cpu.process);
    out += buf;
    return out;
}

/// AzaharFrame bfidr: the line's memory note, from /proc/self/status and /proc/meminfo:
/// "; memory: process N MB, available N MB, swap used N MB" (a field it can not find is left
/// out; all missing gives ""). Added after the headset stalls of 2026-11-18, which came
/// minutes into play: the line now says whether something was growing.
inline long long KbField(std::string_view text, std::string_view key) {
    std::size_t at = 0;
    while ((at = text.find(key, at)) != std::string_view::npos) {
        if (at == 0 || text[at - 1] == '\n') {
            std::size_t i = at + key.size();
            while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
                i++;
            }
            long long v = 0;
            bool any = false;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
                v = v * 10 + (text[i] - '0');
                i++;
                any = true;
            }
            return any ? v : -1;
        }
        at += key.size();
    }
    return -1;
}

inline std::string MemoryNote(std::string_view status, std::string_view meminfo) {
    const long long rss = KbField(status, "VmRSS:");
    const long long avail = KbField(meminfo, "MemAvailable:");
    const long long swap_total = KbField(meminfo, "SwapTotal:");
    const long long swap_free = KbField(meminfo, "SwapFree:");
    std::string out;
    char buf[64];
    const auto add = [&](const char* what, long long kb) {
        std::snprintf(buf, sizeof(buf), "%s%s %lld MB", out.empty() ? "; memory: " : ", ", what,
                      kb / 1024);
        out += buf;
    };
    if (rss >= 0) {
        add("process", rss);
    }
    if (avail >= 0) {
        add("available", avail);
    }
    if (swap_total > 0 && swap_free >= 0) {
        add("swap used", swap_total - swap_free);
    }
    return out;
}

} // namespace FrameXr::PerfLine
