// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "frame_xr/input/kiu_turn.h"

// AzaharFrame kiuturn: Right Stick turning's pure part (frame_xr/input/kiu_turn.h): the guard
// chain over a fake memory, the turn owed and its writes, smooth and snap turning.
using namespace FrameXr::Input::KiuTurn;
using Catch::Approx;

namespace {

/// The game's memory: only the bytes set are mapped.
class FakeMemory final : public Reader {
public:
    void Set32(uint32_t addr, uint32_t v) {
        for (uint32_t i = 0; i < 4; ++i) {
            bytes_[addr + i] = static_cast<uint8_t>(v >> (8 * i));
        }
    }
    void SetF(uint32_t addr, float f) {
        Set32(addr, std::bit_cast<uint32_t>(f));
    }
    void Set8(uint32_t addr, uint8_t v) {
        bytes_[addr] = v;
    }
    void Unmap(uint32_t addr) {
        bytes_.erase(addr);
    }
    bool Read32(uint32_t addr, uint32_t& v) const override {
        if ((addr & 3u) != 0) {
            return false;
        }
        uint32_t out = 0;
        for (uint32_t i = 0; i < 4; ++i) {
            const auto it = bytes_.find(addr + i);
            if (it == bytes_.end()) {
                return false;
            }
            out |= static_cast<uint32_t>(it->second) << (8 * i);
        }
        v = out;
        return true;
    }
    bool Read8(uint32_t addr, uint8_t& v) const override {
        const auto it = bytes_.find(addr);
        if (it == bytes_.end()) {
            return false;
        }
        v = it->second;
        return true;
    }

private:
    std::map<uint32_t, uint8_t> bytes_;
};

constexpr uint32_t kAim = 0x141DEC08;
constexpr uint32_t kOwner = 0x14100000;
constexpr uint32_t kCam = 0x141DDAF8;

/// A land battle as 3DSrecomp's Practice Range showed it (the original code's root).
FakeMemory LandBattle() {
    FakeMemory m;
    for (const CodeWord& w : kCodeCheck) {
        m.Set32(w.addr, w.word);
    }
    m.Set8(kPlaying, 1);
    constexpr uint32_t root = 0x14200000;
    m.Set32(kAimRoot, root);
    m.Set32(root + kAimRootNext, kAim + kAimBack);
    m.Set32(kAim, kAimVtableLand);
    m.Set8(kAim + kAimGround, 1);
    m.Set8(kAim + kAimGamePlaces, 0);
    m.Set32(kAim + kAimOwner, kOwner);
    m.Set32(kOwner, 0x00CF0000);
    m.Set32(kOwner + kOwnerCam, kCam);
    m.Set32(kCam, kCamVtable);
    m.Set32(kCamGlobal, kCam);
    m.Set8(kCam + kCamMode, 2);
    m.Set8(kCam + kCamModScale, 1);
    m.SetF(kCam + kCamYaw, 1.0f);
    m.SetF(kCam + kCamPitch, 0.0524f);
    m.Set32(kCam + kCamYawIn, 0);
    m.Set32(kCam + kCamPitchIn, 0);
    return m;
}

/// A look at the camera with the given yaw, D38 and the mod's flag.
Camera Look(float yaw, float yaw_in, uint8_t mod_flag = 1) {
    Camera c;
    c.guard = Guard::Ok;
    c.land = true;
    c.cam = kCam;
    c.mode = 2;
    c.mod_scale_flag = mod_flag;
    c.yaw = yaw;
    c.yaw_in_bits = std::bit_cast<uint32_t>(yaw_in);
    return c;
}

Camera Failed(Guard g) {
    Camera c;
    c.guard = g;
    return c;
}

constexpr float kCap = kSmoothCapRad;

} // namespace

TEST_CASE("Kiu turn: the code check", "[frame_xr][kiuturn]") {
    FakeMemory m = LandBattle();
    REQUIRE(CodeMatches(m));
    SECTION("one word differs (another region, another build)") {
        m.Set32(0x704178, 0xEEB00A48); // the USA game's word there
        REQUIRE_FALSE(CodeMatches(m));
    }
    SECTION("not mapped") {
        m.Unmap(0x703D4C);
        REQUIRE_FALSE(CodeMatches(m));
    }
}

TEST_CASE("Kiu turn: the guards", "[frame_xr][kiuturn]") {
    FakeMemory m = LandBattle();
    SECTION("a land battle") {
        const Camera c = FindCamera(m);
        REQUIRE(c.Ok());
        REQUIRE(c.land);
        REQUIRE(c.aim == kAim);
        REQUIRE(c.cam == kCam);
        REQUIRE(c.yaw == Approx(1.0f));
        REQUIRE(c.mode == 2);
        REQUIRE(IsZero(c.yaw_in_bits));
    }
    SECTION("the Practice Range's mode 1 turns too") {
        m.Set8(kCam + kCamMode, 1);
        REQUIRE(FindCamera(m).Ok());
    }
    SECTION("not playing") {
        m.Set8(kPlaying, 0);
        REQUIRE(FindCamera(m).guard == Guard::NotPlaying);
        m.Unmap(kPlaying);
        REQUIRE(FindCamera(m).guard == Guard::NotPlaying);
    }
    SECTION("the root or its +8 not mapped (a menu's garbage)") {
        m.Set32(kAimRoot, 0x30000000);
        REQUIRE(FindCamera(m).guard == Guard::NoRoot);
        m.Set32(kAimRoot, 0);
        REQUIRE(FindCamera(m).guard == Guard::NoRoot);
    }
    SECTION("the aim not mapped") {
        m.Unmap(kAim);
        REQUIRE(FindCamera(m).guard == Guard::NoAim);
    }
    SECTION("an air battle: no turning and no land") {
        m.Set32(kAim, kAimVtableAir);
        const Camera c = FindCamera(m);
        REQUIRE(c.guard == Guard::Air);
        REQUIRE_FALSE(c.land);
    }
    SECTION("another object") {
        m.Set32(kAim, 0x00123456);
        REQUIRE(FindCamera(m).guard == Guard::NotLand);
        REQUIRE_FALSE(FindCamera(m).land);
    }
    SECTION("the game holds the aim: still a land battle, no turning") {
        m.Set8(kAim + kAimGamePlaces, 1);
        Camera c = FindCamera(m);
        REQUIRE(c.guard == Guard::AimBusy);
        REQUIRE(c.land);
        m.Set8(kAim + kAimGamePlaces, 0);
        m.Set8(kAim + kAimGround, 0);
        c = FindCamera(m);
        REQUIRE(c.guard == Guard::AimBusy);
        REQUIRE(c.land);
    }
    SECTION("no owner") {
        m.Set32(kAim + kAimOwner, 0);
        REQUIRE(FindCamera(m).guard == Guard::NoOwner);
        m.Set32(kAim + kAimOwner, 0x30000000);
        REQUIRE(FindCamera(m).guard == Guard::NoOwner);
    }
    SECTION("the camera: zero, not aligned, not mapped, not the battle camera") {
        m.Set32(kOwner + kOwnerCam, 0);
        REQUIRE(FindCamera(m).guard == Guard::NoCamera);
        m.Set32(kOwner + kOwnerCam, kCam + 2);
        REQUIRE(FindCamera(m).guard == Guard::NoCamera);
        m.Set32(kOwner + kOwnerCam, 0x30000000);
        REQUIRE(FindCamera(m).guard == Guard::NoCamera);
        m.Set32(kOwner + kOwnerCam, kCam);
        m.Set32(kCam, 0x00CEEA00);
        REQUIRE(FindCamera(m).guard == Guard::NoCamera);
    }
    SECTION("not the current camera") {
        m.Set32(kCamGlobal, kCam + 0x100);
        REQUIRE(FindCamera(m).guard == Guard::NotCurrent);
    }
    SECTION("an easing or rail mode") {
        for (const uint8_t mode : {0, 3, 4, 8, 0xFF}) {
            m.Set8(kCam + kCamMode, mode);
            REQUIRE(FindCamera(m).guard == Guard::Mode);
        }
    }
    SECTION("the camera's fields not readable or not finite") {
        m.Unmap(kCam + kCamYawIn);
        REQUIRE(FindCamera(m).guard == Guard::Unreadable);
        m.Set32(kCam + kCamYawIn, 0);
        m.Set32(kCam + kCamYaw, 0x7FC00000); // NaN
        REQUIRE(FindCamera(m).guard == Guard::Unreadable);
    }
}

TEST_CASE("Kiu turn: wrap at plus or minus pi", "[frame_xr][kiuturn]") {
    REQUIRE(WrapPi(0.5f) == Approx(0.5f));
    REQUIRE(WrapPi(kPi + 0.5236f) == Approx(-kPi + 0.5236f).margin(1e-5));
    REQUIRE(WrapPi(-kPi - 0.1f) == Approx(kPi - 0.1f).margin(1e-5));
    REQUIRE(WrapPi(-kPi) == Approx(kPi).margin(1e-6)); // (-pi, pi]
    REQUIRE(WrapPi(NAN) == 0.0f);
}

TEST_CASE("Kiu turn: the writes and their verdicts", "[frame_xr][kiuturn]") {
    Ledger l;
    SECTION("applied: D38 cleared, the yaw changed by the write") {
        Ledger::Step s = l.Update(Look(1.0f, 0.0f), -0.1f, kCap);
        REQUIRE(s.write);
        REQUIRE(s.event == Event::Wrote);
        REQUIRE(s.value == Approx(-0.1f));
        s = l.Update(Look(0.9f, 0.0f), 0.0f, kCap);
        REQUIRE(s.verdict == Event::Applied);
        REQUIRE(s.ratio == 1.0f);
        REQUIRE_FALSE(s.write);
        REQUIRE(l.owed() == Approx(0.0f).margin(1e-6));
    }
    SECTION("lost: D38 cleared, the yaw unchanged: owed kept and written again") {
        l.Update(Look(1.0f, 0.0f), -0.1f, kCap);
        const Ledger::Step s = l.Update(Look(1.0f, 0.0f), 0.0f, kCap);
        REQUIRE(s.verdict == Event::Lost);
        REQUIRE(l.owed() == Approx(-0.1f));
        REQUIRE(s.write);
        REQUIRE(s.value == Approx(-0.1f));
    }
    SECTION("not taken yet: the total written over it, measured from the first write") {
        Ledger::Step s = l.Update(Look(1.0f, 0.0f), -0.05f, kCap);
        s = l.Update(Look(1.0f, -0.05f), -0.05f, kCap);
        REQUIRE(s.verdict == Event::None);
        REQUIRE(s.event == Event::Rewrote);
        REQUIRE(s.value == Approx(-0.1f));
        s = l.Update(Look(0.9f, 0.0f), 0.0f, kCap);
        REQUIRE(s.verdict == Event::Applied);
        REQUIRE(l.owed() == Approx(0.0f).margin(1e-6));
    }
    SECTION("taken, not cleared yet (the VBlank inside the game's update): no write then") {
        l.Update(Look(1.0f, 0.0f), -0.1f, kCap);
        Ledger::Step s = l.Update(Look(0.9f, -0.1f), -0.02f, kCap);
        REQUIRE(s.verdict == Event::Applied);
        REQUIRE_FALSE(s.write);
        REQUIRE(l.owed() == Approx(-0.02f));
        s = l.Update(Look(0.9f, 0.0f), 0.0f, kCap);
        REQUIRE(s.event == Event::Wrote);
        REQUIRE(s.value == Approx(-0.02f));
    }
    SECTION("a yaw change of the game's own is not counted as ours") {
        l.Update(Look(1.0f, 0.0f), -0.1f, kCap);
        const Ledger::Step s = l.Update(Look(1.3f, 0.0f), 0.0f, kCap); // an eased look-at
        REQUIRE(s.verdict == Event::Foreign);
        REQUIRE(l.owed() == Approx(-0.1f));
        REQUIRE(s.write); // written again, measured from the new yaw
        const Ledger::Step t = l.Update(Look(1.2f, 0.0f), 0.0f, kCap);
        REQUIRE(t.verdict == Event::Applied);
    }
    SECTION("the game's own input in D38 is never overwritten") {
        Ledger::Step s = l.Update(Look(1.0f, 0.3f), -0.1f, kCap);
        REQUIRE_FALSE(s.write);
        REQUIRE(l.owed() == Approx(-0.1f));
        l.Update(Look(1.0f, 0.0f), 0.0f, kCap);
        s = l.Update(Look(1.0f, 0.25f), 0.0f, kCap); // the clamp's, over ours
        REQUIRE(s.verdict == Event::Foreign);
        REQUIRE_FALSE(s.write);
        REQUIRE(l.owed() == Approx(-0.1f));
    }
    SECTION("a guard fails: owed dropped, never kept for later") {
        l.Update(Look(1.0f, 0.0f), -0.3f, kCap);
        Ledger::Step s = l.Update(Failed(Guard::Air), -0.1f, kCap);
        REQUIRE(s.verdict == Event::Dropped);
        REQUIRE_FALSE(s.write);
        REQUIRE(l.owed() == 0.0f);
        REQUIRE_FALSE(l.pending());
        s = l.Update(Failed(Guard::Mode), -0.1f, kCap);
        REQUIRE(s.verdict == Event::None); // nothing left to drop
        s = l.Update(Look(0.7f, 0.0f), 0.0f, kCap);
        REQUIRE_FALSE(s.write);
        REQUIRE(s.verdict == Event::None);
    }
    SECTION("owed is capped") {
        Ledger::Step s = l.Update(Look(1.0f, 0.3f), -2.0f, kCap); // the game's input waits
        REQUIRE(l.owed() == Approx(-kCap));
        s = l.Update(Look(1.0f, 0.0f), -1.0f, kCap);
        REQUIRE(s.value == Approx(-kCap));
        const float snap30 = 30.0f * kDegToRad;
        Ledger k;
        k.Update(Look(1.0f, 0.3f), snap30, snap30);
        s = k.Update(Look(1.0f, 0.0f), snap30, snap30);
        REQUIRE(s.value == Approx(snap30)); // two snaps in flight are one
    }
    SECTION("across plus or minus pi") {
        Ledger::Step s = l.Update(Look(3.0f, 0.0f), 0.3f, kCap);
        REQUIRE(s.write);
        s = l.Update(Look(WrapPi(3.3f), 0.0f), 0.0f, kCap); // -2.983
        REQUIRE(s.verdict == Event::Applied);
        Ledger k;
        k.Update(Look(-3.0f, 0.0f), -0.3f, kCap);
        s = k.Update(Look(WrapPi(-3.3f), 0.0f), 0.0f, kCap);
        REQUIRE(s.verdict == Event::Applied);
    }
    SECTION("the mod's x1.25 counts only when cam + 0x31A == 0") {
        l.Update(Look(1.0f, 0.0f, 0), -0.2f, kCap);
        Ledger::Step s = l.Update(Look(0.75f, 0.0f, 0), 0.0f, kCap);
        REQUIRE(s.verdict == Event::Applied);
        REQUIRE(s.ratio == kModScale);
        REQUIRE(l.owed() == Approx(0.0f).margin(1e-6)); // the written value is what is paid
        Ledger k;
        k.Update(Look(1.0f, 0.0f, 1), -0.2f, kCap);
        s = k.Update(Look(0.75f, 0.0f, 1), 0.0f, kCap);
        REQUIRE(s.verdict == Event::Foreign);
    }
    SECTION("within the tolerance") {
        l.Update(Look(1.0f, 0.0f), -0.1f, kCap);
        REQUIRE(l.Update(Look(0.9015f, 0.0f), 0.0f, kCap).verdict == Event::Applied);
        Ledger k;
        k.Update(Look(1.0f, 0.0f), -0.1f, kCap);
        REQUIRE(k.Update(Look(0.905f, 0.0f), 0.0f, kCap).verdict == Event::Foreign);
    }
    SECTION("a small write the yaw did not follow is lost, not applied") {
        l.Update(Look(1.0f, 0.0f), -0.00015f, kCap);
        const Ledger::Step s = l.Update(Look(1.0f, 0.0f), 0.0f, kCap);
        REQUIRE(s.verdict == Event::Lost);
        REQUIRE(l.owed() == Approx(-0.00015f));
    }
    SECTION("a rewrite after the game loaded the value before: that value is paid") {
        l.Update(Look(1.0f, 0.0f), -0.05f, kCap);                       // W1
        Ledger::Step s = l.Update(Look(1.0f, -0.05f), -0.05f, kCap);    // W2 = -0.1 over it
        REQUIRE(s.event == Event::Rewrote);
        s = l.Update(Look(0.95f, 0.0f), 0.0f, kCap); // the game turned by W1, then cleared
        REQUIRE(s.verdict == Event::Applied);
        REQUIRE(l.owed() == Approx(-0.05f));
        REQUIRE(s.write);
        REQUIRE(s.value == Approx(-0.05f));
        s = l.Update(Look(0.95f, 0.0f), 0.0f, kCap); // and the earlier value is forgotten
        REQUIRE(s.verdict == Event::Lost);
    }
    SECTION("less than the game turns by is kept, not written") {
        Ledger::Step s = l.Update(Look(1.0f, 0.0f), -0.00005f, kCap);
        REQUIRE_FALSE(s.write);
        s = l.Update(Look(1.0f, 0.0f), -0.00006f, kCap);
        REQUIRE(s.write);
        REQUIRE(s.value == Approx(-0.00011f));
    }
    SECTION("owed back to nothing while a write waits: taken back") {
        l.Update(Look(1.0f, 0.0f), -0.05f, kCap);
        const Ledger::Step s = l.Update(Look(1.0f, -0.05f), 0.05f, kCap);
        REQUIRE(s.write);
        REQUIRE(s.value == 0.0f);
        REQUIRE_FALSE(l.pending());
    }
}

TEST_CASE("Kiu turn: smooth turning", "[frame_xr][kiuturn]") {
    // A right turn is a negative yaw input: full right at 1.0x, 90 degrees a second, 1/60 s.
    REQUIRE(SmoothYaw(1.0f, 1.0f, 90.0f, 1.0f / 60.0f) == Approx(-1.5f * kDegToRad));
    REQUIRE(SmoothYaw(-0.5f, 2.0f, 90.0f, 1.0f / 60.0f) == Approx(1.5f * kDegToRad));
    REQUIRE(SmoothYaw(0.005f, 1.0f, 90.0f, 1.0f / 60.0f) == 0.0f); // under kStickMin
    REQUIRE(SmoothYaw(2.0f, 1.0f, 90.0f, 1.0f) == Approx(-9.0f * kDegToRad)); // x and dt held
    REQUIRE(SmoothYaw(1.0f, 1.0f, 90.0f, -1.0f) == 0.0f);
    REQUIRE(SmoothYaw(NAN, 1.0f, 90.0f, 0.1f) == 0.0f);
    REQUIRE(SmoothDpsFrom(nullptr) == kSmoothDpsDefault);
    REQUIRE(SmoothDpsFrom("120") == 120.0f);
    REQUIRE(SmoothDpsFrom("0") == kSmoothDpsDefault);
    REQUIRE(SmoothDpsFrom("fast") == kSmoothDpsDefault);
    REQUIRE(SmoothDpsFrom("5000") == kSmoothDpsDefault);
    REQUIRE(SnapDegreesFrom(nullptr) == 0);
    REQUIRE(SnapDegreesFrom("0") == 0);
    REQUIRE(SnapDegreesFrom("45") == 45);
    REQUIRE(SnapDegreesFrom("90") == 90);
    REQUIRE(SnapDegreesFrom("50") == 0);
}

// PrimedFrame's snap turn tests (tests/port_vr_snap_turn.cpp), without the head zone.
TEST_CASE("Kiu turn: snap turning", "[frame_xr][kiuturn]") {
    using namespace FrameXr::Input::KiuTurn::Snap;
    Input in;
    in.enabled = true;
    in.connected = true;
    in.accepting = true;
    in.degrees = 45;
    double now = 1.0;
    const auto sample = [&](Tracker& tracker, float x) {
        in.stick_x = x;
        in.now = now;
        now += 1.0 / 60.0;
        return tracker.Update(in);
    };
    SECTION("the flick, holding, re-arming") {
        Tracker t;
        REQUIRE(sample(t, 0.0f) == 0.0f);
        REQUIRE(sample(t, 0.7f) == 0.0f);   // short of the flick
        REQUIRE(sample(t, 0.75f) == 45.0f); // past 0.72: 45 degrees right
        REQUIRE(sample(t, 1.0f) == 0.0f);   // held: no second snap
        REQUIRE(sample(t, 0.4f) == 0.0f);   // above 0.32 does not re-arm
        now += 1.0;
        REQUIRE(sample(t, 1.0f) == 0.0f);   // nor does waiting with it held
        REQUIRE(sample(t, 0.3f) == 0.0f);   // under 0.32 re-arms
        REQUIRE(sample(t, -0.9f) == -45.0f);
    }
    SECTION("eight game frames between two snaps") {
        Tracker t;
        REQUIRE(sample(t, 1.0f) == 45.0f);
        REQUIRE(sample(t, 0.0f) == 0.0f);
        REQUIRE(sample(t, 1.0f) == 0.0f); // inside the cooldown
        REQUIRE(sample(t, 0.0f) == 0.0f);
        now += kCooldownSeconds;
        REQUIRE(sample(t, 1.0f) == 45.0f);
    }
    SECTION("a flick while turning is not possible is used up and dropped") {
        Tracker t;
        in.accepting = false;
        REQUIRE(sample(t, 1.0f) == 0.0f);
        in.accepting = true;
        REQUIRE(sample(t, 1.0f) == 0.0f); // held through it: still nothing
        REQUIRE(sample(t, 0.0f) == 0.0f);
        REQUIRE(sample(t, 1.0f) == 45.0f);
    }
    SECTION("off, or no stick, resets") {
        Tracker t;
        REQUIRE(sample(t, 1.0f) == 45.0f);
        in.enabled = false;
        REQUIRE(sample(t, 1.0f) == 0.0f);
        in.enabled = true;
        now += 1.0;
        REQUIRE(sample(t, 1.0f) == 45.0f);
        in.connected = false;
        REQUIRE(sample(t, 1.0f) == 0.0f);
        in.connected = true;
        REQUIRE(sample(t, 1.0f) == 45.0f); // re-armed, the cooldown cleared
    }
    SECTION("each angle, clamped to 30 to 90") {
        for (const int deg : {30, 45, 60, 90}) {
            Tracker t;
            in.degrees = deg;
            REQUIRE(sample(t, 1.0f) == static_cast<float>(deg));
        }
        Tracker t;
        in.degrees = 15;
        REQUIRE(sample(t, -1.0f) == -30.0f);
        REQUIRE(ClampDegrees(120) == 90);
    }
}
