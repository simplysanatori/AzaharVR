// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuview: first person's talk panel, HUD, field, soft edge and third-person
// switches (kiu_first_person.h) and the field's share of the cover (immersive_layout.h).

#include <cmath>
#include <utility>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "frame_xr/immersive_layout.h"
#include "frame_xr/input/kiu_hooks.h"
#include "frame_xr/kiu_first_person.h"
#include "frame_xr/input/geometry.h"
#include "common/settings.h"

namespace FP = FrameXr::Kiu::FirstPerson;
namespace L = FrameXr::Immersive::Layout;
using Catch::Matchers::WithinAbs;

TEST_CASE("kiuview: the talk panel's switches and place", "[kiuview]") {
    const FP::Talk d0 = FP::ParseTalk(nullptr, nullptr);
    CHECK_FALSE(d0.on); // off until the headset check
    const FP::Talk d = FP::ParseTalk("1", nullptr);
    CHECK(d.on);
    CHECK(d.down_deg == FP::kTalkDownDeg);
    CHECK(d.width_deg == FP::kTalkWidthDeg);
    CHECK_FALSE(FP::ParseTalk("0", nullptr).on);
    CHECK(FP::ParseTalk("1", nullptr).on);
    const FP::Talk at = FP::ParseTalk(nullptr, "30,40");
    CHECK(at.down_deg == 30.0f);
    CHECK(at.width_deg == 40.0f);
    const FP::Talk clamped = FP::ParseTalk(nullptr, "90,5");
    CHECK(clamped.down_deg == 60.0f);
    CHECK(clamped.width_deg == 10.0f);
    CHECK(FP::ParseTalk(nullptr, "30").down_deg == FP::kTalkDownDeg); // both or nothing

    // The middle down_deg below straight ahead at the distance, facing the eye, width_deg wide.
    const FP::TalkPlace p = FP::TalkPanelInView(d, 320.0f / 240.0f);
    const float deg = 3.14159265f / 180.0f;
    CHECK_THAT(std::atan2(-p.y, -p.z) / deg, WithinAbs(FP::kTalkDownDeg, 1e-3));
    CHECK_THAT(std::hypot(p.y, p.z), WithinAbs(FP::kTalkDistanceM, 1e-5));
    CHECK_THAT(p.pitch_rad / deg, WithinAbs(-FP::kTalkDownDeg, 1e-3));
    CHECK_THAT(2.0f * std::atan(0.5f * p.width / FP::kTalkDistanceM) / deg,
               WithinAbs(FP::kTalkWidthDeg, 1e-3));
    CHECK_THAT(p.width / p.height, WithinAbs(320.0 / 240.0, 1e-5));
    // Turned by its pitch, the panel's normal (+z) points back at the eye.
    const FrameXr::Input::KiuHooks::Quat q{std::sin(0.5f * p.pitch_rad), 0.0f, 0.0f,
                                            std::cos(0.5f * p.pitch_rad)};
    const auto n = FrameXr::Input::KiuHooks::Rotate(q, {0.0f, 0.0f, 1.0f});
    CHECK_THAT(n.y * p.y + n.z * p.z, WithinAbs(-FP::kTalkDistanceM, 1e-4)); // n . middle
}

TEST_CASE("kiuview: the field, the soft edge and the shoulder", "[kiuview]") {
    CHECK(FP::ParseFieldShare(nullptr) == 1.0f);
    CHECK(FP::ParseFieldShare("x") == 1.0f);
    CHECK(FP::ParseFieldShare("80") == 0.8f);
    CHECK(FP::ParseFieldShare("20") == 0.5f);
    CHECK(FP::ParseFieldShare("120") == 1.0f);
    CHECK(FP::ParseEdgeShare(nullptr) == 0.0f);
    CHECK(FP::ParseEdgeShare("15") == 0.15f);
    CHECK(FP::ParseEdgeShare("90") == 0.5f);
    CHECK(FP::ParseEdgeShare("-4") == 0.0f);

    float b = 9.0f, u = 9.0f, s = 9.0f;
    CHECK_FALSE(FP::ParseShoulder(nullptr, b, u, s));
    CHECK_FALSE(FP::ParseShoulder("1.5", b, u, s));
    CHECK_FALSE(FP::ParseShoulder("0,0,0", b, u, s));
    REQUIRE(FP::ParseShoulder("1.5,0.4", b, u, s));
    CHECK((b == 1.5f && u == 0.4f && s == 0.0f));
    REQUIRE(FP::ParseShoulder("2,0.5,-0.3", b, u, s));
    CHECK(s == -0.3f);
    REQUIRE(FP::ParseShoulder("50,0,0", b, u, s));
    CHECK(b == FP::kMaxShoulderM);
    // Back is -z of the level view's axes, up +y; right is -x there (+z ahead, +y up: +x left).
    const auto o = FP::ShoulderOffset(2.0f, 0.5f, 0.3f, 10.0f);
    CHECK((o.x == -3.0f && o.y == 5.0f && o.z == -20.0f));
    const auto axes = FrameXr::Input::KiuHooks::LevelYawAxes({1.0f, 0.0f, 0.0f}); // looking +x
    const auto back = FrameXr::Input::KiuHooks::Rotate(axes, FP::ShoulderOffset(1.0f, 0.0f, 0.0f, 1.0f));
    CHECK_THAT(back.x, WithinAbs(-1.0, 1e-5)); // behind a view along +x is -x
    CHECK_THAT(back.z, WithinAbs(0.0, 1e-5));
}

// AzaharFrame kiutoggles: Talk Panel's and Third Person's settings when their switches are
// not set; a switch, when set, wins either way.
TEST_CASE("kiutoggles: the talk panel and the shoulder, switch or setting", "[kiuview][kiutoggles]") {
    CHECK_FALSE(FP::TalkOnFor(nullptr, false));
    CHECK(FP::TalkOnFor(nullptr, true));
    CHECK(FP::TalkOnFor("1", false));
    CHECK_FALSE(FP::TalkOnFor("0", true));

    float b = 9.0f, u = 9.0f, s = 9.0f;
    CHECK_FALSE(FP::ShoulderFor(nullptr, false, b, u, s));
    REQUIRE(FP::ShoulderFor(nullptr, true, b, u, s));
    CHECK((b == FP::kShoulderSettingBackM && u == FP::kShoulderSettingUpM && s == 0.0f));
    CHECK(FP::kShoulderSettingBackM == 2.5f); // Pit solid at 2.3 m back or more (kiufp3)
    CHECK(FP::kShoulderSettingUpM == 0.5f);
    REQUIRE(FP::ShoulderFor("1.5,0.4", false, b, u, s)); // the switch's own place
    CHECK((b == 1.5f && u == 0.4f && s == 0.0f));
    REQUIRE(FP::ShoulderFor("2,0.6,0.2", true, b, u, s)); // the switch wins over the setting
    CHECK((b == 2.0f && u == 0.6f && s == 0.2f));
    CHECK_FALSE(FP::ShoulderFor("0", true, b, u, s)); // set to 0: off, whatever the setting
    CHECK_FALSE(FP::ShoulderFor("", true, b, u, s));
}

TEST_CASE("kiuview: the field's share keeps the wide camera at factor 1", "[kiuview]") {
    L::ViewFov view;
    view.left = view.outer_left = -59.1f * L::kDegToRad;
    view.right = view.outer_right = 59.1f * L::kDegToRad;
    view.up = view.outer_up = 49.5f * L::kDegToRad;
    view.down = view.outer_down = -60.0f * L::kDegToRad;
    const L::Cover c{2.0f, 1.5f};
    const L::Cover same = L::ShareCover(c, 1.0f);
    CHECK((same.tan_x == c.tan_x && same.tan_y == c.tan_y));
    CHECK(L::ShareCover(c, 0.2f).tan_x == c.tan_x); // out of range: unchanged
    const L::Cover half = L::ShareCover(c, 0.8f);
    CHECK_THAT(std::atan(half.tan_x), WithinAbs(0.8 * std::atan(2.0), 1e-5));
    CHECK_THAT(std::atan(half.tan_y), WithinAbs(0.8 * std::atan(1.5), 1e-5));
    for (const float field : {1.0f, 0.85f, 0.7f}) {
        const L::KiuWide w = L::KiuWideCamera(view, 0.0f, 0.0f, field);
        REQUIRE(w.ok);
        const L::HeadLocked s = L::SolveKiuFirstPerson(w.fov_y_deg, w.aspect, view, 0.0f,
                                                       L::kKiuFpFactorMax, 1.0f, field);
        CHECK_THAT(s.factor, WithinAbs(1.0, 2e-3));
        CHECK_THAT(s.factor_y, WithinAbs(1.0, 2e-3));
        CHECK(s.gain == 1.0f);
    }
    const L::KiuWide full = L::KiuWideCamera(view, 0.0f, 0.0f, 1.0f);
    const L::KiuWide less = L::KiuWideCamera(view, 0.0f, 0.0f, 0.8f);
    CHECK(less.fov_y_deg < full.fov_y_deg);
}

// AzaharFrame kiufp3: what the desktop logged live (126 x 112 degrees at 100, 101 x 89 at 80,
// 76 x 67 at 60): the screen's angle on each axis is the share times the whole one.
TEST_CASE("kiufp3: the field's share is the screen's angle", "[kiuview]") {
    L::ViewFov view;
    view.left = view.outer_left = -59.1f * L::kDegToRad;
    view.right = view.outer_right = 59.1f * L::kDegToRad;
    view.up = view.outer_up = 49.5f * L::kDegToRad;
    view.down = view.outer_down = -60.0f * L::kDegToRad;
    const auto angles = [&](float field) {
        const L::KiuWide w = L::KiuWideCamera(view, 0.032f, 0.0f, field);
        REQUIRE(w.ok);
        const L::HeadLocked s = L::SolveKiuFirstPerson(w.fov_y_deg, w.aspect, view, 0.032f,
                                                       L::kKiuFpFactorMax, 1.0f, field);
        return std::pair{2.0 * std::atan(s.tan_half_y * s.aspect), 2.0 * std::atan(s.tan_half_y)};
    };
    const auto [across, high] = angles(1.0f);
    for (const float field : {0.8f, 0.6f}) {
        const auto [a, h] = angles(field);
        CHECK_THAT(a, WithinAbs(field * across, 0.01)); // radians: about half a degree
        CHECK_THAT(h, WithinAbs(field * high, 0.01));
    }
    // Unset (share 1): the whole view; 50 % is the least the switch takes.
    CHECK(FP::ParseFieldShare("40") == 0.5f);
}

// AzaharFrame kiu131: Bottom Screen. The stored choice; while Off, the two settings it replaced
// (Wrist won while both were on); the test switches over it; Persistent's place.
TEST_CASE("kiu131: Bottom Screen's choice, migration, switches and place", "[kiuview][kiu131]") {
    using BS = FP::BottomScreen;
    CHECK(FP::BottomScreenModeFor(0, false, false) == BS::Off);
    CHECK(FP::BottomScreenModeFor(1, false, false) == BS::Persistent);
    CHECK(FP::BottomScreenModeFor(2, false, false) == BS::Wrist);
    CHECK(FP::BottomScreenModeFor(0, false, true) == BS::Persistent); // Talk Panel was on
    CHECK(FP::BottomScreenModeFor(0, true, false) == BS::Wrist);      // the wrist screen was on
    CHECK(FP::BottomScreenModeFor(0, true, true) == BS::Wrist);       // both: the wrist won
    CHECK(FP::BottomScreenModeFor(1, true, false) == BS::Persistent); // chosen: the old go
    CHECK(FP::BottomScreenModeFor(9, false, true) == BS::Persistent);
    CHECK(FP::BottomScreenWithSwitches(BS::Off, nullptr, nullptr) == BS::Off);
    CHECK(FP::BottomScreenWithSwitches(BS::Off, "1", nullptr) == BS::Wrist);
    CHECK(FP::BottomScreenWithSwitches(BS::Persistent, "on", "1") == BS::Wrist);
    CHECK(FP::BottomScreenWithSwitches(BS::Wrist, "0", nullptr) == BS::Off);
    CHECK(FP::BottomScreenWithSwitches(BS::Persistent, "0", nullptr) == BS::Persistent);
    CHECK(FP::BottomScreenWithSwitches(BS::Off, nullptr, "1") == BS::Persistent);
    CHECK(FP::BottomScreenWithSwitches(BS::Wrist, "0", "1") == BS::Persistent);
    CHECK(FP::BottomScreenWithSwitches(BS::Persistent, nullptr, "0") == BS::Off);
    CHECK(FP::BottomScreenWithSwitches(BS::Wrist, nullptr, "0") == BS::Wrist);
    CHECK(FP::BottomScaleFor(100) == 1.0f);
    CHECK(FP::BottomScaleFor(10) == 0.5f);
    CHECK(FP::BottomScaleFor(500) == 2.0f);
    CHECK(FP::BottomAlphaFor(0) == 0.2f);
    CHECK(FP::BottomAlphaFor(100) == 1.0f);
    // The default place: the talk panel's (24 degrees down, 34 wide, 0.8 m).
    const FP::PanelInView p = FP::PersistentInView(0.0f, FP::kTalkDownDeg, FP::kTalkWidthDeg, 4.0f / 3.0f);
    const FP::TalkPlace t = FP::TalkPanelInView(FP::Talk{true}, 4.0f / 3.0f);
    CHECK_THAT(p.y, WithinAbs(t.y, 1e-5));
    CHECK_THAT(p.z, WithinAbs(t.z, 1e-5));
    CHECK_THAT(p.x, WithinAbs(0.0, 1e-6));
    CHECK_THAT(p.width, WithinAbs(t.width, 1e-5));
    CHECK_THAT(p.qx, WithinAbs(std::sin(0.5f * t.pitch_rad), 1e-5));
    // Moved 20 degrees right and 10 more down: still 0.8 m away, facing the eye.
    const FP::PanelInView m = FP::PersistentInView(20.0f, 34.0f, 34.0f, 4.0f / 3.0f);
    CHECK_THAT(std::sqrt(m.x * m.x + m.y * m.y + m.z * m.z), WithinAbs(0.8, 1e-5));
    CHECK(m.x > 0.0f);
    CHECK_THAT(std::atan2(m.x, -m.z) * 57.29578f, WithinAbs(20.0, 1e-3));
    CHECK_THAT(-std::asin(m.y / 0.8f) * 57.29578f, WithinAbs(34.0, 1e-3));
    const FrameXr::Input::Quatf q{m.qx, m.qy, m.qz, m.qw};
    const auto front = FrameXr::Input::Rotate(q, {0.0f, 0.0f, 1.0f}); // toward the eye
    CHECK_THAT(front.x, WithinAbs(-m.x / 0.8f, 1e-4));
    CHECK_THAT(front.y, WithinAbs(-m.y / 0.8f, 1e-4));
    CHECK_THAT(front.z, WithinAbs(-m.z / 0.8f, 1e-4));
}

// AzaharFrame kiu131: choosing Bottom Screen clears the two settings it replaced at both scopes
// (the game's own value and the global one), so neither decides again.
TEST_CASE("kiu131: the old Bottom Screen keys cleared at both scopes", "[kiuview][kiu131]") {
    auto& w = Settings::values.vr_kiu_wrist_screen;
    auto& t = Settings::values.vr_kiu_talk_panel;
    w.SetGlobal(true);
    w.SetValue(true); // global: on
    t.SetGlobal(false);
    t.SetValue(true); // the game's own: on
    CHECK(FP::ClearOldBottomKeys());
    CHECK(w.UsingGlobal());
    CHECK_FALSE(w.GetValue());
    CHECK_FALSE(t.UsingGlobal()); // still the game's own, now off
    CHECK_FALSE(t.GetValue());
    t.SetGlobal(true);
    CHECK_FALSE(t.GetValue()); // and the global one off
    CHECK_FALSE(FP::ClearOldBottomKeys()); // nothing of the game's own left
}
