// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame bfikit: which top screen swaps carry a new game picture (video_core/
// vr_content_repeat.h): a world redrawn at 60 with its logic at 30 repeats every other swap, a
// still scene repeats in a run, a camera turned at 60 over a world moved at 30 repeats only
// without the camera's rows; and the ring the XR thread reads the serials from.

#include <array>
#include <cstdint>
#include <catch2/catch_test_macros.hpp>
#include "video_core/vr_content_repeat.h"

namespace {

namespace I = VideoCore::Immersive;

/// One frame's world uploads: `n` of them, the world's state `world` and the camera's `cam`.
void Frame(I::ContentRepeat& c, uint64_t world, uint64_t cam, int n = 3) {
    for (int i = 0; i < n; i++) {
        std::array<uint64_t, 4> rows{world, world * 7 + static_cast<uint64_t>(i), cam, 0};
        const uint64_t all = I::HashWords(rows.data(), sizeof(rows));
        const uint64_t scene = I::HashWords(rows.data(), 2 * sizeof(uint64_t));
        c.Upload(all, scene);
    }
    c.Transfer(); // the picture copied into the framebuffer
    c.Swap();
}

} // namespace

TEST_CASE("Content repeats: the hash", "[video_core][bfikit]") {
    const std::array<uint32_t, 5> a{1, 2, 3, 4, 5}, b{1, 2, 3, 4, 6};
    CHECK(I::HashWords(a.data(), sizeof(a)) == I::HashWords(a.data(), sizeof(a)));
    CHECK(I::HashWords(a.data(), sizeof(a)) != I::HashWords(b.data(), sizeof(b))); // the tail
    CHECK(I::HashWords(a.data(), 8) != I::HashWords(a.data(), 16));
    CHECK(I::HashWords(a.data(), 0) != I::HashWords(a.data(), 4));
}

TEST_CASE("Content repeats: 60 swaps, 60 pictures", "[video_core][bfikit]") {
    I::ContentRepeat c;
    for (uint64_t f = 1; f <= 60; f++) {
        Frame(c, f, f);
    }
    const I::ContentCounts n = c.Take();
    CHECK(n.swaps == 60);
    CHECK(n.read == 60);
    CHECK(n.same == 0);
    CHECK(n.scene_same == 0);
    CHECK(c.Serial(I::ContentBy::All) == 60);
    CHECK(c.Take().swaps == 0); // a new window
}

TEST_CASE("Content repeats: 60 swaps, 30 pictures", "[video_core][bfikit]") {
    I::ContentRepeat c;
    // The world and its camera move every other swap (a 30 fps game redrawn at each VBlank).
    for (uint64_t f = 0; f < 60; f++) {
        Frame(c, f / 2, f / 2);
    }
    const I::ContentCounts n = c.Take();
    CHECK(n.swaps == 60);
    CHECK(n.same == 30);
    CHECK(n.same_alt == 30); // each right after a new one: the 30-in-60 pattern
    CHECK(n.scene_same == 30);
    CHECK(c.Serial(I::ContentBy::All) == 30);
    // The camera turned with the head at every swap, the world at 30: new by all the uniforms,
    // repeats without the camera's rows.
    I::ContentRepeat h;
    for (uint64_t f = 0; f < 60; f++) {
        Frame(h, f / 2, 1000 + f);
    }
    const I::ContentCounts m = h.Take();
    CHECK(m.same == 0);
    CHECK(m.scene_same == 30);
    CHECK(m.scene_same_alt == 30);
    CHECK(h.Serial(I::ContentBy::All) == 60);
    CHECK(h.Serial(I::ContentBy::Scene) == 30);
}

TEST_CASE("Content repeats: a still scene, and frames not read", "[video_core][bfikit]") {
    I::ContentRepeat c;
    Frame(c, 5, 5);
    for (int i = 0; i < 10; i++) {
        Frame(c, 5, 5); // nothing moves: a run of repeats, only the first right after a new one
    }
    I::ContentCounts n = c.Take();
    CHECK(n.same == 10);
    CHECK(n.same_alt == 1);
    // A swap without world uploads (a 2D screen): not known, a new picture, and the one after is
    // not compared with it.
    c.Transfer();
    c.Swap();
    Frame(c, 5, 5);
    n = c.Take();
    CHECK(n.swaps == 2);
    CHECK(n.read == 1);
    CHECK(n.same == 0);
    CHECK(c.Serial(I::ContentBy::All) == 3);
}

TEST_CASE("Content repeats: swaps with nothing copied", "[video_core][bfikit]") {
    // Super Mario 3D Land in session 4: about every other swap without a display transfer (3634
    // of 7271): the picture before again, whatever was uploaded.
    I::ContentRepeat c;
    for (uint64_t f = 0; f < 60; f++) {
        if (f % 2 == 0) {
            Frame(c, f, f);
        } else {
            c.Upload(1, 1); // drawn, not copied
            c.Swap();
        }
    }
    const I::ContentCounts n = c.Take();
    CHECK(n.swaps == 60);
    CHECK(n.uncopied == 30);
    CHECK(n.uncopied_alt == 30);
    CHECK(n.read == 30);
    CHECK(n.same == 0);
    CHECK(c.Serial(I::ContentBy::All) == 30);
    CHECK(c.Serial(I::ContentBy::Scene) == 30);
}

TEST_CASE("Content repeats: the ring by swap number", "[video_core][bfikit]") {
    I::ContentRing r;
    CHECK_FALSE(r.Get(0, I::ContentBy::All));
    CHECK_FALSE(r.Get(5, I::ContentBy::All)); // never put
    r.Put(5, 3, 2);
    CHECK(r.Get(5, I::ContentBy::All) == 3u);
    CHECK(r.Get(5, I::ContentBy::Scene) == 2u);
    r.Put(5 + I::ContentRing::kRing, 9, 9); // the slot taken by a later swap
    CHECK_FALSE(r.Get(5, I::ContentBy::All));
    CHECK(r.Get(5 + I::ContentRing::kRing, I::ContentBy::All) == 9u);
}
