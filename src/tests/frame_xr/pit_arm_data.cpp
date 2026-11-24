// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuhand: Pit's drawn arm as data (frame_xr/vis/pit_arm_data.inc, made by
// tools/kiu_hand/make_arm.py): within its budget, every index, joint and weight sound, the
// plate in its own range at the end, the joints as the model file wants them (pit_model.h) and
// one for every role the module poses (pit_hand.h).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "frame_xr/vis/pit_arm.h"
#include "frame_xr/vis/pit_hand.h"

using Catch::Approx;
using namespace FrameXr::Vis;

namespace {

#include "frame_xr/vis/pit_arm_data.inc"

std::array<float, 3> Position(uint32_t v) {
    return {kPitArmPos[v * 3] * kPitArmPosScale[0] + kPitArmPosMin[0],
            kPitArmPos[v * 3 + 1] * kPitArmPosScale[1] + kPitArmPosMin[1],
            kPitArmPos[v * 3 + 2] * kPitArmPosScale[2] + kPitArmPosMin[2]};
}

} // namespace

TEST_CASE("Pit's drawn arm: within its budget, indices and weights sound", "[kiuhand]") {
    REQUIRE(kPitArmVertexCount == std::size(kPitArmPos) / 3);
    REQUIRE(kPitArmVertexCount == std::size(kPitArmNormal) / 3);
    REQUIRE(kPitArmVertexCount == std::size(kPitArmTangent) / 4);
    REQUIRE(kPitArmVertexCount == std::size(kPitArmUv) / 2);
    REQUIRE(kPitArmVertexCount == std::size(kPitArmBone) / 4);
    REQUIRE(kPitArmVertexCount == std::size(kPitArmWeight) / 4);
    REQUIRE(kPitArmTriangleCount == std::size(kPitArmIndex) / 3);
    CHECK(kPitArmTriangleCount <= 7000);
    CHECK(kPitArmPlateTriangleCount > 0);
    CHECK(kPitArmPlateTriangleCount < kPitArmTriangleCount);
    CHECK(kPitArmTextureSize <= 1024);
    for (const uint16_t i : kPitArmIndex) {
        REQUIRE(i < kPitArmVertexCount);
    }
    for (uint32_t v = 0; v < kPitArmVertexCount; v++) {
        int sum = 0;
        for (int k = 0; k < 4; k++) {
            REQUIRE(kPitArmBone[v * 4 + k] < kPitArmJointCount);
            sum += kPitArmWeight[v * 4 + k];
            if (k > 0) {
                REQUIRE(kPitArmWeight[v * 4 + k] <= kPitArmWeight[v * 4 + k - 1]);
            }
        }
        REQUIRE(sum == 255);
        const float n = std::sqrt(static_cast<float>(kPitArmNormal[v * 3] * kPitArmNormal[v * 3] +
                                                     kPitArmNormal[v * 3 + 1] * kPitArmNormal[v * 3 + 1] +
                                                     kPitArmNormal[v * 3 + 2] * kPitArmNormal[v * 3 + 2])) /
                        127.0f;
        REQUIRE(n == Approx(1.0f).margin(0.03f));
        REQUIRE(std::abs(kPitArmTangent[v * 4 + 3]) == 127);
    }
    // No triangle without area.
    for (uint32_t t = 0; t < kPitArmTriangleCount; t++) {
        const auto a = Position(kPitArmIndex[t * 3]), b = Position(kPitArmIndex[t * 3 + 1]),
                   c = Position(kPitArmIndex[t * 3 + 2]);
        const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        const float cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
        REQUIRE(cx * cx + cy * cy + cz * cz > 0.0f);
    }
}

TEST_CASE("Pit's drawn arm: the plate is the last range and shares no vertex with the body", "[kiuhand]") {
    const uint32_t body = (kPitArmTriangleCount - kPitArmPlateTriangleCount) * 3;
    std::set<uint16_t> in_body(kPitArmIndex, kPitArmIndex + body);
    float least_y = 1.0f;
    for (uint32_t i = body; i < kPitArmTriangleCount * 3; i++) {
        REQUIRE(in_body.count(kPitArmIndex[i]) == 0);
        least_y = std::min(least_y, Position(kPitArmIndex[i])[1]);
    }
    // It lies over the back of the wrist (+y).
    CHECK(least_y > 0.0f);
}

TEST_CASE("Pit's drawn arm: the joints are the model file's and cover the module's roles", "[kiuhand]") {
    REQUIRE(kPitArmJointCount == std::size(kPitArmJointParent));
    REQUIRE(kPitArmJointCount == std::size(kPitArmJointRole));
    REQUIRE(kPitArmJointCount == std::size(kPitArmJointFrame) / 9);
    std::set<int> roles;
    for (uint32_t j = 0; j < kPitArmJointCount; j++) {
        REQUIRE(kPitArmJointParent[j] >= -1);
        REQUIRE(kPitArmJointParent[j] < static_cast<int>(j));
        REQUIRE(kPitArmJointRole[j] >= 0);
        REQUIRE(kPitArmJointRole[j] < kPitHandJoints);
        REQUIRE(roles.insert(kPitArmJointRole[j]).second);
        // The same parent as the module's joint of that role.
        const int parent = PitHandParent(kPitArmJointRole[j]);
        CHECK((kPitArmJointParent[j] < 0 ? -1 : kPitArmJointRole[kPitArmJointParent[j]]) == parent);
        // The frame is a rotation.
        const float* f = &kPitArmJointFrame[j * 9];
        for (int a = 0; a < 3; a++) {
            for (int b = 0; b < 3; b++) {
                const float dot = f[a * 3] * f[b * 3] + f[a * 3 + 1] * f[b * 3 + 1] + f[a * 3 + 2] * f[b * 3 + 2];
                REQUIRE(dot == Approx(a == b ? 1.0f : 0.0f).margin(1e-4f));
            }
        }
    }
    CHECK(roles.size() == static_cast<std::size_t>(kPitHandJoints));
    // The hand is as long as the forearm, the elbow behind the wrist.
    CHECK(kPitArmElbow[0] == Approx(-kPitArmHandLength).margin(0.002f));
    // The handle lies under the palm, and the fit is a rigid placing.
    CHECK(kPitArmHandle[1] < 0.0f);
    CHECK(kPitArmHandle[6] > 0.01f);
    const float axis = kPitArmHandle[3] * kPitArmHandle[3] + kPitArmHandle[4] * kPitArmHandle[4] +
                       kPitArmHandle[5] * kPitArmHandle[5];
    CHECK(axis == Approx(1.0f).margin(1e-4f));
    for (int a = 0; a < 3; a++) {
        const float len = kPitArmFit[a * 4] * kPitArmFit[a * 4] + kPitArmFit[a * 4 + 1] * kPitArmFit[a * 4 + 1] +
                          kPitArmFit[a * 4 + 2] * kPitArmFit[a * 4 + 2];
        CHECK(len == Approx(1.0f).margin(1e-4f));
    }
}

TEST_CASE("Pit's drawn arm: the app reads it as it reads a hand file", "[kiuhand]") {
    PitModel m;
    std::string error;
    REQUIRE(BuiltInPitModel(m, error));
    CHECK(error.empty());
    CHECK(m.vertex_count == kPitArmVertexCount);
    CHECK(m.plate_index_count == kPitArmPlateTriangleCount * 3);
    CHECK(m.body_index_count + m.plate_index_count == kPitArmTriangleCount * 3);
    CHECK(m.texture_width == kPitArmTextureSize);
    CHECK(m.colour.size() == std::size_t{4} * kPitArmTextureSize * kPitArmTextureSize);
    CHECK(m.surface.size() == m.colour.size());
    // Every pose of the app's, on each hand.
    for (int n = 0; n < static_cast<int>(PitHandNamed::Count); n++) {
        std::vector<float> out(std::size_t{m.vertex_count} * kPitVertexFloats);
        const PitHandPose& pose = PitHandNamedPose(static_cast<PitHandNamed>(n));
        PitModelSkin(m, PitModelEvaluate(m, &pose), out);
        float longest = 0.0f;
        for (uint32_t v = 0; v < m.vertex_count; v++) {
            for (int k = 0; k < 3; k++) {
                REQUIRE(std::isfinite(out[v * kPitVertexFloats + k]));
                longest = std::max(longest, std::abs(out[v * kPitVertexFloats + k]));
            }
        }
        CHECK(longest < 0.3f);
    }
}
