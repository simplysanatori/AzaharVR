// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuvr: Kid Icarus: Uprising in the Full (6DoF) view, first person: the one
// transform between the room and the game's world, pure maths (no emulator or OpenXR types), so
// tests/frame_xr/kiu_room.cpp checks it. A test build behind test switches only (KIU_HOOKS.md,
// "First person in 6DoF").
//
// The convention is not chosen here: it is what mode 3 draws (video_core/
// rasterizer_accelerated.cpp ApplyVRDataToPicaVSUniforms with frame_xr/immersive.cpp
// FillInverseView). With V the game's view matrix (world -> the game camera's axes), the picture
// is drawn through
//     view'(x) = Rc^-1 * (V * x  -  s * h)
// where h is the head's position and Rc its rotation, both taken in the REST frame (the head's
// place and heading when the Full view started or was re-centred, Layout::RestPose: yaw only, so
// the rest frame is level), and s is the leaning scale in game units per metre. Read the other
// way round: **the rest frame, scaled by s, IS the game camera's frame.** A point p of the room,
// taken in the rest frame, is drawn where the world point
//     world = eye + Qcam * (s * p)
// is drawn, and a direction d of the rest frame is the world direction Qcam * d. (Rc is the
// head's own rotation only while the Full view's screen has its default size and place: a
// resized or pitched screen gives the game Layout::GameCameraRotation instead, and then nothing
// in the room lines up with the picture except along the head's forward axis.)
//
// Spaces, all right handed:
//   room   OpenXR's LOCAL space, metres, +y up, a pose looks along its own -z.
//   rest   the room seen from the rest pose: p_rest = conj(rest.q) * (p_room - rest.p).
//   camera the scene camera's own axes, game units from the scene's eye: +x right, +y up, -z
//          ahead (the game's camera node looks along its -z too). camera = s * rest.
//   world  the game's world, game units, +y up: world = eye + Qcam * camera.
//   rail   in an air battle everything but the player's own parent hangs on a frame that moves
//          with the rail: local = world - origin, the axes the world's (KIU_HOOKS.md).
// The game's shots use another forward: a shot's quaternion applied to +z is its direction.
//
// The scene camera (eye, look-at, up at 0xE14B90, EUR) is read at a VBlank, so it is the one of
// the game frame before; what is taken relative to it (an offset from the eye, a direction)
// stays right while the camera does not turn, and is one game frame old while it does.

#pragma once

#include <cmath>
#include "frame_xr/input/kiu_hooks.h"

namespace FrameXr::Input::KiuRoom {

using KiuHooks::Conj;
using KiuHooks::Mul;
using KiuHooks::Normalized;
using KiuHooks::Quat;
using KiuHooks::Rotate;
using KiuHooks::Vec3;

constexpr float Dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
constexpr Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Length(const Vec3& a) {
    return std::sqrt(Dot(a, a));
}
/// `a` at unit length; `fallback` when it has none.
inline Vec3 Unit(const Vec3& a, const Vec3& fallback = {0.0f, 0.0f, -1.0f}) {
    const float n = Length(a);
    if (!(n > 1e-12f) || !std::isfinite(n)) {
        return fallback;
    }
    return a * (1.0f / n);
}

/// A place and an orientation in some space; `q` takes the pose's own axes to the space's.
struct Pose {
    Vec3 p;
    Quat q;
};
/// Where a pose looks (an OpenXR pose, the game's camera node): its own -z.
constexpr Vec3 Ahead(const Quat& q) {
    return Rotate(q, {0.0f, 0.0f, -1.0f});
}

/// The quaternion of the axes `right`, `up`, `back` (orthonormal, right handed: right x up =
/// back), i.e. the one that takes +x, +y, +z to them.
inline Quat FromAxes(const Vec3& right, const Vec3& up, const Vec3& back) {
    const float m00 = right.x, m10 = right.y, m20 = right.z;
    const float m01 = up.x, m11 = up.y, m21 = up.z;
    const float m02 = back.x, m12 = back.y, m22 = back.z;
    const float trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return Normalized(q);
}

/// The game's scene camera in its world: the eye, and `q` taking the camera's axes (+x right,
/// +y up, -z ahead) to the world's.
struct GameCamera {
    Vec3 eye;
    Quat q;
};

/// The scene camera from the scene node's three vectors (0xE14B90 eye, 0xE14B9C look-at,
/// 0xE14BA8 up; the game's own look-at camera). Looking straight along `up`: any level side.
inline GameCamera CameraFromLookAt(const Vec3& eye, const Vec3& look_at, const Vec3& up) {
    const Vec3 ahead = Unit(look_at - eye);
    Vec3 right = Cross(ahead, up);
    if (!(Length(right) > 1e-6f)) {
        right = Cross(ahead, std::abs(ahead.y) < 0.9f ? Vec3{0.0f, 1.0f, 0.0f}
                                                      : Vec3{1.0f, 0.0f, 0.0f});
    }
    right = Unit(right, {1.0f, 0.0f, 0.0f});
    const Vec3 true_up = Cross(right, ahead);
    return {eye, FromAxes(right, true_up, ahead * -1.0f)};
}

/// The rest pose in the room (Layout::RestPose as immersive.cpp keeps it: the head's position
/// and its heading, yaw only).
struct Rest {
    Vec3 position;
    Quat orientation;
};

/// Everything the transform needs for one moment.
struct Frame {
    Rest rest;
    GameCamera camera;
    /// The leaning scale mode 3 moves the game camera by: game units per metre of the room.
    float units_per_metre = 1.0f;
};

// ---- room <-> rest ----

constexpr Pose RoomToRest(const Rest& rest, const Pose& room) {
    const Quat inv = Conj(rest.orientation);
    return {Rotate(inv, room.p - rest.position), Mul(inv, room.q)};
}
constexpr Pose RestToRoom(const Rest& rest, const Pose& in_rest) {
    return {rest.position + Rotate(rest.orientation, in_rest.p), Mul(rest.orientation, in_rest.q)};
}

// ---- rest <-> the game's world ----

/// Where a point of the rest frame is from the scene's eye, in the world's axes (game units).
/// This needs no absolute position, so it is what the shot's start is made from (the eye itself
/// is the player plus the eye offset, both known to the stubs at the spawn's own frame).
constexpr Vec3 RestToEyeOffset(const GameCamera& camera, float units_per_metre, const Vec3& p) {
    return Rotate(camera.q, p * units_per_metre);
}
/// A direction of the rest frame in the world.
constexpr Vec3 RestToWorldDirection(const GameCamera& camera, const Vec3& d) {
    return Rotate(camera.q, d);
}
constexpr Vec3 WorldToRestDirection(const GameCamera& camera, const Vec3& d) {
    return Rotate(Conj(camera.q), d);
}
constexpr Pose RestToWorld(const GameCamera& camera, float units_per_metre, const Pose& in_rest) {
    return {camera.eye + RestToEyeOffset(camera, units_per_metre, in_rest.p),
            Mul(camera.q, in_rest.q)};
}
/// `units_per_metre` must be above 0.
constexpr Pose WorldToRest(const GameCamera& camera, float units_per_metre, const Pose& world) {
    const Quat inv = Conj(camera.q);
    return {Rotate(inv, world.p - camera.eye) * (1.0f / units_per_metre), Mul(inv, world.q)};
}

// ---- room <-> the game's world ----

constexpr Pose RoomToWorld(const Frame& f, const Pose& room) {
    return RestToWorld(f.camera, f.units_per_metre, RoomToRest(f.rest, room));
}
constexpr Pose WorldToRoom(const Frame& f, const Pose& world) {
    return RestToRoom(f.rest, WorldToRest(f.camera, f.units_per_metre, world));
}

// ---- the rail (air battles) ----

/// The rail frame's origin in the world, from the player's two transform blocks: his parent
/// has the rail's origin (and a small rotation of its own, `parent_q` = world q * conj(local q),
/// KiuHooks::ParentRotation). On land the player has no parent and this is (0, 0, 0).
constexpr Vec3 RailOrigin(const Vec3& player_world, const Vec3& player_local,
                          const Quat& parent_q) {
    return player_world - Rotate(parent_q, player_local);
}
constexpr Vec3 WorldToRail(const Vec3& world, const Vec3& origin) {
    return world - origin;
}
constexpr Vec3 RailToWorld(const Vec3& local, const Vec3& origin) {
    return local + origin;
}

// ---- the game's own aim (the reticle) ----
//
// The game keeps its aim as a reticle pixel (the aim object's +0xA4, +0xA8, in a screen of
// +0xE0 by +0xE8 pixels with its centre at +0xE4, +0xEC) and, each frame, puts a point in the
// world on the scene camera's ray through that pixel: at a fixed depth along the camera's view
// (500.6 units in a land battle, 1500 in the air) or on what is under the reticle. The pixel of
// a world point is the scene projection's (its angle and shape are read from the game, never
// assumed: a widescreen build of the game has another shape):
//     x = cx + (w / 2) * X / (Z tan(fov / 2) aspect),   y = cy - (h / 2) * Y / (Z tan(fov / 2))
// with X, Y, Z the point from the eye along the camera's right, up and view. (V: read back
// from the running game, land and air, KIU_HOOKS.md.)

/// The scene camera's projection as the game has it (0xE14BC4 the angle up and down, radians;
/// 0xE14BC8 the width over the height).
struct Projection {
    float fov_y = 0.0f;
    float aspect = 0.0f;
    bool Ok() const {
        return std::isfinite(fov_y) && std::isfinite(aspect) && fov_y > 0.05f && fov_y < 3.0f &&
               aspect > 0.2f && aspect < 10.0f;
    }
};
/// The reticle's screen (the aim object's +0xE0 w, +0xE8 h, +0xE4 cx, +0xEC cy).
struct ReticleScreen {
    float w = 0.0f, h = 0.0f, cx = 0.0f, cy = 0.0f;
    bool Ok() const {
        return w > 2.0f && h > 2.0f && w < 4096.0f && h < 4096.0f && cx > 0.0f && cx < w &&
               cy > 0.0f && cy < h;
    }
};
struct Pixel {
    float x = 0.0f, y = 0.0f;
};

/// The shape (width over height) the game AIMS with, measured from the game's own aim: the
/// scene projection's field is not it on every build of the game (the C-Stick & Gyro mod's
/// widescreen builds leave 5:3 there and aim, and draw, 16:9 or 21:9 wide: V on the 21:9 build).
/// Each reading is the reticle's pixel and the point the game has put in the world for it: the
/// point's X / (Z tan(fov / 2)) over the pixel's share of the half width is the shape. Only
/// readings with the reticle well off the middle and not moving count (the point is the game
/// frame's, the pixel may be newer); they are eased, so one odd reading does little.
class AimShape {
public:
    static constexpr float kMinOffPx = 16.0f;    ///< the reticle at least this far from the middle
    static constexpr float kMaxMoveShare = 0.005f; ///< and moved less than this share of that
    static constexpr float kMin = 0.8f, kMax = 4.0f;
    static constexpr float kEase = 0.2f;

    void Update(const GameCamera& camera, float fov_y, const ReticleScreen& screen,
                const Pixel& pixel, const Vec3& aim_point) {
        const float off = pixel.x - screen.cx;
        const bool moved = have_last_ && std::abs(pixel.x - last_x_) > kMaxMoveShare * std::abs(off);
        last_x_ = pixel.x;
        have_last_ = true;
        if (moved || !(std::abs(off) >= kMinOffPx) || !screen.Ok() || !(fov_y > 0.05f) ||
            !(fov_y < 3.0f)) {
            return;
        }
        const Vec3 v = Rotate(Conj(camera.q), aim_point - camera.eye);
        if (!(-v.z > 1.0f)) {
            return;
        }
        const float sample =
            (v.x / (-v.z * std::tan(fov_y * 0.5f))) / (off / (screen.w * 0.5f));
        if (!std::isfinite(sample) || sample < kMin || sample > kMax) {
            return;
        }
        value_ = count_ == 0 ? sample : value_ + kEase * (sample - value_);
        ++count_;
    }
    /// The shape to aim with: the measured one, else `fallback`.
    float Get(float fallback) const {
        return count_ > 0 ? value_ : fallback;
    }
    int Count() const {
        return count_;
    }
    void Reset() {
        *this = {};
    }

private:
    float value_ = 0.0f;
    int count_ = 0;
    float last_x_ = 0.0f;
    bool have_last_ = false;
};

/// How far a world point is in front of the camera, along its view (below 0: behind it).
constexpr float CameraDepth(const GameCamera& camera, const Vec3& world) {
    return Dot(world - camera.eye, Ahead(camera.q));
}

/// The reticle pixel of a world point. `ok` false: the point is not in front of the camera
/// (the pixel then is that of its direction mirrored forward: on the right side of the screen
/// for a point behind to the right, far outside it).
inline Pixel WorldToReticle(const GameCamera& camera, const Projection& projection,
                            const ReticleScreen& screen, const Vec3& world, bool* ok = nullptr) {
    const Vec3 v = Rotate(Conj(camera.q), world - camera.eye); // camera axes: -z ahead
    const float t = std::tan(projection.fov_y * 0.5f);
    const bool ahead = -v.z > 1e-3f;
    if (ok) {
        *ok = ahead;
    }
    const float depth = ahead ? -v.z : 1e-3f;
    return {screen.cx + screen.w * 0.5f * v.x / (depth * t * projection.aspect),
            screen.cy - screen.h * 0.5f * v.y / (depth * t)};
}

/// The world direction (unit) the camera sees a reticle pixel in.
inline Vec3 ReticleToDirection(const GameCamera& camera, const Projection& projection,
                               const ReticleScreen& screen, const Pixel& pixel) {
    const float t = std::tan(projection.fov_y * 0.5f);
    const Vec3 v{(pixel.x - screen.cx) / (screen.w * 0.5f) * t * projection.aspect,
                 -(pixel.y - screen.cy) / (screen.h * 0.5f) * t, -1.0f};
    return Rotate(camera.q, Unit(v));
}

/// Where a ray (world; `direction` unit) meets the plane `depth` in front of the camera. A ray
/// that does not reach that plane going forward (it runs along it or away from it) gives its
/// point `depth` along itself: a direction to aim in all the same.
inline Vec3 RayAtCameraDepth(const GameCamera& camera, const Vec3& origin, const Vec3& direction,
                             float depth) {
    const Vec3 ahead = Ahead(camera.q);
    const float along = Dot(direction, ahead);
    const float have = Dot(origin - camera.eye, ahead);
    if (along > 1e-3f && depth > have) {
        return origin + direction * ((depth - have) / along);
    }
    return origin + direction * (depth > 1.0f ? depth : 1.0f);
}

/// The aim through the game's camera: the reticle pixel that makes the game aim where the
/// hand's ray points. The ray (world) is followed to the depth the game's own aim point has now
/// (`aim_depth`: its fixed range, or what the reticle is on; at least kAimDepthMin), and that
/// point is seen through the scene camera. `ok` false: the point is behind the camera (the
/// hand points out of the game's view to the back).
inline constexpr float kAimDepthMin = 5.0f;
inline Pixel AimPixel(const GameCamera& camera, const Projection& projection,
                      const ReticleScreen& screen, const Vec3& ray_origin,
                      const Vec3& ray_direction, float aim_depth, bool* ok = nullptr,
                      Vec3* point = nullptr) {
    const float depth = std::isfinite(aim_depth) && aim_depth > kAimDepthMin ? aim_depth
                                                                             : kAimDepthMin;
    const Vec3 hit = RayAtCameraDepth(camera, ray_origin, Unit(ray_direction), depth);
    if (point) {
        *point = hit;
    }
    return WorldToReticle(camera, projection, screen, hit, ok);
}

// ---- the game's shots ----
//
// A shot's quaternion applied to +z is its direction. The game makes its aim quaternion from
// the direction to its aim point with the world's up (its "look" rotation: z the direction, x
// = up x z, y = z x x), then each bullet adds its own scatter and the weapon's tilt in that
// quaternion's own axes. The shot stub's "absolute" mode gives a bullet
//     q' = wanted * conj(base) * q
// with `base` the game's aim quaternion before scatter (the stub reads it at the spawn) and
// `wanted` the same kind of quaternion for the hand's ray: every bullet keeps its own scatter
// and tilt, about the hand's ray instead of the game's aim.

/// The game's aim quaternion for a direction (need not be unit): +z onto it, the world's up
/// kept up. Straight up or down: any level side.
inline Quat ShotQuat(const Vec3& direction) {
    const Vec3 z = Unit(direction, {0.0f, 0.0f, 1.0f});
    Vec3 x = Cross({0.0f, 1.0f, 0.0f}, z);
    if (!(Length(x) > 1e-4f)) {
        x = {1.0f, 0.0f, 0.0f};
    }
    x = Unit(x, {1.0f, 0.0f, 0.0f});
    return FromAxes(x, Cross(z, x), z);
}
/// Where a shot's quaternion points.
constexpr Vec3 ShotDirection(const Quat& q) {
    return Rotate(q, {0.0f, 0.0f, 1.0f});
}

// ---- life size ----

/// The character's eyes above his feet on land (the game camera's own target height, 12.29;
/// KIU_HOOKS.md) and the height they are taken to be above the floor in the room.
inline constexpr float kEyeUnitsLand = 12.3f;
inline constexpr float kEyeMetresDefault = 1.6f;
/// What the scale is kept within (a tenth to ten times life size at the defaults).
inline constexpr float kUnitsPerMetreMin = 0.5f;
inline constexpr float kUnitsPerMetreMax = 100.0f;

/// Life size: the leaning scale at which the character's eye height in game units is the
/// player's in metres. Anything not usable gives the defaults' scale.
inline float LifeSizeUnitsPerMetre(float eye_units = kEyeUnitsLand,
                                   float eye_metres = kEyeMetresDefault) {
    if (!(eye_units > 0.0f) || !(eye_metres > 0.0f) || !std::isfinite(eye_units) ||
        !std::isfinite(eye_metres)) {
        return kEyeUnitsLand / kEyeMetresDefault;
    }
    const float s = eye_units / eye_metres;
    return s < kUnitsPerMetreMin ? kUnitsPerMetreMin
                                 : (s > kUnitsPerMetreMax ? kUnitsPerMetreMax : s);
}

} // namespace FrameXr::Input::KiuRoom
