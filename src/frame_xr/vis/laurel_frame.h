// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuhand: the laurel frame: a thin gold laurel wreath of our own round a 4:3 panel
// (the wrist screen, wrist_screen.h), as if Pit wore the screen on his vambrace (pit_hand.h).
// Made by tools/kiu_hand (laurel.py; gen.py writes laurel_frame_data.inc): no game data in it.
// No OpenXR or Vulkan here (unit tested in src/tests/frame_xr/pit_hand.cpp).
//
//   Space   The panel's: the panel 1 wide and 0.75 high, centred on the origin in the x-y
//           plane, +y its top, +z its front. Scale everything by the panel's width in metres.
//   Wreath  Two branches from under the panel's middle, out along its lower edge, up round
//           its sides and in over its top, their tips not quite meeting, tied where they
//           cross. Every leaf is clear of the panel (kLaurelClear at least) and stands no
//           more than kLaurelRelief off the plate (3.5 mm on a 10 cm panel).
//   Plate   A leather plate under the panel and the wreath, its face just behind the panel's
//           plane, its back at kLaurelBack.
//   Worn    The plate rides on the vambrace's two bands (pit_hand.h PitHandPanelSeat: the
//           origin kLaurelBack above the seat's centre along its normal, the panel's x along
//           the forearm), in the forearm bone's frame so it turns with the vambrace.
//           LaurelFrameWorn adds two leather straps from under the plate round the forearm,
//           one over each band, as if buckled on: for a section of the given half thickness
//           and half width, the straps the given distance either side of the middle.
//
// Vertices are 12 floats (model_file.h: position, normal, tangent, uv); the uv is into Pit's
// hand's palette (pit_hand.h PitHandPalette: the Gold and Strap rows), so one texture draws
// the hand, the wreath and the straps.

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "frame_xr/vis/pit_hand.h"

namespace FrameXr::Vis {

inline constexpr float kLaurelPanelWidth = 1.0f;
inline constexpr float kLaurelPanelHeight = 0.75f;
/// The least distance from any part of the wreath to the panel, in the panel's plane.
inline constexpr float kLaurelClear = 0.012f;
/// How far the wreath stands in front of the panel's plane at most.
inline constexpr float kLaurelRelief = 0.036f;
/// The plate's back: behind the panel's plane.
inline constexpr float kLaurelBack = -0.040f;

struct LaurelPart {
    std::string_view name; ///< laurel, plate, strap
    PitHandMaterial material{};
    uint32_t first_index = 0;
    uint32_t index_count = 0;
};
struct LaurelMesh {
    std::vector<float> vertices; ///< kPitVertexFloats each
    std::vector<uint32_t> indices;
    std::vector<LaurelPart> parts;
};

/// The wreath and the plate.
const LaurelMesh& LaurelFrame();
/// The same with the two straps round a forearm whose section under the plate is an ellipse
/// `arm_half_thickness` (along the panel's z) by `arm_half_width` (along its y), the straps'
/// middles `strap_offset` either side of the panel's middle along x: all in panel widths (a
/// PitHandSeat's arm_radius, arm_half_width and band_offset times kPitHandUnit over the
/// panel's width in metres: for the 10 cm wrist screen about 0.41, 0.46 and 0.54). The offset
/// is kept inside the plate.
LaurelMesh LaurelFrameWorn(float arm_half_thickness, float arm_half_width, float strap_offset);

} // namespace FrameXr::Vis
