// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiufire: Trigger Layout's Fire and Power (frame_xr/input/kiu_trigger_layout.h):
// the aiming hand's trigger is L only, the other uses the power (D-pad Down), the bumpers pick it
// (D-pad Left and Right), recentring is a bumper hold with Gyro Aim and never with Direct Aim.

#include <cstdint>
#include <catch2/catch_test_macros.hpp>
#include "common/settings.h"
#include "frame_xr/input/default_bindings.h"
#include "frame_xr/input/kiu_trigger_layout.h"
#include "frame_xr/kiu_scan.h"

using namespace FrameXr::Input;
using namespace FrameXr::Input::KiuTriggers;
namespace Btn = Settings::NativeButton;

namespace {

constexpr uint64_t kMs = 1'000'000;

Frame On(GyroAimMode mode, Hand aiming = Hand::Right, uint64_t t = 0) {
    Frame f;
    f.fire_and_power = true;
    f.mode = mode;
    f.aiming = aiming;
    f.time_ns = t;
    return f;
}

ControlState With(std::initializer_list<Control> held) {
    ControlState c;
    for (const Control ctl : held) {
        c.Set(ctl, true);
    }
    return c;
}

/// The 3DS buttons the default profile makes of the controls.
ThreeDsPadState Pad(const ControlState& c) {
    return ApplyDefaultBindings(c);
}

/// Runs the layout on `held` at `t` (ms) and returns the 3DS pad.
ThreeDsPadState Step(Layout& l, std::initializer_list<Control> held, const Frame& f) {
    ControlState c = With(held);
    l.Apply(c, f);
    return Pad(c);
}

bool OnlyThese(const ThreeDsPadState& p, std::initializer_list<Btn::Values> want) {
    for (int b = 0; b < Btn::NumButtons; b++) {
        bool wanted = false;
        for (const auto w : want) {
            wanted = wanted || w == b;
        }
        if (p.buttons[static_cast<std::size_t>(b)] != wanted) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("Trigger Layout: Original, Aiming Off or another game change nothing", "[kiufire]") {
    Layout l;
    for (const Control ctl : {Control::TriggerL, Control::TriggerR, Control::BumperL,
                              Control::BumperR, Control::DpadDown}) {
        const ControlState before = With({ctl});
        // Original (the default), any mode.
        for (const GyroAimMode mode : {GyroAimMode::Off, GyroAimMode::Gyro, GyroAimMode::LightGun}) {
            Frame f = On(mode);
            f.fire_and_power = false;
            ControlState c = before;
            CHECK_FALSE(l.Apply(c, f));
            CHECK(c.buttons == before.buttons);
        }
        // Fire and Power but Aiming Off.
        ControlState c = before;
        CHECK_FALSE(l.Apply(c, On(GyroAimMode::Off)));
        CHECK(c.buttons == before.buttons);
    }
    // controls.cpp sets fire_and_power only for Kid Icarus: Uprising's title ids.
    for (const uint64_t kiu : {0x0004000000030000ull, 0x0004000000030100ull,
                               0x0004000000030200ull, 0x0004000000030300ull}) {
        CHECK(FrameXr::Kiu::IsKidIcarus(kiu));
    }
    for (const uint64_t other : {0x0004000000030600ull, 0x0004000000030800ull,
                                 0x0004000000033500ull, 0ull}) {
        CHECK_FALSE(FrameXr::Kiu::IsKidIcarus(other));
    }
    CHECK(Settings::values.vr_kiu_trigger_layout.GetDefault() == kOriginal);
}

TEST_CASE("Trigger Layout: the aiming trigger fires (L) and nothing else", "[kiufire]") {
    for (const GyroAimMode mode : {GyroAimMode::Gyro, GyroAimMode::LightGun}) {
        Layout l;
        ControlState c = With({Control::TriggerR});
        CHECK(l.Apply(c, On(mode, Hand::Right, 0)));
        CHECK(OnlyThese(Pad(c), {Btn::L}));
        // Held: L stays held (rapid fire, charge).
        for (uint64_t t = 10; t <= 1500; t += 10) {
            ControlState h = With({Control::TriggerR});
            CHECK(l.Apply(h, On(mode, Hand::Right, t * kMs)));
            CHECK(OnlyThese(Pad(h), {Btn::L}));
        }
        // Let go: nothing.
        CHECK(OnlyThese(Step(l, {}, On(mode, Hand::Right, 1600 * kMs)), {}));
    }
}

TEST_CASE("Trigger Layout: the other trigger uses the power (D-pad Down)", "[kiufire]") {
    for (const GyroAimMode mode : {GyroAimMode::Gyro, GyroAimMode::LightGun}) {
        Layout l;
        ControlState c = With({Control::TriggerL});
        CHECK_FALSE(l.Apply(c, On(mode, Hand::Right)));
        CHECK(OnlyThese(Pad(c), {Btn::Down}));
        // Both triggers: fire and the power, no ZL or ZR.
        CHECK(OnlyThese(Step(l, {Control::TriggerL, Control::TriggerR}, On(mode, Hand::Right, kMs)),
                        {Btn::L, Btn::Down}));
        // Held 1.5 s: one short press of D-pad Down (the game repeats a held D-pad).
        int presses = 0;
        bool last = true;
        for (uint64_t t = 10; t <= 1500; t += 10) {
            const auto p = Step(l, {Control::TriggerL}, On(mode, Hand::Right, t * kMs));
            CHECK_FALSE(p.buttons[Btn::ZL]);
            presses += p.buttons[Btn::Down] && !last ? 1 : 0;
            last = p.buttons[Btn::Down];
            if (t > kPressNs / kMs + 10) {
                CHECK_FALSE(p.buttons[Btn::Down]);
            }
        }
        CHECK(presses == 0); // the first frame's press only
        // Let go and pull again: a second press.
        Step(l, {}, On(mode, Hand::Right, 1600 * kMs));
        CHECK(OnlyThese(Step(l, {Control::TriggerL}, On(mode, Hand::Right, 1610 * kMs)),
                        {Btn::Down}));
    }
}

TEST_CASE("Trigger Layout: a bumper tap picks the previous or next power", "[kiufire]") {
    for (const GyroAimMode mode : {GyroAimMode::Gyro, GyroAimMode::LightGun}) {
        INFO("mode " << static_cast<int>(mode));
        Layout l;
        uint64_t t = 0;
        bool left_seen = false;
        // A 150 ms tap of the left bumper: never L (fire), D-pad Left for a short press.
        for (; t <= 150; t += 10) {
            const auto p = Step(l, {Control::BumperL}, On(mode, Hand::Right, t * kMs));
            CHECK_FALSE(p.buttons[Btn::L]);
            CHECK_FALSE(p.buttons[Btn::ZL]);
            left_seen = left_seen || p.buttons[Btn::Left];
        }
        for (; t <= 600; t += 10) {
            const auto p = Step(l, {}, On(mode, Hand::Right, t * kMs));
            CHECK_FALSE(p.buttons[Btn::ZL]); // a tap never recentres
            CHECK_FALSE(p.buttons[Btn::Right]);
            left_seen = left_seen || p.buttons[Btn::Left];
        }
        CHECK(left_seen);
        CHECK(OnlyThese(Step(l, {}, On(mode, Hand::Right, t * kMs)), {})); // the press ended
        // The right bumper: D-pad Right, never R.
        bool right_seen = false;
        for (uint64_t u = t + 10; u <= t + 150; u += 10) {
            const auto p = Step(l, {Control::BumperR}, On(mode, Hand::Right, u * kMs));
            CHECK_FALSE(p.buttons[Btn::R]);
            right_seen = right_seen || p.buttons[Btn::Right];
        }
        for (uint64_t u = t + 160; u <= t + 600; u += 10) {
            const auto p = Step(l, {}, On(mode, Hand::Right, u * kMs));
            CHECK_FALSE(p.buttons[Btn::Left]);
            right_seen = right_seen || p.buttons[Btn::Right];
        }
        CHECK(right_seen);
    }
}

TEST_CASE("Trigger Layout: Direct Aim picks the power at the press, never recentres",
          "[kiufire]") {
    Layout l;
    const auto first = Step(l, {Control::BumperR}, On(GyroAimMode::LightGun, Hand::Right, 0));
    CHECK(OnlyThese(first, {Btn::Right}));
    // Held for 3 s: one press of D-pad Right, no ZL ever.
    int presses = 0;
    bool last = true;
    for (uint64_t t = 10; t <= 3000; t += 10) {
        const auto p = Step(l, {Control::BumperR}, On(GyroAimMode::LightGun, Hand::Right, t * kMs));
        CHECK_FALSE(p.buttons[Btn::ZL]);
        CHECK_FALSE(p.buttons[Btn::R]);
        presses += p.buttons[Btn::Right] && !last ? 1 : 0;
        last = p.buttons[Btn::Right];
    }
    CHECK(presses == 0); // the first press only
    const auto after = Step(l, {}, On(GyroAimMode::LightGun, Hand::Right, 3010 * kMs));
    CHECK(OnlyThese(after, {}));
}

TEST_CASE("Trigger Layout: Gyro Aim recentres on a bumper hold, and picks nothing then",
          "[kiufire]") {
    Layout l;
    bool zl = false, dpad = false;
    uint64_t t = 0;
    for (; t < kBumperHoldNs / kMs; t += 10) {
        const auto p = Step(l, {Control::BumperL}, On(GyroAimMode::Gyro, Hand::Right, t * kMs));
        CHECK_FALSE(p.buttons[Btn::ZL]); // not yet
        CHECK_FALSE(p.buttons[Btn::Left]);
    }
    for (; t <= 1500; t += 10) {
        const auto p = Step(l, {Control::BumperL}, On(GyroAimMode::Gyro, Hand::Right, t * kMs));
        zl = zl || p.buttons[Btn::ZL];
        dpad = dpad || p.buttons[Btn::Left] || p.buttons[Btn::Right];
        CHECK_FALSE(p.buttons[Btn::L]);
    }
    for (; t <= 2500; t += 10) {
        const auto p = Step(l, {}, On(GyroAimMode::Gyro, Hand::Right, t * kMs));
        dpad = dpad || p.buttons[Btn::Left] || p.buttons[Btn::Right];
        CHECK_FALSE(p.buttons[Btn::ZL]); // once
    }
    CHECK(zl);
    CHECK_FALSE(dpad);
    // The right bumper held recentres too.
    zl = false;
    for (uint64_t u = 3000; u <= 3800; u += 10) {
        zl = zl || Step(l, {Control::BumperR}, On(GyroAimMode::Gyro, Hand::Right, u * kMs))
                       .buttons[Btn::ZL];
    }
    CHECK(zl);
}

TEST_CASE("Trigger Layout: left-handed mirrors the triggers", "[kiufire]") {
    CHECK(AimingHand(2) == Hand::Left);
    for (const uint32_t m : {0u, 1u, 3u}) {
        CHECK(AimingHand(m) == Hand::Right);
    }
    for (const GyroAimMode mode : {GyroAimMode::Gyro, GyroAimMode::LightGun}) {
        Layout l;
        ControlState c = With({Control::TriggerL});
        CHECK(l.Apply(c, On(mode, Hand::Left)));
        CHECK(OnlyThese(Pad(c), {Btn::L}));
        ControlState d = With({Control::TriggerR});
        CHECK_FALSE(l.Apply(d, On(mode, Hand::Left, kMs)));
        CHECK(OnlyThese(Pad(d), {Btn::Down}));
        // The bumpers keep their sides: left previous, right next.
        CHECK(Step(l, {Control::BumperL}, On(GyroAimMode::LightGun, Hand::Left, 2 * kMs))
                  .buttons[Btn::Left]);
    }
}

TEST_CASE("Trigger Layout: the Frame's D-pad keeps working", "[kiufire]") {
    Layout l;
    for (const auto& [ctl, btn] : {std::pair{Control::DpadUp, Btn::Up},
                                   std::pair{Control::DpadDown, Btn::Down},
                                   std::pair{Control::DpadLeft, Btn::Left},
                                   std::pair{Control::DpadRight, Btn::Right}}) {
        CHECK(OnlyThese(Step(l, {ctl}, On(GyroAimMode::LightGun)), {btn}));
    }
    // Leaving the layout (Original chosen, Aiming Off) forgets a bumper held meanwhile.
    Step(l, {Control::BumperL}, On(GyroAimMode::Gyro, Hand::Right, 0));
    Frame off = On(GyroAimMode::Gyro, Hand::Right, 100 * kMs);
    off.fire_and_power = false;
    ControlState c;
    l.Apply(c, off);
    CHECK(OnlyThese(Step(l, {}, On(GyroAimMode::Gyro, Hand::Right, 200 * kMs)), {}));
}
