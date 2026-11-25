// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiupowers: power_menu.h.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <utility>

#include "frame_xr/vis/power_menu.h"
#include "video_core/kiu_powers.h"

namespace FrameXr::Vis::Powers {

namespace {

constexpr float kDeg = 3.14159265f / 180.0f;

float Toward(float now, float target, float step) {
    return now < target ? std::min(target, now + step) : std::max(target, now - step);
}

} // namespace

At AtFrom(const std::optional<std::string>& text) {
    return !text                                  ? At::Hand
           : *text == "bar"                       ? At::Bar
           : *text == "middle"                    ? At::Middle
           : *text == "score" || *text == "right" ? At::Score
                                                  : At::Hand;
}

float NumberFrom(const std::optional<std::string>& text, float low, float high, float fallback) {
    if (!text) {
        return fallback;
    }
    char* end = nullptr;
    const float v = std::strtof(text->c_str(), &end);
    return end != text->c_str() && *end == '\0' && v >= low && v <= high ? v : fallback;
}

float OpacityFrom(const std::optional<std::string>& text) {
    return NumberFrom(text, 20.0f, 100.0f, 100.0f) / 100.0f;
}

Settings SettingsFor(uint32_t menu, uint32_t opacity, const Switches& sw) {
    const bool on = menu >= static_cast<uint32_t>(Menu::OffHand) &&
                    menu <= static_cast<uint32_t>(Menu::Score);
    const auto share = [&](const std::optional<std::string>& text) {
        return text ? OpacityFrom(text)
                    : static_cast<float>(std::clamp(opacity, 20u, 100u)) / 100.0f;
    };
    Settings s;
    s.powers = sw.powers ? *sw.powers == "1" : on;
    s.dots = sw.dots ? *sw.dots == "1" : on;
    if (!s.powers && !s.dots) {
        return s;
    }
    s.gold = sw.gold != "0";
    s.at = sw.at                                            ? AtFrom(sw.at)
           : menu == static_cast<uint32_t>(Menu::HealthBar) ? At::Bar
           : menu == static_cast<uint32_t>(Menu::Middle)    ? At::Middle
           : menu == static_cast<uint32_t>(Menu::Score)     ? At::Score
                                                            : At::Hand;
    s.powers_opacity = share(sw.powers_opacity);
    s.dots_opacity = share(sw.dots_opacity);
    s.stay_s = NumberFrom(sw.stay_s, 0.5f, 30.0f, s.stay_s);
    s.width_deg = NumberFrom(sw.width_deg, 6.0f, 30.0f, s.width_deg);
    s.up_m = NumberFrom(sw.up_m, 0.0f, 0.3f, s.up_m);
    return s;
}

Show::Out Show::Update(const Settings& s, const Frame& f) {
    const float dt = std::clamp(f.dt_s, 0.0f, 0.1f);
    const bool hand = s.at == At::Hand;
    if (Kiu::Powers::Touched(last_, f.game)) {
        left_s_ = s.stay_s;
    } else {
        left_s_ = std::max(0.0f, left_s_ - dt);
    }
    if (Kiu::Powers::DotChanged(last_, f.game)) {
        dot_s_ = kDotChangedS;
    } else {
        dot_s_ = std::max(0.0f, dot_s_ - dt);
    }
    last_ = f.game;

    const bool here = f.game.known && f.battle && (!hand || f.tracked);
    // Not with the Bottom Screen on view where it would be twice: over the hand never, with the
    // HUD not beside the Persistent one.
    const bool twice = f.bottom_up && (hand || !f.bottom_taken);
    const bool powers = s.powers && here && !twice && !f.game.air && f.game.count != 0;
    if (!powers) {
        left_s_ = 0.0f;
        shown_ = 0.0f;
    } else if (!hand || left_s_ > 0.0f) {
        shown_ = 1.0f;
    } else {
        shown_ = Toward(shown_, 0.0f, dt / kFadeS);
    }
    aimed_s_ = hand && f.aimed ? kAimedS : std::max(0.0f, aimed_s_ - dt);
    aimed_ = Toward(aimed_, aimed_s_ > 0.0f ? kAimedShare : 1.0f, dt / kAimedFadeS);

    Out out;
    out.powers = shown_ * aimed_ * s.powers_opacity;
    if (s.dots && here && !(hand && f.bottom_taken) && f.game.air) {
        const float share = dot_s_ > 0.0f            ? 1.0f
                            : f.game.charge >= 1.0f ? kDotsShare
                                                    : kNoDotShare;
        out.dots = share * aimed_ * s.dots_opacity;
    }
    return out;
}

Input::Vec3f Billboard::Update(const Input::Vec3f& position, const Input::Vec3f& eye, float dt_s) {
    const Input::Vec3f to = eye - position;
    if (!(Input::Length(to) > 1e-3f)) {
        return held_ ? toward_ : Input::Vec3f{0.0f, 0.0f, 1.0f};
    }
    Input::Vec3f want = Input::Normalized(to);
    // Nearly under or over the eyes: the way it is turned is kept, so it does not spin.
    constexpr float kLeast = 0.2f;
    const float was = std::hypot(toward_.x, toward_.z);
    if (held_ && was > 0.01f && std::hypot(want.x, want.z) < kLeast) {
        want = Input::Normalized(
            Input::Vec3f{toward_.x / was * kLeast, want.y, toward_.z / was * kLeast});
    }
    if (!std::exchange(held_, true)) {
        return toward_ = want;
    }
    const float off = std::acos(std::clamp(Input::Dot(toward_, want), -1.0f, 1.0f)) / kDeg;
    if (off > 90.0f) { // turned away altogether
        return toward_ = want;
    }
    if (off > kLeashDeg) {
        const float share =
            (off - kLeashDeg) / off * (1.0f - std::exp(-std::max(dt_s, 0.0f) / kFollowS));
        toward_ = Input::Normalized(toward_ * (1.0f - share) + want * share);
    }
    return toward_;
}

Place OverHand(const Input::Posef& aim, const Input::Vec3f& eye, float up_m, float width_deg,
               float aspect, float keep_width, const std::optional<Input::Vec3f>& toward) {
    Place p;
    p.pose.position = aim.position + Input::Vec3f{0.0f, up_m, 0.0f};
    p.pose.orientation = Input::FaceViewer(
        p.pose.position, toward ? p.pose.position + *toward : eye, {0.0f, 1.0f, 0.0f});
    const float distance = Input::Length(eye - p.pose.position);
    p.width = keep_width > 0.0f
                  ? keep_width
                  : std::clamp(2.0f * std::tan(0.5f * width_deg * kDeg) * distance,
                               0.09f * width_deg / 13.6f, 0.16f * width_deg / 13.6f);
    p.height = p.width / aspect;
    return p;
}

float AngleTo(const Input::Posef& ray, const Place& p) {
    const Input::Vec3f dir = Input::Rotate(ray.orientation, {0.0f, 0.0f, -1.0f});
    const Input::Posef inv = Input::Inverse(p.pose);
    const Input::Vec3f from = Input::Transform(inv, ray.position);
    const Input::Vec3f along = Input::Rotate(inv.orientation, dir);
    // Where the ray meets the panel's plane, held to the panel; a ray that does not meet it: the
    // panel's middle.
    Input::Vec3f at{0.0f, 0.0f, 0.0f};
    if (std::abs(along.z) > 1e-4f && -from.z / along.z > 0.0f) {
        const float t = -from.z / along.z;
        at = {std::clamp(from.x + along.x * t, -0.5f * p.width, 0.5f * p.width),
              std::clamp(from.y + along.y * t, -0.5f * p.height, 0.5f * p.height), 0.0f};
    }
    const Input::Vec3f to = at - from;
    const float length = Input::Length(to);
    if (!(length > 1e-4f)) {
        return 0.0f;
    }
    return std::acos(std::clamp(Input::Dot(to, along) / length, -1.0f, 1.0f)) / kDeg;
}

bool Overlap(const Rect& a, const Rect& b) {
    return a.left < b.right && b.left < a.right && a.down < b.up && b.down < a.up;
}

Rect WithHud(At at, const VideoCore::KiuHud::Place& hud, float tan_x, float tan_y, float wide,
             float high, float row) {
    namespace KH = VideoCore::KiuHud;
    const float w = wide * kHudScale / 200.0f * hud.half_across;
    const float h = high * kHudScale / 120.0f * hud.half_up;
    const float line_h = row * kHudScale / 120.0f * hud.half_up;
    const auto piece = [](float across, float up, float half_across, float half_up, float left,
                          float right, float bottom) {
        return Rect{across + half_across * left, across + half_across * right,
                    up + half_up * bottom, up + half_up * 1.0f};
    };
    const Rect bar = piece(hud.bar_across, hud.bar_up, hud.half_across, hud.half_up, KH::kBarLeft,
                           KH::kBarRight, kBarBottom);
    const Rect score = piece(hud.score_across, hud.score_up, hud.half_across, hud.half_up,
                             KH::kScoreLeft, KH::kScoreRight, kScoreBottom);
    const float r = std::tan(kReticleDeg * kDeg);
    const Rect reticle{-r / tan_x, r / tan_x, -r / tan_y, r / tan_y};
    const Rect words{-hud.subtitle_half_across, hud.subtitle_half_across,
                     hud.subtitle_up - 0.2f * hud.subtitle_half_up,
                     hud.subtitle_up + 0.2f * hud.subtitle_half_up};
    const float gap = kGap * hud.half_up;
    const auto box = [&](float left, float down) { return Rect{left, left + w, down, down + h}; };
    const auto clear = [&](const Rect& p) {
        return !Overlap(p, bar) && !Overlap(p, score) && !Overlap(p, reticle) &&
               !Overlap(p, words) && p.left >= -1.0f && p.right <= 1.0f && p.down >= -1.0f &&
               p.up <= 1.0f;
    };
    const auto first = [&](const auto& places) {
        for (const Rect& p : places) {
            if (clear(p)) {
                return p;
            }
        }
        return places[0];
    };
    if (at == At::Middle) {
        const Rect& one = bar.left < score.left ? bar : score;
        const Rect& two = bar.left < score.left ? score : bar;
        const float left = 0.5f * (one.right + two.left) - 0.5f * w;
        const float level = 0.25f * (bar.down + bar.up + score.down + score.up) + 0.5f * line_h;
        const float low = std::min(bar.down, score.down), top = std::max(bar.up, score.up);
        return first(std::array<Rect, 4>{
            box(left, level - h),
            box(left, low - gap - h),
            box(left, top + gap),
            box(one.left - gap - w, level - h),
        });
    }
    const Rect& own = at == At::Bar ? bar : score;
    const float line = at == At::Bar ? own.left : own.right - w;
    const float other = at == At::Bar ? own.right - w : own.left;
    return first(std::array<Rect, 7>{
        box(line, own.down - gap - h),
        box(other, own.down - gap - h),
        box(line, own.up + gap),
        box(other, own.up + gap),
        box(own.right + gap, own.up - h),
        box(own.left - gap - w, own.up - h),
        box(line, own.down - 2.0f * gap - h - (own.up - own.down)),
    });
}

Rect Moved(Rect r, float tan_x, float tan_y, float across_deg, float height_deg) {
    const auto shift = [](float low, float high, float t, float deg) {
        if (deg == 0.0f) {
            return 0.0f;
        }
        const float at = 0.5f * (low + high), half = 0.5f * (high - low);
        const float a = std::clamp(std::atan(at * t) + deg * kDeg, -1.2f, 1.2f);
        const float most = std::max(1.0f - half, 0.0f);
        return std::clamp(std::tan(a) / t, -most, most) - at;
    };
    const float x = shift(r.left, r.right, tan_x, across_deg);
    const float y = shift(r.down, r.up, tan_y, height_deg);
    return {r.left + x, r.right + x, r.down + y, r.up + y};
}

Place OnScreen(const Rect& r, const Input::Posef& screen, float half_x, float half_y) {
    return {{screen.orientation,
             Input::Transform(screen, {0.5f * (r.left + r.right) * half_x,
                                       0.5f * (r.down + r.up) * half_y, 0.0f})},
            (r.right - r.left) * half_x, (r.up - r.down) * half_y};
}

float HudGap(const Settings& s) {
    namespace KH = VideoCore::KiuHud;
    if ((!s.powers && !s.dots) || s.at != At::Middle) {
        return 0.0f;
    }
    return static_cast<float>(VideoCore::KiuPowers::kWide) * kHudScale / 200.0f + 0.06f -
           KH::kLinkGap;
}

int DotsFor(float charge) {
    return std::clamp(static_cast<int>(charge) + 1, 1, kDots);
}

int DotsWidthPx(int dots) {
    return dots * kDotPx + (dots - 1) * kDotGapPx + 2 * kDotPad;
}

uint32_t DotsKey(float charge, float opacity) {
    return static_cast<uint32_t>(std::clamp(charge, 0.0f, 3.0f) * 50.0f) * 64u +
           static_cast<uint32_t>(std::clamp(opacity, 0.0f, 1.0f) * 32.0f + 0.5f);
}

void PaintDots(float charge, float opacity, std::vector<uint8_t>& rgba) {
    rgba.assign(static_cast<std::size_t>(kDotsWide) * kDotsHigh * 4, 0);
    const auto linear = [](float c) {
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    };
    const auto srgb = [](float c) {
        return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    };
    struct Color {
        float r, g, b, a;
    };
    const auto color = [&](uint32_t rgb, float a) {
        return Color{linear(static_cast<float>(rgb >> 16 & 0xFF) / 255.0f),
                     linear(static_cast<float>(rgb >> 8 & 0xFF) / 255.0f),
                     linear(static_cast<float>(rgb & 0xFF) / 255.0f), a};
    };
    const Color back = color(0x10202A, 0.60f), dark = color(0x061018, 1.0f),
                ring = color(0xF2F9FF, 1.0f), full = color(0x1E86C8, 1.0f),
                shine = color(0x8FD6FF, 1.0f), filling = color(0xBFE6F5, 1.0f);
    // A dot of radius 121 in kDotPx: a light ring 7 wide on a dark one 20 wide, so one of them
    // stands out on whatever is behind.
    constexpr float kRing = 111.0f, kDisc = 104.0f;
    constexpr float kUnit = 246.0f / static_cast<float>(kDotPx);
    const int dots = DotsFor(charge);
    for (int d = 0; d < dots; ++d) {
        const float x0 = static_cast<float>(kDotPad + d * (kDotPx + kDotGapPx));
        const float fill = std::clamp(charge - static_cast<float>(d), 0.0f, 1.0f);
        const bool is_full = fill >= 1.0f;
        const float level = kDisc - 2.0f * kDisc * fill; // down positive
        for (int py = 0; py < kDotPx; ++py) {
            for (int px = 0; px < kDotPx; ++px) {
                const float x = (static_cast<float>(px) + 0.5f) * kUnit - 123.0f;
                const float y = (static_cast<float>(py) + 0.5f) * kUnit - 123.0f;
                const float r = std::sqrt(x * x + y * y);
                // Each share of the pixel inside, from the distance to the edge.
                const auto inside = [](float distance) {
                    return std::clamp(0.5f - distance / kUnit, 0.0f, 1.0f);
                };
                Color c{0, 0, 0, 0}; // premultiplied
                const auto over = [&c](const Color& s, float share) {
                    const float a = s.a * share;
                    c = {s.r * a + c.r * (1 - a), s.g * a + c.g * (1 - a), s.b * a + c.b * (1 - a),
                         a + c.a * (1 - a)};
                };
                over(back, inside(r - kRing));
                if (is_full) {
                    over(full, inside(r - kDisc));
                    const float e = std::sqrt((x + 22) * (x + 22) / (44.0f * 44.0f) +
                                              (y + 48) * (y + 48) / (20.0f * 20.0f));
                    over(shine, inside((e - 1.0f) * 28.0f));
                } else if (fill > 0.0f) {
                    over(filling, inside(r - kDisc) * inside(level - y));
                }
                over(dark, inside(std::abs(r - kRing) - 10.0f));
                over(ring, inside(std::abs(r - kRing) - 3.5f));
                uint8_t* out = rgba.data() + 4 * ((kDotPad + py) * kDotsWide +
                                                  static_cast<int>(x0) + px);
                const auto byte = [](float v) {
                    return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
                };
                out[0] = byte(srgb(c.r * opacity));
                out[1] = byte(srgb(c.g * opacity));
                out[2] = byte(srgb(c.b * opacity));
                out[3] = byte(c.a * opacity);
            }
        }
    }
}

} // namespace FrameXr::Vis::Powers
