// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include "common/frame_emu_poll.h"
#include "common/frame_test_switches.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"
#include "frame_xr/frame_xr.h"
#include "frame_xr/input/kiu_hooks.h"
#include "frame_xr/kiu_hooks.h"
#include "frame_xr/kiu_scan.h"
#include "frame_xr/test_switches.h"

// AzaharFrame kiuhook (kiu_hooks.h)
namespace FrameXr::Kiu::Hooks {

namespace {

namespace KH = Input::KiuHooks;

constexpr const char* kSwitchNoCull = "AZAHARFRAME_KIU_NOCULL";
constexpr const char* kSwitchBulletKeep = "AZAHARFRAME_KIU_BULLET_KEEP";
constexpr const char* kSwitchEye = "AZAHARFRAME_KIU_EYE";
constexpr const char* kSwitchEyeOffset = "AZAHARFRAME_KIU_EYE_OFFSET";
constexpr const char* kSwitchHide = "AZAHARFRAME_KIU_HIDE_PLAYER";
constexpr const char* kSwitchShot = "AZAHARFRAME_KIU_SHOT_TEST";
constexpr const char* kSwitchShotNoLock = "AZAHARFRAME_KIU_SHOT_NO_LOCK";
constexpr const char* kSwitchEnemyKeep = "AZAHARFRAME_KIU_ENEMY_KEEP";
constexpr const char* kSwitchEnemyKeepS = "AZAHARFRAME_KIU_ENEMY_KEEP_S";
constexpr const char* kSwitchGroupKeep = "AZAHARFRAME_KIU_GROUP_KEEP";
constexpr const char* kSwitchTrace = "AZAHARFRAME_KIU_HOOK_TRACE";

constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;
/// With no switch on, how often the player is looked for (to show him again after a save state
/// that was made while he was hidden).
constexpr uint32_t kIdleLookVBlanks = 60;

int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::optional<std::string> Switch(const char* name) {
    return Common::FrameTestSwitches::Value(name);
}
bool SwitchIs(const char* name, const char* value) {
    const auto v = Switch(name);
    return v && *v == value;
}

/// The game's memory through the emulator: only mapped addresses are read or written; a code
/// word's write drops every CPU core's cached code for it.
class GuestMemory final : public KH::GameMemory {
public:
    GuestMemory(Core::System& system, Memory::MemorySystem& memory, const Kernel::Process& process)
        : system_(system), memory_(memory), process_(process) {}
    bool Read32(uint32_t addr, uint32_t& value) const override {
        if (!Mapped32(addr)) {
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
    bool WriteCode(uint32_t addr, uint32_t value) override {
        if (!Mapped32(addr)) {
            return false;
        }
        memory_.Write32(process_, addr, value);
        system_.InvalidateCacheRange(addr, sizeof(uint32_t));
        return true;
    }
    bool WriteData(uint32_t addr, uint32_t value) override {
        if (!Mapped32(addr)) {
            return false;
        }
        memory_.Write32(process_, addr, value);
        return true;
    }

private:
    bool Mapped32(uint32_t addr) const {
        return (addr & 3u) == 0 && memory_.IsValidVirtualAddress(process_, addr) &&
               memory_.IsValidVirtualAddress(process_, addr + 3);
    }
    Core::System& system_;
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
    uint32_t serial = 0;
    uint32_t idle_vblanks = 0;
    bool have_last = false;
    KH::SyncResult last;
    KH::PlayerGuard last_guard = KH::PlayerGuard::NoRoot;
    bool hidden = false;
    int64_t poll_ns = 0;
    int64_t trace_ns = 0;
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
    LOG_INFO(Frontend, "xr: Kid Icarus hooks: {}", note);
}

double TicksToS(int64_t ticks) {
    return static_cast<double>(ticks) / static_cast<double>(BASE_CLOCK_RATE_ARM11);
}

std::string Text(const KH::Vec3& v) {
    return fmt::format("({:.3f}, {:.3f}, {:.3f})", v.x, v.y, v.z);
}
std::string Text(const KH::Quat& q) {
    return fmt::format("({:.4f}, {:.4f}, {:.4f}, {:.4f})", q.x, q.y, q.z, q.w);
}

/// In Flat no XR thread reads the switches file again: this hook does, once a second.
void PollSwitchesInFlat(State& s, int64_t host_now) {
    if (FrameXr::IsStarted() || host_now - s.poll_ns < 1'000'000'000) {
        return;
    }
    s.poll_ns = host_now;
    for (const auto& change : Common::FrameTestSwitches::Live().Poll()) {
        LOG_INFO(Frontend, "xr: {}", FrameXr::TestSwitches::LogLine(change));
    }
}

void LogChanges(State& s, const KH::SyncResult& r) {
    if (r.image_writes != 0) {
        LOG_INFO(Frontend,
                 "xr: Kid Icarus hooks: the stubs written to the code cave ({} words at "
                 "0x{:08X}, version {}; mailbox at 0x{:08X})",
                 r.image_writes, KH::kImageBase, KH::Stubs::kImageVersion, KH::kMailbox);
    }
    for (std::size_t g = 0; g < KH::kGroupCount; ++g) {
        const KH::GroupState now = r.state[g];
        if (s.have_last && s.last.state[g] == now) {
            continue;
        }
        if (!s.have_last && now == KH::GroupState::Out) {
            continue;
        }
        const auto group = static_cast<KH::Group>(g);
        if (now == KH::GroupState::Different) {
            LOG_WARNING(Frontend,
                        "xr: Kid Icarus hooks: {}: {} (0x{:08X} holds 0x{:08X}); nothing of it "
                        "is written",
                        KH::GroupName(group), KH::GroupStateName(now), r.different_addr,
                        r.different_word);
        } else {
            LOG_INFO(Frontend, "xr: Kid Icarus hooks: {}: {}", KH::GroupName(group),
                     KH::GroupStateName(now));
        }
    }
    s.last = r;
    s.have_last = true;
}

void Hook(int64_t ticks) {
    State& s = TheState();
    const int64_t host_now = NowNs();
    PollSwitchesInFlat(s, host_now);
    Core::System& system = Core::System::GetInstance();
    if (!system.IsPoweredOn()) {
        s.process = nullptr;
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
        s.process = nullptr;
        return;
    }
    const uint64_t title = process->codeset->program_id;
    if (process.get() != s.process || title != s.title) {
        const int64_t poll_ns = s.poll_ns;
        s = State{};
        s.poll_ns = poll_ns;
        s.process = process.get();
        s.title = title;
    }
    const bool want_nocull = SwitchIs(kSwitchNoCull, "1");
    const bool want_keep = SwitchIs(kSwitchBulletKeep, "1");
    const bool want_eye = SwitchIs(kSwitchEye, "1");
    const bool want_hide = SwitchIs(kSwitchHide, "1");
    const auto shot_text = Switch(kSwitchShot);
    float shot_values[5] = {};
    const int shot_count = shot_text ? KH::ParseFloats(shot_text->c_str(), shot_values, 5) : 0;
    const bool want_shot = shot_count == 2 || shot_count == 5;
    // The timed keep replaces the plain one (they share a site).
    float keep_seconds = 0.0f;
    if (const auto text = Switch(kSwitchEnemyKeepS)) {
        float v = 0.0f;
        if (KH::ParseFloats(text->c_str(), &v, 1) == 1 && v > 0.0f) {
            keep_seconds = v;
        }
    }
    const bool want_keep_timed = keep_seconds > 0.0f;
    const bool want_enemy_keep = !want_keep_timed && SwitchIs(kSwitchEnemyKeep, "1");
    const bool want_group_keep = SwitchIs(kSwitchGroupKeep, "1");
    const auto trace_text = Switch(kSwitchTrace);
    const int trace = trace_text ? std::atoi(trace_text->c_str()) : 0;
    const bool any = want_nocull || want_keep || want_eye || want_hide || want_shot ||
                     want_keep_timed || want_enemy_keep || want_group_keep || trace > 0;

    if (title != Input::KiuTurn::kTitleEur) {
        if (any) {
            Note(s, fmt::format("off: only the European game (0004000000030200) is mapped, this "
                                "is {:016X}",
                                title));
        }
        return;
    }
    Memory::MemorySystem& memory = system.Memory();
    GuestMemory mem(system, memory, *process);
    if (!s.checked) {
        s.checked = true;
        s.code_ok = KH::GameCodeMatches(mem);
    }
    if (!s.code_ok) {
        if (any) {
            Note(s, "off: the game's code differs from the European game's (0x704178, 0x703894, "
                    "0x703D4C)");
        }
        return;
    }
    if (!any) {
        // Nothing asked for. A save state made with a switch on brings our words (and a hidden
        // player) back: take them out; otherwise only look.
        const bool look = s.idle_vblanks++ % kIdleLookVBlanks == 0;
        if (!KH::AnyOurs(mem) && !(s.hidden || look)) {
            return;
        }
    } else {
        s.idle_vblanks = 0;
        Note(s, "the game's code matches (European game)");
    }

    const KH::Player p = KH::FindPlayer(mem);
    KH::EyeRequest eye;
    eye.on = want_eye;
    if (const auto text = Switch(kSwitchEyeOffset)) {
        float v[3] = {};
        if (KH::ParseFloats(text->c_str(), v, 3) == 3) {
            eye.offset = {v[0], v[1], v[2]};
        }
    }
    KH::ShotRequest shot;
    if (want_shot && p.Ok()) {
        // Laid out on the game camera's view direction (the player's body turns as he fires).
        KH::Vec3 eye_at, look_at, view = KH::Rotate(p.world_q, {0.0f, 0.0f, 1.0f});
        if (KH::ReadVec3(mem, KH::kSceneEye, eye_at) && KH::ReadVec3(mem, KH::kSceneLookAt, look_at)) {
            view = {look_at.x - eye_at.x, look_at.y - eye_at.y, look_at.z - eye_at.z};
        }
        shot = KH::ShotTest(view, shot_values[0] * kDegToRad, shot_values[1] * kDegToRad,
                            {shot_values[2], shot_values[3], shot_values[4]}, shot_count == 5,
                            SwitchIs(kSwitchShotNoLock, "1"));
    }
    const KH::Mailbox mailbox = KH::Fill(p, eye, shot, ++s.serial, keep_seconds);
    const uint32_t wanted = (want_nocull ? KH::Bit(KH::Group::NoCull) : 0u) |
                            (want_keep ? KH::Bit(KH::Group::BulletKeep) : 0u) |
                            (want_eye ? KH::Bit(KH::Group::Eye) : 0u) |
                            (want_shot ? KH::Bit(KH::Group::Shot) : 0u) |
                            (want_enemy_keep ? KH::Bit(KH::Group::EnemyKeep) : 0u) |
                            (want_keep_timed ? KH::Bit(KH::Group::EnemyKeepTimed) : 0u) |
                            (want_group_keep ? KH::Bit(KH::Group::GroupKeep) : 0u);
    const KH::SyncResult r = KH::Sync(mem, wanted, mailbox);
    LogChanges(s, r);

    // The player's model: a data write into his actor, only while every guard holds; put back
    // whenever it is not wanted and the actor is known.
    if (p.actor != 0) {
        const bool hide = want_hide && p.Ok();
        if (const auto bits = KH::HideWrite(p.local_scale_bits, hide)) {
            if (mem.WriteData(p.actor + KH::kActorLocalScale, *bits)) {
                s.hidden = hide;
                LOG_INFO(Frontend, "xr: Kid Icarus hooks: the player's model {}",
                         hide ? "hidden (local scale 0.001)" : "shown again (local scale 1)");
            }
        } else if (!hide) {
            s.hidden = false;
        }
    }

    if (trace <= 0) {
        return;
    }
    const bool guard_changed = p.guard != s.last_guard;
    s.last_guard = p.guard;
    if (trace < 2 && !guard_changed && host_now - s.trace_ns < 1'000'000'000) {
        return;
    }
    s.trace_ns = host_now;
    std::string groups;
    for (std::size_t g = 0; g < KH::kGroupCount; ++g) {
        groups += fmt::format("{}{} {}", g ? ", " : "", KH::GroupName(static_cast<KH::Group>(g)),
                              r.state[g] == KH::GroupState::In    ? "in"
                              : r.state[g] == KH::GroupState::Out ? "out"
                                                                  : "OFF");
    }
    std::string player = fmt::format("no player ({})", KH::PlayerGuardName(p.guard));
    if (p.actor != 0) {
        KH::Vec3 scene_eye;
        const bool have_eye = KH::ReadVec3(mem, KH::kSceneEye, scene_eye);
        const KH::Vec3 off = mailbox.eye_offset;
        player = fmt::format(
            "{}{} actor {:08X} world {} local {} q {} scale {:.4f}; scene eye {}{}",
            p.land ? "land" : "air",
            p.Ok() ? "" : fmt::format(" ({})", KH::PlayerGuardName(p.guard)), p.actor,
            Text(p.world_p), Text(p.local_p), Text(p.world_q),
            std::bit_cast<float>(p.local_scale_bits), have_eye ? Text(scene_eye) : "-",
            have_eye && mailbox.eye_on
                ? fmt::format(" (eye - player - offset {})",
                              Text(KH::Vec3{scene_eye.x - p.world_p.x - off.x,
                                            scene_eye.y - p.world_p.y - off.y,
                                            scene_eye.z - p.world_p.z - off.z}))
                : std::string{});
    }
    std::string box;
    if (mailbox.eye_on) {
        box += fmt::format("; eye offset {}", Text(mailbox.eye_offset));
    }
    if (mailbox.shot_rotate || mailbox.shot_move || mailbox.shot_no_target) {
        box += fmt::format("; shot{}{}{} rotation {} frame {} offset {}",
                           mailbox.shot_rotate ? " rotate" : "", mailbox.shot_move ? " move" : "",
                           mailbox.shot_no_target ? " no-lock" : "", Text(mailbox.shot_rotation),
                           Text(mailbox.shot_frame), Text(mailbox.shot_offset));
    }
    if (mailbox.keep_seconds > 0.0f) {
        box += fmt::format("; fliers kept {:.1f} s", mailbox.keep_seconds);
    }
    LOG_INFO(Frontend, "xr: Kid Icarus hooks: t {:.4f} [{}] {}{}", TicksToS(ticks), groups, player,
             box);
}

} // namespace

void OnGameBoot() {
    static std::atomic<bool> installed{false};
    if (!installed.exchange(true)) {
        Common::FrameEmuPoll::SetPatch(Hook);
    }
}

} // namespace FrameXr::Kiu::Hooks
