// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiufire: Trigger Layout's Fire and Power (Settings::values.vr_kiu_trigger_layout 1,
// Kid Icarus: Uprising only, with Aiming on), the pure part. InputBridge::Process runs it on the
// virtual pad's controls after the mapper and the other gamepads, before the pad is written; the
// default controls profile then turns them into the 3DS buttons (BumperL is L, the D-pad is the
// D-pad, TriggerL is ZL). No emulator or OpenXR types here: tests/frame_xr/kiu_trigger_layout.cpp.
//
// What the game does with each button (desktop runs of the EUR game in a land battle, original
// code.bin md5 b107c9b1 and the C-Stick & Gyro mod v172 21:9 b95ca36b; the game's held word is
// [[0xD638A4] + 0x104], the same address in both):
//   L            fires (rapid while held, charged after a pause)
//   D-pad Left   the previous power (the power bar on the bottom screen slides)
//   D-pad Right  the next power
//   D-pad Down   uses the selected power (Idol Transformation turned Pit into the idol)
//   ZR (mod)     fires too (the mod writes ir:rst's ZR into the held word, bit 15)
//   ZL (mod)     reaches the held word (bit 14); no shot, and neither the reticle nor the camera
//                moved (a tap and a 1.5 s hold, Aiming Off)
//   ZL, ZR       without the mod they reach the held word too (bits 14, 15, a pull with Aiming on
//                and Original), so the game's own code gets them as with the mod (what it does
//                with them: as above, inferred, the same code)
// Original (0) keeps today's behaviour: with Aiming on, either trigger presses L as well as its
// own ZL/ZR, so one pull fires and also does what ZL/ZR do.
//
// Fire and Power (1):
//   - the aiming hand's trigger (Motion Controls' controller: the left with Left Controller, else
//     the right) presses L only, held while the trigger is held (rapid fire, charge);
//   - the other trigger presses D-pad Down (uses the selected power) once a pull, as a short
//     press: the game repeats a held D-pad (a long hold moved the power bar twice);
//   - the bumpers no longer press L and R: the left one is D-pad Left (previous power), the right
//     one D-pad Right (next power), as a short press;
//   - Direct Aim: the reticle is where you point, so nothing recentres and a bumper acts the
//     moment it is pressed;
//   - Gyro Aim: a bumper held kBumperHoldNs picks no power and presses ZL briefly instead, meant
//     as the recentre (the player reports had a trigger recentre). NOT proven: no recentre was
//     found on the desktop, so this is the slot for it; let go sooner, the bumper picks the power.
// The Frame's D-pad keeps its own presses (they are added to).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "frame_xr/input/controls.h"
#include "frame_xr/input/gyro_aim.h"
#include "frame_xr/input/types.h"

namespace FrameXr::Input::KiuTriggers {

/// Trigger Layout's stored values (vr_kiu_trigger_layout).
inline constexpr uint32_t kOriginal = 0;
inline constexpr uint32_t kFireAndPower = 1;

/// Gyro Aim: a bumper held this long recentres instead of picking a power.
inline constexpr uint64_t kBumperHoldNs = 500'000'000;
/// How long a power's D-pad press (use, previous, next; and the recentre's ZL) lasts: several of
/// the game's frames, so it is never missed, and short of the game's D-pad repeat.
inline constexpr uint64_t kPressNs = 100'000'000;

/// The aiming hand from Motion Controls (vr_motion_controls: 1 right, 2 left, 3 headset, 0 off)
/// as Gyro Aim picks its controller (GyroAimSource): the left with 2, else the right.
constexpr Hand AimingHand(uint32_t motion_controls) {
    return GyroAimSource(motion_controls) == 2 ? Hand::Left : Hand::Right;
}

/// This frame's inputs.
struct Frame {
    bool fire_and_power = false; ///< the setting is Fire and Power and Kid Icarus is running
    GyroAimMode mode = GyroAimMode::Off;
    Hand aiming = Hand::Right;
    uint64_t time_ns = 0;

    /// Whether the layout is in use: Fire and Power, with Aiming on.
    bool Active() const {
        return fire_and_power && mode != GyroAimMode::Off;
    }
};

class Layout {
public:
    /// Rewrites `c` (the pad's controls) for Fire and Power while `f.Active()`; otherwise leaves it
    /// as it is (and forgets the bumpers). Returns whether the aiming trigger fires (Recoil's shot).
    bool Apply(ControlState& c, const Frame& f) {
        if (!f.Active()) {
            Reset();
            return false;
        }
        const bool trig_l = c.Get(Control::TriggerL);
        const bool trig_r = c.Get(Control::TriggerR);
        const std::array<bool, 2> bump{c.Get(Control::BumperL), c.Get(Control::BumperR)};
        c.Set(Control::TriggerL, false);
        c.Set(Control::TriggerR, false);
        c.Set(Control::BumperL, false);
        c.Set(Control::BumperR, false);

        const bool left_aims = f.aiming == Hand::Left;
        const bool fire = left_aims ? trig_l : trig_r;
        const bool power = left_aims ? trig_r : trig_l;
        c.Set(Control::BumperL, fire); // L: fire
        if (power && !power_down_) {
            power_until_ = f.time_ns + kPressNs; // use the selected power: one press a pull
        }
        power_down_ = power;

        const bool gyro = f.mode == GyroAimMode::Gyro;
        for (std::size_t i = 0; i < 2; i++) {
            Bumper& b = bumpers_[i];
            uint64_t& press = i == 0 ? previous_until_ : next_until_;
            if (bump[i] && !b.down) {
                b.down = true;
                b.since = f.time_ns;
                b.long_hold = false;
                if (!gyro) {
                    press = f.time_ns + kPressNs; // Direct Aim: at once
                }
            } else if (bump[i] && b.down) {
                if (gyro && !b.long_hold && f.time_ns - b.since >= kBumperHoldNs) {
                    b.long_hold = true;
                    recentre_until_ = f.time_ns + kPressNs;
                }
            } else if (!bump[i] && b.down) {
                b.down = false;
                if (gyro && !b.long_hold) {
                    press = f.time_ns + kPressNs; // Gyro Aim: a tap, on release
                }
            }
        }
        if (f.time_ns < power_until_) {
            c.Set(Control::DpadDown, true);
        }
        if (f.time_ns < previous_until_) {
            c.Set(Control::DpadLeft, true);
        }
        if (f.time_ns < next_until_) {
            c.Set(Control::DpadRight, true);
        }
        if (f.time_ns < recentre_until_) {
            c.Set(Control::TriggerL, true); // ZL: the recentre's slot (not proven)
        }
        return fire;
    }

    void Reset() {
        bumpers_ = {};
        power_down_ = false;
        power_until_ = previous_until_ = next_until_ = recentre_until_ = 0;
    }

private:
    struct Bumper {
        bool down = false;
        bool long_hold = false; ///< held past kBumperHoldNs (Gyro Aim): recentred, picks nothing
        uint64_t since = 0;
    };
    std::array<Bumper, 2> bumpers_{};
    bool power_down_ = false; ///< the other trigger was held last frame
    uint64_t power_until_ = 0;
    uint64_t previous_until_ = 0;
    uint64_t next_until_ = 0;
    uint64_t recentre_until_ = 0;
};

} // namespace FrameXr::Input::KiuTriggers
