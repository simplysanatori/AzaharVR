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

TEST_CASE("LayerDepth: inverted, the layers are in the opposite order, as far apart",
          "[common][layer_depth]") {
    CHECK(!Get().inverted.load());
    for (uint32_t level = 0; level <= kMaxLevel; ++level) {
        const auto shift = [level](uint32_t step, bool inverted) {
            return Directed(LayerShift(level, step, 3, 240, kNoLimit), inverted);
        };
        for (uint32_t step = 1; step <= 3; ++step) {
            const int32_t apart = shift(step, false) - shift(step - 1, false);
            CHECK(apart == static_cast<int32_t>(level));
            CHECK(shift(step, true) - shift(step - 1, true) == -apart);
        }
    }
}

TEST_CASE("LayerDepth: a screen that takes touches has its pictures where they are",
          "[common][layer_depth]") {
    constexpr int kWide = 256;
    for (const int farthest : {-9, -6, -3, -1, 0, 1, 3, 9}) {
        const float shift = static_cast<float>(farthest) / kWide;
        const int cut = std::abs(farthest);
        for (const bool right : {false, true}) {
            INFO("farthest " << farthest << " right " << right);
            const int first = CutsRightEdge(right, shift) ? 0 : cut;
            const float middle = kWide * (0.5f + OffsetInPlace(right, shift));
            // A column is where it is in the whole picture, so it is what a tap there touches
            CHECK(middle - static_cast<float>(kWide - cut) / 2 ==
                  Approx(static_cast<float>(first)).margin(1e-4));
        }
        if (farthest < 0) { // as the other screen's then
            CHECK(OffsetInPlace(false, shift) == Offset(false, shift));
            CHECK(OffsetInPlace(true, shift) == Offset(true, shift));
        }
    }
}

TEST_CASE("LayerDepth: what is left of the pictures is a window where the nearest layer is",
          "[common][layer_depth]") {
    CHECK(Cut(0.0f) == 0.0f);
    CHECK(Offset(false, 0.0f) == 0.0f);
    CHECK(Offset(true, 0.0f) == 0.0f);
    for (const bool gba : {true, false}) {
        const int width = gba ? 240 : 160;
        const uint32_t steps = gba ? 3 : 2;
        for (const bool inverted : {false, true}) {
            for (uint32_t level = 0; level <= kMaxLevel; ++level) {
                INFO("width " << width << " inverted " << inverted << " level " << level);
                const auto moved = [&](uint32_t step) {
                    return Directed(
                        LayerShift(level, step, steps, static_cast<uint32_t>(width), kNoLimit),
                        inverted);
                };
                const int farthest = moved(steps);
                const float shift = static_cast<float>(farthest) / static_cast<float>(width);
                const int cut = static_cast<int>(std::lround(Cut(shift) * width));
                REQUIRE(cut == std::abs(farthest));
                struct Eye {
                    int first, last; ///< the picture's columns that are shown
                    float left;      ///< the screen's column the first one is at
                };
                const auto eye = [&](bool right) {
                    const bool at_right = CutsRightEdge(right, shift);
                    const int first = at_right ? 0 : cut;
                    const float middle = width * (0.5f + Offset(right, shift));
                    return Eye{first, at_right ? width - 1 - cut : width - 1,
                               middle - static_cast<float>(width - cut) / 2};
                };
                const Eye left = eye(false);
                const Eye right = eye(true);
                REQUIRE(left.last - left.first == right.last - right.first);
                const int nearest = std::min(0, farthest);
                // Nothing beyond the game's picture is in either eye's
                for (uint32_t step = 0; step <= steps; ++step) {
                    REQUIRE(right.first - moved(step) >= 0);
                    REQUIRE(right.last - moved(step) < width);
                }
                // The window's edges are as far apart as the nearest layer's two pictures, and in
                // the middle of the screen
                REQUIRE(right.left - left.left == Approx(nearest).margin(1e-3));
                REQUIRE((right.left + left.left) / 2 + static_cast<float>(width - cut) / 2 ==
                        Approx(width / 2.0f));
                // Both eyes have the same part of it
                REQUIRE(right.first - nearest == left.first);
                REQUIRE(right.last - nearest == left.last);
                // Its pictures are as far apart on the screen as in the pictures
                REQUIRE((right.left - right.first) - (left.left - left.first) ==
                        Approx(0.0f).margin(1e-3));
            }
        }
    }
}

TEST_CASE("LayerDepth: the pictures are cut, or their layers end; never both",
          "[common][layer_depth]") {
    for (int32_t shift = -9; shift <= 9; ++shift) {
        const bool cut = kCutsEdge || shift < 0;
        CHECK(EdgeShift(shift, 240) == (cut ? Approx(shift / 240.0) : Approx(0.0)));
        CHECK(EdgeColumns(shift) == (cut ? 0 : shift));
    }
}
