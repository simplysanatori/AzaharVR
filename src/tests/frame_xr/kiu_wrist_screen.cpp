// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuwrist: Angel's Aim's Secondary Screen on the off hand's wrist
// (frame_xr/vis/wrist_screen.h): where it sits, when it shows, and that the aiming hand's beam
// touches it at the right 3DS pixel without pressing a trigger.

#include <cmath>
#include <utility>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "frame_xr/input/gyro_aim.h"
#include "frame_xr/input/mapper.h"
#include "frame_xr/kiu_first_person.h"
#include "frame_xr/vis/stats_place.h"
#include "frame_xr/vis/wrist_screen.h"

using Catch::Approx;
using namespace FrameXr;
using Input::Hand;
using Input::Posef;
using Input::Quatf;
using Input::Vec3f;

namespace {

constexpr Vec3f kEye{0.0f, 0.0f, 0.0f};

/// The left controller's aim pose that puts the wrist screen at `centre`, facing the eyes with
/// its top up (the wrist turned to the face to read it).
Posef LeftWristUp(const Vec3f& centre) {
    const Vec3f z = Input::Normalized(kEye - centre);
    const Vec3f x = Input::Normalized(Input::Cross({0.0f, 1.0f, 0.0f}, z));
    const Vec3f y = Input::Cross(z, x);
    const Quatf panel = Input::Normalized(Input::FromBasis(x, y, z));
    const Posef local = Vis::WristScreenLocalPose(Hand::Left, false);
    const Quatf q = Input::Normalized(Input::Multiply(panel, Input::Conjugate(local.orientation)));
    return {q, centre - Input::Rotate(q, local.position)};
}

/// The same pose mirrored across the middle of the body (x to -x): the right hand doing the same.
Posef Mirrored(const Posef& p) {
    return {{p.orientation.x, -p.orientation.y, -p.orientation.z, p.orientation.w},
            {-p.position.x, p.position.y, p.position.z}};
}

Vec3f Axis(const Posef& p, Vec3f a) {
    return Input::Rotate(p.orientation, a);
}

} // namespace

TEST_CASE("FrameXr kiuwrist: the switch and the off hand", "[frame_xr][kiuwrist]") {
    REQUIRE_FALSE(Vis::WristScreenSwitchOn(std::nullopt));
    REQUIRE_FALSE(Vis::WristScreenSwitchOn(std::string("0")));
    REQUIRE_FALSE(Vis::WristScreenSwitchOn(std::string("")));
    REQUIRE(Vis::WristScreenSwitchOn(std::string("1")));
    REQUIRE(Vis::WristScreenSwitchOn(std::string("On")));
    // AzaharFrame kiutoggles: the setting (Secondary Screen on Wrist) when the switch is not set;
    // the switch, when set, wins either way.
    REQUIRE_FALSE(Vis::WristScreenOn(std::nullopt, false));
    REQUIRE(Vis::WristScreenOn(std::nullopt, true));
    REQUIRE(Vis::WristScreenOn(std::string("1"), false));
    REQUIRE_FALSE(Vis::WristScreenOn(std::string("0"), true));
    REQUIRE_FALSE(Vis::WristScreenOn(std::string(""), true));
    // Motion Controls: 0 off, 1 right, 2 left, 3 headset. The aiming hand is the left one only
    // with Left Controller; the screen goes on the other.
    REQUIRE(Vis::WristScreenHand(0) == Hand::Left);
    REQUIRE(Vis::WristScreenHand(1) == Hand::Left);
    REQUIRE(Vis::WristScreenHand(2) == Hand::Right);
    REQUIRE(Vis::WristScreenHand(3) == Hand::Left);
    // The size: the Secondary Screen's 4:3, 10 cm wide.
    REQUIRE(Vis::kWristScreenWidth / Vis::kWristScreenHeight == Approx(320.0f / 240.0f));
    REQUIRE(Vis::kWristScreenWidth == Approx(0.10f));
}

TEST_CASE("FrameXr kiuwrist: the left wrist is where the wrist stats go", "[frame_xr][kiuwrist]") {
    const Posef stats = Vis::WristLocalPose();
    const Posef left = Vis::WristScreenLocalPose(Hand::Left, false);
    REQUIRE(Input::Length(left.position - stats.position) < 1e-6f);
    REQUIRE(std::abs(Input::Dot(Axis(left, {0, 0, 1}), Axis(stats, {0, 0, 1})) - 1.0f) < 1e-5f);
    // Beside the wrist stats: the same plane, kWristScreenBesideStats toward the elbow.
    const Posef beside = Vis::WristScreenLocalPose(Hand::Left, true);
    const Vec3f moved = beside.position - left.position;
    REQUIRE(Input::Length(moved) == Approx(Vis::kWristScreenBesideStats).margin(1e-5));
    REQUIRE(Input::Dot(moved, Axis(left, {1, 0, 0})) < -0.10f); // against the reading: the elbow
    REQUIRE(std::abs(Input::Dot(moved, Axis(left, {0, 0, 1}))) < 1e-5f);
    // Far enough that the stats (10 cm along the forearm) and the screen do not overlap.
    REQUIRE(Vis::kWristScreenBesideStats > 0.5f * 0.10f + 0.5f * Vis::kWristScreenWidth);
}

TEST_CASE("FrameXr kiuwrist: the right wrist is the left one mirrored, upright and readable",
          "[frame_xr][kiuwrist]") {
    const Posef l = Vis::WristScreenLocalPose(Hand::Left, false);
    const Posef r = Vis::WristScreenLocalPose(Hand::Right, false);
    REQUIRE(r.position.x == Approx(-l.position.x));
    REQUIRE(r.position.y == Approx(l.position.y));
    REQUIRE(r.position.z == Approx(l.position.z));
    // Its front faces the right controller's inner side (-X), a little up, as the left one's +X.
    REQUIRE(Axis(r, {0, 0, 1}).x < -0.95f);
    REQUIRE(Axis(r, {0, 0, 1}).y > 0.0f);
    // A proper rotation (no mirrored picture): x cross y is z.
    const Vec3f c = Input::Cross(Axis(r, {1, 0, 0}), Axis(r, {0, 1, 0}));
    REQUIRE(Input::Dot(c, Axis(r, {0, 0, 1})) == Approx(1.0f).margin(1e-4));

    // Turned up to the face: each wrist's screen faces the eyes, its top up and its picture
    // reading left to right as seen (+X to the viewer's right).
    const Posef left_aim = LeftWristUp({-0.06f, -0.28f, -0.30f});
    const Posef left = Vis::WristScreenPose(left_aim, Hand::Left, false);
    // Upright: its top is the way up looks on its tilted face (up less its part along the front).
    const auto upright = [](const Posef& p) {
        const Vec3f z = Axis(p, {0, 0, 1});
        const Vec3f up = Input::Normalized(Vec3f{0, 1, 0} - z * z.y);
        return Input::Dot(Axis(p, {0, 1, 0}), up);
    };
    REQUIRE(Vis::WristFacingDeg(left, kEye) < 0.5f);
    REQUIRE(upright(left) > 0.99f);
    REQUIRE(Axis(left, {1, 0, 0}).x > 0.9f);
    const Posef right = Vis::WristScreenPose(Mirrored(left_aim), Hand::Right, false);
    REQUIRE(right.position.x == Approx(-left.position.x).margin(1e-5));
    REQUIRE(Vis::WristFacingDeg(right, kEye) < 0.5f);
    REQUIRE(upright(right) > 0.99f);
    REQUIRE(Axis(right, {1, 0, 0}).x > 0.9f);
}

TEST_CASE("FrameXr kiuwrist: it shows only while the wrist is turned up", "[frame_xr][kiuwrist]") {
    Vis::WristScreen w;
    const Posef up = LeftWristUp({-0.06f, -0.28f, -0.30f});
    // Held to play: pointing ahead, 0.4 m under the eyes, 0.25 m to the side (the mock's rest).
    const Posef play{{}, {-0.25f, -0.4f, -0.35f}};
    Vis::WristScreen::Out o = w.Update(true, true, play, Hand::Left, false, kEye);
    REQUIRE_FALSE(o.shown);
    REQUIRE(o.facing_deg > Vis::kWristHideDeg);
    REQUIRE_FALSE(o.changed);
    o = w.Update(true, true, up, Hand::Left, false, kEye);
    REQUIRE(o.shown);
    REQUIRE(o.changed);
    REQUIRE(o.facing_deg < 1.0f);
    REQUIRE(o.distance_m == Approx(Input::Length(Vec3f{-0.06f, -0.28f, -0.30f})).margin(1e-4));
    // The right hand's play pose (Left Controller aims): hidden too.
    Vis::WristScreen wr;
    const Posef play_r{{}, {0.25f, -0.4f, -0.35f}};
    REQUIRE_FALSE(wr.Update(true, true, play_r, Hand::Right, false, kEye).shown);
    REQUIRE(wr.Update(true, true, Mirrored(up), Hand::Right, false, kEye).shown);
    // Turned away a little: kept (hysteresis) until past kWristHideDeg.
    const auto turned = [&](float deg) {
        // Turn the hand about the screen's middle, about the screen's own up (so its front turns
        // away from the eyes by exactly `deg`).
        const Posef s = Vis::WristScreenPose(up, Hand::Left, false);
        const Vec3f a = Axis(s, {0, 1, 0});
        const float h = deg * 3.14159265f / 360.0f;
        const Quatf q{a.x * std::sin(h), a.y * std::sin(h), a.z * std::sin(h), std::cos(h)};
        const Quatf q2 = Input::Normalized(Input::Multiply(q, up.orientation));
        const Vec3f off = up.position - s.position;
        return Posef{q2, s.position + Input::Rotate(q, off)};
    };
    REQUIRE(w.Update(true, true, turned(37.0f), Hand::Left, false, kEye).shown);
    o = w.Update(true, true, turned(42.0f), Hand::Left, false, kEye);
    REQUIRE_FALSE(o.shown);
    REQUIRE(o.changed);
    REQUIRE_FALSE(w.Update(true, true, turned(37.0f), Hand::Left, false, kEye).shown);
    REQUIRE(w.Update(true, true, turned(30.0f), Hand::Left, false, kEye).shown);
    // Not wanted (switch off, not Angel's Aim) or the controller lost: gone at once.
    REQUIRE_FALSE(w.Update(false, true, up, Hand::Left, false, kEye).shown);
    REQUIRE(w.Update(true, true, up, Hand::Left, false, kEye).shown);
    REQUIRE_FALSE(w.Update(true, false, up, Hand::Left, false, kEye).shown);
}

TEST_CASE("FrameXr kiuwrist: the aiming hand's beam touches the wrist screen, fires nothing",
          "[frame_xr][kiuwrist]") {
    const Posef left_aim = LeftWristUp({-0.06f, -0.28f, -0.30f});
    Input::PanelGeometry panels;
    // Angel's Aim: the immersive screen is not a target; the bottom one is the wrist screen.
    panels.top.visible = false;
    panels.bottom = {Vis::WristScreenPose(left_aim, Hand::Left, false), Vis::kWristScreenWidth,
                     Vis::kWristScreenHeight, true};
    panels.bottom_fixed = true;
    panels.bottom_toggle_enabled = true;
    Input::Mapper mapper;
    int64_t now = 0;
    // The right hand near the chest, in front of the screen's face, pointing down and across.
    const Vec3f hand_at{0.15f, -0.15f, -0.15f};
    const auto frame = [&](Vec3f target, float trigger, float grip = 0.0f) {
        Input::InputFrame f;
        f.time_ns = now;
        now += 11'111'111;
        f.hand[0].active = true;
        f.hand[0].aim = left_aim;
        f.hand[1].active = true;
        f.hand[1].aim = {Input::FaceViewer(target, hand_at, {0.0f, 1.0f, 0.0f}), hand_at};
        f.hand[1].trigger = trigger;
        f.hand[1].grip = grip;
        return f;
    };
    const struct {
        float u, v;
    } taps[] = {{0.25f, 0.75f}, {0.5f, 0.5f}, {0.9f, 0.1f}};
    for (const auto& t : taps) {
        const Vec3f p = Input::PointOnQuad(panels.bottom, t.u, t.v);
        mapper.Update(frame(p, 0.0f), panels);
        const Input::MapperOutput& pressed = mapper.Update(frame(p, 1.0f), panels);
        REQUIRE(pressed.touch_event_count == 1);
        REQUIRE(pressed.touch_events[0].type == Input::TouchEventType::Pressed);
        // The 3DS touch pixel: u x 320, v x 240 (v from the screen's top).
        REQUIRE(pressed.touch_events[0].x == Approx(t.u * 320.0f).margin(0.5));
        REQUIRE(pressed.touch_events[0].y == Approx(t.v * 240.0f).margin(0.5));
        REQUIRE_FALSE(pressed.controls.Get(Input::Control::TriggerR));
        REQUIRE_FALSE(pressed.controls.Get(Input::Control::TriggerL));
        const Input::MapperOutput& held = mapper.Update(frame(p, 1.0f), panels);
        REQUIRE_FALSE(held.controls.Get(Input::Control::TriggerR));
        const Input::MapperOutput& released = mapper.Update(frame(p, 0.0f), panels);
        REQUIRE(released.touch_event_count == 1);
        REQUIRE(released.touch_events[0].type == Input::TouchEventType::Released);
    }
    // The control: the same pull pointed past the wrist is ZR (the shot).
    const Vec3f past{0.0f, 0.0f, -2.0f};
    mapper.Update(frame(past, 0.0f), panels);
    const Input::MapperOutput& fire = mapper.Update(frame(past, 1.0f), panels);
    REQUIRE(fire.touch_event_count == 0);
    REQUIRE(fire.controls.Get(Input::Control::TriggerR));
    mapper.Update(frame(past, 0.0f), panels);
    // Hidden (wrist down): a pull where it was is a shot, not a touch.
    panels.bottom.visible = false;
    const Vec3f where = Input::PointOnQuad(panels.bottom, 0.5f, 0.5f);
    const Input::MapperOutput& hidden = mapper.Update(frame(where, 1.0f), panels);
    REQUIRE(hidden.touch_event_count == 0);
    REQUIRE(hidden.controls.Get(Input::Control::TriggerR));
    mapper.Update(frame(where, 0.0f), panels);
    // A grip on it grabs nothing (it goes with the hand).
    panels.bottom.visible = true;
    mapper.Update(frame(where, 0.0f, 1.0f), panels);
    REQUIRE_FALSE(mapper.DragActive());
    mapper.Update(frame(where, 0.0f, 0.0f), panels);
    panels.bottom_fixed = false; // a normal bottom screen still can be grabbed
    mapper.Update(frame(where, 0.0f, 1.0f), panels);
    REQUIRE(mapper.DragActive());
    // AzaharFrame kiu1s: the menu panel comes up meanwhile: let go, and it grabs nothing.
    panels.no_drag = true;
    mapper.Update(frame(where, 0.0f, 1.0f), panels);
    REQUIRE_FALSE(mapper.DragActive());
    mapper.Update(frame(where, 0.0f, 0.0f), panels);
    mapper.Update(frame(where, 0.0f, 1.0f), panels);
    REQUIRE_FALSE(mapper.DragActive());
    mapper.Update(frame(where, 0.0f, 0.0f), panels);
    REQUIRE(mapper.Update(frame(where, 1.0f), panels).touch_event_count == 1); // still touched
}

// AzaharFrame kiu1s: the menu panel (Angel's Aim, the game's menus): the Secondary Screen large
// in front, the Primary Screen above it. A pull on the large one is a touch at that pixel, held
// and moved a drag (the game's lists), and no shot; a grip grabs neither.
TEST_CASE("FrameXr kiu1s: the menu panel is touched and dragged by the beam, fires nothing",
          "[frame_xr][kiu1s]") {
    namespace FP = Kiu::FirstPerson;
    for (const FP::MenuPlace& place : {FP::kMenuPlace, FP::kMenuPlaceCauldron}) {
        const auto quad = [](float down_deg, float width_deg, float aspect) {
            const FP::PanelInView p = FP::PersistentInView(0.0f, down_deg, width_deg, aspect);
            return Input::PanelQuad{{{p.qx, p.qy, p.qz, p.qw}, {p.x, p.y, p.z}}, p.width, p.height,
                                    true};
        };
        Input::PanelGeometry panels;
        panels.top = quad(-place.up_deg, place.top_width_deg, 400.0f / 240.0f);
        panels.bottom = quad(place.down_deg, place.width_deg, 320.0f / 240.0f);
        panels.no_drag = true;
        panels.bottom_toggle_enabled = true;
        Input::Mapper mapper;
        int64_t now = 0;
        const Vec3f hand_at{0.2f, -0.35f, -0.2f}; // the right hand at rest
        const auto frame = [&](Vec3f target, float trigger, float grip = 0.0f) {
            Input::InputFrame f;
            f.time_ns = now;
            now += 11'111'111;
            f.hand[1].active = true;
            f.hand[1].aim = {Input::FaceViewer(target, hand_at, {0.0f, 1.0f, 0.0f}), hand_at};
            f.hand[1].trigger = trigger;
            f.hand[1].grip = grip;
            return f;
        };
        const auto at = [&](float u, float v) { return Input::PointOnQuad(panels.bottom, u, v); };
        // Every corner of it is in reach of the beam from the hand at rest: a tap is that pixel.
        for (const auto& [u, v] : {std::pair{0.03f, 0.03f}, {0.97f, 0.03f}, {0.5f, 0.5f},
                                   {0.03f, 0.97f}, {0.97f, 0.97f}}) {
            mapper.Update(frame(at(u, v), 0.0f), panels);
            const Input::MapperOutput& pressed = mapper.Update(frame(at(u, v), 1.0f), panels);
            REQUIRE(pressed.touch_event_count == 1);
            REQUIRE(pressed.touch_events[0].type == Input::TouchEventType::Pressed);
            REQUIRE(pressed.touch_events[0].x == Approx(u * 320.0f).margin(0.5));
            REQUIRE(pressed.touch_events[0].y == Approx(v * 240.0f).margin(0.5));
            REQUIRE_FALSE(pressed.controls.Get(Input::Control::TriggerR));
            const Input::MapperOutput& released = mapper.Update(frame(at(u, v), 0.0f), panels);
            REQUIRE(released.touch_event_count == 1);
            REQUIRE(released.touch_events[0].type == Input::TouchEventType::Released);
        }
        // A drag down a list: pressed, moved with the beam, let go where it ends; never a shot.
        mapper.Update(frame(at(0.5f, 0.3f), 0.0f), panels);
        REQUIRE(mapper.Update(frame(at(0.5f, 0.3f), 1.0f), panels).touch_event_count == 1);
        float last_y = 0.3f * 240.0f;
        int moves = 0;
        for (int i = 1; i <= 10; ++i) {
            const float v = 0.3f + 0.05f * static_cast<float>(i);
            const Input::MapperOutput& out = mapper.Update(frame(at(0.5f, v), 1.0f), panels);
            REQUIRE_FALSE(out.controls.Get(Input::Control::TriggerR));
            for (int e = 0; e < out.touch_event_count; ++e) {
                REQUIRE(out.touch_events[e].type == Input::TouchEventType::Moved);
                REQUIRE(out.touch_events[e].y > last_y);
                last_y = out.touch_events[e].y;
                moves++;
            }
        }
        CHECK(moves >= 8);
        CHECK(last_y == Approx(0.8f * 240.0f).margin(1.0));
        const Input::MapperOutput& let_go = mapper.Update(frame(at(0.5f, 0.8f), 0.0f), panels);
        REQUIRE(let_go.touch_event_count == 1);
        REQUIRE(let_go.touch_events[0].type == Input::TouchEventType::Released);
        // A grip on either screen grabs nothing.
        mapper.Update(frame(at(0.5f, 0.5f), 0.0f, 1.0f), panels);
        REQUIRE_FALSE(mapper.DragActive());
        mapper.Update(frame(at(0.5f, 0.5f), 0.0f, 0.0f), panels);
        mapper.Update(frame(Input::PointOnQuad(panels.top, 0.5f, 0.5f), 0.0f, 1.0f), panels);
        REQUIRE_FALSE(mapper.DragActive());
    }
}

// AzaharFrame kiu131: Wrist Side. Back (the default setting): on the back of the wrist like a
// watch face; Inside: the palm side (as before). Each side shows only in its own reading pose:
// the back of the hand toward the eyes (the forearm across the body) for Back, the palm toward
// the face for Inside; the other pose turns it away. Both wrists, both sizes.
TEST_CASE("FrameXr kiu131: Wrist Side, Back and Inside, each in its own pose",
          "[frame_xr][kiuwrist][kiu131]") {
    using Vis::WristSide;
    REQUIRE(Vis::WristSideFrom(0) == WristSide::Back);
    REQUIRE(Vis::WristSideFrom(1) == WristSide::Inside);
    REQUIRE(Vis::WristSideFrom(7) == WristSide::Back);
    // The two sides: mirror images across the handle's middle (x -0.0135), the same distance
    // from the controller, the same place along the forearm.
    const Posef back = Vis::WristLocalPose(WristSide::Back);
    const Posef inside = Vis::WristLocalPose(WristSide::Inside);
    REQUIRE(back.position.x + inside.position.x == Approx(2.0f * -0.0135f).margin(1e-5));
    REQUIRE(back.position.y == Approx(inside.position.y));
    REQUIRE(back.position.z == Approx(inside.position.z));
    REQUIRE(back.position.x < -0.04f); // the outer side, clear of the controller's outer face
    REQUIRE(Axis(back, {0, 0, 1}).x < -0.95f); // facing outward
    REQUIRE(Axis(back, {1, 0, 0}).z == Approx(Axis(inside, {1, 0, 0}).z)); // reads toward the hand
    const Vec3f c = Input::Cross(Axis(back, {1, 0, 0}), Axis(back, {0, 1, 0}));
    REQUIRE(Input::Dot(c, Axis(back, {0, 0, 1})) == Approx(1.0f).margin(1e-4)); // not mirrored

    // The pose that shows Back on the left wrist, facing the eyes, upright, reading left to right.
    const auto aim_for = [](WristSide side, const Vec3f& centre) {
        const Vec3f z = Input::Normalized(kEye - centre);
        const Vec3f x = Input::Normalized(Input::Cross({0.0f, 1.0f, 0.0f}, z));
        const Vec3f y = Input::Cross(z, x);
        const Quatf panel = Input::Normalized(Input::FromBasis(x, y, z));
        const Posef local = Vis::WristScreenLocalPose(Hand::Left, false, side);
        const Quatf q =
            Input::Normalized(Input::Multiply(panel, Input::Conjugate(local.orientation)));
        return Posef{q, centre - Input::Rotate(q, local.position)};
    };
    const Vec3f centre{-0.05f, -0.30f, -0.30f};
    const Posef watch = aim_for(WristSide::Back, centre);
    const Posef palm = aim_for(WristSide::Inside, centre);
    // The watch pose is the natural one: the controller points the way the forearm runs, across
    // the body to the right; its top (the thumb's side) toward the body, its inner (palm) side
    // down. The palm pose has the palm (+X) toward the face.
    INFO("watch beam " << Axis(watch, {0, 0, -1}).x << ", " << Axis(watch, {0, 0, -1}).y << ", "
                       << Axis(watch, {0, 0, -1}).z);
    REQUIRE(Axis(watch, {0, 0, -1}).x > 0.5f);
    REQUIRE(Axis(watch, {1, 0, 0}).y < -0.5f);
    REQUIRE(Axis(watch, {0, 1, 0}).z > 0.3f);
    REQUIRE(Input::Dot(Axis(palm, {1, 0, 0}), Input::Normalized(kEye - centre)) > 0.9f);
    for (const float scale : {1.0f, 2.0f}) {
        const float w = Vis::kWristScreenWidth * scale;
        Vis::WristScreen b, i;
        // Back: shown in the watch pose, turned away with the palm to the face.
        Vis::WristScreen::Out o = b.Update(true, true, watch, Hand::Left, false, kEye,
                                           WristSide::Back, w);
        REQUIRE(o.shown);
        REQUIRE(o.facing_deg < 1.0f);
        REQUIRE(Axis(o.pose, {1, 0, 0}).x > 0.9f); // reads left to right
        REQUIRE(Axis(o.pose, {0, 1, 0}).y > 0.0f);
        REQUIRE(b.Update(true, true, palm, Hand::Left, false, kEye, WristSide::Back, w).facing_deg >
                Vis::kWristHideDeg);
        REQUIRE_FALSE(b.Update(true, true, palm, Hand::Left, false, kEye, WristSide::Back, w).shown);
        // Inside: the other way round.
        REQUIRE(i.Update(true, true, palm, Hand::Left, false, kEye, WristSide::Inside, w).shown);
        REQUIRE_FALSE(
            i.Update(true, true, watch, Hand::Left, false, kEye, WristSide::Inside, w).shown);
        // Held to play: neither shows.
        const Posef play{{}, {-0.25f, -0.4f, -0.35f}};
        Vis::WristScreen p1, p2;
        REQUIRE_FALSE(p1.Update(true, true, play, Hand::Left, false, kEye, WristSide::Back, w).shown);
        REQUIRE_FALSE(
            p2.Update(true, true, play, Hand::Left, false, kEye, WristSide::Inside, w).shown);
        // The right wrist: the left one's pose mirrored, shown, upright, reading left to right.
        Vis::WristScreen r;
        const Vis::WristScreen::Out ro =
            r.Update(true, true, Mirrored(watch), Hand::Right, false, kEye, WristSide::Back, w);
        REQUIRE(ro.shown);
        REQUIRE(Axis(ro.pose, {1, 0, 0}).x > 0.9f);
        REQUIRE(Axis(ro.pose, {0, 1, 0}).y > 0.0f);
        // Beside the wrist stats (both on the left wrist, the same side): moved along the
        // forearm by half of each plus 1 cm, so they never overlap.
        const Posef beside = Vis::WristScreenLocalPose(Hand::Left, true, WristSide::Back, w);
        const Posef alone = Vis::WristScreenLocalPose(Hand::Left, false, WristSide::Back, w);
        REQUIRE(Input::Length(beside.position - alone.position) ==
                Approx(0.5f * Vis::kWristWidth + 0.5f * w + 0.01f).margin(1e-5));
    }
    // The wrist stats take the same place and rule.
    const Posef stats = Vis::WristStatsPose(watch, WristSide::Back);
    REQUIRE(Vis::WristShown(Vis::WristFacingDeg(stats, kEye), false));
    REQUIRE_FALSE(Vis::WristShown(
        Vis::WristFacingDeg(Vis::WristStatsPose(palm, WristSide::Back), kEye), false));
}

// AzaharFrame kiu140: the place round the wrist and along the arm.
TEST_CASE("FrameXr kiu140: the wrist screen's place round the wrist and up the arm",
          "[frame_xr][kiuwrist][kiu140]") {
    using Vis::WristSide;
    const auto at = [](int deg, int cm, Hand hand = Hand::Left, WristSide side = WristSide::Back) {
        return Vis::WristScreenLocalPose(hand, false, side, Vis::kWristScreenWidth,
                                         Vis::WristAtFor(deg, cm));
    };
    for (const WristSide side : {WristSide::Back, WristSide::Inside}) {
        // 0 is where it always was, and 360 the same place.
        const Posef was = Vis::WristScreenLocalPose(Hand::Left, false, side);
        const Posef zero = at(0, 0, Hand::Left, side), full = at(360, 0, Hand::Left, side);
        REQUIRE(Input::Length(zero.position - was.position) == Approx(0.0f));
        REQUIRE(Input::Length(full.position - was.position) < 1e-4f);
        const Vec3f axis = Axis(was, {1, 0, 0});
        const Vec3f middle{Vis::kWristMiddleX, was.position.y, was.position.z};
        for (int deg = 0; deg <= 360; deg += 5) {
            const Posef p = at(deg, 0, Hand::Left, side);
            INFO(deg);
            // The same way round the wrist's line: as far from it, at the same place along it,
            // reading along the forearm and facing out.
            REQUIRE(Input::Length(p.position - middle) ==
                    Approx(Input::Length(was.position - middle)).margin(1e-4));
            REQUIRE(std::abs(Input::Dot(p.position - middle, axis)) < 1e-4f);
            REQUIRE(Input::Dot(Axis(p, {1, 0, 0}), axis) > 0.9999f);
            REQUIRE(Input::Dot(Axis(p, {0, 0, 1}), Input::Normalized(p.position - middle)) > 0.95f);
        }
    }
    // A quarter from the back is over the wrist, on the thumb's side, facing up.
    REQUIRE(Axis(at(90, 0), {0, 0, 1}).y > 0.7f);
    // The right wrist: the mirror image, whatever the place.
    for (int deg = 0; deg < 360; deg += 30) {
        const Posef l = at(deg, 7), r = at(deg, 7, Hand::Right);
        REQUIRE(r.position.x == Approx(-l.position.x).margin(1e-5));
        REQUIRE(r.position.y == Approx(l.position.y).margin(1e-5));
        REQUIRE(Axis(r, {0, 0, 1}).x == Approx(-Axis(l, {0, 0, 1}).x).margin(1e-4));
        REQUIRE(Axis(r, {0, 0, 1}).y == Approx(Axis(l, {0, 0, 1}).y).margin(1e-4));
    }
    // Along the arm: toward the elbow, or the hand below 0.
    const Vec3f toward_hand = Axis(at(0, 0), {1, 0, 0});
    REQUIRE(Input::Dot(at(0, 10).position - at(0, 0).position, toward_hand) == Approx(-0.10f));
    REQUIRE(Input::Dot(at(0, -5).position - at(0, 0).position, toward_hand) == Approx(0.05f));
    REQUIRE(Vis::WristAtFor(999, 99).angle_deg == 360.0f);
    REQUIRE(Vis::WristAtFor(-3, -99).up_arm_m == Approx(-0.12f));
    // Round a drawn arm's own line: clear of it at every angle and tilt, reading along it, upright
    // the way it was, and the mirror image on the right.
    for (const Vec3f& dir : {Vec3f{0.0f, -0.6f, 0.8f}, Vec3f{0.3f, -0.7f, 0.6f}, Vec3f{-0.3f, -0.4f, 0.9f},
                             Vec3f{0.0f, -0.9f, 0.4f}}) {
        const Vec3f wrist{-0.01f, -0.05f, 0.08f};
        const Vec3f elbow = wrist + Input::Normalized(dir) * 0.26f;
        for (int deg = 0; deg < 360; deg += 15) {
            auto a = Vis::WristAtFor(deg, 2);
            a.arm = true;
            a.wrist = wrist;
            a.elbow = elbow;
            a.out_m = Vis::WristOutFor(0.06f);
            for (const Hand hand : {Hand::Left, Hand::Right}) {
                auto here = a;
                if (hand == Hand::Right) {
                    here.wrist.x = -wrist.x;
                    here.elbow.x = -elbow.x;
                }
                const Posef p = Vis::WristScreenLocalPose(hand, false, WristSide::Back,
                                                          Vis::kWristScreenWidth, here);
                const Vec3f w = here.wrist, along = Input::Normalized(here.elbow - here.wrist);
                INFO(deg << " " << (hand == Hand::Right));
                // Its middle is the panel's gap outside a 6 cm arm.
                const Vec3f rel = p.position - w;
                const Vec3f across = rel - along * Input::Dot(rel, along);
                REQUIRE(Input::Length(across) > 0.06f + 0.9f * Vis::kWristScreenGap);
                // Reading along the arm, facing away from it.
                REQUIRE(std::abs(Input::Dot(Axis(p, {1, 0, 0}), along)) > 0.999f);
                REQUIRE(Input::Dot(Axis(p, {0, 0, 1}), Input::Normalized(across)) > 0.9f);
            }
        }
    }
    // Out to clear an arm: straight away from the wrist's line, nothing without one.
    REQUIRE(Vis::WristOutFor(0.0f) == 0.0f);
    REQUIRE(Vis::WristOutFor(0.03f) == 0.0f);
    REQUIRE(Vis::WristOutFor(0.06f) == Approx(0.0285f));
    for (int deg = 0; deg < 360; deg += 15) {
        auto a = Vis::WristAtFor(deg, 6);
        const Posef near = Vis::WristScreenLocalPose(Hand::Left, false, WristSide::Back,
                                                     Vis::kWristScreenWidth, a);
        a.out_m = 0.02f;
        const Posef far = Vis::WristScreenLocalPose(Hand::Left, false, WristSide::Back,
                                                    Vis::kWristScreenWidth, a);
        const Vec3f moved = far.position - near.position;
        REQUIRE(Input::Length(moved) == Approx(0.02f).margin(1e-5));
        REQUIRE(std::abs(Input::Dot(moved, Axis(near, {1, 0, 0}))) < 1e-4f);
        REQUIRE(Input::Dot(Input::Normalized(moved), Axis(near, {0, 0, 1})) > 0.95f);
        REQUIRE(Input::Dot(Axis(far, {0, 0, 1}), Axis(near, {0, 0, 1})) > 0.9999f);
    }

    // Upright in quarter turns, with hysteresis.
    const Posef flat{{}, {0, 0, 0}}; // its top up, its front +z
    for (int k = 0; k < 4; ++k) {
        const Vec3f up = Axis(Vis::TurnedQuarters(flat, k), {0, 1, 0});
        REQUIRE(Vis::UprightTurns(flat, up, (k + 2) % 4) == k);
    }
    const auto up_at = [](float deg) {
        return Vec3f{-std::sin(deg * 0.0174533f), std::cos(deg * 0.0174533f), 0.0f};
    };
    REQUIRE(Vis::UprightTurns(flat, up_at(55), 0) == 0);
    REQUIRE(Vis::UprightTurns(flat, up_at(65), 0) == 1);
    REQUIRE(Vis::UprightTurns(flat, up_at(35), 1) == 1);
    REQUIRE(Vis::UprightTurns(flat, up_at(25), 1) == 0);
    REQUIRE(Vis::UprightTurns(flat, {0, 0, 1}, 2) == 2); // edge on: kept
    // While it shows: a quarter turn of the hand flips nothing, its head down does.
    REQUIRE(Vis::UprightTurns(flat, up_at(65), 0, Vis::kUprightShownDeg) == 0);
    REQUIRE(Vis::UprightTurns(flat, up_at(110), 0, Vis::kUprightShownDeg) == 0);
    REQUIRE(Vis::UprightTurns(flat, up_at(170), 0, Vis::kUprightShownDeg) == 2);
}
