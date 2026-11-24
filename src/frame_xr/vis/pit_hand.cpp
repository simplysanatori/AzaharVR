// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_xr/vis/pit_hand.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace FrameXr::Vis {

namespace {

#include "frame_xr/vis/pit_hand_data.inc"

constexpr float kDeg = 3.14159265358979f / 180.0f;

// ---- small maths ----

struct V3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
V3 operator+(V3 a, V3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
V3 operator-(V3 a, V3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
V3 operator*(V3 a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
float Dot(V3 a, V3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
V3 Cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float Length(V3 a) {
    return std::sqrt(Dot(a, a));
}
V3 Normal(V3 a) {
    const float l = Length(a);
    return l > 1e-12f ? a * (1.0f / l) : V3{1.0f, 0.0f, 0.0f};
}
V3 FromArray(const PitVec3& a) {
    return {a[0], a[1], a[2]};
}
PitVec3 ToArray(V3 a) {
    return {a.x, a.y, a.z};
}

PitQuat Mul(const PitQuat& a, const PitQuat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
PitQuat Conj(const PitQuat& q) {
    return {-q.x, -q.y, -q.z, q.w};
}
PitQuat Normalized(const PitQuat& q) {
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(l > 1e-12f)) {
        return {};
    }
    const float s = 1.0f / l;
    return {q.x * s, q.y * s, q.z * s, q.w * s};
}
V3 Rotate(const PitQuat& q, V3 v) {
    const V3 u{q.x, q.y, q.z};
    const V3 t = Cross(u, v) * 2.0f;
    return v + t * q.w + Cross(u, t);
}
/// Between two rotations the short way (normalized lerp: the steps here are small or eased).
PitQuat Nlerp(const PitQuat& a, const PitQuat& b, float t) {
    const float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    const float s = d < 0.0f ? -1.0f : 1.0f;
    return Normalized({a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t,
                       a.w + (s * b.w - a.w) * t});
}
/// The shortest rotation taking unit `from` to unit `to`.
PitQuat Between(V3 from, V3 to) {
    const float d = Dot(from, to);
    if (d < -0.99999f) {
        const V3 axis = Normal(std::abs(from.x) < 0.9f ? Cross(from, {1, 0, 0}) : Cross(from, {0, 1, 0}));
        return {axis.x, axis.y, axis.z, 0.0f};
    }
    const V3 c = Cross(from, to);
    return Normalized({c.x, c.y, c.z, 1.0f + d});
}
PitQuat QuatAt(const float* table, int i) {
    return {table[i * 4], table[i * 4 + 1], table[i * 4 + 2], table[i * 4 + 3]};
}
PitMat34 ToMat(const PitQuat& q, V3 t) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    return {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),     t.x,
            2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),     t.y,
            2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y), t.z};
}
PitQuat FromMat(const PitMat34& m) {
    const float m00 = m[0], m01 = m[1], m02 = m[2], m10 = m[4], m11 = m[5], m12 = m[6], m20 = m[8],
                m21 = m[9], m22 = m[10];
    const float t = m00 + m11 + m22;
    PitQuat q;
    if (t > 0.0f) {
        const float s = std::sqrt(t + 1.0f) * 2.0f;
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
V3 Place(const PitMat34& m) {
    return {m[3], m[7], m[11]};
}

// ---- the skeleton ----

/// The order joints are placed in: a parent before its children.
constexpr std::array<int, kPitHandJoints> kOrder{
    kPitWrist,          kPitPalm,
    kPitThumbMetacarpal, kPitThumbProximal,     kPitThumbDistal,  kPitThumbTip,
    kPitIndexMetacarpal, kPitIndexProximal,     kPitIndexIntermediate, kPitIndexDistal, kPitIndexTip,
    kPitMiddleMetacarpal, kPitMiddleProximal,   kPitMiddleIntermediate, kPitMiddleDistal, kPitMiddleTip,
    kPitRingMetacarpal, kPitRingProximal,       kPitRingIntermediate, kPitRingDistal, kPitRingTip,
    kPitLittleMetacarpal, kPitLittleProximal,   kPitLittleIntermediate, kPitLittleDistal, kPitLittleTip,
    kPitForearm,        kPitElbow};

V3 LocalT(int j) {
    return {kPitLocalT[j * 3], kPitLocalT[j * 3 + 1], kPitLocalT[j * 3 + 2]};
}

struct Frames {
    std::array<PitQuat, kPitHandJoints> q{};
    std::array<V3, kPitHandJoints> p{};
};

/// The joints placed from local rotations; the forearm and elbow as the pose has them.
Frames Forward(const PitHandPose& pose) {
    Frames f;
    for (const int j : kOrder) {
        const int parent = kPitParent[j];
        if (parent < 0) {
            f.q[j] = Normalized(pose.local[j]);
            f.p[j] = LocalT(j);
        } else {
            f.q[j] = Normalized(Mul(f.q[parent], pose.local[j]));
            f.p[j] = f.p[parent] + Rotate(f.q[parent], LocalT(j));
        }
    }
    return f;
}

PitHandPose BindPose() {
    PitHandPose p;
    for (int j = 0; j < kPitHandJoints; j++) {
        p.local[j] = QuatAt(kPitBindLocal, j);
    }
    return p;
}

struct Bind {
    PitHandPose pose;
    Frames frames;
    std::array<PitQuat, kPitHandJoints> inv_q{};
};
const Bind& TheBind() {
    static const Bind bind = [] {
        Bind b;
        b.pose = BindPose();
        b.frames = Forward(b.pose);
        for (int j = 0; j < kPitHandJoints; j++) {
            b.inv_q[j] = Conj(b.frames.q[j]);
        }
        return b;
    }();
    return bind;
}

// ---- the key poses ----

constexpr std::array<int, 3> kThumbJoints{kPitThumbMetacarpal, kPitThumbProximal, kPitThumbDistal};
constexpr std::array<int, 4> kIndexJoints{kPitIndexMetacarpal, kPitIndexProximal, kPitIndexIntermediate,
                                          kPitIndexDistal};
constexpr std::array<int, 12> kRestJoints{
    kPitMiddleMetacarpal, kPitMiddleProximal, kPitMiddleIntermediate, kPitMiddleDistal,
    kPitRingMetacarpal,   kPitRingProximal,   kPitRingIntermediate,   kPitRingDistal,
    kPitLittleMetacarpal, kPitLittleProximal, kPitLittleIntermediate, kPitLittleDistal};

// The keys' order in the tables (tools/kiu_hand/gen.py).
enum ThumbKey { kTOpen, kTRelaxed, kTFist, kTUp, kTHeldUp, kTHeldRest, kTHeldStick, kTHeldButtons };
enum IndexKey { kIOpen, kIRelaxed, kIFist, kIPoint, kIHeldOff, kIHeldTouch, kIHeldPull };
enum RestKey { kROpen, kRRelaxed, kRFist, kRHeldOpen, kRHeld };

PitQuat ThumbKeyQuat(int key, int i) {
    return QuatAt(kPitThumbKeys, key * 3 + i);
}
PitQuat IndexKeyQuat(int key, int i) {
    return QuatAt(kPitIndexKeys, key * 4 + i);
}
PitQuat RestKeyQuat(int key, int i) {
    return QuatAt(kPitRestKeys, key * 12 + i);
}

/// The joints no key moves, as the flat hand has them.
PitHandPose FlatPose() {
    PitHandPose p;
    for (int j = 0; j < kPitHandJoints; j++) {
        p.local[j] = QuatAt(kPitFlatLocal, j);
    }
    return p;
}

PitHandPose FromKeys(int thumb, int index, int rest) {
    PitHandPose p = FlatPose();
    for (std::size_t i = 0; i < kThumbJoints.size(); i++) {
        p.local[kThumbJoints[i]] = ThumbKeyQuat(thumb, static_cast<int>(i));
    }
    for (std::size_t i = 0; i < kIndexJoints.size(); i++) {
        p.local[kIndexJoints[i]] = IndexKeyQuat(index, static_cast<int>(i));
    }
    for (std::size_t i = 0; i < kRestJoints.size(); i++) {
        p.local[kRestJoints[i]] = RestKeyQuat(rest, static_cast<int>(i));
    }
    return p;
}

float Clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

// ---- the mesh ----

struct Mesh {
    std::vector<float> positions, normals, tangents, uvs;
    std::vector<float> weights; ///< 4 a vertex, summing to 1
    std::vector<uint32_t> indices, mirrored;
    std::vector<PitHandPart> parts;
};

const Mesh& TheMesh() {
    static const Mesh mesh = [] {
        Mesh m;
        const uint32_t n = kPitVertexCount;
        m.positions.resize(n * 3);
        m.normals.resize(n * 3);
        m.tangents.resize(n * 3);
        m.uvs.resize(n * 2);
        m.weights.resize(n * 4);
        for (uint32_t v = 0; v < n; v++) {
            V3 nrm{};
            for (int k = 0; k < 3; k++) {
                m.positions[v * 3 + k] = kPitPosMin[k] + kPitPosScale[k] * static_cast<float>(kPitPos[v * 3 + k]);
            }
            nrm = Normal({static_cast<float>(kPitNormal[v * 3]), static_cast<float>(kPitNormal[v * 3 + 1]),
                          static_cast<float>(kPitNormal[v * 3 + 2])});
            const V3 tan = Normal(Cross(std::abs(nrm.x) < 0.9f ? V3{1, 0, 0} : V3{0, 0, 1}, nrm));
            m.normals[v * 3] = nrm.x;
            m.normals[v * 3 + 1] = nrm.y;
            m.normals[v * 3 + 2] = nrm.z;
            m.tangents[v * 3] = tan.x;
            m.tangents[v * 3 + 1] = tan.y;
            m.tangents[v * 3 + 2] = tan.z;
            for (int k = 0; k < 4; k++) {
                m.weights[v * 4 + k] = static_cast<float>(kPitWeight[v * 4 + k]) / 255.0f;
            }
            // The palette: u the baked shade, v the material's band and how warm.
            const float shade = static_cast<float>(kPitShade[v]) / 255.0f;
            const float tint = static_cast<float>(kPitTint[v]) / 255.0f;
            m.uvs[v * 2] = (0.5f + shade * static_cast<float>(kPitPaletteWidth - 1)) /
                           static_cast<float>(kPitPaletteWidth);
            m.uvs[v * 2 + 1] = (static_cast<float>(kPitMaterial[v]) * 8.0f + 1.0f + 6.0f * tint) /
                               static_cast<float>(kPitPaletteHeight);
        }
        m.indices.resize(kPitTriangleCount * 3);
        m.mirrored.resize(kPitTriangleCount * 3);
        for (uint32_t t = 0; t < kPitTriangleCount; t++) {
            const uint32_t a = kPitIndex[t * 3], b = kPitIndex[t * 3 + 1], c = kPitIndex[t * 3 + 2];
            m.indices[t * 3] = a;
            m.indices[t * 3 + 1] = b;
            m.indices[t * 3 + 2] = c;
            m.mirrored[t * 3] = a;
            m.mirrored[t * 3 + 1] = c;
            m.mirrored[t * 3 + 2] = b;
        }
        for (const PitPartData& p : kPitParts) {
            m.parts.push_back({p.name, static_cast<PitHandMaterial>(p.material), p.first_index, p.index_count});
        }
        return m;
    }();
    return mesh;
}

PitRgb Mix(const PitRgb& a, const PitRgb& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
PitRgb Scaled(const PitRgb& a, float r, float g, float b) {
    return {Clamp01(a.r * r), Clamp01(a.g * g), Clamp01(a.b * b)};
}

PitMat4 MirrorX(const PitMat4& m) {
    // x of the result turned over (rows 0 of every column).
    PitMat4 o = m;
    for (int col = 0; col < 4; col++) {
        o[static_cast<std::size_t>(col * 4)] = -o[static_cast<std::size_t>(col * 4)];
    }
    return o;
}

} // namespace

int PitHandParent(int joint) {
    return joint >= 0 && joint < kPitHandJoints ? kPitParent[joint] : -1;
}

const PitHandPose& PitHandNamedPose(PitHandNamed name) {
    static const std::array<PitHandPose, static_cast<std::size_t>(PitHandNamed::Count)> poses = [] {
        std::array<PitHandPose, static_cast<std::size_t>(PitHandNamed::Count)> p;
        p[0] = BindPose();
        for (std::size_t i = 1; i < p.size(); i++) {
            const auto& k = kPitNamed[i - 1];
            p[i] = FromKeys(k[0], k[1], k[2]);
        }
        return p;
    }();
    const auto i = static_cast<std::size_t>(name);
    return poses[i < poses.size() ? i : 0];
}

std::string_view PitHandNamedLabel(PitHandNamed name) {
    constexpr std::array<std::string_view, static_cast<std::size_t>(PitHandNamed::Count)> labels{
        "bind", "open", "relaxed", "fist", "point", "thumb_up", "grip", "trigger", "grip_thumb_up",
        "grip_stick", "grip_buttons", "grip_open"};
    const auto i = static_cast<std::size_t>(name);
    return i < labels.size() ? labels[i] : std::string_view{};
}

PitHandCurls PitHandCurlsFromInputs(const PitHandInputs& in) {
    PitHandCurls c;
    const float trigger = Clamp01(in.trigger);
    const float grip = Clamp01(in.grip);
    c.index = in.trigger_touch ? 0.5f + 0.5f * trigger : 0.5f * trigger;
    const float rest = in.grip_touch ? 0.7f + 0.3f * grip : grip;
    c.middle = c.ring = c.little = rest;
    if (in.thumb_stick) {
        c.thumb_on = PitHandThumb::Stick;
    } else if (in.thumb_buttons) {
        c.thumb_on = PitHandThumb::Buttons;
    } else if (in.thumb_rest) {
        c.thumb_on = PitHandThumb::Rest;
    }
    c.thumb = c.thumb_on == PitHandThumb::Up ? 0.0f : 1.0f;
    c.spread = 1.0f - 0.25f * (c.index + c.middle + c.ring + c.little);
    return c;
}

PitHandPose PitHandPoseFromCurls(const PitHandCurls& curls, PitHandGrasp grasp) {
    PitHandPose p = FlatPose();
    const bool held = grasp == PitHandGrasp::Held;
    const float index = Clamp01(curls.index);
    const std::array<float, 3> rest{Clamp01(curls.middle), Clamp01(curls.ring), Clamp01(curls.little)};
    const float thumb = Clamp01(curls.thumb);
    // The index finger: three keys, the middle one at a curl of a half.
    const int i0 = held ? kIHeldOff : kIPoint, i1 = held ? kIHeldTouch : kIRelaxed,
              i2 = held ? kIHeldPull : kIFist;
    for (std::size_t i = 0; i < kIndexJoints.size(); i++) {
        const int k = static_cast<int>(i);
        p.local[kIndexJoints[i]] = index < 0.5f
                                       ? Nlerp(IndexKeyQuat(i0, k), IndexKeyQuat(i1, k), index * 2.0f)
                                       : Nlerp(IndexKeyQuat(i1, k), IndexKeyQuat(i2, k), index * 2.0f - 1.0f);
    }
    // The other three, each by its own curl.
    const int r0 = held ? kRHeldOpen : kRRelaxed, r1 = held ? kRHeld : kRFist;
    for (std::size_t i = 0; i < kRestJoints.size(); i++) {
        const int k = static_cast<int>(i);
        p.local[kRestJoints[i]] = Nlerp(RestKeyQuat(r0, k), RestKeyQuat(r1, k), rest[i / 4]);
    }
    // The thumb.
    const float closed = (rest[0] + rest[1] + rest[2]) / 3.0f;
    for (std::size_t i = 0; i < kThumbJoints.size(); i++) {
        const int k = static_cast<int>(i);
        PitQuat q;
        if (held) {
            const int on = curls.thumb_on == PitHandThumb::Stick     ? kTHeldStick
                           : curls.thumb_on == PitHandThumb::Buttons ? kTHeldButtons
                                                                     : kTHeldRest;
            q = Nlerp(ThumbKeyQuat(kTHeldUp, k), ThumbKeyQuat(on, k), thumb);
        } else {
            const PitQuat up = Nlerp(ThumbKeyQuat(kTOpen, k), ThumbKeyQuat(kTUp, k), closed);
            const PitQuat down = Nlerp(ThumbKeyQuat(kTRelaxed, k), ThumbKeyQuat(kTFist, k), closed);
            q = Nlerp(up, down, thumb);
        }
        p.local[kThumbJoints[i]] = q;
    }
    return p;
}

PitHandPose PitHandBlend(const PitHandPose& a, const PitHandPose& b, float t) {
    PitHandPose p;
    for (int j = 0; j < kPitHandJoints; j++) {
        p.local[j] = Nlerp(a.local[j], b.local[j], t);
    }
    return p;
}

PitHandPose PitHandEase(const PitHandPose& previous, const PitHandPose& target, float dt_s, float tau_s) {
    if (!(tau_s > 1e-5f) || !(dt_s >= 0.0f)) {
        return target;
    }
    return PitHandBlend(previous, target, 1.0f - std::exp(-dt_s / tau_s));
}

bool PitHandPoseDiffers(const PitHandPose& a, const PitHandPose& b, float degrees) {
    // For a small angle the two rotations' difference is half the angle long (in radians):
    // exact enough where a cosine would round to one.
    const float limit = 0.5f * degrees * kDeg;
    for (int j = 0; j < kPitHandJoints; j++) {
        const PitQuat& p = a.local[j];
        const PitQuat& q = b.local[j];
        const float s = p.x * q.x + p.y * q.y + p.z * q.z + p.w * q.w < 0.0f ? -1.0f : 1.0f;
        const float dx = p.x - s * q.x, dy = p.y - s * q.y, dz = p.z - s * q.z, dw = p.w - s * q.w;
        if (!(dx * dx + dy * dy + dz * dz + dw * dw <= limit * limit)) {
            return true;
        }
    }
    return false;
}

PitHandPose PitHandPoseFromJoints(std::span<const PitJointPose, kPitXrJoints> joints, bool right_hand,
                                  float metacarpal_follow) {
    const Bind& bind = TheBind();
    // Every joint's orientation as seen from the wrist; a right hand's mirrored into a left's
    // (x turned over: a rotation's y and z change sign).
    std::array<PitQuat, kPitXrJoints> rel;
    const auto left = [right_hand](const PitQuat& q) {
        return right_hand ? PitQuat{q.x, -q.y, -q.z, q.w} : q;
    };
    const PitQuat wrist_inv = Conj(Normalized(left(joints[kPitWrist].orientation)));
    for (int j = 0; j < kPitXrJoints; j++) {
        rel[static_cast<std::size_t>(j)] = Mul(wrist_inv, Normalized(left(joints[static_cast<std::size_t>(j)].orientation)));
    }
    // On our skeleton: its wrist stays where arm space has it; each joint points as tracked.
    const float follow = Clamp01(metacarpal_follow);
    std::array<PitQuat, kPitHandJoints> world;
    world[kPitWrist] = bind.frames.q[kPitWrist];
    for (const int j : kOrder) {
        if (j == kPitWrist || j >= kPitXrJoints) {
            continue;
        }
        PitQuat q = Mul(world[kPitWrist], rel[static_cast<std::size_t>(j)]);
        const bool finger_metacarpal = j == kPitIndexMetacarpal || j == kPitMiddleMetacarpal ||
                                       j == kPitRingMetacarpal || j == kPitLittleMetacarpal;
        if (finger_metacarpal && follow < 1.0f) {
            q = Nlerp(bind.frames.q[j], q, follow);
        }
        world[static_cast<std::size_t>(j)] = Normalized(q);
    }
    PitHandPose p = bind.pose;
    for (int j = 0; j < kPitXrJoints; j++) {
        const int parent = kPitParent[j];
        if (parent >= 0) {
            p.local[j] = Normalized(Mul(Conj(world[static_cast<std::size_t>(parent)]), world[static_cast<std::size_t>(j)]));
        }
    }
    return p;
}

PitMat4 PitHandTrackedFit(const PitJointPose& wrist, bool right_hand, float scale) {
    const Bind& bind = TheBind();
    PitQuat q = Normalized(wrist.orientation);
    V3 at = FromArray(wrist.position);
    if (right_hand) { // into the mirrored space, where the hand is a left one
        q = {q.x, -q.y, -q.z, q.w};
        at.x = -at.x;
    }
    // arm -> the wrist joint's own frame -> the tracked wrist.
    const PitQuat r = Mul(q, bind.inv_q[kPitWrist]);
    const float s = kPitHandUnit * scale;
    const PitMat34 m = ToMat(r, at - Rotate(r, bind.frames.p[kPitWrist]) * s);
    PitMat4 o{};
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            o[static_cast<std::size_t>(col * 4 + row)] = m[static_cast<std::size_t>(row * 4 + col)] * s;
        }
        o[static_cast<std::size_t>(12 + row)] = m[static_cast<std::size_t>(row * 4 + 3)];
    }
    o[15] = 1.0f;
    return right_hand ? MirrorX(o) : o;
}

PitHandSkeleton PitHandEvaluate(const PitHandPose& pose, const PitVec3* elbow) {
    const Bind& bind = TheBind();
    Frames f = Forward(pose);
    // The forearm: at the wrist, its bind direction turned onto the line to the elbow.
    f.p[kPitForearm] = f.p[kPitWrist];
    PitQuat fq = bind.frames.q[kPitForearm];
    if (elbow) {
        const V3 to = FromArray(*elbow) - f.p[kPitWrist];
        if (Length(to) > 1e-5f) {
            const V3 from = Normal(bind.frames.p[kPitElbow] - bind.frames.p[kPitForearm]);
            fq = Mul(Between(from, Normal(to)), fq);
        }
    }
    f.q[kPitForearm] = Normalized(fq);
    f.p[kPitElbow] = f.p[kPitForearm] + Rotate(f.q[kPitForearm], LocalT(kPitElbow));
    f.q[kPitElbow] = f.q[kPitForearm];
    PitHandSkeleton s;
    for (int j = 0; j < kPitHandJoints; j++) {
        s.joint[j] = ToMat(f.q[j], f.p[j]);
        const PitQuat r = Mul(f.q[j], bind.inv_q[j]);
        s.bone[j] = ToMat(r, f.p[j] - Rotate(r, bind.frames.p[j]));
    }
    return s;
}

std::array<PitJointPose, kPitXrJoints> PitHandJointsOf(const PitHandSkeleton& s) {
    std::array<PitJointPose, kPitXrJoints> out;
    for (int j = 0; j < kPitXrJoints; j++) {
        const auto i = static_cast<std::size_t>(j);
        out[i].orientation = FromMat(s.joint[i]);
        out[i].position = ToArray(Place(s.joint[i]) * kPitHandUnit);
    }
    return out;
}

std::span<const PitHandPart> PitHandParts() {
    return TheMesh().parts;
}

uint32_t PitHandVertexCount() {
    return kPitVertexCount;
}

std::span<const uint32_t> PitHandIndices(bool mirrored) {
    const Mesh& m = TheMesh();
    return mirrored ? m.mirrored : m.indices;
}

PitHandBind PitHandBindData() {
    const Mesh& m = TheMesh();
    return {m.positions, m.normals, kPitBone, kPitWeight, kPitMaterial};
}

void PitHandSkin(const PitHandSkeleton& skeleton, bool mirrored, std::span<float> out) {
    const Mesh& m = TheMesh();
    const uint32_t n = std::min<uint32_t>(kPitVertexCount, static_cast<uint32_t>(out.size() / kPitVertexFloats));
    const float flip = mirrored ? -1.0f : 1.0f;
    const float* pos = m.positions.data();
    const float* nrm = m.normals.data();
    const float* tan = m.tangents.data();
    float* o = out.data();
    const float* uv = m.uvs.data();
    const float* w = m.weights.data();
    const uint8_t* bones = kPitBone;
    for (uint32_t v = 0; v < n; v++, pos += 3, nrm += 3, tan += 3, uv += 2, w += 4, bones += 4, o += kPitVertexFloats) {
        // The vertex's own matrix: its bones' blended (most hang on one or two; the weights
        // are in falling order, so the first zero ends them).
        const float* b = skeleton.bone[bones[0]].data();
        float blend[12];
        if (w[1] != 0.0f) {
            for (int i = 0; i < 12; i++) {
                blend[i] = w[0] * b[i];
            }
            for (int k = 1; k < 4 && w[k] != 0.0f; k++) {
                const float* bk = skeleton.bone[bones[k]].data();
                const float wk = w[k];
                for (int i = 0; i < 12; i++) {
                    blend[i] += wk * bk[i];
                }
            }
            b = blend;
        }
        const float nx = b[0] * nrm[0] + b[1] * nrm[1] + b[2] * nrm[2];
        const float ny = b[4] * nrm[0] + b[5] * nrm[1] + b[6] * nrm[2];
        const float nz = b[8] * nrm[0] + b[9] * nrm[1] + b[10] * nrm[2];
        const float len = nx * nx + ny * ny + nz * nz;
        const float inv = len > 1e-20f ? 1.0f / std::sqrt(len) : 0.0f;
        o[0] = flip * (b[0] * pos[0] + b[1] * pos[1] + b[2] * pos[2] + b[3]);
        o[1] = b[4] * pos[0] + b[5] * pos[1] + b[6] * pos[2] + b[7];
        o[2] = b[8] * pos[0] + b[9] * pos[1] + b[10] * pos[2] + b[11];
        o[3] = flip * nx * inv;
        o[4] = ny * inv;
        o[5] = nz * inv;
        // The tangent (the surface image is flat: it need only make a frame with the normal).
        o[6] = flip * (b[0] * tan[0] + b[1] * tan[1] + b[2] * tan[2]) * inv;
        o[7] = (b[4] * tan[0] + b[5] * tan[1] + b[6] * tan[2]) * inv;
        o[8] = (b[8] * tan[0] + b[9] * tan[1] + b[10] * tan[2]) * inv;
        o[9] = flip;
        o[10] = uv[0];
        o[11] = uv[1];
    }
}

PitHandColours PitHandDefaultColours() {
    PitHandColours c;
    const auto set = [&c](PitHandMaterial m, PitRgb lit, PitRgb warm, PitRgb shadow) {
        c.of[static_cast<std::size_t>(m)] = {lit, warm, shadow};
    };
    set(PitHandMaterial::Skin, {0.98f, 0.80f, 0.67f}, {0.97f, 0.63f, 0.55f}, {0.70f, 0.40f, 0.36f});
    set(PitHandMaterial::Wrap, {0.20f, 0.22f, 0.31f}, {0.20f, 0.22f, 0.31f}, {0.08f, 0.09f, 0.14f});
    set(PitHandMaterial::Leather, {0.37f, 0.23f, 0.15f}, {0.37f, 0.23f, 0.15f}, {0.16f, 0.09f, 0.06f});
    set(PitHandMaterial::Band, {0.82f, 0.57f, 0.26f}, {0.82f, 0.57f, 0.26f}, {0.45f, 0.26f, 0.10f});
    set(PitHandMaterial::Nail, {0.99f, 0.85f, 0.79f}, {0.99f, 0.85f, 0.79f}, {0.80f, 0.60f, 0.55f});
    set(PitHandMaterial::Thread, {0.80f, 0.66f, 0.44f}, {0.80f, 0.66f, 0.44f}, {0.45f, 0.33f, 0.20f});
    set(PitHandMaterial::Gold, {0.92f, 0.74f, 0.30f}, {0.92f, 0.74f, 0.30f}, {0.50f, 0.34f, 0.10f});
    set(PitHandMaterial::Strap, {0.30f, 0.20f, 0.12f}, {0.30f, 0.20f, 0.12f}, {0.12f, 0.08f, 0.05f});
    return c;
}

PitHandColours PitHandColoursFrom(const PitRgb& skin, const PitRgb& wrap, const PitRgb& leather,
                                  const PitRgb& band) {
    PitHandColours c = PitHandDefaultColours();
    const auto plain = [&c](PitHandMaterial m, const PitRgb& lit, float shadow) {
        c.of[static_cast<std::size_t>(m)] = {lit, lit, Scaled(lit, shadow, shadow * 0.95f, shadow)};
    };
    // Skin: rosier where warm, redder and darker in a fold (never grey).
    c.of[static_cast<std::size_t>(PitHandMaterial::Skin)] = {skin, Scaled(skin, 0.99f, 0.79f, 0.82f),
                                                              Scaled(skin, 0.71f, 0.50f, 0.54f)};
    c.of[static_cast<std::size_t>(PitHandMaterial::Nail)] = {
        Mix(skin, {1.0f, 0.93f, 0.92f}, 0.45f), Mix(skin, {1.0f, 0.93f, 0.92f}, 0.45f), Scaled(skin, 0.82f, 0.75f, 0.82f)};
    plain(PitHandMaterial::Wrap, wrap, 0.42f);
    plain(PitHandMaterial::Leather, leather, 0.43f);
    plain(PitHandMaterial::Strap, Scaled(leather, 0.82f, 0.86f, 0.80f), 0.42f);
    plain(PitHandMaterial::Band, band, 0.52f);
    return c;
}

PitRgb PitHandSampleColour(std::span<const uint8_t> rgba, uint32_t width, uint32_t height, float u0,
                           float v0, float u1, float v1) {
    const PitRgb fallback = PitHandDefaultColours().of[0].lit;
    if (width == 0 || height == 0 || rgba.size() < static_cast<std::size_t>(width) * height * 4) {
        return fallback;
    }
    const auto texel = [](float t, uint32_t n) {
        return static_cast<uint32_t>(std::clamp(t, 0.0f, 1.0f) * static_cast<float>(n - 1) + 0.5f);
    };
    const uint32_t x0 = texel(std::min(u0, u1), width), x1 = texel(std::max(u0, u1), width);
    const uint32_t y0 = texel(std::min(v0, v1), height), y1 = texel(std::max(v0, v1), height);
    double r = 0, g = 0, b = 0, count = 0;
    for (uint32_t y = y0; y <= y1; y++) {
        for (uint32_t x = x0; x <= x1; x++) {
            const uint8_t* p = &rgba[(static_cast<std::size_t>(y) * width + x) * 4];
            if (p[3] < 128) {
                continue;
            }
            r += p[0];
            g += p[1];
            b += p[2];
            count += 1;
        }
    }
    if (count < 1) {
        return fallback;
    }
    return {static_cast<float>(r / count / 255.0), static_cast<float>(g / count / 255.0),
            static_cast<float>(b / count / 255.0)};
}

PitRgb PitHandColourAt(const PitHandColours& colours, float u, float v) {
    const float shade = Clamp01((u * static_cast<float>(kPitPaletteWidth) - 0.5f) /
                                static_cast<float>(kPitPaletteWidth - 1));
    const float row = v * static_cast<float>(kPitPaletteHeight);
    const int material = std::clamp(static_cast<int>(row / 8.0f), 0, static_cast<int>(PitHandMaterial::Count) - 1);
    const float tint = Clamp01((row - static_cast<float>(material) * 8.0f - 1.0f) / 6.0f);
    const auto& e = colours.of[static_cast<std::size_t>(material)];
    return Mix(e.shadow, Mix(e.lit, e.warm, tint), shade);
}

std::vector<uint8_t> PitHandPalette(const PitHandColours& colours) {
    std::vector<uint8_t> out(static_cast<std::size_t>(kPitPaletteWidth) * kPitPaletteHeight * 4);
    for (uint32_t y = 0; y < kPitPaletteHeight; y++) {
        for (uint32_t x = 0; x < kPitPaletteWidth; x++) {
            const PitRgb c = PitHandColourAt(colours, (static_cast<float>(x) + 0.5f) / static_cast<float>(kPitPaletteWidth),
                                             (static_cast<float>(y) + 0.5f) / static_cast<float>(kPitPaletteHeight));
            uint8_t* p = &out[(static_cast<std::size_t>(y) * kPitPaletteWidth + x) * 4];
            p[0] = static_cast<uint8_t>(Clamp01(c.r) * 255.0f + 0.5f);
            p[1] = static_cast<uint8_t>(Clamp01(c.g) * 255.0f + 0.5f);
            p[2] = static_cast<uint8_t>(Clamp01(c.b) * 255.0f + 0.5f);
            p[3] = 255;
        }
    }
    return out;
}

std::vector<uint8_t> PitHandSurface() {
    // Roughness per material: skin, wrap, leather, band, nail, thread, gold, strap.
    constexpr std::array<uint8_t, 8> rough{200, 235, 190, 105, 120, 220, 90, 195};
    std::vector<uint8_t> out(static_cast<std::size_t>(kPitPaletteWidth) * kPitPaletteHeight * 4);
    for (uint32_t y = 0; y < kPitPaletteHeight; y++) {
        for (uint32_t x = 0; x < kPitPaletteWidth; x++) {
            uint8_t* p = &out[(static_cast<std::size_t>(y) * kPitPaletteWidth + x) * 4];
            p[0] = 128;
            p[1] = 128;
            p[2] = 255;
            p[3] = rough[std::min<std::size_t>(y / 8, rough.size() - 1)];
        }
    }
    return out;
}

PitHandHandle PitHandHeld() {
    return {{kPitHeld[0], kPitHeld[1], kPitHeld[2]}, {kPitHeld[3], kPitHeld[4], kPitHeld[5]}, kPitHeld[6]};
}

PitHandHandle PitHandFist() {
    return {{kPitFist[0], kPitFist[1], kPitFist[2]}, {kPitFist[3], kPitFist[4], kPitFist[5]}, kPitFist[6]};
}

std::vector<PitHandCapsule> PitHandCapsules(const PitHandSkeleton& s) {
    std::vector<PitHandCapsule> out;
    const auto at = [&s](int j) { return Place(s.joint[static_cast<std::size_t>(j)]); };
    const auto phalanges = [&](int first, int count) {
        // `count` bones from joint `first`; the last ends a tip's radius short of the tip.
        for (int i = 0; i < count; i++) {
            const int a = first + i, b = a + 1;
            V3 end = at(b);
            if (i == count - 1) {
                end = end - Normal(at(b) - at(a)) * kPitRadius[b];
            }
            out.push_back({ToArray(at(a)), ToArray(end), kPitRadius[a], kPitRadius[b], a});
        }
    };
    for (const int knuckle : {kPitIndexProximal, kPitMiddleProximal, kPitRingProximal, kPitLittleProximal}) {
        phalanges(knuckle, 3);
    }
    phalanges(kPitThumbProximal, 2);
    out.push_back({ToArray(at(kPitThumbMetacarpal)), ToArray(at(kPitThumbProximal)), kPitRadius[kPitThumbMetacarpal],
                   kPitRadius[kPitThumbProximal], kPitThumbMetacarpal});
    for (const int m : {kPitIndexMetacarpal, kPitMiddleMetacarpal, kPitRingMetacarpal, kPitLittleMetacarpal}) {
        out.push_back({ToArray(at(m)), ToArray(at(m + 1)), kPitRadius[m], kPitRadius[m], m});
    }
    return out;
}

PitHandSeat PitHandPanelSeat(bool inside) {
    const float side = inside ? -1.0f : 1.0f;
    return {{kPitSeatX, side * kPitSeatRadius, 0.0f}, {0.0f, side, 0.0f}, {1.0f, 0.0f, 0.0f},
            kPitSeatRadius, kPitSeatWidth, kPitSeatBands};
}

PitMat4 PitHandControllerFit(bool left_hand, float scale) {
    // aim = R (arm metres) + T for the left controller; a scale keeps the handle's centre.
    const float s = kPitHandUnit * scale;
    PitMat4 o{};
    V3 t{kPitControllerT[0], kPitControllerT[1], kPitControllerT[2]};
    const V3 handle{kPitControllerHandle[0], kPitControllerHandle[1], kPitControllerHandle[2]};
    t = handle + (t - handle) * scale;
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            o[static_cast<std::size_t>(col * 4 + row)] = kPitControllerR[row * 3 + col] * s;
        }
    }
    o[12] = t.x;
    o[13] = t.y;
    o[14] = t.z;
    o[15] = 1.0f;
    return left_hand ? o : MirrorX(o);
}

} // namespace FrameXr::Vis
