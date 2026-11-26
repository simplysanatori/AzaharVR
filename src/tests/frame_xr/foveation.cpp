// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharVR foveation: the layout of the shading rate image (vk_foveation.h).

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
    REQUIRE_FALSE(F::IsEyePicture(1920, 1440, 6)); // the bottom screen (320 x 240)
    REQUIRE_FALSE(F::IsEyePicture(400, 240, 1));
}
