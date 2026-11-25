// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include "frame_xr/input/gyro_aim.h" // AzaharFrame kiugyro
#include "frame_xr/input/kiu_trigger_layout.h" // AzaharFrame kiufire
#include "frame_xr/input/mapper.h"
#include "frame_xr/input/motion.h"
#include "frame_xr/input/pc_scheme.h" // AzaharVR
#include "frame_xr/input/mouse.h"
#include "frame_xr/input/touchpad_touch.h"

// AzaharFrame
// Glue between the mapper and the emulator: runs the mapper on an InputFrame, writes the virtual
// pad that the "frame_xr" input engine reads, and sends touches to the touch sink. The panel
// commands and the cursor are returned for the frame loop to apply. XR thread only.
namespace FrameXr::Input {

/// AzaharFrame kiugyro: Gyro Aim this frame (controls.cpp): the mode (Settings::values.vr_gyro_aim,
/// Off outside a game or with the menu open) and whether the grip of its controller is held.
struct GyroAimFrame {
    GyroAimMode mode = GyroAimMode::Off;
    bool clutch = false;
    /// AzaharFrame kiuaim2: Direct Aim's target this frame, from the one ray it draws and aims
    /// with (Input::AimRay), through the game's Check Aim map (controls.cpp).
    LightGunTarget target;
    bool checking = false; ///< AzaharFrame kiuaim2: Check Aim runs (no Turn Zone meanwhile)
    /// AzaharFrame kiufire: Trigger Layout is Fire and Power and Kid Icarus: Uprising is running
    /// (controls.cpp; kiu_trigger_layout.h).
    bool kiu_fire_and_power = false;
    bool kiu_air = false; ///< AzaharFrame kiupowers: an air battle (KiuTriggers::Frame::air)
};

class InputBridge {
public:
    explicit InputBridge(const MapperConfig& config = {});

    /// One display frame. `panels` is where the screens are now. The returned reference is valid
    /// until the next call. AzaharFrame pads: with `gamepads` (VR, a game running, the menu
    /// closed: the controllers' raw input of this frame) the other gamepads join the Frame's
    /// virtual pad (frame_xr/input/devices.h).
    /// AzaharFrame padsui: `mouse` (the hub's PollMouse of this frame) in Bottom Screen mode
    /// moves its cursor and touches; its buttons join with the gamepads'.
    /// AzaharFrame mouse: `reticle` (Bottom Screen Reticle) touches through the same pipeline.
    const MapperOutput& Process(const InputFrame& frame, const PanelGeometry& panels,
                                const InputFrame* gamepads = nullptr,
                                const MouseFrame* mouse = nullptr,
                                const ReticleSample* reticle = nullptr,
                                const GyroAimFrame* gyro_aim = nullptr);

    /// Releases every button, centres the sticks and lifts a touch in progress. Call when the
    /// game ends, the session loses focus, or the picker takes over.
    void Release();

    Mapper& mapper() {
        return mapper_;
    }

    /// AzaharVR: the controller scheme in front of the mapper (pc_scheme.h). The Frame's scheme
    /// (the default) passes everything through. Process runs its Game step; the caller runs its
    /// Normalize on the frame first (VrUi::Frame).
    PcScheme& pc() {
        return pc_;
    }
    /// AzaharVR: the scheme's own part of the last Process (empty with the Frame's scheme).
    const PcSchemeOut& pc_out() const {
        return pc_out_;
    }

    /// AzaharFrame touchpad: the pad's or the mouse's dot of the last Process (never visible
    /// while the beam holds the touch, or with the bottom screen hidden).
    const TouchpadCursor& touchpad_cursor() const {
        return touchpad_cursor_;
    }

    /// AzaharFrame kiu: C-Stick at Edge's push added to the C-Stick in the last Process (zero
    /// when none).
    Vec2f edge_stick() const {
        return edge_stick_;
    }
    /// AzaharFrame kiugyro: fire (a trigger as ZL/ZR, pressing L too) while Gyro Aim was on, in
    /// the last Process: Recoil's shot.
    bool gyro_fire() const {
        return gyro_fire_;
    }
    /// AzaharFrame kiu2: C-Stick at Edge's hand-over state after the last Process.
    ReticleEdgeHandoff::State edge_state() const {
        return touchpad_.edge_state();
    }

private:
    Mapper mapper_;
    PcScheme pc_;        ///< AzaharVR
    PcSchemeOut pc_out_; ///< AzaharVR
    MotionTracker motion_;
    GyroAimTracker gyro_aim_; ///< AzaharFrame kiugyro
    bool light_gun_on_ = false;
    bool kiu_turn_on_ = false; ///< AzaharFrame kiuturn: Right Stick turning's stick published
    bool gyro_fire_ = false;
    KiuTriggers::Layout kiu_triggers_; ///< AzaharFrame kiufire: Trigger Layout's Fire and Power
    TouchpadTouch touchpad_;
    TouchpadCursor touchpad_cursor_;
    Vec2f edge_stick_; ///< AzaharFrame kiu
    bool alt_touching_ = false;
    uint16_t alt_touch_x_ = 0;
    uint16_t alt_touch_y_ = 0;
    MapperOutput last_;
};

} // namespace FrameXr::Input
