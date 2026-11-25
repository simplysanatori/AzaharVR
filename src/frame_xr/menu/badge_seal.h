// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// The PokeDaisy badge's shape, our own drawing: a scalloped seal, as the Recommended check-mark's,
// with a band and a button cut out of it, in one colour. The VR menu (games_tab.cpp, PokeDaisySeal)
// and the desktop list (game_list.cpp, DrawPokeDaisySeal) both draw it from these, in parts of half
// its 20 dp box.

#pragma once

#include <cmath>
#include <utility>

namespace FrameXr::Menu::Seal {

constexpr int kLobes = 8;
constexpr float kBody = 0.84f;      ///< the disc's radius
constexpr float kLobe = 0.26f;      ///< a lobe's, on its rim
constexpr float kLobeAt = 0.74f;    ///< how far out its centre is
constexpr float kBand = 0.16f;      ///< the band's height, across the whole disc
constexpr float kButtonGap = 0.32f; ///< the cut round the button
constexpr float kButton = 0.15f;

/// A lobe's centre from the seal's.
inline std::pair<float, float> Lobe(int i) {
    const float a = 6.2831853f * (static_cast<float>(i) + 0.5f) / static_cast<float>(kLobes);
    return {kLobeAt * std::cos(a), kLobeAt * std::sin(a)};
}

} // namespace FrameXr::Menu::Seal
