// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <set>
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

constexpr LayerShifts Shifts(uint32_t level, bool inverted = false) {
    return MakeDepth(level, Common::LayerDepth::kNoLimit, inverted).layers;
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
    CHECK(Shifts(2, true) == LayerShifts{0, -2, -4, -6});
    CHECK(MakeDepth(0, kNoLimit, true) == Depth{}); // Invert Depth is nothing without
    for (uint32_t level = 1; level <= kMaxLevel; ++level) {
        const Depth depth = MakeDepth(level, kNoLimit);
        CHECK(depth.on);
        CHECK(depth.convergence == 0);
    }
    CHECK(MakeDepth(3, kNoLimit, false, 9 * kOneW).convergence == 9 * kOneW);

    // The screen is not known yet: drawn, with nothing moved
    CHECK(MakeDepth(3, kOnScreen) == Depth{true, {}, {}, 0});

    // No layer, and nothing in the scene, is farther apart than the eyes
    for (const float ipd : {0.045f, 0.063f, 0.070f, 0.085f}) {
        for (float width = 0.3f; width < 12.0f; width += 0.05f) {
            const Depth depth = MakeDepth(3, MaxShift(ipd, width));
            const float pixel = width / kWidth;
            REQUIRE(static_cast<float>(depth.layers[3]) * pixel <= ipd * 1.00001f);
            for (const int32_t far : {0, 5 * kOneW, 100 * kOneW}) {
                const SceneShift scene =
                    MakeScene({0, 0, depth.layers[3]}, SceneView{4 * kOneW, far});
                REQUIRE(VertexShift(scene, 0xFFFFFF) <= depth.layers[3]);
            }
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

namespace {

/// 8 px for what is far away, from 0, in a room of 6 px each way
constexpr SceneShift kEight{8 * 16, 4 * kOneW, -6, 6};

} // namespace

TEST_CASE("NDS Layer Depth: 3D vertices", "[frame_cores]") {
    constexpr int32_t kMaxW = 0xFFFFFF;

    SECTION("on the base at the convergence, behind it further away, before it nearer") {
        CHECK(VertexShift(kEight, 4 * kOneW) == 0);
        CHECK(VertexShift(kEight, 8 * kOneW) == 4);
        CHECK(VertexShift(kEight, 2 * kOneW + kOneW / 2) == -5); // 8 * (1 - 1.6) = -4.8
        CHECK(VertexShift({8 * 16, 4 * kOneW, -20, 20}, 2 * kOneW) == -8);
        CHECK(VertexShift({8 * 16, 4 * kOneW, 0, 9, 3}, 8 * kOneW) == 7);
        CHECK(VertexShift({8 * 16, 4 * kOneW, 0, 9, 3}, 4 * kOneW) == 3);
        CHECK(VertexShift({8, 4 * kOneW, -6, 6}, 0x7FFFFF) == 0); // half a pixel, not quite
    }

    SECTION("never out of the room") {
        CHECK(VertexShift(kEight, kMaxW) == 6);
        CHECK(VertexShift(kEight, 1) == -6);
        CHECK(VertexShift({8 * 16, 4 * kOneW, -8, 8}, kMaxW) == 8); // far away is the strength
        CHECK(VertexShift(kEight, 0) == 0);
        CHECK(VertexShift({8 * 16, 4 * kOneW, 2, 9, 3}, 1) == 2);
        CHECK(VertexShift({8 * 16, 4 * kOneW, 2, 9, 3}, kMaxW) == 9);
    }

    SECTION("strength 0 moves nothing") {
        for (const int32_t w : {kNearestW + 1, kOneW, 64 * kOneW, kMaxW}) {
            CHECK(VertexShift({0, 4 * kOneW, -6, 6}, w) == 0);
        }
    }

    SECTION("rounded to the nearest, the same on both sides") {
        constexpr SceneShift one{16, 4 * kOneW, -9, 9};
        CHECK(VertexShift(one, 8 * kOneW - 8) == 0); // just under a half
        CHECK(VertexShift(one, 8 * kOneW) == 1);     // a half
        CHECK(VertexShift(one, 2 * kOneW) == -1);
        CHECK(VertexShift(one, 3 * kOneW) == 0); // -1/3
    }

    SECTION("never less for a larger w") {
        int last = kEight.nearest;
        for (int32_t w = 1; w <= 1024 * kOneW; w += 37) {
            const int shift = VertexShift(kEight, w);
            if (shift < last) {
                FAIL("w " << w << ": " << shift << " after " << last);
            }
            last = shift;
        }
        CHECK(last == kEight.farthest);
    }

    SECTION("x stays in the rasteriser's range") {
        CHECK(ShiftVertexX(100, 6) == 106);
        CHECK(ShiftVertexX(100, -6) == 94);
        CHECK(ShiftVertexX(3, -6) == 0);    // below 0 it would write into the row above
        CHECK(ShiftVertexX(256, 6) == 262); // the rasteriser cuts at 256 by itself
        CHECK(ShiftVertexX(kMaxX, 6) == kMaxX);
        CHECK(ShiftVertexX(100, 0) == 100);
    }

    SECTION("vertices at one depth keep their order, also at the edges of the screen") {
        for (const int32_t w : {1, 3 * kOneW, 4 * kOneW, 600 * kOneW}) {
            const int shift = VertexShift(kEight, w);
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
        const int left = ShiftVertexX(100, VertexShift(kEight, kMaxW));
        const int right = ShiftVertexX(101, VertexShift(kEight, kOneW));
        CHECK(left == 106);
        CHECK(right == 95);
    }
}

TEST_CASE("NDS Layer Depth: 3D vertices in parts of a pixel", "[frame_cores]") {
    constexpr int32_t kMaxW = 0xFFFFFF;

    SECTION("the formula, finer") {
        CHECK(VertexShiftUnits(kEight, 4 * kOneW, 16) == 0);
        CHECK(VertexShiftUnits(kEight, 8 * kOneW, 16) == 64);              // 4 px
        CHECK(VertexShiftUnits(kEight, 2 * kOneW + kOneW / 2, 16) == -77); // -4.8 px
        CHECK(VertexShiftUnits(kEight, 5 * kOneW, 16) == 26);              // 1.6 px
        CHECK(VertexShiftUnits(kEight, kMaxW, 16) == 6 * 16);              // the room's end
        CHECK(VertexShiftUnits(kEight, 1, 16) == -6 * 16);
        CHECK(VertexShiftUnits({8 * 16, 4 * kOneW, 0, 9, 3}, 5 * kOneW, 16) == 3 * 16 + 26);
        CHECK(VertexShiftUnits({8 * 16, 4 * kOneW, -8, 8}, kMaxW, 16 * 8) == 8 * 16 * 8 - 1); // 8x
    }

    SECTION("never more than half a unit from the whole pixels' place, never less for a larger w") {
        for (const int units : {2, 16, 64, 128}) {
            int last = kEight.nearest * units;
            for (int32_t w = 1; w <= 1024 * kOneW; w += 37) {
                const int fine = VertexShiftUnits(kEight, w, units);
                const int whole = VertexShift(kEight, w) * units;
                if (fine < last || fine - whole > units / 2 || whole - fine > units / 2) {
                    FAIL("w " << w << " units " << units << ": " << fine << ", whole " << whole);
                }
                last = fine;
            }
        }
    }
}

TEST_CASE("NDS Layer Depth: which lines and frames have a right picture", "[frame_cores]") {
    constexpr uint32_t kLayers = 1u << 16;
    constexpr uint32_t kScene = (1u << 3) | (1u << 8);

    SECTION("backgrounds move by the priority in BGxCNT") {
        constexpr LayerShifts shifts{0, 3, 6, 9};
        CHECK(BackgroundShift(0x0000, shifts) == 0);
        CHECK(BackgroundShift(0xFFFD, shifts) == 3);
        CHECK(BackgroundShift(0x4082, shifts) == 6);
        CHECK(BackgroundShift(0x0003, shifts) == 9);
    }

    SECTION("the scene is in BG0 when both bits say so") {
        CHECK(ShowsScene(kScene));
        CHECK_FALSE(ShowsScene(1u << 3)); // BG0 off
        CHECK_FALSE(ShowsScene(1u << 8)); // BG0 is a 2D layer
        CHECK(LineShowsScene(1u << 3, true));
        CHECK_FALSE(LineShowsScene(kScene, false)); // not on yet for this line
        CHECK_FALSE(LineShowsScene(1u << 8, true));
    }

    SECTION("lines that are the left eye's") {
        CHECK_FALSE(IsFlatLine(kLayers, true, false));         // 2D only
        CHECK_FALSE(IsFlatLine(kLayers | kScene, true, true)); // the scene, and the right eye's
        CHECK(IsFlatLine(kLayers | kScene, true, false));      // the scene without it
        CHECK_FALSE(IsFlatLine(kLayers | kScene, false, false));
        CHECK(IsFlatLine(0, false, true));       // display off
        CHECK(IsFlatLine(2u << 16, true, true)); // VRAM display
        CHECK(IsFlatLine(3u << 16, true, true)); // memory display
    }

    SECTION("a game that says before line 215 which screen engine A is on next") {
        TopWatch watch;
        watch.BeginFrame(true);
        CHECK(watch.a_on_top);
        // Engine A is on another screen every frame; the game writes it in the vertical blank
        bool on_top = true;
        for (int frame = 0; frame < 20; ++frame) {
            const bool next = !on_top;
            const bool needed = watch.SceneNeeded(next);
            if (frame >= 2) {
                REQUIRE(needed == next); // drawn for the frames it is on top in, and only those
                REQUIRE(watch.NextOnTop() == next);
            }
            REQUIRE((needed || !next));
            watch.BeginFrame(next);
            on_top = next;
            REQUIRE(watch.a_on_top == on_top);
        }
        CHECK(watch.early);
        // A frame the game is late for: nothing written, the same screens again
        CHECK_FALSE(watch.SceneNeeded(false));
        watch.BeginFrame(false);
        CHECK_FALSE(watch.SceneNeeded(false));
    }

    SECTION("a game that says it after line 215") {
        TopWatch watch;
        watch.BeginFrame(true);
        bool on_top = true;
        for (int frame = 0; frame < 21; ++frame) {
            const bool needed = watch.SceneNeeded(on_top); // it has not written yet
            on_top = !on_top;
            if (frame >= 1) {
                REQUIRE(watch.NextOnTop() == on_top);
            }
            if (on_top) {
                REQUIRE(needed); // never a frame on top without its scene
            }
            watch.BeginFrame(on_top);
        }
        CHECK_FALSE(watch.early);
        // It stops changing with engine A below: the scene is drawn a few frames more, then not
        REQUIRE_FALSE(on_top);
        int drawn = 0;
        for (int frame = 0; frame < 30; ++frame) {
            drawn += watch.SceneNeeded(false);
            watch.BeginFrame(false);
        }
        CHECK(drawn == TopWatch::kRecent);
        CHECK_FALSE(watch.SceneNeeded(false));
    }

    SECTION("engine A stays where it is") {
        TopWatch top;
        TopWatch below;
        for (int frame = 0; frame < 10; ++frame) {
            top.BeginFrame(true);
            below.BeginFrame(false);
            if (frame >= 1) {
                REQUIRE(top.SceneNeeded(true));
                REQUIRE(top.NextOnTop());
            }
            REQUIRE_FALSE(below.SceneNeeded(false));
            REQUIRE_FALSE(below.NextOnTop());
        }
    }

    SECTION("only for the screens that have depth") {
        const Screens bottom{false, true};
        const Screens both{true, true};
        for (const bool on_top : {true, false}) {
            TopWatch watch;
            for (int frame = 0; frame < 10; ++frame) { // until it says nothing of a change
                watch.BeginFrame(on_top);
                static_cast<void>(watch.SceneNeeded(on_top, both));
            }
            CHECK(watch.SceneNeeded(on_top, both));
            CHECK(watch.SceneNeeded(on_top, bottom) == !on_top);
            CHECK(watch.SceneNeeded(on_top, {}) == on_top);
            CHECK_FALSE(watch.SceneNeeded(on_top, Screens{false, false}));
        }
    }

    SECTION("a captured picture is the picture it was captured from") {
        std::vector<uint32_t> drawn(256 * 192);
        for (std::size_t i = 0; i < drawn.size(); ++i) {
            const auto c6 = static_cast<uint32_t>((i * 7 + i / 256) & 63); // 6 bits a colour
            const uint32_t c8 = c6 << 2 | c6 >> 4;
            drawn[i] = 0xFF000000u | c8 << 16 | ((c8 + 40) & 0xFF) << 8 | (255 - c8);
        }
        std::vector<uint32_t> captured = drawn;
        for (uint32_t& pixel : captured) {
            pixel &= 0xFFF8F8F8u; // 5 bits a colour
        }
        CHECK(SamePicture(drawn.data(), captured.data(), drawn.size()));
        CHECK(SamePicture(drawn.data(), drawn.data(), drawn.size()));

        // Something drawn over an eighth of it, and over a little more
        std::vector<uint32_t> over = captured;
        for (std::size_t i = 0; i < over.size() / 8; ++i) {
            over[i] ^= 0x00808080u;
        }
        CHECK(SamePicture(drawn.data(), over.data(), drawn.size()));
        over[over.size() - 1] ^= 0x00000008u;
        CHECK_FALSE(SamePicture(drawn.data(), over.data(), drawn.size()));

        const std::vector<uint32_t> black(drawn.size(), 0xFF000000u);
        CHECK_FALSE(SamePicture(drawn.data(), black.data(), drawn.size()));
        CHECK(SamePicture(nullptr, nullptr, 0));
    }

    SECTION("the right picture stays for engine B's frame of that picture only") {
        CHECK(KeepsRightPicture(false, true, true));
        CHECK_FALSE(KeepsRightPicture(true, true, true));   // engine A's frames are drawn
        CHECK_FALSE(KeepsRightPicture(false, false, true)); // no picture of engine A's to keep
        CHECK_FALSE(KeepsRightPicture(false, true, false)); // another picture: engine B's layers
    }
}

TEST_CASE("NDS Layer Depth: the scene's room", "[frame_cores]") {
    constexpr LayerShifts layers{0, 3, 6, 9};
    constexpr uint32_t kAll = 0x0F00;

    SECTION("between the background in front and the one behind") {
        CHECK(MakeRoom(layers, kAll, {0x1F01, 0x0000, 0x0002, 0x0003}) == SceneRoom{0, 3, 6});
        CHECK(MakeRoom(layers, kAll, {0x0002, 0x0001, 0x0000, 0x0003}) == SceneRoom{3, 6, 9});
        // One of its own priority is behind it: the convergence moves a layer forward
        CHECK(MakeRoom(layers, kAll, {0x0002, 0x0000, 0x0002, 0x0003}) == SceneRoom{0, 3, 6});
        CHECK(MakeRoom(layers, kAll, {0x0002, 0x0001, 0x0002, 0x0003}) == SceneRoom{3, 3, 6});
        // Behind everything, its front taken
        CHECK(MakeRoom(layers, kAll, {0x0003, 0x0002, 0x0001, 0x0000}) == SceneRoom{6, 6, 9});
        // In front of everything, right before the next
        CHECK(MakeRoom(layers, kAll, {0x0000, 0x0001, 0x0002, 0x0003}) == SceneRoom{0, 0, 3});
        // No room at all
        CHECK(MakeRoom(layers, kAll, {0x0000, 0x0000, 0x0002, 0x0003}) == SceneRoom{0, 0, 0});
    }

    SECTION("backgrounds that are off take none") {
        CHECK(MakeRoom(layers, 0x0100, {0x0002, 0x0000, 0x0002, 0x0003}) == SceneRoom{0, 6, 9});
        CHECK(MakeRoom(layers, 0x0100, {0x0003, 0x0000, 0x0002, 0x0003}) == SceneRoom{0, 6, 9});
        CHECK(MakeRoom(layers, 0x0100, {0x0000, 0x0000, 0x0000, 0x0000}) == SceneRoom{0, 0, 9});
        CHECK(MakeRoom(layers, 0x0300, {0x0001, 0x0002, 0x0000, 0x0000}) == SceneRoom{0, 3, 6});
        CHECK(MakeRoom(layers, 0x0500, {0x0001, 0x0002, 0x0000, 0x0000}) == SceneRoom{0, 3, 9});
        CHECK(MakeRoom(layers, 0x0900, {0x0001, 0x0002, 0x0000, 0x0003}) == SceneRoom{0, 3, 9});
    }

    SECTION("whatever the registers say, the scene is in front of what it covers") {
        for (uint32_t bits = 0; bits < 256 * 8; ++bits) {
            const auto priority = [bits](int bg) {
                return static_cast<uint16_t>((bits >> (2 * bg)) & 3);
            };
            const uint32_t on = 0x0100 | (bits >> 8) << 9;
            const SceneRoom room =
                MakeRoom(layers, on, {priority(0), priority(1), priority(2), priority(3)});
            REQUIRE(room.nearest <= room.base);
            REQUIRE(room.base <= room.farthest);
            for (int bg = 1; bg < 4; ++bg) {
                if ((on & (0x100u << bg)) == 0) {
                    continue;
                }
                if (priority(bg) < priority(0)) {
                    REQUIRE(room.nearest >= layers[priority(bg)]);
                } else {
                    REQUIRE(room.farthest <= layers[priority(bg)]);
                }
            }
        }
    }

    SECTION("inverted: between the same layers, what is far still further right") {
        constexpr LayerShifts inverted{0, -3, -6, -9};
        for (uint32_t bits = 0; bits < 256 * 8; ++bits) {
            const auto priority = [bits](int bg) {
                return static_cast<uint16_t>((bits >> (2 * bg)) & 3);
            };
            const uint32_t on = 0x0100 | (bits >> 8) << 9;
            const std::array<uint16_t, 4> bgcnt{priority(0), priority(1), priority(2), priority(3)};
            const SceneRoom room = MakeRoom(layers, on, bgcnt);
            // The same, moved to the other side of the screen
            const int by = room.nearest + room.farthest;
            REQUIRE(MakeRoom(inverted, on, bgcnt) ==
                    SceneRoom{-room.farthest, room.base - by, -room.nearest});
        }
        const SceneRoom room = MakeRoom(layers, 0x0100, {0x0001, 0, 0, 0});
        const SceneRoom turned = MakeRoom(inverted, 0x0100, {0x0001, 0, 0, 0});
        constexpr SceneView view{4 * kOneW, 8 * kOneW};
        for (const int32_t w : {kNearestW, 2 * kOneW, 4 * kOneW, 6 * kOneW, 900 * kOneW}) {
            CHECK(VertexShift(MakeScene(turned, view), w) ==
                  VertexShift(MakeScene(room, view), w) - 9);
        }
    }

    SECTION("the view fills it, at most four times as deep") {
        constexpr SceneRoom room{0, 3, 9};
        CHECK(MakeScene(room, {4 * kOneW, 0}) == SceneShift{6 * 16, 4 * kOneW, 0, 9, 3});
        CHECK(MakeScene(room, {4 * kOneW, 8 * kOneW}) == SceneShift{12 * 16, 4 * kOneW, 0, 9, 3});
        CHECK(MakeScene(room, {4 * kOneW, 5 * kOneW}) == SceneShift{24 * 16, 4 * kOneW, 0, 9, 3});
        CHECK(MakeScene(room, {4 * kOneW, 4 * kOneW}) == SceneShift{24 * 16, 4 * kOneW, 0, 9, 3});
        // A town: the far end of its view is the far end of its room
        const SceneShift town = MakeScene(room, {480 * kOneW, 660 * kOneW});
        CHECK(VertexShift(town, 480 * kOneW) == 3);
        CHECK(VertexShift(town, 660 * kOneW) == 9);
        CHECK(VertexShift(town, 900 * kOneW) == 9);
        CHECK(VertexShift(town, 300 * kOneW) == 0);
    }
}

TEST_CASE("NDS Layer Depth: the scene's own view", "[frame_cores]") {
    SECTION("log w and back") {
        CHECK(Log2W(kOneW) == 12 * 256);
        CHECK(Log2W(8 * kOneW) == 15 * 256);
        CHECK(Log2W(3 * kOneW) == 13 * 256 + 128); // halfway between 2 and 4
        CHECK(Log2W(1) == 0);
        CHECK(Log2W(0) == 0);
        CHECK(Log2W(0xFFFFFF) == 23 * 256 + 255);
        for (const int32_t w : {1, 2, 3, 100, kOneW, 5 * kOneW, 130 * kOneW, 0xFFFFFF}) {
            const int32_t back = ExpW(Log2W(w));
            INFO("w " << w);
            CHECK(back <= w);
            CHECK(back >= w - w / 128); // 8 bits kept
        }
        int32_t last = 0;
        for (int32_t w = 1; w < 0xFFFFFF; w += w / 300 + 1) {
            REQUIRE(Log2W(w) >= last);
            last = Log2W(w);
        }
    }

    SECTION("one distance: that one") {
        SceneStats stats;
        CHECK(stats.View() == SceneView{});
        for (int i = 0; i < 40; ++i) {
            stats.Add(130 * kOneW);
        }
        stats.Add(0); // not a vertex in view
        stats.Add(-5);
        CHECK(stats.count == 40);
        CHECK(stats.View().convergence == ExpW(Log2W(130 * kOneW)));
        CHECK(stats.View().far == stats.View().convergence);
    }

    SECTION("a little nearer than the middle, and as much further") {
        // A side-scroller: planes at 130, 260 and 360
        SceneStats planes;
        for (int i = 0; i < 67; ++i) {
            planes.Add(130 * kOneW);
        }
        for (int i = 0; i < 28; ++i) {
            planes.Add(260 * kOneW);
        }
        for (int i = 0; i < 5; ++i) {
            planes.Add(360 * kOneW);
        }
        const SceneView side = planes.View();
        CHECK(side.convergence > 100 * kOneW);
        CHECK(side.convergence < 130 * kOneW); // the nearest plane just behind its base
        CHECK(side.far > 200 * kOneW);
        CHECK(side.far < 360 * kOneW);

        // A road from 2 to 180, evenly in log w: the nearest part comes forward
        SceneStats road;
        for (int32_t w = 2 * kOneW; w < 180 * kOneW; w += w / 16) {
            road.Add(w);
        }
        CHECK(road.View().convergence > 3 * kOneW);
        CHECK(road.View().convergence < 8 * kOneW);

        // Something small comes near: it moves smoothly, by its share
        SceneStats with_one = planes;
        with_one.Add(8 * kOneW);
        CHECK(with_one.View().convergence < side.convergence);
        CHECK(with_one.View().convergence > side.convergence * 3 / 4);
    }

    SECTION("held from frame to frame") {
        SlowW slow;
        CHECK(slow.Follow(0, false) == 0);
        CHECK(slow.Follow(100 * kOneW, false) == 100 * kOneW); // the first: at once
        // Within a half more or a third less it stays, so no edge moves; at a cut too
        for (const int w : {110, 149, 100, 67, 120, 0}) {
            CHECK(slow.Follow(w * kOneW, false) == 100 * kOneW);
            CHECK(slow.Follow(w * kOneW, true) == 100 * kOneW);
        }
    }

    SECTION("followed slowly, nearer and further") {
        for (const int32_t to : {3 * kOneW, 25 * kOneW, 400 * kOneW, 1600 * kOneW}) {
            SlowW slow;
            int32_t last = slow.Follow(100 * kOneW, false);
            int frames = 0;
            for (; last != to && frames < 2000; ++frames) {
                const int32_t now = slow.Follow(to, false);
                REQUIRE(std::abs(Log2W(now) - Log2W(last)) <= SlowW::kSlew);
                REQUIRE((to > last ? now > last && now <= to : now < last && now >= to));
                last = now;
            }
            CHECK(last == to);
            // A factor of two takes about two seconds
            CHECK(frames >= std::abs(Log2W(to) - Log2W(100 * kOneW)) / SlowW::kSlew);
            CHECK(frames <= std::abs(Log2W(to) - Log2W(100 * kOneW)) / SlowW::kSlew + 1);
        }
    }

    SECTION("no edge at or behind the convergence moves a pixel in a frame") {
        constexpr SceneRoom room{0, 0, 9};
        for (const int32_t from : {3 * kOneW, 480 * kOneW}) {
            SlowW convergence;
            SlowW far;
            SceneShift last =
                MakeScene(room, {convergence.Follow(from, false), far.Follow(from * 5 / 4, false)});
            for (int i = 0; i < 400; ++i) {
                const SceneShift now = MakeScene(
                    room, {convergence.Follow(from * 4, false), far.Follow(from * 5, false)});
                for (int32_t w = now.convergence; w < from * 16; w += from / 8) {
                    REQUIRE(std::abs(VertexShiftUnits(now, w, 16) -
                                     VertexShiftUnits(last, w, 16)) <= 8); // half a pixel
                }
                last = now;
            }
        }
    }

    SECTION("at a cut at once") {
        SlowW slow;
        slow.Follow(100 * kOneW, false);
        CHECK(slow.Follow(400 * kOneW, true) == 400 * kOneW);
        CHECK(slow.Follow(30 * kOneW, true) == 30 * kOneW);
    }

    SECTION("what a cut is") {
        constexpr SceneSetup setup = MakeSetup(0x00010308, 0x9006);
        constexpr SceneView near{4 * kOneW, 130 * kOneW}; // they overlap
        constexpr SceneView far{100 * kOneW, 900 * kOneW};
        ViewTrack track;
        CHECK(track.Follow({}, setup, false) == SceneView{});
        CHECK(track.Follow(near, setup, false) == near);
        const SceneView next = track.Follow(far, setup, false); // none: slowly
        CHECK(next.convergence > near.convergence);
        CHECK(next.convergence < near.convergence * 11 / 10);
        CHECK(next.far < near.far * 11 / 10);
        CHECK(track.Follow(far, setup, true) == far); // the picture is blank
        CHECK(track.Follow(near, MakeSetup(0x00010308, 0x9007), false) == near); // BG0's priority
        CHECK(track.Follow(far, MakeSetup(0x00010108, 0x9007), false) == far);   // a layer is off
        CHECK(track.Follow(near, MakeSetup(0x00010508, 0x9007), false).convergence ==
              near.convergence);
        track.Nothing(); // no 3D in a frame
        CHECK(track.Follow(far, MakeSetup(0x00010508, 0x9007), false) == far);
        // Nothing to take it from (a menu over an empty scene): held, and no cut is used up
        CHECK(track.Follow({}, MakeSetup(0x00010508, 0x9007), false) == far);
        CHECK(track.Follow({}, MakeSetup(0x00010708, 0x9007), false) == far);
        CHECK(track.Follow(near, MakeSetup(0x00010708, 0x9007), false) == near);

        // Nothing is where it was
        constexpr SceneView behind{1400 * kOneW, 1800 * kOneW};
        CHECK(track.Follow(behind, MakeSetup(0x00010708, 0x9007), false) == behind);
        CHECK(track.Follow(near, MakeSetup(0x00010708, 0x9007), false) == near);
        CHECK(IsElsewhere(near, {196 * kOneW, 300 * kOneW}));
        CHECK_FALSE(IsElsewhere(near, {195 * kOneW, 300 * kOneW}));
        CHECK(IsElsewhere(near, {kOneW, 2 * kOneW}));
        CHECK_FALSE(IsElsewhere(near, {kOneW, 3 * kOneW}));
        CHECK_FALSE(IsElsewhere({}, near));
        CHECK_FALSE(IsElsewhere(near, {}));

        CHECK(MakeSetup(0x40111F5D, 1) == MakeSetup(0x80001000u | 0x00010F0D, 1)); // the rest
        CHECK(IsBlank(0x8010));
        CHECK(IsBlank(0x401F));
        CHECK_FALSE(IsBlank(0x800F));
        CHECK_FALSE(IsBlank(0x0010));
        CHECK_FALSE(IsBlank(0xC010));
    }
}

TEST_CASE("NDS Layer Depth: polygons that face the camera", "[frame_cores]") {
    // depth = a + b / w, as a projection with its near plane at w 1 makes it
    constexpr auto depth = [](double w) {
        return static_cast<int32_t>(((1.0 - 2.0 / w) * 0x4000 + 0x3FFF) * 0x200);
    };
    DepthLine line;
    line.Add(8 * kOneW, depth(8), 9 * kOneW, depth(9));
    line.Add(2 * kOneW, depth(2), 14 * kOneW, depth(14)); // the largest span
    line.Add(8 * kOneW, 0, 10 * kOneW, 0xFFFFFF);         // not this one
    REQUIRE(line.known);
    const auto place = [&line](int32_t w, int32_t at, bool w_buffer = false) {
        const std::array<int32_t, 4> all{w, w, w, w};
        return PlacePolygon(all.data(), all.size(), at, w_buffer, line);
    };
    constexpr PolygonPlace kWorld{};

    SECTION("on the world's line: in the world") {
        for (const int w : {1, 2, 5, 8, 30, 600}) {
            INFO("w " << w);
            CHECK(place(w * kOneW, depth(w)) == kWorld);
        }
    }

    SECTION("drawn over the whole world: at the room's near end, whatever its w") {
        for (const int32_t w : {kOneW / 8, 16 * kOneW, 1023 * kOneW, 0xFFFFFF}) {
            CHECK(place(w, 0) == PolygonPlace{false, kNearestW, false});
            CHECK(place(w, depth(1.9)) == PolygonPlace{false, kNearestW, false});
        }
        // Where its depth has it, when something in the world is nearer than it
        const PolygonPlace between = place(1023 * kOneW, depth(4));
        CHECK_FALSE(between.world);
        CHECK(between.w > 4 * kOneW - 8);
        CHECK(between.w < 4 * kOneW + 8);
    }

    SECTION("a backdrop: as far as can be") {
        const PolygonPlace behind = place(2 * kOneW, 0xFFFFFF);
        CHECK_FALSE(behind.world);
        CHECK(behind.w > 4000 * kOneW);
    }

    SECTION("in a w-buffer the w is the depth") {
        CHECK(place(1023 * kOneW, 1023 * kOneW, true) == kWorld);
        CHECK(place(kOneW, kOneW, true) == PolygonPlace{false, kNearestW, false}); // orthographic
    }

    SECTION("each vertex by its own w otherwise") {
        constexpr std::array<int32_t, 3> slanted{4 * kOneW, 4 * kOneW, 5 * kOneW};
        CHECK(PlacePolygon(slanted.data(), 3, 0, false, line) == kWorld);
        CHECK_FALSE(PlacePolygon(slanted.data(), 2, 0, false, line) == kWorld); // the first two
    }

    SECTION("without a perspective in the scene") {
        constexpr std::array<int32_t, 4> ortho{kOneW, kOneW, kOneW, kOneW};
        constexpr std::array<int32_t, 3> facing{60 * kOneW, 60 * kOneW, 60 * kOneW};
        CHECK(PlacePolygon(ortho.data(), 4, 0, false, {}) == PolygonPlace{false, kNearestW, false});
        CHECK(PlacePolygon(facing.data(), 3, 0, false, {}) == kWorld);
        for (const int32_t base : {0, 3, 9}) { // the near end, whatever the view
            for (const int32_t convergence : {1, kOneW, 600 * kOneW}) {
                CHECK(VertexShift({(9 - base) * 16, convergence, 0, 9, base}, kNearestW) == 0);
            }
        }
        DepthLine none;
        none.Add(2 * kOneW, 0x3EFF00, 7 * kOneW, 0x3EFF00); // one depth for every w
        none.Add(0, 0, 7 * kOneW, 0xFFFFFF);
        none.Add(7 * kOneW, 0, 7 * kOneW, 0xFFFFFF);
        none.Add(700 * kOneW, 0, 701 * kOneW, 0xFFFFFF); // too little to tell from
        CHECK_FALSE(none.known);
    }
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
    /// Every other frame, starting with the first; in the others only its lowest byte is written.
    /// (The address of the write that follows gets the 8 bit mark in those frames.)
    void Write32Alternately(uint32_t address, uint32_t value) {
        const auto next = static_cast<uint32_t>(list.size() / 3 + 1);
        Write32(kListAddress + next * 12, address, 0x80000000u);
        Write32(address, value);
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
    static constexpr uint32_t kListAddress = 0x02000400;

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
    /// Into engine B's sprite memory, which it shows in the frames between
    bool capture_often = false;
    bool capture_once = false;
    /// 3D on both screens, as games do it: engine A on another screen every frame and captured
    /// while it is on top; engine B shows that picture on the top screen in the frame after
    bool both_screens = false;
    /// Where the layers and the 3D are drawn, beside their place: the right eye that is expected
    LayerShifts moved{};
    int moved_3d = 0;
    int moved_ortho = 0;
    /// Only the 3D: a floor in perspective (with near_step), and things drawn over it with w 1024
    bool hud = false;
    /// Something close too, with this added to its places (y in the upper half) every frame: in
    /// view every other frame (0x80000000) or one in four (0x40000000)
    uint32_t near_step = 0;
    /// The 3D's background every other frame only
    bool on_and_off = false;
    /// Engine A: this background (4: the sprites) and nothing else, without windows or mosaic
    int single = -1;
    /// With it, an empty sprite of another priority
    bool empty_sprite = false;
    /// The 3D is something at w 8 across the picture and past its edges
    bool wide = false;
    /// The 3D is the picture behind it, in banks C and D
    bool clear_image = false;
    uint32_t scroll_3d = 0; ///< BG0HOFS of the 3D
    bool blank = false;     ///< both engines' layers are off (white)
    /// Nothing behind engine A's layers, where its backdrop is and what is behind its 3D
    bool dark = false;
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
    game.Noise(bg, 0x1000 / 4, 0x0F0F0F0F);          // 4 bit tiles with holes; 8 bit ones too
    game.Noise(bg + 0x4000, 0x1400 / 4, 0x001F001F); // the maps
    game.Noise(bg + 0x10000, 0x4000 / 4);            // the bitmap
    game.Noise(obj, 0x1000 / 4);

    // BG1 and BG2 with mosaic: BG2's lines go on with an earlier line's reference point
    const std::array<uint32_t, 4> controls{0x0800, 0x0940, 0x2A40, 0x2480};
    for (uint32_t i = 0; i < 4; ++i) {
        game.Write16(io + 8 + i * 2, controls[i] | kPriorities[i]);
    }
    for (uint32_t i = 0; i < 128; ++i) {
        game.Write16(oam + i * 8, 0x0200);
    }
    if (scene.empty_sprite && !b) {
        const uint32_t own = scene.single == 4 ? 3 : kPriorities[scene.single];
        game.Write16(oam + 12 * 8, 100);
        game.Write16(oam + 12 * 8 + 2, 100);
        game.Write16(oam + 12 * 8 + 4, 1000 | (own ^ 1) << 10);
    }
    // A window, half transparent, mosaic
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
    if (scene.dark && !b) {
        game.Write16(palette, 0);
    }
    uint32_t control = scene.hud && !b ? 0x10108 : 0x1BF14 | bg0;
    if (scene.single >= 0 && !b) {
        control = 0x10014 | 0x100u << scene.single | bg0 | (scene.empty_sprite ? 0x1000 : 0);
        game.Write16(io + 0x4C, 0);
    }
    game.Write32(io, control | (scene.blank ? 0x80 : 0));
}

void MoveEngine(WriteListGame& game, const Scene& scene, bool b) {
    const uint32_t io = b ? 0x04001000 : 0x04000000;
    const uint32_t oam = b ? 0x07000400 : 0x07000000;
    // Across the screen's edge, so a line starts inside it; from the middle down not any more
    game.Write16(io + 0x40, 200 << 8 | 60);
    const auto shift = [&scene](uint32_t priority) {
        return static_cast<uint32_t>(scene.moved[priority]);
    };
    game.Write16(io + 0x10, scene.scene_3d && !b ? scene.scroll_3d : 0u - shift(kPriorities[0]),
                 b ? 1 : 0);
    game.Write16(io + 0x14, 0u - shift(kPriorities[1]), 2);
    for (uint32_t i = 0; i < 2; ++i) {
        const uint32_t s = shift(kPriorities[2 + i]);
        const uint32_t rot_a = i == 0 ? 0xB5 : 0x80;
        game.Write32(io + 0x28 + i * 0x10, 0x1234 - s * rot_a, 0x40);
        game.Write32(io + 0x2C + i * 0x10, 0x4321 - s * (rot_a / 2), 0x20);
    }
    for (uint32_t i = 0; i < 12; ++i) {
        const uint32_t priority = scene.single == 4 ? 3 : i & 3;
        const bool window = i == 11;
        const bool turned = i == 5 || i == 6;
        const uint32_t x = i * 23 + (window ? 0 : shift(priority));
        const bool off = scene.empty_sprite && !b && scene.single != 4;
        game.Write16(oam + i * 8,
                     off ? 0x0200 : (i * 15) | (window ? 0x0800 : 0) | (turned ? 0x0300 : 0));
        game.Write16(oam + i * 8 + 2, 0x8000 | ((x - (scene.single == 4 ? 50u : 0u)) & 0x1FF),
                     turned ? 0 : 1);
        game.Write16(oam + i * 8 + 4, (i * 4) | priority << 10 | (i & 15) << 12);
    }
    const std::array<uint32_t, 4> sprite_turn{0xE0, 0x40, 0xFFC0, 0xE0};
    for (uint32_t i = 0; i < 4; ++i) {
        game.Write16(oam + 6 + i * 8, sprite_turn[i]);
    }
}

constexpr uint32_t kFx = 0x1000;

void Vertex(WriteListGame& game, int x, int y, int z, uint32_t color, uint32_t step = 0) {
    game.Write32(0x04000480, color);
    game.Write32(0x0400048C, (static_cast<uint32_t>(x) & 0xFFFF) | static_cast<uint32_t>(y) << 16,
                 step);
    game.Write32(0x0400048C, static_cast<uint32_t>(z) & 0xFFFF);
}

void Projection(WriteListGame& game, uint32_t scale, uint32_t zw, uint32_t w, uint32_t move_x,
                uint32_t step_x, uint32_t z = 64, uint32_t move_z = 0) {
    game.Write32(0x04000440, 0);
    const std::array<uint32_t, 16> matrix{scale, 0, 0, 0,  0,      scale, 0,      0, //
                                          0,     0, z, zw, move_x, 0,     move_z, w};
    for (uint32_t i = 0; i < 16; ++i) {
        game.Write32(0x04000458, matrix[i], i == 12 ? step_x : 0);
    }
    game.Write32(0x04000440, 1);
    game.Write32(0x04000454, 0);
}

void Quad(WriteListGame& game, uint32_t id, int left, int top, int right, int bottom, int z_top,
          int z_bottom, uint32_t alpha, uint32_t step = 0) {
    game.Write32(0x040004A4, 0x80C0 | alpha << 16 | id << 24);
    game.Write32(0x04000500, 1);
    Vertex(game, left, top, z_top, 0x001F, step);
    Vertex(game, left, bottom, z_bottom, 0x03E0, step);
    Vertex(game, right, bottom, z_bottom, 0x7C1F, step);
    Vertex(game, right, top, z_top, 0x7FFF, step);
    game.Write32(0x04000504, 0);
}

/// A floor (w 2 to 7), something at w 8 that moves and a half transparent one over it, and
/// something orthographic, with fog, edges and antialiasing.
void Add3D(WriteListGame& game, const Scene& scene) {
    constexpr int kOne = static_cast<int>(kFx);
    if (scene.hud) {
        Projection(game, kFx, 0u - kFx, 0, 0, 0, 0u - kFx, 0u - 2 * kFx); // depth 1 - 2 / w
        Quad(game, 1, -4 * kOne, -kOne / 2, 4 * kOne, -kOne / 2, -7 * kOne, -2 * kOne, 31,
             scene.near_step);
        // In front of all of the floor; as depth - 0.5 it would be at w 1.33 in it
        Projection(game, 1024 * kFx, 0, 1024 * kFx, 0, 0, kFx, 0u - 512 * kFx);
        Quad(game, 5, -kOne / 2, -kOne / 5, kOne / 2, -kOne * 3 / 5, 0, 0, 31);
        // A strip of two above the floor: the left one's first depth is the same, the right one's
        // is 0.99, as w 200
        Projection(game, 1024 * kFx, 0, 1024 * kFx, 0, 0, 256 * kFx, 0u - 512 * kFx);
        game.Write32(0x040004A4, 0x00C0 | 31 << 16 | 6 << 24);
        game.Write32(0x04000500, 3);
        for (int i = 0; i < 3; ++i) {
            const int z = i == 0 ? 0 : kOne * 596 / 100;
            const int x = (i - 1) * kOne / 2;
            Vertex(game, x, kOne * 3 / 5, z, 0x001F << (5 * i));
            Vertex(game, x, kOne / 5, z, 0x7C1F >> (5 * i));
        }
        game.Write32(0x04000504, 0);
        game.Write32(0x04000540, 0);
        return;
    }
    if (scene.wide || scene.clear_image) {
        if (scene.wide) {
            Projection(game, 2 * kFx, 0, 8 * kFx, 0, 0);
            Quad(game, 2, -7 * kOne, 6 * kOne, 7 * kOne, -3 * kOne, 0, 0, 31);
            Quad(game, 3, -2 * kOne, 0, 2 * kOne, -6 * kOne, 1, 1, 16);
        }
        game.Write32(0x04000540, 0);
        return;
    }
    if (scene.floor) {
        Projection(game, kFx, 0u - kFx, 0, 0, 0);
        Quad(game, 1, -4 * kOne, -kOne / 2, 4 * kOne, -kOne / 2, -7 * kOne, -2 * kOne, 31);
    }
    // A pixel is 1/128 at w 1, so 1/16 at w 8
    Projection(game, kFx, 0, 8 * kFx, static_cast<uint32_t>(scene.moved_3d * kOne / 16), 64);
    Quad(game, 2, -3 * kOne, 3 * kOne, kOne, -2 * kOne, 0, 0, 31);
    Quad(game, 3, -2 * kOne, 5 * kOne, 2 * kOne, 0, 1, 1, 16);
    Projection(game, kFx, 0, kFx, static_cast<uint32_t>(scene.moved_ortho * kOne / 128), 0);
    Quad(game, 4, kOne / 4, kOne / 2, kOne * 3 / 4, 0, 0, 0, 31);
    if (scene.near_step != 0) {
        Projection(game, kFx / 4, 0, kFx / 4, 0, 0);
        const int y = scene.on_and_off ? -4 * kOne : 0; // in view in frames with the background
        Quad(game, 6, -kOne / 4, y + kOne / 2, kOne / 4, y + kOne / 4, 0, 0, 31, scene.near_step);
    }
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
    if (scene.both_screens) {
        // Engine B: nothing but a 256x256 bitmap of bank C's start, where the capture goes
        game.Write16(0x0400100C, 0x4084 | 1);
        game.Write16(0x04001020, 0x100);
        game.Write16(0x04001026, 0x100);
        game.Write32(0x04001000, 0x10405);
    } else {
        AddEngine(game, scene, true);
    }
    if (scene.scene_3d) {
        game.Write16(0x04000060, 0xB8);
        // Behind it all, half transparent and in the fog
        game.Write32(0x04000350, scene.dark ? 0x3F000000 : 0x3F089234);
        game.Write16(0x04000354, 0x7FFF);
        game.Write32(0x04000358, 0x00101234);
        game.Write16(0x0400035C, 0x3000); // before everything drawn (about 0x3FFF)
        for (uint32_t i = 0; i < 8; ++i) {
            game.Write16(0x04000330 + i * 2, 0x0421 * (i + 3));
            game.Write32(0x04000360 + i * 4, 0x01010101 * (20 + i * 12));
        }
        game.Write32(0x04000580, 0xBFFF0000);
    }
    if (scene.clear_image) {
        game.Write8(0x04000242, 0x80);
        game.Write8(0x04000243, 0x80);
        game.Noise(0x06840000, 0x20000 / 4, 0xFFFFFFFF);
        game.Write8(0x04000242, 0x93); // C: the picture, D: how far away it is
        game.Write8(0x04000243, 0x9B);
        game.Write16(0x04000060, 0x4000);
        game.Write16(0x04000356, 0x2010); // where it starts
    }
    if (scene.capture_once) {
        game.Write8(0x04000243, 0x80); // D: captures
        game.WaitForLine(200);
        game.Write32(0x04000064, 0x80330010);
    }
    game.EveryFrame();
    if (scene.capture_once || scene.capture_often) {
        game.Write8(0x04000243, 0x84); // D: engine B's sprites
    }
    if (scene.capture_often) {
        game.Write32Alternately(0x04000240, 0x80848281);
        game.Write32Alternately(0x04000064, 0x80330010);
    }
    if (scene.both_screens) {
        game.Write16(0x04000304, 0x820F, 0x8000);        // engine A: top, bottom, top
        game.Write8(0x04000242, 0x84);                   // C: engine B's background,
        game.Write32Alternately(0x04000240, 0x00808281); // or what is captured into
        game.Write32Alternately(0x04000064, 0x80320010);
    }
    if (scene.on_and_off) {
        game.Write32(0x04000000, 0x1BE1C);
        game.Write32Alternately(0x04000000, 0x1BF1C);
    }
    MoveEngine(game, scene, false);
    if (!scene.both_screens) {
        MoveEngine(game, scene, true);
    }
    if (scene.scene_3d) {
        Add3D(game, scene);
    }
    // Halfway down, other scrolls
    game.WaitForLine(100);
    game.Write16(0x04000016, 7, 1);
    game.Write16(0x04000040, 60 << 8 | 200);
    if (!scene.both_screens) {
        game.Write16(0x04001016, 11, 1);
        game.Write16(0x04001040, 60 << 8 | 200);
    }
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

    /// `convergence`: 4 as the tests' scenes are made for; 0: the scene's own view
    Game(const Scene& scene, uint32_t level, bool threaded, bool jit = true,
         float convergence = 4.0f) {
        static int count = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        dir = fs::temp_directory_path() /
              ("azaharvr_nds_depth_" + std::to_string(stamp) + "_" + std::to_string(count++));
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
        core.SetDepthConvergence(convergence);
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
    /// The game's own pictures
    Pixels Own() const {
        return Copy(out.primary);
    }
    Pixels OwnBottom() const {
        return Copy(out.secondary);
    }
    /// The top screen has a picture for each eye
    bool HasEyes() const {
        return Differ(out.primary_left, out.primary_right, out.primary);
    }
    bool HasEyesBottom() const {
        return Differ(out.secondary_left, out.secondary_right, out.secondary);
    }
    Pixels Seen(Eye eye) const {
        return Copy(eye == Eye::Right ? out.primary_right : out.primary_left);
    }
    Pixels SeenBottom(Eye eye) const {
        return Copy(eye == Eye::Right ? out.secondary_right : out.secondary_left);
    }
    static bool Differ(const Vulkan::AltScreenImage& left, const Vulkan::AltScreenImage& right,
                       const Vulkan::AltScreenImage& own) {
        return left.HasPicture() && right.HasPicture() && left.pixels != right.pixels &&
               left.pixels != own.pixels && right.pixels != own.pixels;
    }
};

/// The setting, for a test.
struct InvertDepth {
    explicit InvertDepth(bool on) {
        Common::LayerDepth::Get().inverted = on;
    }
    ~InvertDepth() {
        Common::LayerDepth::Get().inverted = false;
    }
};

constexpr uint32_t kBlack = 0xFF000000;
constexpr int kStart = 20; // frames until the pictures are set up

constexpr int kNone = -1;

constexpr std::array kEyes{Eye::Left, Eye::Right};

/// The columns of an eye's picture that the headset shows, with the farthest layer `farthest` to
/// the right between the two.
bool IsShown(int x, int farthest, Eye eye) {
    if (farthest < 0) {
        return eye == Eye::Right ? x < kWidth + farthest : x >= -farthest;
    }
    return !kCutsEdge || (eye == Eye::Right ? x >= farthest : x < kWidth - farthest);
}

/// The columns that only a shift of an eye has: at the left edge or the right one.
constexpr bool IsPast(int x, int shift) {
    return shift >= 0 ? x < shift : x >= kWidth + shift;
}

/// The first pixel of an eye's picture, where it is shown, that is not the game's own moved by the
/// eye's part of `shift`. At the edge the layers end, over nothing: `dark` is the game's own with
/// nothing behind its layers. `smooth`: but for this column, an edge that has some of what is
/// behind it. From `begin` to before `end` of the columns there are.
int FirstMoved(const Pixels& own, const Pixels& dark, const Pixels& seen, Eye eye, int shift,
               int farthest, int smooth = -1, int begin = 0, int end = kWidth) {
    if (own.size() != seen.size() || own.size() != dark.size() ||
        std::abs(shift) > std::abs(farthest)) {
        return 0;
    }
    const int by = EyeShift(shift, eye);
    for (int at = 0; at < static_cast<int>(own.size()); ++at) {
        const int x = at % kWidth;
        if (!IsShown(x, farthest, eye) || x == smooth || x < begin || x >= end) {
            continue;
        }
        const uint32_t expected = IsPast(x, by) ? kBlack
                                                : (IsPast(x, EyeShift(farthest, eye))
                                                       ? dark
                                                       : own)[static_cast<std::size_t>(at - by)];
        if (seen[at] != expected) {
            return at;
        }
    }
    return kNone;
}

/// Both eyes' of a screen.
bool IsMoved(const Game& game, const Pixels& own, const Pixels& dark, int shift, int farthest,
             bool bottom = false) {
    return std::ranges::all_of(kEyes, [&](Eye eye) {
        return FirstMoved(own, dark, bottom ? game.SeenBottom(eye) : game.Seen(eye), eye, shift,
                          farthest) == kNone;
    });
}

/// The same but for the columns at the edge that only what is moved (an eye's part) has.
bool SameInside(const Pixels& a, const Pixels& b, int moved) {
    for (int at = 0; at < static_cast<int>(a.size()); ++at) {
        if (!IsPast(at % kWidth, moved) && a[at] != b[at]) {
            return false;
        }
    }
    return a.size() == b.size();
}

/// What each eye is to see: the game drawn where the eye's layers are.
struct Expected {
    std::vector<std::unique_ptr<Game>> games;
    int farthest;

    /// `scene`: with the shifts between the eyes.
    Expected(const Scene& scene, bool threaded) : farthest{scene.moved[3]} {
        for (const Eye eye : kEyes) {
            Scene of = scene;
            of.moved = EyeLayers(scene.moved, eye);
            of.moved_3d = EyeShift(scene.moved_3d, eye);
            of.moved_ortho = EyeShift(scene.moved_ortho, eye);
            games.push_back(std::make_unique<Game>(of, 0, threaded));
        }
    }
    void Run(int frames = 1) {
        for (const auto& game : games) {
            game->Run(frames);
        }
    }
    bool Fits(const Game& seen) const {
        return std::ranges::all_of(kEyes, [&](Eye eye) {
            return SameInside(seen.Seen(eye), games[static_cast<std::size_t>(eye)]->Own(),
                              EyeShift(farthest, eye));
        });
    }
    bool FitsBottom(const Game& seen) const {
        return std::ranges::all_of(kEyes, [&](Eye eye) {
            return SameInside(seen.SeenBottom(eye),
                              games[static_cast<std::size_t>(eye)]->OwnBottom(),
                              EyeShift(farthest, eye));
        });
    }
};

/// Neither eye sees the game's own picture.
bool BothMoved(const Game& game) {
    return game.Seen(Eye::Left) != game.Own() && game.Seen(Eye::Right) != game.Own() &&
           game.Seen(Eye::Left) != game.Seen(Eye::Right);
}

/// Both see the game's own.
bool BothOwn(const Game& game) {
    return game.Seen(Eye::Left) == game.Own() && game.Seen(Eye::Right) == game.Own();
}

/// The 3D of the tests' scenes: all the backgrounds are on
constexpr SceneRoom RoomAt(uint32_t level, bool inverted = false) {
    return MakeRoom(Shifts(level, inverted), 0x0F00,
                    {static_cast<uint16_t>(kPriorities[0]), static_cast<uint16_t>(kPriorities[1]),
                     static_cast<uint16_t>(kPriorities[2]), static_cast<uint16_t>(kPriorities[3])});
}

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
    const InvertDepth invert{threaded};
    Game game({.a_on_top = a_on_top, .scene_3d = scene_3d}, 1, threaded);
    game.core.SetMaxLayerShift(Common::LayerDepth::kOnScreen);
    Pixels last;
    int changes = 0;
    int moved = 0;
    int with_3d = 0;
    for (int i = 0; i < 600; ++i) {
        INFO("frame " << i);
        game.Run();
        const Pixels own = game.Own();
        REQUIRE(BothOwn(game));
        if (i >= kStart) {
            REQUIRE(game.HasEyes());
            moved += game.core.DepthLines().moved;
            with_3d += game.core.DepthLines().with_3d;
        }
        changes += own != last;
        last = own;
    }
    CHECK(changes > 500);
    CHECK(moved == (600 - kStart) * 192);
    CHECK(with_3d == (scene_3d && a_on_top ? (600 - kStart) * 192 : 0));
}

TEST_CASE("NDS Layer Depth: each layer moves by its priority", "[frame_cores]") {
    const uint32_t level = GENERATE(1u, 2u, 3u);
    const bool a_on_top = GENERATE(true, false);
    const bool inverted = GENERATE(false, true);
    const InvertDepth invert{inverted};
    Game game({.a_on_top = a_on_top}, level, true);
    Expected expected({.a_on_top = a_on_top, .moved = Shifts(level, inverted)}, true);
    for (int i = 0; i < 120; ++i) {
        INFO("frame " << i);
        game.Run();
        expected.Run();
        if (i >= kStart) {
            REQUIRE(expected.Fits(game));
            REQUIRE(BothMoved(game));
            REQUIRE(game.out.layer_shift == EdgeShift(Shifts(level, inverted)[3]));
        }
    }
}

TEST_CASE("NDS Layer Depth: 3D moves by its distance, what is orthographic is in front",
          "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    const uint32_t level = GENERATE(1u, 2u, 3u);
    const bool inverted = GENERATE(false, true);
    const InvertDepth invert{inverted};
    const SceneRoom room = RoomAt(level, inverted);
    const int moved_3d = VertexShift(MakeScene(room, {4 * kOneW, 0}), 8 * kOneW);
    REQUIRE(moved_3d > room.base);
    // Without the floor, whose vertices move differently
    Game game({.scene_3d = true, .floor = false}, level, threaded);
    Expected expected({.scene_3d = true,
                       .floor = false,
                       .moved = Shifts(level, inverted),
                       .moved_3d = moved_3d,
                       .moved_ortho = room.nearest},
                      threaded);
    for (int i = 0; i < 120; ++i) {
        INFO("frame " << i);
        game.Run();
        expected.Run();
        if (i >= kStart) {
            REQUIRE(game.core.DepthLines().with_3d == 192);
            REQUIRE(expected.Fits(game));
        }
    }
}

TEST_CASE("NDS Layer Depth: the convergence is the scene's own", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    const uint32_t level = 3;
    // Everything in the scene is at w 8: that is its convergence, and it stands at the base
    Game game({.scene_3d = true, .floor = false}, level, threaded, true, 0.0f);
    Expected expected({.scene_3d = true,
                       .floor = false,
                       .moved = Shifts(level),
                       .moved_3d = RoomAt(level).base,
                       .moved_ortho = RoomAt(level).nearest},
                      threaded);
    for (int i = 0; i < 120; ++i) {
        INFO("frame " << i);
        game.Run();
        expected.Run();
        if (i >= kStart) {
            REQUIRE(game.core.DepthLines().with_3d == 192);
            REQUIRE(game.core.DepthLines().convergence == 8 * kOneW);
            REQUIRE(expected.Fits(game));
        }
    }
    // With the floor (w 2 to 7) it is nearer, and the same every frame
    Game floor({.scene_3d = true}, level, threaded, true, 0.0f);
    floor.Run(kStart);
    const int32_t convergence = floor.core.DepthLines().convergence;
    CHECK(convergence > 2 * kOneW);
    CHECK(convergence < 8 * kOneW);
    for (int i = 0; i < 60; ++i) {
        floor.Run();
        REQUIRE(floor.core.DepthLines().convergence == convergence);
        REQUIRE(floor.HasEyes());
    }
}

TEST_CASE("NDS Layer Depth: each screen's 3D has its own view", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    Game game({.scene_3d = true, .floor = false, .both_screens = true, .near_step = 0x80000000}, 3,
              threaded, true, 0.0f);
    game.core.SetDepthOnSecondary(true);
    game.Run(kStart);
    std::array<std::set<int32_t>, 2> views;
    for (int i = 0; i < 40; ++i) {
        game.Run();
        views[i % 2].insert(game.core.DepthLines().convergence);
    }
    CHECK(views[0].size() == 1);
    CHECK(views[1].size() == 1);
    CHECK(views[0] != views[1]);
    CHECK((views[0].contains(8 * kOneW) || views[1].contains(8 * kOneW)));
}

TEST_CASE("NDS Layer Depth: 3D after a frame without has its view at once", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    // Some at w 8, some with something at w 1/4 too; with the 3D there all the time the view
    // follows slowly
    const bool on_and_off = GENERATE(true, false);
    Game game({.scene_3d = true, .floor = false, .near_step = 0x40000000, .on_and_off = on_and_off},
              3, threaded, true, 0.0f);
    game.Run(kStart);
    std::set<int32_t> views;
    int32_t last = 0;
    int32_t most = 0;
    for (int i = 0; i < 40; ++i) {
        game.Run();
        const int32_t view = game.core.DepthLines().convergence;
        if (view > 0) {
            views.insert(view);
            most = last > 0 ? std::max(most, std::abs(view - last)) : most;
            last = view;
        }
    }
    CHECK(views.contains(8 * kOneW) == on_and_off);
    CHECK(views.size() >= 2);
    CHECK((most > kOneW) == on_and_off);
    CHECK((most < kOneW / 8) == !on_and_off);
}

TEST_CASE("NDS Layer Depth: what is drawn over the 3D is never behind it", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    const bool inverted = GENERATE(false, true);
    const InvertDepth invert{inverted};
    const int over = inverted ? -9 : 0; // the near end of the room
    Game game({.scene_3d = true, .hud = true}, 3, threaded, true, 0.0f);
    game.Run(kStart);
    for (int i = 0; i < 10; ++i) {
        game.Run();
        REQUIRE(game.core.DepthLines().with_3d == 192);
        // The floor's, 2 to 7: what is drawn over it, at w 1024, is not in it
        REQUIRE(game.core.DepthLines().convergence > kOneW);
        REQUIRE(game.core.DepthLines().convergence < 7 * kOneW);
        const Pixels own = game.Own();
        for (const Eye eye : kEyes) {
            const Pixels seen = game.Seen(eye);
            const auto by = [eye](int shift) { return EyeShift(shift, eye); };
            int floor_moved = 0;
            for (int y = 118; y < 150; ++y) {
                for (int x = 0; x < 256; ++x) {
                    const int at = y * 256 + x;
                    if (x >= 66 && x < 190) { // not its antialiased edges
                        REQUIRE(own[at] == seen[at + by(over)]);
                    } else if (x < 50 || x >= 200) {
                        floor_moved += own[at] != seen[at];
                    }
                }
            }
            REQUIRE(floor_moved > 0);
            // The strip's halves are at either end of the room; they share their middle's vertices
            const int behind = inverted ? 0 : 9;
            for (int y = 42; y < 72; ++y) {
                for (int x = 66; x < 190; ++x) {
                    const int at = y * 256 + x;
                    if (x < 124 || x >= 132) {
                        REQUIRE(own[at] == seen[at + by(x < 128 ? over : behind)]);
                    }
                }
            }
        }
    }
}

TEST_CASE("NDS Layer Depth: what is drawn over the 3D is by the frame's own 3D", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    // The floor every other frame: without it there is nothing to be drawn over
    Game game({.scene_3d = true, .hud = true, .near_step = 0x80000000}, 3, threaded, true, 0.0f);
    game.Run(kStart);
    std::set<int32_t> views;
    for (int i = 0; i < 10; ++i) {
        game.Run();
        views.insert(game.core.DepthLines().convergence);
    }
    CHECK(views.size() == 2);
    CHECK(*views.begin() < 7 * kOneW);
    CHECK(*views.rbegin() == 1024 * kOneW);
}

TEST_CASE("NDS Layer Depth: a loaded state has its view at once", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    Game game({.scene_3d = true, .floor = false, .near_step = 0x40000000}, 3, threaded, true, 0.0f);
    game.Run(kStart);
    std::vector<uint8_t> state;
    REQUIRE(game.core.SaveStateToMemory(state));
    std::set<int32_t> views;
    for (int i = 0; i < 100; ++i) {
        game.Run();
        views.insert(game.core.DepthLines().convergence);
    }
    CHECK(*views.rbegin() > *views.begin() * 9 / 8); // followed slowly
    const int32_t first = *views.begin();
    for (int i = 0; i < 4; ++i) {
        REQUIRE(game.core.LoadStateFromMemory(state.data(), state.size()));
        game.Run(4);
        const int32_t view = game.core.DepthLines().convergence;
        INFO("view " << view << ", at first " << first);
        // As measured, or a few frames from it
        CHECK((view > 7 * kOneW || view < first * 33 / 32));
        game.Run(100);
    }
}

TEST_CASE("NDS Layer Depth: a right picture for the bottom screen when asked", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    const bool a_on_top = GENERATE(true, false);
    const uint32_t level = 2;
    const int moved_3d = VertexShift(MakeScene(RoomAt(level), {4 * kOneW, 0}), 8 * kOneW);
    const Scene scene{.a_on_top = a_on_top, .scene_3d = true, .floor = false};
    Game game(scene, level, threaded);
    Game without(scene, 0, threaded);
    Expected expected({.a_on_top = a_on_top,
                       .scene_3d = true,
                       .floor = false,
                       .moved = Shifts(level),
                       .moved_3d = moved_3d,
                       .moved_ortho = RoomAt(level).nearest},
                      threaded);
    game.Run(kStart);
    without.Run(kStart);
    expected.Run(kStart);
    CHECK(!game.out.secondary_right.HasPicture()); // not asked for yet
    game.core.SetDepthOnSecondary(true);
    for (int i = 0; i < 100; ++i) {
        INFO("frame " << i);
        game.Run();
        without.Run();
        expected.Run();
        if (i >= 3) {
            REQUIRE(game.HasEyes());
            REQUIRE(game.HasEyesBottom());
            REQUIRE(expected.Fits(game));
            REQUIRE(expected.FitsBottom(game));
            REQUIRE((game.SeenBottom(Eye::Left) != game.OwnBottom()));
            REQUIRE((game.SeenBottom(Eye::Right) != game.OwnBottom()));
        }
        REQUIRE((game.Own() == without.Own()));
        REQUIRE((game.OwnBottom() == without.OwnBottom()));
    }
    game.core.SetDepthOnSecondary(false);
    game.Run(3);
    CHECK(!game.out.secondary_right.HasPicture());
    CHECK(game.HasEyes());
}

TEST_CASE("NDS Layer Depth leaves the game's pictures and sound alone", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    const bool scene_3d = GENERATE(false, true);
    const bool a_on_top = GENERATE(true, false);
    const InvertDepth invert{a_on_top != threaded};
    const Scene scene{.a_on_top = a_on_top, .scene_3d = scene_3d};
    Game with(scene, 3, threaded);
    Game without(scene, 0, threaded);
    for (int i = 0; i < 300; ++i) {
        INFO("frame " << i);
        with.Run();
        without.Run();
        REQUIRE((with.Own() == without.Own()));
        REQUIRE((with.OwnBottom() == without.OwnBottom()));
        REQUIRE((with.out.audio_pcm == without.out.audio_pcm));
        if (i >= kStart) {
            REQUIRE(BothMoved(with));
        }
    }
}

TEST_CASE("NDS Layer Depth: what is captured is the game's own", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    const bool once = GENERATE(false, true);
    const Scene scene{.scene_3d = true, .capture_often = !once, .capture_once = once};
    Game with(scene, 3, threaded);
    Game without(scene, 0, threaded);
    for (int i = 0; i < 200; ++i) {
        INFO("frame " << i);
        with.Run();
        without.Run();
        if (i >= kStart) {
            REQUIRE(with.HasEyes());
            REQUIRE(with.core.DepthLines().with_3d == 192);
            REQUIRE(BothMoved(with));
        }
        // The bottom screen shows the memory captured into
        REQUIRE((with.Own() == without.Own()));
        REQUIRE((with.OwnBottom() == without.OwnBottom()));
    }
    Game never({.scene_3d = true}, 0, threaded);
    never.Run(200);
    CHECK((never.OwnBottom() != without.OwnBottom()));
}

TEST_CASE("NDS Layer Depth: 3D on both screens", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    const InvertDepth invert{GENERATE(false, true)};
    const Scene scene{.scene_3d = true, .both_screens = true};
    Game with(scene, 3, threaded);
    Game without(scene, 0, threaded);
    Pixels last_own;
    std::array<Pixels, 2> last_seen;
    bool last_kept = false;
    int kept_frames = 0;
    for (int i = 0; i < 160; ++i) {
        INFO("frame " << i);
        with.Run();
        without.Run();
        const Pixels own = with.Own();
        const std::array<Pixels, 2> seen{with.Seen(Eye::Left), with.Seen(Eye::Right)};
        REQUIRE((own == without.Own()));
        REQUIRE((with.OwnBottom() == without.OwnBottom()));
        if (i >= kStart) {
            REQUIRE(with.HasEyes());
            REQUIRE(BothMoved(with));
            // Engine A's frame has new pictures; engine B's, of that frame as it was captured, the
            // same ones again
            const bool kept = seen == last_seen;
            REQUIRE(((seen[0] == last_seen[0]) == kept));
            if (i > kStart) {
                REQUIRE(kept != last_kept);
            }
            if (kept) {
                REQUIRE((own != last_own)); // 5 bits a colour, not 6
                REQUIRE(SamePicture(own.data(), last_own.data(), own.size()));
                ++kept_frames;
            }
            REQUIRE(with.core.DepthLines().with_3d == 192); // engine B's are not drawn again
            last_kept = kept;
        }
        last_own = own;
        last_seen = seen;
    }
    CHECK(kept_frames == (160 - kStart) / 2);
}

TEST_CASE("NDS Layer Depth after a reset, a state and a change", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    Game game({.scene_3d = true}, 1, threaded);
    game.core.SetMaxLayerShift(Common::LayerDepth::kOnScreen);
    const auto same = [&game](int frames) {
        for (int i = 0; i < frames; ++i) {
            INFO("frame " << i);
            game.Run();
            REQUIRE(BothOwn(game));
        }
        REQUIRE(game.HasEyes());
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
            REQUIRE((with.Own() == without.Own()));
            REQUIRE((with.OwnBottom() == without.OwnBottom()));
        }
        CHECK(with.HasEyes());
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
            REQUIRE((game.Own() == other.Own()));
            REQUIRE(BothMoved(game));
        }
    }
}

TEST_CASE("NDS Layer Depth: a picture is cut or its layers end, never both", "[frame_cores]") {
    const LayerShifts layers{0, 3, 6, 9};
    const LayerShifts inverted{0, -3, -6, -9};
    CHECK(EdgeShift(-9) == -9.0f / kWidth);
    CHECK(EdgeLayers(inverted, Eye::Left) == LayerShifts{});
    CHECK(EdgeLayers(inverted, Eye::Right) == LayerShifts{});
    CHECK(EdgeShift(9) == (kCutsEdge ? 9.0f / kWidth : 0.0f));
    CHECK(EdgeLayers(layers, Eye::Right) == (kCutsEdge ? LayerShifts{} : LayerShifts{0, 2, 3, 5}));
    CHECK(EdgeLayers(layers, Eye::Left) ==
          (kCutsEdge ? LayerShifts{} : LayerShifts{0, -1, -3, -4}));
    CHECK(MakeEdgeColumns({0, 2, 3, 5}) == EdgeColumns{0, 5});
    CHECK(MakeEdgeColumns({0, -1, -3, -4}) == EdgeColumns{252, 256});
    CHECK(MakeEdgeColumns({}) == EdgeColumns{});
    CHECK((IsBeyond(4, 5) && !IsBeyond(5, 5) && !IsBeyond(255, 5)));
    CHECK((IsBeyond(252, -4) && !IsBeyond(251, -4) && !IsBeyond(0, -4)));
    CHECK((!IsBeyond(0, 0) && !IsBeyond(255, 0)));
    CHECK(EdgeShift(0) == 0.0f);
}

TEST_CASE("NDS Layer Depth: a pixel ends with what it is from", "[frame_cores]") {
    const std::array<uint16_t, 4> bgcnt{2, 0x41, 3, 0};
    const LayerShifts shifts{0, 3, 6, 9};
    CHECK(PixelShift(0x01, bgcnt.data(), shifts) == 6);
    CHECK(PixelShift(0x02, bgcnt.data(), shifts) == 3);
    CHECK(PixelShift(0x04, bgcnt.data(), shifts) == 9);
    CHECK(PixelShift(0x08, bgcnt.data(), shifts) == 0);
    CHECK(PixelShift(0x20, bgcnt.data(), shifts) == 9); // the backdrop
    CHECK(PixelShift(0x10, bgcnt.data(), shifts) == 0); // sprites, also translucent ones
    CHECK(PixelShift(0x90, bgcnt.data(), shifts) == 0);
    CHECK(PixelShift(0x41, bgcnt.data(), shifts) == 0); // 3D is BG0 too
}

TEST_CASE("NDS Layer Depth: a picture without layers has no edge", "[frame_cores]") {
    const uint32_t level = GENERATE(1u, 3u);
    Game game({.blank = true}, level, true);
    game.core.SetDepthOnSecondary(true);
    game.Run(kStart);
    for (int i = 0; i < 10; ++i) {
        game.Run();
        REQUIRE(game.HasEyes());
        REQUIRE(BothOwn(game));
        REQUIRE_FALSE(game.out.secondary_right.HasPicture());
    }
}

TEST_CASE("NDS Layer Depth: which screens", "[frame_cores]") {
    // ds_depth_on's values
    CHECK(MakeScreens(0, true) == Screens{true, true});
    CHECK(MakeScreens(1, true) == Screens{true, false});
    CHECK(MakeScreens(2, true) == Screens{false, true});
    // ds_depth_by_slot: the slots', which follow Swap Screens
    for (const uint32_t on : {0u, 1u, 2u}) {
        CHECK(OnScreens(on, false, false) == on);
        CHECK(OnScreens(on, false, true) == on);
        CHECK(OnScreens(on, true, false) == on);
    }
    CHECK(OnScreens(0, true, true) == 0);
    CHECK(OnScreens(1, true, true) == 2);
    CHECK(OnScreens(2, true, true) == 1);
    // Nothing shows a right eye's picture of the bottom screen
    CHECK(MakeScreens(0, false) == Screens{true, false});
    CHECK_FALSE(MakeScreens(2, false).Any());
    CHECK(MakeDepth(3, Common::LayerDepth::kNoLimit, false, 0, {true, true}).screens ==
          Screens{true, true});
    CHECK_FALSE(MakeDepth(3, Common::LayerDepth::kNoLimit, false, 0, {false, false}).on);
}

TEST_CASE("NDS Layer Depth: each eye has half of a shift", "[frame_cores]") {
    for (int shift = -2000; shift <= 2000; ++shift) {
        const int right = EyeShift(shift, Eye::Right);
        const int left = EyeShift(shift, Eye::Left);
        REQUIRE(right - left == shift);
        REQUIRE(std::abs(right) - std::abs(left) == std::abs(shift) % 2);
        REQUIRE(EyeShift(-shift, Eye::Right) == -right); // Invert Depth
        REQUIRE(EyeShift(-shift, Eye::Left) == -left);
        // What is farther is never in front
        REQUIRE(EyeShift(shift + 1, Eye::Right) >= right);
        REQUIRE(EyeShift(shift + 1, Eye::Left) <= left);
    }
    CHECK(EyeLayers(Shifts(3), Eye::Right) == LayerShifts{0, 2, 3, 5});
    CHECK(EyeLayers(Shifts(3), Eye::Left) == LayerShifts{0, -1, -3, -4});
    CHECK(EyeLayers(Shifts(3, true), Eye::Right) == LayerShifts{0, -2, -3, -5});
    CHECK(EyeLayers(Shifts(3, true), Eye::Left) == LayerShifts{0, 1, 3, 4});
}

TEST_CASE("NDS Layer Depth: what there is to see", "[frame_cores]") {
    const LayerShifts shifts{0, 3, 6, 9};
    const LayerShifts inverted{0, -3, -6, -9};
    std::array<uint16_t, 512> oam{};
    for (std::size_t i = 0; i < oam.size(); i += 4) {
        oam[i] = 0x0200; // off
    }
    std::array<uint16_t, 4> bgcnt{1, 1, 3, 1};
    const auto has = [&](uint32_t dispcnt, bool engine_a = false) {
        return HasDepth(dispcnt, engine_a, bgcnt.data(), oam.data(), shifts);
    };
    constexpr uint32_t kOn = 0x10000;
    CHECK_FALSE(has(kOn));
    CHECK_FALSE(has(kOn | 0x100));
    CHECK_FALSE(has(kOn | 0xB00)); // three of one priority
    CHECK(has(kOn | 0xF00));
    CHECK(has(kOn | 0x500));
    CHECK_FALSE(has(0xF00));                 // the display is off
    CHECK_FALSE(has(0x20000 | 0xF00, true)); // shows video memory
    CHECK_FALSE(has(kOn | 0x80 | 0xF00));    // blank
    // 3D is in front of itself, on engine A alone
    CHECK(has(kOn | 0x108, true));
    CHECK_FALSE(has(kOn | 0x108, false));
    CHECK_FALSE(has(kOn | 0x208, true));
    // Video memory on engine A's screen, with captures of both eyes in it
    CHECK(HasDepth(0x20000 | 0xF00, true, bgcnt.data(), oam.data(), shifts, true));
    CHECK_FALSE(HasDepth(0x20000 | 0xF00, false, bgcnt.data(), oam.data(), shifts, true));
    CHECK_FALSE(HasDepth(0xF00, true, bgcnt.data(), oam.data(), shifts, true));
    CHECK_FALSE(HasDepth(0x30000 | 0xF00, true, bgcnt.data(), oam.data(), shifts, true));
    // A sprite of another priority
    oam[8] = 100;
    oam[9] = 20;
    oam[10] = 0x0000;
    CHECK(has(kOn | 0x1100));
    CHECK_FALSE(has(kOn | 0x100)); // not shown
    oam[10] = 0x0400;
    CHECK_FALSE(has(kOn | 0x1100)); // the same
    CHECK_FALSE(has(kOn | 0x1000)); // alone
    // Inverted as well; not where the limit leaves nothing
    bgcnt[0] = 3;
    CHECK(HasDepth(kOn | 0x1100, false, bgcnt.data(), oam.data(), inverted));
    CHECK_FALSE(HasDepth(kOn | 0x1100, false, bgcnt.data(), oam.data(), {}));
    CHECK_FALSE(HasDepth(kOn | 0x108, true, bgcnt.data(), oam.data(), {}));
    CHECK_FALSE(HasDepth(kOn | 0x600, false, std::array<uint16_t, 4>{0, 1, 0, 0}.data(), oam.data(),
                         {0, 0, 1, 1})); // priorities 0 and 1 are at one place

    SECTION("sprites parked beyond the screen") {
        CHECK(IsSpriteSeen(0, 0));
        CHECK(IsSpriteSeen(191, 255));
        CHECK_FALSE(IsSpriteSeen(192, 0));
        CHECK(IsSpriteSeen(250, 0));       // the bottom of it: 8 high
        CHECK_FALSE(IsSpriteSeen(248, 0)); // all of it above
        CHECK_FALSE(IsSpriteSeen(0, 256));
        CHECK(IsSpriteSeen(0, 505));
        CHECK_FALSE(IsSpriteSeen(0, 504));
        CHECK(IsSpriteSeen(200, 0xC000)); // 64 high
        CHECK_FALSE(IsSpriteSeen(192, 0xC000));
        CHECK(IsSpriteSeen(192 | 0x300, 0xC000)); // 128
        CHECK_FALSE(IsSpriteSeen(0x200, 0));      // off
        CHECK_FALSE(IsSpriteSeen(0x800, 0));      // a window
        CHECK(SizeOfSprite(0x4000, 0xC000).width == 64);
        CHECK(SizeOfSprite(0x4000, 0xC000).height == 32);
        CHECK(SizeOfSprite(0x8000, 0x4000).width == 8);
        CHECK(SizeOfSprite(0x8000, 0x4000).height == 32);
    }
}

TEST_CASE("NDS Layer Depth: on one screen", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    const bool a_on_top = GENERATE(true, false);
    const bool both_screens = GENERATE(false, true);
    const bool inverted = GENERATE(false, true);
    const InvertDepth invert{inverted};
    const Scene scene{.a_on_top = a_on_top, .scene_3d = true, .both_screens = both_screens};
    Game both(scene, 3, threaded);
    Game one(scene, 3, threaded);
    Game without(scene, 0, threaded);
    both.core.SetDepthOnSecondary(true);
    one.core.SetDepthOnSecondary(true);
    both.Run(kStart);
    one.Run(kStart);
    without.Run(kStart);
    // Changed while it runs
    for (const uint32_t on : {1u, 2u, 0u, 2u, 1u, 0u}) {
        one.core.SetDepthOn(on);
        for (int i = 0; i < 24; ++i) {
            INFO("on " << on << " frame " << i);
            both.Run();
            one.Run();
            without.Run();
            REQUIRE((one.Own() == without.Own()));
            REQUIRE((one.OwnBottom() == without.OwnBottom()));
            const bool top = on != 2;
            const bool bottom = on != 1;
            REQUIRE(one.out.primary_right.HasPicture() == top);
            if (!bottom || i >= 3) {
                REQUIRE(one.out.secondary_right.HasPicture() == bottom);
            }
            REQUIRE(one.out.layer_shift == (top ? EdgeShift(inverted ? -9 : 9) : 0.0f));
            if (i < 6) {
                continue; // the renderer's first frames, and a screen that has just been turned on
            }
            REQUIRE(one.out.secondary_shift == (bottom ? EdgeShift(inverted ? -9 : 9) : 0.0f));
            if (top) {
                REQUIRE((one.Seen(Eye::Left) == both.Seen(Eye::Left)));
                REQUIRE((one.Seen(Eye::Right) == both.Seen(Eye::Right)));
            }
            if (bottom) {
                REQUIRE((one.SeenBottom(Eye::Left) == both.SeenBottom(Eye::Left)));
                REQUIRE((one.SeenBottom(Eye::Right) == both.SeenBottom(Eye::Right)));
            }
            if (!both_screens && (a_on_top ? !top : !bottom)) {
                REQUIRE(one.core.DepthLines().with_3d == 0); // nor is it drawn (SceneNeeded)
            }
        }
    }
}

TEST_CASE("NDS Layer Depth: a flat bottom screen stays one picture", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    // One background on the bottom screen; on the other one everything
    Game flat({.a_on_top = false, .single = 1}, 3, threaded);
    flat.core.SetDepthOnSecondary(true);
    for (int i = 0; i < 60; ++i) {
        flat.Run();
        REQUIRE(flat.out.primary_right.HasPicture());
        REQUIRE_FALSE(flat.out.secondary_right.HasPicture());
        REQUIRE(flat.out.secondary_shift == 0.0f);
    }
    // Once there was something to see, it has two for as long as it is on
    Game game({.a_on_top = false}, 3, threaded);
    game.core.SetDepthOnSecondary(true);
    game.Run(kStart);
    REQUIRE(game.HasEyesBottom());
    std::vector<uint8_t> state;
    REQUIRE(game.core.SaveStateToMemory(state));
    for (const uint32_t level : {1u, 3u, 2u}) {
        game.core.SetLayerDepth(level);
        if (level == 3) {
            REQUIRE(game.core.LoadStateFromMemory(state.data(), state.size()));
        }
        for (int i = 0; i < 20; ++i) {
            game.Run();
            REQUIRE(game.out.secondary_right.HasPicture());
        }
    }
    // Off (a level, or what shows it) and on again
    game.core.SetLayerDepth(0);
    game.Run();
    REQUIRE_FALSE(game.out.secondary_right.HasPicture());
    game.core.SetLayerDepth(3);
    game.Run(4);
    REQUIRE(game.HasEyesBottom());
    game.core.SetDepthOnSecondary(false);
    game.Run();
    REQUIRE_FALSE(game.out.secondary_right.HasPicture());
    game.core.SetDepthOnSecondary(true);
    game.Run(4);
    REQUIRE(game.HasEyesBottom());
}

TEST_CASE("NDS Layer Depth: a layer ends where the game's picture does", "[frame_cores]") {
    const uint32_t level = GENERATE(1u, 3u);
    const bool inverted = GENERATE(false, true);
    const bool a_on_top = GENERATE(true, false);
    const InvertDepth invert{inverted};
    const int single = GENERATE(0, 1, 2, 3, 4);
    Game game({.a_on_top = a_on_top, .single = single, .empty_sprite = true}, level, true);
    Game dark({.a_on_top = a_on_top, .single = single, .empty_sprite = true, .dark = true}, 0,
              true);
    game.core.SetDepthOnSecondary(true);
    game.Run(kStart);
    dark.Run(kStart);
    const LayerShifts shifts = Shifts(level, inverted);
    const int shift = shifts[single == 4 ? 3 : kPriorities[single]];
    for (int i = 0; i < 40; ++i) {
        game.Run();
        dark.Run();
        INFO("single " << single << " frame " << i);
        REQUIRE(game.out.layer_shift == EdgeShift(shifts[3]));
        REQUIRE(IsMoved(game, a_on_top ? game.Own() : game.OwnBottom(),
                        a_on_top ? dark.Own() : dark.OwnBottom(), shift, shifts[3], !a_on_top));
    }
}

TEST_CASE("NDS Layer Depth: 3D past the picture's edges is not squeezed at them", "[frame_cores]") {
    const uint32_t level = GENERATE(1u, 2u, 3u);
    const bool inverted = GENERATE(false, true);
    const bool clear_image = GENERATE(false, true);
    const bool threaded = GENERATE(true, false);
    const uint32_t scroll = GENERATE(0u, 4u, 0x1FBu);
    const InvertDepth invert{inverted};
    Scene scene{.scene_3d = true,
                .single = 0,
                .wide = !clear_image,
                .clear_image = clear_image,
                .scroll_3d = scroll};
    Game game(scene, level, threaded);
    scene.dark = true;
    Game dark(scene, 0, threaded);
    const SceneRoom room = MakeRoom(Shifts(level, inverted), 0x0100, {2, 0, 0, 0});
    const int shift =
        clear_image ? room.farthest : VertexShift(MakeScene(room, {4 * kOneW, 0}), 8 * kOneW);
    REQUIRE((shift != 0 || inverted));
    game.Run(kStart);
    dark.Run(kStart);
    for (int i = 0; i < 20; ++i) {
        game.Run();
        dark.Run();
        INFO("frame " << i);
        REQUIRE(game.core.DepthLines().with_3d == 192);
        for (const Eye eye : kEyes) {
            Pixels seen = game.Seen(eye);
            const int by = EyeShift(shift, eye);
            const int farthest = Shifts(level, inverted)[3];
            const bool far_end = !inverted && !clear_image;
            int begin = 0;
            int end = scroll == 4 ? kWidth - 4 + std::min(EyeShift(farthest, eye), 0) : kWidth;
            if (eye == Eye::Right) {
                for (int at = 0; far_end && scroll == 4 && at < static_cast<int>(seen.size());
                     ++at) {
                    if (IsPast(at % kWidth, by)) { // beyond the game's edge is the scene's
                        seen[at] = kBlack;
                    }
                }
            } else if (scroll == 0x1FB) {
                begin = 5 + std::max(EyeShift(farthest, eye), 0);
            }
            if (eye == Eye::Left && far_end) {
                // All of it ends where its farthest does
                end = std::min(end, kWidth + EyeShift(room.farthest, eye));
                for (int at = 0; scroll == 0 && at < static_cast<int>(seen.size()); ++at) {
                    REQUIRE((at % kWidth < end || seen[at] == kBlack));
                }
            }
            const int edge = scroll == 0x1FB ? 5 : 0; // the game's own
            const int at = FirstMoved(game.Own(), dark.Own(), seen, eye, shift, farthest,
                                      clear_image ? -1 : by + edge, begin, end);
            INFO("column " << at % kWidth << " line " << at / kWidth << " shift " << shift
                           << " eye " << static_cast<int>(eye) << " level " << level << " scroll "
                           << scroll);
            REQUIRE(at == kNone);
        }
    }
}

TEST_CASE("NDS Layer Depth: a picture is cut for what it was drawn with", "[frame_cores]") {
    const bool both_screens = GENERATE(false, true);
    const InvertDepth as_it_is{false};
    const Scene scene{.both_screens = both_screens, .single = 2}; // all of it at priority 3
    Game game(scene, 3, true);
    Scene nothing_behind = scene;
    nothing_behind.dark = true;
    Game dark(nothing_behind, 0, true);
    game.Run(kStart);
    dark.Run(kStart);
    auto& inverted = Common::LayerDepth::Get().inverted;
    Pixels own_before = game.Own();
    Pixels dark_before = dark.Own();
    int last = 9;
    const auto run = [&](int wanted) {
        for (int i = 0; i < 10; ++i) {
            game.Run();
            dark.Run();
            INFO("frame " << i << " wanted " << wanted);
            REQUIRE(game.HasEyes());
            const Pixels own = game.Own();
            const Pixels dark_own = dark.Own();
            const auto is = [&](int shift) {
                // In a game with 3D on both screens every other picture is of the frame before
                return IsMoved(game, own, dark_own, shift, shift) ||
                       (both_screens && IsMoved(game, own_before, dark_before, shift, shift));
            };
            const int shift =
                game.out.layer_shift == EdgeShift(wanted) && is(wanted) ? wanted : last;
            REQUIRE(is(shift));
            REQUIRE(game.out.layer_shift == EdgeShift(shift));
            REQUIRE((shift == wanted || i < 2)); // it comes soon, and stays
            last = shift;
            own_before = own;
            dark_before = dark_own;
        }
    };
    run(9);
    game.core.SetLayerDepth(1);
    run(3);
    inverted = true;
    run(-3);
    game.core.SetLayerDepth(3);
    run(-9);
    game.core.SetMaxLayerShift(4.0f / kWidth);
    run(Common::LayerDepth::Directed(Common::LayerDepth::LayerShift(3, 3, 3, kWidth, 4.0f / kWidth),
                                     true));
    inverted = false;
    game.core.SetMaxLayerShift(Common::LayerDepth::kNoLimit);
    run(9);
}

TEST_CASE("NDS Layer Depth: the pictures are cut the same in every frame", "[frame_cores]") {
    const bool inverted = GENERATE(false, true);
    const InvertDepth invert{inverted};
    Game game({.scene_3d = true}, 3, true);
    std::vector<uint8_t> state;
    const auto run = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            game.Run();
            REQUIRE(game.out.primary_right.HasPicture());
            REQUIRE(game.out.layer_shift == EdgeShift(inverted ? -9 : 9));
        }
    };
    run(30);
    REQUIRE(game.core.SaveStateToMemory(state));
    run(5);
    game.core.Reset();
    run(30);
    REQUIRE(game.core.LoadStateFromMemory(state.data(), state.size()));
    run(10);
}

TEST_CASE("NDS Layer Depth: inverted while the game runs", "[frame_cores]") {
    const bool threaded = GENERATE(true, false);
    Game game({.scene_3d = true}, 2, threaded);
    Game off({.scene_3d = true}, 0, threaded);
    game.Run(40);
    off.Run(40);
    {
        const InvertDepth invert{true};
        Game other({.scene_3d = true}, 2, threaded); // inverted from the start
        other.Run(40);
        for (int i = 0; i < 20; ++i) {
            INFO("frame " << i);
            game.Run();
            other.Run();
            off.Run();
            REQUIRE((game.Own() == off.Own()));
            REQUIRE(!off.out.primary_right.HasPicture()); // no level
            REQUIRE(off.out.layer_shift == 0.0f);
            if (i >= 2) {
                REQUIRE((game.Seen(Eye::Left) == other.Seen(Eye::Left)));
                REQUIRE((game.Seen(Eye::Right) == other.Seen(Eye::Right)));
                REQUIRE(game.out.layer_shift < 0.0f);
            }
        }
    }
    game.Run(3);
    CHECK(game.out.layer_shift == EdgeShift(Shifts(2)[3]));
}
