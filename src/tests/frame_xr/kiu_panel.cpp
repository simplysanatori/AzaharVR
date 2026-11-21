// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiupanel: Angel's Aim's wrist screen holds the aim while the beam is on it
// (vis/wrist_screen.h WristAimHold), a touch of it ends when the wrist turns away
// (input/mapper.cpp), and the talk panel is for reading only
// (immersive.h ScreenLayers::bottom_pointable; the switch stays off by default).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "frame_xr/input/gyro_aim.h"
#include "frame_xr/input/mapper.h"
#include "frame_xr/kiu_first_person.h"
#include "frame_xr/vis/wrist_screen.h"

using Catch::Approx;
using namespace FrameXr;
using Input::Hand;
using Input::Posef;
using Input::Quatf;
using Input::Vec3f;

namespace {

constexpr Vec3f kEye{0.0f, 0.0f, 0.0f};

Posef LeftWristUp(const Vec3f& centre) {
    const Vec3f z = Input::Normalized(kEye - centre);
    const Vec3f x = Input::Normalized(Input::Cross({0.0f, 1.0f, 0.0f}, z));
    const Vec3f y = Input::Cross(z, x);
    const Quatf panel = Input::Normalized(Input::FromBasis(x, y, z));
    const Posef local = Vis::WristScreenLocalPose(Hand::Left, false);
    const Quatf q = Input::Normalized(Input::Multiply(panel, Input::Conjugate(local.orientation)));
    return {q, centre - Input::Rotate(q, local.position)};
}

} // namespace

TEST_CASE("kiupanel: the hold switch", "[kiupanel]") {
    CHECK(Vis::WristHoldSwitchOn(std::nullopt)); // on by default
    CHECK(Vis::WristHoldSwitchOn(std::string{"1"}));
    CHECK(Vis::WristHoldSwitchOn(std::string{"on"}));
    CHECK_FALSE(Vis::WristHoldSwitchOn(std::string{"0"}));
    CHECK_FALSE(Vis::WristHoldSwitchOn(std::string{"Off"}));
    CHECK_FALSE(Vis::WristHoldSwitchOn(std::string{"false"}));
}

TEST_CASE("kiupanel: the aim is held on the wrist screen and a short while after",
          "[kiupanel]") {
    Vis::WristAimHold h;
    constexpr int64_t kFrame = 8'333'333;
    int64_t t = 1'000'000'000;
    Vis::WristAimHold::Out o = h.Update(false, t);
    CHECK_FALSE(o.hold);
    CHECK_FALSE(o.changed);
    o = h.Update(true, t += kFrame);
    CHECK(o.hold);
    CHECK(o.changed);
    o = h.Update(true, t += kFrame);
    CHECK(o.hold);
    CHECK_FALSE(o.changed);
    // Off the screen: held through the grace, then free.
    const int64_t left = t;
    while (t + kFrame - left <= Vis::WristAimHold::kGraceNs) {
        o = h.Update(false, t += kFrame);
        REQUIRE(o.hold);
    }
    o = h.Update(false, t += kFrame);
    CHECK_FALSE(o.hold);
    CHECK(o.changed);
    // Back on within the grace: the grace starts again from there.
    h.Update(true, t += kFrame);
    h.Update(false, t += 100'000'000);
    CHECK(h.Update(true, t += kFrame).hold);
    CHECK(h.Update(false, t += Vis::WristAimHold::kGraceNs).hold);
    CHECK_FALSE(h.Update(false, t += kFrame).hold);
}

TEST_CASE("kiupanel: the beam on the wrist screen is what the touch takes", "[kiupanel]") {
    const Posef left_aim = LeftWristUp({-0.06f, -0.28f, -0.30f});
    Input::PanelGeometry panels;
    panels.top.visible = false;
    panels.bottom = {Vis::WristScreenPose(left_aim, Hand::Left, false), Vis::kWristScreenWidth,
                     Vis::kWristScreenHeight, true};
    panels.bottom_fixed = true;
    const Vec3f hand_at{0.15f, -0.15f, -0.15f};
    const auto ray_to = [&](Vec3f target) {
        return Input::RayFromPose({Input::FaceViewer(target, hand_at, {0.0f, 1.0f, 0.0f}), hand_at});
    };
    CHECK(Input::PanelUnderRay(ray_to(Input::PointOnQuad(panels.bottom, 0.5f, 0.5f)), panels) ==
          Input::Panel::Bottom);
    CHECK(Input::PanelUnderRay(ray_to(Input::PointOnQuad(panels.bottom, 0.02f, 0.97f)), panels) ==
          Input::Panel::Bottom);
    CHECK(Input::PanelUnderRay(ray_to(Input::PointOnQuad(panels.bottom, 1.2f, 0.5f)), panels) ==
          Input::Panel::None);
    CHECK(Input::PanelUnderRay(ray_to({0.0f, 0.0f, -2.0f}), panels) == Input::Panel::None);
    panels.bottom.visible = false; // hidden: nothing
    CHECK(Input::PanelUnderRay(ray_to(Input::PointOnQuad(panels.bottom, 0.5f, 0.5f)), panels) ==
          Input::Panel::None);
}

TEST_CASE("kiupanel: a touch of the wrist screen ends when the wrist turns away",
          "[kiupanel]") {
    const Posef left_aim = LeftWristUp({-0.06f, -0.28f, -0.30f});
    Input::PanelGeometry panels;
    panels.top.visible = false;
    panels.bottom = {Vis::WristScreenPose(left_aim, Hand::Left, false), Vis::kWristScreenWidth,
                     Vis::kWristScreenHeight, true};
    panels.bottom_fixed = true;
    Input::Mapper mapper;
    int64_t now = 0;
    const Vec3f hand_at{0.15f, -0.15f, -0.15f};
    const auto frame = [&](Vec3f target, float trigger) {
        Input::InputFrame f;
        f.time_ns = now;
        now += 8'333'333;
        f.hand[0].active = true;
        f.hand[0].aim = left_aim;
        f.hand[1].active = true;
        f.hand[1].aim = {Input::FaceViewer(target, hand_at, {0.0f, 1.0f, 0.0f}), hand_at};
        f.hand[1].trigger = trigger;
        return f;
    };
    const Vec3f p = Input::PointOnQuad(panels.bottom, 0.5f, 0.5f);
    mapper.Update(frame(p, 0.0f), panels);
    const Input::MapperOutput& pressed = mapper.Update(frame(p, 1.0f), panels);
    REQUIRE(pressed.touch_event_count == 1);
    REQUIRE(pressed.touch_events[0].type == Input::TouchEventType::Pressed);
    // The wrist turns away with the trigger held: the bottom screen is not the wrist's now (with
    // the talk panel it is a panel for reading only: not a target either).
    panels.bottom_fixed = false;
    panels.bottom.visible = false;
    const Input::MapperOutput& gone = mapper.Update(frame(p, 1.0f), panels);
    REQUIRE(gone.touch_event_count == 1);
    CHECK(gone.touch_events[0].type == Input::TouchEventType::Released);
    CHECK(gone.touch_events[0].x == Approx(160.0f).margin(0.5));
    CHECK(gone.touch_events[0].y == Approx(120.0f).margin(0.5));
    CHECK_FALSE(gone.controls.Get(Input::Control::TriggerR));
    // Still held: no drag, no shot.
    for (int i = 0; i < 30; i++) {
        const Input::MapperOutput& held = mapper.Update(frame({0.0f, 0.0f, -2.0f}, 1.0f), panels);
        REQUIRE(held.touch_event_count == 0);
        REQUIRE_FALSE(held.controls.Get(Input::Control::TriggerR));
        REQUIRE_FALSE(held.controls.Get(Input::Control::TriggerL));
    }
    // Let go, then a new pull fires.
    REQUIRE(mapper.Update(frame({0.0f, 0.0f, -2.0f}, 0.0f), panels).touch_event_count == 0);
    CHECK(mapper.Update(frame({0.0f, 0.0f, -2.0f}, 1.0f), panels)
              .controls.Get(Input::Control::TriggerR));
    mapper.Update(frame({0.0f, 0.0f, -2.0f}, 0.0f), panels);

    // Not the wrist screen (a bottom screen hidden in another mode): its touch is as before.
    panels.bottom.visible = true;
    mapper.Update(frame(p, 0.0f), panels);
    REQUIRE(mapper.Update(frame(p, 1.0f), panels).touch_event_count == 1);
    panels.bottom.visible = false;
    CHECK(mapper.Update(frame(p, 1.0f), panels).touch_event_count == 0); // no release
    panels.bottom.visible = true;
    const Input::MapperOutput& up = mapper.Update(frame(p, 0.0f), panels);
    REQUIRE(up.touch_event_count == 1);
    CHECK(up.touch_events[0].type == Input::TouchEventType::Released);
}

TEST_CASE("kiupanel: the talk panel stays off by default", "[kiupanel]") {
    namespace FP = Kiu::FirstPerson;
    CHECK_FALSE(FP::ParseTalk(nullptr, nullptr).on);
    CHECK_FALSE(FP::ParseTalk("0", nullptr).on);
    CHECK(FP::ParseTalk("1", nullptr).on);
}

TEST_CASE("kiupanel: a ray straight ahead from a low hand crosses the talk panel",
          "[kiupanel]") {
    // Why it is for reading only: the panel (0.8 m, 24 degrees down, 34 degrees wide, measured on
    // the mock at exactly these) is in the way of a hand held low and aimed straight ahead.
    namespace FP = Kiu::FirstPerson;
    const FP::TalkPlace p = FP::TalkPanelInView(FP::ParseTalk("1", nullptr), 320.0f / 240.0f);
    Input::PanelQuad talk;
    talk.pose = {Input::Normalized(Quatf{std::sin(0.5f * p.pitch_rad), 0.0f, 0.0f,
                                         std::cos(0.5f * p.pitch_rad)}),
                 {0.0f, p.y, p.z}};
    talk.width_m = p.width;
    talk.height_m = p.height;
    talk.visible = true;
    Input::PanelGeometry panels;
    panels.top.visible = false;
    panels.bottom = talk;
    const Input::Ray ahead = Input::RayFromPose({{0.0f, 0.0f, 0.0f, 1.0f}, {0.08f, -0.35f, -0.30f}});
    CHECK(Input::PanelUnderRay(ahead, panels) == Input::Panel::Bottom);
    // Not pointable (ScreenLayers::bottom_pointable false: the slot is not visible to the beam):
    // the same pull is ZR.
    panels.bottom.visible = false;
    CHECK(Input::PanelUnderRay(ahead, panels) == Input::Panel::None);
    Input::Mapper mapper;
    Input::InputFrame f;
    f.hand[1].active = true;
    f.hand[1].aim = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.08f, -0.35f, -0.30f}};
    mapper.Update(f, panels);
    f.time_ns += 8'333'333;
    f.hand[1].trigger = 1.0f;
    const Input::MapperOutput& out = mapper.Update(f, panels);
    CHECK(out.touch_event_count == 0);
    CHECK(out.controls.Get(Input::Control::TriggerR));
}
