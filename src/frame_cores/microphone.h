// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>

namespace AudioCore {
class Input;
}

namespace FrameCores {

/// Makes the input the settings choose (Input Type, Device), as a 3DS game's.
using MicrophoneFactory = std::function<std::unique_ptr<AudioCore::Input>()>;

/// The DS's microphone. The input is opened when the game first samples it and closed a while
/// after the game stopped, so a game that samples in bursts does not open it again and again.
/// Emulation thread, but for Reload.
class Microphone {
public:
    /// The DS samples at 33513982 / 704 Hz; melonDS takes 16 bit samples at that rate.
    static constexpr uint32_t kSampleRate = 47605;

    Microphone();
    ~Microphone();

    /// Without one (or with one that makes nothing) the microphone is silent.
    void SetFactory(MicrophoneFactory factory);

    /// The game starts and stops sampling.
    void Start();
    void Stop();

    /// Up to `max` samples; 0: none yet, melonDS repeats the last one and asks again.
    int Read(int16_t* out, int max);

    /// Once a frame.
    void EndFrame();

    /// Closes the input at once (pause, end). A game that still samples opens it again.
    void Close();

    /// The settings changed: the next EndFrame closes the input.
    void Reload();

private:
    MicrophoneFactory factory;
    std::unique_ptr<AudioCore::Input> input;
    std::deque<int16_t> samples;
    bool started = false;
    int idle_frames = 0; ///< since the game stopped
    int dry_reads; ///< since the input last had samples
    std::atomic<bool> reload{false};
};

} // namespace FrameCores
