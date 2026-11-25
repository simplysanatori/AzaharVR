// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuhand: Pit's arm from a hand file (frame_xr/vis/pit_model.h): a good file is
// read, a broken one refused whole (never read past, never trusted), the app's poses turn its
// joints and keep its bones' lengths, the forearm turns to the elbow, no finger closes through
// the handle, and the plate's triangles come last. The model here is a few triangles made in the
// test: no real one is in the tree.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "frame_xr/vis/pit_model.h"

using Catch::Approx;
using namespace FrameXr::Vis;

namespace {

struct Maker {
    struct Joint {
        int parent, role;
        uint32_t flags;
        float at[3];
        float thick;
    };
    // forearm, wrist, a palm joint, a finger of three bones and its tip
    std::vector<Joint> joints{{-1, kPitForearm, 1, {-0.15f, 0, 0}, 0},     {0, kPitWrist, 1, {0, 0, 0}, 0},
                              {1, kPitIndexMetacarpal, 1, {0.01f, 0, 0}, 0}, {2, kPitIndexProximal, 0, {0.07f, 0, 0}, 0.010f},
                              {3, kPitIndexIntermediate, 0, {0.11f, 0, 0}, 0.009f},
                              {4, kPitIndexDistal, 0, {0.13f, 0, 0}, 0.008f}, {5, kPitIndexTip, 0, {0.15f, 0, 0}, 0.007f}};
    struct Vertex {
        float p[3];
        uint8_t bone;
    };
    // one triangle on the forearm, one on the palm, one on the last finger bone, one "plate"
    std::vector<Vertex> vertices{{{-0.10f, 0.03f, 0}, 0},  {{-0.05f, 0.03f, 0}, 0},  {{-0.08f, 0.03f, 0.02f}, 0},
                                 {{0.02f, 0.01f, 0}, 2},   {{0.05f, 0.01f, 0}, 2},   {{0.03f, 0.01f, 0.02f}, 2},
                                 {{0.135f, 0.005f, 0}, 5}, {{0.145f, 0.005f, 0}, 5}, {{0.14f, 0.005f, 0.01f}, 5},
                                 {{0.0f, 0.02f, 0}, 1},    {{0.03f, 0.02f, 0}, 1},   {{0.01f, 0.02f, 0.02f}, 1}};
    std::vector<uint32_t> indices{0, 2, 1, 3, 5, 4, 6, 8, 7, 9, 11, 10};
    uint32_t body = 9, plate = 3, width = 2, height = 2, version = 1;
    float handle[7] = {0.10f, -0.035f, 0, 0, 0, 1, 0.018f};
    float hand_length = 0.15f;

    template <typename T>
    static void Put(std::vector<uint8_t>& out, T v) {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        out.insert(out.end(), p, p + sizeof(T));
    }
    std::vector<uint8_t> Bytes() const {
        std::vector<uint8_t> out{'P', 'I', 'T', 'H', 'A', 'N', 'D', '1'};
        for (const uint32_t v : {version, static_cast<uint32_t>(vertices.size()), body, plate,
                                 static_cast<uint32_t>(joints.size()), width, height, 0u}) {
            Put(out, v);
        }
        for (const float v : {hand_length, 0.06f, 0.05f, 0.0f, -0.15f, 0.0f, 0.0f}) {
            Put(out, v);
        }
        for (const float v : handle) {
            Put(out, v);
        }
        const float fit[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.1f, 0.2f, 0.3f, 1};
        for (const float v : fit) {
            Put(out, v);
        }
        for (const float v : {0.1f, 0.2f, 0.3f}) {
            Put(out, v);
        }
        for (const Joint& j : joints) {
            Put<int32_t>(out, j.parent);
            Put<int32_t>(out, j.role);
            Put<uint32_t>(out, j.flags);
            for (const float v : j.at) {
                Put(out, v);
            }
            // the axes of a joint whose bone runs along +x, its back +y: X = +z, Y = +y, Z = -x
            for (const float v : {0.f, 0.f, -1.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f}) {
                Put(out, v);
            }
            Put(out, j.thick);
        }
        for (const Vertex& v : vertices) {
            for (const float f : {v.p[0], v.p[1], v.p[2], 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.5f, 0.5f}) {
                Put(out, f);
            }
        }
        for (const Vertex& v : vertices) {
            for (const uint8_t b : {v.bone, v.bone, v.bone, v.bone, uint8_t{255}, uint8_t{0}, uint8_t{0}, uint8_t{0}}) {
                out.push_back(b);
            }
        }
        for (const uint32_t i : indices) {
            Put(out, i);
        }
        out.insert(out.end(), static_cast<std::size_t>(width) * height * 4, 200); // colour
        out.insert(out.end(), static_cast<std::size_t>(width) * height * 4, 179); // surface
        return out;
    }
};

PitModel Good() {
    PitModel m;
    std::string error;
    REQUIRE(ParsePitModel(Maker{}.Bytes(), m, error));
    return m;
}

std::array<float, 3> Apply(const PitMat34& m, const float* p) {
    return {m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3], m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
            m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11]};
}
float Distance(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}
bool Refused(const std::vector<uint8_t>& bytes) {
    PitModel m;
    std::string error;
    const bool ok = ParsePitModel(bytes, m, error);
    if (!ok) {
        CHECK_FALSE(error.empty());
        CHECK(m.vertices.empty()); // nothing of a refused file is kept
        CHECK(m.vertex_count == 0);
    }
    return !ok;
}

} // namespace

TEST_CASE("Hand file: a good one is read as written", "[kiuhand]") {
    const PitModel m = Good();
    CHECK(m.vertex_count == 12);
    CHECK(m.body_index_count == 9);
    CHECK(m.plate_index_count == 3);
    CHECK(m.indices.size() == 12);
    CHECK(m.indices[9] == 9); // the plate's triangles are the last
    CHECK(m.joints.size() == 7);
    CHECK(m.joints[3].role == kPitIndexProximal);
    CHECK(m.joints[2].relative);
    CHECK_FALSE(m.joints[3].relative);
    CHECK(m.joints[3].radius == Approx(0.010f));
    CHECK(m.texture_width == 2);
    CHECK(m.colour.size() == 16);
    CHECK(m.surface[3] == 179);
    CHECK(m.hand_length == Approx(0.15f));
    CHECK(m.forearm_radius == Approx(0.06f));
    CHECK(m.wrist_radius == Approx(0.05f));
    CHECK(m.handle.radius == Approx(0.018f));
    CHECK(m.fit[12] == Approx(0.1f));
    CHECK(kPitModelFile == std::string("kiu_hand/pit_hand.bin"));
    // No file: false and nothing to say; a file that is not one: false with a reason.
    PitModel none;
    std::string error = "x";
    CHECK_FALSE(LoadPitModel("/nonexistent/kiu_hand/pit_hand.bin", none, error));
    CHECK(error.empty());
}

TEST_CASE("Hand file: a broken one is refused whole", "[kiuhand]") {
    const std::vector<uint8_t> good = Maker{}.Bytes();
    // Cut short anywhere, or with anything after it.
    for (std::size_t n = 0; n < good.size(); n++) {
        REQUIRE(Refused({good.begin(), good.begin() + static_cast<std::ptrdiff_t>(n)}));
    }
    std::vector<uint8_t> longer = good;
    longer.push_back(0);
    CHECK(Refused(longer));
    // Counts that lie about the size, or are past the limits (nothing is allocated from them).
    for (const std::size_t field : {8u, 12u, 16u, 20u, 24u, 28u, 32u}) {
        for (const uint32_t value : {0u, 1u, 7u, 0x7FFFFFFFu, 0xFFFFFFFFu, 4097u, 200001u}) {
            std::vector<uint8_t> bad = good;
            uint32_t was = 0;
            std::memcpy(&was, &bad[field], 4);
            if (was == value) {
                continue;
            }
            std::memcpy(&bad[field], &value, 4);
            INFO("field at " << field << " = " << value);
            CHECK(Refused(bad));
        }
    }
    const auto with = [](auto change) {
        Maker m;
        change(m);
        return m.Bytes();
    };
    CHECK(Refused(with([](Maker& m) { m.indices[4] = 12; })));              // an index past the vertices
    CHECK(Refused(with([](Maker& m) { m.vertices[2].bone = 7; })));         // a joint the file has not
    CHECK(Refused(with([](Maker& m) { m.joints[3].parent = 3; })));         // its own parent
    CHECK(Refused(with([](Maker& m) { m.joints[3].parent = 5; })));         // a later parent
    CHECK(Refused(with([](Maker& m) { m.joints[3].parent = -2; })));
    CHECK(Refused(with([](Maker& m) { m.joints[3].role = kPitHandJoints; }))); // no such role
    CHECK(Refused(with([](Maker& m) { m.joints[4].role = kPitIndexProximal; }))); // a role twice
    CHECK(Refused(with([](Maker& m) { m.joints[4].thick = -1.0f; })));
    CHECK(Refused(with([](Maker& m) { m.joints[4].at[1] = std::nanf(""); })));
    CHECK(Refused(with([](Maker& m) { m.vertices[5].p[0] = std::nanf(""); })));
    CHECK(Refused(with([](Maker& m) { m.vertices[5].p[2] = 50.0f; })));     // metres from the wrist
    CHECK(Refused(with([](Maker& m) { m.hand_length = 0.0f; })));
    CHECK(Refused(with([](Maker& m) { m.handle[6] = std::nanf(""); })));
    CHECK(Refused(with([](Maker& m) { m.body = 8; m.plate = 4; })));        // not whole triangles
    // Weights that do not sum to 255 or are out of order; a frame that is no rotation.
    {
        std::vector<uint8_t> bad = good;
        const std::size_t skin = 8 + 32 + 16 + 12 + 28 + 64 + 12 + 7 * 68 + 12 * 48;
        bad[skin + 4] = 200;
        CHECK(Refused(bad));
        bad[skin + 4] = 100;
        bad[skin + 5] = 155;
        CHECK(Refused(bad));
        bad = good;
        const std::size_t frame = 8 + 32 + 16 + 12 + 28 + 64 + 12 + 24; // the first joint's
        const float two = 2.0f;
        std::memcpy(&bad[frame], &two, 4);
        CHECK(Refused(bad));
        bad = good;
        const float one = 1.0f, zero = 0.0f; // a mirror: every axis of length one, left handed
        for (std::size_t k = 0; k < 9; k++) {
            std::memcpy(&bad[frame + k * 4], k % 4 == 0 ? &one : &zero, 4);
        }
        const float minus = -1.0f;
        std::memcpy(&bad[frame], &minus, 4);
        CHECK(Refused(bad));
        bad = good;
        bad[0] = 'X';
        CHECK(Refused(bad));
    }
    // Any single byte changed: read or refused, never worse (run under a sanitizer to see more).
    for (std::size_t at = 0; at < good.size(); at++) {
        for (const uint8_t value : {uint8_t{0}, uint8_t{0xFF}, static_cast<uint8_t>(good[at] ^ 0x80)}) {
            std::vector<uint8_t> bad = good;
            bad[at] = value;
            PitModel m;
            std::string error;
            if (ParsePitModel(bad, m, error)) {
                std::vector<float> out(std::size_t{m.vertex_count} * kPitVertexFloats);
                PitModelSkin(m, PitModelEvaluate(m, &PitHandNamedPose(PitHandNamed::Fist), &m.elbow), out);
            }
        }
    }
}

TEST_CASE("Hand file: the app's poses turn its joints, its bones keep their lengths", "[kiuhand]") {
    const PitModel m = Good();
    // No pose: the file's own bind pose.
    for (const PitMat34& b : PitModelEvaluate(m, nullptr)) {
        const PitMat34 identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        for (std::size_t k = 0; k < 12; k++) {
            REQUIRE(b[k] == Approx(identity[k]).margin(1e-6));
        }
    }
    std::vector<float> out(std::size_t{m.vertex_count} * kPitVertexFloats);
    PitModelSkin(m, PitModelEvaluate(m, nullptr), out);
    for (std::size_t i = 0; i < out.size(); i++) {
        REQUIRE(out[i] == Approx(m.vertices[i]).margin(1e-6));
    }
    for (const PitHandNamed name : {PitHandNamed::Open, PitHandNamed::Relaxed, PitHandNamed::Fist, PitHandNamed::Point,
                                    PitHandNamed::Grip, PitHandNamed::Trigger}) {
        for (const bool clear : {false, true}) {
            const std::vector<PitMat34> bones = PitModelEvaluate(m, &PitHandNamedPose(name), nullptr, clear);
            REQUIRE(bones.size() == m.joints.size());
            for (std::size_t j = 0; j < m.joints.size(); j++) {
                for (const float f : bones[j]) {
                    REQUIRE(std::isfinite(f));
                }
                // every matrix a rotation
                const PitMat34& b = bones[j];
                CHECK(b[0] * b[0] + b[4] * b[4] + b[8] * b[8] == Approx(1.0f).margin(1e-4));
                CHECK(b[0] * b[1] + b[4] * b[5] + b[8] * b[9] == Approx(0.0f).margin(1e-4));
                if (m.joints[j].parent >= 0 && j >= 3) {
                    const auto parent = static_cast<std::size_t>(m.joints[j].parent);
                    const float now = Distance(Apply(bones[j], m.joints[j].position.data()),
                                               Apply(bones[parent], m.joints[parent].position.data()));
                    const float was = Distance({m.joints[j].position[0], m.joints[j].position[1], m.joints[j].position[2]},
                                               {m.joints[parent].position[0], m.joints[parent].position[1], m.joints[parent].position[2]});
                    CHECK(now == Approx(was).margin(1e-5)); // a bone's length is the file's
                }
            }
            // the wrist, the palm and the forearm stay (no elbow given)
            for (const std::size_t j : {std::size_t{0}, std::size_t{1}}) {
                CHECK(bones[j][3] == Approx(0.0f).margin(1e-6));
                CHECK(bones[j][0] == Approx(1.0f).margin(1e-6));
            }
        }
    }
    // A fist curls the finger down toward the palm's side (-y); the point leaves it out.
    const auto tip = [&m](PitHandNamed name) {
        const auto bones = PitModelEvaluate(m, &PitHandNamedPose(name), nullptr, false);
        return Apply(bones[6], m.joints[6].position.data());
    };
    CHECK(tip(PitHandNamed::Fist)[1] < -0.02f);
    CHECK(tip(PitHandNamed::Fist)[0] < 0.10f);
    CHECK(tip(PitHandNamed::Point)[0] > 0.14f);
    CHECK(std::abs(tip(PitHandNamed::Point)[1]) < 0.02f);
}

TEST_CASE("Hand file: the forearm turns to the elbow, the hand stays", "[kiuhand]") {
    const PitModel m = Good();
    const PitVec3 elbow{-0.10f, -0.10f, 0.05f};
    const auto bones = PitModelEvaluate(m, &PitHandNamedPose(PitHandNamed::Relaxed), &elbow);
    const auto straight = PitModelEvaluate(m, &PitHandNamedPose(PitHandNamed::Relaxed), nullptr);
    const auto moved = Apply(bones[0], m.elbow.data());
    const float length = std::hypot(m.elbow[0], m.elbow[1], m.elbow[2]);
    // Its length is the file's, slid from the hand as the wrist bends (none straight).
    const float slid = std::hypot(moved[0], moved[1], moved[2]) - length;
    CHECK(slid > 0.0f);
    CHECK(slid <= 0.03f + 1e-5f);
    const auto level = Apply(PitModelEvaluate(m, &PitHandNamedPose(PitHandNamed::Relaxed), &m.elbow)[0], m.elbow.data());
    CHECK(std::hypot(level[0], level[1], level[2]) == Approx(length).margin(1e-5));
    const float along = (moved[0] * elbow[0] + moved[1] * elbow[1] + moved[2] * elbow[2]) / ((length + slid) * std::hypot(elbow[0], elbow[1], elbow[2]));
    CHECK(along == Approx(1.0f).margin(1e-5));
    for (std::size_t j = 1; j < m.joints.size(); j++) {
        for (std::size_t k = 0; k < 12; k++) {
            REQUIRE(bones[j][k] == straight[j][k]); // the hand does not go with it
        }
    }
    std::vector<float> a(std::size_t{m.vertex_count} * kPitVertexFloats), b(a.size());
    PitModelSkin(m, bones, a);
    PitModelSkin(m, straight, b);
    CHECK(a[1] != Approx(b[1]).margin(1e-4));                 // a forearm vertex moved
    CHECK(a[9 * kPitVertexFloats] == b[9 * kPitVertexFloats]); // the plate, on the wrist, did not
    // An elbow that is nowhere, or at the wrist itself: the forearm as the file has it.
    const PitVec3 bad{std::nanf(""), 0, 0}, here{0, 0, 0};
    for (const PitVec3* e : {&bad, &here}) {
        for (const PitMat34& bone : PitModelEvaluate(m, &PitHandNamedPose(PitHandNamed::Open), e)) {
            for (const float f : bone) {
                REQUIRE(std::isfinite(f));
            }
        }
    }
}

TEST_CASE("Hand file: no finger closes through the controller's handle", "[kiuhand]") {
    const PitModel m = Good();
    const auto nearest = [&m](bool clear) {
        const auto bones = PitModelEvaluate(m, &PitHandNamedPose(PitHandNamed::Fist), nullptr, clear);
        float least = 1e9f;
        for (std::size_t j = 3; j < m.joints.size(); j++) {
            const auto p = Apply(bones[j], m.joints[j].position.data());
            // the handle runs along z through (0.10, -0.035)
            least = std::min(least, std::hypot(p[0] - m.handle.centre[0], p[1] - m.handle.centre[1]) - m.handle.radius -
                                        m.joints[j].radius * kPitModelPress);
        }
        return least;
    };
    CHECK(nearest(false) < -0.004f); // the fist as posed goes through it
    CHECK(nearest(true) > -0.0005f); // held off it
    CHECK(nearest(true) < 0.004f);   // and lying on it, not flung open
    // The finger is still curled: closed round the handle, not left flat.
    const auto bones = PitModelEvaluate(m, &PitHandNamedPose(PitHandNamed::Fist), nullptr, true);
    CHECK(Apply(bones[6], m.joints[6].position.data())[1] < -0.01f);
    // A file without a handle: the pose as it is.
    Maker maker;
    maker.handle[6] = 0.0f;
    PitModel none;
    std::string error;
    REQUIRE(ParsePitModel(maker.Bytes(), none, error));
    const auto free = PitModelEvaluate(none, &PitHandNamedPose(PitHandNamed::Fist), nullptr, true);
    const auto plain = PitModelEvaluate(m, &PitHandNamedPose(PitHandNamed::Fist), nullptr, false);
    for (std::size_t k = 0; k < 12; k++) {
        CHECK(free[6][k] == plain[6][k]);
    }
}

TEST_CASE("Hand file: the controller's fit scales about its pivot and mirrors for the right hand", "[kiuhand]") {
    const PitModel m = Good();
    const PitMat4 left = PitModelControllerFit(m, true);
    const PitMat4 right = PitModelControllerFit(m, false);
    const PitMat4 big = PitModelControllerFit(m, true, 1.5f);
    CHECK(left[12] == Approx(0.1f));
    CHECK(right[12] == Approx(-0.1f));
    CHECK(right[0] == Approx(-left[0]));
    CHECK(right[5] == Approx(left[5]));
    CHECK(big[0] == Approx(1.5f * left[0]));
    // the arm point that lies on the pivot (here the origin) stays there
    CHECK(big[12] == Approx(left[12]));
    CHECK(big[13] == Approx(left[13]));
}

TEST_CASE("Hand file: two hands at once, each with its own pose, the right the left's mirror", "[kiuhand]") {
    const PitModel m = Good();
    // One model, two hands: nothing is kept between calls, so each hand has its own bones and
    // its own vertex buffer (vertex_count x kPitVertexFloats floats a hand).
    PitHandInputs left_in, right_in;
    left_in.trigger_touch = true;
    left_in.trigger = 1.0f;
    right_in.grip_touch = true;
    right_in.grip = 1.0f;
    const PitHandPose left_pose = PitHandPoseFromInputs(left_in, PitHandGrasp::Free);
    const PitHandPose right_pose = PitHandPoseFromInputs(right_in, PitHandGrasp::Free);
    const PitVec3 left_elbow{-0.12f, -0.08f, 0.02f}, right_elbow{-0.14f, -0.02f, -0.05f};
    std::vector<float> left(std::size_t{m.vertex_count} * kPitVertexFloats), right(left.size()), again(left.size());
    PitModelSkin(m, PitModelEvaluate(m, &left_pose, &left_elbow), left);
    PitModelSkin(m, PitModelEvaluate(m, &right_pose, &right_elbow), right);
    PitModelSkin(m, PitModelEvaluate(m, &left_pose, &left_elbow), again);
    CHECK(left == again);  // the right hand's pose did not touch the left's
    CHECK(left != right);  // and each has its own
    // Drawn: the left with all its indices, the right with the body's only (no plate), through
    // the mirroring fit: each of its points is the left fit's with x turned over.
    CHECK(m.body_index_count + m.plate_index_count == m.indices.size());
    for (std::size_t i = 0; i < m.body_index_count; i++) {
        REQUIRE(m.indices[i] < 9); // the body's triangles use no vertex of the plate
    }
    const PitMat4 lf = PitModelControllerFit(m, true), rf = PitModelControllerFit(m, false);
    const auto place = [](const PitMat4& f, const float* p) {
        return std::array<float, 3>{f[0] * p[0] + f[4] * p[1] + f[8] * p[2] + f[12], f[1] * p[0] + f[5] * p[1] + f[9] * p[2] + f[13],
                                    f[2] * p[0] + f[6] * p[1] + f[10] * p[2] + f[14]};
    };
    for (std::size_t v = 0; v < m.vertex_count; v++) {
        const auto a = place(lf, &left[v * kPitVertexFloats]);
        const auto b = place(rf, &left[v * kPitVertexFloats]); // the same pose on the other hand
        CHECK(b[0] == Approx(-a[0]).margin(1e-6));
        CHECK(b[1] == Approx(a[1]).margin(1e-6));
        CHECK(b[2] == Approx(a[2]).margin(1e-6));
    }
    // The mirroring fit turns space over (draw with the culling turned over, params.w + 1).
    const float det = rf[0] * (rf[5] * rf[10] - rf[6] * rf[9]) - rf[4] * (rf[1] * rf[10] - rf[2] * rf[9]) +
                      rf[8] * (rf[1] * rf[6] - rf[2] * rf[5]);
    CHECK(det < 0.0f);
}

// ---- tracked fingers ----

namespace {

struct Q {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};
Q operator*(const Q& a, const Q& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Q Turn(int axis, float degrees) {
    const float h = degrees * 0.00872664626f, s = std::sin(h);
    return {axis == 0 ? s : 0.0f, axis == 1 ? s : 0.0f, axis == 2 ? s : 0.0f, std::cos(h)};
}
PitVec3 Rotated(const Q& q, const PitVec3& v) {
    const Q o = q * Q{v[0], v[1], v[2], 0.0f} * Q{-q.x, -q.y, -q.z, q.w};
    return {o.x, o.y, o.z};
}

/// A left hand's 26 joints as a runtime has them: the palm along -z with its back +y and its thumb
/// +x, the knuckles fanned, the wrist's own frame on the line to the middle metacarpal joint (as
/// OpenXR says: 24 degrees off the palm), the four fingers fanned, each rolled a little.
struct Tracked {
    std::array<float, 5> bend{};       ///< a joint's more bend, degrees (thumb to little)
    std::array<float, 4> side{6, -4, -14, -24}; ///< index to little, toward the thumb positive
    float wrist_yaw = 24.0f;
    Q turned{};
    PitVec3 moved{};

    std::array<PitJointPose, kPitXrJoints> Joints(bool right = false) const {
        std::array<Q, kPitXrJoints> q{};
        std::array<PitVec3, kPitXrJoints> at{};
        q[kPitWrist] = Turn(1, -wrist_yaw);
        q[kPitPalm] = q[kPitMiddleMetacarpal];
        at[kPitPalm] = {0.005f, 0.0f, -0.05f};
        const std::array<PitVec3, 4> meta{{{0.014f, 0.002f, -0.015f}, {0.007f, 0.002f, -0.016f}, {-0.006f, 0.0f, -0.016f}, {-0.018f, -0.002f, -0.015f}}};
        const std::array<PitVec3, 4> knuckle{{{0.030f, 0.014f, -0.086f}, {0.009f, 0.016f, -0.086f}, {-0.011f, 0.014f, -0.081f}, {-0.030f, 0.010f, -0.072f}}};
        const std::array<float, 4> roll{3, 7, -3, 13};
        for (std::size_t f = 0; f < 4; f++) {
            const auto base = static_cast<std::size_t>(kPitIndexMetacarpal) + f * 5;
            q[base] = Turn(1, -8.0f + 8.0f * static_cast<float>(f)) * Turn(0, 10.0f);
            at[base] = meta[f];
            const float b = bend[f + 1];
            q[base + 1] = Turn(0, -b) * Turn(1, -side[f]) * Turn(2, roll[f]);
            at[base + 1] = knuckle[f];
            q[base + 2] = q[base + 1] * Turn(0, -b);
            q[base + 3] = q[base + 2] * Turn(0, -b);
            q[base + 4] = q[base + 3];
            const std::array<float, 3> length{0.043f, 0.026f, 0.022f};
            for (std::size_t k = 0; k < 3; k++) {
                const PitVec3 along = Rotated(q[base + 1 + k], {0, 0, -length[k]});
                for (std::size_t c = 0; c < 3; c++) {
                    at[base + 2 + k][c] = at[base + 1 + k][c] + along[c];
                }
            }
        }
        q[kPitThumbMetacarpal] = Turn(0, -30.0f) * Turn(1, -30.0f) * Turn(2, -90.0f);
        q[kPitThumbProximal] = q[kPitThumbMetacarpal] * Turn(0, -bend[0]);
        q[kPitThumbDistal] = q[kPitThumbTip] = q[kPitThumbProximal] * Turn(0, -bend[0]);
        std::array<PitJointPose, kPitXrJoints> out;
        for (std::size_t j = 0; j < out.size(); j++) {
            const Q o = turned * q[j];
            const PitVec3 p = Rotated(turned, at[j]);
            out[j].orientation = {o.x, o.y, o.z, o.w};
            out[j].position = {p[0] + moved[0], p[1] + moved[1], p[2] + moved[2]};
            if (right) { // x turned over
                out[j].orientation = {o.x, -o.y, -o.z, o.w};
                out[j].position[0] = -out[j].position[0];
            }
        }
        return out;
    }
};

/// A hand file with all its fingers, straight along +x, `apart` metres between them, `thick` each.
PitModel Hand(float apart, float thick) {
    Maker m;
    m.joints = {{-1, kPitForearm, 1, {-0.15f, 0, 0}, 0}, {0, kPitWrist, 1, {0, 0, 0}, 0}};
    for (int f = 0; f < 4; f++) {
        const float z = (1.2f - static_cast<float>(f)) * apart;
        const int role = kPitIndexMetacarpal + 5 * f;
        const auto first = static_cast<int>(m.joints.size());
        const float knuckle = f == 3 ? 0.070f : 0.074f;
        m.joints.push_back({1, role, 1, {0.01f, 0, z * 0.5f}, 0});
        m.joints.push_back({first, role + 1, 0, {knuckle, 0.003f, z}, thick});
        m.joints.push_back({first + 1, role + 2, 0, {knuckle + 0.042f, 0.003f, z}, thick});
        m.joints.push_back({first + 2, role + 3, 0, {knuckle + 0.060f, 0.003f, z}, thick});
        m.joints.push_back({first + 3, role + 4, 0, {knuckle + 0.078f, 0.003f, z}, thick});
    }
    const auto thumb = static_cast<int>(m.joints.size());
    m.joints.push_back({1, kPitThumbMetacarpal, 0, {0.01f, -0.015f, 0.02f}, thick});
    m.joints.push_back({thumb, kPitThumbProximal, 0, {0.045f, -0.03f, 0.05f}, thick});
    m.joints.push_back({thumb + 1, kPitThumbDistal, 0, {0.085f, -0.035f, 0.05f}, thick});
    // A vertex on the index finger's first bone and one on its second, beside it.
    m.vertices[3] = {{0.10f, 0.003f, 1.2f * apart + thick}, 3};
    m.vertices[4] = {{0.125f, 0.003f, 1.2f * apart - thick}, 4};
    PitModel out;
    std::string error;
    REQUIRE(ParsePitModel(m.Bytes(), out, error));
    return out;
}

std::size_t JointOf(const PitModel& m, int role) {
    for (std::size_t i = 0; i < m.joints.size(); i++) {
        if (m.joints[i].role == role) {
            return i;
        }
    }
    FAIL("no such joint");
    return 0;
}
/// Where a joint's bone points (its frame's -z) and where it is, with these bones.
PitVec3 Points(const PitModel& m, std::span<const PitMat34> bones, int role) {
    const std::size_t i = JointOf(m, role);
    const auto& f = m.joints[i].frame;
    const PitMat34& b = bones[i];
    const PitVec3 d{-f[2], -f[5], -f[8]};
    return {b[0] * d[0] + b[1] * d[1] + b[2] * d[2], b[4] * d[0] + b[5] * d[1] + b[6] * d[2],
            b[8] * d[0] + b[9] * d[1] + b[10] * d[2]};
}
float AngleOf(const PitVec3& a, const PitVec3& b) {
    return 57.2957795f * std::acos(std::clamp(a[0] * b[0] + a[1] * b[1] + a[2] * b[2], -1.0f, 1.0f));
}
std::vector<PitMat34> Shown(const PitModel& m, const Tracked& t, bool gap = false, bool right = false) {
    PitTracked fingers;
    REQUIRE(PitTrackedFromJoints(t.Joints(right), right, fingers));
    return PitModelEvaluateTracked(m, fingers, nullptr, gap);
}

} // namespace

TEST_CASE("Tracked fingers: an open hand is the file's open hand, whatever the runtime's wrist says",
          "[kiuhand][kiuhands]") {
    const PitModel m = Hand(0.025f, 0.010f);
    for (const float yaw : {0.0f, 24.0f, -40.0f}) {
        Tracked t;
        t.wrist_yaw = yaw;
        t.turned = Turn(1, 70.0f) * Turn(0, 25.0f) * Turn(2, -130.0f);
        t.moved = {1.0f, -0.4f, 2.0f};
        const auto bones = Shown(m, t);
        // Down the fingers: within a few degrees of straight, the middle one straight ahead.
        CHECK(AngleOf(Points(m, bones, kPitMiddleProximal), {1, 0, 0}) < 8.0f);
        for (int f = 0; f < 4; f++) {
            const PitVec3 d = Points(m, bones, kPitIndexProximal + 5 * f);
            CHECK(std::abs(std::atan2(-d[1], d[0])) * 57.3f < 8.0f);
        }
        // Fanned: each finger further to the little finger's side (-z) than the one before it.
        float last = 1.0f;
        for (int f = 0; f < 4; f++) {
            const float z = Points(m, bones, kPitIndexProximal + 5 * f)[2];
            CHECK(z < last - 0.08f);
            last = z;
        }
        // The hand stays where the file has it.
        const std::size_t knuckle = JointOf(m, kPitRingProximal);
        CHECK(Distance(Apply(bones[knuckle], m.joints[knuckle].position.data()), m.joints[knuckle].position) < 1e-5f);
    }
    // The way it was before takes the wrist's turn for the fingers': 24 degrees to one side.
    Tracked t;
    const PitHandPose old = PitHandPoseFromJoints(t.Joints(), false, 0.0f);
    const auto bones = PitModelEvaluate(m, &old, nullptr, false);
    CHECK(Points(m, bones, kPitMiddleProximal)[2] < -0.45f);
}

TEST_CASE("Tracked fingers: each moves by itself, as far as its own joints", "[kiuhand][kiuhands]") {
    const PitModel m = Hand(0.025f, 0.010f);
    const auto open = Shown(m, {});
    for (int f = 0; f < 4; f++) {
        Tracked t;
        t.bend[static_cast<std::size_t>(f) + 1] = 60.0f;
        const auto bones = Shown(m, t);
        for (int g = 0; g < 4; g++) {
            const int first = kPitIndexProximal + 5 * g;
            for (int k = 0; k < 3; k++) {
                const float moved = AngleOf(Points(m, open, first + k), Points(m, bones, first + k));
                if (g != f) {
                    CHECK(moved < 0.1f);
                }
            }
            if (g == f) {
                CHECK(AngleOf(Points(m, open, first), Points(m, bones, first)) > 50.0f);
                CHECK(AngleOf(Points(m, bones, first), Points(m, bones, first + 1)) == Approx(60.0f).margin(0.2f));
                CHECK(AngleOf(Points(m, bones, first + 1), Points(m, bones, first + 2)) == Approx(60.0f).margin(0.2f));
                // Toward the palm.
                CHECK(Points(m, bones, first)[1] < -0.7f);
            }
        }
    }
    Tracked t;
    t.bend[0] = 35.0f;
    const auto bones = Shown(m, t);
    CHECK(AngleOf(Points(m, bones, kPitThumbMetacarpal), Points(m, bones, kPitThumbProximal)) == Approx(35.0f).margin(0.2f));
    CHECK(AngleOf(Points(m, open, kPitIndexProximal), Points(m, bones, kPitIndexProximal)) < 0.1f);
}

TEST_CASE("Tracked fingers: a right hand is the left's mirror, the bones keep their lengths", "[kiuhand][kiuhands]") {
    const PitModel m = Hand(0.025f, 0.010f);
    Tracked t;
    t.bend = {20, 10, 80, 45, 30};
    t.turned = Turn(0, 40.0f) * Turn(2, 60.0f);
    t.moved = {0.3f, 1.0f, -0.5f};
    const auto left = Shown(m, t, true), right = Shown(m, t, true, true);
    REQUIRE(left.size() == right.size());
    for (std::size_t i = 0; i < left.size(); i++) {
        for (std::size_t k = 0; k < 12; k++) {
            CHECK(left[i][k] == Approx(right[i][k]).margin(1e-4f));
        }
    }
    for (std::size_t i = 0; i < m.joints.size(); i++) {
        const int parent = m.joints[i].parent;
        if (parent < 1) {
            continue;
        }
        const auto p = static_cast<std::size_t>(parent);
        CHECK(Distance(Apply(left[i], m.joints[i].position.data()), Apply(left[p], m.joints[p].position.data())) ==
              Approx(Distance(m.joints[i].position, m.joints[p].position)).margin(1e-5f));
    }
}

TEST_CASE("Tracked fingers: neighbours are held apart, no more than a little", "[kiuhand][kiuhands]") {
    Tracked t;
    t.side = {-2, -4, -6, -8}; // all along each other
    const PitModel thick = Hand(0.025f, 0.0135f), thin = Hand(0.025f, 0.009f);
    const auto before = PitModelFingerOverlap(thick, Shown(thick, t, false));
    const auto after = PitModelFingerOverlap(thick, Shown(thick, t, true));
    for (std::size_t pair = 0; pair < 3; pair++) {
        CHECK(before[pair] > 0.0f);
        CHECK(after[pair] < before[pair]);
        CHECK(after[pair] < 0.0005f);
    }
    for (int f = 0; f < 4; f++) {
        CHECK(AngleOf(Points(thick, Shown(thick, t, false), kPitIndexProximal + 5 * f),
                      Points(thick, Shown(thick, t, true), kPitIndexProximal + 5 * f)) < kPitFingerAside + 0.1f);
    }
    // Fingers with room are not turned.
    const auto with = Shown(thin, t, true), without = Shown(thin, t, false);
    for (std::size_t i = 0; i < with.size(); i++) {
        for (std::size_t k = 0; k < 12; k++) {
            CHECK(with[i][k] == Approx(without[i][k]).margin(1e-6f));
        }
    }
}

TEST_CASE("Tracked fingers: joints that make no hand are left alone; easing ends at the target", "[kiuhand][kiuhands]") {
    PitTracked out;
    std::array<PitJointPose, kPitXrJoints> nowhere{};
    CHECK_FALSE(PitTrackedFromJoints(nowhere, false, out)); // all in one place
    auto bad = Tracked{}.Joints();
    bad[kPitRingDistal].orientation = {0, 0, 0, 0};
    CHECK_FALSE(PitTrackedFromJoints(bad, false, out));
    bad = Tracked{}.Joints();
    bad[kPitMiddleProximal].position[1] = std::nanf("");
    CHECK_FALSE(PitTrackedFromJoints(bad, false, out));
    Tracked closed;
    closed.bend = {30, 70, 70, 70, 70};
    PitTracked a, b;
    REQUIRE(PitTrackedFromJoints(Tracked{}.Joints(), false, a));
    REQUIRE(PitTrackedFromJoints(closed.Joints(), false, b));
    CHECK(PitTrackedDiffers(a, b));
    CHECK_FALSE(PitTrackedDiffers(b, b));
    CHECK_FALSE(PitTrackedDiffers(PitTrackedEase(a, b, 1.0f, 0.015f), b));
    CHECK(PitTrackedDiffers(PitTrackedEase(a, b, 0.002f, 0.015f), b));
    CHECK_FALSE(PitTrackedDiffers(PitTrackedEase(a, b, 0.01f, 0.0f), b));
}

TEST_CASE("Tracked fingers: a moment without joints keeps the last ones, longer does not", "[kiuhand][kiuhands]") {
    float lost = 0.0f;
    CHECK(PitFingersKeep(false, false, 0.011f, lost) == PitFingersFrom::None); // never had any
    CHECK(PitFingersKeep(true, false, 0.011f, lost) == PitFingersFrom::Joints);
    for (int frame = 0; frame < 20; frame++) { // 0.22 s at 90 a second
        CHECK(PitFingersKeep(false, true, 0.011f, lost) == PitFingersFrom::Held);
    }
    CHECK(PitFingersKeep(true, true, 0.011f, lost) == PitFingersFrom::Joints);
    CHECK(lost == 0.0f);
    for (int frame = 0; frame < 22; frame++) {
        PitFingersKeep(false, true, 0.011f, lost);
    }
    CHECK(PitFingersKeep(false, true, 0.011f, lost) == PitFingersFrom::None);
    // The kept bones are the same as a new list's.
    const PitModel m = Hand(0.025f, 0.010f);
    Tracked t;
    t.bend = {20, 10, 80, 45, 30};
    PitTracked fingers;
    REQUIRE(PitTrackedFromJoints(t.Joints(), false, fingers));
    std::vector<PitMat34> kept(3);
    PitModelEvaluateTracked(m, fingers, nullptr, true, kept);
    CHECK(kept == PitModelEvaluateTracked(m, fingers, nullptr, true));
}

TEST_CASE("Hand file: normals meet over a seam, not over an edge", "[kiuhand][kiuhands]") {
    PitModel m = Good();
    const auto put = [&m](std::size_t v, float x, float nx, float ny) {
        float* p = &m.vertices[v * kPitVertexFloats];
        p[0] = x;
        p[1] = p[2] = 0.0f;
        p[3] = nx;
        p[4] = ny;
        p[5] = 0.0f;
    };
    put(0, 0.5f, 0.2079f, 0.9781f); // 12 degrees either side of +y
    put(1, 0.5f, -0.2079f, 0.9781f);
    put(2, 0.6f, 1.0f, 0.0f); // square to each other
    put(3, 0.6f, 0.0f, 1.0f);
    CHECK(PitModelSmoothSeams(m) == 2);
    CHECK(m.vertices[3] == Approx(0.0f).margin(1e-4f));
    CHECK(m.vertices[4] == Approx(1.0f).margin(1e-4f));
    CHECK(m.vertices[kPitVertexFloats + 4] == Approx(1.0f).margin(1e-4f));
    CHECK(m.vertices[2 * kPitVertexFloats + 3] == 1.0f);
    CHECK(m.vertices[3 * kPitVertexFloats + 4] == 1.0f);
    CHECK(PitModelSmoothSeams(m) == 0);
}

TEST_CASE("Hand file: fingers drawn narrower keep their length and their middle", "[kiuhand][kiuhands]") {
    CHECK(PitFingerWidth(std::nullopt) == kPitFingerWidth);
    CHECK(PitFingerWidth(std::string{"85"}) == 85);
    CHECK(PitFingerWidth(std::string{"100"}) == 100);
    CHECK(PitFingerWidth(std::string{"20"}) == 60);
    CHECK(PitFingerWidth(std::string{"x"}) == kPitFingerWidth);
    CHECK(PitFingerWidth(std::string{""}) == kPitFingerWidth);
    CHECK(PitFingerWidth(std::string{"99999999999"}) == kPitFingerWidth);
    const PitModel m = Hand(0.025f, 0.012f);
    const PitModel n = PitModelThinned(m, 0.75f);
    CHECK(n.vertices.size() == m.vertices.size());
    CHECK(n.colour.empty());
    // On the second bone the whole of it, on the first the more the further from the knuckle.
    const float z = 1.2f * 0.025f;
    CHECK(n.vertices[4 * kPitVertexFloats + 2] == Approx(z - 0.009f).margin(1e-5f));
    CHECK(n.vertices[4 * kPitVertexFloats] == Approx(m.vertices[4 * kPitVertexFloats]).margin(1e-6f));
    CHECK(n.vertices[3 * kPitVertexFloats + 2] < z + 0.0119f);
    CHECK(n.vertices[3 * kPitVertexFloats + 2] > z + 0.0089f);
    CHECK(n.vertices[0] == m.vertices[0]); // the forearm as it was
    CHECK(n.joints[JointOf(m, kPitIndexDistal)].radius == Approx(0.009f));
    CHECK(n.joints[JointOf(m, kPitThumbDistal)].radius == Approx(0.012f));
}
