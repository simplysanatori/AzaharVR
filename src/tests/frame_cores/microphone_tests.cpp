// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "audio_core/input.h"
#include "audio_core/null_input.h"
#include "audio_core/static_input.h"
#include "frame_cores/microphone.h"

// AzaharVR: the DS's microphone (frame_cores/microphone.h), with a made up input: no test opens a
// real one.

namespace {

struct Device {
    int made = 0;
    int opened = 0;
    int closed = 0;
    AudioCore::InputParameters params{};
    std::vector<int16_t> recorded; ///< what the next Read has
};

class FakeInput final : public AudioCore::Input {
public:
    explicit FakeInput(Device& device_) : device{device_} {}
    void StartSampling(const AudioCore::InputParameters& params) override {
        device.opened++;
        device.params = params;
        sampling = true;
    }
    void StopSampling() override {
        device.closed += sampling;
        sampling = false;
    }
    bool IsSampling() override {
        return sampling;
    }
    void AdjustSampleRate(u32) override {}
    AudioCore::Samples Read() override {
        AudioCore::Samples bytes(device.recorded.size() * sizeof(int16_t));
        std::memcpy(bytes.data(), device.recorded.data(), bytes.size());
        device.recorded.clear();
        return bytes;
    }

private:
    Device& device;
    bool sampling = false;
};

FrameCores::MicrophoneFactory Make(Device& device) {
    return [&device] {
        device.made++;
        return std::make_unique<FakeInput>(device);
    };
}

} // namespace

TEST_CASE("Microphone: opened when the game samples, closed a while after", "[frame_cores]") {
    Device device;
    FrameCores::Microphone mic;
    mic.SetFactory(Make(device));
    std::array<int16_t, 64> out{};
    for (int i = 0; i < 300; i++) {
        mic.EndFrame();
    }
    CHECK(mic.Read(out.data(), 64) >= 0);
    CHECK(device.made == 0);

    mic.Start();
    mic.Read(out.data(), 64);
    CHECK(device.made == 1);
    CHECK(device.opened == 1);
    CHECK(device.params.sample_size == 16);
    CHECK(device.params.sign == AudioCore::Signedness::Signed);
    CHECK(device.params.sample_rate == 33513982 / 704);

    // A burst every second does not open it again.
    for (int burst = 0; burst < 5; burst++) {
        mic.Stop();
        for (int i = 0; i < 60; i++) {
            mic.EndFrame();
        }
        mic.Start();
        mic.Read(out.data(), 64);
    }
    CHECK(device.opened == 1);
    CHECK(device.closed == 0);

    mic.Stop();
    for (int i = 0; i < 119; i++) {
        mic.EndFrame();
    }
    CHECK(device.closed == 0);
    mic.EndFrame();
    CHECK(device.closed == 1);
}

TEST_CASE("Microphone: the game gets the device's samples", "[frame_cores]") {
    Device device;
    FrameCores::Microphone mic;
    mic.SetFactory(Make(device));
    mic.Start();
    for (int i = 0; i < 1000; i++) {
        device.recorded.push_back(static_cast<int16_t>(i * 13 - 5000));
    }
    const std::vector<int16_t> recorded = device.recorded;
    std::vector<int16_t> got(1536);
    const int first = mic.Read(got.data(), 600);
    REQUIRE(first == 600);
    const int rest = mic.Read(got.data() + first, 936);
    REQUIRE(rest == 400);
    got.resize(1000);
    CHECK(got == recorded);

    // Nothing new yet: melonDS holds the last sample; after 10 ms, silence.
    std::array<int16_t, 64> out;
    CHECK(mic.Read(out.data(), 64) == 0);
    int silent = 0;
    for (int i = 0; i < 500; i++) {
        out.fill(1);
        silent = mic.Read(out.data(), 64);
    }
    CHECK(silent == 64);
    CHECK(std::all_of(out.begin(), out.end(), [](int16_t s) { return s == 0; }));
}

TEST_CASE("Microphone: the game hears now, not a while ago", "[frame_cores]") {
    Device device;
    FrameCores::Microphone mic;
    mic.SetFactory(Make(device));
    mic.Start();
    device.recorded.assign(FrameCores::Microphone::kSampleRate * 2, 1); // two seconds at once
    device.recorded.back() = 2;
    std::vector<int16_t> out(1536);
    std::size_t total = 0;
    int16_t last = 0;
    for (int got; (got = mic.Read(out.data(), 1536)) > 0; total += got) {
        last = out[got - 1];
    }
    CHECK(total <= FrameCores::Microphone::kSampleRate / 10 + 1536);
    CHECK(last == 2);
}

TEST_CASE("Microphone: pause, settings and no device", "[frame_cores]") {
    Device device;
    FrameCores::Microphone mic;
    std::array<int16_t, 64> out{};
    mic.Start();
    CHECK(mic.Read(out.data(), 64) == 64); // none: silent
    mic.SetFactory(Make(device));
    mic.Read(out.data(), 64);
    CHECK(device.opened == 1);
    mic.Close(); // paused
    CHECK(device.closed == 1);
    mic.Read(out.data(), 64); // goes on, the game still samples
    CHECK(device.opened == 2);
    mic.Reload();
    mic.EndFrame();
    CHECK(device.closed == 2);

    // Input Type None and Static Noise.
    mic.SetFactory([] { return std::make_unique<AudioCore::NullInput>(); });
    out.fill(1);
    CHECK(mic.Read(out.data(), 64) == 64);
    CHECK(out[0] == 0);
    mic.SetFactory([] { return std::make_unique<AudioCore::StaticInput>(); });
    CHECK(mic.Read(out.data(), 64) == 64);
    CHECK(std::any_of(out.begin(), out.end(), [](int16_t s) { return s != 0; }));
}
