// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuview: first person's talk panel, HUD, field, soft edge and over-the-shoulder
// switches (kiu_first_person.h) and the field's share of the cover (immersive_layout.h).

#include <cmath>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "frame_xr/immersive_layout.h"
#include "frame_xr/input/kiu_hooks.h"
#include "frame_xr/kiu_first_person.h"

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

TEST_CASE("kiuview: first person's HUD Spacing", "[kiuview]") {
    CHECK(FP::ParseFpHud(nullptr) == FP::HudPercent{});
    CHECK(FP::ParseFpHud("0") == FP::HudPercent{});
    CHECK(FP::ParseFpHud("junk") == FP::HudPercent{});
    CHECK(FP::ParseFpHud("on") == FP::HudPercent{FP::kFpHudAcross, FP::kFpHudUpDown});
    CHECK(FP::ParseFpHud("60") == FP::HudPercent{60, 60});
    CHECK(FP::ParseFpHud("45,40") == FP::HudPercent{45, 40});
    CHECK(FP::ParseFpHud("500,-3") == FP::HudPercent{150, 1});
    const FP::HudPercent fp{45, 40};
    // Off: the setting's own; on: first person's only while the setting is the game's own.
    CHECK(FP::HudSpacingFor(false, 100, 100, fp) == FP::HudPercent{100, 100});
    CHECK(FP::HudSpacingFor(true, 100, 100, fp) == fp);
    CHECK(FP::HudSpacingFor(true, 90, 100, fp) == FP::HudPercent{90, 100});
    CHECK(FP::HudSpacingFor(true, 100, 120, fp) == FP::HudPercent{100, 120});
    CHECK(FP::HudSpacingFor(true, 100, 100, FP::HudPercent{}) == FP::HudPercent{100, 100});
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
