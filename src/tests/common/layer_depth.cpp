// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "common/layer_depth.h"

using Catch::Approx;
using namespace Common::LayerDepth;

TEST_CASE("LayerDepth: the limit from the IPD and the screen's width", "[common][layer_depth]") {
    CHECK(MaxShift(0.068f, 2.4f) == Approx(0.068 / 2.4));
    CHECK(MaxShift(0.085f, 0.05f) == kNoLimit); // a screen narrower than the eyes are apart
    for (const float bad : {0.0f, -1.0f}) {
        CHECK(MaxShift(bad, 2.4f) == kOnScreen);
        CHECK(MaxShift(0.068f, bad) == kOnScreen);
    }
    CHECK(LayerShift(3, 3, 3, 240, kOnScreen) == 0);
    CHECK(LayerShift(3, 3, 3, 240, 0.0f) == 0);
    CHECK(LayerShift(3, 3, 0, 240, kNoLimit) == 0);
    CHECK(LayerShift(0, 3, 3, 240, kNoLimit) == 0);
    for (uint32_t level = 1; level <= kMaxLevel; ++level) {
        for (uint32_t step = 0; step <= 3; ++step) {
            CHECK(LayerShift(level, step, 3, 240, kNoLimit) == level * step);
        }
    }
    CHECK(LayerShift(9, 3, 3, 240, kNoLimit) == kMaxLevel * 3);
}

TEST_CASE("LayerDepth: the widest screen with a level's whole shift", "[common][layer_depth]") {
    struct Width {
        float ipd, px, level1, level2, level3;
    };
    for (const Width& w : {
             Width{0.063f, 240, 5.04f, 2.52f, 1.68f},
             Width{0.068f, 240, 5.44f, 2.72f, 1.8133f},
             Width{0.070f, 240, 5.60f, 2.80f, 1.8667f},
             Width{0.063f, 256, 5.376f, 2.688f, 1.792f},
             Width{0.068f, 256, 5.8027f, 2.9013f, 1.9342f},
             Width{0.070f, 256, 5.9733f, 2.9867f, 1.9911f},
         }) {
        const float widest[] = {w.level1, w.level2, w.level3};
        for (uint32_t level = 1; level <= 3; ++level) {
            INFO(w.ipd << " m, " << w.px << " px, level " << level);
            const auto shift = [&](float width_m) {
                return LayerShift(level, 3, 3, static_cast<uint32_t>(w.px),
                                  MaxShift(w.ipd, width_m));
            };
            CHECK(shift(widest[level - 1] * 0.999f) == 3 * level);
            CHECK(shift(widest[level - 1] * 1.001f) < 3 * level);
        }
    }
}

TEST_CASE("LayerDepth: the eyes never turn outwards", "[common][layer_depth]") {
    for (const uint32_t px : {160u, 240u, 256u}) {
        const uint32_t farthest = px == 160 ? 2 : 3;
        for (float ipd = 0.045f; ipd <= 0.0851f; ipd += 0.001f) {
            for (float width = 0.2f; width < 12.0f; width *= 1.03f) {
                const float limit = MaxShift(ipd, width);
                uint32_t below = 0; // the same layer, a level lower
                for (uint32_t level = 1; level <= kMaxLevel; ++level) {
                    uint32_t nearer = 0;
                    for (uint32_t step = 0; step <= farthest; ++step) {
                        const uint32_t shift = LayerShift(level, step, farthest, px, limit);
                        const float apart =
                            static_cast<float>(shift) / static_cast<float>(px) * width;
                        REQUIRE(apart <= ipd * 1.00001f);
                        REQUIRE(shift <= level * step);
                        REQUIRE(shift >= nearer);
                        nearer = shift;
                    }
                    REQUIRE(nearer >= below);
                    below = nearer;
                }
            }
        }
    }
}

TEST_CASE("LayerDepth: the farthest layer is straight ahead", "[common][layer_depth]") {
    CHECK(Recentre(0.0f, 2.4f) == 0.0f);
    for (const float ipd : {0.063f, 0.068f, 0.070f}) {
        for (const float width : {0.8f, 1.6f, 2.4f, 6.0f}) {
            const float distance = 3.0f;
            const uint32_t px = LayerShift(3, 3, 3, 240, MaxShift(ipd, width));
            const float share = static_cast<float>(px) / 240.0f;
            const float move = Recentre(share, width);
            // A point of the farthest layer in the picture's middle, on the quad in metres
            const float left = move;
            const float right = move + share * width;
            CHECK(left == Approx(-right).margin(1e-6));
            CHECK(right - left == Approx(px / 240.0 * width)); // moving both changes no depth
            // Where the eyes' rays through the two meet: ahead, at or beyond the screen
            const float apart = right - left;
            if (apart < ipd * 0.9999f) {
                const float meets = distance * ipd / (ipd - apart);
                CHECK(meets >= distance);
                const float t = meets / distance;
                const float x_left = -ipd / 2 + (left + ipd / 2) * t;
                const float x_right = ipd / 2 + (right - ipd / 2) * t;
                CHECK(x_left == Approx(0.0f).margin(1e-3));
                CHECK(x_right == Approx(0.0f).margin(1e-3));
            }
            // A layer on the screen stays on it
            CHECK(std::abs(move) == Approx(apart / 2));
        }
    }
}
