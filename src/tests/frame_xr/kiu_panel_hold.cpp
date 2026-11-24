// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include <catch2/catch_test_macros.hpp>
#include "frame_xr/vis/panel_hold.h"
#include "video_core/vr_hud_spacing.h"

using namespace FrameXr;
using Input::Posef;
using Input::Ray;
using Input::Vec3f;

namespace {
constexpr float kRad = 0.01745329f;
Ray HandRay(float yaw_deg, float pitch_deg) {
    return {{-0.2f, -0.3f, -0.2f}, Vis::DirectionFrom(yaw_deg * kRad, pitch_deg * kRad)};
}
Posef Panel() {
    return {{0.0f, 0.0f, 0.0f, 1.0f}, Vis::DirectionFrom(0.0f, -24.0f * kRad) * 0.8f};
}
} // namespace

TEST_CASE("Bottom Screen hold: a closed grip alone moves and sizes nothing", "[frame_xr][kiu140]") {
    Vis::PanelHold h;
    h.Begin(Panel(), HandRay(10, -20), 0.8f, 130.0f);
    for (int i = 0; i < 90; i++) {
        const Vec3f d = h.Update(HandRay(10 + 1.0f * std::sin(i * 0.3f), -20), 0.1f, -0.2f, 1 / 90.f);
        REQUIRE(Input::Dot(d, Input::Normalized(Panel().position)) > 0.99999f);
    }
    REQUIRE_FALSE(h.moved());
    REQUIRE_FALSE(h.sized());
    REQUIRE(h.SizePercent() == 130);
}

TEST_CASE("Bottom Screen hold: it follows the beam with no jump", "[frame_xr][kiu140]") {
    Vis::PanelHold h;
    h.Begin(Panel(), HandRay(0, 0), 0.8f, 100.0f);
    Vec3f last = Input::Normalized(Panel().position);
    for (int i = 1; i <= 200; i++) {
        const Vec3f d = h.Update(HandRay(-0.2f * i, 0.1f * i), 0, 0, 1 / 90.f);
        // A step of the hand is a step of the panel, never a jump.
        REQUIRE(std::acos(std::min(1.0f, Input::Dot(last, d))) < 1.5f * kRad);
        last = d;
    }
    REQUIRE(h.moved());
    REQUIRE(Vis::YawOf(last) < -25.0f * kRad); // to the right, as the hand
    REQUIRE(Vis::PitchOf(last) > -15.0f * kRad); // and up
}

TEST_CASE("Bottom Screen hold: the stick sizes it within the setting's range", "[frame_xr][kiu140]") {
    Vis::PanelHold h;
    h.Begin(Panel(), HandRay(0, 0), 0.8f, 100.0f);
    for (int i = 0; i < 90; i++) {
        h.Update(HandRay(0, 0), 1.0f, 0.2f, 1 / 90.f);
    }
    REQUIRE(h.sized());
    REQUIRE_FALSE(h.moved());
    REQUIRE(h.SizePercent() > 170);
    for (int i = 0; i < 900; i++) {
        h.Update(HandRay(0, 0), 1.0f, 0.0f, 1 / 90.f);
    }
    REQUIRE(h.SizePercent() == 200);
    for (int i = 0; i < 900; i++) {
        h.Update(HandRay(0, 0), 0.0f, -1.0f, 1 / 90.f); // up and down sizes it too
    }
    REQUIRE(h.SizePercent() == 50);
}

TEST_CASE("Bottom Screen: only its own viewport is kept as drawn", "[frame_xr][kiu140]") {
    using namespace VideoCore::HudSpacing;
    REQUIRE(IsBottomViewport(240.0f, 320.0f));
    REQUIRE_FALSE(IsBottomViewport(240.0f, 400.0f));
    REQUIRE_FALSE(IsBottomViewport(256.0f, 256.0f));
}
