// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Internal Resolution for GBA and Game Boy pictures (video_core/renderer_vulkan/alt_present.h):
// whole times, the setting or less, no more than the screen shows, held near the halfway points,
// never past the size limit; which Texture Filters are drawn over them; and how big the headset
// shows a screen (video_core/xr_screen_cover.h).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "video_core/renderer_vulkan/alt_present.h"
#include "video_core/xr_screen_cover.h"

using Vulkan::AltPictureScale;

namespace {

constexpr u32 kGbaW = 240, kGbaH = 160, kGbW = 160, kGbH = 144;

} // namespace

TEST_CASE("Picture scale: the setting while the screen's size is not known", "[alt_core]") {
    for (u32 setting = 1; setting <= 18; setting++) {
        INFO("setting " << setting);
        CHECK(AltPictureScale(setting, kGbaW, kGbaH, 0.0f, 0) == std::min(setting, 4u));
        CHECK(AltPictureScale(setting, kGbW, kGbH, 0.0f, 0) == std::min(setting, 4u));
    }
    CHECK(AltPictureScale(0, kGbaW, kGbaH, 0.0f, 0) == Vulkan::kAltUnknownScale);
    // What was drawn before is no reason to stay
    CHECK(AltPictureScale(2, kGbaW, kGbaH, 0.0f, 8) == 2);
}

TEST_CASE("Picture scale: the setting, no more than the screen shows", "[alt_core]") {
    // A pixel shown 2.8 wide: 3x and no more
    for (u32 setting = 1; setting <= 18; setting++) {
        INFO("setting " << setting);
        CHECK(AltPictureScale(setting, kGbaW, kGbaH, 2.8f, 0) == std::min(setting, 3u));
    }
    // A screen big enough shows them all, to the highest
    for (u32 setting = 1; setting <= 18; setting++) {
        INFO("setting " << setting);
        CHECK(AltPictureScale(setting, kGbaW, kGbaH, 20.0f, 0) == std::min(setting, 8u));
        CHECK(AltPictureScale(setting, kGbW, kGbH, 20.0f, 0) == std::min(setting, 8u));
    }
    // Auto: all the screen shows
    CHECK(AltPictureScale(0, kGbaW, kGbaH, 0.3f, 0) == 1);
    CHECK(AltPictureScale(0, kGbaW, kGbaH, 1.4f, 0) == 1);
    CHECK(AltPictureScale(0, kGbaW, kGbaH, 1.6f, 0) == 2);
    CHECK(AltPictureScale(0, kGbaW, kGbaH, 2.8f, 0) == 3);
    CHECK(AltPictureScale(0, kGbaW, kGbaH, 5.2f, 0) == 5);
    CHECK(AltPictureScale(0, kGbaW, kGbaH, 7.6f, 0) == 8);
    CHECK(AltPictureScale(0, kGbaW, kGbaH, 20.0f, 0) == 8);
}

TEST_CASE("Picture scale: held near the halfway points", "[alt_core]") {
    // From 3x: up past 3.65, down under 2.35
    CHECK(AltPictureScale(8, kGbaW, kGbaH, 3.45f, 3) == 3);
    CHECK(AltPictureScale(8, kGbaW, kGbaH, 3.6f, 3) == 3);
    CHECK(AltPictureScale(8, kGbaW, kGbaH, 3.7f, 3) == 4);
    CHECK(AltPictureScale(8, kGbaW, kGbaH, 2.4f, 3) == 3);
    CHECK(AltPictureScale(8, kGbaW, kGbaH, 2.3f, 3) == 2);
    // A screen that sways around a halfway point changes the scale once, not each time
    u32 scale = 0, changes = 0;
    for (int i = 0; i < 200; i++) {
        const u32 next = AltPictureScale(0, kGbaW, kGbaH, i % 2 ? 3.55f : 3.45f, scale);
        changes += next != scale;
        scale = next;
    }
    CHECK(changes == 1);
    // The setting still wins over what is held
    CHECK(AltPictureScale(2, kGbaW, kGbaH, 3.6f, 3) == 2);
    CHECK(AltPictureScale(1, kGbaW, kGbaH, 8.0f, 8) == 1);
    // and a far jump is followed at once
    CHECK(AltPictureScale(8, kGbaW, kGbaH, 7.9f, 3) == 8);
    CHECK(AltPictureScale(8, kGbaW, kGbaH, 0.5f, 6) == 1);
}

TEST_CASE("Picture scale: never past the size limit", "[alt_core]") {
    CHECK(AltPictureScale(8, 400, 240, 20.0f, 0) == 8); // the limit itself
    CHECK(AltPictureScale(8, 320, 240, 20.0f, 0) == 8); // PokeDaisy's screen
    CHECK(AltPictureScale(8, 256, 384, 20.0f, 0) == 5);
    CHECK(AltPictureScale(8, 1024, 768, 20.0f, 0) == 2);
    CHECK(AltPictureScale(8, 1024, 768, 0.0f, 0) == 2);
    CHECK(AltPictureScale(8, 4000, 3000, 20.0f, 0) == 1);
    CHECK(AltPictureScale(0, 0, 0, 0.0f, 0) == 4); // no picture: no division by zero
    for (u32 setting = 0; setting <= 18; setting++) {
        for (const float shown : {0.0f, 0.2f, 1.0f, 2.5f, 4.5f, 9.0f, 100.0f}) {
            for (u32 last = 0; last <= 9; last++) {
                const u32 scale = AltPictureScale(setting, kGbaW, kGbaH, shown, last);
                CHECK(scale >= 1);
                CHECK(scale <= Vulkan::kAltMaxScale);
                if (setting > 0) {
                    CHECK(scale <= setting);
                }
                CHECK(kGbaW * scale <= Vulkan::kAltMaxWidth);
                CHECK(kGbaH * scale <= Vulkan::kAltMaxHeight);
            }
        }
    }
}

TEST_CASE("Screen cover: display pixels across a screen", "[alt_core]") {
    namespace C = VideoCore::XrScreenCover;
    // 1.5 m wide at 2 m, an eye image 1728 wide over tan 2.4: 540 pixels
    CHECK(C::Across(1.5f, 2.0f, 2.4f, 1728.0f) == Catch::Approx(540.0f));
    // twice as near or as wide: twice as many
    CHECK(C::Across(1.5f, 1.0f, 2.4f, 1728.0f) == Catch::Approx(1080.0f));
    CHECK(C::Across(3.0f, 2.0f, 2.4f, 1728.0f) == Catch::Approx(1080.0f));
    // Not known
    CHECK(C::Across(0.0f, 2.0f, 2.4f, 1728.0f) == 0.0f);
    CHECK(C::Across(1.5f, 0.0f, 2.4f, 1728.0f) == 0.0f);
    CHECK(C::Across(1.5f, 2.0f, 0.0f, 1728.0f) == 0.0f);
    CHECK(C::Across(1.5f, 2.0f, 2.4f, 0.0f) == 0.0f);
    // The last known stays
    C::Note(VideoCore::XrScreen::Bottom, 300.0f);
    C::Note(VideoCore::XrScreen::Bottom, 0.0f);
    CHECK(C::Get(VideoCore::XrScreen::Bottom) == 300.0f);
    C::Note(VideoCore::XrScreen::Bottom, 450.0f);
    CHECK(C::Get(VideoCore::XrScreen::Bottom) == 450.0f);
}

TEST_CASE("Picture filter: every filter but None, over a bigger picture", "[alt_core]") {
    using Settings::TextureFilter;
    for (const TextureFilter filter : {TextureFilter::Anime4K, TextureFilter::Bicubic,
                                       TextureFilter::ScaleForce, TextureFilter::xBRZ,
                                       TextureFilter::MMPX}) {
        CHECK(!Vulkan::AltPictureFiltered(filter, 0));
        CHECK(!Vulkan::AltPictureFiltered(filter, 1)); // once as big: no pixels to make
        for (u32 scale = 2; scale <= Vulkan::kAltMaxScale; scale++) {
            CHECK(Vulkan::AltPictureFiltered(filter, scale));
        }
    }
    for (u32 scale = 0; scale <= Vulkan::kAltMaxScale; scale++) {
        CHECK(!Vulkan::AltPictureFiltered(TextureFilter::NoFilter, scale));
        CHECK(!Vulkan::AltPictureFiltered(static_cast<TextureFilter>(6), scale)); // none such
    }
}
