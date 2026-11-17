// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame menuux: Dynamic Resolution (Settings::values.vr_dynamic_resolution, Off by
// default).
//
// The rule: the renderer's factor is the ceiling (Internal Resolution, two steps more in the
// immersive modes) and, AzaharFrame flash3, the Internal Resolution setting the floor
// (kFloorAtSetting; 1x with Auto), one step at a time; at 1x it has nothing to do (the menu shows
// its row disabled), nor outside the immersive modes, where the factor is the setting itself. It lowers the resolution only when the GPU is
// the limit: the GPU's time on the game's frames is near or over the headset's frame budget (its
// display period). Frames missed while the GPU has room are the emulation thread's (CPU-bound):
// a lower resolution would not help them, so they never step it down.
//
// What it measures: each second the XR thread's summary (Runtime::LogSummary) gives the display
// period and how long the GPU took to finish each game frame's last work after it was queued
// (video_core/vr_dynamic_resolution.h). A GPU that keeps up finishes it in a few ms; one that
// cannot is still on earlier work, close to or past a whole display period. Whether the game
// runs at 30 or 60 fps does not count; seconds without game frames (paused, the menu open) are
// not judged, and a started or stopped game starts it again from the setting (AzaharFrame ui2:
// Restart too; fixd: from the level remembered for the game). Turned off and on again in a game, it carries on at the level it had.
//
// Not jarring: a step needs kDownAfter bad (or kUpAfter good) seconds in a row, one step at a
// time; the seconds after a step are not judged (the step's own hitch: the rasterizer cache
// starts again at the new factor); and a step up the GPU cannot hold blocks that level for
// kRetryWait, and for the rest of the game the second time, so it settles instead of going up
// and down. AzaharFrame ui2: good seconds count only kHoldAfterDown seconds after the last step
// down's settle.
//
// AzaharFrame flash3 (report 70, session 573: it only ever went down, 6x to 3x in MK7):
//   - a second that is not bad ends a bad run, so a step down needs kDownAfter bad seconds truly
//     in a row (an ordinary second left the count as it was, and the step to 3x counted a bad
//     second from 24 s before);
//   - a step up is judged by its predicted cost: the GPU's time now times the pixels of the next
//     factor over this one's, ((f + 1) / f)^2, under kUpBudget of the bad line. The old rule (a
//     quarter of the budget, 2.08 ms at 120 Hz) was under the GPU's floor in MK7 (3 ms even at
//     3x), so it never went up. The failed-step blocking above is kept;
//   - the floor (FloorFactor): the owner's choice (option B): the Internal Resolution setting
//     itself (kFloorAtSetting), so only the immersive modes' two extra steps come off (1.1.1 went
//     down to 1x: 3x under a 4x setting in session 573). false brings 1.1.1's rule back.
//
// AzaharFrame fixd (session 651, report 82 item 1; the owner: it steps down too quickly and never
// comes back):
//   - slower down: kDownAfter 5 bad seconds in a row (was 3);
//   - up on the measured cost: the next factor's cost is predicted with Ratio instead of the
//     pixels alone, which are pessimistic at the low factors (report 72's MK7 medians, 3x to
//     4x: x1.13, x1.78 by the pixels; session 651's Majora's Mask: 5.4 ms at 5x, 3.4 ms at 6x)
//     and optimistic at the high ones (4x to 5x: x1.73, x1.56). Ratio starts at the pixels'
//     and comes down to what says the step is cheaper: the medians of the two factors' last
//     judged seconds in this game (Cost, kCostSeconds of each), and a step down's run against
//     the seconds after its settle (that run is the GPU behind, its time the cost plus the
//     wait, so it only ever lowers it). A step up measures the step itself (the good run
//     before against kMeasureSeconds judged seconds after its settle, up to kRatioOverPixels
//     times the pixels'), and Ratio is never under that: a step up that could not be held
//     is not tried again on the medians alone;
//   - no oscillation: a step down from a level within kFailWindow (60 s) of the step up to it
//     is a failed step up: that level waits kRetryWait (180 s), three times that the second
//     time, and is not tried again in the game the third time;
//   - remembered per game: the factor it steps down to, or one held kRememberAfter judged
//     seconds, is kept per title ID (kStoreFile in the user folder; older builds never read
//     it), and a game's start or a Load starts there instead of at the ceiling (StartDrop); the ceiling
//     stays the Internal Resolution setting plus the immersive modes' two steps. Each such
//     change is logged as what it is ("back to the remembered level", "reset to the setting"),
//     never as a step up.
//
// AzaharFrame bfidr (report 76 item 4, the 1.2.x plan's Phase 1: Black Frame Insertion on the top
// screen's quads in the Full modes breaks under GPU load, a late picture lit again doubles):
//   - while it flashes (Second::bfi), a second is also bad on its symptoms (BfiRules,
//     SymptomWindow): late releases or late pictures lit again, more than about one a second
//     over the last 10 s, or the compositor missing refreshes, while the GPU is busy; 3 such
//     seconds in a row step down (BfiRules::kDownAfter; the GPU's line alone keeps kDownAfter);
//   - a step up only on a predicted cost under kUpBudget of a stricter line (BfiRules::kUpLine,
//     7.0 ms at 120 Hz), in a second without symptoms;
//   - the floor is still the Internal Resolution setting (only the immersive modes' two steps come
//     off), and nothing of it runs with Dynamic Resolution off: it is the setting's rule;
//   - the screen's ceiling (ScreenFactor, CeilingHold): no factor whose picture has more than
//     1.25 times the display pixels the top screen covers, from its live size and distance and
//     the eye's image; its moves are no step, and it is never under the floor.

#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace FrameXr {

namespace DynamicResolution {

/// One second (about) of the XR thread's frames.
struct Second {
    double seconds = 1.0;
    uint32_t display_frames = 0;
    double period_ms = 0.0;  ///< the runtime's display period
    uint32_t emu_frames = 0; ///< game frames (0: paused, the menu, no game)
    uint32_t gpu_frames = 0; ///< game frames whose GPU finish was timed
    double gpu_ms = 0.0;     ///< their average time from the last submit to the fence
    bool live = true;        ///< a game is running (its screens are live)
    /// AzaharFrame flash3: the factor drawn in this second (0: not known; Judge then takes the
    /// steps up to `max_drop` as the factors down to 1x).
    int factor = 0;
    /// AzaharFrame bfidr: Black Frame Insertion flashed the top screen in a Full mode for most of
    /// this second (MotionClarity::InEffect, not held for a slow game nor in the projection
    /// layer); the symptoms below then count too (BfiRules).
    bool bfi = false;
    /// AzaharFrame bfidr: game frames released late (drawn aside, LogSummary's "late" of the top
    /// screen), refreshes Black Frame Insertion lit a late picture again on (MotionClarity's
    /// "lit for: late"), and the longest xrEndFrame call (ms).
    uint32_t late_releases = 0;
    uint32_t late_lit = 0;
    double end_frame_max_ms = 0.0;
    /// AzaharFrame bfidr: the most the top screen can show (ScreenFactor; 0: not known).
    int screen_factor = 0;
};

/// AzaharFrame bfidr: while Black Frame Insertion flashes in a Full mode, a picture late is lit
/// again and doubles, and the compositor missing a refresh shows as a bright or dark blip (report
/// 76: MK7 at 6x drawn, 2.8 late releases a second, 16 % uneven seconds, 7 seconds with an
/// xrEndFrame over 12 ms in 77 s; at 4x drawn, report 79: 0.10 a second, 0 such seconds). So the
/// symptoms count as well as the GPU's time (SymptomWindow), over the last kWindow judged seconds:
///   - more than kLatePerSecond late releases a second, or late lit refreshes, counted over the
///     whole window (a fixed kWindow seconds, however many are in: the pacing's relock after the
///     VR menu, 4-6 late in one second, and a boot's burst are no trend);
///   - kSlowSeconds or more seconds with an xrEndFrame over kEndFramePeriods display periods
///     (12 ms at 120 Hz);
///   - and only while the GPU is busy: GPU behind at kBusyShare of the period or more (5.0 ms at
///     120 Hz; a GPU with room reads its fixed part, 3.5-4.5 ms in reports 72 and 76): symptoms of
///     a stalled emulation thread (report 76's boot: 9-10 late releases a second at 2-3 ms) never
///     lower the resolution; nor fast forward's (kGameRateMax).
/// Replayed over report 76's per-second lines: a step in each bad MK7 window
/// within 3-5 s and in both OoT Render Margin 1.4 windows, none in OoT's steady 137 s at 1.0,
/// at the boot, nor in KIU flat; one in MK7's 38-s "clean" window at 10.4 ms (past the knee).
/// A step down after kDownAfter such seconds in a row (the GPU's line alone keeps fixd's
/// Controller::kDownAfter: OoT at 6x with Render Margin 1.0, steady, reads 7.3 ms with a third of
/// its seconds over the line). A step up only on a predicted cost under Controller::kUpBudget of
/// kUpLine (0.75 x 7.0 ms at 120 Hz) in a second without symptoms.
struct BfiRules {
    static constexpr int kDownAfter = 3;
    static constexpr int kWindow = 10;
    static constexpr double kLatePerSecond = 1.0;
    static constexpr int kSlowSeconds = 2;
    static constexpr double kEndFramePeriods = 1.44;
    static constexpr double kBusyShare = 0.6;
    /// Game frames a second over this are fast forward (the 3DS draws at most 59.8 a second):
    /// late by design (report 76: 100-116 late releases a second at t=365-371), not judged by the
    /// symptoms.
    static constexpr double kGameRateMax = 66.0;
    /// The stricter line step ups are judged against, a share of the period (7.0 ms at 120 Hz;
    /// report 76: MK7's 5x drawn at 6.05 ms and 4x at 5.1 ms held the rhythm, 6x at 10.6 did not;
    /// OoT degraded at 7.8 ms). Not a line for a step down alone: GPU behind is the time from a
    /// submit to its fence, and OoT was steady at 7.3 ms.
    static constexpr double kUpLine = 0.84;
};

/// AzaharFrame bfidr: the symptoms of the last BfiRules::kWindow judged seconds (cleared at a
/// step, a pause and while Black Frame Insertion is not in effect).
class SymptomWindow {
public:
    void Add(const Second& s);
    void Clear() {
        *this = SymptomWindow{};
    }
    /// The symptoms say the GPU is the limit, whatever this second's GPU time (Bad's gate is
    /// applied by the caller).
    bool Symptoms() const;
    /// This second had none (a step up's second).
    static bool Clean(const Second& s);
    uint32_t LateReleases() const;
    uint32_t LateLit() const;
    int SlowSeconds() const;
    int Size() const {
        return n;
    }

private:
    struct Entry {
        uint32_t late = 0, lit = 0;
        bool slow = false;
    };
    Entry entries[BfiRules::kWindow] = {};
    int next = 0, n = 0;
};

/// AzaharFrame bfidr: the most a top screen can show. The factor whose picture (kTopWidth x
/// kTopHeight times the factor, the same pixels whatever the immersive "draws" widen) has more
/// than kScreenOversample times the display pixels the screen covers on both axes only costs GPU
/// time. `tan_w`, `tan_h`: the screen's width and height over its distance; `eye_tan_w`,
/// `eye_tan_h`: the eye's field of view as tan(right) - tan(left) and tan(up) - tan(down);
/// `eye_px_w`, `eye_px_h`: the runtime's recommended eye image (its pixels at the middle of the
/// view, where the screen is). The axis that needs the most decides; 0: not known.
inline constexpr double kScreenOversample = 1.25;
inline constexpr int kTopWidth = 400, kTopHeight = 240;
struct ScreenCover {
    double tan_w = 0.0, tan_h = 0.0;
    double eye_tan_w = 0.0, eye_tan_h = 0.0;
    uint32_t eye_px_w = 0, eye_px_h = 0;
};
int ScreenFactor(const ScreenCover& c);

/// AzaharFrame bfidr: the screen's ceiling, with a hold: a new value is taken after
/// kCeilingSeconds seconds in a row (a grip resize passes through many sizes: one step, not one
/// per size).
class CeilingHold {
public:
    static constexpr int kCeilingSeconds = 3;
    /// Once a second, the factor the screen can show now (0: none, no ceiling): the one in force.
    int Next(int factor);
    int Now() const {
        return now;
    }

private:
    int now = 0, candidate = 0, seen = 0;
};

/// AzaharFrame flash3: whether Dynamic Resolution stops at the Internal Resolution setting
/// (true, the owner's choice: only the immersive modes' two extra steps come off; outside them it
/// has nothing to do) or may go down to 1x (false, as in 1.1.1: in the immersive modes it could
/// go under the setting, e.g. 3x under a 4x setting).
inline constexpr bool kFloorAtSetting = true;

/// AzaharFrame flash3: the lowest factor it draws at for the renderer's factor `base` (with the
/// immersive modes' steps) and the Internal Resolution setting `setting` (0: Auto), and how many
/// steps that leaves (MaxDrop). Internal Resolution 1x leaves none either way.
constexpr int FloorFactor(int base, int setting, bool floor_at_setting = kFloorAtSetting) {
    if (setting == 1) {
        return base; // at 1x it has nothing to do (the menu's row is locked)
    }
    const int floor = floor_at_setting && setting > 1 ? setting : 1;
    return floor < base ? floor : base;
}
constexpr int MaxDrop(int base, int setting, bool floor_at_setting = kFloorAtSetting) {
    return base > 1 ? base - FloorFactor(base, setting, floor_at_setting) : 0;
}

/// The decisions, without threads or settings (unit-tested).
class Controller {
public:
    /// The GPU's time as a share of the display period: from kBadBudget on it is the limit.
    static constexpr double kBadBudget = 0.9;
    /// AzaharFrame flash3: a second has room for a step up when the GPU's time, scaled to the
    /// next factor's pixels, is under this share of the bad line (5.6 ms at 120 Hz). AzaharFrame
    /// ui2's rule was a quarter of the budget (0.5 before it: in report 14's logs 3 of 4 steps
    /// up from 2.2-2.9 ms at 120 Hz failed within 8 s), whatever the factor; in session 573 MK7
    /// never went under 3 ms, even at 3x, so it never went up again.
    static constexpr double kUpBudget = 0.75;
    static constexpr uint32_t kMinGpuFrames = 5; ///< fewer timed frames say nothing
    static constexpr int kDownAfter = 5;        ///< bad seconds in a row for a step down (fixd: was 3)
    static constexpr int kUpAfter = 10;         ///< good seconds in a row for a step up
    static constexpr int kSettle = 5;           ///< seconds not judged after a step
    /// AzaharFrame ui2: after a step down (and kSettle), seconds whose good ones do not count
    /// yet, so it does not bounce straight back up; bad ones still do.
    static constexpr int kHoldAfterDown = 5;
    /// A step down this soon (seconds) after the step up to that level: the step up failed
    /// (AzaharFrame fixd: was 20; a step down takes kSettle + kDownAfter = 10 s at the least).
    static constexpr int kFailWindow = 60;
    /// Seconds before a failed level is tried again: this the first time, kRetryGrowth times
    /// that the second, and not again in the game after kFailsForGame failures (AzaharFrame
    /// fixd: was 120, then the game).
    static constexpr int kRetryWait = 180;
    static constexpr int kRetryGrowth = 3;
    static constexpr int kFailsForGame = 3;
    /// AzaharFrame fixd: the judged seconds after a step's settle that measure the new factor.
    static constexpr int kMeasureSeconds = 3;
    /// AzaharFrame fixd: a measured ratio is kept from 1 (never cheaper one step up) to this
    /// many times the pixels' ratio after a step up (MK7's x1.73 from 4x to 5x, the pixels'
    /// x1.56, fits; a scene that changed during the measurement cannot block the step for the
    /// rest of the game), and to the pixels' ratio after a step down.
    static constexpr double kRatioMin = 1.0, kRatioOverPixels = 1.25;
    /// AzaharFrame fixd: the cost per factor: the median of its last kCostKeep judged seconds
    /// in this game, once it has kCostSeconds of them.
    static constexpr int kCostKeep = 120, kCostSeconds = 10;
    static constexpr int kMaxSteps = 32;

    /// Judges a second; `max_drop` is how far it may go (the factor less 1). Returns the steps
    /// under the factor to draw at now. AzaharFrame bfidr: `min_drop`, the steps the screen's
    /// ceiling takes off (CeilingHold): the factor is never above it, and its moves are no step
    /// (nor a failed step up, nor a level to remember); when it lifts, the level the seconds
    /// decided comes back.
    int Judge(const Second& s, int max_drop, int min_drop = 0);
    int Drop() const {
        return std::max(drop, min_drop_);
    }
    /// AzaharFrame bfidr: the level the seconds decided (Drop without the screen's ceiling).
    int OwnDrop() const {
        return drop;
    }
    /// AzaharFrame bfidr: the screen's ceiling decides the factor now (at or under the level the
    /// seconds decided): held there, it is no level to remember.
    bool Ceilinged() const {
        return min_drop_ > 0 && min_drop_ >= drop;
    }
    /// From the factor itself again (the setting changed, a game started or stopped).
    void Reset();
    /// AzaharFrame fixd: from the factor itself again, then `steps` under it (the level
    /// remembered for the game); the seconds after are not judged and good ones wait a hold.
    /// `same_game`: a Load or a Restart in the game it has measured: what it measured (the
    /// costs per factor, the ratios) is kept.
    void Start(int steps, bool same_game = false);
    /// AzaharFrame ui2: carries on from a copy taken when it was turned off (the same game):
    /// the streaks start again and, at a lower level, the seconds after are not judged.
    void Resume();

    /// Whether a second is bad (the GPU is the limit) or good (it has room for the next factor
    /// up from `factor`, AzaharFrame flash3); neither: neutral.
    static bool Bad(const Second& s);
    bool Good(const Second& s, int factor) const;
    /// AzaharFrame bfidr: with Black Frame Insertion, the symptoms of the window with this second
    /// (the GPU busy too, BfiRules::kBusyShare); false without it.
    bool SymptomBad(const Second& s) const;
    /// AzaharFrame bfidr: what the last step down was for: the symptoms (true) or the GPU's line.
    bool LastStepSymptoms() const {
        return last_symptoms;
    }
    const SymptomWindow& Symptoms() const {
        return window;
    }
    /// AzaharFrame bfidr: the window as it was at the last step down (for the log).
    const SymptomWindow& StepWindow() const {
        return step_window;
    }
    /// AzaharFrame flash3: the GPU's time predicted at `factor` + 1 from the time at `factor`.
    /// AzaharFrame fixd: times Ratio.
    double PredictUp(double gpu_ms, int factor) const;
    /// The next factor's pixels over this one's, ((f + 1) / f)^2.
    static double PixelRatio(int factor);
    /// AzaharFrame fixd: the cost of `factor` + 1 over `factor` a step up is predicted with: the
    /// pixels', lowered by what says the step is cheaper (a step down's measurement, the
    /// medians of the two factors' seconds), never under a step up's own measurement.
    double Ratio(int factor) const;
    /// AzaharFrame fixd: Ratio and where it came from, for the log.
    std::string RatioWhy(int factor) const;
    /// AzaharFrame fixd: the ratio measured at the step ups and the step downs between `factor`
    /// and `factor` + 1 (0: none yet).
    double MeasuredUp(int factor) const;
    double MeasuredDown(int factor) const;
    /// AzaharFrame fixd: the median GPU time of the last kCostKeep judged seconds at `factor`
    /// (0: under kCostSeconds of them), and `factor` + 1's over `factor`'s (0: either unknown).
    double Cost(int factor) const;
    double CostRatio(int factor) const;
    /// AzaharFrame fixd: judged seconds since the level last changed (what "settled" means).
    int Held() const {
        return held;
    }
    /// AzaharFrame fixd: the level a failed step up went to is waiting (or for the game).
    bool Blocked(int level) const;
    /// AzaharFrame fixd: what the last Judge did: 1 a step down, -1 a step up, 0 neither (a
    /// change of the drop without a step is the ceiling or the floor moving, or a reset).
    int LastStep() const {
        return last_step;
    }

private:
    void StepDone(bool down, int from_factor, double from_cost);
    void Measure(const Second& s, int factor);

    int drop = 0;
    int min_drop_ = 0; ///< AzaharFrame bfidr: the screen's ceiling (Judge)
    int bad = 0;
    int symptom_bad = 0; ///< AzaharFrame bfidr: SymptomBad seconds in a row
    bool last_symptoms = false;
    SymptomWindow window; ///< AzaharFrame bfidr
    SymptomWindow step_window; ///< AzaharFrame bfidr: StepWindow
    int good = 0;
    int settle = 0;
    int hold = 0; ///< AzaharFrame ui2: kHoldAfterDown left
    int held = 0; ///< AzaharFrame fixd: judged seconds at this level
    int last_step = 0; ///< AzaharFrame fixd: LastStep
    double now = 0.0; ///< seconds since the start
    double last_up = -1e9;
    /// Per drop level (the level a step up went to): failed step ups and until when it waits.
    int failed[kMaxSteps] = {};
    double blocked_until[kMaxSteps] = {};
    /// AzaharFrame fixd: the measured cost ratios, per factor (f to f + 1; 0 not measured), the
    /// last judged seconds' GPU times per factor, the current run's GPU time (bad or good
    /// seconds), and a step's measurement under way.
    double up_ratio[kMaxSteps + 1] = {};
    double down_ratio[kMaxSteps + 1] = {};
    float costs[kMaxSteps + 1][kCostKeep] = {};
    int cost_n[kMaxSteps + 1] = {};
    double run_sum = 0.0;
    int run_n = 0;
    int measure_from = 0;      ///< the factor stepped from (0: nothing to measure)
    bool measure_down = false; ///< the step was down (its run the GPU behind)
    double measure_cost = 0.0; ///< its cost, the run that led to the step
    double measure_sum = 0.0;
    int measure_n = 0;
};

/// AzaharFrame fixd: the steps under `base` to start a game at, for the factor remembered for it
/// (0: none): never above the ceiling nor under the floor (max_drop).
constexpr int StartDrop(int base, int remembered, int max_drop) {
    if (remembered <= 0 || base <= remembered) {
        return 0;
    }
    const int steps = base - remembered;
    return steps < max_drop ? steps : max_drop;
}

/// AzaharFrame fixd: the remembered levels, one line per game: "<title ID, 16 hex> <factor>".
/// Lines this build cannot read are left out; a title ID of 0 is never kept.
inline constexpr const char* kStoreFile = "dynamic-resolution.txt";
/// AzaharFrame fixd: judged seconds at one level before it is remembered for the game (a level
/// it steps down to is remembered at once).
inline constexpr int kRememberAfter = 60;
std::string FormatStore(const std::vector<std::pair<uint64_t, int>>& levels);
std::vector<std::pair<uint64_t, int>> ParseStore(const std::string& text);

/// AzaharFrame fixd: where the remembered levels live (the file under the user folder by
/// default; a test sets its own). `load` gives the whole list, `save` writes it (it may run on
/// another thread).
struct LevelStore {
    std::function<std::vector<std::pair<uint64_t, int>>()> load;
    std::function<void(std::vector<std::pair<uint64_t, int>>)> save;
    std::function<uint64_t()> title; ///< the running game's title ID (0: none)
};
void SetLevelStore(LevelStore store);
/// The factor remembered for `title` (0: none).
int RememberedFactor(uint64_t title);

/// Frame loop side: the controller for the running session, fed by Runtime::LogSummary.
void Install();
void Uninstall();
/// XR thread, once a second: judges it when the setting is on (else nothing is taken off).
void NoteSecond(const Second& s);
/// The steps taken off now.
int Drop();
/// Any thread: from the factor itself again.
void Reset();
/// AzaharFrame ui2: the setting was turned off or on in this game. Off: the full factor at once,
/// and the controller is kept aside; on again: it carries on where it was (its level and what it
/// learnt), so turning it off and on is not a start from the full factor that needs bad seconds
/// all over again. A new game or a new Internal Resolution drops the copy (Reset).
void SetEnabled(bool on);

} // namespace DynamicResolution

} // namespace FrameXr
