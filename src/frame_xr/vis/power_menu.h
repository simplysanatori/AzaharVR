// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiupowers: Kid Icarus: Uprising's powers (on land) and its special attack's dots (in
// the air) in Angel's Aim, over the off hand or in the view with the HUD (test switches, off by
// default; KIU_HOOKS.md "Powers"). What shows and when, where it goes, and the dots' image. No
// OpenXR calls (unit tested in src/tests/frame_xr/kiu_powers.cpp).
//
//   AZAHARFRAME_KIU_POWERS=1              the powers: the game's own icons, arrows and name
//                                         (video_core/kiu_powers.h) over what is behind them
//   AZAHARFRAME_KIU_POWERS_GOLD=0         without the gold piece behind the selected power
//   AZAHARFRAME_KIU_POWER_DOTS=1          the dots
//   AZAHARFRAME_KIU_POWERS_AT=hand        over the off hand: the powers for a while after one is
//                                         selected or used, the dots always (the default)
//                           |bar|right    with the HUD, always in a battle: under the health bar,
//                                         or at the top right (under the score)
//   AZAHARFRAME_KIU_POWERS_OPACITY=<%>    20 to 100
//   AZAHARFRAME_KIU_POWER_DOTS_OPACITY=<%>
//   AZAHARFRAME_KIU_POWERS_S=<seconds>    how long they stay over the hand (1.5)
//   AZAHARFRAME_KIU_POWERS_DEG=<degrees>  their width over the hand (13.6)
//   AZAHARFRAME_KIU_POWERS_UP=<metres>    their middle over the hand (0.11)

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "frame_xr/input/geometry.h"
#include "frame_xr/input/types.h"
#include "frame_xr/kiu_powers.h"
#include "video_core/kiu_hud.h"

namespace FrameXr::Vis::Powers {

enum class At : uint8_t { Hand, Bar, Right };

struct Settings {
    bool powers = false, dots = false;
    bool gold = true; ///< the selected power's gold piece behind it
    At at = At::Hand;
    float powers_opacity = 1.0f, dots_opacity = 1.0f;
    float stay_s = 1.5f;
    float width_deg = 13.6f;
    float up_m = 0.11f;
    bool operator==(const Settings&) const = default;
};
At AtFrom(const std::optional<std::string>& text);
/// 20 to 100 percent as a share; 1 for none or anything else.
float OpacityFrom(const std::optional<std::string>& text);
/// `text` within `low` to `high`, else `fallback`.
float NumberFrom(const std::optional<std::string>& text, float low, float high, float fallback);

// ---- What shows ------------------------------------------------------------------------------

inline constexpr float kFadeS = 0.25f;
/// The aiming ray within this of the powers over the hand: faded to kAimedShare, back this long after.
inline constexpr float kAimedDeg = 3.0f, kAimedShare = 0.15f, kAimedS = 0.4f, kAimedFadeS = 0.1f;
inline constexpr float kDotsShare = 0.45f, kNoDotShare = 0.30f, kDotChangedS = 1.0f;

struct Frame {
    Kiu::Powers::State game;
    bool battle = false;  ///< first person is engaged and the player plays (no menu, pause, cutscene)
    bool tracked = false; ///< the off hand's controller
    bool bottom_up = false; ///< the Bottom Screen, which has them too, is on view
    bool bottom_taken = false; ///< and it is more than the Persistent one: the dots go as well
    bool aimed = false;   ///< the aiming ray on the powers over the hand or the dots there
    float dt_s = 0.0f;
};
class Show {
public:
    struct Out {
        float powers = 0.0f, dots = 0.0f; ///< opacity
    };
    Out Update(const Settings& s, const Frame& f);

private:
    Kiu::Powers::State last_;
    float left_s_ = 0.0f;   ///< of the stay over the hand
    float dot_s_ = 0.0f;    ///< since a dot changed, counted down
    float aimed_s_ = 0.0f;  ///< after the aim left, counted down
    float aimed_ = 1.0f;    ///< the share now
    float shown_ = 0.0f;    ///< the powers' own fade
};

// ---- Over the hand ---------------------------------------------------------------------------

struct Place {
    Input::Posef pose{};
    float width = 0.0f, height = 0.0f; ///< metres
};
/// Upright over the hand (the room's up), facing the eye, as wide as `width_deg` is there (9 to 16
/// cm). `keep_width`: its width while it is up (0: not up), so a moving hand does not resize it.
Place OverHand(const Input::Posef& aim, const Input::Vec3f& eye, float up_m, float width_deg,
               float aspect, float keep_width = 0.0f);
/// The angle from `ray` to the nearest point of `p`, degrees (0 on it); 180 behind the ray.
float AngleTo(const Input::Posef& ray, const Place& p);

// ---- With the HUD ----------------------------------------------------------------------------

/// A rectangle on first person's screen, its own coordinates (-1 to 1).
struct Rect {
    float left = 0.0f, right = 0.0f, down = 0.0f, up = 0.0f;
};
bool Overlap(const Rect& a, const Rect& b);
/// A pixel of the bottom screen in the HUD is this many of the HUD's.
inline constexpr float kHudScale = 0.7f;
/// The HUD's bottom edges in its frame (desktop pictures), what is kept clear of the reticle (half,
/// degrees) and the gap.
inline constexpr float kBarBottom = 0.70f, kScoreBottom = 0.70f;
inline constexpr float kReticleDeg = 6.0f, kGap = 0.03f;
/// `wide`, `high`: in the bottom screen's pixels. `tan_x`, `tan_y`: the screen's half angles.
/// Under the bar or the score, its outer side in line with theirs; where that would cover another
/// piece, the reticle's middle or the subtitles, over it, else beside it.
Rect WithHud(At at, const VideoCore::KiuHud::Place& hud, float tan_x, float tan_y, float wide,
             float high);

// ---- The dots' image -------------------------------------------------------------------------

inline constexpr int kDotPx = 96, kDotGapPx = 15, kDotPad = 2, kDots = 3;
inline constexpr int kDotsWide = kDots * kDotPx + (kDots - 1) * kDotGapPx + 2 * kDotPad;
inline constexpr int kDotsHigh = kDotPx + 2 * kDotPad;
/// The size of one: over the hand in degrees, with the HUD in its pixels (3.2 degrees at HUD Size
/// 100 %).
inline constexpr float kDotDeg = 3.2f, kHudDotPx = 19.3f;
/// How many: the full ones and the one filling.
int DotsFor(float charge);
/// The image's part that has them: from the left.
int DotsWidthPx(int dots);
/// Premultiplied RGBA8 in linear light stored as sRGB, kDotsWide x kDotsHigh, top down.
void PaintDots(float charge, float opacity, std::vector<uint8_t>& rgba);
/// What is worth painting again for: the charge in steps of 2 %, the opacity in steps of 1/32.
uint32_t DotsKey(float charge, float opacity);

} // namespace FrameXr::Vis::Powers
