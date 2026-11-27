// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame gpupasses: see gpu_pass_stats.h.

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include "video_core/gpu_pass_stats.h"
#include "video_core/rasterizer_cache/pixel_format.h"

namespace VideoCore::GpuPasses {

namespace {

constexpr std::size_t Index(Bucket b) {
    return static_cast<std::size_t>(b);
}

constexpr std::size_t Index(Why w) {
    return static_cast<std::size_t>(w);
}

double FormatBytes(u8 format) {
    if (format == kNoFormat || format >= static_cast<u8>(PixelFormat::MaxPixelFormat)) {
        return 0.0;
    }
    return static_cast<double>(GetFormatBytesPerPixel(static_cast<PixelFormat>(format)));
}

std::string FormatName(u8 format) {
    if (format == kNoFormat || format >= static_cast<u8>(PixelFormat::MaxPixelFormat)) {
        return "-";
    }
    return std::string{PixelFormatAsString(static_cast<PixelFormat>(format))};
}

/// A count a frame: one decimal, two below 0.05 (a rare event is not shown as 0.0).
std::string Rate(double v) {
    return v > 0.0 && v < 0.05 ? fmt::format("{:.2f}", v) : fmt::format("{:.1f}", v);
}

double P95(std::vector<float> values) {
    if (values.empty()) {
        return 0.0;
    }
    const std::size_t k = static_cast<std::size_t>(
        std::ceil(0.95 * static_cast<double>(values.size()))) - 1;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(k),
                     values.end());
    return values[k];
}

} // namespace

const char* Name(Bucket bucket) {
    switch (bucket) {
    case Bucket::Top3D:
        return "top 3D";
    case Bucket::Top2D:
        return "top 2D";
    case Bucket::Bottom:
        return "bottom";
    case Bucket::Offscreen:
        return "offscreen";
    case Bucket::Copy:
        return "copies";
    case Bucket::Upload:
        return "uploads";
    case Bucket::Readback:
        return "readback";
    case Bucket::Present:
        return "present";
    case Bucket::Other:
        return "other";
    default:
        return "?";
    }
}

const char* Name(Why why) {
    switch (why) {
    case Why::None:
        return "none";
    case Why::Target:
        return "target change";
    case Why::Area:
        return "area change";
    case Why::Copy:
        return "copy";
    case Why::Blit:
        return "blit";
    case Why::Clear:
        return "clear";
    case Why::Convert:
        return "conversion";
    case Why::Scale:
        return "scale";
    case Why::Upload:
        return "upload";
    case Why::Mipmap:
        return "mipmaps";
    case Why::Readback:
        return "readback";
    case Why::Present:
        return "present";
    case Why::Submit:
        return "submit";
    case Why::Foveation:
        return "foveation";
    case Why::Other:
        return "other";
    default:
        return "?";
    }
}

Screen ScreenOf(u32 w, u32 h) {
    if ((w == 240 && h == 400) || (w == 400 && h == 240)) {
        return Screen::Top;
    }
    if ((w == 240 && h == 320) || (w == 320 && h == 240)) {
        return Screen::Bottom;
    }
    return Screen::None;
}

Bucket GameBucket(bool shadow, Screen screen, u32 world_draws) {
    if (shadow) {
        return Bucket::Offscreen;
    }
    switch (screen) {
    case Screen::Top:
        return world_draws > 0 ? Bucket::Top3D : Bucket::Top2D;
    case Screen::Bottom:
        return Bucket::Bottom;
    default:
        return Bucket::Offscreen;
    }
}

double LoadStoreBytes(const Segment& s) {
    const double per_pixel = FormatBytes(s.color) + FormatBytes(s.depth);
    const double times = (s.load ? 1.0 : 0.0) + (s.store ? 1.0 : 0.0);
    return static_cast<double>(s.area) * per_pixel * times;
}

void Aggregator::Sums::AddTo(Sums& into) const {
    const auto add = [](auto& to, const auto& from) {
        for (std::size_t i = 0; i < to.size(); ++i) {
            to[i] += from[i];
        }
    };
    add(into.ms, ms);
    add(into.passes, passes);
    add(into.draws, draws);
    add(into.flat, flat);
    add(into.ops, ops);
    add(into.loads, loads);
    add(into.clears, clears);
    add(into.stores, stores);
    add(into.mpx, mpx);
    add(into.mbytes, mbytes);
    add(into.foveated, foveated);
    add(into.targets, targets);
    add(into.ended, ended);
    add(into.reopened, reopened);
    into.scaled_ms += scaled_ms;
    into.scaled_mpx += scaled_mpx;
    into.unscaled_ms += unscaled_ms;
    into.unscaled_mpx += unscaled_mpx;
    into.outside_ms += outside_ms;
    into.marks += marks;
}

void Aggregator::CloseFrame() {
    if (!open) {
        return;
    }
    double total = 0.0;
    for (std::size_t b = 0; b < kBuckets; ++b) {
        cur.targets[b] = static_cast<double>(cur_targets[b].size());
        cur_targets[b].clear();
        per_frame[b].push_back(static_cast<float>(cur.ms[b]));
        total += cur.ms[b];
    }
    totals.push_back(static_cast<float>(total));
    cur.AddTo(window);
    cur = {};
    open = false;
}

void Aggregator::Add(const Segment& s, double ms) {
    if (open && s.frame != frame) {
        CloseFrame();
    }
    if (!open) {
        open = true;
        frame = s.frame;
    }
    const std::size_t b = Index(s.bucket < Bucket::Count ? s.bucket : Bucket::Other);
    cur.ms[b] += ms;
    cur.marks += 1.0;
    if (!s.pass) {
        cur.ops[b] += s.ops;
        cur.outside_ms += ms;
        return;
    }
    const double attachments = s.attachments;
    const double mpx = static_cast<double>(s.area) / 1e6;
    cur.passes[b] += 1.0;
    cur.draws[b] += s.draws;
    cur.flat[b] += s.flat;
    cur.loads[b] += s.load ? attachments : 0.0;
    cur.clears[b] += s.clear ? attachments : 0.0;
    cur.stores[b] += s.store ? attachments : 0.0;
    cur.mpx[b] += mpx;
    cur.mbytes[b] += LoadStoreBytes(s) / 1e6;
    cur.foveated[b] += s.foveated ? 1.0 : 0.0;
    cur.ended[Index(s.end < Why::Count ? s.end : Why::Other)] += 1.0;
    if (s.reopen != Why::None) {
        cur.reopened[Index(s.reopen < Why::Count ? s.reopen : Why::Other)] += 1.0;
    }
    if (!IsPassBucket(s.bucket)) {
        cur.outside_ms += ms; // a helper's pass (a conversion, a filter): the operation's
    } else if (s.scaled) {
        cur.scaled_ms += ms;
        cur.scaled_mpx += mpx;
    } else {
        cur.unscaled_ms += ms;
        cur.unscaled_mpx += mpx;
    }
    auto& targets = cur_targets[b];
    if (std::find(targets.begin(), targets.end(), s.target) == targets.end()) {
        targets.push_back(s.target);
    }
    const Key key{static_cast<u8>(b), s.width, s.height, s.color, s.depth, s.index};
    auto it = top.find(key);
    if (it == top.end()) {
        if (top.size() >= kMostKeys) {
            ++top_dropped;
            return;
        }
        it = top.emplace(key, Top{}).first;
    }
    it->second.ms += ms;
    it->second.n += 1.0;
    it->second.draws += s.draws;
}

std::vector<std::string> Aggregator::Report(double seconds) {
    std::vector<std::string> lines;
    const std::size_t frames = totals.size();
    if (frames > 0) {
        const double n = static_cast<double>(frames);
        const Sums& w = window;
        double total = 0.0;
        for (double v : w.ms) {
            total += v;
        }
        lines.push_back(fmt::format(
            "gpu passes: {} game frames in {:.1f} s, the renderer's GPU time {:.2f} ms a frame "
            "(p95 {:.2f}): the buckets below add up to it (timestamps outside render passes only, "
            "each pass whole with its loads and stores); {:.0f} timestamps a frame{}",
            frames, seconds, total / n, P95(totals), w.marks / n,
            untimed ? fmt::format(", {} command buffers not timed (query slots full)", untimed)
                    : std::string{}));
        lines.push_back(fmt::format(
            "gpu passes: the game's render passes into scaled targets {:.2f} ms a frame ({:.2f} "
            "Mpx), into 1x targets {:.2f} ms ({:.2f} Mpx); work outside them {:.2f} ms (copies, "
            "uploads, readback, present, other, helper passes)",
            w.scaled_ms / n, w.scaled_mpx / n, w.unscaled_ms / n, w.unscaled_mpx / n,
            w.outside_ms / n));
        for (std::size_t b = 0; b < kBuckets; ++b) {
            if (w.ms[b] <= 0.0 && w.passes[b] <= 0.0 && w.ops[b] <= 0.0) {
                continue;
            }
            const Bucket bucket = static_cast<Bucket>(b);
            std::string line = fmt::format("gpu passes: {} {:.2f} ms a frame (p95 {:.2f})",
                                           Name(bucket), w.ms[b] / n, P95(per_frame[b]));
            if (IsPassBucket(bucket)) {
                line += fmt::format(", {:.1f} passes, {:.1f} draws ({:.1f} 2D), {:.1f} targets, "
                                    "{:.2f} Mpx, attachments loaded {:.1f} cleared {:.1f} stored "
                                    "{:.1f}, ~{:.1f} MB load+store",
                                    w.passes[b] / n, w.draws[b] / n, w.flat[b] / n,
                                    w.targets[b] / n, w.mpx[b] / n, w.loads[b] / n,
                                    w.clears[b] / n, w.stores[b] / n, w.mbytes[b] / n);
                if (w.foveated[b] > 0.0) {
                    line += fmt::format(", {:.1f} foveated", w.foveated[b] / n);
                }
            } else {
                line += fmt::format(", {} operations", Rate(w.ops[b] / n));
            }
            lines.push_back(std::move(line));
        }
        std::string ended;
        double ended_n = 0.0;
        std::string reopened;
        double reopened_n = 0.0;
        for (std::size_t i = 0; i < kWhys; ++i) {
            if (w.ended[i] > 0.0) {
                ended += fmt::format("{}{} {}", ended.empty() ? "" : ", ",
                                     Name(static_cast<Why>(i)), Rate(w.ended[i] / n));
                ended_n += w.ended[i];
            }
            if (w.reopened[i] > 0.0) {
                reopened += fmt::format("{}{} {}", reopened.empty() ? "" : ", ",
                                        Name(static_cast<Why>(i)), Rate(w.reopened[i] / n));
                reopened_n += w.reopened[i];
            }
        }
        lines.push_back(fmt::format(
            "gpu passes: pass ends a frame {:.1f} ({}); passes that load a target an earlier pass "
            "this frame stored {:.1f}{}",
            ended_n / n, ended.empty() ? "none" : ended, reopened_n / n,
            reopened.empty() ? "" : fmt::format(" (after {})", reopened)));
        std::vector<std::pair<Key, Top>> sorted(top.begin(), top.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
        std::string best;
        for (std::size_t i = 0; i < std::min<std::size_t>(5, sorted.size()); ++i) {
            const auto& [key, t] = sorted[i];
            const auto& [bucket, width, height, color, depth, index] = key;
            best += fmt::format("{}{}) {} {}x{} {}+{} pass {} on its target: {:.2f} ms each, "
                                "{:.1f} a frame, {:.0f} draws",
                                i ? "; " : "", i + 1, Name(static_cast<Bucket>(bucket)), width,
                                height, FormatName(color), FormatName(depth), index + 1,
                                t.ms / t.n, t.n / n, t.draws / t.n);
        }
        lines.push_back(fmt::format("gpu passes: top 5 passes by time: {}{}",
                                    best.empty() ? "none" : best,
                                    top_dropped ? fmt::format(" ({} not kept)", top_dropped)
                                                : std::string{}));
    }
    window = {};
    for (auto& v : per_frame) {
        v.clear();
    }
    totals.clear();
    top.clear();
    top_dropped = 0;
    untimed = 0;
    return lines;
}

} // namespace VideoCore::GpuPasses
