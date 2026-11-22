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
#include "common/settings.h" // AzaharFrame kiutoggles: Over the Shoulder
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"
#include "frame_xr/auto_shape.h" // AzaharFrame kiufix2
#include "frame_xr/frame_xr.h"
#include "frame_xr/input/kiu_hooks.h"
#include "frame_xr/input/kiu_room.h" // AzaharFrame kiuvr
#include "frame_xr/kiu_first_person.h"
#include "frame_xr/kiu_hooks.h"
#include "frame_xr/kiu_turn.h" // AzaharFrame kiufov
#include "frame_xr/kiu_scan.h"
#include "frame_xr/test_switches.h"
#include "video_core/kiu_view_tap.h" // AzaharFrame kiuvr: the view register's tap

// AzaharFrame kiuhook (kiu_hooks.h)
namespace FrameXr::Kiu::Hooks {

namespace {

namespace KH = Input::KiuHooks;

constexpr const char* kSwitchNoCull = "AZAHARFRAME_KIU_NOCULL";
constexpr const char* kSwitchBulletKeep = "AZAHARFRAME_KIU_BULLET_KEEP";
constexpr const char* kSwitchEye = "AZAHARFRAME_KIU_EYE";
constexpr const char* kSwitchEyeOffset = "AZAHARFRAME_KIU_EYE_OFFSET";
constexpr const char* kSwitchShoulder = "AZAHARFRAME_KIU_SHOULDER"; // AzaharFrame kiuview
constexpr const char* kSwitchHide = "AZAHARFRAME_KIU_HIDE_PLAYER";
constexpr const char* kSwitchHideBy = "AZAHARFRAME_KIU_HIDE_BY"; // AzaharFrame kiu131
constexpr const char* kSwitchShot = "AZAHARFRAME_KIU_SHOT_TEST";
constexpr const char* kSwitchShotNoLock = "AZAHARFRAME_KIU_SHOT_NO_LOCK";
constexpr const char* kSwitchShotLock = "AZAHARFRAME_KIU_SHOT_LOCK"; // AzaharFrame aa6dof2
constexpr const char* kSwitchShotAhead = "AZAHARFRAME_KIU_SHOT_AHEAD"; // AzaharFrame kiu130
constexpr const char* kSwitchEnemyKeep = "AZAHARFRAME_KIU_ENEMY_KEEP";
constexpr const char* kSwitchEnemyKeepS = "AZAHARFRAME_KIU_ENEMY_KEEP_S";
constexpr const char* kSwitchGroupKeep = "AZAHARFRAME_KIU_GROUP_KEEP";
constexpr const char* kSwitchNoFollow = "AZAHARFRAME_KIU_NO_FOLLOW";
constexpr const char* kSwitchNoPlayerShadow = "AZAHARFRAME_KIU_NO_PLAYER_SHADOW"; // kiuperf
constexpr const char* kSwitchFp2d = "AZAHARFRAME_KIU_FP_2D"; // AzaharFrame kiu2d
constexpr const char* kSwitchReticleSize = "AZAHARFRAME_KIU_RETICLE_SIZE"; // AzaharFrame kiu131
constexpr const char* kSwitchReticleTexture = "AZAHARFRAME_KIU_RETICLE_TEX";
constexpr const char* kSwitchTrace = "AZAHARFRAME_KIU_HOOK_TRACE";
// AzaharFrame kiuvr (kiu_first_person.h)
constexpr const char* kSwitchFpTrace = "AZAHARFRAME_KIU_FP_TRACE";
constexpr const char* kSwitchFpProbe = "AZAHARFRAME_KIU_FP_PROBE";
/// The eye above the player's position in an air battle (he lies flat there), his own axes.
constexpr KH::Vec3 kEyeOffsetAir{0.0f, 3.0f, 0.0f};

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
        // The hooks' own patches only: every page table's JIT may hold the game's old block.
        system_.InvalidateCacheRangeAllContexts(addr, sizeof(uint32_t));
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
    uint32_t run = 0; ///< Kiu::FirstPerson::RunCount() when this state began
    bool checked = false;
    bool code_ok = false;
    /// AzaharFrame kiuusa: the build whose code matched (code_ok), else the European one's.
    const Input::KiuTurn::Build* build = &Input::KiuTurn::kBuildEur;
    std::string note;
    uint32_t serial = 0;
    uint32_t idle_vblanks = 0;
    bool have_last = false;
    KH::SyncResult last;
    KH::PlayerGuard last_guard = KH::PlayerGuard::NoRoot;
    bool hidden = false;
    /// AzaharFrame kiu131: the model node whose drawn byte this app cleared (0: none), and
    /// whether the first look of this run (a save state made while hidden) was made.
    float reticle_scale_logged = 1.0f; // AzaharFrame kiu131: Reticle Size, as last logged
    uint32_t reticle_tex_logged = 0;
    uint32_t model_hidden = 0;
    bool model_looked = false;
    int64_t poll_ns = 0;
    int64_t trace_ns = 0;
    int64_t fp_trace_ns = 0; ///< AzaharFrame kiuvr
    /// AzaharFrame kiuwide: the scene camera's shape the app asked for last (0: none yet), and
    /// the game's own shape once known.
    uint32_t wide_asked_bits = 0;
    float game_aspect = 0.0f;
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

// ---- AzaharFrame kiuvr: first person (kiu_first_person.h) ----

namespace KR = Input::KiuRoom;

std::string Csv(const KH::Vec3& v) {
    return fmt::format("{:.5f},{:.5f},{:.5f}", v.x, v.y, v.z);
}
std::string Csv(const KH::Quat& q) {
    return fmt::format("{:.6f},{:.6f},{:.6f},{:.6f}", q.x, q.y, q.z, q.w);
}

/// What the first person pass knows at this VBlank.
struct FirstPersonNow {
    Kiu::FirstPerson::Scene scene; ///< the scene camera (of the game frame before this VBlank)
    Kiu::FirstPerson::Sample sample;
    Kiu::FirstPerson::WorldRay ray; ///< the hand's ray in the world
    KR::Frame frame;                ///< valid with frame_ok
    bool frame_ok = false;
};

FirstPersonNow ReadFirstPerson(const KH::GameMemory& mem, int64_t host_now) {
    FirstPersonNow now;
    now.scene = Kiu::FirstPerson::ReadScene(mem);
    if (now.scene.ok) {
        Kiu::FirstPerson::PublishProjection(now.scene.projection); // AzaharFrame kiufov
    }
    now.sample = Kiu::FirstPerson::Latest(host_now);
    if (now.scene.ok && now.sample.view.valid && now.sample.view.units_per_metre > 0.0f) {
        now.frame = {now.sample.view.rest, now.scene.camera, now.sample.view.units_per_metre};
        now.frame_ok = true;
    }
    now.ray = Kiu::FirstPerson::HandRay(now.sample, now.scene);
    return now;
}

/// AZAHARFRAME_KIU_SHOTS_6DOF: the player's shots from the hand's ray: each bullet starts at
/// the ray's origin (the eye offset plus the origin's place from the eye: no absolute position,
/// so nothing of it is a game frame old but the camera's turn) and gets the game's kind of aim
/// quaternion for the ray's direction in place of the game's own aim (the stub keeps each
/// bullet's scatter). AzaharFrame aa6dof2: no lock target, so no shot bends off the ray to an
/// enemy beside it; AZAHARFRAME_KIU_SHOT_LOCK=1 keeps the game's lock (KH::ShotAlongRay).
/// Without a hand or a view: nothing (the game's own).
/// AzaharFrame kiu130: the start is moved along the ray past the muzzle (KH::ShotAheadFor,
/// AZAHARFRAME_KIU_SHOT_AHEAD), so a new bullet is not drawn on the weapon in hand's tip.
KH::ShotRequest ShotFromHand(const FirstPersonNow& now, const KH::Mailbox& eye_mailbox) {
    if (!now.ray.ok || !eye_mailbox.eye_on) {
        return {};
    }
    const auto ahead_text = Switch(kSwitchShotAhead);
    const float ahead = KH::ShotAheadFor(ahead_text ? ahead_text->c_str() : nullptr);
    return KH::ShotAlongRay(KR::ShotQuat(now.ray.direction),
                            eye_mailbox.eye_offset + now.ray.from_eye + now.ray.direction * ahead,
                            SwitchIs(kSwitchShotLock, "1"));
}

/// AZAHARFRAME_KIU_FP_TRACE: one line of key=value words (the proof scripts read them).
void TraceFirstPerson(State& s, const KH::GameMemory& mem, const KH::Player& p,
                      const KH::Mailbox& mailbox, const FirstPersonNow& now, int64_t ticks,
                      int64_t host_now, int level) {
    auto& tap = VideoCore::Immersive::g_world_view_tap;
    tap.wanted.store(level > 0, std::memory_order_relaxed);
    if (level <= 0 || (level < 2 && host_now - s.fp_trace_ns < 1'000'000'000)) {
        return;
    }
    s.fp_trace_ns = host_now;
    std::string line = fmt::format("t={:.4f} host={} player={} land={} playing={}",
                                   TicksToS(ticks), host_now, p.actor != 0 ? 1 : 0,
                                   p.land ? 1 : 0, p.Ok() ? 1 : 0);
    if (p.actor != 0) {
        line += fmt::format(" player_world={} player_local={} player_q={} eye_offset={}",
                            Csv(p.world_p), Csv(p.local_p), Csv(p.world_q),
                            Csv(mailbox.eye_offset));
    }
    if (now.scene.ok) {
        const KR::GameCamera& camera = now.scene.camera;
        KH::Vec3 look_at, up;
        KH::ReadVec3(mem, KH::kSceneLookAt, look_at);
        KH::ReadVec3(mem, KH::kSceneUp, up);
        line += fmt::format(" cam_eye={} cam_look={} cam_up={} cam_q={} fov={:.5f} aspect={:.5f}",
                            Csv(camera.eye), Csv(look_at), Csv(up), Csv(camera.q),
                            now.scene.projection.fov_y, now.scene.projection.aspect);
        // AzaharFrame kiupit: the node's words after the shape (+0x44 to +0x68), as floats: its
        // near plane was never found (moved out, it could cut Pit away in the air too).
        std::string node_words;
        for (uint32_t off = 0x44; off <= 0x68; off += 4) {
            uint32_t w = 0;
            node_words += fmt::format("{}{}", off == 0x44 ? "" : ",",
                                      mem.Read32(KH::kSceneNode + off, w)
                                          ? fmt::format("{:.4g}", std::bit_cast<float>(w))
                                          : std::string{"-"});
        }
        line += fmt::format(" cam_node_44={}", node_words);
        // The view register of the last world draw against that camera: how far the register's
        // axes are from the camera's (degrees) and its eye from the camera's (game units).
        float rows[12];
        uint32_t stored = 0;
        if (tap.Load(rows, stored)) {
            const KH::Vec3 rx{rows[0], rows[1], rows[2]}, ry{rows[4], rows[5], rows[6]},
                rz{rows[8], rows[9], rows[10]};
            const KH::Quat reg_q = KR::FromAxes(rx, ry, rz);
            const KH::Quat d = KH::Mul(KH::Conj(camera.q), reg_q);
            const float turn_deg =
                2.0f * std::atan2(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), std::abs(d.w)) *
                (180.0f / std::numbers::pi_v<float>);
            // eye = -R^T t
            const KH::Vec3 reg_eye{-(rx.x * rows[3] + ry.x * rows[7] + rz.x * rows[11]),
                                   -(rx.y * rows[3] + ry.y * rows[7] + rz.y * rows[11]),
                                   -(rx.z * rows[3] + ry.z * rows[7] + rz.z * rows[11])};
            line += fmt::format(" reg_count={} reg_rows={:.5f},{:.5f},{:.5f},{:.5f},{:.5f},{:.5f},"
                                "{:.5f},{:.5f},{:.5f},{:.5f},{:.5f},{:.5f} reg_turn_deg={:.4f} "
                                "reg_eye={} reg_eye_off={:.4f}",
                                stored, rows[0], rows[1], rows[2], rows[3], rows[4], rows[5],
                                rows[6], rows[7], rows[8], rows[9], rows[10], rows[11], turn_deg,
                                Csv(reg_eye), KR::Length(reg_eye - camera.eye));
        }
    }
    const auto& view = now.sample.view;
    line += fmt::format(" view={}", view.valid ? 1 : 0);
    if (view.valid) {
        const KR::Pose head = KR::RoomToRest(view.rest, view.head);
        line += fmt::format(" s={:.5f} rest_p={} rest_q={} head_p={} head_q={} head_rest_p={} "
                            "head_rest_q={} given_q={} gains={:.4f},{:.4f},{:.4f},{:.4f} "
                            "measured_aspect={:.5f}",
                            view.units_per_metre, Csv(view.rest.position),
                            Csv(view.rest.orientation), Csv(view.head.p), Csv(view.head.q),
                            Csv(head.p), Csv(head.q), Csv(view.given), view.gain_x, view.gain_y,
                            view.screen_pitch, view.screen_yaw, view.measured_aspect);
        if (now.frame_ok) {
            const KR::Pose head_world = KR::RoomToWorld(now.frame, view.head);
            line += fmt::format(" head_world={} head_ahead={}", Csv(head_world.p),
                                Csv(KR::Ahead(head_world.q)));
        }
    }
    const auto& hand = now.sample.hand;
    line += fmt::format(" hand={}", hand.valid ? 1 : 0);
    if (hand.valid) {
        line += fmt::format(" hand_o={} hand_d={} muzzle={}", Csv(hand.origin), Csv(hand.direction),
                            hand.muzzle ? 1 : 0);
        if (now.ray.ok) {
            line += fmt::format(" hand_world_o={} hand_from_eye={} hand_world_d={}",
                                Csv(now.ray.origin), Csv(now.ray.from_eye),
                                Csv(now.ray.direction));
        }
    }
    // The game's own aim: the reticle's pixel, its "held" flags, the point it has in the world;
    // and the pixel first person's aim asks for.
    if (p.aim != 0) {
        uint32_t x = 0, y = 0, flags = 0, w = 0, h = 0, cx = 0, cy = 0;
        KH::Vec3 point;
        if (mem.Read32(p.aim + 0xA4, x) && mem.Read32(p.aim + 0xA8, y) &&
            mem.Read32(p.aim + 0x84, flags) && mem.Read32(p.aim + 0xE0, w) &&
            mem.Read32(p.aim + 0xE8, h) && mem.Read32(p.aim + 0xE4, cx) &&
            mem.Read32(p.aim + 0xEC, cy) && KH::ReadVec3(mem, p.aim + 0x54, point)) {
            const KR::ReticleScreen screen{std::bit_cast<float>(w), std::bit_cast<float>(h),
                                           std::bit_cast<float>(cx), std::bit_cast<float>(cy)};
            line += fmt::format(" reticle={:.3f},{:.3f} reticle_flags={:X} reticle_screen={:.1f},"
                                "{:.1f},{:.1f},{:.1f} aim_point={}",
                                std::bit_cast<float>(x), std::bit_cast<float>(y), flags, screen.w,
                                screen.h, screen.cx, screen.cy, Csv(point));
            const Kiu::FirstPerson::AimNow aim =
                Kiu::FirstPerson::AimThroughCamera(mem, p.aim, screen, now.sample,
                                                 false); // AzaharFrame kiufix2: tracing only reads
            if (aim.valid) {
                line += fmt::format(" aim_pixel={:.3f},{:.3f} aim_ahead={} aim_depth={:.3f} "
                                    "aim_hit={} aim_aspect={:.5f} aim_aspect_n={}",
                                    aim.pixel.x, aim.pixel.y, aim.ahead ? 1 : 0, aim.depth,
                                    Csv(aim.point), aim.aspect, aim.aspect_readings);
            }
        }
    }
    if (mailbox.wide_node != 0) { // AzaharFrame kiuwide
        line += fmt::format(" wide_fov={:.5f} wide_aspect={:.5f} game_aspect={:.5f}",
                            mailbox.wide_fov_y, mailbox.wide_aspect, mailbox.wide_game_aspect);
    }
    if (mailbox.shot_rotate || mailbox.shot_move) {
        line += fmt::format(" shot_flags={}{}{}{} shot_q={} shot_offset={} shot_frame={}",
                            mailbox.shot_rotate ? "r" : "", mailbox.shot_absolute ? "a" : "",
                            mailbox.shot_move ? "m" : "", mailbox.shot_no_target ? "n" : "",
                            Csv(mailbox.shot_rotation), Csv(mailbox.shot_offset),
                            Csv(mailbox.shot_frame));
    }
    // The probe: an actor by its address; where it is in the world and where the head's middle
    // eye sees it (the patched view's axes: -z ahead), as mode 3 draws it.
    if (const auto text = Switch(kSwitchFpProbe)) {
        char* end = nullptr;
        const uint32_t addr = static_cast<uint32_t>(std::strtoul(text->c_str(), &end, 16));
        uint32_t vtable = 0;
        KH::Vec3 world;
        if (end != text->c_str() && addr != 0 && (addr & 3u) == 0 && mem.Read32(addr, vtable) &&
            vtable == KH::kActorVtable && KH::ReadVec3(mem, addr + KH::kActorWorldPos, world)) {
            line += fmt::format(" probe={:08X} probe_world={}", addr, Csv(world));
            if (now.frame_ok) {
                const KR::Pose in_rest =
                    KR::WorldToRest(now.scene.camera, now.frame.units_per_metre, {world, {}});
                const KR::Pose head = KR::RoomToRest(view.rest, view.head);
                const KH::Vec3 seen =
                    KH::Rotate(KH::Conj(view.given), in_rest.p - head.p);
                line += fmt::format(" probe_view={}", Csv(seen * now.frame.units_per_metre));
            }
        } else {
            line += " probe=0";
        }
    }
    LOG_INFO(Frontend, "xr: Kid Icarus first person: {}", line);
}

void Hook(int64_t ticks) {
    State& s = TheState();
    const int64_t host_now = NowNs();
    PollSwitchesInFlat(s, host_now);
    Core::System& system = Core::System::GetInstance();
    if (!system.IsPoweredOn()) {
        s.process = nullptr;
        VideoCore::Immersive::g_soft_draw_turn.store(false, std::memory_order_relaxed); // kiu2d
        VideoCore::Immersive::g_reticle_texture.store(0, std::memory_order_relaxed); // kiu131
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
        Kiu::FirstPerson::SetGameCode(0, false); // AzaharFrame kiumode
        Kiu::FirstPerson::SupplyRegisters(false);
        VideoCore::Immersive::g_soft_draw_turn.store(false, std::memory_order_relaxed); // kiu2d
        VideoCore::Immersive::g_reticle_texture.store(0, std::memory_order_relaxed); // kiu131
        return;
    }
    const uint64_t title = process->codeset->program_id;
    if (process.get() != s.process || title != s.title || s.run != Kiu::FirstPerson::RunCount()) {
        const int64_t poll_ns = s.poll_ns;
        s = State{};
        s.poll_ns = poll_ns;
        s.process = process.get();
        s.title = title;
        s.run = Kiu::FirstPerson::RunCount(); // AzaharFrame kiufix2: a new run, same address
    }
    Memory::MemorySystem& memory = system.Memory();
    GuestMemory mem(system, memory, *process);
    // AzaharFrame kiumode: the game's code first: Angel's Aim (Immersive Mode 4) is in effect only
    // where it is the mapped one (kiu_first_person.h).
    // AzaharFrame kiuusa: the European game or USA Rev 1 (KiuTurn::kBuilds), by title and code.
    const bool mapped = Input::KiuTurn::IsMappedTitle(title);
    if (mapped && !s.checked) {
        s.checked = true;
        const Input::KiuTurn::Build* build = Input::KiuTurn::MatchBuild(title, mem);
        s.code_ok = build != nullptr;
        s.build = build ? build : &Input::KiuTurn::kBuildEur;
    }
    const KH::Code& code = KH::CodeFor(s.build);
    Kiu::FirstPerson::SetGameCode(title, mapped && s.code_ok);
    Kiu::FirstPerson::NoteCStickMod(); // AzaharFrame kiu132: its toasts (a mod left out, or loaded)
    // AzaharFrame kiuvr: first person turns on the eye, the culls' and the follow's switches
    // with it (kiu_first_person.h). AzaharFrame kiumode: Angel's Aim, or the developer switch.
    const bool first_person = Kiu::FirstPerson::On();
    Kiu::FirstPerson::SupplyRegisters(first_person && mapped && s.code_ok);
    const bool fp_wanted = Kiu::FirstPerson::Wanted();
    const bool angels_aim = Kiu::FirstPerson::AngelsAim();
    const auto fp_trace_text = Switch(kSwitchFpTrace);
    const int fp_trace = fp_trace_text ? std::atoi(fp_trace_text->c_str()) : 0;
    const bool shots_6dof = Kiu::FirstPerson::Shots6DofOn();
    const bool want_aim_hold = Kiu::FirstPerson::AimNoModWanted();
    // AzaharFrame kiufov: with Right Stick turning (first person turns it on too) the stick
    // turns the land camera and the reticle never does: the land aim box is the whole screen,
    // with the mod or without it.
    const bool want_aim_box = want_aim_hold || Kiu::Turn::RightStickOn();
    // AzaharFrame kiuperf: a group first person puts in by itself is kept out with its switch at
    // "0" (an A/B for the cost of each; KH::GroupWanted).
    const auto nocull_text = Switch(kSwitchNoCull);
    const bool want_nocull =
        KH::GroupWanted(first_person, nocull_text ? nocull_text->c_str() : nullptr);
    const bool want_keep = shots_6dof || SwitchIs(kSwitchBulletKeep, "1");
    const bool want_eye = first_person || SwitchIs(kSwitchEye, "1");
    // AzaharFrame kiupit: first person also hides the player on land (below, once he is known);
    // first person alone keeps `any` true.
    const auto hide_text = Switch(kSwitchHide);
    const bool want_hide = SwitchIs(kSwitchHide, "1");
    const auto shot_text = Switch(kSwitchShot);
    float shot_values[5] = {};
    const int shot_count = shot_text ? KH::ParseFloats(shot_text->c_str(), shot_values, 5) : 0;
    const bool shot_test = shot_count == 2 || shot_count == 5;
    const bool want_shot = shot_test || shots_6dof;
    // The timed keep replaces the plain one (they share a site). AzaharFrame kiumode: Angel's
    // Aim's own (12 s) unless the switch gives one. AzaharFrame kiuperf: ENEMY_KEEP_S=0 or
    // ENEMY_KEEP=0 keeps it out (KH::ChooseEnemyKeep).
    const auto keep_plain_text = Switch(kSwitchEnemyKeep);
    const auto keep_timed_text = Switch(kSwitchEnemyKeepS);
    const KH::EnemyKeepChoice keep_choice = KH::ChooseEnemyKeep(
        angels_aim ? Kiu::FirstPerson::kAngelsAimEnemyKeepS : 0.0f,
        keep_plain_text ? keep_plain_text->c_str() : nullptr,
        keep_timed_text ? keep_timed_text->c_str() : nullptr);
    const float keep_seconds = keep_choice.seconds;
    const bool want_keep_timed = keep_seconds > 0.0f;
    const bool want_enemy_keep = keep_choice.plain;
    const bool want_group_keep = SwitchIs(kSwitchGroupKeep, "1");
    const bool want_no_follow = first_person || SwitchIs(kSwitchNoFollow, "1");
    // AzaharFrame kiushadow: in first person the player's own round shadow lies right under the
    // eye: only his is left out (everyone else's stays).
    // AzaharFrame kiuperf: AZAHARFRAME_KIU_NO_PLAYER_SHADOW=0 keeps his shadow (the group out).
    const auto shadow_text = Switch(kSwitchNoPlayerShadow);
    const bool want_player_shadow =
        KH::GroupWanted(first_person, shadow_text ? shadow_text->c_str() : nullptr);
    const auto trace_text = Switch(kSwitchTrace);
    const int trace = trace_text ? std::atoi(trace_text->c_str()) : 0;
    const bool any = want_nocull || want_keep || want_eye || want_hide || want_shot ||
                     want_keep_timed || want_enemy_keep || want_group_keep || want_no_follow ||
                     trace > 0 || fp_trace > 0 || want_aim_hold || want_aim_box || fp_wanted ||
                     want_player_shadow;
    if (fp_trace <= 0) {
        VideoCore::Immersive::g_world_view_tap.wanted.store(false, std::memory_order_relaxed);
    }
    // AzaharFrame kiu2d: in first person the game's sprites drawn through its geometry shader
    // (lock-on marks, hit numbers) turn with the world like everything else mode 3 draws
    // (vr_immersive.h g_soft_draw_turn); AZAHARFRAME_KIU_FP_2D=0 leaves them where the game's
    // camera put them, for an A/B.
    const auto fp2d_text = Switch(kSwitchFp2d);
    VideoCore::Immersive::g_soft_draw_turn.store(
        KH::GroupWanted(first_person, fp2d_text ? fp2d_text->c_str() : nullptr),
        std::memory_order_relaxed);
    // AzaharFrame kiu131: Reticle Size: the game's reticle's draws (its texture) scaled about
    // their middle, in first person (video_core/kiu_view_tap.h).
    {
        const auto size_text = Switch(kSwitchReticleSize);
        const auto tex_text = Switch(kSwitchReticleTexture);
        const float scale = KH::ReticleScaleFor(
            size_text ? size_text->c_str() : nullptr, Settings::values.vr_kiu_reticle_size.GetValue());
        const uint32_t tex = KH::ReticleTextureFor(tex_text ? tex_text->c_str() : nullptr,
                                                   s.build->reticle_texture);
        VideoCore::Immersive::g_reticle_scale.store(scale, std::memory_order_relaxed);
        VideoCore::Immersive::g_reticle_texture.store(first_person ? tex : 0u,
                                                      std::memory_order_relaxed);
        if (first_person && (scale != s.reticle_scale_logged || tex != s.reticle_tex_logged)) {
            s.reticle_scale_logged = scale;
            s.reticle_tex_logged = tex;
            LOG_INFO(Frontend, "xr: Kid Icarus hooks: Reticle Size {:.0f}% (the reticle's texture {:08X})",
                     scale * 100.0f, tex);
        }
    }
    // AzaharFrame kiumode: why Angel's Aim plays as Full (6DoF): Other Games here.
    const char* const as_other =
        fp_wanted && !first_person ? "; Angel's Aim plays as Full (6DoF): Other Games" : "";

    if (!mapped) {
        if (any) {
            Note(s, fmt::format("off: only {} are mapped, this is {:016X}{}",
                                Input::KiuTurn::kMappedList, title, as_other));
        }
        return;
    }
    if (!s.code_ok) {
        if (any) {
            const Input::KiuTurn::Build& b = Input::KiuTurn::BuildOfTitle(title);
            Note(s, fmt::format("off: the game's code differs from {} (0x{:06X}, 0x{:06X}, "
                                "0x{:06X}){}",
                                Input::KiuTurn::BuildsCode(b), b.code_check[0].addr,
                                b.code_check[4].addr, b.code_check[8].addr, as_other));
        }
        return;
    }
    if (!any) {
        // Nothing asked for. A save state made with a switch on brings our words (and a hidden
        // player) back: take them out; otherwise only look.
        const bool look = s.idle_vblanks++ % kIdleLookVBlanks == 0;
        if (!KH::AnyOurs(mem, code) && !(s.hidden || s.model_hidden != 0 || look)) {
            return;
        }
    } else {
        s.idle_vblanks = 0;
        Note(s, fmt::format("the game's code matches ({}){}", s.build->name,
                            angels_aim ? "; Angel's Aim: first person" : ""));
    }

    const KH::Player p = KH::FindPlayer(mem, *s.build);
    KH::EyeRequest eye;
    eye.on = want_eye;
    if (first_person && !p.land) {
        eye.offset = kEyeOffsetAir; // AzaharFrame kiuvr
    }
    if (const auto text = Switch(kSwitchEyeOffset)) {
        float v[3] = {};
        if (KH::ParseFloats(text->c_str(), v, 3) == 3) {
            eye.offset = {v[0], v[1], v[2]};
            // AzaharFrame kiufov: in first person an eye moved off the player's middle (behind
            // him) is laid out along the game camera's level view, not his body, which turns
            // to every shot: the eye would swing around his head.
            KH::Vec3 eye_at, look_at;
            if (first_person && (v[0] != 0.0f || v[2] != 0.0f) &&
                KH::ReadVec3(mem, KH::kSceneEye, eye_at) &&
                KH::ReadVec3(mem, KH::kSceneLookAt, look_at)) {
                eye.own_axes = true;
                eye.axes = KH::LevelYawAxes(look_at - eye_at);
            }
        }
    }
    // AzaharFrame kiuview: the over-the-shoulder view (AZAHARFRAME_KIU_SHOULDER, metres back, up
    // and right of the first-person eye along the camera's level view); EYE_OFFSET wins.
    // AzaharFrame kiutoggles: or Over the Shoulder's setting (vr_kiu_over_shoulder, a fixed
    // place) when the switch is not set; the switch, when set, wins.
    if (first_person && !Switch(kSwitchEyeOffset)) {
        float back = 0.0f, up = 0.0f, side = 0.0f;
        const auto text = Switch(kSwitchShoulder);
        KH::Vec3 eye_at, look_at;
        if (Kiu::FirstPerson::ShoulderFor(text ? text->c_str() : nullptr,
                                          Settings::values.vr_kiu_over_shoulder.GetValue(), back,
                                          up, side) &&
            KH::ReadVec3(mem, KH::kSceneEye, eye_at) &&
            KH::ReadVec3(mem, KH::kSceneLookAt, look_at)) {
            eye.extra = KH::Rotate(KH::LevelYawAxes(look_at - eye_at),
                                   Kiu::FirstPerson::ShoulderOffset(
                                       back, up, side, Kiu::FirstPerson::UnitsPerMetre()));
        }
    }
    KH::ShotRequest shot;
    if (shot_test && p.Ok()) {
        // Laid out on the game camera's view direction (the player's body turns as he fires).
        KH::Vec3 eye_at, look_at, view = KH::Rotate(p.world_q, {0.0f, 0.0f, 1.0f});
        if (KH::ReadVec3(mem, KH::kSceneEye, eye_at) && KH::ReadVec3(mem, KH::kSceneLookAt, look_at)) {
            view = {look_at.x - eye_at.x, look_at.y - eye_at.y, look_at.z - eye_at.z};
        }
        shot = KH::ShotTest(view, shot_values[0] * kDegToRad, shot_values[1] * kDegToRad,
                            {shot_values[2], shot_values[3], shot_values[4]}, shot_count == 5,
                            SwitchIs(kSwitchShotNoLock, "1"));
    }
    // AzaharFrame kiuvr: first person's transform for this VBlank; with AZAHARFRAME_KIU_SHOTS_6DOF
    // (and no test shot) the shots come from it.
    FirstPersonNow fp_now;
    if (first_person || fp_trace > 0) {
        fp_now = ReadFirstPerson(mem, host_now);
        if (shots_6dof && !shot_test && p.Ok()) {
            shot = ShotFromHand(fp_now, KH::Fill(p, eye, {}, 0));
        }
    }
    KH::Mailbox mailbox = KH::Fill(p, eye, shot, ++s.serial, keep_seconds);
    // AzaharFrame kiuwide: in first person, in a battle (the player's actor known), the scene
    // camera gets the angle and shape that make the game's own frame what fills the view
    // (Kiu::FirstPerson::WideTarget, AZAHARFRAME_KIU_FP_WIDE); otherwise a shape the app gave it
    // goes back to the game's own (the angle comes back with the game's next frame by itself).
    {
        KH::WideRequest wide;
        if (first_person && p.actor != 0) {
            wide.on = Kiu::FirstPerson::WideTarget(wide.fov_y, wide.aspect);
        }
        uint32_t node_aspect = 0;
        const bool have_aspect = mem.Read32(KH::kSceneAspect, node_aspect);
        const KH::WideRecord rec = KH::ReadWideRecord(mem);
        if (have_aspect && !(s.game_aspect > 0.0f)) {
            const float game = KH::WideGameAspect(node_aspect, rec, s.wide_asked_bits);
            if (KH::GameAspectOk(game)) {
                s.game_aspect = game;
            }
        }
        if (!wide.on && have_aspect) {
            if (const auto back = KH::WideRestore(node_aspect, rec, s.wide_asked_bits)) {
                uint32_t flags = 0;
                if (mem.WriteData(KH::kSceneAspect, *back) &&
                    mem.Read32(KH::kSceneNode + KH::kNodeFlags, flags)) {
                    mem.WriteData(KH::kSceneNode + KH::kNodeFlags, flags & ~KH::kNodeDirty);
                }
                LOG_INFO(Frontend,
                         "xr: Kid Icarus hooks: the scene camera's shape is the game's again "
                         "({:.4f})",
                         std::bit_cast<float>(*back));
            }
        }
        KH::FillWide(mailbox, wide, s.game_aspect);
        if (mailbox.wide_node != 0) {
            s.wide_asked_bits = std::bit_cast<uint32_t>(mailbox.wide_aspect);
        }
        Kiu::FirstPerson::PublishGameAspect(s.game_aspect);
    }
    const uint32_t wanted = (want_nocull ? KH::Bit(KH::Group::NoCull) : 0u) |
                            (want_keep ? KH::Bit(KH::Group::BulletKeep) : 0u) |
                            (want_eye ? KH::Bit(KH::Group::Eye) : 0u) |
                            (want_shot ? KH::Bit(KH::Group::Shot) : 0u) |
                            (want_enemy_keep ? KH::Bit(KH::Group::EnemyKeep) : 0u) |
                            (want_keep_timed ? KH::Bit(KH::Group::EnemyKeepTimed) : 0u) |
                            (want_group_keep ? KH::Bit(KH::Group::GroupKeep) : 0u) |
                            (want_no_follow ? KH::Bit(KH::Group::NoFollow) : 0u) |
                            (want_aim_hold ? KH::Bit(KH::Group::AimHold) : 0u) |
                            (want_aim_box ? KH::Bit(KH::Group::AimBox) : 0u) |
                            (want_player_shadow ? KH::Bit(KH::Group::PlayerShadow) : 0u) |
                            (mailbox.wide_node != 0 ? KH::Bit(KH::Group::WideCamera) : 0u);
    const KH::SyncResult r = KH::Sync(mem, wanted, mailbox, code);
    LogChanges(s, r);
    if ((r.in_mask & KH::Bit(KH::Group::WideCamera)) != 0) {
        AutoShape::HoldWhileWide(); // AzaharFrame kiufix2: Screen Shape Automatic does not measure
    }

    // AzaharFrame kiuvr: first person's trace.
    if (first_person || fp_trace > 0) {
        TraceFirstPerson(s, mem, p, mailbox, fp_now, ticks, host_now, fp_trace);
    }

    // The player's model: a data write into his actor, only while every guard holds; put back
    // whenever it is not wanted and the actor is known.
    // AzaharFrame kiupit: in first person on land the game's see-through Pit about the eye (his
    // head; his body looking down) goes the same way (KH::HideInFirstPerson);
    // AZAHARFRAME_KIU_HIDE_PLAYER=0 keeps him, for an A/B; =1 hides him land and air, as before.
    // AzaharFrame kiu131: by default first person hides him by his model's draw instead, land
    // and air (KH::ModelDrawWrite): the scale write left his weapon drawn, full size, while he
    // fired. AZAHARFRAME_KIU_HIDE_BY=scale is the kiupit way, for an A/B.
    const auto hide_by_text = Switch(kSwitchHideBy);
    const KH::HideBy hide_by = KH::HideByFrom(hide_by_text ? hide_by_text->c_str() : nullptr);
    if (p.actor != 0) {
        const bool eye_moved =
            eye.own_axes || eye.extra.x != 0.0f || eye.extra.y != 0.0f || eye.extra.z != 0.0f;
        const bool by_mode = hide_by == KH::HideBy::Scale &&
                             KH::HideInFirstPerson(first_person, p.land, shots_6dof, eye_moved);
        const bool hide =
            KH::GroupWanted(by_mode, hide_text ? hide_text->c_str() : nullptr) && p.Ok();
        if (const auto bits = KH::HideWrite(p.local_scale_bits, hide)) {
            if (mem.WriteData(p.actor + KH::kActorLocalScale, *bits)) {
                s.hidden = hide;
                LOG_INFO(Frontend, "xr: Kid Icarus hooks: the player's model {}{}",
                         hide ? "hidden (local scale 0.001)" : "shown again (local scale 1)",
                         hide && !want_hide ? " (first person, on land)" : "");
            }
        } else if (!hide) {
            s.hidden = false;
        }
        // AzaharFrame kiu131: his model's draw. AZAHARFRAME_KIU_HIDE_PLAYER set ("1": the scale
        // above; "0": keep him) leaves it drawn.
        uint32_t model = 0, vt = 0, word = 0;
        if (mem.Read32(p.actor + KH::kActorModel, model) && model != 0 && (model & 3u) == 0 &&
            mem.Read32(model, vt) && vt == s.build->model_vtable &&
            mem.Read32(model + KH::kModelDrawnWord, word)) {
            const bool hide_draw = hide_by == KH::HideBy::Draw && !hide_text &&
                                   KH::HideByDrawInFirstPerson(first_person, eye_moved) && p.Ok();
            // Ours to put back: the node this app cleared, or (once a run, playing) a cleared
            // byte a save state brought.
            const bool ours = s.model_hidden == model || (!s.model_looked && p.Ok());
            if (p.Ok()) {
                s.model_looked = true;
            }
            if (const auto w = KH::ModelDrawWrite(word, hide_draw, ours)) {
                if (mem.WriteData(model + KH::kModelDrawnWord, *w)) {
                    // Logged when it changes, not at each re-clear (on land the game draws him
                    // again every game frame).
                    const uint32_t now_hidden = hide_draw ? model : 0;
                    if (now_hidden != s.model_hidden) {
                        LOG_INFO(Frontend, "xr: Kid Icarus hooks: the player's model {} ({}, {})",
                                 hide_draw ? "hidden by its draw" : "drawn again",
                                 first_person ? "first person" : "not first person",
                                 p.land ? "land" : "air");
                    }
                    s.model_hidden = now_hidden;
                }
            } else if (!hide_draw && s.model_hidden == model &&
                       ((word >> KH::kModelDrawnShift) & 0xFFu) != 0u) {
                s.model_hidden = 0; // drawn again by the game itself
            }
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
    // AzaharFrame kiupit: the round shadows in the game's list (the player's is not among them
    // while "player shadow" is in; "scale 0.0010" above: his model hidden).
    const int shadows = KH::RoundShadowCount(mem);
    box += fmt::format("; round shadows {}",
                       shadows < 0 ? std::string{"-"} : std::to_string(shadows));
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

void OnGameSettings(uint64_t /*title_id*/) {
    // AzaharFrame kiumode: every game, before its presets: the last run's code check and
    // registers are forgotten (no setting is written).
    Kiu::FirstPerson::OnGameBoot();
}

} // namespace FrameXr::Kiu::Hooks
