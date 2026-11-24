// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/audio_resampler.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "audio_core/audio_types.h"
#include "audio_core/sink_details.h"
#include "common/logging/log.h"
#include "common/xr_pacer.h"

namespace FrameCores {

AudioResampler::AudioResampler() : ring(kRingCapacityFrames * 2u, 0) {}

AudioResampler::~AudioResampler() {
    Shutdown();
}

bool AudioResampler::Init(uint32_t core_sample_rate, bool use_null_sink) {
    Shutdown();

    core_rate.store(core_sample_rate > 0 ? core_sample_rate : 32768u, std::memory_order_relaxed);

    if (use_null_sink) {
        const auto& details = AudioCore::GetSinkDetails(AudioCore::SinkType::Null);
        sink = details.create_sink("None");
    } else {
        const auto sink_type = Settings::values.output_type.GetValue();
        const auto device = Settings::values.output_device.GetValue();
        const auto& details = AudioCore::GetSinkDetails(sink_type);
        sink = details.create_sink(device);
        if (!sink) {
            LOG_WARNING(Audio_Sink, "AudioResampler: falling back to null sink");
            sink = AudioCore::GetSinkDetails(AudioCore::SinkType::Null).create_sink("None");
        }
    }

    if (!sink) {
        return false;
    }

    const unsigned int native_rate = sink->GetNativeSampleRate();
    output_rate.store(native_rate > 0 ? native_rate
                                      : static_cast<uint32_t>(AudioCore::native_sample_rate),
                      std::memory_order_relaxed);
    immediate_submission = sink->ImmediateSubmission();

    Flush();

    if (!immediate_submission) {
        sink->SetCallback([this](s16* out, std::size_t frames) { Pull(out, frames); });
    }
    return true;
}

void AudioResampler::Shutdown() {
    // Destroy sink first so its callback thread stops before touching the FIFO.
    sink.reset();
    immediate_submission = false;
    Flush();
}

void AudioResampler::SetCoreSampleRate(uint32_t rate) {
    if (rate > 0) {
        core_rate.store(rate, std::memory_order_relaxed);
    }
}

void AudioResampler::SetFastForward(bool on, Settings::FastForwardAudio mode) {
    const bool prev = fast_forward.exchange(on, std::memory_order_relaxed);
    ff_mode.store(mode, std::memory_order_relaxed);
    if (prev != on) {
        // Flush on fast-forward transitions so latency never lingers afterward.
        Flush();
    }
}

void AudioResampler::SetPaused(bool is_paused) {
    const bool prev = paused.exchange(is_paused, std::memory_order_relaxed);
    if (prev != is_paused) {
        Flush();
    }
}

void AudioResampler::Flush() {
    std::lock_guard lock(mutex);
    read_pos = 0;
    write_pos = 0;
    queued_frames = 0;
    frac_pos = 0.0;
    prev_left = 0;
    prev_right = 0;
}

void AudioResampler::Push(const int16_t* stereo_pcm, std::size_t frame_count) {
    if (!stereo_pcm || frame_count == 0) {
        return;
    }
    if (paused.load(std::memory_order_relaxed)) {
        return;
    }
    const bool is_ff = fast_forward.load(std::memory_order_relaxed);
    if (is_ff && ff_mode.load(std::memory_order_relaxed) == Settings::FastForwardAudio::Muted) {
        return;
    }

    const uint32_t in_rate = core_rate.load(std::memory_order_relaxed);
    // Keep buffered latency bounded so audio lag never accumulates (normal: <= 90 ms,
    // fast-forward: <= 45 ms).
    const std::size_t max_watermark =
        std::min<std::size_t>(kRingCapacityFrames - 64u,
                              static_cast<std::size_t>(in_rate) * (is_ff ? 45u : 90u) / 1000u);
    const std::size_t trim_target = max_watermark / 2u;

    std::size_t immediate_out_frames = 0;
    {
        std::lock_guard lock(mutex);

        // If incoming chunk alone exceeds watermark, keep only its newest tail.
        if (frame_count > max_watermark) {
            const std::size_t skip = frame_count - trim_target;
            stereo_pcm += skip * 2u;
            frame_count -= skip;
        }

        // Drop oldest queued backlog if adding this chunk would exceed max_watermark.
        if (queued_frames + frame_count > max_watermark) {
            const std::size_t drop = (queued_frames + frame_count) - trim_target;
            const std::size_t actual_drop = std::min(drop, queued_frames);
            read_pos = (read_pos + actual_drop) & kRingMask;
            queued_frames -= actual_drop;
            frac_pos = 0.0;
        }

        for (std::size_t i = 0; i < frame_count; ++i) {
            ring[write_pos * 2u] = stereo_pcm[i * 2u];
            ring[write_pos * 2u + 1u] = stereo_pcm[i * 2u + 1u];
            write_pos = (write_pos + 1u) & kRingMask;
        }
        queued_frames += frame_count;

        if (immediate_submission && sink) {
            const uint32_t out_rate = output_rate.load(std::memory_order_relaxed);
            const double pacer_ratio = std::clamp(
                Common::XrPacer::Get().audio_ratio.load(std::memory_order_relaxed), 0.95, 1.05);
            const double step =
                (static_cast<double>(in_rate) * pacer_ratio) / static_cast<double>(out_rate);
            if (queued_frames >= 2 && step > 0.0) {
                immediate_out_frames = static_cast<std::size_t>(
                    std::floor((static_cast<double>(queued_frames - 1) - frac_pos) / step));
                if (immediate_out_frames > 0) {
                    immediate_scratch.resize(immediate_out_frames * 2u);
                    ResampleLocked(immediate_scratch.data(), immediate_out_frames);
                }
            }
        }
    }

    if (immediate_out_frames > 0 && sink) {
        sink->PushSamples(immediate_scratch.data(), immediate_out_frames);
    }
}

void AudioResampler::Pull(int16_t* out, std::size_t out_frames) {
    if (!out || out_frames == 0) {
        return;
    }
    if (paused.load(std::memory_order_relaxed) ||
        (fast_forward.load(std::memory_order_relaxed) &&
         ff_mode.load(std::memory_order_relaxed) == Settings::FastForwardAudio::Muted)) {
        std::memset(out, 0, out_frames * 2u * sizeof(int16_t));
        return;
    }
    std::lock_guard lock(mutex);
    ResampleLocked(out, out_frames);
}

void AudioResampler::ResampleLocked(int16_t* out, std::size_t out_frames) {
    const uint32_t in_rate = core_rate.load(std::memory_order_relaxed);
    const uint32_t out_rate = output_rate.load(std::memory_order_relaxed);
    const double pacer_ratio = std::clamp(
        Common::XrPacer::Get().audio_ratio.load(std::memory_order_relaxed), 0.95, 1.05);
    const double step =
        (static_cast<double>(in_rate) * pacer_ratio) / static_cast<double>(std::max(1u, out_rate));
    const float vol = Settings::values.audio_muted
                          ? 0.0f
                          : std::clamp(Settings::values.volume.GetValue(), 0.0f, 1.0f);

    std::size_t written = 0;
    while (written < out_frames && queued_frames >= 2) {
        const std::size_t idx0 = read_pos;
        const std::size_t idx1 = (read_pos + 1u) & kRingMask;

        const double l0 = static_cast<double>(ring[idx0 * 2u]);
        const double r0 = static_cast<double>(ring[idx0 * 2u + 1u]);
        const double l1 = static_cast<double>(ring[idx1 * 2u]);
        const double r1 = static_cast<double>(ring[idx1 * 2u + 1u]);

        const double l = (l0 + frac_pos * (l1 - l0)) * static_cast<double>(vol);
        const double r = (r0 + frac_pos * (r1 - r0)) * static_cast<double>(vol);

        const auto sl = static_cast<int16_t>(std::clamp(std::lround(l), -32768l, 32767l));
        const auto sr = static_cast<int16_t>(std::clamp(std::lround(r), -32768l, 32767l));
        out[written * 2u] = sl;
        out[written * 2u + 1u] = sr;
        prev_left = sl;
        prev_right = sr;
        ++written;

        frac_pos += step;
        const auto advance = static_cast<std::size_t>(frac_pos);
        if (advance > 0) {
            const std::size_t consume = std::min(advance, queued_frames - 1u);
            read_pos = (read_pos + consume) & kRingMask;
            queued_frames -= consume;
            frac_pos -= static_cast<double>(consume);
            if (consume < advance) {
                frac_pos = 0.0;
                break;
            }
        }
    }

    // Smoothly decay any underrun tail to zero to avoid pops/clicks.
    while (written < out_frames) {
        prev_left = static_cast<int16_t>((static_cast<int32_t>(prev_left) * 15) / 16);
        prev_right = static_cast<int16_t>((static_cast<int32_t>(prev_right) * 15) / 16);
        out[written * 2u] = prev_left;
        out[written * 2u + 1u] = prev_right;
        ++written;
    }
}

double AudioResampler::BufferedMs() const {
    std::lock_guard lock(mutex);
    const uint32_t rate = core_rate.load(std::memory_order_relaxed);
    if (rate == 0) {
        return 0.0;
    }
    return (static_cast<double>(queued_frames) * 1000.0) / static_cast<double>(rate);
}

} // namespace FrameCores
