// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "frame_xr/input/kiu_room.h"

// AzaharFrame kiuvr: the room <-> game transform (frame_xr/input/kiu_room.h) with made-up poses:
// identity, a camera turned 90 degrees, a rest pose that is somewhere else and turned, a scale
// that is not 1, a rail origin that is not 0; there and back; and against the arithmetic mode 3
// draws with (copied below from the renderer), which is what makes the convention the right one.
using namespace FrameXr::Input::KiuRoom;
using FrameXr::Input::KiuHooks::AxisAngle;
using Catch::Approx;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

void CheckVec(const Vec3& got, const Vec3& want, float margin = 1e-4f) {
    INFO("got (" << got.x << ", " << got.y << ", " << got.z << ") want (" << want.x << ", "
                 << want.y << ", " << want.z << ")");
    CHECK(got.x == Approx(want.x).margin(margin));
    CHECK(got.y == Approx(want.y).margin(margin));
    CHECK(got.z == Approx(want.z).margin(margin));
}
/// The same rotation (q and -q are).
void CheckRot(const Quat& got, const Quat& want, float margin = 1e-4f) {
    for (const Vec3 axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}}) {
        CheckVec(Rotate(got, axis), Rotate(want, axis), margin);
    }
}
Quat Yaw(float deg) { // to the left, about +y
    return AxisAngle({0.0f, 1.0f, 0.0f}, deg * kPi / 180.0f);
}
Quat Pitch(float deg) { // up, about +x
    return AxisAngle({1.0f, 0.0f, 0.0f}, deg * kPi / 180.0f);
}

// ---- What mode 3 does, as the renderer writes it ----

/// frame_xr/immersive.cpp FillInverseView: the head's rotation `q` and position (rest frame).
void FillInverseView(const Quat& q, const Vec3& position, float scaler,
                     std::array<float, 16>& inv) {
    const float x2 = q.x + q.x, y2 = q.y + q.y, z2 = q.z + q.z;
    const float xx2 = q.x * x2, xy2 = q.x * y2, xz2 = q.x * z2;
    const float yy2 = q.y * y2, yz2 = q.y * z2, zz2 = q.z * z2;
    const float sx2 = q.w * x2, sy2 = q.w * y2, sz2 = q.w * z2;
    const float r[16] = {1 - (yy2 + zz2), xy2 + sz2,       xz2 - sy2,       0.f,
                         xy2 - sz2,       1 - (xx2 + zz2), yz2 + sx2,       0.f,
                         xz2 + sy2,       yz2 - sx2,       1 - (xx2 + yy2), 0.f,
                         0.f,             0.f,             0.f,             1.f};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            inv[i * 4 + j] = r[j * 4 + i];
        }
    }
    const Vec3 p = Rotate(Conj(q), position);
    inv[12] = -p.x * scaler;
    inv[13] = -p.y * scaler;
    inv[14] = -p.z * scaler;
}
void MatrixTranspose(float m[16], const float src[16]) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            m[i * 4 + j] = src[j * 4 + i];
        }
    }
}
void MatrixMultiply(float m[16], const float a[16], const float b[16]) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            m[i * 4 + j] =
                a[j] * b[i * 4] + a[j + 4] * b[i * 4 + 1] + a[j + 8] * b[i * 4 + 2] +
                a[j + 12] * b[i * 4 + 3];
        }
    }
}
/// The game's view matrix as it lies in the uniform registers: four rows of four.
struct Rows {
    float f[16];
};
/// The view matrix of a camera at `eye` with the axes `q` (world -> camera axes).
Rows GameView(const GameCamera& c) {
    Rows v{};
    const Vec3 axes[3] = {Rotate(c.q, {1, 0, 0}), Rotate(c.q, {0, 1, 0}), Rotate(c.q, {0, 0, 1})};
    for (int i = 0; i < 3; ++i) {
        v.f[i * 4 + 0] = axes[i].x;
        v.f[i * 4 + 1] = axes[i].y;
        v.f[i * 4 + 2] = axes[i].z;
        v.f[i * 4 + 3] = -Dot(axes[i], c.eye);
    }
    v.f[15] = 1.0f;
    return v;
}
/// video_core/rasterizer_accelerated.cpp ApplyVRDataToPicaVSUniforms, mode 3, one eye in the
/// middle: the registers after the patch.
Rows Patched(Rows view, const std::array<float, 16>& inv_view) {
    float v[16], v2[16], v3[16];
    MatrixTranspose(v, view.f);
    std::memcpy(v2, inv_view.data(), sizeof(v2));
    v2[12] = v2[13] = v2[14] = 0.f;
    MatrixMultiply(v3, v2, v);
    MatrixTranspose(view.f, v3);
    view.f[3] += inv_view[12];
    view.f[7] += inv_view[13];
    view.f[11] += inv_view[14];
    return view;
}
Vec3 Apply(const Rows& m, const Vec3& x) {
    return {m.f[0] * x.x + m.f[1] * x.y + m.f[2] * x.z + m.f[3],
            m.f[4] * x.x + m.f[5] * x.y + m.f[6] * x.z + m.f[7],
            m.f[8] * x.x + m.f[9] * x.y + m.f[10] * x.z + m.f[11]};
}

} // namespace

TEST_CASE("kiuvr: the axes of a quaternion and back", "[frame_xr][kiuvr]") {
    for (const Quat q : {Quat{}, Yaw(90.0f), Yaw(-135.0f), Pitch(60.0f), Mul(Yaw(170.0f), Pitch(-80.0f)),
                         Mul(Mul(Yaw(40.0f), Pitch(20.0f)), AxisAngle({0, 0, 1}, 2.0f))}) {
        const Quat back = FromAxes(Rotate(q, {1, 0, 0}), Rotate(q, {0, 1, 0}), Rotate(q, {0, 0, 1}));
        CheckRot(back, q);
    }
}

TEST_CASE("kiuvr: the scene camera from the game's eye, look-at and up", "[frame_xr][kiuvr]") {
    // Looking along -z with +y up: the camera's axes are the world's.
    GameCamera c = CameraFromLookAt({1, 2, 3}, {1, 2, -97}, {0, 1, 0});
    CheckRot(c.q, Quat{});
    CheckVec(c.eye, {1, 2, 3});
    // The land camera's own direction (KIU_HOOKS.md): dir = (cos p sin y, -sin p, cos p cos y),
    // look-at = eye + dir * 100.
    const float y = 0.7f, p = 0.0524f;
    const Vec3 dir{std::cos(p) * std::sin(y), -std::sin(p), std::cos(p) * std::cos(y)};
    c = CameraFromLookAt({10, 20, 30}, Vec3{10, 20, 30} + dir * 100.0f, {0, 1, 0});
    CheckVec(Ahead(c.q), dir);
    // Its side is level and its up points up.
    CHECK(Rotate(c.q, {1, 0, 0}).y == Approx(0.0f).margin(1e-5f));
    CHECK(Rotate(c.q, {0, 1, 0}).y > 0.99f);
    // Right handed: right = ahead x up.
    CheckVec(Rotate(c.q, {1, 0, 0}), Cross(Ahead(c.q), Rotate(c.q, {0, 1, 0})));
    // Straight down, the up vector along the view: still a camera (some level side).
    c = CameraFromLookAt({0, 50, 0}, {0, 0, 0}, {0, 1, 0});
    CheckVec(Ahead(c.q), {0, -1, 0});
    CHECK(Length(Rotate(c.q, {1, 0, 0})) == Approx(1.0f).margin(1e-5f));
}

TEST_CASE("kiuvr: identity: the room is the world", "[frame_xr][kiuvr]") {
    Frame f;
    const Pose hand{{0.2f, -0.3f, -0.5f}, Mul(Yaw(15.0f), Pitch(-10.0f))};
    const Pose w = RoomToWorld(f, hand);
    CheckVec(w.p, hand.p);
    CheckRot(w.q, hand.q);
    CheckVec(Ahead(w.q), Ahead(hand.q));
    const Pose back = WorldToRoom(f, w);
    CheckVec(back.p, hand.p);
    CheckRot(back.q, hand.q);
}

TEST_CASE("kiuvr: a camera turned 90 degrees to the left", "[frame_xr][kiuvr]") {
    Frame f;
    f.camera = {{100.0f, 12.3f, -40.0f}, Yaw(90.0f)}; // looks along -x
    CheckVec(Ahead(f.camera.q), {-1, 0, 0});
    // One metre ahead of the rest pose is one unit along the camera's view.
    CheckVec(RoomToWorld(f, {{0, 0, -1}, {}}).p, {99.0f, 12.3f, -40.0f});
    // One metre to the right of the rest pose is the camera's right: -z of the world.
    CheckVec(RoomToWorld(f, {{1, 0, 0}, {}}).p, {100.0f, 12.3f, -41.0f});
    // Up stays up.
    CheckVec(RoomToWorld(f, {{0, 0.5f, 0}, {}}).p, {100.0f, 12.8f, -40.0f});
    // A controller pointing 30 degrees to its left points 120 degrees left of the world's -z.
    const Vec3 d = Ahead(RoomToWorld(f, {{}, Yaw(30.0f)}).q);
    CheckVec(d, Ahead(Yaw(120.0f)));
}

TEST_CASE("kiuvr: a rest pose that is elsewhere and turned", "[frame_xr][kiuvr]") {
    Frame f;
    f.rest = {{2.0f, 1.5f, -3.0f}, Yaw(-90.0f)}; // re-centred facing the room's +x
    f.camera = {{0, 10, 0}, {}};
    // The rest pose itself is the eye.
    CheckVec(RoomToWorld(f, {f.rest.position, f.rest.orientation}).p, {0, 10, 0});
    CheckRot(RoomToWorld(f, {f.rest.position, f.rest.orientation}).q, Quat{});
    // One metre ahead of the rest pose in the room (+x) is the camera's ahead (-z).
    CheckVec(RoomToWorld(f, {{3.0f, 1.5f, -3.0f}, {}}).p, {0, 10, -1});
    // The room's -z is the rest pose's left: the world's -x.
    CheckVec(RoomToWorld(f, {{2.0f, 1.5f, -4.0f}, {}}).p, {-1, 10, 0});
    // A hand pointing along the room's +x points along the camera's view.
    CheckVec(Ahead(RoomToWorld(f, {{}, Yaw(-90.0f)}).q), {0, 0, -1});
    // And back.
    const Pose hand{{2.4f, 1.1f, -3.3f}, Mul(Yaw(-70.0f), Pitch(25.0f))};
    const Pose back = WorldToRoom(f, RoomToWorld(f, hand));
    CheckVec(back.p, hand.p);
    CheckRot(back.q, hand.q);
}

TEST_CASE("kiuvr: a scale that is not 1", "[frame_xr][kiuvr]") {
    Frame f;
    f.units_per_metre = LifeSizeUnitsPerMetre(); // 12.3 units are 1.6 m
    CHECK(f.units_per_metre == Approx(7.6875f));
    f.camera = {{5, 12.3f, 7}, {}};
    // The floor under the rest pose, 1.6 m down, is the character's feet.
    CheckVec(RoomToWorld(f, {{0, -1.6f, 0}, {}}).p, {5, 0, 7});
    // Positions scale, directions do not.
    CheckVec(RoomToWorld(f, {{0.1f, 0, -0.4f}, {}}).p, {5 + 0.76875f, 12.3f, 7 - 3.075f});
    CheckVec(Ahead(RoomToWorld(f, {{0.1f, 0, -0.4f}, Pitch(30.0f)}).q), Ahead(Pitch(30.0f)));
    CheckVec(RestToEyeOffset(f.camera, f.units_per_metre, {0, 0, -2}), {0, 0, -15.375f});
    const Pose hand{{0.3f, -0.2f, -0.6f}, Yaw(20.0f)};
    const Pose back = WorldToRoom(f, RoomToWorld(f, hand));
    CheckVec(back.p, hand.p, 1e-5f);
    CheckRot(back.q, hand.q);
    // The scale's limits and bad values.
    CHECK(LifeSizeUnitsPerMetre(12.3f, 1.23f) == Approx(10.0f));
    CHECK(LifeSizeUnitsPerMetre(12.3f, 0.0f) == Approx(7.6875f));
    CHECK(LifeSizeUnitsPerMetre(std::nanf(""), 1.6f) == Approx(7.6875f));
    CHECK(LifeSizeUnitsPerMetre(1.0f, 100.0f) == Approx(kUnitsPerMetreMin));
    CHECK(LifeSizeUnitsPerMetre(1000.0f, 1.0f) == Approx(kUnitsPerMetreMax));
}

TEST_CASE("kiuvr: a rail origin that is not 0", "[frame_xr][kiuvr]") {
    // The air battle's numbers (KIU_HOOKS.md): the frame's origin far from the world's, the
    // player near the frame's own origin, his parent turned a little.
    const Vec3 origin{7.9f, -9920.7f, -34191.5f};
    const Quat parent_q = Mul(Yaw(3.0f), Pitch(-2.0f));
    const Vec3 local{0.0f, 0.0f, -3.4f};
    const Vec3 world = origin + Rotate(parent_q, local);
    CheckVec(RailOrigin(world, local, parent_q), origin, 2e-2f);
    // On land: no parent, local = world, the origin is the world's.
    CheckVec(RailOrigin({10, 0, 20}, {10, 0, 20}, Quat{}), {0, 0, 0});
    // A point of the world in the rail's frame and back.
    const Vec3 p{origin.x + 30.0f, origin.y - 4.0f, origin.z - 200.0f};
    CheckVec(WorldToRail(p, origin), {30, -4, -200}, 2e-2f);
    CheckVec(RailToWorld(WorldToRail(p, origin), origin), p, 2e-2f);
    // The muzzle's offset from the player needs no origin at all: the eye is the player plus
    // the eye offset, the muzzle the eye plus the camera's turn of the scaled rest point.
    Frame f;
    f.units_per_metre = 8.0f;
    const Vec3 eye_offset{0.0f, 3.0f, 0.0f};
    f.camera = {world + eye_offset, Yaw(180.0f)}; // flying along +z
    const Vec3 muzzle_rest{0.25f, -0.4f, -0.35f};
    const Vec3 offset = eye_offset + RestToEyeOffset(f.camera, f.units_per_metre, muzzle_rest);
    CheckVec(offset, {-2.0f, 3.0f - 3.2f, 2.8f});
    // The same point through the world and the rail: the player's local position plus it.
    const Vec3 muzzle_world = RoomToWorld(f, {muzzle_rest, {}}).p;
    CheckVec(WorldToRail(muzzle_world, origin), Rotate(parent_q, local) + offset, 2e-2f);
}

TEST_CASE("kiuvr: what is at a point of the room is drawn at that point", "[frame_xr][kiuvr]") {
    // Mode 3 draws through Rc^-1 (V x - s h). A point of the room taken into the world must come
    // out, in the patched view, where it is from the head: (point - head) in the head's axes,
    // times the scale. Then the controller in the room and the game's world around it agree.
    Frame f;
    f.rest = {{0.3f, 1.6f, 0.2f}, Yaw(25.0f)};
    f.camera = CameraFromLookAt({120.0f, 30.0f, -75.0f}, {180.0f, 22.0f, -10.0f}, {0, 1, 0});
    f.units_per_metre = 7.6875f;
    const Pose head_room{{0.45f, 1.52f, 0.05f}, Mul(Mul(Yaw(-40.0f), Pitch(18.0f)),
                                                    AxisAngle({0, 0, 1}, 0.1f))};
    const Pose hand_room{{0.7f, 1.2f, -0.3f}, Mul(Yaw(60.0f), Pitch(-5.0f))};
    const Pose head = RoomToRest(f.rest, head_room);
    std::array<float, 16> inv{};
    FillInverseView(head.q, head.p, f.units_per_metre, inv);
    const Rows patched = Patched(GameView(f.camera), inv);

    const Pose hand_world = RoomToWorld(f, hand_room);
    const Vec3 seen = Apply(patched, hand_world.p);
    const Vec3 in_head = Rotate(Conj(head_room.q), hand_room.p - head_room.p);
    CheckVec(seen, in_head * f.units_per_metre, 2e-3f);
    // A point far along the hand's ray is seen along the hand's direction as the head has it.
    const Vec3 far_world = hand_world.p + Ahead(hand_world.q) * 5000.0f;
    const Vec3 far_seen = Unit(Apply(patched, far_world) - seen);
    CheckVec(far_seen, Rotate(Conj(head_room.q), Ahead(hand_room.q)), 1e-4f);
    // The head itself is the patched view's origin, and looks along its -z.
    CheckVec(Apply(patched, RoomToWorld(f, head_room).p), {0, 0, 0}, 2e-3f);
    const Vec3 ahead_world = RoomToWorld(f, head_room).p + Ahead(RoomToWorld(f, head_room).q);
    CheckVec(Apply(patched, ahead_world), {0, 0, -1}, 2e-3f);
    // With the head at the rest pose the patched view is the game's own.
    FillInverseView(Quat{}, {0, 0, 0}, f.units_per_metre, inv);
    const Rows same = Patched(GameView(f.camera), inv);
    CheckVec(Apply(same, {1, 2, 3}), Apply(GameView(f.camera), {1, 2, 3}), 1e-3f);
}

TEST_CASE("kiuvr: the reticle pixel of a world point, as the game has it", "[frame_xr][kiuvr]") {
    // Read back from the running game (Practice Range, land): the scene camera, and the point
    // the game put in the world for two reticle pixels.
    const GameCamera cam = CameraFromLookAt({0.0f, 12.41002f, 300.0f},
                                            {0.0f, 7.17662f, 200.13704f},
                                            {0.0f, 0.99863f, -0.05233f});
    const Projection proj{0.785398f, 1.666667f};
    const ReticleScreen screen{400.0f, 240.0f, 200.0f, 120.0f};
    REQUIRE(proj.Ok());
    REQUIRE(screen.Ok());
    bool ok = false;
    Pixel p = WorldToReticle(cam, proj, screen, {0.0f, 13.821f, -201.405f}, &ok);
    CHECK(ok);
    CHECK(p.x == Approx(200.0f).margin(0.01f));
    CHECK(p.y == Approx(104.0f).margin(0.05f)); // the land reticle's rest: 16 pixels above the middle
    CHECK(CameraDepth(cam, {0.0f, 13.821f, -201.405f}) == Approx(500.64f).margin(0.05f));
    p = WorldToReticle(cam, proj, screen, {103.687f, 46.975f, -203.142f}, &ok);
    CHECK(p.x == Approx(260.0f).margin(0.05f));
    // And back: the pixel's direction passes through the point.
    const Vec3 d = ReticleToDirection(cam, proj, screen, {200.0f, 104.0f});
    CheckVec(d, Unit(Vec3{0.0f, 13.821f, -201.405f} - cam.eye), 2e-4f);
    for (const Pixel px : {Pixel{0.5f, 0.5f}, Pixel{399.0f, 239.0f}, Pixel{123.0f, 200.0f}}) {
        const Vec3 dir = ReticleToDirection(cam, proj, screen, px);
        const Pixel back = WorldToReticle(cam, proj, screen, cam.eye + dir * 777.0f, &ok);
        CHECK(ok);
        CHECK(back.x == Approx(px.x).margin(0.02f));
        CHECK(back.y == Approx(px.y).margin(0.02f));
    }
    // A wider picture (a widescreen build's projection) puts the same point nearer the middle.
    const Projection wide{0.785398f, 2.333333f};
    const Pixel w = WorldToReticle(cam, wide, screen, {103.687f, 46.975f, -203.142f});
    CHECK(w.x == Approx(200.0f + 60.0f * 1.666667f / 2.333333f).margin(0.05f));
    CHECK(w.y == Approx(p.y).margin(1e-3f));
    // Behind the camera: said so.
    WorldToReticle(cam, proj, screen, {0.0f, 12.0f, 400.0f}, &ok);
    CHECK_FALSE(ok);
    CHECK_FALSE(Projection{0.0f, 1.6f}.Ok());
    CHECK_FALSE(ReticleScreen{400.0f, 240.0f, 0.0f, 120.0f}.Ok());
}

TEST_CASE("kiuvr: the pixel that makes the game aim where the hand points", "[frame_xr][kiuvr]") {
    const GameCamera cam{{10.0f, 12.0f, -30.0f}, Yaw(40.0f)};
    const Projection proj{0.785398f, 1.666667f};
    const ReticleScreen screen{400.0f, 240.0f, 200.0f, 120.0f};
    const Vec3 ahead = Ahead(cam.q), right = Rotate(cam.q, {1, 0, 0}), up = Rotate(cam.q, {0, 1, 0});
    // A hand at the eye pointing along the view: the middle, at any depth.
    bool ok = false;
    Vec3 hit;
    Pixel p = AimPixel(cam, proj, screen, cam.eye, ahead, 500.0f, &ok, &hit);
    CHECK(ok);
    CHECK(p.x == Approx(200.0f).margin(0.01f));
    CHECK(p.y == Approx(120.0f).margin(0.01f));
    CheckVec(hit, cam.eye + ahead * 500.0f, 1e-2f);
    // A hand 4 units to the right and 3 down, pointing along the view (parallel to it): far
    // away it is almost the middle, on something near it is clearly to the right and below.
    const Vec3 hand = cam.eye + right * 4.0f + up * -3.0f + ahead * 2.0f;
    p = AimPixel(cam, proj, screen, hand, ahead, 1500.0f, &ok, &hit);
    CHECK(CameraDepth(cam, hit) == Approx(1500.0f).margin(0.05f));
    CHECK(p.x == Approx(200.0f + 200.0f * 4.0f / (1500.0f * 0.414214f * 1.666667f)).margin(0.01f));
    CHECK(p.y == Approx(120.0f + 120.0f * 3.0f / (1500.0f * 0.414214f)).margin(0.01f));
    p = AimPixel(cam, proj, screen, hand, ahead, 40.0f, &ok, &hit);
    CHECK(p.x == Approx(200.0f + 200.0f * 4.0f / (40.0f * 0.414214f * 1.666667f)).margin(0.01f));
    CHECK(p.y == Approx(120.0f + 120.0f * 3.0f / (40.0f * 0.414214f)).margin(0.01f));
    // The point is on the hand's ray: seen from the hand it is where the hand points.
    CheckVec(Unit(hit - hand), ahead, 1e-5f);
    // A hand pointing 20 degrees to the left of the view from that place: the point is on its
    // ray, at the asked depth, and the pixel is left of the middle.
    const Vec3 left20 = Rotate(Mul(cam.q, Yaw(20.0f)), {0, 0, -1});
    p = AimPixel(cam, proj, screen, hand, left20, 300.0f, &ok, &hit);
    CHECK(ok);
    CheckVec(Unit(hit - hand), left20, 1e-5f);
    CHECK(CameraDepth(cam, hit) == Approx(300.0f).margin(0.05f));
    CHECK(p.x < 200.0f);
    // Through the game's own way back: the pixel's direction from the eye passes through it.
    CheckVec(ReticleToDirection(cam, proj, screen, p), Unit(hit - cam.eye), 1e-4f);
    // A hand pointing backwards: no pixel in front, said so; sideways along the plane: a point
    // along the ray all the same.
    AimPixel(cam, proj, screen, hand, ahead * -1.0f, 300.0f, &ok, &hit);
    CHECK_FALSE(ok);
    AimPixel(cam, proj, screen, hand, right, 300.0f, &ok, &hit);
    CheckVec(Unit(hit - hand), right, 1e-5f);
    // A depth that is not usable (the game's aim point at the eye): the least depth.
    AimPixel(cam, proj, screen, cam.eye, ahead, std::nanf(""), &ok, &hit);
    CheckVec(hit, cam.eye + ahead * kAimDepthMin, 1e-3f);
}

TEST_CASE("kiuvr: the game's aim quaternion for a direction", "[frame_xr][kiuvr]") {
    // +z is the shot's direction.
    CheckRot(ShotQuat({0, 0, 1}), Quat{});
    CheckVec(ShotDirection(ShotQuat({0, 0, 5})), {0, 0, 1});
    for (const Vec3 d : {Vec3{1, 0, 0}, Vec3{-0.3f, 0.2f, -0.9f}, Vec3{0.5f, -0.7f, 0.1f},
                         Vec3{0, 1, 0}, Vec3{0, -1, 0}}) {
        const Quat q = ShotQuat(d);
        CheckVec(ShotDirection(q), Unit(d), 1e-5f);
        // Its side is level (the world's up kept up), its up never points down.
        CHECK(Rotate(q, {1, 0, 0}).y == Approx(0.0f).margin(1e-5f));
        CHECK(Rotate(q, {0, 1, 0}).y >= -1e-5f);
    }
    // What the shot stub does with it: q' = wanted * conj(base) * q. A bullet with the game's
    // aim exactly flies along the hand's ray; one scattered 3 degrees to its own left and 2 up
    // keeps exactly that from the hand's ray.
    const Vec3 game_aim = Unit(Vec3{0.2f, 0.05f, -1.0f}), ray = Unit(Vec3{-0.8f, 0.3f, -0.4f});
    const Quat base = ShotQuat(game_aim), wanted = ShotQuat(ray);
    CheckVec(ShotDirection(Mul(wanted, Mul(Conj(base), base))), ray, 1e-5f);
    const Quat scatter = Mul(AxisAngle({0, 1, 0}, 3.0f * kPi / 180.0f),
                             AxisAngle({1, 0, 0}, -2.0f * kPi / 180.0f));
    const Quat bullet = Mul(base, scatter);
    const Quat turned = Mul(wanted, Mul(Conj(base), bullet));
    CheckRot(turned, Mul(wanted, scatter), 1e-5f);
    const float off_game = std::acos(Dot(ShotDirection(bullet), game_aim));
    const float off_ray = std::acos(Dot(ShotDirection(turned), ray));
    CHECK(off_ray == Approx(off_game).margin(1e-4f));
    CHECK(off_ray == Approx(std::sqrt(9.0f + 4.0f) * kPi / 180.0f).margin(2e-3f));
}

TEST_CASE("kiuvr: the shape the game aims with, from its own aim point", "[frame_xr][kiuvr]") {
    const ReticleScreen screen{400.0f, 240.0f, 200.0f, 120.0f};
    const float fov = 0.785398f, t = std::tan(fov * 0.5f);
    const GameCamera cam{{4.7f, 14.2f, 333.7f}, Pitch(-3.0f)};
    // A point the game would put in the world for a pixel when it aims `shape` wide.
    const auto point_for = [&](const Pixel& px, float shape, float depth) {
        const Vec3 v{(px.x - 200.0f) / 200.0f * t * shape * depth,
                     -(px.y - 120.0f) / 120.0f * t * depth, -depth};
        return cam.eye + Rotate(cam.q, v);
    };
    AimShape shape;
    CHECK(shape.Get(1.6667f) == Approx(1.6667f)); // nothing measured: the fallback
    // The reticle in the middle says nothing.
    shape.Update(cam, fov, screen, {200.0f, 104.0f}, point_for({200.0f, 104.0f}, 2.3333f, 500.0f));
    shape.Update(cam, fov, screen, {205.0f, 104.0f}, point_for({205.0f, 104.0f}, 2.3333f, 500.0f));
    CHECK(shape.Count() == 0);
    // The original game: 5:3 (as read back from it: pixel 260 and its point).
    {
        AimShape s;
        const GameCamera c = CameraFromLookAt({0.0f, 12.41002f, 300.0f},
                                              {0.0f, 7.17662f, 200.13704f},
                                              {0.0f, 0.99863f, -0.05233f});
        s.Update(c, fov, screen, {260.0f, 84.8f}, {103.687f, 46.975f, -203.142f});
        s.Update(c, fov, screen, {260.0f, 84.8f}, {103.687f, 46.975f, -203.142f});
        CHECK(s.Get(9.0f) == Approx(1.6667f).margin(2e-3f));
    }
    // A 21:9 build: the first still reading gives it at once, from any depth.
    shape.Update(cam, fov, screen, {250.0f, 92.8f}, point_for({250.0f, 92.8f}, 2.3333f, 534.4f));
    shape.Update(cam, fov, screen, {250.0f, 92.8f}, point_for({250.0f, 92.8f}, 2.3333f, 534.4f));
    CHECK(shape.Get(1.6667f) == Approx(2.3333f).margin(1e-3f));
    shape.Update(cam, fov, screen, {150.0f, 130.0f}, point_for({150.0f, 130.0f}, 2.3333f, 196.7f));
    shape.Update(cam, fov, screen, {150.0f, 130.0f}, point_for({150.0f, 130.0f}, 2.3333f, 196.7f));
    CHECK(shape.Get(1.6667f) == Approx(2.3333f).margin(1e-3f));
    // A reticle on the move (the point is the frame before's): not counted.
    const int before = shape.Count();
    shape.Update(cam, fov, screen, {300.0f, 100.0f}, point_for({150.0f, 130.0f}, 2.3333f, 196.7f));
    CHECK(shape.Count() == before);
    // Readings that cannot be a shape: a point behind the camera, a pixel that makes it absurd.
    shape.Update(cam, fov, screen, {300.0f, 100.0f}, cam.eye + Rotate(cam.q, {5, 0, 30}));
    shape.Update(cam, fov, screen, {300.0f, 100.0f}, point_for({300.0f, 100.0f}, 9.0f, 300.0f));
    CHECK(shape.Count() == before);
    CHECK(shape.Get(1.6667f) == Approx(2.3333f).margin(1e-3f));
    // One odd reading in range moves it a fifth of the way only.
    shape.Update(cam, fov, screen, {300.0f, 100.0f}, point_for({300.0f, 100.0f}, 1.8333f, 300.0f));
    CHECK(shape.Get(1.6667f) == Approx(2.2333f).margin(2e-3f));
    shape.Reset();
    CHECK(shape.Count() == 0);
}
