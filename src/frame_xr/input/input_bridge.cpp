// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include "common/settings.h"
#include "frame_xr/input/devices.h" // AzaharFrame pads
#include "frame_xr/input/input_bridge.h"
#include "frame_xr/kiu_probe.h" // AzaharFrame kiugyro
#include "frame_xr/kiu_turn.h"  // AzaharFrame kiuturn
#include "input_common/frame_xr.h"
#include "input_common/main.h"

// AzaharFrame
namespace FrameXr::Input {

InputBridge::InputBridge(const MapperConfig& config) : mapper_(config) {}

const MapperOutput& InputBridge::Process(const InputFrame& frame, const PanelGeometry& panels,
                                         const InputFrame* gamepads, const MouseFrame* mouse,
                                         const ReticleSample* reticle,
                                         const GyroAimFrame* gyro_aim) {
    const bool by_position = Settings::values.vr_face_buttons.GetValue() == 0;
    if (pc_.active()) {
        // AzaharVR: a PC controller scheme: its buttons by label; grips, Menu and the stick
        // clicks its own way (pc_scheme.h), the rest through the mapper as on the Frame.
        mapper_.SetFaceButtonsByPosition(by_position &&
                                         !SchemeTable(pc_.scheme()).face_buttons_by_label);
        const InputFrame game = pc_.Game(frame, pc_out_);
        last_ = mapper_.Update(game, panels);
        PcScheme::Merge(pc_out_, last_);
    } else {
        pc_out_ = {};
        mapper_.SetFaceButtonsByPosition(by_position);
        last_ = mapper_.Update(frame, panels);
    }
    // AzaharFrame pads: the other gamepads join the virtual pad (through the controls profile's
    // routes), and give Touchpad Touch its touchpad.
    const TouchpadMode touch_mode = TouchpadModeFrom(Settings::values.vr_touchpad_touch.GetValue());
    GamepadHub::MergeResult pads;
    const bool mouse_here = mouse && mouse->present; // AzaharFrame padsui
    if (gamepads || touch_mode != TouchpadMode::Off || mouse_here) {
        GamepadHub& hub = Devices::Hub();
        hub.SetFaceButtonsByPosition(by_position);
        pads = hub.Merge(gamepads ? *gamepads : frame, InputCommon::FrameXr::GetProfileRoutes(),
                         last_.controls, gamepads != nullptr);
    }
    // AzaharFrame kiuturn: Right Stick turning (test switch AZAHARFRAME_KIU_RIGHT_STICK_TURN, in
    // VR only): the right stick's x goes to the land camera's turn (kiu_turn.h); while a land
    // battle runs the game gets a centred C-Stick, Direct Aim does not pause for the stick and
    // the Turn Zone is skipped. Off, or anywhere else, nothing changes.
    bool kiu_turn_land = false;
    if (gyro_aim && Kiu::Turn::Wanted()) {
        Kiu::Turn::Publish(last_.controls.GetStick(AnalogControl::StickR).x,
                           ClampTurnSpeed(Settings::values.vr_light_gun_turn_speed.GetValue()));
        kiu_turn_land = Kiu::Turn::LandActive();
        kiu_turn_on_ = true;
    } else if (kiu_turn_on_) {
        Kiu::Turn::Stop();
        kiu_turn_on_ = false;
    }
    {
        // 0 = off, 1 = right controller, 2 = left controller, 3 = headset. AzaharFrame
        // kiugyro: Gyro Aim uses the right controller when Motion Controls is Off.
        const GyroAimMode aim = gyro_aim ? gyro_aim->mode : GyroAimMode::Off;
        const u32 setting = Settings::values.vr_motion_controls.GetValue();
        const u32 choice = aim != GyroAimMode::Off ? GyroAimSource(setting) : setting;
        const Hand hand = choice == 2 ? Hand::Left : Hand::Right;
        const HandInput source = choice == 3
                                     ? HeadAsMotionSource(frame)
                                     : frame.hand[static_cast<std::size_t>(HandIndex(hand))];
        const MotionSample motion = motion_.Update(source, frame.time_ns, choice != 0);
        // AzaharFrame kiugyro: Gyro Sensitivity, every game.
        const float sensitivity =
            ClampGyroSensitivity(Settings::values.vr_gyro_sensitivity.GetValue());
        Vec3f gyro = motion.gyro * sensitivity;
        if (aim != GyroAimMode::Off) {
            // The mod's axes (z right, x down) from the ray's yaw and pitch; zero while the grip
            // is held. Light Gun replaces it on the emulation thread while it reads the reticle;
            // this is its fallback.
            const GyroAimTracker::Out g =
                gyro_aim_.Update(source, frame.time_ns, sensitivity, gyro_aim->clutch);
            gyro = ModGyro(g.right_dps, g.down_dps);
        } else {
            gyro_aim_.Reset();
        }
        InputCommon::FrameXr::GetMotion().Set(
            Common::Vec3f{motion.accel.x, motion.accel.y, motion.accel.z},
            Common::Vec3f{gyro.x, gyro.y, gyro.z});
        if (aim == GyroAimMode::LightGun) {
            Kiu::LightGunFrame lg;
            lg.enabled = true;
            lg.target = gyro_aim->target; // AzaharFrame kiuaim2: the beam's own hit
            lg.zone_fraction =
                gyro_aim->checking || kiu_turn_land // AzaharFrame kiuturn
                    ? 0.0f
                    : static_cast<float>(std::min<u32>(
                          Settings::values.vr_light_gun_turn_zone.GetValue(), kTurnZoneMaxPercent)) /
                          100.0f;
            lg.zone_from = // AzaharFrame kiuaim3
                TurnZoneFromValue(Settings::values.vr_light_gun_turn_zone_from.GetValue());
            lg.turn_speed = ClampTurnSpeed(Settings::values.vr_light_gun_turn_speed.GetValue());
            lg.turn_speed_vertical = ClampTurnSpeedVertical( // AzaharFrame fixa
                Settings::values.vr_light_gun_turn_speed_vertical.GetValue());
            lg.clutch = gyro_aim->clutch;
            // AzaharFrame kiufix2: the C-Stick (the right stick, past its dead zone, with the
            // other gamepads') steers while it is pushed; Direct Aim pauses meanwhile.
            lg.stick = !kiu_turn_land && // AzaharFrame kiuturn: the stick turns instead
                       CStickTakesAim(last_.controls.GetStick(AnalogControl::StickR));
            Kiu::Publish(lg);
            light_gun_on_ = true;
        } else if (light_gun_on_) {
            Kiu::Stop();
            light_gun_on_ = false;
        }
    }
    // AzaharFrame touchpad: the beam's touch and the touchpad's (or the mouse's) share the sink;
    // the last one to touch wins (touchpad_touch.h). With Touchpad Touch off the beam's events
    // pass through as they were.
    TouchpadFrame touch;
    touch.mode = touch_mode;
    touch.bottom_visible = panels.bottom.visible;
    touch.time_ns = frame.time_ns;
    touch.beam = last_.touch_events.data();
    touch.beam_count = last_.touch_event_count;
    if (touch.mode != TouchpadMode::Off) {
        touch.pad = pads.touchpad.pad; // AzaharFrame pads: the chosen pad's, or the touched one's
        touch.pad_recentre = pads.touchpad.recentre;
        // The pointer over the window (a touchpad as a mouse, Steam Input's); not while a real
        // mouse is read directly (AzaharFrame padsui), whose clicks the window also gets.
        if (!mouse_here) {
            const auto window_mouse = InputCommon::FrameXr::GetMouseState();
            touch.mouse = {window_mouse.inside, window_mouse.down, window_mouse.x, window_mouse.y};
        }
    }
    // AzaharFrame padsui: the mouse in Bottom Screen mode, a game running and the menu closed.
    if (mouse_here && gamepads && mouse->mode == MouseMode::BottomScreen) {
        touch.cursor = {true, mouse->motion, MouseScreenGain(mouse->speed), mouse->Touch(),
                        mouse->recentre,
                        // AzaharFrame fixes7: Aim Only While Pressed, as the reticle's
                        Settings::values.vr_reticle_center_until_pressed.GetValue()};
    }
    if (reticle) {
        touch.reticle = *reticle; // AzaharFrame mouse
    }
    const TouchpadResult result = touchpad_.Update(touch);
    touchpad_cursor_ = result.cursor;
    // AzaharFrame kiu: C-Stick at Edge: the push goes onto the C-Stick (the right stick's, after
    // its dead zone and the other gamepads). AzaharFrame kiu2: the touch pipeline hands the
    // reticle's touch over to it in the edge band (reticle.h ReticleEdgeHandoff).
    edge_stick_ = result.edge_stick;
    if (edge_stick_.x != 0.0f || edge_stick_.y != 0.0f) {
        last_.controls.SetStick(AnalogControl::StickR,
                                AddStick(last_.controls.GetStick(AnalogControl::StickR), edge_stick_));
    }
    if (kiu_turn_land) {
        last_.controls.SetStick(AnalogControl::StickR, {}); // AzaharFrame kiuturn: centred C-Stick
    }
    // AzaharFrame kiugyro: while Gyro Aim is on, a trigger that is ZL/ZR (not a touch) also
    // presses the Left Bumper's control, L in the default profile: Kid Icarus: Uprising fires
    // with L, so point and pull fires with either hand. The mod passes ZL/ZR on as well (it
    // writes ir:rst's ZL/ZR into the game's held buttons).
    gyro_fire_ = false;
    // AzaharFrame kiufire: Trigger Layout's Fire and Power (Kid Icarus, Aiming on) instead: the
    // aiming hand's trigger is L only, the other uses the power, the bumpers pick it
    // (kiu_trigger_layout.h). Original, or any other game: as before.
    KiuTriggers::Frame layout;
    if (gyro_aim) {
        layout.fire_and_power = gyro_aim->kiu_fire_and_power;
        layout.mode = gyro_aim->mode;
        layout.aiming = KiuTriggers::AimingHand(Settings::values.vr_motion_controls.GetValue());
        layout.time_ns = frame.time_ns;
    }
    if (layout.Active()) {
        gyro_fire_ = kiu_triggers_.Apply(last_.controls, layout);
    } else {
        kiu_triggers_.Reset();
        if (gyro_aim && gyro_aim->mode != GyroAimMode::Off &&
            (last_.controls.Get(Control::TriggerL) || last_.controls.Get(Control::TriggerR))) {
            last_.controls.Set(Control::BumperL, true);
            gyro_fire_ = true;
        }
    }
    InputCommon::FrameXr::GetPad().SetControls(last_.controls);
    for (int i = 0; i < result.event_count; ++i) {
        const TouchEvent& event = result.events[static_cast<std::size_t>(i)];
        switch (event.type) {
        case TouchEventType::Pressed:
            InputCommon::FrameXr::SendTouchPressed(event.x, event.y);
            break;
        case TouchEventType::Moved:
            InputCommon::FrameXr::SendTouchMoved(event.x, event.y);
            break;
        case TouchEventType::Released:
            InputCommon::FrameXr::SendTouchReleased();
            break;
        }
    }
    return last_;
}

void InputBridge::Release() {
    const bool mapper_touching = mapper_.Reset();
    pc_.Reset(); // AzaharVR
    pc_out_ = {};
    if (touchpad_.Reset() || mapper_touching) { // AzaharFrame touchpad
        InputCommon::FrameXr::SendTouchReleased();
    }
    touchpad_cursor_ = {};
    edge_stick_ = {}; // AzaharFrame kiu
    gyro_fire_ = false; // AzaharFrame kiugyro
    kiu_triggers_.Reset(); // AzaharFrame kiufire
    InputCommon::FrameXr::GetPad().ReleaseAll();
    Devices::Hub().Release(); // AzaharFrame pads: what is held stays away until let go
    motion_.Reset();
    gyro_aim_.Reset(); // AzaharFrame kiugyro
    if (light_gun_on_) {
        Kiu::Stop();
        light_gun_on_ = false;
    }
    if (kiu_turn_on_) { // AzaharFrame kiuturn
        Kiu::Turn::Stop();
        kiu_turn_on_ = false;
    }
    InputCommon::FrameXr::GetMotion().Reset();
    last_ = MapperOutput{};
}

} // namespace FrameXr::Input
