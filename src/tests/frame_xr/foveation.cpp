// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharVR foveation: the layout of the shading rate image (vk_foveation.h).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "video_core/renderer_vulkan/vk_foveation.h"

namespace F = VideoCore::Foveation;

TEST_CASE("foveation: the level of the switch", "[frame_xr]") {
    REQUIRE(F::LevelFromText(nullptr) == 0);
    REQUIRE(F::LevelFromText("") == 0);
    REQUIRE(F::LevelFromText("0") == 0);
    REQUIRE(F::LevelFromText("1") == 1);
    REQUIRE(F::LevelFromText("2") == 2);
    REQUIRE(F::LevelFromText("7") == 2);
    REQUIRE(F::LevelFromText("abc") == 0);
}

TEST_CASE("foveation: the rate image has a full rate centre", "[frame_xr]") {
    for (int level : {1, 2}) {
        const auto img = F::MakeRateImage(level, 2400, 1440, 16);
        const std::uint32_t w = 150, h = 90;
        REQUIRE(img.size() == w * h);
        REQUIRE(img[(h / 2) * w + w / 2] == F::kRate1x1);
        REQUIRE(img[0] == (level == 1 ? F::kRate2x2 : F::kRate4x4));
    }
    // Level 2 has a middle ring of 2x2; level 1 has no 4x4; off is all 1x1.
    const auto l2 = F::MakeRateImage(2, 2400, 1440, 16);
    REQUIRE(std::count(l2.begin(), l2.end(), F::kRate2x2) > 0);
    const auto l1 = F::MakeRateImage(1, 2400, 1440, 16);
    REQUIRE(std::count(l1.begin(), l1.end(), F::kRate4x4) == 0);
    const auto l0 = F::MakeRateImage(0, 2400, 1440, 16);
    REQUIRE(std::count(l0.begin(), l0.end(), F::kRate1x1) == static_cast<long>(l0.size()));
}

TEST_CASE("foveation: only the eye picture is found", "[frame_xr]") {
    REQUIRE(F::IsEyePicture(2400, 1440, 6));
    REQUIRE(F::IsEyePicture(1440, 2400, 6));
    // Power-of-two padded top-screen surfaces (256 x 512, e.g. Kid Icarus: Uprising)
    REQUIRE(F::IsEyePicture(1536, 3072, 6));
    REQUIRE(F::IsEyePicture(1280, 2560, 5));
    REQUIRE(F::IsEyePicture(1024, 2048, 4));
    REQUIRE(F::IsEyePicture(3072, 1536, 6));
    REQUIRE_FALSE(F::IsEyePicture(1920, 1440, 6)); // the bottom screen (320 x 240)
    REQUIRE_FALSE(F::IsEyePicture(1536, 1536, 6)); // square offscreen target (256 x 256)
    REQUIRE_FALSE(F::IsEyePicture(400, 240, 1));
    REQUIRE_FALSE(F::IsEyePicture(256, 512, 1));
}

TEST_CASE("foveation: padded surface centres on active draw rect", "[frame_xr]") {
    // 256 x 512 at 6x = 1536 x 3072, with active 240 x 400 eye picture (1440 x 2400) at y = 0..2400
    const auto img = F::MakeRateImage(2, 1536, 3072, 16, 0.5f, 0.5f, 0, 0, 1440, 2400);
    const std::uint32_t w = 96, h = 192;
    REQUIRE(img.size() == w * h);
    // Centre of active 1440 x 2400 rect is (720, 1200) -> texel (45, 75): must be 1x1
    REQUIRE(img[75 * w + 45] == F::kRate1x1);
    // Far bottom padding region (y = 2900 -> texel 181): must be 4x4, not 1x1
    REQUIRE(img[181 * w + 45] == F::kRate4x4);
}

#include "video_core/vr_gaze.h"

TEST_CASE("foveation: the gaze lands in the picture", "[frame_xr]") {
    namespace G = VideoCore::Gaze;
    float u, v;
    G::TanToPicture(0.0f, 0.0f, u, v); // straight ahead: the middle across, a little above the middle
    REQUIRE(u == Catch::Approx(0.5f));
    REQUIRE(v == Catch::Approx(G::kForwardV));
    REQUIRE(v < 0.5f);
    G::TanToPicture(G::kHalfTanX, 0.0f, u, v); // the right edge
    REQUIRE(u == Catch::Approx(1.0f));
    G::TanToPicture(0.0f, G::kHalfTanY + G::kMiddleTanY, u, v); // the top edge
    REQUIRE(v == Catch::Approx(0.0f).margin(1e-4));
    float cx, cy;
    G::PictureToImage(0.8f, 0.3f, 240, 400, 0, cx, cy); // the PICA's rotated surface
    REQUIRE(cx == Catch::Approx(0.7f));
    REQUIRE(cy == Catch::Approx(0.8f));
    G::PictureToImage(0.8f, 0.3f, 400, 240, 0, cx, cy);
    REQUIRE(cx == Catch::Approx(0.8f));
    REQUIRE(cy == Catch::Approx(0.3f));
    G::PictureToImage(0.8f, 0.3f, 400, 240, 3, cx, cy);
    REQUIRE(cx == Catch::Approx(0.2f));
    REQUIRE(cy == Catch::Approx(0.7f));
}

TEST_CASE("foveation: the full rate centre follows the gaze", "[frame_xr]") {
    const auto img = F::MakeRateImage(2, 2400, 1440, 16, 0.25f, 0.75f);
    const std::uint32_t w = 150, h = 90;
    REQUIRE(img[(h / 4) * w + (3 * w / 4)] == F::kRate1x1);
    REQUIRE(img[(3 * h / 4) * w + (w / 4)] != F::kRate1x1);
}
