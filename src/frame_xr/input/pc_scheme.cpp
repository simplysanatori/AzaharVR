// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cmath>
#include "frame_xr/input/pc_scheme.h"

// AzaharVR
namespace FrameXr::Input {

namespace {

bool Hysteresis(bool was_down, float value, float press, float release) {
    return was_down ? value > release : value >= press;
}

float Magnitude(Vec2f v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

/// 0 up, 1 down, 2 left, 3 right: the axis pushed furthest.
int FourWay(Vec2f v) {
    if (std::abs(v.y) >= std::abs(v.x)) {
        return v.y > 0.0f ? 0 : 1;
    }
    return v.x < 0.0f ? 2 : 3;
}

void PressDpad(InputFrame& f, int dir) {
    switch (dir) {
    case 0:
        f.dpad_up = true;
        break;
    case 1:
        f.dpad_down = true;
        break;
    case 2:
        f.dpad_left = true;
        break;
    case 3:
        f.dpad_right = true;
        break;
    default:
        break;
    }
}

} // namespace

void PcScheme::SetScheme(ControllerScheme scheme) {
    if (scheme != scheme_) {
        scheme_ = scheme;
        Reset();
    }
}

void PcScheme::Reset() {
    right_pad_down_ = left_pad_down_ = false;
    click_down_ = {};
    click_ignored_ = {};
    both_clicks_ = false;
    tap_ = {};
    grip_down_ = {};
    swallow_grip_ = {};
    trigger_r_down_ = swallow_trigger_r_ = false;
    menu_down_ = menu_fired_ = chord_used_ = false;
    save_pending_ = load_pending_ = load_fired_ = ff_pending_ = false;
    quick_done_ = false;
    start_until_ns_ = select_until_ns_ = -1;
    stick_dir_ = -1;
}

void PcScheme::Normalize(InputFrame& in) {
    if (!active()) {
        return;
    }
    const ControllerSchemeTable& t = SchemeTable(scheme_);
    tap_ = {};
    if (t.menu == MenuSource::RightTrackpadPress) {
        right_pad_down_ = Hysteresis(right_pad_down_, in.hand[1].trackpad_force,
                                     config_.trackpad_press, config_.trackpad_release);
        in.menu = in.menu || right_pad_down_;
    }
    if (t.dpad == DpadSource::LeftTrackpadPress) {
        left_pad_down_ = Hysteresis(left_pad_down_, in.hand[0].trackpad_force,
                                    config_.trackpad_press, config_.trackpad_release);
        const Vec2f pad = in.hand[0].trackpad;
        if (left_pad_down_ && std::max(std::abs(pad.x), std::abs(pad.y)) >= config_.trackpad_zone) {
            PressDpad(in, FourWay(pad));
        }
    }
    // Stick clicks: a single click is a tap when it is let go; both together are the Menu role
    // (Touch, Focus 3) and neither is a tap. A click that starts with its stick pushed over is
    // ignored (the stick clicks by accident while it moves).
    const std::array<bool, kNumHands> click{in.hand[0].stick_click, in.hand[1].stick_click};
    for (std::size_t h = 0; h < click.size(); h++) {
        if (click[h] && !click_down_[h]) {
            click_ignored_[h] = Magnitude(in.hand[h].stick) > config_.click_max_deflection;
        }
    }
    if (t.both_clicks_menu && click[0] && click[1]) {
        both_clicks_ = true;
    }
    for (std::size_t h = 0; h < click.size(); h++) {
        if (!click[h] && click_down_[h] && !both_clicks_ && !click_ignored_[h]) {
            tap_[h] = true;
        }
    }
    click_down_ = click;
    if (both_clicks_) {
        in.menu = in.menu || (click[0] && click[1]);
        in.hand[0].stick_click = false;
        in.hand[1].stick_click = false;
        if (!click[0] && !click[1]) {
            both_clicks_ = false;
        }
    }
}

InputFrame PcScheme::Game(const InputFrame& in, PcSchemeOut& out) {
    out = {};
    if (!active()) {
        return in;
    }
    const ControllerSchemeTable& t = SchemeTable(scheme_);
    const int64_t now = in.time_ns;
    InputFrame g = in;
    g.menu = false; // the scheme's own (below)
    g.view = false;

    // ---- Grips: L and R (they grab nothing) ----
    std::array<bool, kNumHands> grip_edge{};
    for (std::size_t h = 0; h < grip_down_.size(); h++) {
        const bool down = Hysteresis(grip_down_[h], in.hand[h].grip, config_.grip_press,
                                     config_.grip_release);
        grip_edge[h] = down && !grip_down_[h];
        grip_down_[h] = down;
        if (!down) {
            swallow_grip_[h] = false;
        }
        g.hand[h].grip = 0.0f;
        g.hand[h].stick_click = false; // the scheme's own (below), never the screens' gestures
    }
    const bool trigger_down = Hysteresis(trigger_r_down_, in.hand[1].trigger,
                                         config_.trigger_press, config_.trigger_release);
    const bool trigger_edge = trigger_down && !trigger_r_down_;
    trigger_r_down_ = trigger_down;
    if (!trigger_down) {
        swallow_trigger_r_ = false;
    }

    // ---- The Menu role: tap Start, hold 1 s the VR menu; held, the chords ----
    const bool menu = in.menu;
    if (menu && !menu_down_) {
        menu_down_ = true;
        menu_since_ns_ = now;
        menu_fired_ = false;
        chord_used_ = false;
        start_until_ns_ = -1;
    }
    const bool cooled = !quick_done_ || now - quick_last_ns_ >= config_.quick_cooldown_ns;
    if (menu_down_) {
        // A grip or the right trigger going down while the Menu role is held is a chord: it
        // never reaches the game, and the Menu press is neither Start nor the VR menu.
        if (grip_edge[1] && !save_pending_) {
            save_pending_ = true;
            swallow_grip_[1] = true;
            chord_used_ = true;
        }
        if (grip_edge[0] && !load_pending_) {
            load_pending_ = true;
            load_since_ns_ = now;
            load_fired_ = false;
            swallow_grip_[0] = true;
            chord_used_ = true;
        }
        if (trigger_edge && !ff_pending_) {
            ff_pending_ = true;
            swallow_trigger_r_ = true;
            chord_used_ = true;
        }
    }
    const auto fire = [&](bool& flag) {
        flag = true;
        quick_done_ = true;
        quick_last_ns_ = now;
    };
    // Quick Load: the left grip held long enough with the Menu role (a brush of it loads nothing).
    if (load_pending_) {
        if (menu && grip_down_[0] && !load_fired_ && cooled &&
            now - load_since_ns_ >= config_.quick_load_hold_ns) {
            fire(out.quick_load);
            load_fired_ = true;
        }
        if (!grip_down_[0] || !menu) {
            load_pending_ = false;
        }
    }
    // Quick Save and Fast Forward: when the grip or the trigger is let go with the Menu role
    // still held (or let go in the same frame). Menu let go first: nothing.
    if (save_pending_ && !grip_down_[1]) {
        if (menu_down_ && cooled) {
            fire(out.quick_save);
        }
        save_pending_ = false;
    }
    if (ff_pending_ && !trigger_down) {
        if (menu_down_ && cooled) {
            fire(out.toggle_fast_forward);
        }
        ff_pending_ = false;
    }
    if (menu_down_) {
        if (menu) {
            const int64_t held = now - menu_since_ns_;
            if (!menu_fired_ && !chord_used_) {
                out.menu_hold_progress =
                    std::clamp(static_cast<float>(held) / static_cast<float>(config_.menu_hold_ns),
                               0.0f, 1.0f);
                if (held >= config_.menu_hold_ns) {
                    out.menu_toggle = true;
                    menu_fired_ = true;
                    out.menu_hold_progress = 0.0f;
                }
            }
        } else {
            menu_down_ = false;
            if (!menu_fired_ && !chord_used_ && now - menu_since_ns_ < config_.menu_tap_max_ns) {
                start_until_ns_ = now + config_.pulse_ns;
            }
            save_pending_ = false; // its grip, still held, stays away from the game
            ff_pending_ = false;
            load_pending_ = false;
        }
    }
    out.start = start_until_ns_ >= 0 && now < start_until_ns_;

    for (std::size_t h = 0; h < grip_down_.size(); h++) {
        g.hand[h].bumper = grip_down_[h] && !swallow_grip_[h];
    }
    if (swallow_trigger_r_) {
        g.hand[1].trigger = 0.0f; // the Fast Forward pull: no ZR, no touch
    }

    // ---- Stick clicks: right Select, left the D-pad mode (Touch, Focus 3) ----
    if (t.stick_clicks_for_game) {
        if (tap_[1]) {
            select_until_ns_ = now + config_.pulse_ns;
        }
        if (tap_[0] && t.dpad == DpadSource::LeftStickToggle) {
            dpad_mode_ = !dpad_mode_;
            stick_dir_ = -1;
            out.dpad_mode_changed = true;
        }
    }
    out.select = select_until_ns_ >= 0 && now < select_until_ns_;

    // ---- The left stick as a four-way D-pad ----
    if (t.dpad == DpadSource::LeftStickToggle && dpad_mode_) {
        const Vec2f stick = in.hand[0].stick;
        const float m = Magnitude(stick);
        const bool pushed =
            stick_dir_ < 0 ? m >= config_.stick_dpad_press : m > config_.stick_dpad_release;
        stick_dir_ = pushed ? FourWay(stick) : -1;
        PressDpad(g, stick_dir_);
        g.hand[0].stick = {};
    }
    return g;
}

void PcScheme::Merge(const PcSchemeOut& pc, MapperOutput& out) {
    ControlState& c = out.controls;
    c.Set(Control::Menu, c.Get(Control::Menu) || pc.start);
    c.Set(Control::View, c.Get(Control::View) || pc.select);
    PanelCommands& cmd = out.commands;
    cmd.menu_toggle = cmd.menu_toggle || pc.menu_toggle;
    cmd.menu_hold_progress = std::max(cmd.menu_hold_progress, pc.menu_hold_progress);
    cmd.quick_save = cmd.quick_save || pc.quick_save;
    cmd.quick_load = cmd.quick_load || pc.quick_load;
    cmd.toggle_fast_forward = cmd.toggle_fast_forward || pc.toggle_fast_forward;
}

void MoveScreensInput::Reset() {
    grip_down_ = {};
    grip_swallowed_ = {true, true};
    first_ = -1;
    b_down_ = menu_down_ = true;
    dpad_hand_ = -1;
    stick_dir_ = -1;
}

bool MoveScreensInput::Filter(InputFrame& in, int holder) {
    // B or Menu: a new press finishes (one held as the mode started waits until let go).
    const bool finish = (in.b && !b_down_) || (in.menu && !menu_down_);
    b_down_ = in.b;
    menu_down_ = in.menu;
    for (std::size_t h = 0; h < kNumHands; h++) {
        HandInput& hand = in.hand[h];
        if (grip_swallowed_[h] && hand.grip <= config_.grip_release) {
            grip_swallowed_[h] = false;
        }
        if (grip_swallowed_[h]) {
            hand.grip = 0.0f;
        }
        grip_down_[h] = Hysteresis(grip_down_[h], hand.grip, config_.grip_press,
                                   config_.grip_release);
        // Nothing but the pose, the grip and the stick.
        hand.trigger = 0.0f;
        hand.bumper = false;
        hand.stick_click = false;
        hand.trackpad = {};
        hand.trackpad_force = 0.0f;
        hand.mouse = false;
        hand.reticle = false;
    }
    if (first_ >= 0 && !grip_down_[static_cast<std::size_t>(first_)]) {
        first_ = -1;
    }
    for (int h = 0; h < kNumHands && first_ < 0; h++) {
        if (grip_down_[static_cast<std::size_t>(h)]) {
            first_ = h;
        }
    }
    const int holding = holder >= 0 && holder < kNumHands ? holder : first_;
    dpad_hand_ = holding >= 0 ? holding ^ 1 : -1;
    if (dpad_hand_ >= 0) {
        HandInput& other = in.hand[static_cast<std::size_t>(dpad_hand_)];
        // As the left stick's D-pad mode (Game): past the press, until back within the release.
        const float m = Magnitude(other.stick);
        const bool pushed =
            stick_dir_ < 0 ? m >= config_.stick_dpad_press : m > config_.stick_dpad_release;
        stick_dir_ = pushed ? FourWay(other.stick) : -1;
        PressDpad(in, stick_dir_);
        other.stick = {};
    } else {
        stick_dir_ = -1;
    }
    in.a = in.b = in.x = in.y = false;
    in.menu = in.view = false;
    return finish;
}

MapperOutput ScreensOnly(const MapperOutput& out) {
    MapperOutput only;
    only.cursor = out.cursor;
    const PanelCommands& c = out.commands;
    PanelCommands& d = only.commands;
    d.top_drag_active = c.top_drag_active;
    d.top_drag_target = c.top_drag_target;
    d.drag_panel = c.drag_panel;
    d.drag_hand = c.drag_hand;
    d.top_depth_delta_z = c.top_depth_delta_z;
    d.top_scale_stick = c.top_scale_stick;
    d.top_scale_dt = c.top_scale_dt;
    return only;
}

} // namespace FrameXr::Input
