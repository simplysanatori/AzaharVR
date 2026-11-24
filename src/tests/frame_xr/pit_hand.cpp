// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuhand: Pit's hand (frame_xr/vis/pit_hand.h) and the laurel frame
// (frame_xr/vis/laurel_frame.h): the mesh is sound (weights, closed parts, normals), every pose
// is finite, the right hand is the left one's mirror, the grip closes round the handle without
// passing through it or the palm, hand tracking's joints give back the pose they came from, and
// the wreath stays off the panel.

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "frame_xr/vis/laurel_frame.h"
#include "frame_xr/vis/pit_hand.h"
#include "frame_xr/vis/weapon_fit.h"

using Catch::Approx;
using namespace FrameXr::Vis;

namespace {

using V3 = std::array<float, 3>;

V3 Sub(const V3& a, const V3& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
V3 Add(const V3& a, const V3& b) {
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}
V3 Mul(const V3& a, float s) {
    return {a[0] * s, a[1] * s, a[2] * s};
}
float Dot(const V3& a, const V3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
V3 Cross(const V3& a, const V3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float Len(const V3& a) {
    return std::sqrt(Dot(a, a));
}
V3 Unit(const V3& a) {
    return Mul(a, 1.0f / Len(a));
}
V3 Apply(const PitMat34& m, const V3& p) {
    return {m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3], m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
            m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11]};
}
V3 Apply4(const PitMat4& m, const V3& p) {
    return {m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
            m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]};
}
V3 Dir4(const PitMat4& m, const V3& d) {
    return {m[0] * d[0] + m[4] * d[1] + m[8] * d[2], m[1] * d[0] + m[5] * d[1] + m[9] * d[2],
            m[2] * d[0] + m[6] * d[1] + m[10] * d[2]};
}
V3 Place(const PitMat34& m) {
    return {m[3], m[7], m[11]};
}

constexpr std::array<PitHandNamed, 12> kAll{
    PitHandNamed::Bind,        PitHandNamed::Open,      PitHandNamed::Relaxed,     PitHandNamed::Fist,
    PitHandNamed::Point,       PitHandNamed::ThumbUp,   PitHandNamed::Grip,        PitHandNamed::Trigger,
    PitHandNamed::GripThumbUp, PitHandNamed::GripStick, PitHandNamed::GripButtons, PitHandNamed::GripOpen};

/// A mesh's edges by welded position: closed when every edge has two triangles that run it
/// opposite ways. Returns (closed, vertices - edges + faces).
struct Topology {
    bool closed = false;
    int euler = 0;
    int components = 0;
};
Topology TopologyOf(const std::vector<V3>& positions, std::span<const uint32_t> indices) {
    std::map<std::tuple<int, int, int>, int> weld;
    const auto id = [&](uint32_t v) {
        const V3& p = positions[v];
        const auto key = std::make_tuple(static_cast<int>(std::lround(p[0] * 20000.0f)),
                                         static_cast<int>(std::lround(p[1] * 20000.0f)),
                                         static_cast<int>(std::lround(p[2] * 20000.0f)));
        return weld.try_emplace(key, static_cast<int>(weld.size())).first->second;
    };
    std::map<std::pair<int, int>, std::pair<int, int>> edges; // count, signed sum
    std::set<int> used;
    std::vector<std::array<int, 3>> tris;
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        const std::array<int, 3> v{id(indices[t]), id(indices[t + 1]), id(indices[t + 2])};
        tris.push_back(v);
        for (int k = 0; k < 3; k++) {
            const int a = v[static_cast<std::size_t>(k)], b = v[static_cast<std::size_t>((k + 1) % 3)];
            auto& e = edges[{std::min(a, b), std::max(a, b)}];
            e.first++;
            e.second += a < b ? 1 : -1;
            used.insert(a);
        }
    }
    Topology out;
    out.closed = true;
    for (const auto& [key, e] : edges) {
        if (e.first != 2 || e.second != 0) {
            out.closed = false;
        }
    }
    out.euler = static_cast<int>(used.size()) - static_cast<int>(edges.size()) + static_cast<int>(tris.size());
    // components
    std::map<int, int> parent;
    const auto find = [&parent](int x) {
        while (parent.try_emplace(x, x).first->second != x) {
            x = parent[x];
        }
        return x;
    };
    for (const auto& [key, e] : edges) {
        parent[find(key.first)] = find(key.second);
    }
    std::set<int> roots;
    for (const int v : used) {
        roots.insert(find(v));
    }
    out.components = static_cast<int>(roots.size());
    return out;
}

std::vector<V3> Positions(std::span<const float> vertices, uint32_t stride) {
    std::vector<V3> out(vertices.size() / stride);
    for (std::size_t i = 0; i < out.size(); i++) {
        out[i] = {vertices[i * stride], vertices[i * stride + 1], vertices[i * stride + 2]};
    }
    return out;
}

std::vector<float> Skinned(const PitHandSkeleton& s, bool mirrored = false) {
    std::vector<float> out(static_cast<std::size_t>(PitHandVertexCount()) * kPitVertexFloats);
    PitHandSkin(s, mirrored, out);
    return out;
}

/// The clearance between a rounded cone and an endless cylinder (negative: through it).
float CylinderGap(const PitHandCapsule& c, const PitHandHandle& h) {
    float best = 1e9f;
    for (int i = 0; i <= 8; i++) {
        const float t = static_cast<float>(i) / 8.0f;
        const V3 q = Sub(Add(c.a, Mul(Sub(c.b, c.a), t)), h.centre);
        const float d = Len(Sub(q, Mul(h.axis, Dot(q, h.axis))));
        best = std::min(best, d - h.radius - (c.ra + (c.rb - c.ra) * t));
    }
    return best;
}

/// The clearance between two rounded cones, by sampling both.
float CapsuleGap(const PitHandCapsule& a, const PitHandCapsule& b) {
    float best = 1e9f;
    for (int i = 0; i <= 10; i++) {
        const float s = static_cast<float>(i) / 10.0f;
        const V3 p = Add(a.a, Mul(Sub(a.b, a.a), s));
        for (int j = 0; j <= 10; j++) {
            const float t = static_cast<float>(j) / 10.0f;
            const V3 q = Add(b.a, Mul(Sub(b.b, b.a), t));
            best = std::min(best, Len(Sub(p, q)) - (a.ra + (a.rb - a.ra) * s) - (b.ra + (b.rb - b.ra) * t));
        }
    }
    return best;
}

/// Whether a triangle (x, y) touches the rectangle |x| <= hw, |y| <= hh (separating axes).
bool TouchesRect(const std::array<std::array<float, 2>, 3>& t, float hw, float hh) {
    const auto [min_x, max_x] = std::minmax({t[0][0], t[1][0], t[2][0]});
    const auto [min_y, max_y] = std::minmax({t[0][1], t[1][1], t[2][1]});
    if (min_x > hw || max_x < -hw || min_y > hh || max_y < -hh) {
        return false;
    }
    for (int k = 0; k < 3; k++) {
        const auto& a = t[static_cast<std::size_t>(k)];
        const auto& b = t[static_cast<std::size_t>((k + 1) % 3)];
        const float nx = b[1] - a[1], ny = a[0] - b[0];
        float tri_min = 1e9f, tri_max = -1e9f;
        for (const auto& p : t) {
            const float d = p[0] * nx + p[1] * ny;
            tri_min = std::min(tri_min, d);
            tri_max = std::max(tri_max, d);
        }
        const float r = hw * std::abs(nx) + hh * std::abs(ny);
        if (tri_min > r || tri_max < -r) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("Pit's hand: the mesh is within its budget and every vertex hangs on its bones", "[kiuhand]") {
    const PitHandBind bind = PitHandBindData();
    const uint32_t n = PitHandVertexCount();
    const auto indices = PitHandIndices();
    REQUIRE(bind.positions.size() == n * 3);
    REQUIRE(bind.bones.size() == n * 4);
    REQUIRE(bind.weights.size() == n * 4);
    CHECK(indices.size() % 3 == 0);
    CHECK(indices.size() / 3 >= 3000);
    CHECK(indices.size() / 3 <= 6000);
    for (uint32_t v = 0; v < n; v++) {
        int sum = 0;
        for (int k = 0; k < 4; k++) {
            const int w = bind.weights[v * 4 + static_cast<uint32_t>(k)];
            sum += w;
            REQUIRE(bind.bones[v * 4 + static_cast<uint32_t>(k)] < kPitHandJoints);
            if (k > 0) { // in falling order (PitHandSkin stops at the first zero)
                REQUIRE(w <= bind.weights[v * 4 + static_cast<uint32_t>(k) - 1]);
            }
        }
        REQUIRE(sum == 255);
        REQUIRE(bind.weights[v * 4] > 0);
        const V3 nrm{bind.normals[v * 3], bind.normals[v * 3 + 1], bind.normals[v * 3 + 2]};
        REQUIRE(Len(nrm) == Approx(1.0f).margin(1e-3));
    }
    for (const uint32_t i : indices) {
        REQUIRE(i < n);
    }
    // No bone but the ones that carry skin: the tips and the palm carry none.
    for (uint32_t v = 0; v < n; v++) {
        for (int k = 0; k < 4; k++) {
            if (bind.weights[v * 4 + static_cast<uint32_t>(k)] > 0) {
                const int b = bind.bones[v * 4 + static_cast<uint32_t>(k)];
                CHECK(b != kPitPalm);
                CHECK(b != kPitElbow);
            }
        }
    }
}

TEST_CASE("Pit's hand: no degenerate triangle, every part closed, normals outward", "[kiuhand]") {
    const PitHandBind bind = PitHandBindData();
    const std::vector<V3> pos = Positions(bind.positions, 3);
    const auto indices = PitHandIndices();
    uint32_t agree = 0, total = 0;
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        const V3& a = pos[indices[t]];
        const V3& b = pos[indices[t + 1]];
        const V3& c = pos[indices[t + 2]];
        const V3 n = Cross(Sub(b, a), Sub(c, a));
        REQUIRE(Len(n) > 1e-7f); // arm units squared: no triangle without area
        for (int k = 0; k < 3; k++) {
            const uint32_t v = indices[t + static_cast<std::size_t>(k)];
            const V3 vn{bind.normals[v * 3], bind.normals[v * 3 + 1], bind.normals[v * 3 + 2]};
            agree += Dot(vn, n) > 0.0f ? 1u : 0u;
            total++;
        }
    }
    CHECK(agree > total * 97 / 100); // a normal may lean off a face only at a hard edge's rim
    uint32_t covered = 0;
    for (const PitHandPart& part : PitHandParts()) {
        const Topology topo = TopologyOf(pos, indices.subspan(part.first_index, part.index_count));
        INFO(part.name);
        CHECK(topo.closed);
        if (part.name == "skin" || part.name == "wrap" || part.name == "nail" || part.name == "vambrace" ||
            part.name == "forearm") {
            CHECK(topo.components == 1);
            CHECK(topo.euler == 2); // a sphere: no finger fused to another, no hole
        } else if (part.name == "band") {
            CHECK(topo.components == 1);
            CHECK(topo.euler == 0); // a ring
        } else {
            CHECK(topo.euler == 2 * topo.components); // the seam: closed stitches
        }
        covered += part.index_count;
    }
    CHECK(covered == indices.size());
}

TEST_CASE("Pit's hand: the bind pose skins to the mesh as made, in the arm's space", "[kiuhand]") {
    const PitHandSkeleton s = PitHandEvaluate(PitHandNamedPose(PitHandNamed::Bind));
    for (int j = 0; j < kPitHandJoints; j++) {
        const PitMat34& b = s.bone[static_cast<std::size_t>(j)];
        const PitMat34 identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        for (std::size_t k = 0; k < 12; k++) {
            REQUIRE(b[k] == Approx(identity[k]).margin(1e-5));
        }
    }
    const PitHandBind bind = PitHandBindData();
    const std::vector<float> out = Skinned(s);
    float lo_x = 1e9f, hi_x = -1e9f;
    for (uint32_t v = 0; v < PitHandVertexCount(); v++) {
        for (uint32_t k = 0; k < 3; k++) {
            REQUIRE(out[v * kPitVertexFloats + k] == Approx(bind.positions[v * 3 + k]).margin(1e-4));
        }
        lo_x = std::min(lo_x, out[v * kPitVertexFloats]);
        hi_x = std::max(hi_x, out[v * kPitVertexFloats]);
        // the tangent is a unit vector square enough to the normal to make a frame
        const float* p = &out[v * kPitVertexFloats];
        REQUIRE(Len({p[6], p[7], p[8]}) == Approx(1.0f).margin(1e-3));
        REQUIRE(std::abs(Dot({p[3], p[4], p[5]}, {p[6], p[7], p[8]})) < 0.05f);
        REQUIRE(p[10] > 0.0f);
        REQUIRE(p[10] < 1.0f);
        REQUIRE(p[11] > 0.0f);
        REQUIRE(p[11] < 1.0f);
    }
    // The wrist at the origin, the fingers toward +x, the elbow where the game's arm ends:
    // about 35 cm from the finger tips to the elbow at the arm's scale.
    CHECK(Place(s.joint[kPitWrist])[0] == Approx(0.0f).margin(1e-5));
    CHECK(lo_x * kArmScale == Approx(-0.164f).margin(0.002));
    CHECK(hi_x * kArmScale > 0.17f);
    CHECK(hi_x * kArmScale < 0.20f);
    CHECK(kPitHandUnit == kArmScale);
    CHECK(Place(s.joint[kPitElbow])[0] == Approx(kPitElbowAt[0]).margin(1e-4));
    // The thumb on +z, the back of the hand +y.
    CHECK(Place(s.joint[kPitThumbTip])[2] > Place(s.joint[kPitIndexTip])[2]);
    const PitMat34& w = s.joint[kPitWrist];
    CHECK(w[5] == Approx(1.0f).margin(1e-4));  // the wrist's +Y is the arm's +y
    CHECK(w[2] == Approx(-1.0f).margin(1e-4)); // its -Z the arm's +x
}

TEST_CASE("Pit's hand: every pose is finite and keeps the bones' lengths", "[kiuhand]") {
    const PitHandSkeleton bind = PitHandEvaluate(PitHandNamedPose(PitHandNamed::Bind));
    std::vector<PitHandPose> poses;
    for (const PitHandNamed name : kAll) {
        CHECK_FALSE(PitHandNamedLabel(name).empty());
        poses.push_back(PitHandNamedPose(name));
    }
    for (const PitHandGrasp grasp : {PitHandGrasp::Free, PitHandGrasp::Held}) {
        for (int bits = 0; bits < 32; bits++) {
            for (const float value : {0.0f, 0.3f, 1.0f, 7.0f, -2.0f}) {
                PitHandInputs in;
                in.trigger_touch = bits & 1;
                in.grip_touch = bits & 2;
                in.thumb_stick = bits & 4;
                in.thumb_buttons = bits & 8;
                in.thumb_rest = bits & 16;
                in.trigger = value;
                in.grip = 1.0f - value;
                poses.push_back(PitHandPoseFromInputs(in, grasp));
            }
        }
    }
    poses.push_back(PitHandBlend(PitHandNamedPose(PitHandNamed::Open), PitHandNamedPose(PitHandNamed::Fist), 0.37f));
    poses.push_back(PitHandEase(PitHandNamedPose(PitHandNamed::Open), PitHandNamedPose(PitHandNamed::Fist), 0.011f));
    std::vector<float> out(static_cast<std::size_t>(PitHandVertexCount()) * kPitVertexFloats);
    for (const PitHandPose& pose : poses) {
        const PitHandSkeleton s = PitHandEvaluate(pose);
        for (int j = 0; j < kPitHandJoints; j++) {
            for (const float f : s.bone[static_cast<std::size_t>(j)]) {
                REQUIRE(std::isfinite(f));
            }
            const int parent = PitHandParent(j);
            if (parent >= 0) {
                const float len = Len(Sub(Place(s.joint[static_cast<std::size_t>(j)]),
                                          Place(s.joint[static_cast<std::size_t>(parent)])));
                const float was = Len(Sub(Place(bind.joint[static_cast<std::size_t>(j)]),
                                          Place(bind.joint[static_cast<std::size_t>(parent)])));
                REQUIRE(len == Approx(was).margin(1e-4));
            }
        }
        PitHandSkin(s, false, out);
        for (const float f : out) {
            REQUIRE(std::isfinite(f));
        }
        // Nothing flies off: the whole hand stays within reach of the wrist.
        for (std::size_t v = 0; v < out.size(); v += kPitVertexFloats) {
            REQUIRE(Len({out[v], out[v + 1], out[v + 2]}) * kPitHandUnit < 0.26f);
        }
    }
}

TEST_CASE("Pit's hand: the controller's touches and pulls curl the fingers as the table says", "[kiuhand]") {
    PitHandInputs in;
    PitHandCurls c = PitHandCurlsFromInputs(in);
    CHECK(c.index == 0.0f);
    CHECK(c.middle == 0.0f);
    CHECK(c.thumb == 0.0f);
    CHECK(c.thumb_on == PitHandThumb::Up);
    CHECK(c.spread == 1.0f);
    in.trigger_touch = true;
    CHECK(PitHandCurlsFromInputs(in).index == Approx(0.5f));
    in.trigger = 1.0f;
    CHECK(PitHandCurlsFromInputs(in).index == Approx(1.0f));
    in.trigger_touch = false;
    in.trigger = 0.5f;
    CHECK(PitHandCurlsFromInputs(in).index == Approx(0.25f));
    in.grip_touch = true;
    c = PitHandCurlsFromInputs(in);
    CHECK(c.middle == Approx(0.7f));
    CHECK(c.ring == c.middle);
    CHECK(c.little == c.middle);
    in.grip = 1.0f;
    CHECK(PitHandCurlsFromInputs(in).little == Approx(1.0f));
    CHECK(PitHandCurlsFromInputs(in).spread < 0.25f);
    in.thumb_rest = true;
    CHECK(PitHandCurlsFromInputs(in).thumb_on == PitHandThumb::Rest);
    in.thumb_buttons = true;
    CHECK(PitHandCurlsFromInputs(in).thumb_on == PitHandThumb::Buttons);
    in.thumb_stick = true;
    c = PitHandCurlsFromInputs(in);
    CHECK(c.thumb_on == PitHandThumb::Stick);
    CHECK(c.thumb == 1.0f);

    // The ends of the table are the named poses.
    const auto same = [](const PitHandPose& a, PitHandNamed name) {
        return !PitHandPoseDiffers(a, PitHandNamedPose(name), 0.01f);
    };
    PitHandInputs none;
    PitHandInputs all;
    all.trigger_touch = all.grip_touch = all.thumb_rest = true;
    all.trigger = all.grip = 1.0f;
    CHECK(same(PitHandPoseFromInputs(all, PitHandGrasp::Free), PitHandNamed::Fist));
    CHECK(same(PitHandPoseFromInputs(all, PitHandGrasp::Held), PitHandNamed::Trigger));
    CHECK(same(PitHandPoseFromInputs(none, PitHandGrasp::Held), PitHandNamed::GripOpen));
    PitHandInputs point = all;
    point.trigger_touch = false;
    point.trigger = 0.0f;
    CHECK(same(PitHandPoseFromInputs(point, PitHandGrasp::Free), PitHandNamed::Point));
    PitHandInputs thumb_up = all;
    thumb_up.thumb_rest = false;
    CHECK(same(PitHandPoseFromInputs(thumb_up, PitHandGrasp::Free), PitHandNamed::ThumbUp));
    PitHandInputs grip = all;
    grip.trigger = 0.0f;
    CHECK(same(PitHandPoseFromInputs(grip, PitHandGrasp::Held), PitHandNamed::Grip));
    grip.thumb_stick = true;
    CHECK(same(PitHandPoseFromInputs(grip, PitHandGrasp::Held), PitHandNamed::GripStick));
    // A finger closes further the more its control is pulled: the tip nears the palm.
    float last = 1e9f;
    for (const float v : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        PitHandInputs i;
        i.grip = v;
        const PitHandSkeleton s = PitHandEvaluate(PitHandPoseFromInputs(i, PitHandGrasp::Free));
        const float d = Len(Sub(Place(s.joint[kPitMiddleTip]), Place(s.joint[kPitPalm])));
        CHECK(d < last);
        last = d;
    }
    // Easing moves toward the target and arrives; an unchanged pose is not dirty.
    const PitHandPose& a = PitHandNamedPose(PitHandNamed::Open);
    const PitHandPose& b = PitHandNamedPose(PitHandNamed::Fist);
    CHECK(PitHandPoseDiffers(a, b));
    CHECK_FALSE(PitHandPoseDiffers(a, a));
    PitHandPose p = a;
    for (int i = 0; i < 200; i++) {
        p = PitHandEase(p, b, 1.0f / 90.0f);
    }
    CHECK_FALSE(PitHandPoseDiffers(p, b, 0.1f));
    CHECK(PitHandPoseDiffers(PitHandEase(a, b, 1.0f / 90.0f), a));
}

TEST_CASE("Pit's hand: the right hand is the left one's mirror", "[kiuhand]") {
    const PitHandSkeleton s = PitHandEvaluate(PitHandNamedPose(PitHandNamed::Grip));
    const std::vector<float> left = Skinned(s, false);
    const std::vector<float> right = Skinned(s, true);
    for (std::size_t v = 0; v < left.size(); v += kPitVertexFloats) {
        REQUIRE(right[v] == -left[v]);
        REQUIRE(right[v + 1] == left[v + 1]);
        REQUIRE(right[v + 2] == left[v + 2]);
        REQUIRE(right[v + 3] == -left[v + 3]);
        REQUIRE(right[v + 4] == left[v + 4]);
        REQUIRE(right[v + 6] == -left[v + 6]);
        REQUIRE(right[v + 9] == -left[v + 9]);
        REQUIRE(right[v + 10] == left[v + 10]);
        REQUIRE(right[v + 11] == left[v + 11]);
    }
    // The mirrored triangles still face out: against their own vertex normals.
    const auto li = PitHandIndices(false);
    const auto ri = PitHandIndices(true);
    REQUIRE(li.size() == ri.size());
    uint32_t agree = 0, total = 0;
    const std::vector<V3> pos = Positions(right, kPitVertexFloats);
    for (std::size_t t = 0; t + 2 < ri.size(); t += 3) {
        REQUIRE(ri[t] == li[t]);
        REQUIRE(ri[t + 1] == li[t + 2]);
        REQUIRE(ri[t + 2] == li[t + 1]);
        const V3 n = Cross(Sub(pos[ri[t + 1]], pos[ri[t]]), Sub(pos[ri[t + 2]], pos[ri[t]]));
        const float* vn = &right[static_cast<std::size_t>(ri[t]) * kPitVertexFloats + 3];
        agree += Dot(n, {vn[0], vn[1], vn[2]}) > 0.0f ? 1u : 0u;
        total++;
    }
    CHECK(agree > total * 95 / 100);
    // The fits mirror too: the right controller's is the left one's with x turned over.
    const PitMat4 lf = PitHandControllerFit(true);
    const PitMat4 rf = PitHandControllerFit(false);
    const V3 p{0.7f, -0.2f, 0.3f};
    const V3 a = Apply4(lf, p);
    const V3 b = Apply4(rf, p);
    CHECK(b[0] == Approx(-a[0]).margin(1e-6));
    CHECK(b[1] == Approx(a[1]).margin(1e-6));
    CHECK(b[2] == Approx(a[2]).margin(1e-6));
}

TEST_CASE("Pit's hand: the grip closes round the handle without passing through it or the palm", "[kiuhand]") {
    const PitHandHandle handle = PitHandHeld();
    CHECK(Len(handle.axis) == Approx(1.0f).margin(1e-4));
    CHECK(handle.radius * kPitHandUnit == Approx(0.018f).margin(0.0005));
    CHECK(handle.axis[2] > 0.7f);     // toward the thumb's side, leaning to the fingers
    CHECK(handle.centre[1] < 0.0f);   // under the palm
    const float tol = 0.0016f / kPitHandUnit; // skin gives a little where it presses
    for (const PitHandNamed name : {PitHandNamed::Grip, PitHandNamed::Trigger, PitHandNamed::GripStick,
                                    PitHandNamed::GripButtons, PitHandNamed::GripThumbUp, PitHandNamed::GripOpen}) {
        INFO(PitHandNamedLabel(name));
        const PitHandSkeleton s = PitHandEvaluate(PitHandNamedPose(name));
        const std::vector<PitHandCapsule> caps = PitHandCapsules(s);
        REQUIRE(caps.size() == 19);
        // No bone of a finger or the thumb inside the handle.
        for (std::size_t i = 0; i < 15; i++) {
            INFO("capsule " << i);
            CHECK(CylinderGap(caps[i], handle) > -tol);
        }
        // No finger through the palm: a finger's two outer bones clear of every metacarpal but
        // where its own knuckle is.
        for (std::size_t f = 0; f < 4; f++) {
            for (std::size_t k = 1; k < 3; k++) {
                for (std::size_t m = 15; m < 19; m++) {
                    INFO("finger " << f << " bone " << k << " metacarpal " << m);
                    CHECK(CapsuleGap(caps[f * 3 + k], caps[m]) > -tol);
                }
            }
        }
    }
    // Grip: the three gripping fingers touch the handle and reach more than half way round it.
    const PitHandSkeleton s = PitHandEvaluate(PitHandNamedPose(PitHandNamed::Grip));
    const std::vector<PitHandCapsule> caps = PitHandCapsules(s);
    const auto around = [&handle](const V3& p) {
        const V3 q = Sub(p, handle.centre);
        return Unit(Sub(q, Mul(handle.axis, Dot(q, handle.axis))));
    };
    for (const int knuckle : {kPitMiddleProximal, kPitRingProximal, kPitLittleProximal}) {
        const std::size_t f = static_cast<std::size_t>((knuckle - kPitIndexProximal) / 5);
        float nearest = 1e9f;
        for (std::size_t k = 0; k < 3; k++) {
            nearest = std::min(nearest, CylinderGap(caps[f * 3 + k], handle));
        }
        CHECK(nearest < 0.002f / kPitHandUnit); // on the handle
        const V3 from = around(Place(s.joint[static_cast<std::size_t>(knuckle)]));
        const V3 to = around(Place(s.joint[static_cast<std::size_t>(knuckle + 3)]));
        CHECK(Dot(from, to) < 0.0f); // the tip is past the handle's far side from the knuckle
        // and it wraps under, toward the palm's side, not over the back of the hand
        CHECK(Place(s.joint[static_cast<std::size_t>(knuckle + 3)])[1] < handle.centre[1]);
    }
    // The palm lies on the handle: the handle is clear of the metacarpals but near them.
    for (std::size_t m = 15; m < 19; m++) {
        CHECK(CylinderGap(caps[m], handle) > -0.003f / kPitHandUnit);
    }
    // The fist is tighter than the grip: its rod is thinner and the tips nearer the palm.
    CHECK(PitHandFist().radius < handle.radius);
    const PitHandSkeleton fist = PitHandEvaluate(PitHandNamedPose(PitHandNamed::Fist));
    CHECK(Len(Sub(Place(fist.joint[kPitMiddleTip]), Place(fist.joint[kPitPalm]))) <
          Len(Sub(Place(s.joint[kPitMiddleTip]), Place(s.joint[kPitPalm]))));
    for (const PitHandCapsule& c : PitHandCapsules(fist)) {
        CHECK(c.ra > 0.0f);
    }
}

TEST_CASE("Pit's hand: the controller's fit puts the handle through the fist", "[kiuhand]") {
    const PitHandHandle handle = PitHandHeld();
    // The left controller's handle (weapon_fit.h's is the right one's).
    const V3 axis{-kHandleAxis[0], kHandleAxis[1], kHandleAxis[2]};
    const V3 centre{-kHandleCentre[0], kHandleCentre[1], kHandleCentre[2]};
    const PitMat4 fit = PitHandControllerFit(true);
    const V3 a = Unit(Dir4(fit, handle.axis));
    CHECK(Dot(a, Unit(axis)) == Approx(1.0f).margin(1e-4));
    CHECK(Len(Dir4(fit, {1, 0, 0})) == Approx(kPitHandUnit).margin(1e-6));
    // The hand's handle lies on the controller's handle's line, within 3 cm of its middle.
    const V3 c = Sub(Apply4(fit, handle.centre), centre);
    const float along = Dot(c, Unit(axis));
    CHECK(Len(Sub(c, Mul(Unit(axis), along))) < 0.0005f);
    CHECK(std::abs(along) < 0.03f);
    // The back of the hand is outward (the left hand's: -x), the fingers ahead (-z).
    CHECK(Dir4(fit, {0, 1, 0})[0] < 0.0f);
    CHECK(Dir4(fit, {1, 0, 0})[2] < 0.0f);
    // A scale keeps that point of the handle where it is.
    const PitMat4 big = PitHandControllerFit(true, 1.3f);
    CHECK(Len(Dir4(big, {1, 0, 0})) == Approx(1.3f * kPitHandUnit).margin(1e-6));
    CHECK(Len(Sub(Apply4(big, handle.centre), Apply4(fit, handle.centre))) < 1e-5f);
    // The index finger's tip is at the trigger with the trigger pulled: ahead of the handle.
    const PitHandSkeleton s = PitHandEvaluate(PitHandNamedPose(PitHandNamed::Trigger));
    const V3 tip = Apply4(fit, Place(s.joint[kPitIndexTip]));
    CHECK(tip[2] < centre[2] - 0.04f);
    CHECK(Len(Sub(tip, V3{-0.0055f, -0.035f, 0.0185f})) < 0.02f);
}

TEST_CASE("Pit's hand: hand tracking's joints give back the pose they came from", "[kiuhand]") {
    for (const PitHandNamed name : kAll) {
        INFO(PitHandNamedLabel(name));
        const PitHandSkeleton s = PitHandEvaluate(PitHandNamedPose(name));
        const std::array<PitJointPose, kPitXrJoints> joints = PitHandJointsOf(s);
        const PitHandSkeleton back = PitHandEvaluate(PitHandPoseFromJoints(joints, false));
        for (int j = 0; j < kPitXrJoints; j++) {
            for (std::size_t k = 0; k < 12; k++) {
                REQUIRE(back.joint[static_cast<std::size_t>(j)][k] ==
                        Approx(s.joint[static_cast<std::size_t>(j)][k]).margin(2e-4));
            }
        }
        // Wherever the hand is and however it is turned: the same pose, and the fit puts our
        // wrist on the tracked one.
        const PitQuat turn{0.2705981f, 0.6532815f, -0.2705981f, 0.6532815f};
        const V3 at{0.3f, 1.2f, -0.4f};
        const auto rotate = [&turn](const V3& v) {
            const V3 u{turn.x, turn.y, turn.z};
            const V3 t = Mul(Cross(u, v), 2.0f);
            return Add(Add(v, Mul(t, turn.w)), Cross(u, t));
        };
        const auto times = [](const PitQuat& a, const PitQuat& b) {
            return PitQuat{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                           a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
        };
        std::array<PitJointPose, kPitXrJoints> moved = joints;
        std::array<PitJointPose, kPitXrJoints> right = joints;
        for (std::size_t j = 0; j < moved.size(); j++) {
            moved[j].orientation = times(turn, joints[j].orientation);
            moved[j].position = Add(rotate(joints[j].position), at);
            // The same hand as a right one: the moved joints mirrored in x.
            const PitQuat& q = moved[j].orientation;
            right[j].orientation = {q.x, -q.y, -q.z, q.w};
            right[j].position = {-moved[j].position[0], moved[j].position[1], moved[j].position[2]};
        }
        CHECK_FALSE(PitHandPoseDiffers(PitHandPoseFromJoints(moved, false), PitHandPoseFromJoints(joints, false), 0.05f));
        CHECK_FALSE(PitHandPoseDiffers(PitHandPoseFromJoints(right, true), PitHandPoseFromJoints(joints, false), 0.05f));
        const PitMat4 fit = PitHandTrackedFit(moved[kPitWrist], false);
        const PitMat4 rfit = PitHandTrackedFit(right[kPitWrist], true);
        for (const int j : {kPitWrist, kPitIndexTip, kPitThumbTip, kPitLittleProximal}) {
            const V3 ours = Apply4(fit, Place(s.joint[static_cast<std::size_t>(j)]));
            CHECK(Len(Sub(ours, moved[static_cast<std::size_t>(j)].position)) < 2e-4f);
            const V3 mirrored = Apply4(rfit, Place(s.joint[static_cast<std::size_t>(j)]));
            CHECK(Len(Sub(mirrored, right[static_cast<std::size_t>(j)].position)) < 2e-4f);
        }
    }
    // A hand with other bone lengths: ours keep theirs, the fingers point as tracked.
    const PitHandSkeleton s = PitHandEvaluate(PitHandNamedPose(PitHandNamed::Relaxed));
    std::array<PitJointPose, kPitXrJoints> big = PitHandJointsOf(s);
    for (PitJointPose& j : big) {
        j.position = Mul(j.position, 1.4f);
    }
    const PitHandSkeleton same = PitHandEvaluate(PitHandPoseFromJoints(big, false));
    CHECK(Len(Sub(Place(same.joint[kPitMiddleTip]), Place(s.joint[kPitMiddleTip]))) < 1e-3f);
    // With the metacarpals kept ours the fingers still point as tracked.
    const PitHandSkeleton kept = PitHandEvaluate(PitHandPoseFromJoints(PitHandJointsOf(s), false, 0.0f));
    for (std::size_t k = 0; k < 12; k++) {
        if (k % 4 != 3) {
            CHECK(kept.joint[kPitRingIntermediate][k] == Approx(s.joint[kPitRingIntermediate][k]).margin(2e-4));
        }
    }
}

TEST_CASE("Pit's hand: the forearm turns about the wrist toward the elbow", "[kiuhand]") {
    const PitHandPose& pose = PitHandNamedPose(PitHandNamed::Grip);
    const PitHandSkeleton straight = PitHandEvaluate(pose);
    CHECK(Len(Sub(Place(straight.joint[kPitElbow]), kPitElbowAt)) < 1e-4f);
    const V3 target{-1.5f, -1.4f, 0.6f};
    const PitHandSkeleton turned = PitHandEvaluate(pose, &target);
    const V3 elbow = Place(turned.joint[kPitElbow]);
    CHECK(Len(elbow) == Approx(Len(kPitElbowAt)).margin(1e-4)); // the forearm's length is ours
    CHECK(Dot(Unit(elbow), Unit(target)) == Approx(1.0f).margin(1e-5));
    // The hand's bones do not move; the vambrace goes with the forearm.
    for (int j = 0; j < kPitXrJoints; j++) {
        for (std::size_t k = 0; k < 12; k++) {
            REQUIRE(turned.bone[static_cast<std::size_t>(j)][k] == straight.bone[static_cast<std::size_t>(j)][k]);
        }
    }
    const PitHandSeat seat = PitHandPanelSeat(false);
    CHECK(seat.normal[1] == 1.0f);
    CHECK(PitHandPanelSeat(true).normal[1] == -1.0f);
    CHECK(seat.centre[0] < kPitVambraceWristX);
    CHECK(seat.centre[0] > kPitVambraceElbowX);
    const V3 moved = Apply(turned.bone[kPitForearm], seat.centre);
    CHECK(Len(moved) == Approx(Len(seat.centre)).margin(1e-4));
    CHECK(Dot(Unit(moved), Unit(target)) > 0.85f);
    // The seat is on the bands: no vertex of the vambrace or its bands stands above it, and
    // each band's top is just under it, a band_offset either side.
    const PitHandBind bind = PitHandBindData();
    float highest = -1e9f;
    for (uint32_t v = 0; v < PitHandVertexCount(); v++) {
        const V3 p{bind.positions[v * 3], bind.positions[v * 3 + 1], bind.positions[v * 3 + 2]};
        if (bind.materials[v] == static_cast<uint8_t>(PitHandMaterial::Band)) {
            highest = std::max(highest, p[1]);
            CHECK(std::abs(std::abs(p[0] - seat.centre[0]) - seat.band_offset) * kPitHandUnit < 0.016f);
        }
        if (p[0] < kPitVambraceWristX) {
            CHECK(p[1] <= seat.centre[1]);
        }
    }
    CHECK((seat.centre[1] - highest) * kPitHandUnit < 0.001f);
}

TEST_CASE("Pit's hand: the palette and the colours from the player's game", "[kiuhand]") {
    const PitHandColours def = PitHandDefaultColours();
    const std::vector<uint8_t> palette = PitHandPalette(def);
    REQUIRE(palette.size() == kPitPaletteWidth * kPitPaletteHeight * 4);
    CHECK(PitHandSurface().size() == palette.size());
    for (std::size_t i = 3; i < palette.size(); i += 4) {
        REQUIRE(palette[i] == 255);
    }
    // The skin's row: lit at the right, its shadow at the left, warmer down the rows.
    const PitRgb lit = PitHandColourAt(def, 1.0f, 1.0f / 64.0f);
    const PitRgb shadow = PitHandColourAt(def, 0.0f, 1.0f / 64.0f);
    const PitRgb warm = PitHandColourAt(def, 1.0f, 7.0f / 64.0f);
    CHECK(lit.r == Approx(def.of[0].lit.r));
    CHECK(shadow.g == Approx(def.of[0].shadow.g));
    CHECK(warm.g == Approx(def.of[0].warm.g));
    CHECK(shadow.r > shadow.b); // a fold in skin is warm, not grey
    // Every vertex's uv lands in its own material's rows.
    const PitHandBind bind = PitHandBindData();
    const std::vector<float> out = Skinned(PitHandEvaluate(PitHandNamedPose(PitHandNamed::Bind)));
    for (uint32_t v = 0; v < PitHandVertexCount(); v++) {
        const float row = out[v * kPitVertexFloats + 11] * 64.0f;
        REQUIRE(row >= static_cast<float>(bind.materials[v]) * 8.0f + 0.99f);
        REQUIRE(row <= static_cast<float>(bind.materials[v]) * 8.0f + 7.01f);
    }
    // From a texture: a region's mean, texels without alpha left out.
    std::vector<uint8_t> tex(4 * 4 * 4, 0);
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            uint8_t* p = &tex[static_cast<std::size_t>(y * 4 + x) * 4];
            p[0] = x < 2 ? 200 : 40;
            p[1] = 100;
            p[2] = 50;
            p[3] = (x == 0 && y == 0) ? 0 : 255;
        }
    }
    const PitRgb left = PitHandSampleColour(tex, 4, 4, 0.0f, 0.0f, 0.3f, 1.0f);
    CHECK(left.r == Approx(200.0f / 255.0f));
    const PitRgb right = PitHandSampleColour(tex, 4, 4, 0.7f, 0.0f, 1.0f, 1.0f);
    CHECK(right.r == Approx(40.0f / 255.0f));
    CHECK(PitHandSampleColour({}, 0, 0, 0, 0, 1, 1).r == Approx(def.of[0].lit.r));
    const PitHandColours game = PitHandColoursFrom(left, right, {0.3f, 0.2f, 0.1f}, {0.8f, 0.6f, 0.2f});
    CHECK(game.of[static_cast<std::size_t>(PitHandMaterial::Skin)].lit.r == Approx(left.r));
    CHECK(game.of[static_cast<std::size_t>(PitHandMaterial::Wrap)].lit.r == Approx(right.r));
    CHECK(game.of[static_cast<std::size_t>(PitHandMaterial::Skin)].shadow.r < left.r);
    CHECK(game.of[static_cast<std::size_t>(PitHandMaterial::Gold)].lit.r ==
          Approx(def.of[static_cast<std::size_t>(PitHandMaterial::Gold)].lit.r));
}

TEST_CASE("The laurel frame stays off the panel and every leaf is closed", "[kiuhand]") {
    const LaurelMesh& m = LaurelFrame();
    REQUIRE(m.vertices.size() % kPitVertexFloats == 0);
    const std::vector<V3> pos = Positions(m.vertices, kPitVertexFloats);
    for (const uint32_t i : m.indices) {
        REQUIRE(i < pos.size());
    }
    bool laurel = false, plate = false;
    for (const LaurelPart& part : m.parts) {
        INFO(part.name);
        const auto indices = std::span<const uint32_t>(m.indices).subspan(part.first_index, part.index_count);
        const Topology topo = TopologyOf(pos, indices);
        CHECK(topo.closed);
        CHECK(topo.euler == 2 * topo.components);
        if (part.name == "laurel") {
            laurel = true;
            CHECK(part.material == PitHandMaterial::Gold);
            CHECK(part.index_count / 3 >= 1000);
            CHECK(part.index_count / 3 <= 1500);
            CHECK(topo.components > 60); // two stems, their leaves, the knot
            bool left = false, right = false, top = false, bottom = false;
            for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
                std::array<std::array<float, 2>, 3> tri;
                for (std::size_t k = 0; k < 3; k++) {
                    const V3& p = pos[indices[t + k]];
                    tri[k] = {p[0], p[1]};
                    // a few mm of relief on a 10 cm panel, none of it behind the plate's face
                    REQUIRE(p[2] <= kLaurelRelief);
                    REQUIRE(p[2] > -0.006f);
                    left |= p[0] < -0.55f;
                    right |= p[0] > 0.55f;
                    top |= p[1] > 0.42f;
                    bottom |= p[1] < -0.42f;
                }
                // Not a corner of any triangle, nor an edge crossing a corner of the panel.
                REQUIRE_FALSE(TouchesRect(tri, 0.5f * kLaurelPanelWidth + kLaurelClear,
                                          0.5f * kLaurelPanelHeight + kLaurelClear));
            }
            CHECK((left && right && top && bottom)); // it goes all the way round
        } else if (part.name == "plate") {
            plate = true;
            CHECK(part.material == PitHandMaterial::Strap);
            for (const uint32_t i : indices) {
                REQUIRE(pos[i][2] < 0.0f); // behind the panel's plane
                REQUIRE(pos[i][2] >= kLaurelBack - 1e-4f);
            }
        }
    }
    CHECK(laurel);
    CHECK(plate);
    // The wreath is the same on both sides.
    float lo = 1e9f, hi = -1e9f;
    for (const V3& p : pos) {
        lo = std::min(lo, p[0]);
        hi = std::max(hi, p[0]);
    }
    CHECK(lo == Approx(-hi).margin(1e-3));
    // Normals agree with the faces.
    uint32_t agree = 0, total = 0;
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const V3 n = Cross(Sub(pos[m.indices[t + 1]], pos[m.indices[t]]), Sub(pos[m.indices[t + 2]], pos[m.indices[t]]));
        REQUIRE(Len(n) > 1e-9f);
        const float* vn = &m.vertices[static_cast<std::size_t>(m.indices[t]) * kPitVertexFloats + 3];
        agree += Dot(n, {vn[0], vn[1], vn[2]}) > 0.0f ? 1u : 0u;
        total++;
    }
    CHECK(agree > total * 97 / 100);
}

TEST_CASE("The laurel frame worn: two straps round the vambrace, under the plate", "[kiuhand]") {
    const float width_m = 0.10f; // wrist_screen.h kWristScreenWidth
    const PitHandSeat seat = PitHandPanelSeat(false);
    const float rz = seat.arm_radius * kPitHandUnit / width_m;
    const float ry = seat.arm_half_width * kPitHandUnit / width_m;
    const float off = seat.band_offset * kPitHandUnit / width_m;
    CHECK(rz == Approx(0.41f).margin(0.03));
    CHECK(ry > rz);
    CHECK(off == Approx(0.54f).margin(0.03));
    const LaurelMesh worn = LaurelFrameWorn(rz, ry, off);
    const LaurelMesh& plain = LaurelFrame();
    REQUIRE(worn.parts.size() == plain.parts.size() + 1);
    const LaurelPart& strap = worn.parts.back();
    CHECK(strap.name == "strap");
    CHECK(strap.first_index == plain.indices.size());
    const std::vector<V3> pos = Positions(worn.vertices, kPitVertexFloats);
    const auto indices = std::span<const uint32_t>(worn.indices).subspan(strap.first_index, strap.index_count);
    const Topology topo = TopologyOf(pos, indices);
    CHECK(topo.closed);
    CHECK(topo.components == 2);
    CHECK(topo.euler == 0); // two rings
    uint32_t agree = 0, total = 0;
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        const V3 n = Cross(Sub(pos[indices[t + 1]], pos[indices[t]]), Sub(pos[indices[t + 2]], pos[indices[t]]));
        const float* vn = &worn.vertices[static_cast<std::size_t>(indices[t]) * kPitVertexFloats + 3];
        agree += Dot(n, {vn[0], vn[1], vn[2]}) > 0.0f ? 1u : 0u;
        total++;
        for (std::size_t k = 0; k < 3; k++) {
            const V3& p = pos[indices[t + k]];
            // round the arm: outside its section, never in front of the panel's plane
            const float dy = p[1] / ry, dz = (p[2] - (kLaurelBack - rz)) / rz;
            REQUIRE(dy * dy + dz * dz >= 1.0f);
            REQUIRE(p[2] < 0.0f);
            REQUIRE(std::abs(p[0]) < 0.70f); // under the plate
            REQUIRE(std::abs(p[0]) > off - 0.08f); // over a band
        }
    }
    CHECK(agree == total);
}
