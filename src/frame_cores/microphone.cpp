// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/microphone.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "audio_core/input.h"
#include "common/logging/log.h"

namespace FrameCores {
namespace {

constexpr int kIdleFrames = 120; // 2 s
/// What may wait (100 ms): the oldest goes, so the game hears now and not a while ago.
constexpr std::size_t kMaxWaiting = Microphone::kSampleRate / 10;
/// Reads (one a sample) with nothing new until the game gets silence in place of the last sample
/// (10 ms): held, that is a level no silence has.
constexpr int kDryReads = Microphone::kSampleRate / 100;

} // namespace

Microphone::Microphone() : dry_reads(kDryReads) {}

Microphone::~Microphone() {
    Close();
}

void Microphone::SetFactory(MicrophoneFactory factory_) {
    Close();
    factory = std::move(factory_);
}

void Microphone::Start() {
    started = true;
    idle_frames = 0;
}

void Microphone::Stop() {
    started = false;
}

int Microphone::Read(int16_t* out, int max) {
    if (!out || max <= 0) {
        return 0;
    }
    if (started && !input && factory) {
        input = factory();
        if (input) {
            input->StartSampling({.sign = AudioCore::Signedness::Signed,
                                  .sample_size = 16,
                                  .buffer_loop = false,
                                  .sample_rate = kSampleRate,
                                  .buffer_offset = 0,
                                  .buffer_size = 0});
            LOG_INFO(Audio, "Microphone opened for the game");
        }
    }
    // A device has all it recorded in one Read; a synthetic input a few samples in each.
    while (input && samples.size() < static_cast<std::size_t>(max)) {
        const AudioCore::Samples bytes = input->Read();
        if (bytes.size() < sizeof(int16_t)) {
            break;
        }
        for (std::size_t i = 0; i + sizeof(int16_t) <= bytes.size(); i += sizeof(int16_t)) {
            int16_t sample;
            std::memcpy(&sample, bytes.data() + i, sizeof(sample));
            samples.push_back(sample);
        }
    }
    const std::size_t limit = static_cast<std::size_t>(max) + kMaxWaiting;
    if (samples.size() > limit) {
        samples.erase(samples.begin(),
                      samples.begin() + static_cast<std::ptrdiff_t>(samples.size() - limit));
    }
    if (samples.empty()) {
        dry_reads = std::min(dry_reads + 1, kDryReads);
        if (dry_reads < kDryReads) {
            return 0;
        }
        const int silent = std::min(max, 64);
        std::fill_n(out, silent, int16_t{0});
        return silent;
    }
    dry_reads = 0;
    const int count = std::min(max, static_cast<int>(samples.size()));
    std::copy_n(samples.begin(), count, out);
    samples.erase(samples.begin(), samples.begin() + count);
    return count;
}

void Microphone::EndFrame() {
    if (reload.exchange(false, std::memory_order_relaxed)) {
        Close();
    }
    if (input && !started && ++idle_frames >= kIdleFrames) {
        Close();
    }
}

void Microphone::Close() {
    if (input) {
        input->StopSampling();
        input.reset();
        LOG_INFO(Audio, "Microphone closed");
    }
    samples.clear();
    idle_frames = 0;
    dry_reads = kDryReads;
}

void Microphone::Reload() {
    reload.store(true, std::memory_order_relaxed);
}

} // namespace FrameCores
