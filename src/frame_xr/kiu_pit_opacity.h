// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiu133: Pit Opacity (vr_kiu_pit_opacity) in the views from outside Pit (Third
// Person, the Game's Camera). His draws are found once per model (and again when the view or
// the setting comes back): the world's draws are collected for a few VBlanks with his model
// drawn, then a few with his model's drawn byte cleared; his are those only in the first. Then
// the rasterizer blends those he is drawn opaque with with the chosen alpha
// (video_core/kiu_view_tap.h, PitDraws). KIU_HOOKS.md, "Pit Opacity".

#pragma once

#include <cstdint>
#include "frame_xr/input/kiu_hooks.h"

namespace FrameXr::Kiu::PitOpacity {

/// Each VBlank on the emulation thread, after the hide by draw: `model` his model node (0: none),
/// `outside` an outside view in effect while playing, `percent` the opacity wanted (20 to 100).
void OnVBlank(Input::KiuHooks::GameMemory& mem, uint32_t model, bool outside, uint32_t percent);

/// AZAHARFRAME_KIU_PIT_OPACITY=<percent> wins over the setting when set (20 to 100).
uint32_t PercentFor(const char* switch_text, uint32_t setting);

} // namespace FrameXr::Kiu::PitOpacity
