// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiupowers: Kid Icarus: Uprising's powers (on land) and its special attack's dots (in
// the air) in Angel's Aim, over the off hand or in the view with the HUD (vr_kiu_power_menu and its
// opacity, off by default; KIU_HOOKS.md "Powers"). What shows and when, where it goes, and the dots'
// image. No OpenXR calls (unit tested in src/tests/frame_xr/kiu_powers.cpp).
//
// Test switches; one that is set wins over the settings:
//
//   AZAHARFRAME_KIU_POWERS=1              the powers: the game's own icons, arrows and name
//                                         (video_core/kiu_powers.h) over what is behind them
//   AZAHARFRAME_KIU_POWERS_GOLD=0         without the gold piece behind the selected power
//   AZAHARFRAME_KIU_POWER_DOTS=1          the dots
//   AZAHARFRAME_KIU_POWERS_AT=hand        over the off hand: the powers for a while after another is
//                                         selected, the dots always (the default)
//                           |bar|middle|score   with the HUD, always in a battle: under the health
//                                         bar, between it and the score, under the score
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

enum class At : uint8_t { Hand, Bar, Middle, Score };

struct Settings {
    bool powers = false, dots = false;
    bool gold = true; ///< the selected power's gold piece behind it
    At at = At::Hand;
    float powers_opacity = 1.0f, dots_opacity = 1.0f;
    float stay_s = 1.5f;
    float width_deg = 13.6f;
    float up_m = 0.11f;
    float across_deg = 0.0f, height_deg = 0.0f; ///< with the HUD: from the place, right and up
    bool operator==(const Settings&) const = default;
};
At AtFrom(const std::optional<std::string>& text);
/// 20 to 100 percent as a share; 1 for none or anything else.
float OpacityFrom(const std::optional<std::string>& text);
/// `text` within `low` to `high`, else `fallback`.
float NumberFrom(const std::optional<std::string>& text, float low, float high, float fallback);

/// vr_kiu_power_menu's values.
enum class Menu : uint32_t { Off, OffHand, HealthBar, Middle, Score };
/// The test switches as they are set.
struct Switches {
    std::optional<std::string> powers, dots, gold, at, powers_opacity, dots_opacity, stay_s,
        width_deg, up_m;
};
/// From the settings (`menu`, `opacity` in percent) and the switches: not Off is the powers and the
/// dots, there and with that opacity; a switch that is set wins.
Settings SettingsFor(uint32_t menu, uint32_t opacity, const Switches& sw);

// ---- What shows ------------------------------------------------------------------------------

inline constexpr float kFadeS = 0.25f;
/// The aiming ray within this of the powers over the hand: faded to kAimedShare, back this long after.
inline constexpr float kAimedDeg = 3.0f, kAimedShare = 0.15f, kAimedS = 0.4f, kAimedFadeS = 0.1f;
/// The dots' opacity with a full one, with none, and how long in full after one changed.
inline constexpr float kDotsShare = 0.80f, kNoDotShare = 0.65f, kDotChangedS = 1.0f;

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
/// Which way a card over the hand faces: to the eyes, upright whatever the hand does. It follows
/// them once they are kLeashDeg off, so small head motion does not move it.
inline constexpr float kLeashDeg = 2.0f, kFollowS = 0.08f;
class Billboard {
public:
    /// The direction from `position` to `eye`, as held; the first after Reset at once.
    Input::Vec3f Update(const Input::Vec3f& position, const Input::Vec3f& eye, float dt_s);
    void Reset() {
        held_ = false;
    }

private:
    Input::Vec3f toward_{};
    bool held_ = false;
};
/// Upright over the hand (the room's up), facing the eye, as wide as `width_deg` is there (9 to 16
/// cm). `keep_width`: its width while it is up (0: not up), so a moving hand does not resize it.
/// `toward`: which way it faces (Billboard's; none: straight to the eye).
Place OverHand(const Input::Posef& aim, const Input::Vec3f& eye, float up_m, float width_deg,
               float aspect, float keep_width = 0.0f,
               const std::optional<Input::Vec3f>& toward = std::nullopt);
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
inline constexpr float kBarBottom = 0.72f, kScoreBottom = 0.72f;
inline constexpr float kReticleDeg = 6.0f, kGap = 0.02f;
/// `wide`, `high`: in the bottom screen's pixels, `row` of it from the top the part that counts for
/// the middle's height. `tan_x`, `tan_y`: the screen's half angles.
/// Just under the bar or the score, its outer side in line with theirs, or between the two, the row
/// level with them (no room there: under both); where that would cover another piece, the reticle's
/// middle or the subtitles, over it, else beside it.
Rect WithHud(At at, const VideoCore::KiuHud::Place& hud, float tan_x, float tan_y, float wide,
             float high, float row);
/// `r` moved by these degrees to the right and up, held to the screen.
Rect Moved(Rect r, float tan_x, float tan_y, float across_deg, float height_deg);
/// AzaharFrame kiuhs3a: `r` in the plane of the screen at `screen` with these half sizes (metres).
Place OnScreen(const Rect& r, const Input::Posef& screen, float half_x, float half_y);
/// The gap the bar and the score need between them, beside their own, for the powers in the middle
/// of the linked HUD (KiuHud::Wanted::link_gap; 0 for the other places).
float HudGap(const Settings& s);

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
