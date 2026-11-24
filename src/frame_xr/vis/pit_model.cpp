// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_xr/vis/pit_model.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace FrameXr::Vis {

namespace {

constexpr char kMagic[8] = {'P', 'I', 'T', 'H', 'A', 'N', 'D', '1'};
constexpr std::size_t kJointBytes = 4 + 4 + 4 + 3 * 4 + 9 * 4 + 4;
constexpr std::size_t kHeaderBytes = 8 + 8 * 4 + 4 * 4 + 3 * 4 + 7 * 4 + 16 * 4 + 3 * 4;

/// A cursor over the file that never reads past its end.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> data) : data_(data) {}
    bool Ok() const {
        return ok_;
    }
    std::size_t Left() const {
        return data_.size() - at_;
    }
    template <typename T>
    T Get() {
        T v{};
        if (!ok_ || Left() < sizeof(T)) {
            ok_ = false;
            return v;
        }
        std::memcpy(&v, data_.data() + at_, sizeof(T));
        at_ += sizeof(T);
        return v;
    }
    template <typename T>
    bool Copy(std::vector<T>& out, std::size_t count) {
        if (!ok_ || count > Left() / sizeof(T)) {
            ok_ = false;
            return false;
        }
        out.resize(count);
        if (count > 0) {
            std::memcpy(out.data(), data_.data() + at_, count * sizeof(T));
        }
        at_ += count * sizeof(T);
        return true;
    }

private:
    std::span<const uint8_t> data_;
    std::size_t at_ = 0;
    bool ok_ = true;
};

bool Finite(std::span<const float> values) {
    return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
}

struct V3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
V3 operator-(V3 a, V3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
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

using M3 = std::array<float, 9>; // row major

M3 Mul(const M3& a, const M3& b) {
    M3 o{};
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            o[static_cast<std::size_t>(r * 3 + c)] = a[static_cast<std::size_t>(r * 3)] * b[static_cast<std::size_t>(c)] +
                                                     a[static_cast<std::size_t>(r * 3 + 1)] * b[static_cast<std::size_t>(3 + c)] +
                                                     a[static_cast<std::size_t>(r * 3 + 2)] * b[static_cast<std::size_t>(6 + c)];
        }
    }
    return o;
}
M3 Transposed(const M3& a) {
    return {a[0], a[3], a[6], a[1], a[4], a[7], a[2], a[5], a[8]};
}
V3 Apply(const M3& m, V3 v) {
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z,
            m[6] * v.x + m[7] * v.y + m[8] * v.z};
}
M3 Rotation(const PitMat34& m) {
    return {m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]};
}
V3 Apply(const PitMat34& m, V3 v) {
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3], m[4] * v.x + m[5] * v.y + m[6] * v.z + m[7],
            m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11]};
}
constexpr M3 kIdentity3{1, 0, 0, 0, 1, 0, 0, 0, 1};
constexpr PitMat34 kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

/// A rotation `d` that leaves `pivot_from` at `pivot_to`.
PitMat34 About(const M3& d, V3 pivot_from, V3 pivot_to) {
    const V3 r = Apply(d, pivot_from);
    return {d[0], d[1], d[2], pivot_to.x - r.x, d[3], d[4], d[5], pivot_to.y - r.y,
            d[6], d[7], d[8], pivot_to.z - r.z};
}

/// How far the forearm slides from the hand as the wrist bends, over the wrist band's radius (times
/// the bend's sine), and the most it does, in metres.
constexpr float kPitBandSlide = 0.6f, kPitBandSlideMost = 0.03f;

/// The shortest rotation taking unit `from` to unit `to`.
M3 Between(V3 from, V3 to) {
    const V3 axis = Cross(from, to);
    const float s = Length(axis), c = Dot(from, to);
    if (s < 1e-6f) {
        if (c > 0.0f) {
            return kIdentity3;
        }
        // Opposite: half a turn about any axis square to `from`.
        V3 a = std::abs(from.x) < 0.9f ? Cross(from, {1, 0, 0}) : Cross(from, {0, 1, 0});
        const float l = Length(a);
        a = {a.x / l, a.y / l, a.z / l};
        return {2 * a.x * a.x - 1, 2 * a.x * a.y,     2 * a.x * a.z,     2 * a.x * a.y, 2 * a.y * a.y - 1,
                2 * a.y * a.z,     2 * a.x * a.z,     2 * a.y * a.z,     2 * a.z * a.z - 1};
    }
    const V3 k{axis.x / s, axis.y / s, axis.z / s};
    const float t = 1.0f - c;
    return {c + k.x * k.x * t,       k.x * k.y * t - k.z * s, k.x * k.z * t + k.y * s,
            k.y * k.x * t + k.z * s, c + k.y * k.y * t,       k.y * k.z * t - k.x * s,
            k.z * k.x * t - k.y * s, k.z * k.y * t + k.x * s, c + k.z * k.z * t};
}

bool Fail(PitModel& out, std::string& error, const char* why) {
    out = PitModel{};
    error = why;
    return false;
}

} // namespace

bool ParsePitModel(std::span<const uint8_t> file, PitModel& out, std::string& error) {
    out = PitModel{};
    error.clear();
    if (file.size() < kHeaderBytes || std::memcmp(file.data(), kMagic, sizeof(kMagic)) != 0) {
        return Fail(out, error, "not a hand file");
    }
    Reader r(file.subspan(sizeof(kMagic)));
    const auto version = r.Get<uint32_t>();
    const auto vertices = r.Get<uint32_t>();
    const auto body = r.Get<uint32_t>();
    const auto plate = r.Get<uint32_t>();
    const auto joints = r.Get<uint32_t>();
    const auto width = r.Get<uint32_t>();
    const auto height = r.Get<uint32_t>();
    r.Get<uint32_t>();
    if (version != 1) {
        return Fail(out, error, "a version this build does not read");
    }
    if (vertices < 3 || vertices > kPitModelMaxVertices) {
        return Fail(out, error, "vertex count out of range");
    }
    if (body == 0 || body % 3 != 0 || plate % 3 != 0 || body > kPitModelMaxIndices ||
        plate > kPitModelMaxIndices - body) {
        return Fail(out, error, "index count out of range");
    }
    if (joints == 0 || joints > kPitModelMaxJoints) {
        return Fail(out, error, "joint count out of range");
    }
    if (width == 0 || height == 0 || width > kPitModelMaxTexture || height > kPitModelMaxTexture) {
        return Fail(out, error, "texture size out of range");
    }
    // The whole size, before anything is allocated (64 bit: the limits above cannot overflow it).
    const uint64_t indices = uint64_t{body} + plate;
    const uint64_t pixels = uint64_t{width} * height * 4;
    const uint64_t need = kHeaderBytes + uint64_t{joints} * kJointBytes +
                          uint64_t{vertices} * (kPitVertexFloats * 4 + 8) + indices * 4 + pixels * 2;
    if (file.size() != need) {
        return Fail(out, error, "the file's size does not match its counts");
    }
    out.hand_length = r.Get<float>();
    out.forearm_radius = r.Get<float>();
    out.wrist_radius = r.Get<float>();
    r.Get<float>();
    for (float& v : out.elbow) {
        v = r.Get<float>();
    }
    for (float& v : out.handle.centre) {
        v = r.Get<float>();
    }
    for (float& v : out.handle.axis) {
        v = r.Get<float>();
    }
    out.handle.radius = r.Get<float>();
    for (float& v : out.fit) {
        v = r.Get<float>();
    }
    for (float& v : out.fit_pivot) {
        v = r.Get<float>();
    }
    const auto sane = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
    if (!sane(out.hand_length, 0.02f, 1.0f) || !sane(out.forearm_radius, 0.0f, 1.0f) ||
        !sane(out.wrist_radius, 0.0f, 1.0f) || !sane(out.handle.radius, 0.0f, 1.0f) || !Finite(out.elbow) ||
        !Finite(out.handle.centre) || !Finite(out.handle.axis) || !Finite(out.fit) || !Finite(out.fit_pivot)) {
        return Fail(out, error, "a number in the header is out of range");
    }
    // The fit: a rotation and a translation (its last row 0 0 0 1, its axes of length one).
    if (out.fit[3] != 0.0f || out.fit[7] != 0.0f || out.fit[11] != 0.0f || out.fit[15] != 1.0f) {
        return Fail(out, error, "the fit is not a placement");
    }
    for (int c = 0; c < 3; c++) {
        const V3 axis{out.fit[static_cast<std::size_t>(c * 4)], out.fit[static_cast<std::size_t>(c * 4 + 1)],
                      out.fit[static_cast<std::size_t>(c * 4 + 2)]};
        if (std::abs(Length(axis) - 1.0f) > 0.01f) {
            return Fail(out, error, "the fit is not a placement");
        }
    }
    out.joints.resize(joints);
    for (uint32_t j = 0; j < joints; j++) {
        PitModelJoint& joint = out.joints[j];
        joint.parent = r.Get<int32_t>();
        joint.role = r.Get<int32_t>();
        const auto flags = r.Get<uint32_t>();
        joint.relative = (flags & 1u) != 0;
        for (float& v : joint.position) {
            v = r.Get<float>();
        }
        for (float& v : joint.frame) {
            v = r.Get<float>();
        }
        joint.radius = r.Get<float>();
        if (!(joint.radius >= 0.0f && joint.radius <= 0.2f)) {
            return Fail(out, error, "a joint's thickness is out of range");
        }
        if (joint.parent < -1 || joint.parent >= static_cast<int>(j)) {
            return Fail(out, error, "a joint's parent does not come before it");
        }
        if (joint.role < -1 || joint.role >= kPitHandJoints) {
            return Fail(out, error, "a joint's role is unknown");
        }
        if (!Finite(joint.position) || !Finite(joint.frame)) {
            return Fail(out, error, "a joint is not finite");
        }
        // Its axes: of length one and square to one another, right handed.
        const M3 should_be_one = Mul(joint.frame, Transposed(joint.frame));
        for (std::size_t k = 0; k < 9; k++) {
            if (std::abs(should_be_one[k] - kIdentity3[k]) > 0.01f) {
                return Fail(out, error, "a joint's axes are not a rotation");
            }
        }
        const V3 x{joint.frame[0], joint.frame[3], joint.frame[6]}, y{joint.frame[1], joint.frame[4], joint.frame[7]},
            z{joint.frame[2], joint.frame[5], joint.frame[8]};
        if (Dot(Cross(x, y), z) < 0.9f) {
            return Fail(out, error, "a joint's axes are not a rotation");
        }
        for (uint32_t other = 0; other < j; other++) {
            if (joint.role >= 0 && out.joints[other].role == joint.role) {
                return Fail(out, error, "two joints share a role");
            }
        }
    }
    out.vertex_count = vertices;
    out.body_index_count = body;
    out.plate_index_count = plate;
    out.texture_width = width;
    out.texture_height = height;
    std::vector<uint8_t> skin;
    if (!r.Copy(out.vertices, std::size_t{vertices} * kPitVertexFloats) || !r.Copy(skin, std::size_t{vertices} * 8) ||
        !r.Copy(out.indices, static_cast<std::size_t>(indices)) || !r.Copy(out.colour, static_cast<std::size_t>(pixels)) ||
        !r.Copy(out.surface, static_cast<std::size_t>(pixels)) || !r.Ok() || r.Left() != 0) {
        return Fail(out, error, "the file ends early");
    }
    if (!Finite(out.vertices)) {
        return Fail(out, error, "a vertex is not finite");
    }
    out.bones.resize(std::size_t{vertices} * 4);
    out.weights.resize(std::size_t{vertices} * 4);
    for (uint32_t v = 0; v < vertices; v++) {
        float* p = &out.vertices[std::size_t{v} * kPitVertexFloats];
        // Within reach of the wrist, a normal and a tangent that can be made of length one.
        if (std::abs(p[0]) > 2.0f || std::abs(p[1]) > 2.0f || std::abs(p[2]) > 2.0f) {
            return Fail(out, error, "a vertex is too far away");
        }
        for (const int at : {3, 6}) {
            const float len = std::sqrt(p[at] * p[at] + p[at + 1] * p[at + 1] + p[at + 2] * p[at + 2]);
            if (!(len > 1e-6f)) {
                return Fail(out, error, "a vertex has no normal or tangent");
            }
            p[at] /= len;
            p[at + 1] /= len;
            p[at + 2] /= len;
        }
        p[9] = p[9] < 0.0f ? -1.0f : 1.0f;
        int sum = 0;
        for (int k = 0; k < 4; k++) {
            const uint8_t bone = skin[std::size_t{v} * 8 + static_cast<std::size_t>(k)];
            const uint8_t weight = skin[std::size_t{v} * 8 + 4 + static_cast<std::size_t>(k)];
            if (bone >= joints) {
                return Fail(out, error, "a vertex hangs on a joint the file does not have");
            }
            if (k > 0 && weight > skin[std::size_t{v} * 8 + 4 + static_cast<std::size_t>(k) - 1]) {
                return Fail(out, error, "a vertex's weights are not in falling order");
            }
            out.bones[std::size_t{v} * 4 + static_cast<std::size_t>(k)] = bone;
            out.weights[std::size_t{v} * 4 + static_cast<std::size_t>(k)] = weight;
            sum += weight;
        }
        if (sum != 255) {
            return Fail(out, error, "a vertex's weights do not sum to 255");
        }
    }
    for (const uint32_t i : out.indices) {
        if (i >= vertices) {
            return Fail(out, error, "an index is past the last vertex");
        }
    }
    return true;
}

bool LoadPitModel(const std::string& path, PitModel& out, std::string& error) {
    out = PitModel{};
    error.clear();
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return false; // no file: nothing to say
    }
    const std::streamoff size = in.tellg();
    // The largest file the limits allow, so a wrong file is not read into memory whole.
    constexpr uint64_t kMost = kHeaderBytes + uint64_t{kPitModelMaxJoints} * kJointBytes +
                               uint64_t{kPitModelMaxVertices} * (kPitVertexFloats * 4 + 8) +
                               uint64_t{kPitModelMaxIndices} * 4 +
                               uint64_t{kPitModelMaxTexture} * kPitModelMaxTexture * 8;
    if (size <= 0 || static_cast<uint64_t>(size) > kMost) {
        error = "the file's size is out of range";
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!in || in.gcount() != size) {
        error = "the file could not be read";
        return false;
    }
    return ParsePitModel(bytes, out, error);
}

namespace {

struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};
Quat FromMatrix(const M3& m) {
    const float t = m[0] + m[4] + m[8];
    Quat q;
    if (t > 0.0f) {
        const float s = std::sqrt(t + 1.0f) * 2.0f;
        q = {(m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s, 0.25f * s};
    } else if (m[0] > m[4] && m[0] > m[8]) {
        const float s = std::sqrt(1.0f + m[0] - m[4] - m[8]) * 2.0f;
        q = {0.25f * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s, (m[7] - m[5]) / s};
    } else if (m[4] > m[8]) {
        const float s = std::sqrt(1.0f + m[4] - m[0] - m[8]) * 2.0f;
        q = {(m[1] + m[3]) / s, 0.25f * s, (m[5] + m[7]) / s, (m[2] - m[6]) / s};
    } else {
        const float s = std::sqrt(1.0f + m[8] - m[0] - m[4]) * 2.0f;
        q = {(m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25f * s, (m[3] - m[1]) / s};
    }
    return q;
}
M3 ToMatrix(const Quat& q) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    return {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
            2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
            2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
}
/// Between two orientations the short way (t 0: a, 1: b).
M3 Mix(const M3& a, const M3& b, float t) {
    if (t >= 1.0f) {
        return b;
    }
    const Quat p = FromMatrix(a), q = FromMatrix(b);
    const float s = p.x * q.x + p.y * q.y + p.z * q.z + p.w * q.w < 0.0f ? -1.0f : 1.0f;
    Quat o{p.x + (s * q.x - p.x) * t, p.y + (s * q.y - p.y) * t, p.z + (s * q.z - p.z) * t,
           p.w + (s * q.w - p.w) * t};
    const float l = std::sqrt(o.x * o.x + o.y * o.y + o.z * o.z + o.w * o.w);
    if (!(l > 1e-9f)) {
        return b;
    }
    o = {o.x / l, o.y / l, o.z / l, o.w / l};
    return ToMatrix(o);
}

/// Which finger a role belongs to (0 the thumb to 4 the little finger), or -1: the joints a
/// finger closes with (not a finger's metacarpal, which is the palm's).
int FingerOf(int role) {
    if (role >= kPitThumbMetacarpal && role <= kPitThumbTip) {
        return 0;
    }
    if (role >= kPitIndexMetacarpal && role <= kPitLittleTip) {
        const int k = (role - kPitIndexMetacarpal) % 5;
        return k == 0 ? -1 : 1 + (role - kPitIndexMetacarpal) / 5;
    }
    return -1;
}

} // namespace

std::vector<PitMat34> PitModelEvaluate(const PitModel& model, const PitHandPose* pose, const PitVec3* elbow,
                                       bool clear_handle) {
    std::vector<PitMat34> bones(model.joints.size(), kIdentity);
    PitHandSkeleton ours{}, flat{};
    if (pose) {
        ours = PitHandEvaluate(*pose);
        flat = PitHandEvaluate(PitHandFlatPose());
    }
    // The wrist: where the hand's joints stay, and what the forearm turns about.
    V3 wrist{};
    for (const PitModelJoint& j : model.joints) {
        if (j.role == kPitWrist) {
            wrist = {j.position[0], j.position[1], j.position[2]};
        }
    }
    M3 forearm = kIdentity3;
    V3 slid{};
    if (elbow) {
        const V3 from = V3{model.elbow[0], model.elbow[1], model.elbow[2]} - wrist;
        const V3 to = V3{(*elbow)[0], (*elbow)[1], (*elbow)[2]} - wrist;
        const float lf = Length(from), lt = Length(to);
        if (lf > 1e-5f && lt > 1e-5f && std::isfinite(lt)) {
            const V3 f{from.x / lf, from.y / lf, from.z / lf}, t{to.x / lt, to.y / lt, to.z / lt};
            const float bent = Dot(f, t) < 0.0f ? 1.0f : Length(Cross(f, t));
            // Toward the elbow as the wrist bends, so the wrist band's near edge stays off the hand.
            const float back = std::min(kPitBandSlide * model.wrist_radius * bent, kPitBandSlideMost);
            slid = {t.x * back, t.y * back, t.z * back};
            forearm = Between({from.x / lf, from.y / lf, from.z / lf}, {to.x / lt, to.y / lt, to.z / lt});
        }
    }
    // Every joint placed, each finger closed `closed[finger]` of the way from the flat hand to
    // the pose (1: the pose itself).
    std::vector<M3> turn(model.joints.size(), kIdentity3);
    std::vector<V3> at_now(model.joints.size());
    const auto place = [&](const std::array<float, 5>& closed) {
        for (std::size_t i = 0; i < model.joints.size(); i++) {
            const PitModelJoint& j = model.joints[i];
            const V3 at{j.position[0], j.position[1], j.position[2]};
            const bool rooted = j.parent < 0;
            const std::size_t parent = rooted ? 0 : static_cast<std::size_t>(j.parent);
            if (j.role == kPitForearm || j.role == kPitElbow) {
                turn[i] = forearm;
                bones[i] = About(forearm, wrist, {wrist.x + slid.x, wrist.y + slid.y, wrist.z + slid.z});
            } else if (j.role < 0 || !pose) {
                // With its parent (without a pose only the forearm moves).
                turn[i] = rooted ? kIdentity3 : turn[parent];
                bones[i] = rooted ? kIdentity : bones[parent];
            } else {
                const M3 open = Rotation(flat.joint[static_cast<std::size_t>(j.role)]);
                M3 now = Rotation(ours.joint[static_cast<std::size_t>(j.role)]);
                const int finger = FingerOf(j.role);
                if (finger >= 0 && closed[static_cast<std::size_t>(finger)] < 1.0f) {
                    now = Mix(open, now, closed[static_cast<std::size_t>(finger)]);
                }
                turn[i] = Mul(now, Transposed(j.relative ? open : j.frame));
                // The hand does not go with the forearm: the wrist, and a joint whose parent is
                // the forearm or nothing, stay where they are; any other rides on its parent.
                const bool stays = rooted || j.role == kPitWrist || model.joints[parent].role == kPitForearm ||
                                   model.joints[parent].role == kPitElbow;
                bones[i] = About(turn[i], at, stays ? at : Apply(bones[parent], at));
            }
            at_now[i] = Apply(bones[i], at);
        }
    };
    std::array<float, 5> closed{1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    place(closed);
    if (!pose || !clear_handle || !(model.handle.radius > 0.0f)) {
        return bones;
    }
    // The controller's handle is in the palm: a finger that would close through it is opened
    // until it lies on it (each finger by itself, halving the step seven times).
    const V3 centre{model.handle.centre[0], model.handle.centre[1], model.handle.centre[2]};
    V3 axis{model.handle.axis[0], model.handle.axis[1], model.handle.axis[2]};
    const float axis_length = Length(axis);
    if (!(axis_length > 1e-5f)) {
        return bones;
    }
    axis = {axis.x / axis_length, axis.y / axis_length, axis.z / axis_length};
    const auto inside = [&](int finger) {
        for (std::size_t i = 0; i < model.joints.size(); i++) {
            const PitModelJoint& j = model.joints[i];
            if (FingerOf(j.role) != finger || j.parent < 0 ||
                FingerOf(model.joints[static_cast<std::size_t>(j.parent)].role) != finger) {
                continue;
            }
            // The bone from its parent to this joint, as a rounded cone.
            const std::size_t parent = static_cast<std::size_t>(j.parent);
            const float thick_a = model.joints[parent].radius;
            const float thick_b = j.radius > 0.0f ? j.radius : thick_a * 0.85f;
            for (const float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
                const V3 p{at_now[parent].x + (at_now[i].x - at_now[parent].x) * t,
                           at_now[parent].y + (at_now[i].y - at_now[parent].y) * t,
                           at_now[parent].z + (at_now[i].z - at_now[parent].z) * t};
                const V3 q = p - centre;
                const float along = Dot(q, axis);
                const V3 off{q.x - axis.x * along, q.y - axis.y * along, q.z - axis.z * along};
                if (Length(off) < model.handle.radius + (thick_a + (thick_b - thick_a) * t) * kPitModelPress) {
                    return true;
                }
            }
        }
        return false;
    };
    for (int finger = 0; finger < 5; finger++) {
        if (!inside(finger)) {
            continue;
        }
        float lo = 0.0f, hi = 1.0f;
        for (int step = 0; step < 7; step++) {
            closed[static_cast<std::size_t>(finger)] = 0.5f * (lo + hi);
            place(closed);
            (inside(finger) ? hi : lo) = closed[static_cast<std::size_t>(finger)];
        }
        closed[static_cast<std::size_t>(finger)] = lo;
        place(closed);
    }
    return bones;
}

void PitModelSkin(const PitModel& model, std::span<const PitMat34> bones, std::span<float> out) {
    const std::size_t n = std::min<std::size_t>(model.vertex_count, out.size() / kPitVertexFloats);
    if (bones.size() < model.joints.size()) {
        return;
    }
    const float* in = model.vertices.data();
    const uint8_t* joint = model.bones.data();
    const uint8_t* weight = model.weights.data();
    float* o = out.data();
    for (std::size_t v = 0; v < n; v++, in += kPitVertexFloats, joint += 4, weight += 4, o += kPitVertexFloats) {
        const float* b = bones[joint[0]].data();
        float blend[12];
        if (weight[1] != 0) {
            const float w0 = static_cast<float>(weight[0]) * (1.0f / 255.0f);
            for (int i = 0; i < 12; i++) {
                blend[i] = w0 * b[i];
            }
            for (int k = 1; k < 4 && weight[k] != 0; k++) {
                const float* bk = bones[joint[k]].data();
                const float wk = static_cast<float>(weight[k]) * (1.0f / 255.0f);
                for (int i = 0; i < 12; i++) {
                    blend[i] += wk * bk[i];
                }
            }
            b = blend;
        }
        const float nx = b[0] * in[3] + b[1] * in[4] + b[2] * in[5];
        const float ny = b[4] * in[3] + b[5] * in[4] + b[6] * in[5];
        const float nz = b[8] * in[3] + b[9] * in[4] + b[10] * in[5];
        const float nl = nx * nx + ny * ny + nz * nz;
        const float ni = nl > 1e-20f ? 1.0f / std::sqrt(nl) : 0.0f;
        const float tx = b[0] * in[6] + b[1] * in[7] + b[2] * in[8];
        const float ty = b[4] * in[6] + b[5] * in[7] + b[6] * in[8];
        const float tz = b[8] * in[6] + b[9] * in[7] + b[10] * in[8];
        const float tl = tx * tx + ty * ty + tz * tz;
        const float ti = tl > 1e-20f ? 1.0f / std::sqrt(tl) : 0.0f;
        o[0] = b[0] * in[0] + b[1] * in[1] + b[2] * in[2] + b[3];
        o[1] = b[4] * in[0] + b[5] * in[1] + b[6] * in[2] + b[7];
        o[2] = b[8] * in[0] + b[9] * in[1] + b[10] * in[2] + b[11];
        o[3] = nx * ni;
        o[4] = ny * ni;
        o[5] = nz * ni;
        o[6] = tx * ti;
        o[7] = ty * ti;
        o[8] = tz * ti;
        o[9] = in[9];
        o[10] = in[10];
        o[11] = in[11];
    }
}

PitMat4 PitModelControllerFit(const PitModel& model, bool left_hand, float scale) {
    PitMat4 o = model.fit;
    for (std::size_t c = 0; c < 3; c++) {
        for (std::size_t r = 0; r < 3; r++) {
            o[c * 4 + r] *= scale;
        }
    }
    for (std::size_t r = 0; r < 3; r++) {
        o[12 + r] = model.fit_pivot[r] + (model.fit[12 + r] - model.fit_pivot[r]) * scale;
    }
    if (!left_hand) { // the same on the other side: x turned over
        for (std::size_t c = 0; c < 4; c++) {
            o[c * 4] = -o[c * 4];
        }
    }
    return o;
}

} // namespace FrameXr::Vis
