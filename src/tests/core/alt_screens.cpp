// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include "core/frontend/alt_screens.h"
#include "core/frontend/emu_window.h"

// AzaharVR: DS, GBA and Game Boy screens in the window (core/frontend/alt_screens.h).

namespace {

using Settings::LayoutOption;

class Window final : public Frontend::EmuWindow {
public:
    void PollEvents() override {}
    const Layout::FramebufferLayout& LayoutAt(u32 width, u32 height) {
        UpdateCurrentFramebufferLayout(width, height);
        return GetFramebufferLayout();
    }
};

constexpr Layout::AltScreens kDs{256, 192, 256, 192};
constexpr Layout::AltScreens kGba{240, 160, 0, 0};

/// The settings a test changes, back as they were.
struct Restore {
    ~Restore() {
        auto& v = Settings::values;
        v.layout_option.SetValue(LayoutOption::Default);
        v.swap_screen.SetValue(false);
        v.upright_screen.SetValue(false);
        v.use_integer_scaling.SetValue(false);
        v.aspect_ratio.SetValue(Settings::AspectRatio::Default);
        v.screen_top_stretch.SetValue(false);
        Layout::SetAltScreens(std::nullopt);
    }
};

/// `rect` is `width` x `height`'s shape, to the pixel.
bool HasShape(const Common::Rectangle<u32>& rect, double width, double height) {
    const double w = rect.GetWidth();
    const double h = rect.GetHeight();
    return std::abs(w - h * width / height) <= 2.0;
}

bool Inside(const Common::Rectangle<u32>& inner, const Common::Rectangle<u32>& outer) {
    return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right &&
           inner.bottom <= outer.bottom;
}

} // namespace

TEST_CASE("DS screens keep their shape inside the 3DS's", "[core][alt_screens]") {
    Restore restore;
    Window window;
    for (const auto option : {LayoutOption::Default, LayoutOption::LargeScreen,
                              LayoutOption::SideScreen, LayoutOption::HybridScreen,
                              LayoutOption::CustomLayout}) {
        for (const bool swap : {false, true}) {
            for (const bool upright : {false, true}) {
                Settings::values.layout_option.SetValue(option);
                Settings::values.swap_screen.SetValue(swap);
                Settings::values.upright_screen.SetValue(upright);
                Layout::SetAltScreens(std::nullopt);
                const auto ds3 = window.LayoutAt(1603, 977);
                Layout::SetAltScreens(kDs);
                const auto ds = window.LayoutAt(1603, 977);
                INFO("layout " << static_cast<int>(option) << " swap " << swap << " upright "
                               << upright);
                // Custom Layout is never upright.
                const bool turned = !ds.is_rotated;
                const double w = turned ? 3.0 : 4.0;
                const double h = turned ? 4.0 : 3.0;
                CHECK(HasShape(ds.top_screen, w, h));
                CHECK(HasShape(ds.bottom_screen, w, h));
                CHECK(Inside(ds.top_screen, ds3.top_screen));
                CHECK(Inside(ds.bottom_screen, ds3.bottom_screen));
                // As large as fits: it touches two sides of its 3DS screen's rectangle.
                CHECK((ds.top_screen.GetWidth() == ds3.top_screen.GetWidth() ||
                       ds.top_screen.GetHeight() == ds3.top_screen.GetHeight()));
                CHECK(ds.top_screen_enabled == ds3.top_screen_enabled);
                CHECK(ds.bottom_screen_enabled == ds3.bottom_screen_enabled);
                CHECK(ds.additional_screen_enabled == ds3.additional_screen_enabled);
                if (ds.additional_screen_enabled) {
                    CHECK(HasShape(ds.additional_screen, w, h));
                    CHECK(Inside(ds.additional_screen, ds3.additional_screen));
                }
            }
        }
    }
}

TEST_CASE("Single Screen and one-screen games use the whole window", "[core][alt_screens]") {
    Restore restore;
    Window window;
    Settings::values.layout_option.SetValue(LayoutOption::SingleScreen);
    Layout::SetAltScreens(kDs);
    auto layout = window.LayoutAt(1600, 900);
    CHECK(layout.top_screen == Common::Rectangle<u32>{200, 0, 1400, 900});
    CHECK(layout.top_screen_enabled);
    CHECK_FALSE(layout.bottom_screen_enabled);
    Settings::values.swap_screen.SetValue(true);
    layout = window.LayoutAt(1600, 900);
    CHECK(layout.bottom_screen == Common::Rectangle<u32>{200, 0, 1400, 900});
    CHECK_FALSE(layout.top_screen_enabled);

    // Whatever the layout is, and however it is swapped.
    Layout::SetAltScreens(kGba);
    for (const auto option :
         {LayoutOption::Default, LayoutOption::SingleScreen, LayoutOption::LargeScreen,
          LayoutOption::SideScreen, LayoutOption::HybridScreen, LayoutOption::CustomLayout,
          LayoutOption::SeparateWindows}) {
        Settings::values.layout_option.SetValue(option);
        layout = window.LayoutAt(1600, 900);
        INFO("layout " << static_cast<int>(option));
        CHECK(layout.top_screen == Common::Rectangle<u32>{125, 0, 1475, 900});
        CHECK(layout.top_screen_enabled);
        CHECK_FALSE(layout.bottom_screen_enabled);
        CHECK_FALSE(layout.additional_screen_enabled);
    }
}

TEST_CASE("DS screens: stretch and integer scaling", "[core][alt_screens]") {
    Restore restore;
    Window window;
    Layout::SetAltScreens(kDs);
    Settings::values.layout_option.SetValue(LayoutOption::SingleScreen);
    Settings::values.aspect_ratio.SetValue(Settings::AspectRatio::Stretch);
    CHECK(window.LayoutAt(1600, 900).top_screen == Common::Rectangle<u32>{0, 0, 1600, 900});

    Settings::values.aspect_ratio.SetValue(Settings::AspectRatio::Default);
    Settings::values.use_integer_scaling.SetValue(true);
    // 900 / 192 = 4.69: four times
    CHECK(window.LayoutAt(1600, 900).top_screen == Common::Rectangle<u32>{288, 66, 1312, 834});
}

TEST_CASE("3DS layouts do not change without alt screens", "[core][alt_screens]") {
    Restore restore;
    Window window;
    Layout::SetAltScreens(std::nullopt);
    const auto layout = window.LayoutAt(800, 960);
    CHECK(layout.top_screen == Common::Rectangle<u32>{0, 0, 800, 480});
    CHECK(layout.bottom_screen == Common::Rectangle<u32>{80, 480, 720, 960});
}
