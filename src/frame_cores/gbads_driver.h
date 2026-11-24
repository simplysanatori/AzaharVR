// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "frame_cores/frame_driver.h"
#include "gbads_game.h"

namespace FrameCores {

/// When the loop is given up for the game alone.
struct GbaDsLimits {
    uint32_t frames = 120; ///< in a row that it did not step or show
    /// For one frame, the core's own work aside, and for the end.
    std::chrono::milliseconds wait{2000};
};

/// gba-ds's game.c. Its whole loop runs on a thread of ours, which works only while a call from the
/// host waits for it: from one ui_present to the next in RunFrame.
class GbaDsDriver final : public FrameDriver {
public:
    /// Null: one is in use already (game.c is one game per process), or the loop has no Secondary
    /// Screen for this game. Set-up is done here; no frame runs.
    static std::unique_ptr<GbaDsDriver> Make(const DriverServices& services, const GbadsLoop& loop,
                                             const GbaDsLimits& limits);
    /// Puts the core back and joins the thread.
    ~GbaDsDriver() override;

    GbaDsDriver(const GbaDsDriver&) = delete;
    GbaDsDriver& operator=(const GbaDsDriver&) = delete;

    void RunFrame(const DriverInput& in, DriverFrame& out) override;
    void Reset() override;
    bool StateSafeNow() const override;
    void AfterStateLoad() override;
    std::pair<uint32_t, uint32_t> SecondarySize() const override;
    bool HasTouch() const override {
        return false;
    }

    /// The Secondary Screen's next page, or its other view. Not bound to anything. Any thread; from
    /// the next frame. With a profile's `views` or `onPress` it is the only way there; other pages
    /// turn by themselves, and go on doing so.
    void TurnPage() override {
        page_asked.store(true, std::memory_order_relaxed);
    }

private:
    enum class Turn { Host, Loop };

    GbaDsDriver(const DriverServices& services, const GbadsLoop& loop, const GbaDsLimits& limits);

    bool Start();
    void StopLoop();
    void Drop(const char* why);
    void Step(uint32_t keys);
    void DropSound();

    /// The loop's thread goes on until it comes back. False: it is still at it.
    bool Resume();
    bool WaitForLoop(std::chrono::milliseconds limit);

    // On the loop's thread
    void Body();
    bool Park();
    bool OnEvents(bool* turn_page);
    bool OnStep(uint32_t step_keys);
    bool OnPresent();

    void TakePictures();
    static bool Copy(const GbadsPicture& from, std::vector<uint32_t>& to);

    const DriverServices services;
    const GbadsLoop& loop;
    const GbaDsLimits limits;
    GbadsHost host{};
    std::thread thread;

    std::mutex mutex;
    std::condition_variable cond;
    Turn turn = Turn::Loop;           // under `mutex`
    bool ended = false;               // under `mutex`
    std::atomic<bool> leaving{false}; // set under `mutex`
    std::atomic<bool> aborting{false};
    std::atomic<bool> in_step{false}; // the core's own frame, not the loop's doing
    std::atomic<bool> page_asked{false};
    bool ready = false; // the loop's thread only

    // Handed over with the turn
    uint32_t keys = 0;
    bool stepped = false;
    bool presented = false;
    float rumble = 0.0f;
    std::vector<int16_t> audio;

    std::vector<int16_t> scratch;
    std::vector<uint32_t> primary;
    std::vector<uint32_t> secondary;
    uint32_t primary_w = 0;
    uint32_t primary_h = 0;
    uint32_t secondary_w = 0;
    uint32_t secondary_h = 0;
    bool has_secondary = false;
    uint32_t bad_frames = 0;
    bool late = false; ///< the loop has not come back
    bool holds_game = false;
};

} // namespace FrameCores
