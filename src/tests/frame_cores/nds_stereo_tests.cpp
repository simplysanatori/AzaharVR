// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <cstdint>
#include <initializer_list>

#include <catch2/catch_test_macros.hpp>

#include "frame_cores/nds_stereo_3d.h"
#include "frame_cores/nds_stereo_renderer.h"

using namespace FrameCores::NdsDepth;

namespace {

constexpr uint16_t Attr2(int priority) {
    return static_cast<uint16_t>((priority << 10) | 0xF123); // palette and tile besides
}

/// As the 2D renderer reads it
constexpr int SpriteX(uint16_t attr1) {
    const int x = attr1 & 0x1FF;
    return x >= 256 ? x - 512 : x;
}

} // namespace

TEST_CASE("NDS Layer Depth: shift by level and priority", "[frame_cores]") {
    for (int priority = 0; priority < 4; ++priority) {
        CHECK(LayerShift(0, priority) == 0);
        CHECK(LayerShift(1, priority) == priority);
        CHECK(LayerShift(3, priority) == 3 * priority);
        CHECK(LayerShift(-1, priority) == 0);
        CHECK(LayerShift(9, priority) == LayerShift(kMaxLevel, priority));
    }
    for (int level = 0; level <= kMaxLevel; ++level) {
        CHECK(LayerShift(level, 0) == 0);
    }
}

TEST_CASE("NDS Layer Depth: text backgrounds", "[frame_cores]") {
    CHECK(ShiftScroll(100, 0) == 100);
    CHECK(ShiftScroll(100, 9) == 91);
    // Maps are 256 or 512 pixels wide: the wrapped register shows the same column
    CHECK((ShiftScroll(0, 9) & 0x1FF) == 512 - 9);
    CHECK((ShiftScroll(0, 9) & 0xFF) == 256 - 9);
    CHECK((ShiftScroll(0x1FF, 1) & 0x1FF) == 0x1FE);
}

TEST_CASE("NDS Layer Depth: rotated and scaled backgrounds", "[frame_cores]") {
    constexpr AffinePoint ref{0x1234, -0x5678};

    CHECK(ShiftAffine(ref, 0x100, 0, 0) == ref);
    CHECK(ShiftAffine(ref, 0x100, 0, 3) == AffinePoint{ref.x - 0x300, ref.y}); // no rotation
    CHECK(ShiftAffine(ref, 0, 0x100, 3) == AffinePoint{ref.x, ref.y - 0x300}); // a quarter turn
    CHECK(ShiftAffine(ref, 0x200, 0, 3) == AffinePoint{ref.x - 0x600, ref.y}); // half size

    // Pixel x + shift of the right eye is pixel x of the left one
    for (const int rot_a : {-32768, -0x100, 0, 0xB5, 32767}) {
        for (const int rot_c : {-32768, -0xB5, 0, 0x100, 32767}) {
            for (int shift = 0; shift <= 9; ++shift) {
                const auto moved = ShiftAffine(ref, static_cast<int16_t>(rot_a),
                                               static_cast<int16_t>(rot_c), shift);
                constexpr int x = 40;
                CHECK(moved.x + (x + shift) * rot_a == ref.x + x * rot_a);
                CHECK(moved.y + (x + shift) * rot_c == ref.y + x * rot_c);
            }
        }
    }
}

TEST_CASE("NDS Layer Depth: sprites", "[frame_cores]") {
    constexpr uint16_t kNormal = 0x0040;
    constexpr uint16_t kAffine = 0x0140;
    constexpr uint16_t kDoubleSize = 0x0340;
    constexpr uint16_t kDisabled = 0x0240;
    constexpr uint16_t kWindow = 0x0840;
    constexpr uint16_t kFlags = 0xFE00; // size, flips, rotation: all but x

    SECTION("by their own priority") {
        for (const uint16_t attr0 : {kNormal, kAffine, kDoubleSize}) {
            CHECK(ShiftSprite(attr0, 100, Attr2(0), 3) == 100);
            CHECK(ShiftSprite(attr0, 100, Attr2(1), 3) == 103);
            CHECK(ShiftSprite(attr0, 100, Attr2(3), 3) == 109);
        }
    }

    SECTION("level 0 changes nothing, and the other bits stay") {
        for (int x = 0; x < 512; ++x) {
            const auto attr1 = static_cast<uint16_t>(kFlags | x);
            CHECK(ShiftSprite(kNormal, attr1, Attr2(3), 0) == attr1);
            CHECK((ShiftSprite(kNormal, attr1, Attr2(3), 3) & kFlags) == kFlags);
        }
    }

    SECTION("window sprites and disabled ones stay") {
        CHECK(ShiftSprite(kWindow, 100, Attr2(3), 3) == 100);
        CHECK(ShiftSprite(kDisabled, 100, Attr2(3), 3) == 100);
    }

    SECTION("across the left edge") {
        const uint16_t moved = ShiftSprite(kNormal, 0x1FC, Attr2(3), 3); // -4
        CHECK(SpriteX(moved) == 5);
        CHECK(moved == 5); // no carry into the bits above x
    }

    SECTION("past the right edge: wrapped, and off the screen") {
        CHECK(ShiftSprite(kNormal, 255, Attr2(1), 1) == 0x100);
        CHECK(SpriteX(0x100) == -256);
        for (int x = 247; x < 256; ++x) {
            const int moved =
                SpriteX(ShiftSprite(kNormal, static_cast<uint16_t>(x), Attr2(3), 3));
            CHECK((moved == x + 9 || moved <= -128)); // 128: the widest, at double size
        }
    }
}

TEST_CASE("NDS Layer Depth: 3D by level", "[frame_cores]") {
    CHECK(Depth3DForLevel(0).strength == 0);
    for (int level = 1; level <= kMaxLevel; ++level) {
        const Depth3D depth = Depth3DForLevel(level);
        CHECK(depth.strength == LayerShift(level, 3)); // as deep as the backmost 2D layer
        CHECK(depth.limit == depth.strength);
        CHECK(depth.convergence == 4 * kOneW);
    }
    CHECK(Depth3DForLevel(9) == Depth3DForLevel(kMaxLevel));
}

TEST_CASE("NDS Layer Depth: 3D vertices", "[frame_cores]") {
    constexpr Depth3D kDepth{8, 4 * kOneW, 6};
    constexpr int32_t kMaxW = 0xFFFFFF;

    SECTION("on the screen at the convergence, behind it further away, before it nearer") {
        CHECK(VertexShift(kDepth, 4 * kOneW) == 0);
        CHECK(VertexShift(kDepth, 8 * kOneW) == 4);
        CHECK(VertexShift(kDepth, 2 * kOneW + kOneW / 2) == -5); // 8 * (1 - 1.6) = -4.8
        CHECK(VertexShift({8, 4 * kOneW, 20}, 2 * kOneW) == -8);
    }

    SECTION("limits") {
        CHECK(VertexShift(kDepth, kMaxW) == 6);
        CHECK(VertexShift(kDepth, 1) == -6);
        CHECK(VertexShift({8, 4 * kOneW, 8}, kMaxW) == 8); // far away is the strength
        CHECK(VertexShift(kDepth, 0) == 0);
    }

    SECTION("strength 0 moves nothing") {
        for (const int32_t w : {1, kOneW, 64 * kOneW, kMaxW}) {
            CHECK(VertexShift({0, 4 * kOneW, 6}, w) == 0);
        }
    }

    SECTION("rounded to the nearest, the same on both sides") {
        constexpr Depth3D one{1, 4 * kOneW, 9};
        CHECK(VertexShift(one, 8 * kOneW - 8) == 0); // just under a half
        CHECK(VertexShift(one, 8 * kOneW) == 1);     // a half
        CHECK(VertexShift(one, 2 * kOneW) == -1);
        CHECK(VertexShift(one, 3 * kOneW) == 0); // -1/3
    }

    SECTION("never less for a larger w") {
        int last = -kDepth.limit;
        for (int32_t w = 1; w <= 1024 * kOneW; w += 37) {
            const int shift = VertexShift(kDepth, w);
            if (shift < last) {
                FAIL("w " << w << ": " << shift << " after " << last);
            }
            last = shift;
        }
        CHECK(last == kDepth.limit);
    }

    SECTION("x stays in the rasteriser's range") {
        CHECK(ShiftVertexX(100, 6) == 106);
        CHECK(ShiftVertexX(100, -6) == 94);
        CHECK(ShiftVertexX(3, -6) == 0);      // below 0 it would write into the row above
        CHECK(ShiftVertexX(256, 6) == 262);   // the rasteriser cuts at 256 by itself
        CHECK(ShiftVertexX(kMaxX, 6) == kMaxX);
        CHECK(ShiftVertexX(100, 0) == 100);
    }

    SECTION("vertices at one depth keep their order, also at the edges of the screen") {
        for (const int32_t w : {1, 3 * kOneW, 4 * kOneW, 600 * kOneW}) {
            const int shift = VertexShift(kDepth, w);
            int last = 0;
            for (int32_t x = 0; x <= kMaxX; ++x) {
                const int moved = ShiftVertexX(x, shift);
                if (moved < last) {
                    FAIL("x " << x << " at w " << w);
                }
                last = moved;
            }
        }
    }

    SECTION("edges of different depths can cross") {
        // A thin polygon, its left edge far away: the rasteriser draws swapped spans, as the DS
        // does for crossed edges. Both stay in range.
        const int left = ShiftVertexX(100, VertexShift(kDepth, kMaxW));
        const int right = ShiftVertexX(101, VertexShift(kDepth, kOneW));
        CHECK(left == 106);
        CHECK(right == 95);
        // Wider than both limits, no two can
        CHECK(ShiftVertexX(100, kDepth.limit) <= ShiftVertexX(100 + 2 * kDepth.limit,
                                                              -kDepth.limit));
    }
}

TEST_CASE("NDS Layer Depth: orthographic polygons stay", "[frame_cores]") {
    constexpr int32_t ortho[] = {kOneW, kOneW, kOneW, kOneW};
    constexpr int32_t facing[] = {60 * kOneW, 60 * kOneW, 60 * kOneW}; // a billboard: in the scene
    constexpr int32_t last_differs[] = {kOneW, kOneW, kOneW + 1};
    CHECK(IsOnScreen(ortho, 4));
    CHECK_FALSE(IsOnScreen(facing, 3));
    CHECK_FALSE(IsOnScreen(last_differs, 3));
    CHECK(IsOnScreen(last_differs, 2));
}
