// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "common/alt_core_types.h"
#include "common/layer_depth.h"
#include "frame_cores/core_backend.h"
#include "frame_cores/nds_core.h"
#include "frame_cores/nds_depth.h"

namespace fs = std::filesystem;
using namespace FrameCores::NdsDepth;
using Common::LayerDepth::kMaxLevel;

namespace {

constexpr uint16_t Attr2(int priority) {
    return static_cast<uint16_t>((priority << 10) | 0xF123); // palette and tile besides
}

constexpr LayerShifts Shifts(uint32_t level) {
    return MakeDepth(level, Common::LayerDepth::kNoLimit).layers;
}

/// As the 2D renderer reads it
constexpr int SpriteX(uint16_t attr1) {
    const int x = attr1 & 0x1FF;
    return x >= 256 ? x - 512 : x;
}

} // namespace

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
            CHECK(ShiftSprite(attr0, 100, Attr2(0), Shifts(3)) == 100);
            CHECK(ShiftSprite(attr0, 100, Attr2(1), Shifts(3)) == 103);
            CHECK(ShiftSprite(attr0, 100, Attr2(3), Shifts(3)) == 109);
        }
    }

    SECTION("level 0 changes nothing, and the other bits stay") {
        for (int x = 0; x < 512; ++x) {
            const auto attr1 = static_cast<uint16_t>(kFlags | x);
            CHECK(ShiftSprite(kNormal, attr1, Attr2(3), LayerShifts{}) == attr1);
            CHECK((ShiftSprite(kNormal, attr1, Attr2(3), Shifts(3)) & kFlags) == kFlags);
        }
    }

    SECTION("window sprites and disabled ones stay") {
        CHECK(ShiftSprite(kWindow, 100, Attr2(3), Shifts(3)) == 100);
        CHECK(ShiftSprite(kDisabled, 100, Attr2(3), Shifts(3)) == 100);
    }

    SECTION("across the left edge") {
        const uint16_t moved = ShiftSprite(kNormal, 0x1FC, Attr2(3), Shifts(3)); // -4
        CHECK(SpriteX(moved) == 5);
        CHECK(moved == 5); // no carry into the bits above x
    }

    SECTION("past the right edge: wrapped, and off the screen") {
        CHECK(ShiftSprite(kNormal, 255, Attr2(1), Shifts(1)) == 0x100);
        CHECK(SpriteX(0x100) == -256);
        for (int x = 247; x < 256; ++x) {
            const int moved =
                SpriteX(ShiftSprite(kNormal, static_cast<uint16_t>(x), Attr2(3), Shifts(3)));
            CHECK((moved == x + 9 || moved <= -128)); // 128: the widest, at double size
        }
    }
}

TEST_CASE("NDS Layer Depth: by level and limit", "[frame_cores]") {
    using namespace Common::LayerDepth;
    CHECK_FALSE(MakeDepth(0, kNoLimit).on);
    CHECK_FALSE(MakeDepth(3, 0.0f).on); // the window
    CHECK(Shifts(1) == LayerShifts{0, 1, 2, 3});
    CHECK(Shifts(3) == LayerShifts{0, 3, 6, 9});
    CHECK(Shifts(9) == Shifts(kMaxLevel));
    for (uint32_t level = 1; level <= kMaxLevel; ++level) {
        const Depth depth = MakeDepth(level, kNoLimit);
        CHECK(depth.on);
        CHECK(depth.scene == SceneShift{depth.layers[3], 4 * kOneW, depth.layers[3]});
    }

    // The screen is not known yet: drawn, with nothing moved
    CHECK(MakeDepth(3, kOnScreen) == Depth{true, {}, {0, 4 * kOneW, 0}});

    // No layer, and nothing in the scene, is farther apart than the eyes
    for (const float ipd : {0.045f, 0.063f, 0.070f, 0.085f}) {
        for (float width = 0.3f; width < 12.0f; width += 0.05f) {
            const Depth depth = MakeDepth(3, MaxShift(ipd, width));
            const float pixel = width / kWidth;
            REQUIRE(static_cast<float>(depth.layers[3]) * pixel <= ipd * 1.00001f);
            REQUIRE(VertexShift(depth.scene, 0xFFFFFF) <= depth.layers[3]);
        }
    }
    // Where levels 1 to 3 are no longer whole (63 mm): 5.37, 2.68 and 1.79 m
    CHECK(MakeDepth(1, MaxShift(0.063f, 5.37f)).layers == Shifts(1));
    CHECK(MakeDepth(1, MaxShift(0.063f, 5.39f)).layers != Shifts(1));
    CHECK(MakeDepth(2, MaxShift(0.063f, 2.68f)).layers == Shifts(2));
    CHECK(MakeDepth(2, MaxShift(0.063f, 2.70f)).layers != Shifts(2));
    CHECK(MakeDepth(3, MaxShift(0.063f, 1.79f)).layers == Shifts(3));
    CHECK(MakeDepth(3, MaxShift(0.063f, 1.80f)).layers != Shifts(3));
}

TEST_CASE("NDS Layer Depth: 3D vertices", "[frame_cores]") {
    constexpr SceneShift kDepth{8, 4 * kOneW, 6};
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
        constexpr SceneShift one{1, 4 * kOneW, 9};
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

// The whole core, on generated games

namespace {

/// A DS game that is a list of writes: some once, the others every frame in the vertical blank.
class WriteListGame {
public:
    void Write8(uint32_t address, uint32_t value) {
        Add(address | 0x80000000u, value, 0);
    }
    /// `step`: this much more every frame
    void Write16(uint32_t address, uint32_t value, uint32_t step = 0) {
        Add(address | 0x40000000u, value, step);
    }
    void Write32(uint32_t address, uint32_t value, uint32_t step = 0) {
        Add(address, value, step);
    }
    void WaitForLine(uint32_t line) {
        Add(1, line, 0);
    }
    void Noise(uint32_t address, uint32_t words, uint32_t mask = 0xFFFFFFFF) {
        for (uint32_t i = 0; i < words; ++i) {
            seed = seed * 1664525u + 1013904223u;
            Write32(address + i * 4, (seed ^ (seed >> 13)) & mask);
        }
    }
    /// What is written from here on is for every frame.
    void EveryFrame() {
        Add(0, 0, 0);
        WaitForLine(191);
        WaitForLine(192);
    }

    std::vector<uint8_t> Rom() const {
        constexpr std::array<uint32_t, 26> kCode{
            0xE3A04402, // mov r4, #0x02000000
            0xE2844B01, // add r4, r4, #0x400      the list
            0xE3A05000, // mov r5, #0              the frame
            0xE3A06000, // mov r6, #0
            // next:
            0xE8B40007, // ldmia r4!, {r0, r1, r2}
            0xE3500001, // cmp r0, #1
            0x3A00000D, // blo end
            0x0A000007, // beq wait
            0xE0211592, // mla r1, r2, r5, r1
            0xE1A03F20, // lsr r3, r0, #30
            0xE3C00103, // bic r0, r0, #0xC0000000
            0xE3530001, // cmp r3, #1
            0x35801000, // strlo r1, [r0]
            0x01C010B0, // strheq r1, [r0]
            0x85C01000, // strbhi r1, [r0]
            0xEAFFFFF3, // b next
            // wait:
            0xE3A03301, // mov r3, #0x04000000
            0xE1D330B6, // ldrh r3, [r3, #6]
            0xE1530001, // cmp r3, r1
            0x1AFFFFFB, // bne wait
            0xEAFFFFEE, // b next
            // end:
            0xE3560000, // cmp r6, #0
            0x01A06004, // moveq r6, r4
            0x11A04006, // movne r4, r6
            0x12855001, // addne r5, r5, #1
            0xEAFFFFE9, // b next
        };
        std::vector<uint32_t> words = list;
        words.insert(words.end(), {0, 0, 0});
        const auto arm9_size = static_cast<uint32_t>(0x400 + words.size() * 4);

        std::vector<uint8_t> rom(0x8000 + arm9_size);
        const auto put = [&rom](std::size_t at, const auto& values) {
            std::memcpy(&rom[at], values.data(), values.size() * sizeof(values[0]));
        };
        std::memcpy(&rom[0], "DEPTHTEST", 9);
        std::memcpy(&rom[0x0C], "HNDE01", 6);
        put(0x20, std::array<uint32_t, 4>{0x8000, 0x02000000, 0x02000000, arm9_size});
        put(0x30, std::array<uint32_t, 4>{0x4200, 0x02380000, 0x02380000, 0x100});
        put(0x80, std::array<uint32_t, 2>{static_cast<uint32_t>(rom.size()), 0x4000});
        put(0x4200, std::array<uint32_t, 1>{0xEAFFFFFE}); // ARM7: b .
        put(0x8000, kCode);
        put(0x8400, words);
        return rom;
    }

private:
    void Add(uint32_t address, uint32_t value, uint32_t step) {
        list.insert(list.end(), {address, value, step});
    }

    std::vector<uint32_t> list;
    uint32_t seed = 12345;
};

struct Scene {
    bool a_on_top = true;
    bool scene_3d = false;
    bool floor = true; ///< in the 3D
    bool capture_always = false;
    bool capture_once = false;
    /// Where the layers and the 3D are drawn, beside their place: the right eye that is expected
    LayerShifts moved{};
    int moved_3d = 0;
};

constexpr std::array<uint32_t, 4> kPriorities{2, 1, 3, 2}; // of BG0 to BG3

/// Text, rotated and bitmap backgrounds and sprites of every priority, windows, blending, mosaic;
/// they scroll, and in the middle of the picture they scroll differently.
void AddEngine(WriteListGame& game, const Scene& scene, bool b) {
    const uint32_t io = b ? 0x04001000 : 0x04000000;
    const uint32_t palette = b ? 0x05000400 : 0x05000000;
    const uint32_t bg = b ? 0x06200000 : 0x06000000;
    const uint32_t obj = b ? 0x06600000 : 0x06400000;
    const uint32_t oam = b ? 0x07000400 : 0x07000000;

    game.Noise(palette, 256, 0x7FFF7FFF);
    game.Noise(bg, 0x1000 / 4, 0x0F0F0F0F); // 4 bit tiles with holes; 8 bit ones too
    game.Noise(bg + 0x4000, 0x1400 / 4, 0x001F001F); // the maps
    game.Noise(bg + 0x10000, 0x4000 / 4);            // the bitmap
    game.Noise(obj, 0x1000 / 4);

    const std::array<uint32_t, 4> controls{0x0800, 0x0940, 0x2A00, 0x2480};
    for (uint32_t i = 0; i < 4; ++i) {
        game.Write16(io + 8 + i * 2, controls[i] | kPriorities[i]);
    }
    for (uint32_t i = 0; i < 128; ++i) {
        game.Write16(oam + i * 8, 0x0200);
    }
    // A window, half transparent, mosaic
    game.Write16(io + 0x40, 60 << 8 | 200);
    game.Write16(io + 0x44, 30 << 8 | 150);
    game.Write16(io + 0x48, 0x3F);
    game.Write16(io + 0x4A, 0x1B);
    game.Write16(io + 0x4C, 0x0012);
    game.Write16(io + 0x50, 0x3F42);
    game.Write16(io + 0x52, 0x0A06);
    const std::array<uint32_t, 2> turned{0xB5, 0x80};
    for (uint32_t i = 0; i < 2; ++i) {
        const uint32_t rot = io + 0x20 + i * 0x10;
        game.Write16(rot, turned[i]);
        game.Write16(rot + 2, 0x10000 - turned[i] / 2);
        game.Write16(rot + 4, turned[i] / 2);
        game.Write16(rot + 6, 0xC0);
    }
    const uint32_t bg0 = scene.scene_3d && !b ? 1u << 3 : 0;
    // Mode 4, the layers, sprites in their window and both windows
    game.Write32(io, 0x1BF14 | bg0);
}

void MoveEngine(WriteListGame& game, const Scene& scene, bool b) {
    const uint32_t io = b ? 0x04001000 : 0x04000000;
    const uint32_t oam = b ? 0x07000400 : 0x07000000;
    const auto shift = [&scene](uint32_t priority) {
        return static_cast<uint32_t>(scene.moved[priority]);
    };
    game.Write16(io + 0x10, 0u - (scene.scene_3d && !b ? 0 : shift(kPriorities[0])), b ? 1 : 0);
    game.Write16(io + 0x14, 0u - shift(kPriorities[1]), 2);
    for (uint32_t i = 0; i < 2; ++i) {
        const uint32_t s = shift(kPriorities[2 + i]);
        const uint32_t rot_a = i == 0 ? 0xB5 : 0x80;
        game.Write32(io + 0x28 + i * 0x10, 0x1234 - s * rot_a, 0x40);
        game.Write32(io + 0x2C + i * 0x10, 0x4321 - s * (rot_a / 2), 0x20);
    }
    for (uint32_t i = 0; i < 12; ++i) {
        const uint32_t priority = i & 3;
        const bool window = i == 11;
        const bool turned = i == 5 || i == 6;
        const uint32_t x = i * 23 + (window ? 0 : shift(priority));
        game.Write16(oam + i * 8, (i * 15) | (window ? 0x0800 : 0) | (turned ? 0x0300 : 0));
        game.Write16(oam + i * 8 + 2, 0x8000 | x, turned ? 0 : 1);
        game.Write16(oam + i * 8 + 4, (i * 4) | priority << 10 | (i & 15) << 12);
    }
    const std::array<uint32_t, 4> sprite_turn{0xE0, 0x40, 0xFFC0, 0xE0};
    for (uint32_t i = 0; i < 4; ++i) {
        game.Write16(oam + 6 + i * 8, sprite_turn[i]);
    }
}

constexpr uint32_t kFx = 0x1000;

void Vertex(WriteListGame& game, int x, int y, int z, uint32_t color) {
    game.Write32(0x04000480, color);
    game.Write32(0x0400048C, (static_cast<uint32_t>(x) & 0xFFFF) | static_cast<uint32_t>(y) << 16);
    game.Write32(0x0400048C, static_cast<uint32_t>(z) & 0xFFFF);
}

void Projection(WriteListGame& game, uint32_t xw, uint32_t zw, uint32_t w, uint32_t move_x,
                uint32_t step_x) {
    game.Write32(0x04000440, 0);
    const std::array<uint32_t, 16> matrix{kFx, 0, 0,  xw, 0,      kFx, 0,   0, //
                                          0,   0, 64, zw, move_x, 0,   0, w};
    for (uint32_t i = 0; i < 16; ++i) {
        game.Write32(0x04000458, matrix[i], i == 12 ? step_x : 0);
    }
    game.Write32(0x04000440, 1);
    game.Write32(0x04000454, 0);
}

void Quad(WriteListGame& game, uint32_t id, int left, int top, int right, int bottom, int z_top,
          int z_bottom, uint32_t alpha) {
    game.Write32(0x040004A4, 0x80C0 | alpha << 16 | id << 24);
    game.Write32(0x04000500, 1);
    Vertex(game, left, top, z_top, 0x001F);
    Vertex(game, left, bottom, z_bottom, 0x03E0);
    Vertex(game, right, bottom, z_bottom, 0x7C1F);
    Vertex(game, right, top, z_top, 0x7FFF);
    game.Write32(0x04000504, 0);
}

/// A floor (w 2 to 7), something at w 8 that moves and a half transparent one over it, and
/// something orthographic, with fog, edges and antialiasing.
void Add3D(WriteListGame& game, const Scene& scene) {
    constexpr int kOne = static_cast<int>(kFx);
    if (scene.floor) {
        Projection(game, 0, 0u - kFx, 0, 0, 0);
        Quad(game, 1, -4 * kOne, -kOne / 2, 4 * kOne, -kOne / 2, -7 * kOne, -2 * kOne, 31);
    }
    // A pixel is 1/128 at w 1, so 1/16 at w 8
    Projection(game, 0, 0, 8 * kFx, static_cast<uint32_t>(scene.moved_3d) * kFx / 16, 8);
    Quad(game, 2, -3 * kOne, 3 * kOne, kOne, -2 * kOne, 0, 0, 31);
    Quad(game, 3, -2 * kOne, 5 * kOne, 2 * kOne, 0, 1, 1, 16);
    Projection(game, 0, 0, kFx, 0, 0);
    Quad(game, 4, kOne / 4, kOne / 2, kOne * 3 / 4, 0, 0, 0, 31);
    game.Write32(0x04000540, 0);
}

std::vector<uint8_t> MakeRom(const Scene& scene) {
    WriteListGame game;
    game.Write16(0x04000304, scene.a_on_top ? 0x820F : 0x020F);
    game.Write8(0x04000240, 0x81); // A, B: engine A's backgrounds and sprites
    game.Write8(0x04000241, 0x82);
    game.Write8(0x04000242, 0x84); // C, D: engine B's
    game.Write8(0x04000243, 0x84);
    AddEngine(game, scene, false);
    AddEngine(game, scene, true);
    if (scene.scene_3d) {
        game.Write16(0x04000060, 0xB8);
        game.Write32(0x04000350, 0x3F000000);
        game.Write16(0x04000354, 0x7FFF);
        game.Write32(0x04000358, 0x00101234);
        game.Write16(0x0400035C, 0x4000);
        for (uint32_t i = 0; i < 8; ++i) {
            game.Write16(0x04000330 + i * 2, 0x0421 * (i + 3));
            game.Write32(0x04000360 + i * 4, 0x01010101 * (i * 3));
        }
        game.Write32(0x04000580, 0xBFFF0000);
    }
    if (scene.capture_once) {
        game.Write8(0x04000243, 0x80); // D: captures
        game.WaitForLine(200);
        game.Write32(0x04000064, 0x80330010);
    }
    game.EveryFrame();
    if (scene.capture_always) {
        game.Write8(0x04000243, 0x80);
        game.Write32(0x04000064, 0x80330010);
    }
    MoveEngine(game, scene, false);
    MoveEngine(game, scene, true);
    if (scene.scene_3d) {
        Add3D(game, scene);
    }
    // Halfway down, other scrolls
    game.WaitForLine(100);
    game.Write16(0x04000016, 7, 1);
    game.Write16(0x04001016, 11, 1);
    return game.Rom();
}

using Pixels = std::vector<uint32_t>;

Pixels Copy(const Vulkan::AltScreenImage& image) {
    return image.HasPicture() ? Pixels(image.pixels, image.pixels + image.width * image.height)
                              : Pixels{};
}

struct Game {
    fs::path dir;
    FrameCores::NdsCore core;
    FrameCores::FrameOutput out;

    Game(const Scene& scene, uint32_t level, bool threaded, bool jit = true) {
        static int count = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = fs::temp_directory_path() / ("azaharvr_nds_depth_" + std::to_string(stamp) + "_" +
                                           std::to_string(count++));
        fs::create_directories(dir);
        const fs::path path = dir / "depth.nds";
        const std::vector<uint8_t> rom = MakeRom(scene);
        std::ofstream(path, std::ios::binary)
            .write(reinterpret_cast<const char*>(rom.data()),
                   static_cast<std::streamsize>(rom.size()));
        FrameCores::CorePaths paths;
        paths.data_dir = dir.string();
        paths.game_dir = (dir / "game").string();
        paths.cheat_dir = (dir / "cheats").string();
        FrameCores::CoreBootOptions opts;
        opts.use_bios = false;
        opts.threaded_3d = threaded;
        opts.jit_enabled = jit;
        opts.layer_depth = level;
        REQUIRE(core.Load(path.string(), paths, opts));
    }
    ~Game() {
        core.Unload();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    void Run(int frames = 1) {
        for (int i = 0; i < frames; ++i) {
            core.SetInput({});
            core.RunFrame(out);
        }
    }
    bool HasRight() const {
        return out.primary_right.HasPicture() && out.primary_right.pixels != out.primary.pixels;
    }
    Pixels Left() const {
        return Copy(out.primary);
    }
    Pixels Right() const {
        return Copy(out.primary_right);
    }
    Pixels Bottom() const {
        return Copy(out.secondary);
    }
};

constexpr int kStart = 20; // frames until the pictures are set up

} // namespace

TEST_CASE("NDS Layer Depth off: no right picture", "[frame_cores]") {
    Game game({.scene_3d = true}, 0, true);
    for (int i = 0; i < 30; ++i) {
        game.Run();
        REQUIRE(game.out.primary.HasPicture());
        REQUIRE(!game.out.primary_right.HasPicture());
    }
}

TEST_CASE("NDS Layer Depth: with nothing moved both eyes see the same", "[frame_cores]") {
    const bool scene_3d = GENERATE(false, true);
    const bool a_on_top = GENERATE(true, false);
    const bool threaded = GENERATE(true, false);
    Game game({.a_on_top = a_on_top, .scene_3d = scene_3d}, 1, threaded);
    game.core.SetMaxLayerShift(Common::LayerDepth::kOnScreen);
    Pixels last;
    int changes = 0;
    int moved = 0;
    int with_3d = 0;
    for (int i = 0; i < 600; ++i) {
        INFO("frame " << i);
        game.Run();
        const Pixels left = game.Left();
        REQUIRE((left == game.Right()));
        if (i >= kStart) {
            REQUIRE(game.HasRight());
            moved += game.core.DepthLines().moved;
            with_3d += game.core.DepthLines().with_3d;
        }
        changes += left != last;
        last = left;
    }
    CHECK(changes > 500);
    CHECK(moved == (600 - kStart) * 192);
    CHECK(with_3d == (scene_3d && a_on_top ? (600 - kStart) * 192 : 0));
}

TEST_CASE("NDS Layer Depth: each layer moves by its priority", "[frame_cores]") {
    const uint32_t level = GENERATE(1u, 2u, 3u);
    const bool a_on_top = GENERATE(true, false);
    Game game({.a_on_top = a_on_top}, level, true);
    Game expected({.a_on_top = a_on_top, .moved = Shifts(level)}, 0, true);
    for (int i = 0; i < 120; ++i) {
        INFO("frame " << i);
        game.Run();
        expected.Run();
        if (i >= kStart) {
            REQUIRE((game.Right() == expected.Left()));
            REQUIRE((game.Right() != game.Left()));
        }
    }
}

TEST_CASE("NDS Layer Depth: 3D moves by its distance, what is orthographic stays",
          "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    const uint32_t level = GENERATE(1u, 2u, 3u);
    const int moved_3d = VertexShift(MakeDepth(level, Common::LayerDepth::kNoLimit).scene, 8 * kOneW);
    REQUIRE(moved_3d > 0);
    // Without the floor, whose vertices move differently
    Game game({.scene_3d = true, .floor = false}, level, threaded);
    Game expected({.scene_3d = true,
                   .floor = false,
                   .moved = Shifts(level),
                   .moved_3d = moved_3d},
                  0, threaded);
    for (int i = 0; i < 120; ++i) {
        INFO("frame " << i);
        game.Run();
        expected.Run();
        if (i >= kStart) {
            REQUIRE(game.core.DepthLines().with_3d == 192);
            REQUIRE((game.Right() == expected.Left()));
        }
    }
}

TEST_CASE("NDS Layer Depth leaves the game's pictures and sound alone", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    const bool scene_3d = GENERATE(false, true);
    const bool a_on_top = GENERATE(true, false);
    const Scene scene{.a_on_top = a_on_top, .scene_3d = scene_3d};
    Game with(scene, 3, threaded);
    Game without(scene, 0, threaded);
    for (int i = 0; i < 300; ++i) {
        INFO("frame " << i);
        with.Run();
        without.Run();
        REQUIRE((with.Left() == without.Left()));
        REQUIRE((with.Bottom() == without.Bottom()));
        REQUIRE((with.out.audio_pcm == without.out.audio_pcm));
        if (i >= kStart) {
            REQUIRE((with.Right() != with.Left()));
        }
    }
}

TEST_CASE("NDS Layer Depth: games that capture are flat", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    SECTION("every frame") {
        const Scene scene{.scene_3d = true, .capture_always = true};
        Game with(scene, 3, threaded);
        Game without(scene, 0, threaded);
        for (int i = 0; i < 200; ++i) {
            with.Run();
            without.Run();
            if (i >= kStart) {
                REQUIRE(!with.HasRight());
                REQUIRE(with.out.primary_right.HasPicture()); // the left one
            }
            REQUIRE((with.Left() == without.Left()));
            REQUIRE((with.Bottom() == without.Bottom()));
        }
    }
    SECTION("for a second after the last") {
        Game game({.scene_3d = true, .capture_once = true}, 3, threaded);
        game.Run(kStart);
        CHECK(!game.HasRight());
        game.Run(100);
        CHECK(game.HasRight());
        CHECK(game.core.DepthLines().with_3d == 192);
    }
}

TEST_CASE("NDS Layer Depth after a reset, a state and a change", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    Game game({.scene_3d = true}, 1, threaded);
    game.core.SetMaxLayerShift(Common::LayerDepth::kOnScreen);
    const auto same = [&game](int frames) {
        for (int i = 0; i < frames; ++i) {
            INFO("frame " << i);
            game.Run();
            REQUIRE((game.Right() == game.Left()));
        }
        REQUIRE(game.HasRight());
        REQUIRE(game.core.DepthLines().with_3d == 192);
    };
    same(40);
    std::vector<uint8_t> state;
    REQUIRE(game.core.SaveStateToMemory(state));
    same(20);
    game.core.Reset();
    same(40);
    for (int i = 0; i < 5; ++i) {
        REQUIRE(game.core.LoadStateFromMemory(state.data(), state.size()));
        same(5);
        REQUIRE(game.core.SaveStateToMemory(state));
        same(2);
    }

    // Off and on again, in the game
    game.core.SetLayerDepth(0);
    game.Run();
    CHECK(!game.out.primary_right.HasPicture());
    CHECK(game.out.layer_shift == 0.0f);
    game.Run(5);
    game.core.SetLayerDepth(1);
    game.Run(2);
    same(20);

    // Turned screens have none
    game.core.SetScreenRotation(90);
    game.Run();
    CHECK(!game.out.primary_right.HasPicture());
    game.core.SetScreenRotation(0);
    game.Run(2);
    same(20);

    game.core.Unload();
    game.Run();
    CHECK(!game.out.primary.HasPicture());
    CHECK(!game.out.primary_right.HasPicture());
}

TEST_CASE("NDS Layer Depth: a state load looks the same with it", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    const Scene scene{.scene_3d = true};
    Game with(scene, 3, threaded);
    Game without(scene, 0, threaded);
    with.Run(60);
    without.Run(60);
    std::vector<uint8_t> state_with;
    std::vector<uint8_t> state_without;
    REQUIRE(with.core.SaveStateToMemory(state_with));
    REQUIRE(without.core.SaveStateToMemory(state_without));
    CHECK(state_with.size() == state_without.size()); // the second picture is in none
    for (int round = 0; round < 3; ++round) {
        REQUIRE(with.core.LoadStateFromMemory(state_without.data(), state_without.size()));
        REQUIRE(without.core.LoadStateFromMemory(state_without.data(), state_without.size()));
        for (int i = 0; i < 30; ++i) {
            INFO("round " << round << " frame " << i);
            with.Run();
            without.Run();
            REQUIRE((with.Left() == without.Left()));
            REQUIRE((with.Bottom() == without.Bottom()));
        }
        CHECK(with.HasRight());
    }
}

TEST_CASE("NDS Layer Depth: turned on in the game", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    Game game({.scene_3d = true}, 0, threaded);
    Game other({.scene_3d = true}, 0, threaded);
    game.Run(40);
    other.Run(40);
    game.core.SetLayerDepth(2);
    for (int i = 0; i < 60; ++i) {
        INFO("frame " << i);
        game.Run();
        other.Run();
        if (i >= 2) { // the new renderer's first frames have no 3D or sprites of the one before
            REQUIRE((game.Left() == other.Left()));
            REQUIRE((game.Right() != game.Left()));
        }
    }
}
