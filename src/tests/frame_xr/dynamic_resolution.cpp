// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame menuux: Dynamic Resolution's decisions (frame_xr/dynamic_resolution.h): only when
// the GPU is the limit, one step at a time, only after seconds in a row, never up and down.
// AzaharFrame fixd: up by the measured cost, longer cool-downs, the level remembered per game.

#include <cmath>
#include <cstddef>
#include <map>
#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/settings.h"
#include "frame_xr/dynamic_resolution.h"
#include "video_core/vr_dynamic_resolution.h" // AzaharFrame ui2

using namespace FrameXr::DynamicResolution;

// AzaharFrame fixd: the tests link dynamic_resolution.cpp without the frame loop; the title ID it
// asks for by default (frame_xr.h, immersive.cpp in the app) is none here. The remembered-level
// tests set a LevelStore with their own.
namespace FrameXr {
uint64_t GameTitleId() {
    return 0;
}
} // namespace FrameXr

namespace {

constexpr double kPeriod = 1000.0 / 120.0;

Second Fine() {
    return {1.0, 120, kPeriod, 60, 60, 2.0, true};
}
Second GpuBehind() {
    return {1.0, 120, kPeriod, 50, 50, 25.0, true};
}
Second CpuBound() { // frames missed, the GPU with room: the emulation thread is late
    return {1.0, 100, kPeriod, 45, 45, 3.0, true};
}
Second Neutral() {
    return {1.0, 120, kPeriod, 60, 60, 6.0, true};
}

int Run(Controller& c, const Second& s, int seconds, int max_drop = 3) {
    int d = c.Drop();
    for (int i = 0; i < seconds; i++) {
        d = c.Judge(s, max_drop);
    }
    return d;
}

} // namespace

TEST_CASE("Dynamic Resolution: what a bad and a good second are", "[frame_xr]") {
    CHECK(Controller::Bad(GpuBehind()));
    CHECK_FALSE(Controller::Bad(CpuBound())); // a lower resolution would not help
    // Near the budget (8.3 ms at 120 Hz) is the limit already.
    CHECK(Controller::Bad({1.0, 120, kPeriod, 60, 60, 7.6, true}));
    CHECK_FALSE(Controller::Bad({1.0, 120, kPeriod, 60, 60, 7.0, true}));
    CHECK_FALSE(Controller::Bad(Fine()));
    CHECK(Controller{}.Good(Fine(), 3));
    CHECK_FALSE(Controller{}.Good(Neutral(), 3));
    // AzaharFrame flash3: room is the next factor's predicted cost, the time now times its pixels
    // over this factor's, under three quarters of the bad line (5.6 ms at 120 Hz). Session 573's
    // MK7 at 3x: 3.0 ms predicts 5.3 ms at 4x (room), 4.5 ms (its median) 8.0 ms (none).
    CHECK(std::abs(Controller{}.PredictUp(3.0, 3) - 3.0 * 16.0 / 9.0) < 1e-9);
    CHECK(Controller{}.Good({1.0, 120, kPeriod, 60, 60, 3.0, true}, 3));
    CHECK_FALSE(Controller{}.Good({1.0, 120, kPeriod, 60, 60, 3.2, true}, 3));
    CHECK_FALSE(Controller{}.Good({1.0, 120, kPeriod, 60, 60, 4.5, true}, 3));
    // The higher the factor, the smaller the step: 3.5 ms is room at 5x (to 6x: 5.0 ms) and 4x
    // (5.5 ms), not at 2x (7.9 ms).
    CHECK(Controller{}.Good({1.0, 120, kPeriod, 60, 60, 3.5, true}, 5));
    CHECK(Controller{}.Good({1.0, 120, kPeriod, 60, 60, 3.5, true}, 4));
    CHECK_FALSE(Controller{}.Good({1.0, 120, kPeriod, 60, 60, 3.5, true}, 2));
    // 1x to 2x is four times the pixels.
    CHECK(Controller{}.Good({1.0, 120, kPeriod, 60, 60, 1.3, true}, 1));
    CHECK_FALSE(Controller{}.Good({1.0, 120, kPeriod, 60, 60, 1.5, true}, 1));
    CHECK_FALSE(Controller{}.Good({1.0, 120, kPeriod, 60, 60, 1.5, true}, 0)); // as 1x
    CHECK_FALSE(Controller::Bad({1.0, 120, kPeriod, 60, 60, 2.9, true}));
    CHECK_FALSE(Controller::Bad(Neutral()));
    // The game's own rate does not count: a 30 fps game with the headset at full rate is fine.
    Second thirty = Fine();
    thirty.emu_frames = thirty.gpu_frames = 30;
    CHECK(Controller{}.Good(thirty, 3));
    // 90 Hz: 90 frames are all of them.
    CHECK(Controller{}.Good({1.0, 90, 1000.0 / 90.0, 30, 30, 2.0, true}, 3));
    // Too few timed frames say nothing either way.
    CHECK_FALSE(Controller{}.Good({1.0, 120, kPeriod, 60, 4, 1.0, true}, 3));
    CHECK(Controller::Bad({1.0, 90, 1000.0 / 90.0, 30, 30, 10.5, true}));
    // Too few timed frames to say the GPU is behind.
    CHECK_FALSE(Controller::Bad({1.0, 120, kPeriod, 60, 3, 30.0, true}));
}

TEST_CASE("Dynamic Resolution: CPU-bound seconds never lower it, 1x is the floor",
          "[frame_xr]") {
    Controller c;
    CHECK(Run(c, CpuBound(), 1000) == 0);
    // Internal Resolution 1x: nothing to take off however far behind the GPU is.
    CHECK(Run(c, GpuBehind(), 1000, 0) == 0);
}

TEST_CASE("Dynamic Resolution: down one step after bad seconds in a row", "[frame_xr]") {
    Controller c;
    CHECK(Run(c, GpuBehind(), Controller::kDownAfter - 1) == 0);
    CHECK(c.Judge(GpuBehind(), 3) == 1);
    // The seconds after a step are not judged (its own hitch).
    CHECK(Run(c, GpuBehind(), Controller::kSettle) == 1);
    // Still behind: another step, after as many bad seconds again.
    CHECK(Run(c, GpuBehind(), Controller::kDownAfter) == 2);
    // Never past the factor less 1.
    CHECK(Run(c, GpuBehind(), 100, 2) == 2);
}

TEST_CASE("Dynamic Resolution: a single bad second, or broken runs, change nothing",
          "[frame_xr]") {
    Controller c;
    for (int i = 0; i < 50; i++) {
        c.Judge(GpuBehind(), 3);
        c.Judge(GpuBehind(), 3);
        c.Judge(Fine(), 3);
    }
    CHECK(c.Drop() == 0);
}

TEST_CASE("Dynamic Resolution: up one step after many good seconds", "[frame_xr]") {
    Controller c;
    Run(c, GpuBehind(), Controller::kDownAfter + Controller::kSettle + Controller::kDownAfter);
    REQUIRE(c.Drop() == 2);
    Run(c, Fine(), Controller::kSettle + Controller::kHoldAfterDown);
    CHECK(Run(c, Fine(), Controller::kUpAfter - 1) == 2);
    CHECK(c.Judge(Fine(), 3) == 1);
    // A neutral second breaks the run.
    Run(c, Fine(), Controller::kSettle + Controller::kUpAfter - 1);
    c.Judge(Neutral(), 3);
    CHECK(Run(c, Fine(), Controller::kUpAfter - 1) == 1);
    CHECK(c.Judge(Fine(), 3) == 0);
    CHECK(Run(c, Fine(), 100) == 0); // never above the setting
}

TEST_CASE("Dynamic Resolution: a step up that cannot be held is not tried again soon",
          "[frame_xr]") {
    Controller c;
    Run(c, GpuBehind(), Controller::kDownAfter); // 1
    Run(c, Fine(), Controller::kSettle + Controller::kHoldAfterDown + Controller::kUpAfter); // 0
    REQUIRE(c.Drop() == 0);
    // Behind again at once: down, and the level is blocked.
    Run(c, Fine(), Controller::kSettle);
    Run(c, GpuBehind(), Controller::kDownAfter);
    REQUIRE(c.Drop() == 1);
    Run(c, Fine(), Controller::kSettle);
    CHECK(Run(c, Fine(), Controller::kRetryWait - 30) == 1);
    // After the wait it tries once more...
    int waited = 0;
    while (c.Drop() != 0 && waited < 100) {
        c.Judge(Fine(), 3);
        waited++;
    }
    CHECK(c.Drop() == 0);
    CHECK(waited <= 30);
    // ...AzaharFrame fixd: and when that fails too, it waits kRetryGrowth times as long...
    Run(c, GpuBehind(), Controller::kSettle + Controller::kDownAfter);
    REQUIRE(c.Drop() == 1);
    CHECK(c.Blocked(0));
    CHECK(Run(c, Fine(), Controller::kRetryWait * Controller::kRetryGrowth - 30) == 1);
    waited = 0;
    while (c.Drop() != 0 && waited < 100) {
        c.Judge(Fine(), 3);
        waited++;
    }
    CHECK(c.Drop() == 0);
    CHECK(waited <= 30);
    // ...and after the kFailsForGame-th failure not again in this game.
    Run(c, GpuBehind(), Controller::kSettle + Controller::kDownAfter);
    REQUIRE(c.Drop() == 1);
    CHECK(Run(c, Fine(), 5000) == 1);
    CHECK(c.Blocked(0));
    // A new game (no screens) starts from the setting.
    Second none = Fine();
    none.live = false;
    CHECK(c.Judge(none, 3) == 0);
}

// AzaharFrame ui2: after a step down it holds a few seconds before good seconds count, so it
// does not bounce straight back up; it can still go further down while it holds.
TEST_CASE("Dynamic Resolution: it holds after a step down", "[frame_xr]") {
    constexpr int kEarliestUp =
        Controller::kSettle + Controller::kHoldAfterDown + Controller::kUpAfter;
    Controller c;
    Run(c, GpuBehind(), Controller::kDownAfter);
    REQUIRE(c.Drop() == 1);
    CHECK(Run(c, Fine(), kEarliestUp - 1) == 1);
    CHECK(c.Judge(Fine(), 3) == 0);

    // Bad seconds while it holds still step it down.
    Controller d;
    Run(d, GpuBehind(), Controller::kDownAfter);
    Run(d, GpuBehind(), Controller::kSettle);
    REQUIRE(d.Drop() == 1);
    CHECK(Run(d, GpuBehind(), Controller::kDownAfter) == 2);

    // Neutral seconds in the hold do not shorten it: still the full run of good ones after.
    Controller e;
    Run(e, GpuBehind(), Controller::kDownAfter);
    Run(e, Neutral(), Controller::kSettle + Controller::kHoldAfterDown);
    CHECK(Run(e, Fine(), Controller::kUpAfter - 1) == 1);
    CHECK(e.Judge(Fine(), 3) == 0);

    // A pause does not use the hold up: only judged seconds count.
    Controller f;
    Run(f, GpuBehind(), Controller::kDownAfter);
    Second paused = Fine();
    paused.emu_frames = paused.gpu_frames = 0;
    Run(f, paused, 100);
    CHECK(Run(f, Fine(), kEarliestUp - 1) == 1);
    CHECK(f.Judge(Fine(), 3) == 0);
}

TEST_CASE("Dynamic Resolution: paused seconds are not judged", "[frame_xr]") {
    Controller c;
    Second paused = GpuBehind();
    paused.emu_frames = 0;
    paused.gpu_frames = 0;
    CHECK(Run(c, paused, 100) == 0);
    // A pause breaks a bad run too.
    c.Judge(GpuBehind(), 3);
    c.Judge(GpuBehind(), 3);
    c.Judge(paused, 3);
    CHECK(c.Judge(GpuBehind(), 3) == 0);
    // The factor went down under the steps taken: they follow it.
    Run(c, GpuBehind(), 3 * Controller::kDownAfter + 2 * Controller::kSettle);
    REQUIRE(c.Drop() == 3);
    CHECK(c.Judge(paused, 1) == 1);
    c.Reset();
    CHECK(c.Drop() == 0);
}

// AzaharFrame ui2: the owner's report 14: on at the start it worked; turned off in the game and
// on again it did not seem to. Off is the full factor at once; on again carries on at the level
// it had, and it still steps from there. The frame loop's side, with the setting.
TEST_CASE("Dynamic Resolution: off and on again in a game", "[frame_xr]") {
    namespace V = VideoCore::DynamicResolution;
    auto& setting = Settings::values.vr_dynamic_resolution;
    const bool was = setting.GetValue();
    setting.SetValue(true);
    // AzaharFrame fixa: Internal Resolution Auto, as this case was written for (the default is 4x
    // now, which would be the floor: no step under 4x).
    auto& factor = Settings::values.resolution_factor;
    const auto was_factor = factor.GetValue();
    factor.SetValue(0);
    V::Apply(6); // the renderer's factor: up to 5 steps
    Install();
    const auto run = [](const Second& s, int n) {
        for (int i = 0; i < n; i++) {
            NoteSecond(s);
        }
    };
    const auto drawn = [] { return V::Apply(6); };
    run(GpuBehind(), Controller::kDownAfter + Controller::kSettle + Controller::kDownAfter);
    REQUIRE(Drop() == 2);
    REQUIRE(drawn() == 4);

    // Off in the menu: the full factor at once.
    setting.SetValue(false);
    SetEnabled(false);
    CHECK(Drop() == 0);
    CHECK(drawn() == 6);
    run(GpuBehind(), 20); // nothing judged while it is off
    CHECK(drawn() == 6);

    // On again: where it was, and it carries on from there.
    setting.SetValue(true);
    SetEnabled(true);
    CHECK(Drop() == 2);
    CHECK(drawn() == 4);
    run(GpuBehind(), Controller::kSettle); // the step's own hitch is not judged
    CHECK(Drop() == 2);
    run(GpuBehind(), Controller::kDownAfter);
    CHECK(Drop() == 3);
    CHECK(drawn() == 3);

    // Changed elsewhere (Azahar's window): NoteSecond sees it the same way.
    setting.SetValue(false);
    run(Fine(), 1);
    CHECK(drawn() == 6);
    setting.SetValue(true);
    run(GpuBehind(), 1);
    CHECK(Drop() == 3);

    // Without anything learnt (a new game) it starts from the setting and still steps down.
    Reset();
    setting.SetValue(false);
    SetEnabled(false);
    setting.SetValue(true);
    SetEnabled(true);
    CHECK(Drop() == 0);
    run(GpuBehind(), Controller::kDownAfter);
    CHECK(Drop() == 1);
    // A game started after it was turned off forgets the old level.
    setting.SetValue(false);
    SetEnabled(false);
    Reset();
    setting.SetValue(true);
    SetEnabled(true);
    CHECK(Drop() == 0);

    Uninstall();
    setting.SetValue(was);
    factor.SetValue(was_factor);
    V::g_base = 0;
}

// AzaharFrame flash3 (report 70, session 573): "3 bad seconds in a row" was not enforced: an
// ordinary second left the bad count as it was, and the step to 3x counted a bad second from
// 24 s before. Any second that is not bad now ends the run.
TEST_CASE("Dynamic Resolution: bad seconds must come in a row", "[frame_xr]") {
    Controller c;
    // Bad, bad, an ordinary second (neither bad nor good), bad: no step.
    for (int i = 0; i < 50; i++) {
        c.Judge(GpuBehind(), 3);
        c.Judge(GpuBehind(), 3);
        c.Judge(Neutral(), 3);
    }
    CHECK(c.Drop() == 0);
    // A CPU-bound second, or one with too few timed frames, ends the run too.
    Second few = GpuBehind();
    few.gpu_frames = 3;
    for (int i = 0; i < 50; i++) {
        c.Judge(GpuBehind(), 3);
        c.Judge(GpuBehind(), 3);
        c.Judge(i % 2 ? CpuBound() : few, 3);
    }
    CHECK(c.Drop() == 0);
    // Session 573's 4x stretch before the step to 3x: a bad second every few seconds among
    // ordinary ones (5-6 ms, the 4x median 5.1) is not a GPU that cannot keep up.
    Controller d;
    const double seconds[] = {5.1, 7.9, 5.0, 5.3, 6.1, 4.8, 8.1, 5.2, 5.6, 4.9, 5.4, 7.7,
                              5.0, 5.2, 6.0, 4.7, 5.5, 5.1, 8.4, 5.3, 4.9, 5.2, 9.4, 5.0};
    for (int round = 0; round < 10; round++) {
        for (const double ms : seconds) {
            d.Judge({1.0, 120, kPeriod, 60, 60, ms, true, 4}, 5);
        }
    }
    CHECK(d.Drop() == 0);
    // kDownAfter in a row still step it down.
    for (int i = 0; i < Controller::kDownAfter; i++) {
        d.Judge({1.0, 120, kPeriod, 60, 60, 9.0, true, 4}, 5);
    }
    CHECK(d.Drop() == 1);
}

namespace {

/// A GPU that takes `cost[factor]` ms a frame (the factor drawn from the controller's steps under
/// `base`); returns the factors it drew, one per second.
std::vector<int> Model(Controller& c, int base, const double* cost, int seconds) {
    std::vector<int> drawn;
    for (int i = 0; i < seconds; i++) {
        const int factor = base - c.Drop();
        c.Judge({1.0, 120, kPeriod, 60, 60, cost[factor], true, factor}, base - 1);
        drawn.push_back(base - c.Drop());
    }
    return drawn;
}

int Changes(const std::vector<int>& v) {
    int n = 0;
    for (std::size_t i = 1; i < v.size(); i++) {
        n += v[i] != v[i - 1];
    }
    return n;
}

} // namespace

// AzaharFrame flash3: a step up is judged by its predicted cost (the GPU's time times the next
// factor's pixels over this one's) against three quarters of the bad line, so it climbs back
// where the GPU has room, and the failed-step blocking keeps it from going up and down.
TEST_CASE("Dynamic Resolution: it climbs back where the GPU allows, without bouncing",
          "[frame_xr]") {
    // Session 573's MK7 at 120 Hz, the median per factor (6x over the budget).
    const double mk7[] = {0.0, 2.0, 2.6, 3.0, 5.1, 6.0, 10.6};
    {
        // From 3x (where the old rule left it): up to 4x, where 5.1 ms predicts 8.0 ms at 5x
        // (no room), and it stays there.
        Controller c;
        Model(c, 6, mk7, 0);
        const double heavy[] = {0.0, 9, 9, 9, 9, 9, 9}; // down to 3x first
        Model(c, 6, heavy, 3 * (Controller::kDownAfter + Controller::kSettle));
        REQUIRE(6 - c.Drop() == 3);
        const auto drawn = Model(c, 6, mk7, 1000);
        CHECK(drawn.back() == 4);
        CHECK(Changes(drawn) == 1);
    }
    {
        // A GPU whose cost jumps past the pixels' prediction at 6x (5x has room by the pixels,
        // 6x is over the budget). flash3 tried 6x again after kRetryWait and once more (5
        // changes). AzaharFrame fixd: the first step down's run was the GPU behind (an upper
        // bound only), so 6x is tried once; that step up measures 6x against 5x, and it stays at
        // 5x.
        const double jump[] = {0.0, 1.0, 1.5, 2.0, 2.5, 3.5, 10.0};
        Controller c;
        const auto drawn = Model(c, 6, jump, 2000);
        CHECK(drawn.back() == 5);
        CHECK(Changes(drawn) == 3); // 6 -> 5, the one try: 5 -> 6 -> 5
        CHECK(c.Ratio(5) > Controller::PixelRatio(5));
        CHECK_FALSE(c.Good({1.0, 120, kPeriod, 60, 60, 3.5, true, 5}, 5));
    }
    {
        // The same from 5x, where the pixels say there is room: one try, then the measured
        // ratio keeps it at 5x (no second try, no bouncing).
        const double jump[] = {0.0, 1.0, 1.5, 2.0, 2.5, 3.5, 10.0};
        Controller c;
        const double heavy[] = {0.0, 9, 9, 9, 9, 9, 9};
        Model(c, 6, heavy, Controller::kDownAfter); // to 5x, measured at the bad line only
        REQUIRE(6 - c.Drop() == 5);
        const auto drawn = Model(c, 6, jump, 3000);
        int ups = 0;
        for (std::size_t i = 1; i < drawn.size(); i++) {
            ups += drawn[i] > drawn[i - 1];
        }
        CHECK(ups <= 1);
        CHECK(drawn.back() == 5);
    }
    {
        // A light game: back to the full factor and no change after.
        const double light[] = {0.0, 0.5, 0.8, 1.2, 1.8, 2.6, 3.5};
        Controller c;
        Model(c, 6, mk7, 60);
        REQUIRE(c.Drop() >= 1);
        const auto drawn = Model(c, 6, light, 600);
        CHECK(drawn.back() == 6);
        CHECK(c.Drop() == 0);
    }
}

// AzaharFrame flash3: the floor. The Internal Resolution setting itself (kFloorAtSetting, the
// owner's choice): only the immersive modes' two extra steps come off. 1.1.1's rule (false) went
// down to 1x, under the setting in the immersive modes.
TEST_CASE("Dynamic Resolution: the floor", "[frame_xr]") {
    // 1.1.1's rule.
    CHECK(MaxDrop(6, 4, false) == 5); // immersive over a 4x setting: down to 1x
    CHECK(MaxDrop(4, 4, false) == 3);
    CHECK(MaxDrop(6, 0, false) == 5); // Auto
    CHECK(MaxDrop(3, 1, false) == 0); // the setting at 1x: nothing to do (the row is locked)
    CHECK(MaxDrop(1, 0, false) == 0);
    CHECK(MaxDrop(0, 4, false) == 0); // the renderer's factor not known yet
    // The rule now: never under the setting; only the immersive modes' steps come off.
    CHECK(MaxDrop(6, 4, true) == 2);
    CHECK(MaxDrop(4, 4, true) == 0); // outside immersive it has nothing to do
    CHECK(MaxDrop(6, 0, true) == 5); // Auto: no setting to stop at
    CHECK(MaxDrop(3, 1, true) == 0);
    CHECK(MaxDrop(8, 7, true) == 1); // immersive capped at 8x
    CHECK(FloorFactor(6, 4, true) == 4);
    CHECK(FloorFactor(6, 4, false) == 1);
    // The default is the setting.
    CHECK(kFloorAtSetting);
    CHECK(MaxDrop(6, 4) == 2);
    CHECK(FloorFactor(6, 4) == 4);
}

// AzaharFrame flash3: session 573's case through the frame loop's side: Internal Resolution 4x,
// immersive (the renderer draws 6x), the GPU far behind: 6x, 5x, 4x and no further.
TEST_CASE("Dynamic Resolution: never under the Internal Resolution setting", "[frame_xr]") {
    namespace V = VideoCore::DynamicResolution;
    auto& setting = Settings::values.vr_dynamic_resolution;
    auto& factor = Settings::values.resolution_factor;
    const bool was = setting.GetValue();
    const auto was_factor = factor.GetValue();
    setting.SetValue(true);
    factor.SetValue(4);
    V::Apply(6);
    Install();
    for (int i = 0; i < 200; i++) {
        NoteSecond(GpuBehind());
    }
    CHECK(Drop() == 2);
    CHECK(V::Apply(6) == 4);
    // Outside immersive the renderer's factor is the setting: nothing to take off.
    Reset();
    V::Apply(4);
    for (int i = 0; i < 200; i++) {
        NoteSecond(GpuBehind());
    }
    CHECK(Drop() == 0);
    CHECK(V::Apply(4) == 4);
    Uninstall();
    setting.SetValue(was);
    factor.SetValue(was_factor);
    V::g_base = 0;
}

// AzaharFrame fixd (session 651, the owner: it steps down too quickly): five bad seconds in a row,
// not three.
TEST_CASE("Dynamic Resolution: slower down", "[frame_xr][fixd]") {
    CHECK(Controller::kDownAfter == 5);
    Controller c;
    CHECK(Run(c, GpuBehind(), 4) == 0);
    CHECK(c.Judge(GpuBehind(), 3) == 1);
    CHECK(c.LastStep() == 1);
    // four bad seconds and an ordinary one, again and again: never
    Controller d;
    for (int i = 0; i < 40; i++) {
        Run(d, GpuBehind(), 4);
        d.Judge(Neutral(), 3);
    }
    CHECK(d.Drop() == 0);
}

// AzaharFrame fixd: the next factor's cost from the ratio measured in this game where it has
// stepped between the two; the pixels before that.
TEST_CASE("Dynamic Resolution: up on the measured cost", "[frame_xr][fixd]") {
    Controller fresh;
    CHECK(fresh.Ratio(3) == Controller::PixelRatio(3)); // nothing measured: the pixels
    CHECK(fresh.MeasuredUp(3) == 0.0);
    CHECK(fresh.Cost(3) == 0.0);
    CHECK(std::abs(fresh.PredictUp(3.0, 3) - 3.0 * 16.0 / 9.0) < 1e-9);
    CHECK(std::abs(Controller::PixelRatio(4) - 25.0 / 16.0) < 1e-9);

    // A heavy scene: 4x at 8.0 ms (over the bad line), 3x at 7.0 ms: down to 3x after five bad
    // seconds; the seconds after the settle measure 3x in the same scene: x1.14.
    const auto at = [](double ms, int f) { return Second{1.0, 120, kPeriod, 60, 60, ms, true, f}; };
    Controller c;
    Run(c, at(8.0, 4), Controller::kDownAfter, 1);
    REQUIRE(c.Drop() == 1);
    Run(c, at(7.0, 3), Controller::kSettle + Controller::kMeasureSeconds, 1);
    CHECK(std::abs(c.MeasuredDown(3) - 8.0 / 7.0) < 1e-6);
    CHECK(std::abs(c.Ratio(3) - 8.0 / 7.0) < 1e-6);
    // A light scene at 3x, 4.0 ms: the pixels predict 7.1 ms at 4x (no room), the measured ratio
    // 4.6 ms (room): it climbs back.
    CHECK_FALSE(fresh.Good(at(4.0, 3), 3));
    CHECK(c.Good(at(4.0, 3), 3));
    // (the hold's first seconds went by in the measurement)
    CHECK(Run(c, at(4.0, 3),
              Controller::kHoldAfterDown - Controller::kMeasureSeconds + Controller::kUpAfter, 1) == 0);
    CHECK(c.LastStep() == -1);

    // A step up measures both ways. MK7: 4x at 3.4 ms (5.3 ms by the pixels: room) up to 5x,
    // which takes 6.0 ms there (under the line): x1.76, more than the pixels' x1.56, so the next
    // time 4x at 3.4 ms has no room (6.0 ms), 3.0 ms still has.
    Controller k;
    k.Start(1);
    Run(k, at(3.4, 4), Controller::kSettle + Controller::kHoldAfterDown + Controller::kUpAfter, 1);
    REQUIRE(k.Drop() == 0);
    REQUIRE(k.LastStep() == -1);
    Run(k, at(6.0, 5), Controller::kSettle + Controller::kMeasureSeconds, 1);
    CHECK(std::abs(k.MeasuredUp(4) - 6.0 / 3.4) < 1e-6);
    CHECK(std::abs(k.Ratio(4) - 6.0 / 3.4) < 1e-6);
    CHECK(fresh.Good(at(3.4, 4), 4));
    CHECK_FALSE(k.Good(at(3.4, 4), 4));
    CHECK(k.Good(at(3.0, 4), 4));
    // ... up to 1.25 times the pixels' (a scene that got heavier under the measurement)
    Controller u;
    u.Start(1);
    Run(u, at(2.0, 4), Controller::kSettle + Controller::kHoldAfterDown + Controller::kUpAfter, 1);
    REQUIRE(u.Drop() == 0);
    Run(u, at(7.0, 5), Controller::kSettle + Controller::kMeasureSeconds, 1);
    CHECK(std::abs(u.MeasuredUp(4) - Controller::kRatioOverPixels * Controller::PixelRatio(4)) < 1e-6);

    // A step down's run is the GPU behind (the cost plus the wait): a scene that got lighter
    // right after it (the race ended) says nothing, it never makes the step up dearer than the
    // pixels say.
    Controller e;
    Run(e, at(9.0, 5), Controller::kDownAfter, 1);
    Run(e, at(1.0, 4), Controller::kSettle + Controller::kMeasureSeconds, 1);
    CHECK(e.MeasuredDown(4) == 0.0);
    CHECK(e.Ratio(4) == Controller::PixelRatio(4));
    CHECK(e.Good(at(2.0, 4), 4)); // a light scene climbs as by the pixels
    CHECK_FALSE(e.Good(at(3.8, 4), 4));
    // ... and it never goes under a step up's own measurement: k measured x1.76 going up, then
    // a step down whose 4x is 5.0 ms against 5x's 7.6 (x1.52 at most).
    Run(k, at(7.6, 5), Controller::kDownAfter, 1);
    REQUIRE(k.Drop() == 1);
    Run(k, at(5.0, 4), Controller::kSettle + Controller::kMeasureSeconds, 1);
    CHECK(std::abs(k.MeasuredDown(4) - 7.6 / 5.0) < 1e-6);
    CHECK(std::abs(k.Ratio(4) - 6.0 / 3.4) < 1e-6);

    // Both at the bad line says nothing: no ratio.
    Controller b;
    Run(b, at(9.0, 5), Controller::kDownAfter, 1);
    Run(b, at(8.0, 4), Controller::kSettle + Controller::kMeasureSeconds, 1);
    CHECK(b.MeasuredDown(4) == 0.0);
    CHECK(b.MeasuredUp(4) == 0.0);
}

// AzaharFrame fixd: the medians per factor. Session 651's Majora's Mask: 6x at 3.4 ms most of the
// time, a heavy stretch steps it down, then 5x at 5.4 ms: by the pixels 7.8 ms at 6x (no room,
// so it stayed at 5x for good), by the medians 5.4 ms (room): it comes back.
TEST_CASE("Dynamic Resolution: up on the medians per factor", "[frame_xr][fixd]") {
    const auto at = [](double ms, int f) { return Second{1.0, 120, kPeriod, 60, 60, ms, true, f}; };
    Controller c;
    Run(c, at(3.4, 6), 60, 2);
    CHECK(c.Cost(6) == static_cast<float>(3.4));
    Run(c, at(9.0, 6), Controller::kDownAfter, 2);
    REQUIRE(c.Drop() == 1);
    CHECK(c.Cost(6) == static_cast<float>(3.4)); // a median: five heavy seconds do not move it
    CHECK(c.MeasuredDown(5) == 0.0);             // 9.0 against 5.4: more than the pixels say
    Controller pixels;
    CHECK_FALSE(pixels.Good(at(5.4, 5), 5));
    // its first seconds at 5x: not known yet (kCostSeconds), then the medians' x1 (never under 1)
    int up_after = 0;
    for (int t = 1; t <= 60 && c.Drop() == 1; t++) {
        if (t == Controller::kSettle + Controller::kCostSeconds - 1) {
            CHECK(c.Cost(5) == 0.0);
        }
        c.Judge(at(5.4, 5), 2);
        up_after = t;
    }
    CHECK(c.Drop() == 0);
    CHECK(c.LastStep() == -1);
    CHECK(up_after == Controller::kSettle + Controller::kCostSeconds - 1 + Controller::kUpAfter);
    CHECK(c.Ratio(5) == Controller::kRatioMin);
    CHECK(c.RatioWhy(5) == "x1.00 by the medians of 5x and 6x");

    // A Load in the same game (session 651 loaded a state 17 s after that step down) starts at
    // the remembered 5x and keeps the medians: it comes back as soon as it may.
    c.Start(1, true);
    CHECK(c.Cost(6) == static_cast<float>(3.4));
    CHECK(c.Ratio(5) == Controller::kRatioMin);
    CHECK(Run(c, at(5.4, 5), Controller::kSettle + Controller::kHoldAfterDown + Controller::kUpAfter - 1, 2) == 1);
    CHECK(c.Judge(at(5.4, 5), 2) == 0);
    // another game starts from nothing
    c.Start(1);
    CHECK(c.Cost(6) == 0.0);
    CHECK(c.Ratio(5) == Controller::PixelRatio(5));

    // The medians never make it dearer than the pixels: 6x heavy all along (session 651's
    // 0004000000054000: 14.8 ms at 6x, 4.1 ms at 5x).
    Controller h;
    Run(h, at(14.8, 6), Controller::kDownAfter, 2);
    Run(h, at(3.0, 5), 30, 2);
    CHECK(h.CostRatio(5) == 0.0); // 6x has five seconds: not known
    CHECK(h.Ratio(5) == Controller::PixelRatio(5));
}

// AzaharFrame fixd: a failed level waits longer each time (no oscillation): kRetryWait, then
// kRetryGrowth times that, then the rest of the game.
TEST_CASE("Dynamic Resolution: a failed level waits longer each time", "[frame_xr][fixd]") {
    CHECK(Controller::kRetryWait == 180);
    CHECK(Controller::kFailWindow == 60);
    // 5x is over the line in this scene, 4x at 2.5 ms; the measured ratio is kept at 1.25 times
    // the pixels' (2.5 ms predicts 4.9 ms: room by the rule), so only the waits keep it from
    // bouncing.
    const auto at = [](double ms, int f) { return Second{1.0, 120, kPeriod, 60, 60, ms, true, f}; };
    Controller c;
    std::vector<int> ups_at;
    int t = 0;
    for (; t < 3000; t++) {
        const int f = 5 - c.Drop();
        const int before = c.Drop();
        c.Judge(at(f == 5 ? 9.0 : 2.5, f), 1);
        if (c.Drop() < before) {
            ups_at.push_back(t);
        }
    }
    REQUIRE(ups_at.size() == 3);
    // the waits between the tries grow: 180 s, then 540 s; after the third failure none
    const int gap1 = ups_at[1] - ups_at[0], gap2 = ups_at[2] - ups_at[1];
    CHECK(gap1 >= Controller::kRetryWait);
    CHECK(gap2 >= Controller::kRetryWait * Controller::kRetryGrowth);
    CHECK(gap2 > gap1 * 2);
    CHECK(c.Drop() == 1);
    CHECK(c.Blocked(0));
}

// AzaharFrame fixd: where a game starts: the level remembered for it, never above the ceiling nor
// under the floor.
TEST_CASE("Dynamic Resolution: the start from a remembered level", "[frame_xr][fixd]") {
    CHECK(StartDrop(6, 4, 2) == 2);
    CHECK(StartDrop(6, 5, 2) == 1);
    CHECK(StartDrop(6, 0, 2) == 0); // none remembered: the ceiling
    CHECK(StartDrop(6, 6, 2) == 0);
    CHECK(StartDrop(6, 7, 2) == 0); // remembered above today's ceiling
    CHECK(StartDrop(6, 3, 2) == 2); // under today's floor (a higher setting now): the floor
    CHECK(StartDrop(4, 3, 0) == 0); // outside the Full modes nothing comes off
    CHECK(StartDrop(0, 4, 0) == 0); // the renderer's factor not known yet
    Controller c;
    c.Start(2);
    CHECK(c.Drop() == 2);
    CHECK(c.LastStep() == 0);
    // its first seconds are not judged (the step's own hitch), and good ones wait a hold
    CHECK(Run(c, GpuBehind(), Controller::kSettle) == 2);
    CHECK(Run(c, Fine(), Controller::kHoldAfterDown + Controller::kUpAfter - 1) == 2);
    CHECK(c.Judge(Fine(), 3) == 1);
    c.Start(0);
    CHECK(c.Drop() == 0);
}

TEST_CASE("Dynamic Resolution: the remembered levels' file", "[frame_xr][fixd]") {
    const LevelList levels{{{0x0004000000030800ULL, false}, 4},
                           {{0x0004000000030800ULL, true}, 3},
                           {{0x0004000000125600ULL, false}, 5}};
    const std::string text = FormatStore(levels);
    CHECK(text == "0004000000030800 4\n0004000000030800 3 bfi\n0004000000125600 5\n");
    CHECK(ParseStore(text) == levels);
    // AzaharFrame kiuhs3a: the 60 FPS levels' lines
    const uint64_t kiu = 0x0004000000030200ULL;
    const LevelList four{{{kiu, 0}, 5}, {{kiu, kLevelBfi}, 4}, {{kiu, kLevelSixty}, 3},
                         {{kiu, kLevelBfi | kLevelSixty}, 2}};
    const std::string words = FormatStore(four);
    CHECK(words == "0004000000030200 5\n0004000000030200 4 bfi\n0004000000030200 3 60\n"
                   "0004000000030200 2 bfi60\n");
    CHECK(ParseStore(words) == four);
    CHECK(ParseStore("0004000000030200 3 6\n0004000000030200 3 600\n").empty());
    // lines this build cannot read are left out; the last line for a game wins
    const auto parsed = ParseStore("# a comment\n0004000000030800 4\nzz 3\n0004000000030800 5\n"
                                   "0000000000000000 4\n0004000000054000 0\n0004000000054000 99\n"
                                   "0004000000054000 4 extra\n000400000003080 4\n"
                                   "0004000000054000 4 bfi more\n\n");
    CHECK(parsed == LevelList{{{0x0004000000030800ULL, false}, 5}});
    CHECK(FormatStore({{{0, false}, 4}}).empty());
}

// AzaharFrame fixd: through the frame loop's side: a level stepped down to is remembered for the
// game, a Load (the game's screens off) starts there, not at the ceiling, and is no step up; the
// next boot (a new frame loop) reads it back.
TEST_CASE("Dynamic Resolution: the level remembered per game", "[frame_xr][fixd]") {
    namespace V = VideoCore::DynamicResolution;
    auto& setting = Settings::values.vr_dynamic_resolution;
    auto& factor = Settings::values.resolution_factor;
    const bool was = setting.GetValue();
    const auto was_factor = factor.GetValue();
    setting.SetValue(true);
    factor.SetValue(4);
    std::map<LevelKey, int> disk;
    int saves = 0;
    uint64_t title = 0x0004000000030800ULL;
    const auto store = [&] {
        LevelStore s;
        s.load = [&] { return LevelList(disk.begin(), disk.end()); };
        s.save = [&](LevelList levels) {
            disk = std::map<LevelKey, int>(levels.begin(), levels.end());
            saves++;
        };
        s.title = [&] { return title; };
        return s;
    };
    SetLevelStore(store());
    V::Apply(6); // a Full mode over 4x
    Install();
    const auto run = [](const Second& s, int n) {
        for (int i = 0; i < n; i++) {
            NoteSecond(s);
        }
    };
    Second off = Fine();
    off.live = false;
    run(GpuBehind(), Controller::kDownAfter);
    CHECK(Drop() == 1);
    CHECK(RememberedFactor(title) == 5); // stepped down to it: remembered at once
    CHECK(disk[{title, false}] == 5);
    run(GpuBehind(), Controller::kSettle + Controller::kDownAfter);
    CHECK(Drop() == 2);
    CHECK(disk[{title, false}] == 4);
    // A Load: the screens go off. It starts at 4x again, not at 6x.
    run(off, 3);
    CHECK(Drop() == 2);
    CHECK(V::Apply(6) == 4);
    run(Fine(), 1);
    CHECK(Drop() == 2);
    // Another game with nothing remembered starts at the ceiling.
    title = 0x0004000000054000ULL;
    run(off, 1);
    CHECK(Drop() == 0);
    // A level held kRememberAfter judged seconds is remembered too (6x here).
    run(Neutral(), kRememberAfter + 1);
    CHECK(RememberedFactor(title) == 6);
    Uninstall();

    // The next boot: a new frame loop reads the levels back, and a game already running when VR
    // starts goes to its level at the first second.
    title = 0x0004000000030800ULL;
    SetLevelStore(store());
    CHECK(RememberedFactor(title) == 4);
    Install();
    run(Neutral(), 1);
    CHECK(Drop() == 2);
    // A new Internal Resolution starts from the setting (the player's choice), not the level.
    Reset();
    CHECK(Drop() == 0);
    run(Neutral(), 1);
    CHECK(Drop() == 0);
    Uninstall();

    // Turned off in one game (6x), another game started meanwhile, turned on: the kept copy was
    // the other game's: this game's remembered level (4x).
    title = 0x0004000000054000ULL;
    Install();
    run(Neutral(), 1);
    REQUIRE(Drop() == 0);
    setting.SetValue(false);
    SetEnabled(false);
    run(off, 1);
    title = 0x0004000000030800ULL;
    run(off, 1);
    run(Fine(), 1);
    setting.SetValue(true);
    SetEnabled(true);
    run(Neutral(), 1);
    CHECK(Drop() == 2);
    Uninstall();

    // No title ID (homebrew): nothing remembered, nothing read.
    title = 0;
    const int saved_before = saves;
    Install();
    run(GpuBehind(), Controller::kDownAfter);
    CHECK(Drop() == 1);
    CHECK(saves == saved_before);
    Uninstall();

    SetLevelStore({}); // nothing for the other tests
    setting.SetValue(was);
    factor.SetValue(was_factor);
    V::g_base = 0;
}

// AzaharFrame bfidr: while Black Frame Insertion flashes in a Full mode, a picture late is lit
// again and doubles: the symptoms count as well as the GPU's line (BfiRules), the screen's ceiling
// (ScreenFactor) too; the floor stays the Internal Resolution setting.
namespace {

Second Bfi(double gpu_ms, uint32_t late = 0, uint32_t lit = 0, double end_ms = 1.0,
           double period = kPeriod) {
    Second s{1.0, static_cast<uint32_t>(1000.0 / period + 0.5), period, 60, 60, gpu_ms, true};
    s.bfi = true;
    s.bfi_asked = true;
    s.late_releases = late;
    s.late_lit = lit;
    s.end_frame_max_ms = end_ms;
    return s;
}

int Steps(Controller& c, const Second& s, int seconds, int max_drop = 2, int min_drop = 0) {
    int steps = 0;
    for (int i = 0; i < seconds; i++) {
        c.Judge(s, max_drop, min_drop);
        steps += c.LastStep() != 0 ? 1 : 0;
    }
    return steps;
}

} // namespace

TEST_CASE("Dynamic Resolution: Black Frame Insertion's symptoms window", "[frame_xr][bfidr]") {
    SymptomWindow w;
    // One late release a second is not "more than about one a second": 10 in 10 s is not a trend.
    for (int i = 0; i < BfiRules::kWindow; i++) {
        w.Add(Bfi(6.0, 1));
    }
    CHECK(w.LateReleases() == 10);
    CHECK_FALSE(w.Symptoms());
    w.Add(Bfi(6.0, 2)); // the oldest second leaves: 11 in the last 10 s
    CHECK(w.LateReleases() == 11);
    CHECK(w.Symptoms());
    // A burst of one second (the pacing's relock after the VR menu: 4-6) in a short window is no
    // trend either: the count is over the whole window, however many seconds are in.
    w.Clear();
    w.Add(Bfi(6.0, 6));
    w.Add(Bfi(6.0, 3));
    w.Add(Bfi(6.0, 0));
    CHECK_FALSE(w.Symptoms());
    // Late pictures lit again count the same way, on their own.
    w.Clear();
    for (int i = 0; i < 4; i++) {
        w.Add(Bfi(6.0, 0, 3));
    }
    CHECK(w.LateLit() == 12);
    CHECK(w.Symptoms());
    // Two seconds with an xrEndFrame over 1.44 periods (12 ms at 120 Hz); one is a single miss.
    w.Clear();
    w.Add(Bfi(6.0, 0, 0, 12.5));
    CHECK_FALSE(w.Symptoms());
    w.Add(Bfi(6.0, 0, 0, 11.5));
    CHECK_FALSE(w.Symptoms());
    w.Add(Bfi(6.0, 0, 0, 14.0));
    CHECK(w.SlowSeconds() == 2);
    CHECK(w.Symptoms());
    // At 90 Hz the line is 1.44 periods too (16 ms): 14 ms is no miss there.
    constexpr double k90 = 1000.0 / 90.0;
    w.Clear();
    for (int i = 0; i < 3; i++) {
        w.Add(Bfi(6.0, 0, 0, 14.0, k90));
    }
    CHECK(w.SlowSeconds() == 0);
    CHECK(SymptomWindow::Clean(Bfi(6.0, 1, 1, 14.0, k90)));
    CHECK_FALSE(SymptomWindow::Clean(Bfi(6.0, 1, 1, 14.0)));
    CHECK_FALSE(SymptomWindow::Clean(Bfi(6.0, 2)));
    CHECK_FALSE(SymptomWindow::Clean(Bfi(6.0, 0, 2)));
    CHECK(SymptomWindow::Clean(Bfi(6.0, 1, 1)));
    // Display frames under 118 of 120 (refreshes the loop never ended) are a miss too.
    Second short_second = Bfi(6.0);
    short_second.display_frames = 110;
    CHECK_FALSE(SymptomWindow::NoMiss(short_second));
    short_second.display_frames = 118;
    CHECK(SymptomWindow::NoMiss(short_second));
    Second short90 = Bfi(6.0, 0, 0, 1.0, k90);
    short90.display_frames = 87; // 88.5 is the line at 90 Hz
    CHECK_FALSE(SymptomWindow::NoMiss(short90));
    w.Clear();
    short_second.display_frames = 101; // report 85 at 8x drawn: 67-101 a second
    w.Add(short_second);
    w.Add(short_second);
    CHECK(w.Symptoms());
}

// AzaharFrame bfidr: report 85, the owner's headset session (OoT mode 2, Black Frame Insertion on
// quads, its rhythm perfect throughout): the owner saw doubling at 5x drawn in heavy rooms (GPU
// behind 6.6-8.6 ms, no late releases) and at 6x; "almost" at 4x drawn, with nothing in the log.
TEST_CASE("Dynamic Resolution: with Black Frame Insertion, report 85's seconds",
          "[frame_xr][bfidr]") {
    struct Row {
        double gpu;
        uint32_t late;
    };
    // 5x drawn (a 3x setting), the heavy rooms from t=377.
    const Row heavy[] = {{8.3, 7}, {8.1, 0}, {8.2, 0}, {6.6, 0}, {8.6, 1}};
    Controller c;
    int at = 0;
    for (const Row& r : heavy) {
        at++;
        if (c.Judge(Bfi(r.gpu, r.late), 3) == 1) {
            break;
        }
    }
    CHECK(c.Drop() == 1);
    CHECK(at == 3);
    // Without Black Frame Insertion the same seconds are no step (fixd: 5 over 7.5 ms in a row).
    Controller off;
    for (const Row& r : heavy) {
        Second s = Bfi(r.gpu, r.late);
        s.bfi = false;
        off.Judge(s, 3);
    }
    CHECK(off.Drop() == 0);
    // 4x drawn (a 2x setting), t=476-495: nothing to see, nothing done. The floor (the setting)
    // is not what holds it there: with the floor at 1x it would not step either.
    const double light[] = {5.3, 5.7, 4.7, 4.8, 4.6, 5.2, 5.6, 5.6, 5.0, 4.6,
                            5.0, 4.7, 4.8, 5.9, 5.4, 5.4, 5.6, 4.7, 4.9, 6.0};
    Controller l;
    for (int round = 0; round < 10; round++) {
        for (const double g : light) {
            l.Judge(Bfi(g, 0), 3);
        }
    }
    CHECK(l.Drop() == 0);
}

TEST_CASE("Dynamic Resolution: with Black Frame Insertion, down on the symptoms after 3 seconds",
          "[frame_xr][bfidr]") {
    CHECK(BfiRules::kDownAfter == 3);
    CHECK(Controller::kDownAfter == 5); // fixd's, for the GPU's line alone
    // MK7 at 5x drawn (report 76: GPU behind about 6 ms, under the 7.5 ms line), 3 late releases
    // a second: the window has more than 10 in its fourth second; 3 such seconds in a row.
    Controller c;
    CHECK_FALSE(Controller::Bad(Bfi(6.0, 3)));
    CHECK(Run(c, Bfi(6.0, 3), 5) == 0);
    CHECK(c.Judge(Bfi(6.0, 3), 2) == 1);
    CHECK(c.LastStep() == 1);
    CHECK(c.LastStepSymptoms());
    CHECK(c.StepWindow().LateReleases() == 18);
    // The same seconds without Black Frame Insertion: nothing (the GPU has room by fixd's line).
    Controller off;
    Second plain = Bfi(6.0, 3);
    plain.bfi = false;
    CHECK(Run(off, plain, 200) == 0);
    // Late pictures lit again, or the compositor's misses, the same.
    Controller lit;
    CHECK(Run(lit, Bfi(6.0, 0, 4), 5) == 1);
    Controller slow;
    CHECK(Run(slow, Bfi(6.0, 0, 0, 13.0), 4) == 1); // the 2nd second fills the window, 3 more
    // The stricter line alone (0.84 of the period, 7.0 ms at 120 Hz): 3 seconds too.
    Controller line;
    CHECK(Run(line, Bfi(7.2), 2) == 0);
    CHECK(line.Judge(Bfi(7.2), 2) == 1);
    CHECK(line.LastStepSymptoms());
    // Without Black Frame Insertion: fixd's line (7.5 ms) and 5 seconds.
    Second p72 = Bfi(7.2), p80 = Bfi(8.0);
    p72.bfi = p80.bfi = false;
    Controller plain_line;
    CHECK(Run(plain_line, p72, 100) == 0);
    CHECK(Run(plain_line, p80, 4) == 0);
    CHECK(plain_line.Judge(p80, 2) == 1);
    CHECK_FALSE(plain_line.LastStepSymptoms());
    // A second without symptoms ends the run; the window then has to show them again.
    Controller broken;
    Run(broken, Bfi(6.0, 6), 3); // 18 late in 3 s: 2 symptom seconds (the 2nd and 3rd)
    CHECK(broken.Drop() == 0);
    broken.Judge(Bfi(4.0, 0), 2); // the GPU under the busy line: not a symptom second
    CHECK(broken.Drop() == 0);
    CHECK(Run(broken, Bfi(6.0, 0), 2) == 0); // the window still has 18 late: 2 seconds
    CHECK(broken.Judge(Bfi(6.0, 0), 2) == 1); // the 3rd in a row
}

TEST_CASE("Dynamic Resolution: with Black Frame Insertion, not on a stalled emulation thread",
          "[frame_xr][bfidr]") {
    // Report 76's boot (t=13-16): 9-10 late releases a second with the GPU at 2-3 ms. A lower
    // resolution would not help: never.
    Controller boot;
    CHECK(Run(boot, Bfi(2.4, 10), 200) == 0);
    // Under the busy line (0.6 of the period, 5.0 ms at 120 Hz) the same.
    Controller under;
    CHECK(Run(under, Bfi(4.9, 4, 4, 20.0), 200) == 0);
    // Fast forward (report 76, t=365-371: 100-116 late a second at 5 ms): more game frames than
    // the 3DS draws, late by design.
    Second ff = Bfi(5.2, 110);
    ff.emu_frames = 110;
    Controller fast;
    CHECK(Run(fast, ff, 200) == 0);
    CHECK_FALSE(fast.SymptomBad(ff));
    // The pause after it (the menu) clears the window: its late seconds do not carry over.
    Controller paused;
    Run(paused, Bfi(6.0, 4), 3); // 12 late, 1 symptom second
    Second menu = Bfi(6.0);
    menu.emu_frames = 0;
    paused.Judge(menu, 2);
    CHECK(paused.Symptoms().Size() == 0);
    CHECK(Run(paused, Bfi(6.0, 1), 30) == 0);
}

// AzaharFrame bfidr: report 76's own seconds (azahar_log.txt, the per-second lines): GPU behind,
// late releases of the top screen, the slowest xrEndFrame.
TEST_CASE("Dynamic Resolution: with Black Frame Insertion, report 76's seconds",
          "[frame_xr][bfidr]") {
    struct Row {
        double gpu;
        uint32_t late;
        double end;
    };
    // OoT at 6x drawn, Render Margin 1.0 (6 % uneven seconds), back from the menu at t=542: the
    // pacing's relock, then one second over the stricter line: no step in these eight (earlier,
    // at t=517, three in a row over 7.0 ms do step: report 84's replay).
    const Row oot[] = {{6.9, 6, 0.1}, {6.9, 3, 0.2}, {6.2, 0, 0.2}, {7.5, 0, 0.3}, {6.6, 0, 0.3},
                       {6.8, 0, 0.2}, {6.3, 0, 0.1}, {6.5, 0, 0.2}};
    Controller steady;
    for (const Row& r : oot) {
        steady.Judge(Bfi(r.gpu, r.late, 0, r.end), 2);
    }
    CHECK(steady.Drop() == 0);
    // MK7 at 6x drawn from t=22.8 (the bad window: 5.3 late releases a second, compositor
    // misses): a step at the third (the stricter line alone steps there too; fixd's line alone
    // at the fifth).
    const Row mk7[] = {{7.9, 19, 14.7}, {11.2, 21, 0.2}, {11.6, 13, 15.9},
                       {13.0, 48, 0.1}, {10.6, 2, 0.2},  {10.9, 50, 12.6}};
    Controller bad;
    int at = 0;
    for (const Row& r : mk7) {
        at++;
        if (bad.Judge(Bfi(r.gpu, r.late, 0, r.end), 2) == 1) {
            break;
        }
    }
    CHECK(bad.Drop() == 1);
    CHECK(at == 3);
    CHECK(bad.LastStepSymptoms());
}

TEST_CASE("Dynamic Resolution: with Black Frame Insertion, up only well under the stricter line",
          "[frame_xr][bfidr]") {
    Controller c;
    // 0.75 x 7.0 ms at 120 Hz (5.25 ms) for the next factor, by the pixels' ratio here: 5x to
    // 6x is x1.44. 3.5 ms predicts 5.04 ms (room), 3.8 ms 5.47 ms (none with it, room without:
    // fixd's 5.6 ms).
    CHECK(c.Good(Bfi(3.5), 5));
    CHECK_FALSE(c.Good(Bfi(3.8), 5));
    Second plain = Bfi(3.8);
    plain.bfi = false;
    CHECK(c.Good(plain, 5));
    // A second with symptoms is never good.
    CHECK_FALSE(c.Good(Bfi(3.5, 2), 5));
    CHECK_FALSE(c.Good(Bfi(3.5, 0, 2), 5));
    CHECK_FALSE(c.Good(Bfi(3.5, 0, 0, 13.0), 5));
    // A fraction of the period: at 90 Hz the stricter line is 9.3 ms (up under 7.0 ms).
    constexpr double k90 = 1000.0 / 90.0;
    CHECK(c.Good(Bfi(4.8, 0, 0, 1.0, k90), 5));
    CHECK_FALSE(c.Good(Bfi(5.0, 0, 0, 1.0, k90), 5));
    // MK7 (report 72/76 medians): 5.1 ms at 4x predicts 8.0 ms at 5x by the pixels: it stays at
    // 4x; 6.05 at 5x stays at 5x.
    CHECK_FALSE(c.Good(Bfi(5.1), 4));
    CHECK_FALSE(c.Good(Bfi(6.05), 5));
}

TEST_CASE("Dynamic Resolution: with Black Frame Insertion, no oscillation", "[frame_xr][bfidr]") {
    // A heavy and a light stretch every 40 s. Heavy: 6x is far behind with late pictures, 5x
    // holds (6 ms, clean). Light: 5x at 3.4 ms predicts room for 6x, where 5.0 ms is fine until
    // the next heavy stretch. A failed step up waits 180 s, then 540 s, then not again.
    Controller c;
    int changes = 0, last_change = 0;
    for (int t = 0; t < 1800; t++) {
        const bool heavy = (t / 40) % 2 == 0;
        const int factor = 6 - c.Drop();
        const Second s = heavy ? (factor == 6 ? Bfi(10.5, 4, 1, 13.0) : Bfi(6.0, 0))
                               : (factor == 6 ? Bfi(5.0, 0) : Bfi(3.4, 0));
        c.Judge(s, 1);
        if (c.LastStep() != 0) {
            changes++;
            last_change = t;
        }
    }
    CHECK(changes <= 8);
    CHECK(last_change < 1200); // settled at 5x for good
    CHECK(c.Drop() == 1);
}

TEST_CASE("Dynamic Resolution: with Black Frame Insertion, the floor stays the setting",
          "[frame_xr][bfidr]") {
    namespace V = VideoCore::DynamicResolution;
    auto& setting = Settings::values.vr_dynamic_resolution;
    auto& factor = Settings::values.resolution_factor;
    const bool was = setting.GetValue();
    const auto was_factor = factor.GetValue();
    SetLevelStore({});
    for (const double hz : {90.0, 120.0}) {
        const double period = 1000.0 / hz;
        for (int ir = 2; ir <= 6; ir++) {
            INFO("refresh " << hz << " Hz, Internal Resolution " << ir << "x");
            factor.SetValue(ir);
            const int base = ir + 2; // a Full mode
            // Heavy: the GPU far behind, late pictures; never under the setting.
            setting.SetValue(true);
            V::Apply(base);
            Install();
            for (int i = 0; i < 300; i++) {
                NoteSecond(Bfi(0.95 * period, 5, 2, 1.6 * period, period));
            }
            CHECK(Drop() == 2);
            CHECK(V::Apply(base) == ir);
            Uninstall();
            // Symptoms with the GPU busy but under the line (0.7 of the period): two steps too.
            Install();
            for (int i = 0; i < 300; i++) {
                NoteSecond(Bfi(0.7 * period, 3, 0, 1.0, period));
            }
            CHECK(Drop() == 2);
            Uninstall();
            // Under the busy line (0.5 of the period, a stalled emulation thread): none.
            Install();
            for (int i = 0; i < 300; i++) {
                NoteSecond(Bfi(0.5 * period, 6, 3, 2.0 * period, period));
            }
            CHECK(Drop() == 0);
            Uninstall();
            // Dynamic Resolution off: Black Frame Insertion's rules do not turn it on.
            setting.SetValue(false);
            Install();
            for (int i = 0; i < 300; i++) {
                NoteSecond(Bfi(0.95 * period, 5, 2, 1.6 * period, period));
            }
            CHECK(Drop() == 0);
            CHECK(V::Apply(base) == base);
            Uninstall();
        }
    }
    setting.SetValue(was);
    factor.SetValue(was_factor);
    V::g_base = 0;
}

TEST_CASE("Dynamic Resolution: the screen's ceiling", "[frame_xr][bfidr]") {
    // A 1728 x 1728 eye image over a field of view of tan 2.4 both ways (about 100 degrees): 720
    // pixels per unit of tan. A 5:3 screen of tan 1.6 x 0.96 covers 1152 x 691 of them: 2.88x
    // the picture's at 1x on both axes, 3.6x with the oversampling: 3x is the most.
    ScreenCover c{1.6, 0.96, 2.4, 2.4, 1728, 1728};
    CHECK(ScreenFactor(c) == 3);
    // The axis that needs the most decides (a tall screen: the height).
    CHECK(ScreenFactor({1.6, 1.9, 2.4, 2.4, 1728, 1728}) == 7);
    CHECK(ScreenFactor({2.2, 0.96, 2.4, 2.4, 1728, 1728}) == 4);
    // Not known: none.
    CHECK(ScreenFactor({}) == 0);
    CHECK(ScreenFactor({1.6, 0.96, 0.0, 2.4, 1728, 1728}) == 0);
    CHECK(ScreenFactor({1.6, 0.96, 2.4, 2.4, 0, 1728}) == 0);
    // At least 1x, however small.
    CHECK(ScreenFactor({0.01, 0.006, 2.4, 2.4, 1728, 1728}) == 1);
    // Live from the sizes, never fitted: it only grows with the screen and the eye's pixels.
    int last = 0;
    for (double t = 0.2; t < 3.0; t += 0.05) {
        const int f = ScreenFactor({t, t * 0.6, 2.4, 2.4, 1728, 1728});
        CHECK(f >= last);
        last = f;
    }
    CHECK(ScreenFactor({1.6, 0.96, 2.4, 2.4, 2160, 2160}) >= ScreenFactor(c));

    // Held: a new value after 3 seconds in a row (a grip resize is one change, not many).
    CeilingHold h;
    CHECK(h.Next(4) == 0);
    CHECK(h.Next(4) == 0);
    CHECK(h.Next(4) == 4);
    CHECK(h.Next(5.3) == 4);
    CHECK(h.Next(6.3) == 4);
    CHECK(h.Next(6.3) == 4);
    CHECK(h.Next(6.3) == 6);
    // Higher only kCeilingMargin past a whole factor; lower at once (after the hold).
    CHECK(h.Next(7.1) == 6);
    CHECK(h.Next(7.1) == 6);
    CHECK(h.Next(7.1) == 6);
    CHECK(h.Next(5.9) == 6);
    CHECK(h.Next(5.9) == 6);
    CHECK(h.Next(5.9) == 5);
    CHECK(h.Next(0) == 5);
    CHECK(h.Next(0) == 5);
    CHECK(h.Next(0) == 0);
    // A value that wanders across a whole factor with the head (3.9 to 4.15): one change, not
    // one every few seconds.
    CeilingHold noisy;
    int changes = 0, was = 0;
    for (int t = 0; t < 600; t++) {
        const double v = 4.025 + 0.125 * std::sin(t * 0.7) + 0.05 * ((t * 7) % 3 - 1);
        const int now = noisy.Next(v);
        changes += now != was ? 1 : 0;
        was = now;
    }
    CHECK(changes <= 3);
    CHECK(ScreenCoverage({1.6, 0.96, 2.4, 2.4, 1728, 1728}) > 3.59);
    CHECK(ScreenCoverage({}) == 0.0);

    // The controller: the ceiling's moves are no step, and its level comes back when it lifts.
    Controller k;
    CHECK(k.Judge(Bfi(3.0), 2, 1) == 1);
    CHECK(k.LastStep() == 0);
    CHECK(k.OwnDrop() == 0);
    CHECK(k.Ceilinged());
    CHECK(Steps(k, Bfi(3.0), 60, 2, 1) == 0); // good seconds: no step up over the ceiling
    CHECK(k.Drop() == 1);
    CHECK(k.Judge(Bfi(3.0), 2, 0) == 0);
    CHECK(k.LastStep() == 0);
    // A step down under the ceiling is from the factor drawn.
    Controller d;
    d.Judge(Bfi(3.0), 2, 1);
    CHECK(Steps(d, Bfi(9.0), Controller::kSettle + Controller::kDownAfter, 2, 1) == 1);
    CHECK(d.OwnDrop() == 2);
    CHECK(d.Drop() == 2);
}

TEST_CASE("Dynamic Resolution: the screen's ceiling through the frame loop's side",
          "[frame_xr][bfidr]") {
    namespace V = VideoCore::DynamicResolution;
    auto& setting = Settings::values.vr_dynamic_resolution;
    auto& factor = Settings::values.resolution_factor;
    const bool was = setting.GetValue();
    const auto was_factor = factor.GetValue();
    setting.SetValue(true);
    factor.SetValue(4);
    std::map<LevelKey, int> disk;
    const uint64_t title = 0x0004000000030800ULL;
    LevelStore store;
    store.load = [&] { return LevelList(disk.begin(), disk.end()); };
    store.save = [&](LevelList levels) {
        disk = std::map<LevelKey, int>(levels.begin(), levels.end());
    };
    store.title = [&] { return title; };
    SetLevelStore(store);
    V::Apply(6); // a Full mode over 4x
    Install();
    Second s = Bfi(3.0);
    s.screen_factor = 5;
    for (int i = 0; i < CeilingHold::kCeilingSeconds - 1; i++) {
        NoteSecond(s);
    }
    CHECK(Drop() == 0);
    NoteSecond(s);
    CHECK(Drop() == 1);
    CHECK(V::Apply(6) == 5);
    // Held long: not remembered (the screen decided it, not the GPU).
    for (int i = 0; i < kRememberAfter + 10; i++) {
        NoteSecond(s);
    }
    CHECK(disk.empty());
    // Under the floor: the setting (4x) is the most it takes off.
    s.screen_factor = 2;
    for (int i = 0; i < CeilingHold::kCeilingSeconds; i++) {
        NoteSecond(s);
    }
    CHECK(V::Apply(6) == 4);
    // Black Frame Insertion off: the ceiling holds all the same (the owner, 2026-11-17: whenever
    // Dynamic Resolution runs).
    s.bfi = false;
    s.bfi_asked = false;
    for (int i = 0; i < CeilingHold::kCeilingSeconds + 2; i++) {
        NoteSecond(s);
    }
    CHECK(V::Apply(6) == 4);
    s.screen_factor = 5.5; // up: past the margin
    for (int i = 0; i < CeilingHold::kCeilingSeconds; i++) {
        NoteSecond(s);
    }
    CHECK(V::Apply(6) == 5);
    // Dynamic Resolution off: the setting's factor, no cap.
    setting.SetValue(false);
    NoteSecond(s);
    CHECK(V::Apply(6) == 6);
    Uninstall();
    SetLevelStore({});
    setting.SetValue(was);
    factor.SetValue(was_factor);
    V::g_base = 0;
}

// AzaharFrame bfidr: report 79's Quick Load at t=105 (4x drawn, Black Frame Insertion steady
// before and after): the screens off, then 6-7 s of late releases with the GPU at 6.8-8.0 ms while
// the rasterizer cache fills again. Its symptoms do not count for BfiRules::kAfterStart seconds.
TEST_CASE("Dynamic Resolution: with Black Frame Insertion, a Load's own burst",
          "[frame_xr][bfidr]") {
    struct Row {
        double gpu;
        uint32_t late;
    };
    const Row after_load[] = {{7.2, 4}, {7.4, 5}, {6.8, 2}, {8.0, 0}, {7.8, 7},
                              {7.9, 2}, {7.7, 1}, {6.6, 0}, {6.2, 0}, {6.0, 0}};
    Controller c;
    c.Start(0, true); // the screens back after the Load
    for (const Row& r : after_load) {
        c.Judge(Bfi(r.gpu, r.late), 2);
    }
    CHECK(c.Drop() == 0);
    // After the hold the same symptoms count again.
    for (const Row& r : after_load) {
        c.Judge(Bfi(r.gpu, r.late), 2);
    }
    CHECK(c.Drop() == 1);
    CHECK(c.LastStepSymptoms() == false); // LastStep is the last Judge's: none now
    // A fresh start holds the symptoms, not the GPU's line.
    Controller line;
    line.Start(0);
    CHECK(Run(line, Bfi(8.0), Controller::kDownAfter, 2) == 1);
}

// AzaharFrame bfidr: two remembered levels a game (the owner, 2026-11-17): a step taken for
// Black Frame Insertion is its own level, remembered at once; turning it off goes back to the
// level without it (here none: the full factor) at once, and turning it on again to its own.
TEST_CASE("Dynamic Resolution: a step for Black Frame Insertion is its own level",
          "[frame_xr][bfidr]") {
    namespace V = VideoCore::DynamicResolution;
    auto& setting = Settings::values.vr_dynamic_resolution;
    auto& factor = Settings::values.resolution_factor;
    const bool was = setting.GetValue();
    const auto was_factor = factor.GetValue();
    setting.SetValue(true);
    factor.SetValue(4);
    std::map<LevelKey, int> disk;
    uint64_t title = 0x0004000000030800ULL;
    LevelStore store;
    store.load = [&] { return LevelList(disk.begin(), disk.end()); };
    store.save = [&](LevelList levels) {
        disk = std::map<LevelKey, int>(levels.begin(), levels.end());
    };
    store.title = [&] { return title; };
    SetLevelStore(store);
    V::Apply(6); // a Full mode over 4x
    Install();
    const auto run = [](const Second& s, int n) {
        for (int i = 0; i < n; i++) {
            NoteSecond(s);
        }
    };
    // 7.2 ms with it flashing: under fixd's line (7.5 ms), over the stricter one (7.0 ms). Held
    // after the start, then three seconds.
    run(Bfi(7.2), BfiRules::kAfterStart + BfiRules::kDownAfter);
    CHECK(Drop() == 1);
    CHECK(disk[{title, true}] == 5);
    CHECK(disk.count({title, false}) == 0);
    // Turned off (the setting no longer asks for it): the full factor at the next second.
    Second off = Bfi(7.2);
    off.bfi = false;
    off.bfi_asked = false;
    run(off, 1);
    CHECK(Drop() == 0);
    CHECK(V::Apply(6) == 6);
    // The same load without it: fixd's rules, no step; held long, 6x is its level.
    run(off, kRememberAfter + 1);
    CHECK(Drop() == 0);
    CHECK(disk[{title, false}] == 6);
    CHECK(disk[{title, true}] == 5);
    // On again: its own level at once.
    run(Bfi(3.0), 1);
    CHECK(Drop() == 1);
    Uninstall();
    // The next start of the game: each mode's level.
    SetLevelStore(store);
    CHECK(RememberedFactor(title, true) == 5);
    CHECK(RememberedFactor(title, false) == 6);
    Install();
    run(off, 1);
    CHECK(Drop() == 0);
    run(Bfi(3.0), 1);
    CHECK(Drop() == 1);
    Uninstall();
    // A game with a level without it only: with it, from that level (a step for it only ever
    // goes lower).
    title = 0x0004000000054000ULL;
    disk[{title, false}] = 5;
    SetLevelStore(store);
    Install();
    run(Bfi(3.0), 1);
    CHECK(Drop() == 1);
    Uninstall();

    SetLevelStore({});
    setting.SetValue(was);
    factor.SetValue(was_factor);
    V::g_base = 0;
}

// AzaharFrame bfikit: Kid Icarus (30 pictures a second) with Black Frame Insertion asked for: it
// never flashes (held for a slow game), so its seconds are judged by the line without it; a level
// held that way is not the game's level with Black Frame Insertion (session 4 logged "6x
// remembered for this game with Black Frame Insertion (held 60 s)" at 141 s with nothing
// flashed). A level it held while flashing still is.
TEST_CASE("Dynamic Resolution: a hold with Black Frame Insertion asked for but not flashing",
          "[frame_xr][bfikit]") {
    namespace V = VideoCore::DynamicResolution;
    auto& setting = Settings::values.vr_dynamic_resolution;
    auto& factor = Settings::values.resolution_factor;
    const bool was = setting.GetValue();
    const auto was_factor = factor.GetValue();
    setting.SetValue(true);
    factor.SetValue(4);
    std::map<LevelKey, int> disk;
    const uint64_t title = 0x0004000000030200ULL;
    LevelStore store;
    store.load = [&] { return LevelList(disk.begin(), disk.end()); };
    store.save = [&](LevelList levels) {
        disk = std::map<LevelKey, int>(levels.begin(), levels.end());
    };
    store.title = [&] { return title; };
    SetLevelStore(store);
    V::Apply(6); // a Full mode over 4x
    Install();
    Second held = Bfi(3.0);
    held.bfi = false; // asked for, held for a slow game
    for (int i = 0; i < kRememberAfter + 20; i++) {
        NoteSecond(held);
    }
    CHECK(Drop() == 0);
    CHECK(disk.count({title, true}) == 0);
    CHECK(disk.count({title, false}) == 0);
    // Flashing now: the same level held kRememberAfter flashing seconds is remembered with it.
    const Second flashing = Bfi(3.0);
    for (int i = 0; i < kRememberAfter - 1; i++) {
        NoteSecond(flashing);
    }
    CHECK(disk.count({title, true}) == 0);
    // One second held for the slow game starts the run again.
    NoteSecond(held);
    for (int i = 0; i < kRememberAfter - 1; i++) {
        NoteSecond(flashing);
    }
    CHECK(disk.count({title, true}) == 0);
    NoteSecond(flashing);
    CHECK(disk.count({title, true}) == 1);
    CHECK(RememberedFactor(title, true) == 6);
    // Without Black Frame Insertion asked for, a hold is remembered as before.
    Uninstall();
    disk.clear();
    SetLevelStore(store);
    Install();
    Second plain = Bfi(3.0);
    plain.bfi = false;
    plain.bfi_asked = false;
    for (int i = 0; i < kRememberAfter + 1; i++) {
        NoteSecond(plain);
    }
    CHECK(RememberedFactor(title, false) == 6);
    Uninstall();
    SetLevelStore({});
    setting.SetValue(was);
    factor.SetValue(was_factor);
    V::g_base = 0;
}

// AzaharFrame kiuhs3a: Kid Icarus: Uprising at 60 FPS: down to kSixtyFloor under the setting,
// with levels of its own; everything else at the setting.
TEST_CASE("Dynamic Resolution: the floor at 60 FPS", "[frame_xr][kiuhs3a]") {
    CHECK(kSixtyFloor == 2);
    for (int setting = 2; setting <= 10; setting++) {
        for (int base = setting; base <= setting + 2; base++) {
            INFO("setting " << setting << "x, drawn at " << base << "x");
            CHECK(FloorFactor(base, setting) == setting); // 30 FPS, every other game
            CHECK(FloorFactor(base, setting, kFloorAtSetting, true) == 2);
            CHECK(base - MaxDrop(base, setting, kFloorAtSetting, true) == 2);
        }
    }
    // 1x: nothing to do, at either rate; Auto: as it is at 30.
    CHECK(MaxDrop(3, 1, kFloorAtSetting, true) == 0);
    CHECK(MaxDrop(6, 0, kFloorAtSetting, true) == MaxDrop(6, 0));
    // A setting of 2x: the same floor at both rates.
    CHECK(MaxDrop(4, 2, kFloorAtSetting, true) == MaxDrop(4, 2));
    CHECK(MaxDrop(2, 2, kFloorAtSetting, true) == 0);
    // Drawn under the floor (no setting does it): nothing comes off.
    CHECK(MaxDrop(1, 4, kFloorAtSetting, true) == 0);
}

TEST_CASE("Dynamic Resolution: 60 FPS on and off", "[frame_xr][kiuhs3a]") {
    namespace V = VideoCore::DynamicResolution;
    auto& setting = Settings::values.vr_dynamic_resolution;
    auto& factor = Settings::values.resolution_factor;
    const bool was = setting.GetValue();
    const auto was_factor = factor.GetValue();
    setting.SetValue(true);
    factor.SetValue(4);
    std::map<LevelKey, int> disk;
    const uint64_t title = 0x0004000000030200ULL;
    LevelStore store;
    store.load = [&] { return LevelList(disk.begin(), disk.end()); };
    store.save = [&](LevelList levels) {
        disk = std::map<LevelKey, int>(levels.begin(), levels.end());
    };
    store.title = [&] { return title; };
    const auto run = [](Second s, int n, bool sixty) {
        s.sixty_fps = sixty;
        for (int i = 0; i < n; i++) {
            NoteSecond(s);
        }
    };
    Second off = Fine();
    off.live = false;
    SetLevelStore(store);
    V::Apply(6); // a Full mode over 4x
    Install();

    // 30 FPS, the GPU far behind: the setting.
    run(GpuBehind(), 200, false);
    CHECK(V::Apply(6) == 4);
    CHECK(RememberedFactor(title) == 4);
    // The game started again at 60: from the level at 30, then down to 2x, its own level.
    run(off, 2, true);
    CHECK(V::Apply(6) == 4);
    run(GpuBehind(), 300, true);
    CHECK(V::Apply(6) == 2);
    CHECK(RememberedFactor(title, false, true) == 2);
    CHECK(RememberedFactor(title) == 4);
    // A Load at 60: 2x again.
    run(off, 2, true);
    CHECK(V::Apply(6) == 2);
    run(Neutral(), 1, true);
    CHECK(V::Apply(6) == 2);
    // Back at 30: its own level, never the one at 60.
    run(off, 2, false);
    CHECK(V::Apply(6) == 4);
    run(Neutral(), 1, false);
    CHECK(V::Apply(6) == 4);
    Uninstall();

    // Nothing remembered at 30: a run at 30 starts at the top whatever 60 remembers.
    disk = {{{title, kLevelSixty}, 2}, {{title, kLevelBfi | kLevelSixty}, 2}};
    SetLevelStore(store);
    Install();
    run(Neutral(), 1, false);
    CHECK(V::Apply(6) == 6);
    // Started while the screens were off with the rate not known yet: from the first second.
    run(off, 1, false);
    run(Neutral(), 1, true);
    CHECK(V::Apply(6) == 2);
    Uninstall();

    // Black Frame Insertion's level: the four are apart.
    disk = {{{title, 0}, 5}, {{title, kLevelSixty}, 3}};
    SetLevelStore(store);
    Install();
    run(Bfi(3.0), 1, true); // none of its own: the one at 60 without it
    CHECK(V::Apply(6) == 3);
    run(Bfi(0.95 * kPeriod, 5, 2, 1.6 * kPeriod), 300, true);
    CHECK(V::Apply(6) == 2);
    CHECK(RememberedFactor(title, true, true) == 2);
    CHECK(RememberedFactor(title, false, true) == 3);
    CHECK(RememberedFactor(title, true, false) == 0);
    CHECK(RememberedFactor(title, false, false) == 5);
    run(Neutral(), 1, true); // Black Frame Insertion off
    CHECK(V::Apply(6) == 3);
    run(Bfi(3.0), 1, false); // at 30 with it: the one at 30 without it
    CHECK(V::Apply(6) == 5);
    Uninstall();

    // Internal Resolution changed while it runs at 60: from the new one, down to 2x again.
    disk.clear();
    SetLevelStore(store);
    Install();
    run(GpuBehind(), 300, true);
    CHECK(V::Apply(6) == 2);
    factor.SetValue(3);
    Reset();
    V::Apply(5);
    run(Neutral(), 1, true);
    CHECK(V::Apply(5) == 5);
    run(GpuBehind(), 300, true);
    CHECK(V::Apply(5) == 2);
    // To 2x: the floor is the setting; to 1x: nothing to do.
    factor.SetValue(2);
    Reset();
    V::Apply(4);
    run(GpuBehind(), 300, true);
    CHECK(V::Apply(4) == 2);
    factor.SetValue(1);
    Reset();
    V::Apply(3);
    run(GpuBehind(), 300, true);
    CHECK(V::Apply(3) == 3);
    Uninstall();

    SetLevelStore({});
    setting.SetValue(was);
    factor.SetValue(was_factor);
    V::g_base = 0;
}
