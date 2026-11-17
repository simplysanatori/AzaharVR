// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame bfidr: the "xr: perf" line (frame_xr/perf_line.h): /proc's per-thread times, the
// threads' groups by name, the CPU shares between two reads, and the line itself.

#include <map>
#include <string>
#include <catch2/catch_test_macros.hpp>
#include "frame_xr/perf_line.h"

using namespace FrameXr::PerfLine;

TEST_CASE("perf line: a thread's /proc stat", "[frame_xr][bfidr]") {
    // A line as Linux writes it: utime 1234 and stime 56 are fields 14 and 15.
    const auto t = ParseTaskStat("4242 (EmuThread) R 4200 4200 0 0 -1 4194368 2000 0 0 0 1234 56 "
                                 "0 0 20 0 30 0 100 0 0\n");
    REQUIRE(t);
    CHECK(t->name == "EmuThread");
    CHECK(t->ticks == 1290);
    // A name with spaces and parentheses: the fields count from the last ')'.
    const auto odd =
        ParseTaskStat("17 (Custom tex (1)) S 1 1 0 0 -1 0 0 0 0 0 7 3 0 0 20 0 1 0 5 0 0");
    REQUIRE(odd);
    CHECK(odd->name == "Custom tex (1)");
    CHECK(odd->ticks == 10);
    // Not a stat line, or one cut short: nothing.
    CHECK_FALSE(ParseTaskStat(""));
    CHECK_FALSE(ParseTaskStat("4242 EmuThread R 1 2 3"));
    CHECK_FALSE(ParseTaskStat("4242 (EmuThread) R 4200 4200 0 0 -1 4194368 2000 0 0 0 1234"));
    CHECK_FALSE(ParseTaskStat("4242 (EmuThread) R 4200 4200 0 0 -1 4194368 2000 0 0 0 12x4 56"));
}

TEST_CASE("perf line: the threads' groups", "[frame_xr][bfidr]") {
    CHECK(std::string(Group("EmuThread")) == "emulation");
    CHECK(std::string(Group("VulkanWorker")) == "video");
    CHECK(std::string(Group("VulkanPresent")) == "present");
    CHECK(std::string(Group("SecondaryPrese")) == "present"); // 15 characters kept
    CHECK(std::string(Group("FrameXR")) == "FrameXR");
    CHECK(std::string(Group("XrRelease")) == "handoff");
    CHECK(std::string(Group("threaded-ml")) == "audio");
    CHECK(std::string(Group("SDLAudioP2")) == "audio");
    CHECK(std::string(Group("alsoft-mixer")) == "audio");
    CHECK(Group("FrameXRWatch") == nullptr); // the watchdog is not the frame loop
    CHECK(Group("citra:Log") == nullptr);
    CHECK(Group("") == nullptr);
}

TEST_CASE("perf line: CPU shares between two reads", "[frame_xr][bfidr]") {
    std::map<int, TaskTimes> before{{1, {"EmuThread", 100}},
                                    {2, {"FrameXR", 50}},
                                    {3, {"citra:Log", 10}},
                                    {4, {"gone", 999}}};
    std::map<int, TaskTimes> after{{1, {"EmuThread", 900}}, // 800 ticks in 10 s at 100 Hz: 80 %
                                   {2, {"FrameXR", 350}},   // 30 %
                                   {3, {"citra:Log", 20}},  // 1 %
                                   {5, {"SDLAudioP2", 40}}, // new: all of it, 4 %
                                   {6, {"VulkanWorker", 0}}};
    // A thread named after the first read (Qt names its thread late): by tid.
    before[7] = {"QThread", 200};
    after[7] = {"EmuThread", 300}; // 10 %
    const CpuShares c = Shares(before, after, 10.0, 100);
    CHECK(c.groups.at("emulation") == 90.0);
    CHECK(c.groups.at("FrameXR") == 30.0);
    CHECK(c.groups.at("audio") == 4.0);
    CHECK(c.groups.at("video") == 0.0);
    CHECK(c.groups.at("handoff") == 0.0); // every group is there, 0 when none ran
    CHECK(c.others_total == 1.0);
    REQUIRE(c.others.size() == 1);
    CHECK(c.others[0].first == "citra:Log");
    CHECK(c.process == 125.0);
    // A tick count that went back (a reused tid): from 0, never negative.
    const CpuShares back = Shares({{1, {"EmuThread", 500}}}, {{1, {"EmuThread", 100}}}, 10.0, 100);
    CHECK(back.groups.at("emulation") == 0.0);
    // No time, no clock: nothing.
    CHECK(Shares(before, after, 0.0, 100).process == 0.0);
    CHECK(Shares(before, after, 10.0, 0).process == 0.0);
}

TEST_CASE("perf line: the line", "[frame_xr][bfidr]") {
    Window w;
    w.seconds = 10.0;
    w.display_frames = 1200;
    w.game_frames = 600;
    w.emu_gpu_ns = 600ull * 4'500'000ull; // 4.5 ms a game frame
    w.emu_buffers = 1800;
    w.pass_gpu_ns = 1200ull * 400'000ull; // 0.4 ms a draw
    w.pass_draws = 1200;
    w.late_releases = 3;
    w.late_lit = 1;
    w.end_frame_max_ms = 3.24;
    w.bfi_seconds = 10;
    w.factor = 5;
    w.screen_factor = 7;
    CpuShares c;
    for (const char* g : kGroups) {
        c.groups[g] = 0.0;
    }
    c.groups["emulation"] = 81.4;
    c.groups["FrameXR"] = 30.0;
    c.others = {{"citra:Log", 2.0}, {"Custom tex", 1.0}};
    c.others_total = 3.0;
    c.process = 114.4;
    const std::string line = Format(w, true, c);
    CHECK(line == "perf 10.0 s, 600 game frames, 1200 refreshes: emulator GPU 4.50 ms a game frame "
                  "(2.25 a refresh, 1800 command buffers), XR pass 0.40 ms a draw (1200 draws); "
                  "late releases 3, late pictures lit again 1, longest xrEndFrame 3.2 ms; Black "
                  "Frame Insertion 10 s, drawn at 5x, the screen shows up to 7x; CPU % of a core: "
                  "emulation 81, video 0, present 0, FrameXR 30, handoff 0, audio 0, others 3 "
                  "(citra:Log 2, Custom tex 1), process 114");
    // The timer off (the default): said so, not a zero.
    const std::string off = Format(w, false, c);
    CHECK(off.find("emulator GPU not measured (AZAHARFRAME_GPU_BUSY=1), XR pass") !=
          std::string::npos);
    // On, but no game frames in the window (paused): no number.
    Window paused = w;
    paused.game_frames = 0;
    CHECK(Format(paused, true, c).find("emulator GPU -,") != std::string::npos);
    // No pass, no others, nothing known about the factor.
    Window bare;
    bare.seconds = 10.0;
    CpuShares none = Shares({}, {}, 10.0, 100);
    const std::string b = Format(bare, true, none);
    CHECK(b.find("XR pass -;") != std::string::npos);
    CHECK(b.find("drawn at") == std::string::npos);
    CHECK(b.find("others 0, process 0") != std::string::npos);
}
