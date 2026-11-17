// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuturn: Right Stick turning in Kid Icarus: Uprising's land battles, the pure part
// (test switch AZAHARFRAME_KIU_RIGHT_STICK_TURN=1; frame_xr/kiu_turn.h runs it on the emulation
// thread at each VBlank). No emulator or OpenXR types here, so tests/frame_xr/kiu_turn.cpp checks
// it with a fake memory.
//
// The route (MAPPING.md, "Right Stick turning"): the game turns its land camera by the yaw input
// at cam+0xD38 (radians, added to the yaw at cam+0x2AC in the camera's own update, then cleared
// at the end of that update). The app writes the whole turn it still owes into it at each VBlank
// and counts a write as applied only when the next VBlank finds D38 cleared and the yaw changed by
// what was written. Everything is found through the game's own code (EUR, code.bin md5 b107c9b1;
// the C-Stick & Gyro mod v172's EUR builds have the same words at these addresses):
//   aim = [[0xE14504] + 8] - 0x10     the player's aim (reticle) object
//   cam = [[aim + 0x8C] + 0x2C]       the battle camera, also [0xD3A18C]
// with the guards of FindCamera, each pointer checked mapped before it is read.
//
// Turning, as PrimedFrame's (the owner's GPL-3.0 project, platform/include/vr/vr_snap_turn.h):
// smooth by default (stick x Horizontal Turn Speed x a base rate); with a snap angle, a flick past
// 0.72 turns by it at once, the stick must come back under 0.32 before the next one counts, two
// snaps are at least 8 game frames apart, and a flick made while turning is not possible (an air
// battle, a menu, a cutscene) is used up and dropped.

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <numbers>

namespace FrameXr::Input::KiuTurn {

// ---- The game (EUR) ----

inline constexpr uint64_t kTitleEur = 0x0004000000030200ull;

inline constexpr uint32_t kPlaying = 0xE17514;       ///< u8: 1 while a battle plays
inline constexpr uint32_t kAimRoot = 0xE14504;       ///< [root] + kAimRootNext = aim + kAimBack
inline constexpr uint32_t kAimRootNext = 0x8;
inline constexpr uint32_t kAimBack = 0x10;
inline constexpr uint32_t kAimVtableLand = 0xCF1C34; ///< the land battle's aim
inline constexpr uint32_t kAimVtableAir = 0xCF1BE0;  ///< the air battle's (no camera link)
inline constexpr uint32_t kAimGround = 0x81;         ///< u8: 1, the ground aim box
inline constexpr uint32_t kAimFlags = 0x84;          ///< u32: the clamp's flags (bit 0x08)
inline constexpr uint32_t kAimOwner = 0x8C;          ///< the owner; its +kOwnerCam is the camera
inline constexpr uint32_t kAimX = 0xA4;              ///< float: the reticle's pixel
inline constexpr uint32_t kAimY = 0xA8;
inline constexpr uint32_t kAimGamePlaces = 0xD5;     ///< u8: the game places the reticle itself
inline constexpr uint32_t kOwnerCam = 0x2C;

inline constexpr uint32_t kCamVtable = 0xCEEA74;     ///< the battle camera
inline constexpr uint32_t kCamGlobal = 0xD3A18C;     ///< the camera of the last camera update
inline constexpr uint32_t kCamYaw = 0x2AC;           ///< float radians, (-pi, pi], right lowers it
inline constexpr uint32_t kCamPitch = 0x2B0;         ///< float radians, positive looks down
inline constexpr uint32_t kCamMode = 0x350;          ///< s8: 1 and 2 apply the input directly
inline constexpr uint32_t kCamModScale = 0x31A;      ///< u8: 0, the mod scales its clamp x1.25
inline constexpr uint32_t kCamYawRate = 0x7D8;       ///< float: the last yaw rate applied (log)
inline constexpr uint32_t kCamYawIn = 0xD38;         ///< float: this update's yaw input
inline constexpr uint32_t kCamPitchIn = 0xD3C;       ///< float: this update's pitch input

/// Code words checked before anything is read or written (EUR original; the same in the mod
/// v172 builds): the turn input's load (0x704178), its clear at the end of the camera update
/// (0x703894) and the camera's store into kCamGlobal (0x703D4C). The other regions differ here.
struct CodeWord {
    uint32_t addr;
    uint32_t word;
};
inline constexpr std::array<CodeWord, 9> kCodeCheck{{
    {0x704178, 0xE2840B03}, // add r0, r4, #0xC00
    {0x70417C, 0xE2800F4E}, // add r0, r0, #0x138
    {0x704180, 0xEEB01A48},
    {0x704184, 0xEC900A02}, // vldmia r0, {s0, s1}
    {0x703894, 0xE2840B03},
    {0x703898, 0xE5C49308},
    {0x70389C, 0xED808A4F},
    {0x7038A0, 0xED808A4E},
    {0x703D4C, 0xE5804000}, // str r4, [r0]
}};

/// The game's memory, as the emulation thread sees it (a fake one in the tests). Each read says
/// false when the address is not mapped (Read32: or not 4-byte aligned); nothing is read then.
class Reader {
public:
    virtual ~Reader() = default;
    virtual bool Read32(uint32_t addr, uint32_t& value) const = 0;
    virtual bool Read8(uint32_t addr, uint8_t& value) const = 0;
};

inline bool CodeMatches(const Reader& m) {
    for (const CodeWord& w : kCodeCheck) {
        uint32_t v = 0;
        if (!m.Read32(w.addr, v) || v != w.word) {
            return false;
        }
    }
    return true;
}

/// Why there is no turning now (Ok: turning is possible). In the order checked.
enum class Guard : uint8_t {
    Ok,
    NotPlaying,  ///< u8 [kPlaying] != 1
    NoRoot,      ///< [kAimRoot] or its +8 not mapped, or zero
    NoAim,       ///< the aim's first word not readable
    Air,         ///< the air battle's aim
    NotLand,     ///< another object
    AimBusy,     ///< aim +0x81 != 1, or the game places the reticle (+0xD5)
    NoOwner,     ///< [aim + 0x8C] zero or not mapped
    NoCamera,    ///< [owner + 0x2C] zero, not aligned, or not the battle camera
    NotCurrent,  ///< [kCamGlobal] != cam
    Mode,        ///< s8 cam + 0x350 not 1 or 2
    Unreadable,  ///< the camera's fields not readable, or not finite
};

inline const char* GuardName(Guard g) {
    switch (g) {
    case Guard::Ok:
        return "ok";
    case Guard::NotPlaying:
        return "not playing";
    case Guard::NoRoot:
        return "no aim root";
    case Guard::NoAim:
        return "no aim";
    case Guard::Air:
        return "air battle";
    case Guard::NotLand:
        return "not the land aim";
    case Guard::AimBusy:
        return "the game holds the aim";
    case Guard::NoOwner:
        return "no aim owner";
    case Guard::NoCamera:
        return "no battle camera";
    case Guard::NotCurrent:
        return "not the current camera";
    case Guard::Mode:
        return "camera mode";
    case Guard::Unreadable:
        return "camera unreadable";
    }
    return "?";
}

/// What one look found.
struct Camera {
    Guard guard = Guard::NotPlaying;
    /// A land battle (the playing flag and the land aim): the C-Stick is centred for the game and
    /// Direct Aim keeps aiming, even while a later guard stops the turning.
    bool land = false;
    uint32_t aim = 0;
    uint32_t cam = 0;
    int8_t mode = 0;
    uint8_t mod_scale_flag = 0; ///< cam + 0x31A
    float yaw = 0.0f;
    float pitch = 0.0f;
    uint32_t yaw_in_bits = 0;   ///< cam + 0xD38 as read (before any write)
    uint32_t pitch_in_bits = 0; ///< cam + 0xD3C
    bool Ok() const {
        return guard == Guard::Ok;
    }
};

inline float AsFloat(uint32_t bits) {
    return std::bit_cast<float>(bits);
}

/// The guards, in order (MAPPING.md, "Right Stick turning"): the playing flag; the root and its +8
/// mapped; the aim's vtable the land one (the air one has no camera); aim +0x81 == 1 and +0xD5 ==
/// 0; the owner mapped; the camera non-zero, aligned, the battle camera's vtable; the current
/// camera ([0xD3A18C]); the camera mode 1 or 2.
inline Camera FindCamera(const Reader& m) {
    Camera c;
    uint8_t b = 0;
    if (!m.Read8(kPlaying, b) || b != 1) {
        c.guard = Guard::NotPlaying;
        return c;
    }
    uint32_t root = 0, next = 0;
    if (!m.Read32(kAimRoot, root) || root == 0 || !m.Read32(root + kAimRootNext, next) ||
        next < kAimBack) {
        c.guard = Guard::NoRoot;
        return c;
    }
    c.aim = next - kAimBack;
    uint32_t vtable = 0;
    if (!m.Read32(c.aim, vtable)) {
        c.guard = Guard::NoAim;
        return c;
    }
    if (vtable == kAimVtableAir) {
        c.guard = Guard::Air;
        return c;
    }
    if (vtable != kAimVtableLand) {
        c.guard = Guard::NotLand;
        return c;
    }
    c.land = true;
    uint8_t ground = 0, places = 0;
    if (!m.Read8(c.aim + kAimGround, ground) || !m.Read8(c.aim + kAimGamePlaces, places) ||
        ground != 1 || places != 0) {
        c.guard = Guard::AimBusy;
        return c;
    }
    uint32_t owner = 0;
    if (!m.Read32(c.aim + kAimOwner, owner) || owner == 0 || !m.Read32(owner, vtable)) {
        c.guard = Guard::NoOwner;
        return c;
    }
    uint32_t cam = 0, cam_vtable = 0;
    if (!m.Read32(owner + kOwnerCam, cam) || cam == 0 || (cam & 3u) != 0 ||
        !m.Read32(cam, cam_vtable) || cam_vtable != kCamVtable) {
        c.guard = Guard::NoCamera;
        return c;
    }
    c.cam = cam;
    uint32_t current = 0;
    if (!m.Read32(kCamGlobal, current) || current != cam) {
        c.guard = Guard::NotCurrent;
        return c;
    }
    uint8_t mode = 0;
    if (!m.Read8(cam + kCamMode, mode)) {
        c.guard = Guard::Unreadable;
        return c;
    }
    c.mode = static_cast<int8_t>(mode);
    uint32_t yaw = 0, pitch = 0;
    if (!m.Read8(cam + kCamModScale, c.mod_scale_flag) || !m.Read32(cam + kCamYaw, yaw) ||
        !m.Read32(cam + kCamPitch, pitch) || !m.Read32(cam + kCamYawIn, c.yaw_in_bits) ||
        !m.Read32(cam + kCamPitchIn, c.pitch_in_bits)) {
        c.guard = Guard::Unreadable;
        return c;
    }
    c.yaw = AsFloat(yaw);
    c.pitch = AsFloat(pitch);
    if (c.mode != 1 && c.mode != 2) {
        c.guard = Guard::Mode;
        return c;
    }
    if (!std::isfinite(c.yaw) || !std::isfinite(c.pitch) ||
        !std::isfinite(AsFloat(c.yaw_in_bits))) {
        c.guard = Guard::Unreadable;
        return c;
    }
    c.guard = Guard::Ok;
    return c;
}

// ---- The turn owed, and the writes ----

inline constexpr float kPi = std::numbers::pi_v<float>;

/// To (-pi, pi], as the game wraps its yaw (0x34C2E0).
inline float WrapPi(float a) {
    if (!std::isfinite(a)) {
        return 0.0f;
    }
    a = std::remainder(a, 2.0f * kPi);
    return a <= -kPi ? a + 2.0f * kPi : a;
}

inline constexpr float kDegToRad = kPi / 180.0f;
/// Smooth turning's cap on the turn owed (Snap: one snap angle).
inline constexpr float kSmoothCapRad = 45.0f * kDegToRad;
/// The game turns only by more than 2.9e-5 rad an update (0x74643C); less is kept owed.
inline constexpr float kMinWriteRad = 1.0e-4f;
/// A write counts as applied when the yaw changed by it within this much (plus kMatchRel of it),
/// and never more than half of it (so a small write the yaw did not follow is not applied).
inline constexpr float kMatchAbsRad = 2.0e-4f;
inline constexpr float kMatchRel = 0.02f;
inline constexpr float kMatchMaxShare = 0.5f;
/// The C-Stick & Gyro mod scales the clamp's yaw input by this when cam + 0x31A == 0 (0xF54000);
/// whether it touches a value written into D38 directly is what the trace's ratio shows.
inline constexpr float kModScale = 1.25f;

/// What one VBlank's step decided.
enum class Event : uint8_t {
    None,     ///< nothing owed, nothing pending
    Wrote,    ///< a fresh write
    Rewrote,  ///< the last write not taken yet: the new total written over it
    Applied,  ///< the last write turned the camera (D38 cleared, the yaw changed by it)
    Lost,     ///< D38 cleared, the yaw did not change: dropped (the mod's veto, a race)
    Foreign,  ///< the yaw changed by something else, or D38 held another value
    Dropped,  ///< a guard failed: the turn owed is dropped, never kept for later
};

inline const char* EventName(Event e) {
    switch (e) {
    case Event::None:
        return "none";
    case Event::Wrote:
        return "wrote";
    case Event::Rewrote:
        return "rewrote";
    case Event::Applied:
        return "applied";
    case Event::Lost:
        return "lost";
    case Event::Foreign:
        return "foreign";
    case Event::Dropped:
        return "dropped";
    }
    return "?";
}

/// The yaw change `delta` matches the write `written`: 1 (as written), kModScale (the mod's
/// scale, only when `mod_scale_possible`), or 0 (no match).
inline float MatchRatio(float delta, float written, bool mod_scale_possible) {
    const auto matches = [delta](float expected) {
        const float tol = std::min(kMatchAbsRad + kMatchRel * std::fabs(expected),
                                   kMatchMaxShare * std::fabs(expected));
        return std::fabs(delta - expected) <= tol;
    };
    if (matches(written)) {
        return 1.0f;
    }
    if (mod_scale_possible && matches(kModScale * written)) {
        return kModScale;
    }
    return 0.0f;
}

inline bool IsZero(uint32_t bits) {
    return (bits & 0x7FFFFFFFu) == 0;
}

/// The turn owed and the one write in flight. Emulation thread, one step a VBlank.
class Ledger {
public:
    struct Step {
        Event event = Event::None;  ///< the verdict on the last write, or what was written
        Event verdict = Event::None; ///< Applied, Lost, Foreign, Dropped or None this step
        bool write = false;         ///< write `value` into cam + 0xD38
        float value = 0.0f;
        float ratio = 0.0f;         ///< an applied write: the yaw change / the value written
        float delta = 0.0f;         ///< the yaw change since the write (a verdict's)
    };

    /// `c`: this VBlank's look (FindCamera). `add`: the turn asked since the last step (radians,
    /// a right turn negative). `cap`: the most that may be owed (radians, > 0).
    Step Update(const Camera& c, float add, float cap) {
        Step s;
        if (!c.Ok()) {
            if (owed_ != 0.0f || pending_) {
                s.event = s.verdict = Event::Dropped;
            }
            owed_ = 0.0f;
            pending_ = false;
            has_previous_ = false;
            return s;
        }
        if (std::isfinite(add)) {
            owed_ = std::clamp(owed_ + add, -cap, cap);
        }
        const bool d38_zero = IsZero(c.yaw_in_bits);
        bool rewrite = false;
        if (pending_) {
            const float delta = WrapPi(c.yaw - yaw_at_write_);
            const bool ours = c.yaw_in_bits == written_bits_;
            float paid = written_;
            float ratio = MatchRatio(delta, written_, c.mod_scale_flag == 0);
            if (ratio == 0.0f && has_previous_) {
                // A rewrite landed after the game had loaded the value before it: that one turned.
                ratio = MatchRatio(delta, previous_, c.mod_scale_flag == 0);
                paid = previous_;
            }
            if ((d38_zero || ours) && ratio > 0.0f) {
                // Taken. When D38 still holds it, the update that took it has not cleared it yet
                // (the VBlank fell inside it): anything written now would be cleared too, so the
                // next write waits a VBlank.
                owed_ = std::clamp(owed_ - paid, -cap, cap);
                pending_ = false;
                has_previous_ = false;
                s.event = s.verdict = Event::Applied;
                s.ratio = ratio;
                s.delta = delta;
                if (ours) {
                    return s;
                }
            } else if (ours) {
                // Not taken yet: the new total goes over it. A yaw change meanwhile is the game's
                // (a component before the load), so the measure starts again from here.
                rewrite = true;
                yaw_at_write_ = c.yaw;
            } else {
                pending_ = false;
                has_previous_ = false;
                s.event = s.verdict = d38_zero && std::fabs(delta) <= kMatchAbsRad
                                          ? Event::Lost
                                          : Event::Foreign;
                s.delta = delta;
                if (!d38_zero) {
                    return s; // the game's own input is in flight: not overwritten
                }
            }
        } else if (!d38_zero) {
            return s; // the game's own input is in flight
        }
        if (std::fabs(owed_) < kMinWriteRad) {
            if (rewrite) {
                // Owed went back to about nothing while a write waited: take it back.
                pending_ = false;
                has_previous_ = false;
                s.write = true;
                s.value = 0.0f;
                s.event = Event::Rewrote;
            }
            return s;
        }
        if (!rewrite) {
            yaw_at_write_ = c.yaw;
        }
        has_previous_ = rewrite;
        previous_ = written_;
        pending_ = true;
        written_ = owed_;
        written_bits_ = std::bit_cast<uint32_t>(owed_);
        s.write = true;
        s.value = owed_;
        if (s.event == Event::None) {
            s.event = rewrite ? Event::Rewrote : Event::Wrote;
        }
        return s;
    }

    void Reset() {
        owed_ = 0.0f;
        pending_ = false;
        has_previous_ = false;
    }

    float owed() const {
        return owed_;
    }
    bool pending() const {
        return pending_;
    }

private:
    float owed_ = 0.0f;
    bool pending_ = false;
    float written_ = 0.0f;
    uint32_t written_bits_ = 0;
    float yaw_at_write_ = 0.0f;
    /// The value a rewrite replaced: the game may have loaded it just before (a VBlank inside
    /// its update, between the load and the turn), so its turn is accepted for this write too.
    bool has_previous_ = false;
    float previous_ = 0.0f;
};

// ---- Smooth and snap ----

/// The base smooth rate at Horizontal Turn Speed 1.0x and the stick at full tilt, degrees a
/// second (live test switch AZAHARFRAME_KIU_TURN_DPS).
inline constexpr float kSmoothDpsDefault = 90.0f;
inline constexpr float kSmoothDpsMax = 720.0f;
/// Below this (after the mapper's dead zone) the stick turns nothing.
inline constexpr float kStickMin = 0.01f;
/// The longest step one VBlank may turn by (a pause, a state load): seconds.
inline constexpr float kMaxStepS = 0.1f;

/// Smooth turning's yaw input for one step: radians, a right turn (stick x > 0) negative.
inline float SmoothYaw(float stick_x, float turn_speed, float dps, float dt_s) {
    if (!std::isfinite(stick_x) || !std::isfinite(dt_s) || std::fabs(stick_x) < kStickMin) {
        return 0.0f;
    }
    const float x = std::clamp(stick_x, -1.0f, 1.0f);
    const float dt = std::clamp(dt_s, 0.0f, kMaxStepS);
    return -x * turn_speed * dps * kDegToRad * dt;
}

/// AZAHARFRAME_KIU_TURN_DPS's value: 1 to kSmoothDpsMax; anything else, the default.
inline float SmoothDpsFrom(const char* text) {
    if (!text || !*text) {
        return kSmoothDpsDefault;
    }
    char* end = nullptr;
    const float f = std::strtof(text, &end);
    return end != text && std::isfinite(f) && f >= 1.0f && f <= kSmoothDpsMax ? f
                                                                             : kSmoothDpsDefault;
}

/// AZAHARFRAME_KIU_SNAP_TURN's value: 30, 45, 60 or 90 degrees; 0, unset or anything else: smooth.
inline int SnapDegreesFrom(const char* text) {
    if (!text) {
        return 0;
    }
    const int v = std::atoi(text);
    return v == 30 || v == 45 || v == 60 || v == 90 ? v : 0;
}

/// PrimedFrame's snap turn (platform/include/vr/vr_snap_turn.h, PrimedGun's UpdateSnapTurn),
/// without its head zone (Kid Icarus has no visor gesture).
namespace Snap {

inline constexpr float kEnterThreshold = 0.72f;
inline constexpr float kExitThreshold = 0.32f;
inline constexpr double kCooldownSeconds = 8.0 / 60.0; ///< eight game frames
inline constexpr std::array<int, 4> kDegreeChoices{30, 45, 60, 90};

inline int ClampDegrees(int degrees) {
    return std::clamp(degrees, kDegreeChoices.front(), kDegreeChoices.back());
}

struct Input {
    bool enabled = false;   ///< Snap Turn on (a snap angle set)
    bool connected = false; ///< the stick's sample is fresh
    float stick_x = 0.0f;   ///< +right
    bool accepting = false; ///< turning is possible now (FindCamera's guards hold)
    int degrees = 45;
    double now = 0.0;       ///< seconds of the game's time
};

class Tracker {
public:
    /// One sample: the turn to make, degrees (positive turns right), or 0. A flick is used up
    /// even when turning is not possible, so holding the stick through it does not snap after.
    float Update(const Input& in) {
        if (!in.enabled) {
            ready_ = true;
            return 0.0f;
        }
        if (!in.connected) {
            ready_ = true;
            cooldown_until_ = 0.0;
            return 0.0f;
        }
        const float x = std::clamp(in.stick_x, -1.0f, 1.0f);
        if (std::fabs(x) <= kExitThreshold) {
            ready_ = true;
            return 0.0f;
        }
        if (!ready_ || std::fabs(x) < kEnterThreshold || in.now < cooldown_until_) {
            return 0.0f;
        }
        ready_ = false;
        if (!in.accepting) {
            return 0.0f;
        }
        cooldown_until_ = in.now + kCooldownSeconds;
        return (x > 0.0f ? 1.0f : -1.0f) * static_cast<float>(ClampDegrees(in.degrees));
    }

    void Reset() {
        ready_ = true;
        cooldown_until_ = 0.0;
    }

private:
    bool ready_ = true;
    double cooldown_until_ = 0.0;
};

} // namespace Snap

} // namespace FrameXr::Input::KiuTurn
