// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include "common/frame_emu_poll.h"
#include "common/frame_test_switches.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"
#include "frame_xr/input/kiu_turn.h"
#include "frame_xr/kiu_scan.h"
#include "frame_xr/kiu_turn.h"

// AzaharFrame kiuturn (kiu_turn.h)
namespace FrameXr::Kiu::Turn {

namespace {

namespace KT = Input::KiuTurn;

constexpr const char* kSwitchOn = "AZAHARFRAME_KIU_RIGHT_STICK_TURN";
constexpr const char* kSwitchSnap = "AZAHARFRAME_KIU_SNAP_TURN";
constexpr const char* kSwitchDps = "AZAHARFRAME_KIU_TURN_DPS";
constexpr const char* kSwitchTrace = "AZAHARFRAME_KIU_CAM_TRACE";

/// A stick sample or a land look older than this counts as none.
constexpr int64_t kStaleNs = 200'000'000;
/// The trace: at most this many lines a second, and the applied / lost summary this often.
constexpr int kTraceLinesPerS = 20;
constexpr int64_t kSummaryNs = 10'000'000'000;

int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool SwitchIs(const char* name, const char* value) {
    const auto v = Common::FrameTestSwitches::Value(name);
    return v && *v == value;
}

std::mutex g_mutex;
float g_stick_x = 0.0f;    ///< under g_mutex
float g_turn_speed = 1.0f; ///< under g_mutex
int64_t g_stick_ns = 0;    ///< under g_mutex; 0: no stick
std::atomic<bool> g_installed{false};
std::atomic<int64_t> g_land_ns{0}; ///< host time of the last VBlank that found a land battle

/// The game's memory through the emulator: only mapped addresses are read.
class GuestReader final : public KT::Reader {
public:
    GuestReader(Memory::MemorySystem& memory, const Kernel::Process& process)
        : memory_(memory), process_(process) {}
    bool Read32(uint32_t addr, uint32_t& value) const override {
        if ((addr & 3u) != 0 || !memory_.IsValidVirtualAddress(process_, addr) ||
            !memory_.IsValidVirtualAddress(process_, addr + 3)) {
            return false;
        }
        value = memory_.Read32(process_, addr);
        return true;
    }
    bool Read8(uint32_t addr, uint8_t& value) const override {
        if (!memory_.IsValidVirtualAddress(process_, addr)) {
            return false;
        }
        value = memory_.Read8(process_, addr);
        return true;
    }

private:
    Memory::MemorySystem& memory_;
    const Kernel::Process& process_;
};

/// Emulation thread only.
struct State {
    const Kernel::Process* process = nullptr;
    uint64_t title = 0;
    bool checked = false;
    bool code_ok = false;
    std::string note;
    KT::Ledger ledger;
    KT::Snap::Tracker snap;
    int64_t last_ticks = 0;
    // The trace.
    int64_t line_window_ns = 0;
    int lines = 0;
    int64_t summary_ns = 0;
    KT::Guard last_guard = KT::Guard::NotPlaying;
    float hid_yaw = 0.0f;
    uint32_t hid_yaw_in = 0;
    // Since the last summary.
    uint32_t wrote = 0, rewrote = 0, applied = 0, lost = 0, foreign = 0, dropped = 0;
    uint32_t scaled = 0; ///< applied at the mod's x1.25
    double applied_rad = 0.0, lost_rad = 0.0;
    float last_written = 0.0f;
};

State& TheState() {
    static State state;
    return state;
}

void Note(State& s, const std::string& note) {
    if (note == s.note) {
        return;
    }
    s.note = note;
    LOG_INFO(Frontend, "xr: Kid Icarus turn: {}", note);
}

void Idle(State& s) {
    g_land_ns.store(0, std::memory_order_relaxed);
    s.ledger.Reset();
    s.snap.Reset();
    s.last_ticks = 0;
}

double TicksToS(int64_t ticks) {
    return static_cast<double>(ticks) / static_cast<double>(BASE_CLOCK_RATE_ARM11);
}

/// One trace line within the budget (kTraceLinesPerS a second of host time).
bool TraceBudget(State& s, int64_t host_now) {
    if (host_now - s.line_window_ns >= 1'000'000'000) {
        s.line_window_ns = host_now;
        s.lines = 0;
    }
    return s.lines++ < kTraceLinesPerS;
}

void Summary(State& s, int64_t host_now) {
    if (s.summary_ns == 0) {
        // The trace just started: count from now, not from what ran before.
        s.summary_ns = host_now;
        s.wrote = s.rewrote = s.applied = s.lost = s.foreign = s.dropped = s.scaled = 0;
        s.applied_rad = s.lost_rad = 0.0;
        return;
    }
    if (host_now - s.summary_ns < kSummaryNs) {
        return;
    }
    s.summary_ns = host_now;
    const uint32_t verdicts = s.applied + s.lost + s.foreign;
    if (verdicts != 0 || s.wrote != 0 || s.dropped != 0) {
        LOG_INFO(Frontend,
                 "xr: Kid Icarus turn: last 10 s: {} written ({} rewritten), {} applied ({:.3f} "
                 "rad, {} at x1.25), {} lost, {} foreign ({:.3f} rad not taken), {} dropped by a "
                 "guard; lost rate {:.1f}%",
                 s.wrote, s.rewrote, s.applied, s.applied_rad, s.scaled, s.lost, s.foreign,
                 s.lost_rad, s.dropped,
                 verdicts ? 100.0 * static_cast<double>(s.lost + s.foreign) / verdicts : 0.0);
    }
    s.wrote = s.rewrote = s.applied = s.lost = s.foreign = s.dropped = s.scaled = 0;
    s.applied_rad = s.lost_rad = 0.0;
}

/// The reticle's pixel and flags for the trace (the land aim only).
std::string ReticleText(const KT::Reader& m, const KT::Camera& c) {
    uint32_t x = 0, y = 0, flags = 0;
    if (!c.land || !m.Read32(c.aim + KT::kAimX, x) || !m.Read32(c.aim + KT::kAimY, y) ||
        !m.Read32(c.aim + KT::kAimFlags, flags)) {
        return "reticle -";
    }
    return fmt::format("reticle {:.1f},{:.1f} +84 {:02X}", KT::AsFloat(x), KT::AsFloat(y),
                       flags & 0xFFu);
}

void Hook(int64_t ticks, bool vblank) {
    State& s = TheState();
    const bool on = SwitchIs(kSwitchOn, "1");
    const bool trace = SwitchIs(kSwitchTrace, "1");
    if (!trace) {
        s.summary_ns = 0; // the next trace counts from its start
    }
    if (!on && !trace) {
        if (s.last_ticks != 0 || g_land_ns.load(std::memory_order_relaxed) != 0) {
            Idle(s);
        }
        return;
    }
    if (!vblank && !trace) {
        return; // the pad update only traces
    }
    Core::System& system = Core::System::GetInstance();
    if (!system.IsPoweredOn()) {
        Idle(s);
        return;
    }
    std::shared_ptr<Kernel::Process> process;
    for (const std::shared_ptr<Kernel::Process>& candidate : system.Kernel().GetProcessList()) {
        if (candidate && candidate->codeset && IsKidIcarus(candidate->codeset->program_id)) {
            process = candidate;
            break;
        }
    }
    if (!process) {
        Note(s, "off: not Kid Icarus: Uprising");
        Idle(s);
        return;
    }
    const uint64_t title = process->codeset->program_id;
    if (process.get() != s.process || title != s.title) {
        s.process = process.get();
        s.title = title;
        s.checked = false;
        s.code_ok = false;
        Idle(s);
    }
    if (title != KT::kTitleEur) {
        Note(s, fmt::format("off: only the European game (0004000000030200) is mapped, this is "
                            "{:016X}",
                            title));
        Idle(s);
        return;
    }
    Memory::MemorySystem& memory = system.Memory();
    const GuestReader reader(memory, *process);
    if (!s.checked) {
        s.checked = true;
        s.code_ok = KT::CodeMatches(reader);
        Note(s, s.code_ok ? "the game's code matches (European game)"
                          : "off: the game's code differs from the European game's at the camera "
                            "update (0x704178, 0x703894, 0x703D4C)");
    }
    if (!s.code_ok) {
        Idle(s);
        return;
    }
    const KT::Camera c = KT::FindCamera(reader);
    const int64_t host_now = NowNs();
    const double game_s = TicksToS(ticks);
    if (!vblank) {
        // The pad update: the trace only, when the game's camera update moved something since
        // the last look (where its update falls between VBlanks and pad updates).
        if (trace && c.Ok() && (c.yaw != s.hid_yaw || c.yaw_in_bits != s.hid_yaw_in)) {
            s.hid_yaw = c.yaw;
            s.hid_yaw_in = c.yaw_in_bits;
            if (TraceBudget(s, host_now)) {
                LOG_INFO(Frontend, "xr: Kid Icarus turn: t {:.4f} pad yaw {:.5f} D38 {:.5f}",
                         game_s, c.yaw, KT::AsFloat(c.yaw_in_bits));
            }
        }
        return;
    }
    if (on && c.land) {
        g_land_ns.store(host_now, std::memory_order_relaxed);
    } else {
        g_land_ns.store(0, std::memory_order_relaxed);
    }
    float stick_x = 0.0f, turn_speed = 1.0f;
    bool fresh = false;
    {
        std::scoped_lock lock{g_mutex};
        if (g_stick_ns != 0 && host_now - g_stick_ns <= kStaleNs) {
            stick_x = g_stick_x;
            turn_speed = g_turn_speed;
            fresh = true;
        }
    }
    const float dt = s.last_ticks != 0 && ticks > s.last_ticks
                         ? static_cast<float>(TicksToS(ticks - s.last_ticks))
                         : 0.0f;
    s.last_ticks = ticks;
    KT::Ledger::Step step;
    float add = 0.0f;
    int snap_deg = 0;
    if (on) {
        const auto snap_text = Common::FrameTestSwitches::Value(kSwitchSnap);
        snap_deg = KT::SnapDegreesFrom(snap_text ? snap_text->c_str() : nullptr);
        float cap = KT::kSmoothCapRad;
        if (snap_deg > 0 && !fresh) {
            // No fresh stick (a hitch, the menu): the tracker keeps its state, so a stick held
            // through it neither snaps again nor loses its cooldown.
            cap = static_cast<float>(snap_deg) * KT::kDegToRad;
        } else if (snap_deg > 0) {
            KT::Snap::Input in;
            in.enabled = true;
            in.connected = fresh;
            in.stick_x = stick_x;
            in.accepting = c.Ok();
            in.degrees = snap_deg;
            in.now = game_s;
            add = -s.snap.Update(in) * KT::kDegToRad;
            cap = static_cast<float>(snap_deg) * KT::kDegToRad;
        } else {
            s.snap.Reset();
            const auto dps_text = Common::FrameTestSwitches::Value(kSwitchDps);
            const float dps = KT::SmoothDpsFrom(dps_text ? dps_text->c_str() : nullptr);
            add = fresh ? KT::SmoothYaw(stick_x, turn_speed, dps, dt) : 0.0f;
        }
        const float before = s.ledger.owed();
        step = s.ledger.Update(c, add, cap);
        if (step.verdict == KT::Event::Dropped) {
            s.lost_rad += std::fabs(before);
        }
        if (step.write) {
            // Only the camera's yaw input, of the camera the guards just checked.
            memory.Write32(*process, c.cam + KT::kCamYawIn, std::bit_cast<uint32_t>(step.value));
        }
    } else {
        s.ledger.Reset();
        s.snap.Reset();
    }
    switch (step.verdict) {
    case KT::Event::Applied:
        s.applied++;
        s.applied_rad += std::fabs(s.last_written);
        s.scaled += step.ratio > 1.0f ? 1 : 0;
        break;
    case KT::Event::Lost:
        s.lost++;
        s.lost_rad += std::fabs(s.last_written);
        break;
    case KT::Event::Foreign:
        s.foreign++;
        s.lost_rad += std::fabs(s.last_written);
        break;
    case KT::Event::Dropped:
        s.dropped++;
        break;
    default:
        break;
    }
    if (step.write) {
        if (step.event == KT::Event::Rewrote) {
            s.rewrote++;
        } else {
            s.wrote++;
        }
        s.last_written = step.value;
    }
    if (!trace) {
        return;
    }
    const bool guard_changed = c.guard != s.last_guard;
    s.last_guard = c.guard;
    if ((step.event != KT::Event::None || guard_changed || add != 0.0f) &&
        TraceBudget(s, host_now)) {
        uint32_t rate = 0;
        if (c.cam != 0) {
            reader.Read32(c.cam + KT::kCamYawRate, rate);
        }
        LOG_INFO(Frontend,
                 "xr: Kid Icarus turn: t {:.4f} vblank {} {}cam {:08X} mode {} 31A {} yaw {:.5f} "
                 "pitch {:.5f} D38 {:.5f} D3C {:.5f} 7D8 {:.5f}; {}{}{} owed {:.5f}; {}; stick "
                 "{:+.2f}{}{}",
                 game_s, c.land ? "land" : "-",
                 c.Ok() ? "" : fmt::format("({}) ", KT::GuardName(c.guard)), c.cam, c.mode,
                 c.mod_scale_flag, c.yaw, c.pitch, KT::AsFloat(c.yaw_in_bits),
                 KT::AsFloat(c.pitch_in_bits), KT::AsFloat(rate), KT::EventName(step.event),
                 step.verdict == KT::Event::Applied
                     ? fmt::format(" (yaw {:+.5f}, x{:.2f})", step.delta, step.ratio)
                 : step.verdict == KT::Event::Lost || step.verdict == KT::Event::Foreign
                     ? fmt::format(" (yaw {:+.5f})", step.delta)
                     : std::string{},
                 step.write ? fmt::format(", wrote {:+.5f}", step.value) : std::string{},
                 s.ledger.owed(), ReticleText(reader, c), stick_x, fresh ? "" : " (none)",
                 snap_deg ? fmt::format(", snap {}", snap_deg) : std::string{});
    }
    Summary(s, host_now);
}

} // namespace

bool RightStickOn() {
    return SwitchIs(kSwitchOn, "1");
}

bool Wanted() {
    return RightStickOn() || SwitchIs(kSwitchTrace, "1");
}

void Publish(float stick_x, float turn_speed) {
    {
        std::scoped_lock lock{g_mutex};
        g_stick_x = std::isfinite(stick_x) ? stick_x : 0.0f;
        g_turn_speed = std::isfinite(turn_speed) ? turn_speed : 1.0f;
        g_stick_ns = NowNs();
    }
    if (!g_installed.exchange(true)) {
        Common::FrameEmuPoll::SetTurn(Hook);
    }
}

void Stop() {
    std::scoped_lock lock{g_mutex};
    g_stick_x = 0.0f;
    g_stick_ns = 0;
}

bool LandActive() {
    const int64_t at = g_land_ns.load(std::memory_order_relaxed);
    return at != 0 && NowNs() - at <= kStaleNs && RightStickOn();
}

} // namespace FrameXr::Kiu::Turn
