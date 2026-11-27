// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame gpupasses: the bookkeeping of AZAHARFRAME_GPU_PASSES=1 (video_core/gpu_pass_stats.h):
// which bucket a segment's time goes to, and the sums over game frames in the log lines.

#include <string>
#include <vector>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "video_core/gpu_pass_stats.h"
#include "video_core/rasterizer_cache/pixel_format.h"

namespace GP = VideoCore::GpuPasses;

namespace {

GP::Segment Pass(u64 frame, GP::Bucket bucket, u32 w, u32 h, u32 draws = 10) {
    GP::Segment s;
    s.frame = frame;
    s.bucket = bucket;
    s.pass = true;
    s.load = true;
    s.store = true;
    s.attachments = 2;
    s.color = static_cast<u8>(VideoCore::PixelFormat::RGBA8);
    s.depth = static_cast<u8>(VideoCore::PixelFormat::D24S8);
    s.width = w;
    s.height = h;
    s.area = w * h;
    s.draws = draws;
    s.scaled = true;
    s.target = 1;
    s.end = GP::Why::Target;
    return s;
}

GP::Segment Work(u64 frame, GP::Bucket bucket, u32 ops = 1) {
    GP::Segment s;
    s.frame = frame;
    s.bucket = bucket;
    s.ops = ops;
    return s;
}

const std::string& Line(const std::vector<std::string>& lines, const std::string& start) {
    static const std::string none;
    for (const auto& line : lines) {
        if (line.rfind(start, 0) == 0) {
            return line;
        }
    }
    return none;
}

bool Has(const std::string& line, const std::string& part) {
    return line.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("gpu passes: the work outside passes goes to its bucket", "[gpupasses]") {
    REQUIRE(GP::BucketOf(GP::Why::Copy) == GP::Bucket::Copy);
    REQUIRE(GP::BucketOf(GP::Why::Blit) == GP::Bucket::Copy);
    REQUIRE(GP::BucketOf(GP::Why::Clear) == GP::Bucket::Copy);
    REQUIRE(GP::BucketOf(GP::Why::Convert) == GP::Bucket::Copy);
    REQUIRE(GP::BucketOf(GP::Why::Scale) == GP::Bucket::Copy);
    REQUIRE(GP::BucketOf(GP::Why::Upload) == GP::Bucket::Upload);
    REQUIRE(GP::BucketOf(GP::Why::Mipmap) == GP::Bucket::Upload);
    REQUIRE(GP::BucketOf(GP::Why::Readback) == GP::Bucket::Readback);
    REQUIRE(GP::BucketOf(GP::Why::Present) == GP::Bucket::Present);
    REQUIRE(GP::BucketOf(GP::Why::Foveation) == GP::Bucket::Other);
    // Not work of their own: a target change, a submit, nothing.
    REQUIRE(GP::BucketOf(GP::Why::Target) == GP::Bucket::Count);
    REQUIRE(GP::BucketOf(GP::Why::Area) == GP::Bucket::Count);
    REQUIRE(GP::BucketOf(GP::Why::Submit) == GP::Bucket::Count);
    REQUIRE(GP::BucketOf(GP::Why::None) == GP::Bucket::Count);
}

TEST_CASE("gpu passes: the game's passes by screen and draws", "[gpupasses]") {
    REQUIRE(GP::ScreenOf(240, 400) == GP::Screen::Top);
    REQUIRE(GP::ScreenOf(400, 240) == GP::Screen::Top);
    REQUIRE(GP::ScreenOf(240, 320) == GP::Screen::Bottom);
    REQUIRE(GP::ScreenOf(320, 240) == GP::Screen::Bottom);
    REQUIRE(GP::ScreenOf(256, 256) == GP::Screen::None);
    REQUIRE(GP::GameBucket(false, GP::Screen::Top, 5) == GP::Bucket::Top3D);
    REQUIRE(GP::GameBucket(false, GP::Screen::Top, 0) == GP::Bucket::Top2D);
    REQUIRE(GP::GameBucket(false, GP::Screen::Bottom, 5) == GP::Bucket::Bottom);
    REQUIRE(GP::GameBucket(false, GP::Screen::None, 5) == GP::Bucket::Offscreen);
    REQUIRE(GP::GameBucket(true, GP::Screen::Top, 5) == GP::Bucket::Offscreen); // shadows
    REQUIRE(GP::IsPassBucket(GP::Bucket::Top3D));
    REQUIRE(GP::IsPassBucket(GP::Bucket::Offscreen));
    REQUIRE_FALSE(GP::IsPassBucket(GP::Bucket::Copy));
    REQUIRE_FALSE(GP::IsPassBucket(GP::Bucket::Present));
}

TEST_CASE("gpu passes: a pass's load and store traffic", "[gpupasses]") {
    GP::Segment s = Pass(0, GP::Bucket::Top3D, 100, 10);
    REQUIRE(GP::LoadStoreBytes(s) == Catch::Approx(1000.0 * (4 + 4) * 2)); // loaded and stored
    s.load = false;
    s.clear = true;
    REQUIRE(GP::LoadStoreBytes(s) == Catch::Approx(1000.0 * 8)); // cleared: stored only
    s.depth = GP::kNoFormat;
    REQUIRE(GP::LoadStoreBytes(s) == Catch::Approx(1000.0 * 4));
}

TEST_CASE("gpu passes: sums a frame, the 95th percentile and complete frames", "[gpupasses]") {
    GP::Aggregator a;
    // 20 frames: the top screen's 3D 4 ms (the last one 24 ms), a copy 1 ms.
    for (u64 f = 0; f < 20; ++f) {
        a.Add(Pass(f, GP::Bucket::Top3D, 960, 1600), f == 19 ? 24.0 : 4.0);
        a.Add(Work(f, GP::Bucket::Copy, 2), 1.0);
    }
    // Frame 19 is complete only once a segment of frame 20 came.
    REQUIRE(a.Frames() == 19);
    a.Add(Work(20, GP::Bucket::Other), 0.5);
    REQUIRE(a.Frames() == 20);
    const auto lines = a.Report(10.0);
    const std::string& head = Line(lines, "gpu passes: 20 game frames");
    REQUIRE(Has(head, "GPU time 6.00 ms a frame")); // (19 * 5 + 25) / 20
    REQUIRE(Has(head, "(p95 5.00)"));         // the 19th of 20 sorted
    REQUIRE(Has(head, "2 timestamps a frame"));
    const std::string& top = Line(lines, "gpu passes: top 3D ");
    REQUIRE(Has(top, "5.00 ms a frame (p95 4.00)"));
    REQUIRE(Has(top, "1.0 passes, 10.0 draws (0.0 2D), 1.0 targets, 1.54 Mpx"));
    REQUIRE(Has(top, "attachments loaded 2.0 cleared 0.0 stored 2.0"));
    REQUIRE(Has(top, "~24.6 MB load+store")); // 1.536 Mpx x 8 bytes x 2
    REQUIRE(Has(Line(lines, "gpu passes: copies "), "1.00 ms a frame (p95 1.00), 2.0 operations"));
    REQUIRE(Line(lines, "gpu passes: bottom ").empty()); // nothing there, no line
    REQUIRE(Has(Line(lines, "gpu passes: the game's render passes into scaled"),
                "scaled targets 5.00 ms a frame (1.54 Mpx), into 1x targets 0.00 ms"));
    REQUIRE(Has(Line(lines, "gpu passes: the game's render passes into scaled"),
                "work outside them 1.00 ms"));
    // A new window: frame 20 (still open) is in the next one.
    REQUIRE(a.Frames() == 0);
    a.Add(Work(21, GP::Bucket::Other), 0.5);
    const auto next = a.Report(10.0);
    REQUIRE(!Line(next, "gpu passes: 1 game frames").empty());
}

TEST_CASE("gpu passes: pass ends, re-opened targets and the top 5", "[gpupasses]") {
    GP::Aggregator a;
    for (u64 f = 0; f < 4; ++f) {
        GP::Segment eye = Pass(f, GP::Bucket::Top3D, 960, 1600, 300);
        eye.end = GP::Why::Copy;
        a.Add(eye, 3.0);
        GP::Segment again = Pass(f, GP::Bucket::Top3D, 960, 1600, 20);
        again.index = 1;
        again.reopen = GP::Why::Copy;
        again.flat = 20;
        again.end = GP::Why::Submit;
        a.Add(again, 1.0);
        GP::Segment hud = Pass(f, GP::Bucket::Top2D, 960, 1600, 40);
        hud.target = 2;
        hud.scaled = false;
        hud.flat = 40;
        a.Add(hud, 0.5);
        GP::Segment shadow = Pass(f, GP::Bucket::Offscreen, 256, 256, 5);
        shadow.target = 3;
        shadow.load = shadow.store = false;
        shadow.clear = true;
        a.Add(shadow, 0.25);
    }
    a.Add(Work(4, GP::Bucket::Other), 0.0);
    const auto lines = a.Report(1.0);
    const std::string& ends = Line(lines, "gpu passes: pass ends a frame");
    REQUIRE(Has(ends, "pass ends a frame 4.0 (target change 2.0, copy 1.0, submit 1.0)"));
    REQUIRE(Has(ends, "stored 1.0 (after copy 1.0)"));
    const std::string& top3d = Line(lines, "gpu passes: top 3D ");
    REQUIRE(Has(top3d, "2.0 passes, 320.0 draws (20.0 2D), 1.0 targets"));
    const std::string& off = Line(lines, "gpu passes: offscreen ");
    REQUIRE(Has(off, "attachments loaded 0.0 cleared 2.0 stored 0.0"));
    REQUIRE(Has(Line(lines, "gpu passes: the game's render passes into scaled"),
                "scaled targets 4.25 ms a frame"));
    REQUIRE(Has(Line(lines, "gpu passes: the game's render passes into scaled"),
                "into 1x targets 0.50 ms"));
    const std::string& best = Line(lines, "gpu passes: top 5 passes by time");
    REQUIRE(Has(best, "1) top 3D 960x1600 RGBA8+D24S8 pass 1 on its target: 3.00 ms each, "
                      "1.0 a frame, 300 draws"));
    REQUIRE(Has(best, "2) top 3D 960x1600 RGBA8+D24S8 pass 2 on its target: 1.00 ms each"));
    REQUIRE(Has(best, "3) top 2D"));
    REQUIRE(Has(best, "4) offscreen 256x256"));
    REQUIRE_FALSE(Has(best, "5)"));
    // A rare end is shown with two decimals, not as 0.0.
    GP::Aggregator rare;
    for (u64 f = 0; f < 40; ++f) {
        GP::Segment p = Pass(f, GP::Bucket::Top3D, 8, 8);
        p.end = f == 0 ? GP::Why::Readback : GP::Why::Target;
        rare.Add(p, 0.1);
    }
    rare.Add(Work(40, GP::Bucket::Other), 0.0);
    REQUIRE(Has(Line(rare.Report(1.0), "gpu passes: pass ends a frame"),
                "(target change 1.0, readback 0.03)"));
}

TEST_CASE("gpu passes: untimed command buffers and an empty window", "[gpupasses]") {
    GP::Aggregator a;
    REQUIRE(a.Report(10.0).empty()); // no frame: no line
    a.Add(Work(0, GP::Bucket::Upload, 3), 0.75);
    a.Add(Work(1, GP::Bucket::Present), 0.25);
    a.Untimed(2);
    const auto lines = a.Report(10.0);
    REQUIRE(Has(Line(lines, "gpu passes: 1 game frames"), "2 command buffers not timed"));
    REQUIRE(Has(Line(lines, "gpu passes: uploads "), "0.75 ms a frame (p95 0.75), 3.0 operations"));
    REQUIRE(Has(Line(lines, "gpu passes: top 5 passes by time"), "none"));
    // The count is per window.
    a.Add(Work(2, GP::Bucket::Present), 0.25);
    REQUIRE_FALSE(Has(Line(a.Report(10.0), "gpu passes: 1 game frames"), "not timed"));
}

TEST_CASE("gpu passes: the top 5 keeps a bounded number of passes", "[gpupasses]") {
    GP::Aggregator a;
    for (u32 i = 0; i < GP::Aggregator::kMostKeys + 10; ++i) {
        a.Add(Pass(0, GP::Bucket::Offscreen, 8 + i, 8), 0.01);
    }
    a.Add(Work(1, GP::Bucket::Other), 0.0);
    const auto lines = a.Report(10.0);
    REQUIRE(Has(Line(lines, "gpu passes: top 5 passes by time"), "(10 not kept)"));
    REQUIRE(Has(Line(lines, "gpu passes: offscreen "), "266.0 passes"));
}
