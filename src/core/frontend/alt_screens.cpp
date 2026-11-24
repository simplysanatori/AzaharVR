// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cmath>
#include <mutex>
#include "core/frontend/alt_screens.h"

namespace Layout {

namespace {

std::mutex g_mutex;
std::optional<AltScreens> g_screens;

/// The largest `width` x `height` shape in the middle of `area`.
Common::Rectangle<u32> FitInside(const Common::Rectangle<u32>& area, u32 width, u32 height) {
    double scale = std::min(static_cast<double>(area.GetWidth()) / width,
                            static_cast<double>(area.GetHeight()) / height);
    if (Settings::values.use_integer_scaling.GetValue() && scale >= 1.0) {
        scale = std::floor(scale);
    }
    const u32 w = std::min(static_cast<u32>(std::lround(width * scale)), area.GetWidth());
    const u32 h = std::min(static_cast<u32>(std::lround(height * scale)), area.GetHeight());
    const u32 left = area.left + (area.GetWidth() - w) / 2;
    const u32 top = area.top + (area.GetHeight() - h) / 2;
    return {left, top, left + w, top + h};
}

} // Anonymous namespace

void SetAltScreens(std::optional<AltScreens> screens) {
    std::scoped_lock lock{g_mutex};
    g_screens = screens;
}

std::optional<AltScreens> GetAltScreens() {
    std::scoped_lock lock{g_mutex};
    return g_screens;
}

FramebufferLayout FitAltScreens(FramebufferLayout layout, const AltScreens& screens,
                                Settings::LayoutOption option) {
    const auto& v = Settings::values;
    const bool single = option == Settings::LayoutOption::SingleScreen ||
                        option == Settings::LayoutOption::SeparateWindows;
    const auto fit = [&](Common::Rectangle<u32>& rect, u32 width, u32 height, bool stretch) {
        if (single) {
            // These settings apply to a single screen only, as on the 3DS.
            if (stretch || v.aspect_ratio.GetValue() != Settings::AspectRatio::Default) {
                return;
            }
            rect = {0, 0, layout.width, layout.height};
        }
        if (!layout.is_rotated) { // upright: the screens lie on their side
            std::swap(width, height);
        }
        rect = FitInside(rect, width, height);
    };
    const auto fit_top = [&](Common::Rectangle<u32>& rect) {
        fit(rect, screens.top_width, screens.top_height, v.screen_top_stretch.GetValue());
    };
    const auto fit_bottom = [&](Common::Rectangle<u32>& rect) {
        if (screens.HasBottom()) {
            fit(rect, screens.bottom_width, screens.bottom_height,
                v.screen_bottom_stretch.GetValue());
        }
    };
    fit_top(layout.top_screen);
    fit_bottom(layout.bottom_screen);
    if (layout.additional_screen_enabled) {
        layout.additional_screen_is_bottom ? fit_bottom(layout.additional_screen)
                                           : fit_top(layout.additional_screen);
    }
    return layout;
}

} // namespace Layout
