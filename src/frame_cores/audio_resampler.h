// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "audio_core/sink.h"
#include "common/settings.h"

namespace FrameCores {

/**
 * Stereo PCM16 FIFO + fractional linear resampler bridging non-3DS core audio
 * (e.g. 32768 Hz scaled by Common::XrPacer::Get().audio_ratio) to Azahar's
 * AudioCore::Sink (32728 Hz).
 *
 * Explicitly caps buffered latency and drops excess backlog during fast-forward
 * or pause/menu transitions so audio delay never accumulates.
 */
class AudioResampler {
public:
    AudioResampler();
    ~AudioResampler();

    AudioResampler(const AudioResampler&) = delete;
    AudioResampler& operator=(const AudioResampler&) = delete;

    /// Initializes the audio sink and FIFO for `core_sample_rate` Hz.
    /// Pass `use_null_sink = true` in headless or unit test runs.
    bool Init(uint32_t core_sample_rate, bool use_null_sink = false);

    /// Stops and destroys the audio sink and clears buffered samples.
    void Shutdown();

    /// Updates the core's nominal input sample rate, for what is pushed from here on. (What is
    /// queued, 90 ms at most, then plays at the new rate too.)
    void SetCoreSampleRate(uint32_t rate);

    /// Toggles fast-forward mode: Muted plays nothing, Sped Up reads `speed` times faster,
    /// Normal Pitch and Normal Speed keep the pitch and drop backlog (tight and loose caps).
    void SetFastForward(bool on, Settings::FastForwardAudio mode = Settings::FastForwardAudio::NormalPitch,
                        double speed = 1.0);

    /// How many times faster the FIFO is read (1 unless fast-forwarding in Sped Up).
    static double ReadSpeed(bool fast_forward, Settings::FastForwardAudio mode, double speed);

    /// The most audio, in ms, kept queued before the oldest is dropped.
    static uint32_t LatencyCapMs(bool fast_forward, Settings::FastForwardAudio mode);

    /// Pauses or resumes audio output. Flushing prevents stale audio on resume.
    void SetPaused(bool paused);

    /// Immediately clears all queued samples and resets fractional resampler state.
    void Flush();

    /// Enqueues interleaved stereo PCM16 frames from the emulation thread.
    void Push(const int16_t* stereo_pcm, std::size_t frame_count);

    /// Pulls `out_frames` resampled stereo PCM16 frames (sink callback or unit tests).
    void Pull(int16_t* out, std::size_t out_frames);

    /// Approximate queued audio duration in milliseconds (for tests and telemetry).
    double BufferedMs() const;

    uint32_t OutputSampleRate() const {
        return output_rate.load(std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t kRingCapacityFrames = 16384; // ~500 ms at 32768 Hz
    static constexpr std::size_t kRingMask = kRingCapacityFrames - 1;

    double ReadSpeedNow() const;
    void ResampleLocked(int16_t* out, std::size_t out_frames);

    std::unique_ptr<AudioCore::Sink> sink;
    bool immediate_submission = false;

    mutable std::mutex mutex;
    std::vector<int16_t> ring; // interleaved stereo (kRingCapacityFrames * 2)
    std::size_t read_pos = 0;
    std::size_t write_pos = 0;
    std::size_t queued_frames = 0;
    double frac_pos = 0.0;
    int16_t prev_left = 0;
    int16_t prev_right = 0;
    std::vector<int16_t> immediate_scratch;

    std::atomic<uint32_t> core_rate{32768};
    std::atomic<uint32_t> output_rate{AudioCore::native_sample_rate};
    std::atomic<bool> fast_forward{false};
    std::atomic<Settings::FastForwardAudio> ff_mode{Settings::FastForwardAudio::NormalPitch};
    std::atomic<double> ff_speed{1.0};
    std::atomic<bool> paused{false};
};

} // namespace FrameCores
