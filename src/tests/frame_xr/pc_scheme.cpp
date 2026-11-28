// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "frame_xr/input/default_bindings.h"
#include "frame_xr/input/mapper.h"
#include "frame_xr/input/pc_scheme.h"
#include "tests/frame_xr/input_test_util.h"

// AzaharVR: the PC controller schemes (pc_scheme.h, default_bindings.h kControllerSchemes) in
// front of the mapper, as InputBridge::Process runs them.
using namespace FrameXr::Input;
using namespace FrameXr::Input::TestUtil;

namespace {

struct Runner {
    PcScheme pc;
    Mapper mapper;
    PanelGeometry panels = StandardPanels();
    int64_t now = 0;
    PcSchemeOut last_pc;

    explicit Runner(ControllerScheme scheme) {
        pc.SetScheme(scheme);
    }

    MapperOutput Step(InputFrame frame) {
        frame.time_ns = now;
        now += kFrameNs;
        pc.Normalize(frame);
        if (!pc.active()) {
            last_pc = {};
            mapper.SetFaceButtonsByPosition(true);
            return mapper.Update(frame, panels);
        }
        mapper.SetFaceButtonsByPosition(!SchemeTable(pc.scheme()).face_buttons_by_label);
        const InputFrame game = pc.Game(frame, last_pc);
        MapperOutput out = mapper.Update(game, panels);
        PcScheme::Merge(last_pc, out);
        return out;
    }

    /// `frames` frames of the same input; true when any of them had `control` down.
    bool Hold(const InputFrame& frame, int frames, Control control, MapperOutput* last = nullptr) {
        bool seen = false;
        for (int i = 0; i < frames; i++) {
            const MapperOutput out = Step(frame);
            seen = seen || out.controls.Get(control);
            if (last) {
                *last = out;
            }
        }
        return seen;
    }
};

constexpr int kSecond = 90; // frames (kFrameNs is 90 Hz)

bool AnyButton(const MapperOutput& out) {
    for (int c = 0; c < kNumControls; c++) {
        if (out.controls.Get(static_cast<Control>(c))) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("AzaharVR schemes: profiles pick their table; the Frame's row is the Frame's mapping",
          "[frame_xr][pcvr]") {
    CHECK(SchemeForProfile("") == ControllerScheme::Frame);
    CHECK(SchemeForProfile("/interaction_profiles/valve/frame_controller") ==
          ControllerScheme::Frame);
    CHECK(SchemeForProfile("/interaction_profiles/oculus/touch_controller") ==
          ControllerScheme::Touch);
    CHECK(SchemeForProfile("/interaction_profiles/valve/index_controller") ==
          ControllerScheme::Index);
    CHECK(SchemeForProfile("/interaction_profiles/htc/vive_focus3_controller") ==
          ControllerScheme::Focus3);
    CHECK(SchemeForProfile("/interaction_profiles/meta/touch_controller_plus") ==
          ControllerScheme::Touch);
    for (std::size_t i = 0; i < kControllerSchemes.size(); i++) {
        CHECK(static_cast<std::size_t>(kControllerSchemes[i].scheme) == i);
    }
    const ControllerSchemeTable& frame = SchemeTable(ControllerScheme::Frame);
    CHECK(frame.menu == MenuSource::MenuButton);
    CHECK(frame.dpad == DpadSource::FrameDpad);
    CHECK_FALSE(frame.grips_are_lr);
    CHECK_FALSE(frame.both_clicks_menu);
    CHECK_FALSE(frame.stick_clicks_for_game);
    CHECK_FALSE(frame.face_buttons_by_label);
    // Focus 3 plays as Touch.
    const ControllerSchemeTable& touch = SchemeTable(ControllerScheme::Touch);
    const ControllerSchemeTable& focus3 = SchemeTable(ControllerScheme::Focus3);
    CHECK(focus3.menu == touch.menu);
    CHECK(focus3.dpad == touch.dpad);
    CHECK(focus3.grips_are_lr == touch.grips_are_lr);
    CHECK(focus3.both_clicks_menu == touch.both_clicks_menu);
    CHECK(focus3.stick_clicks_for_game == touch.stick_clicks_for_game);
    CHECK(focus3.face_buttons_by_label == touch.face_buttons_by_label);
}

TEST_CASE("AzaharVR schemes: the Frame's scheme changes nothing the mapper sees or does",
          "[frame_xr][pcvr]") {
    // The same input through the Frame scheme and through the mapper alone, frame by frame: grips
    // grabbing, bumpers, View chords (Quick Save), Menu, stick clicks, the D-pad, triggers.
    Mapper alone;
    Runner with(ControllerScheme::Frame);
    const PanelGeometry panels = StandardPanels();
    std::vector<InputFrame> frames;
    const auto add = [&](int n, auto&& set) {
        for (int i = 0; i < n; i++) {
            InputFrame f = IdleFrame();
            set(f);
            frames.push_back(f);
        }
    };
    add(5, [](InputFrame&) {});
    add(20, [](InputFrame& f) { f.hand[0].bumper = true; });
    add(10, [](InputFrame& f) { f.view = true; });
    add(10, [](InputFrame& f) {
        f.view = true;
        f.hand[1].bumper = true;
    });
    add(10, [](InputFrame& f) { f.view = true; });
    add(5, [](InputFrame&) {});
    add(10, [](InputFrame& f) { f.menu = true; });
    add(5, [](InputFrame&) {});
    add(100, [](InputFrame& f) { f.menu = true; });
    add(10, [](InputFrame& f) {
        f.hand[1].stick_click = true;
        f.hand[0].stick_click = true;
    });
    add(10, [](InputFrame& f) {
        f.dpad_up = true;
        f.hand[0].trigger = 1.0f;
        f.hand[1].grip = 1.0f;
        f.hand[1].trackpad = {0.0f, 0.9f};
        f.hand[1].trackpad_force = 1.0f;
        Point(f, Hand::Right, Vec3f{0.0f, 0.0f, -2.0f});
    });
    add(10, [](InputFrame& f) {
        f.hand[1].grip = 1.0f;
        f.hand[1].stick = {0.0f, 0.9f};
        Point(f, Hand::Right, Vec3f{0.0f, 0.0f, -2.0f});
    });
    add(5, [](InputFrame&) {});
    int64_t now = 0;
    for (InputFrame f : frames) {
        f.time_ns = now;
        now += kFrameNs;
        const MapperOutput a = alone.Update(f, panels);
        // Runner::Step sets its own time from 0 too.
        const MapperOutput b = with.Step(f);
        REQUIRE(with.last_pc.start == false);
        for (int c = 0; c < kNumControls; c++) {
            REQUIRE(a.controls.Get(static_cast<Control>(c)) ==
                    b.controls.Get(static_cast<Control>(c)));
        }
        for (int s = 0; s < kNumAnalogControls; s++) {
            REQUIRE(a.controls.sticks[static_cast<std::size_t>(s)].x ==
                    b.controls.sticks[static_cast<std::size_t>(s)].x);
            REQUIRE(a.controls.sticks[static_cast<std::size_t>(s)].y ==
                    b.controls.sticks[static_cast<std::size_t>(s)].y);
        }
        REQUIRE(a.commands.quick_save == b.commands.quick_save);
        REQUIRE(a.commands.quick_load == b.commands.quick_load);
        REQUIRE(a.commands.menu_toggle == b.commands.menu_toggle);
        REQUIRE(a.commands.swap_screens == b.commands.swap_screens);
        REQUIRE(a.commands.reset_panels == b.commands.reset_panels);
        REQUIRE(a.commands.top_drag_active == b.commands.top_drag_active);
        REQUIRE(a.commands.toggle_fast_forward == b.commands.toggle_fast_forward);
        REQUIRE(a.touch_event_count == b.touch_event_count);
    }
    // And Normalize leaves a Frame frame as it was.
    PcScheme pc;
    InputFrame f = IdleFrame();
    f.hand[0].stick_click = f.hand[1].stick_click = true;
    f.hand[1].trackpad_force = 1.0f;
    InputFrame g = f;
    pc.Normalize(g);
    CHECK(g.menu == f.menu);
    CHECK(g.hand[0].stick_click);
    CHECK(g.hand[1].stick_click);
    CHECK_FALSE(g.dpad_up);
}

TEST_CASE("AzaharVR Touch: buttons by label, grips are L and R, triggers ZL and ZR",
          "[frame_xr][pcvr]") {
    Runner r(ControllerScheme::Touch);
    const auto only = [](const MapperOutput& out, Control c) {
        for (int i = 0; i < kNumControls; i++) {
            if (out.controls.Get(static_cast<Control>(i)) != (static_cast<Control>(i) == c)) {
                return false;
            }
        }
        return true;
    };
    InputFrame f = IdleFrame();
    f.a = true;
    CHECK(only(r.Step(f), Control::A));
    f = IdleFrame();
    f.b = true;
    CHECK(only(r.Step(f), Control::B));
    f = IdleFrame();
    f.x = true;
    CHECK(only(r.Step(f), Control::X));
    f = IdleFrame();
    f.y = true;
    CHECK(only(r.Step(f), Control::Y));
    // Grips: L above 0.60, held down to 0.40, released below.
    f = IdleFrame();
    f.hand[0].grip = 0.55f;
    CHECK_FALSE(r.Step(f).controls.Get(Control::BumperL));
    f.hand[0].grip = 0.65f;
    MapperOutput out = r.Step(f);
    CHECK(only(out, Control::BumperL));
    CHECK_FALSE(out.commands.top_drag_active);
    f.hand[0].grip = 0.45f;
    CHECK(r.Step(f).controls.Get(Control::BumperL));
    f.hand[0].grip = 0.35f;
    CHECK_FALSE(r.Step(f).controls.Get(Control::BumperL));
    // A grip on a screen grabs nothing: it is R.
    f = IdleFrame();
    Point(f, Hand::Right, Vec3f{0.0f, 0.0f, -2.0f});
    f.hand[1].grip = 1.0f;
    out = r.Step(f);
    CHECK(out.controls.Get(Control::BumperR));
    CHECK_FALSE(out.commands.top_drag_active);
    r.Step(IdleFrame());
    f = IdleFrame();
    f.hand[1].trigger = 1.0f;
    CHECK(only(r.Step(f), Control::TriggerR));
    r.Step(IdleFrame());
}

TEST_CASE("AzaharVR Touch: Menu tap is Start, held 1 s the VR menu", "[frame_xr][pcvr]") {
    Runner r(ControllerScheme::Touch);
    InputFrame menu = IdleFrame();
    menu.menu = true;
    CHECK_FALSE(r.Hold(menu, 10, Control::Menu)); // nothing while held
    CHECK(r.Hold(IdleFrame(), 15, Control::Menu)); // Start once let go (a 0.1 s pulse)
    CHECK_FALSE(r.Hold(IdleFrame(), 20, Control::Menu));
    // Held: the VR menu at 1 s, and no Start.
    bool toggled = false;
    for (int i = 0; i < kSecond + 5; i++) {
        const MapperOutput out = r.Step(menu);
        toggled = toggled || out.commands.menu_toggle;
        CHECK_FALSE(out.controls.Get(Control::Menu));
    }
    CHECK(toggled);
    CHECK_FALSE(r.Hold(IdleFrame(), 20, Control::Menu));
    // A hold let go before 1 s (longer than a tap): nothing.
    CHECK_FALSE(r.Hold(menu, 50, Control::Menu));
    CHECK_FALSE(r.Hold(IdleFrame(), 20, Control::Menu));
}

TEST_CASE("AzaharVR Touch: the Menu chords never reach the game", "[frame_xr][pcvr]") {
    SECTION("Menu + right grip: Quick Save when the grip is let go") {
        Runner r(ControllerScheme::Touch);
        InputFrame f = IdleFrame();
        f.menu = true;
        r.Hold(f, 5, Control::Menu);
        f.hand[1].grip = 1.0f;
        bool any_r = false, saved = false, toggled = false;
        for (int i = 0; i < kSecond * 2; i++) { // long enough for the 1 s menu hold
            const MapperOutput out = r.Step(f);
            any_r = any_r || out.controls.Get(Control::BumperR);
            saved = saved || out.commands.quick_save;
            toggled = toggled || out.commands.menu_toggle;
        }
        CHECK_FALSE(saved);
        f.hand[1].grip = 0.0f;
        const MapperOutput out = r.Step(f);
        CHECK(out.commands.quick_save);
        CHECK_FALSE(any_r);
        CHECK_FALSE(toggled);
        f.menu = false;
        CHECK_FALSE(r.Hold(f, 10, Control::Menu)); // no Start either
    }
    SECTION("Menu + left grip held 0.5 s: Quick Load; a brush loads nothing") {
        Runner r(ControllerScheme::Touch);
        InputFrame f = IdleFrame();
        f.menu = true;
        r.Step(f);
        f.hand[0].grip = 1.0f;
        bool loaded = false;
        for (int i = 0; i < kSecond / 5; i++) {
            loaded = loaded || r.Step(f).commands.quick_load;
        }
        f.hand[0].grip = 0.0f;
        loaded = loaded || r.Step(f).commands.quick_load;
        CHECK_FALSE(loaded);
        f.hand[0].grip = 1.0f;
        bool any_l = false;
        for (int i = 0; i < kSecond; i++) {
            const MapperOutput out = r.Step(f);
            loaded = loaded || out.commands.quick_load;
            any_l = any_l || out.controls.Get(Control::BumperL);
        }
        CHECK(loaded);
        CHECK_FALSE(any_l);
    }
    SECTION("Menu + right trigger: Fast Forward when let go; no ZR") {
        Runner r(ControllerScheme::Touch);
        InputFrame f = IdleFrame();
        f.menu = true;
        r.Step(f);
        f.hand[1].trigger = 1.0f;
        CHECK_FALSE(r.Hold(f, 10, Control::TriggerR));
        f.hand[1].trigger = 0.0f;
        CHECK(r.Step(f).commands.toggle_fast_forward);
    }
    SECTION("Menu let go before the grip: nothing; the grip stays away until let go") {
        Runner r(ControllerScheme::Touch);
        InputFrame f = IdleFrame();
        f.menu = true;
        r.Step(f);
        f.hand[1].grip = 1.0f;
        r.Hold(f, 5, Control::BumperR);
        f.menu = false;
        CHECK_FALSE(r.Hold(f, 10, Control::BumperR));
        f.hand[1].grip = 0.0f;
        bool saved = r.Step(f).commands.quick_save;
        CHECK_FALSE(saved);
        f.hand[1].grip = 1.0f;
        CHECK(r.Hold(f, 3, Control::BumperR)); // a new press is R again
    }
}

TEST_CASE("AzaharVR Touch: stick clicks, the D-pad mode and the both-clicks Menu",
          "[frame_xr][pcvr]") {
    Runner r(ControllerScheme::Touch);
    InputFrame click_r = IdleFrame();
    click_r.hand[1].stick_click = true;
    CHECK_FALSE(r.Hold(click_r, 5, Control::View));
    MapperOutput out = r.Step(IdleFrame());
    CHECK(out.controls.Get(Control::View)); // Select
    CHECK_FALSE(out.commands.swap_screens);
    r.Hold(IdleFrame(), 20, Control::View);
    // A click with the stick pushed over is ignored.
    click_r.hand[1].stick = {0.0f, 0.9f};
    r.Hold(click_r, 5, Control::View);
    CHECK_FALSE(r.Hold(IdleFrame(), 10, Control::View));

    // Left click: the left stick becomes a four-way D-pad, and back.
    InputFrame click_l = IdleFrame();
    click_l.hand[0].stick_click = true;
    r.Hold(click_l, 3, Control::View);
    CHECK_FALSE(r.Step(IdleFrame()).commands.toggle_other_screen);
    CHECK(r.last_pc.dpad_mode_changed);
    CHECK(r.pc.dpad_mode());
    InputFrame up = IdleFrame();
    up.hand[0].stick = {0.1f, 0.9f};
    out = r.Step(up);
    CHECK(out.controls.Get(Control::DpadUp));
    CHECK_FALSE(out.controls.Get(Control::DpadLeft));
    CHECK(out.controls.GetStick(AnalogControl::StickL).x == 0.0f);
    CHECK(out.controls.GetStick(AnalogControl::StickL).y == 0.0f);
    InputFrame right = IdleFrame();
    right.hand[0].stick = {0.9f, -0.2f};
    out = r.Step(right);
    CHECK(out.controls.Get(Control::DpadRight));
    CHECK_FALSE(out.controls.Get(Control::DpadDown));
    right.hand[0].stick = {0.4f, 0.0f}; // within the release: still held
    CHECK(r.Step(right).controls.Get(Control::DpadRight));
    right.hand[0].stick = {0.2f, 0.0f};
    CHECK_FALSE(r.Step(right).controls.Get(Control::DpadRight));
    r.Hold(click_l, 3, Control::View);
    r.Step(IdleFrame());
    CHECK_FALSE(r.pc.dpad_mode());
    out = r.Step(up);
    CHECK_FALSE(out.controls.Get(Control::DpadUp));
    CHECK(out.controls.GetStick(AnalogControl::StickL).y > 0.5f); // the Circle Pad again

    // Both clicks: Menu's job. A tap is Start (no Select, no mode switch); held, the VR menu.
    InputFrame both = IdleFrame();
    both.hand[0].stick_click = both.hand[1].stick_click = true;
    CHECK_FALSE(r.Hold(both, 10, Control::Menu));
    bool select = false;
    bool start = false;
    for (int i = 0; i < 10; i++) {
        out = r.Step(IdleFrame());
        select = select || out.controls.Get(Control::View);
        start = start || out.controls.Get(Control::Menu);
    }
    CHECK(start);
    CHECK_FALSE(select);
    CHECK_FALSE(r.pc.dpad_mode());
    bool toggled = false;
    for (int i = 0; i < kSecond + 5; i++) {
        toggled = toggled || r.Step(both).commands.menu_toggle;
    }
    CHECK(toggled);
    // One click let go first, then the other: still no Select.
    InputFrame left_only = IdleFrame();
    left_only.hand[0].stick_click = true;
    select = r.Hold(left_only, 5, Control::View) || r.Hold(IdleFrame(), 10, Control::View);
    CHECK_FALSE(select);
    CHECK_FALSE(r.pc.dpad_mode());
}

TEST_CASE("AzaharVR Index: trackpads are the D-pad and the Menu role; grips from force",
          "[frame_xr][pcvr]") {
    Runner r(ControllerScheme::Index);
    // The left trackpad: pressed, by zone; touched only, nothing; the middle, nothing.
    InputFrame f = IdleFrame();
    f.hand[0].trackpad = {0.0f, 0.8f};
    CHECK_FALSE(AnyButton(r.Step(f)));
    f.hand[0].trackpad_force = 0.8f;
    MapperOutput out = r.Step(f);
    CHECK(out.controls.Get(Control::DpadUp));
    f.hand[0].trackpad = {-0.8f, 0.1f};
    CHECK(r.Step(f).controls.Get(Control::DpadLeft));
    f.hand[0].trackpad = {0.8f, 0.1f};
    CHECK(r.Step(f).controls.Get(Control::DpadRight));
    f.hand[0].trackpad = {0.1f, -0.8f};
    CHECK(r.Step(f).controls.Get(Control::DpadDown));
    f.hand[0].trackpad = {0.1f, 0.1f};
    CHECK_FALSE(AnyButton(r.Step(f)));
    r.Step(IdleFrame());
    // The grip (squeeze/force is bound to it): L and R.
    f = IdleFrame();
    f.hand[1].grip = 0.7f;
    CHECK(r.Step(f).controls.Get(Control::BumperR));
    r.Step(IdleFrame());
    // The right trackpad pressed: a tap is Start, held 1 s the VR menu.
    f = IdleFrame();
    f.hand[1].trackpad_force = 0.9f;
    CHECK_FALSE(r.Hold(f, 5, Control::Menu));
    CHECK(r.Hold(IdleFrame(), 5, Control::Menu));
    bool toggled = false;
    for (int i = 0; i < kSecond + 5; i++) {
        toggled = toggled || r.Step(f).commands.menu_toggle;
    }
    CHECK(toggled);
    r.Hold(IdleFrame(), 20, Control::Menu);
    // ... and its chords, as Touch's Menu.
    r.Step(f);
    InputFrame chord = f;
    chord.hand[1].grip = 1.0f;
    CHECK_FALSE(r.Hold(chord, 5, Control::BumperR));
    CHECK(r.Step(f).commands.quick_save);
    // No both-clicks Menu, no D-pad mode: the right click is Select, the left one is free.
    r.Hold(IdleFrame(), 20, Control::Menu);
    InputFrame both = IdleFrame();
    both.hand[0].stick_click = both.hand[1].stick_click = true;
    r.Hold(both, 5, Control::Menu);
    CHECK(r.Hold(IdleFrame(), 5, Control::View));
    CHECK_FALSE(r.pc.dpad_mode());
    r.Hold(IdleFrame(), 20, Control::View);
    // The left click alone: Hide or Show Secondary Screen, as the Frame's left tap; no Select.
    InputFrame click_l = IdleFrame();
    click_l.hand[0].stick_click = true;
    bool other = false;
    bool select = false;
    for (int i = 0; i < 3; i++) {
        const MapperOutput o = r.Step(click_l);
        other = other || o.commands.toggle_other_screen;
    }
    CHECK_FALSE(other); // sent when let go
    for (int i = 0; i < 10; i++) {
        const MapperOutput o = r.Step(IdleFrame());
        other = other || o.commands.toggle_other_screen;
        select = select || o.controls.Get(Control::View);
    }
    CHECK(other);
    CHECK_FALSE(select);
    CHECK_FALSE(r.pc.dpad_mode());
}

// AzaharVR: Edit Controls with a PC controller listens to what the game gets (an XR Elite's
// presses did not register: it has no bumpers or D-pad, only grips and sticks).
TEST_CASE("AzaharVR Focus 3: Edit Controls hears the grips as L and R and the stick as the D-pad",
          "[frame_xr][pcvr][pclisten]") {
    using S = ControllerScheme;
    InputFrame prev{};
    InputFrame in{};
    in.menu = true;
    in.view = true;
    in.hand[0].grip = 0.7f;
    InputFrame f = PcScheme::ListenFrame(S::Focus3, in, prev);
    CHECK(f.hand[0].bumper);
    CHECK_FALSE(f.hand[1].bumper);
    CHECK_FALSE(f.menu); // Start and Select are the scheme's own
    CHECK_FALSE(f.view);
    // hysteresis: held at 0.5 once down, let go below 0.40; not down from 0.5
    in.hand[0].grip = 0.5f;
    CHECK(PcScheme::ListenFrame(S::Focus3, in, f).hand[0].bumper);
    CHECK_FALSE(PcScheme::ListenFrame(S::Focus3, in, prev).hand[0].bumper);
    in.hand[0].grip = 0.3f;
    CHECK_FALSE(PcScheme::ListenFrame(S::Focus3, in, f).hand[0].bumper);

    InputFrame st{};
    st.hand[0].stick = {0.0f, 0.8f};
    f = PcScheme::ListenFrame(S::Focus3, st, prev);
    CHECK(f.dpad_up);
    CHECK_FALSE(f.dpad_down);
    st.hand[0].stick = {-0.8f, 0.1f};
    CHECK(PcScheme::ListenFrame(S::Touch, st, prev).dpad_left);
    st.hand[0].stick = {0.2f, 0.1f};
    CHECK_FALSE(PcScheme::ListenFrame(S::Focus3, st, prev).dpad_up);
    // Index's D-pad is its left trackpad (Normalize), not the stick
    st.hand[0].stick = {0.0f, 0.9f};
    CHECK_FALSE(PcScheme::ListenFrame(S::Index, st, prev).dpad_up);
}

TEST_CASE("AzaharVR schemes: Edit Controls on the Frame listens as before",
          "[frame_xr][pcvr][pclisten]") {
    InputFrame in{};
    in.menu = true;
    in.view = true;
    in.hand[0].grip = 1.0f;
    in.hand[0].stick = {0.0f, 1.0f};
    const InputFrame f = PcScheme::ListenFrame(ControllerScheme::Frame, in, {});
    CHECK(f.menu);
    CHECK(f.view);
    CHECK_FALSE(f.hand[0].bumper); // the Frame's grips grab the screens
    CHECK_FALSE(f.dpad_up);
}

// AzaharVR: Move Screens (PC schemes): the gestures' frame (MoveScreensInput) and what the paused
// game gets (ScreensOnly).
TEST_CASE("AzaharVR Move Screens: what is held at the start waits; a new B or Menu ends it",
          "[frame_xr][pcvr][pcgrab]") {
    MoveScreensInput mode;
    mode.Reset();
    InputFrame in{};
    in.b = true;               // the press that ended the menu, still held
    in.hand[1].grip = 1.0f;    // and a grip held
    InputFrame f = in;
    CHECK_FALSE(mode.Filter(f, -1));
    CHECK(f.hand[1].grip == 0.0f); // swallowed until let go
    in = {};
    f = in;
    CHECK_FALSE(mode.Filter(f, -1)); // let go
    in.hand[1].grip = 1.0f;
    f = in;
    CHECK_FALSE(mode.Filter(f, -1));
    CHECK(f.hand[1].grip == 1.0f); // a new grip grabs
    in.b = true;
    f = in;
    CHECK(mode.Filter(f, -1)); // a new B finishes
    mode.Reset();
    InputFrame menu{};
    menu.menu = true;
    f = menu;
    CHECK_FALSE(mode.Filter(f, -1)); // Menu held from the start
    f = {};
    mode.Filter(f, -1);
    f = menu;
    CHECK(mode.Filter(f, -1)); // a new Menu finishes
}

TEST_CASE("AzaharVR Move Screens: only poses, grips and sticks; the other stick is the D-pad",
          "[frame_xr][pcvr][pcgrab]") {
    MoveScreensInput mode;
    mode.Reset();
    InputFrame none{};
    mode.Filter(none, -1); // nothing held now
    InputFrame in{};
    in.a = in.x = in.y = true;
    in.hand[0].trigger = 1.0f;
    in.hand[0].stick_click = true;
    in.hand[1].grip = 1.0f;           // the right grip holds
    in.hand[1].stick = {0.9f, 0.0f};  // its stick: bigger (the Mapper's)
    in.hand[0].stick = {0.0f, 0.8f};  // the other stick: D-pad up
    InputFrame f = in;
    CHECK_FALSE(mode.Filter(f, 1));
    CHECK_FALSE(f.a);
    CHECK_FALSE(f.x);
    CHECK_FALSE(f.y);
    CHECK(f.hand[0].trigger == 0.0f);
    CHECK_FALSE(f.hand[0].stick_click);
    CHECK(f.hand[1].grip == 1.0f);
    CHECK(f.hand[1].stick.x == 0.9f); // the holding hand keeps its stick
    CHECK(mode.dpad_hand() == 0);
    CHECK(f.dpad_up);
    CHECK(f.hand[0].stick.x == 0.0f); // the D-pad stick reaches nothing else
    CHECK(f.hand[0].stick.y == 0.0f);
    // held between press and release: still up at 0.4, gone at 0.3
    in.hand[0].stick = {0.0f, 0.4f};
    f = in;
    mode.Filter(f, 1);
    CHECK(f.dpad_up);
    in.hand[0].stick = {0.0f, 0.3f};
    f = in;
    mode.Filter(f, 1);
    CHECK_FALSE(f.dpad_up);
    // both grips down, the left holding: the right stick is the D-pad (Horizontal Scale: right)
    in.hand[0].grip = 1.0f;
    in.hand[1].stick = {0.8f, 0.0f};
    f = in;
    mode.Filter(f, 0);
    CHECK(mode.dpad_hand() == 1);
    CHECK(f.dpad_right);
    CHECK(f.hand[0].grip == 1.0f);
    CHECK(f.hand[1].grip == 1.0f);
    // no grip: no D-pad from a stick, a real D-pad (Index's trackpad) still is one
    InputFrame free{};
    free.hand[0].stick = {0.0f, 0.9f};
    free.dpad_left = true;
    f = free;
    mode.Filter(f, -1);
    CHECK(mode.dpad_hand() == -1);
    CHECK_FALSE(f.dpad_up);
    CHECK(f.dpad_left);
}

TEST_CASE("AzaharVR Move Screens: the paused game gets the screens' moves only",
          "[frame_xr][pcvr][pcgrab]") {
    MapperOutput out;
    out.controls.Set(Control::A, true);
    out.commands.top_drag_active = true;
    out.commands.drag_hand = 1;
    out.commands.top_depth_delta_z = 0.25f;
    out.commands.top_scale_stick = 0.5f;
    out.commands.quick_save = true;
    out.commands.menu_toggle = true;
    out.touch_event_count = 1;
    const MapperOutput only = ScreensOnly(out);
    CHECK_FALSE(only.controls.Get(Control::A));
    CHECK(only.touch_event_count == 0);
    CHECK(only.commands.top_drag_active);
    CHECK(only.commands.drag_hand == 1);
    CHECK(only.commands.top_depth_delta_z == 0.25f);
    CHECK(only.commands.top_scale_stick == 0.5f);
    CHECK_FALSE(only.commands.quick_save);
    CHECK_FALSE(only.commands.menu_toggle);
}
