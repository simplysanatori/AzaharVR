// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiu140: Angel's Aim's Persistent Bottom Screen held by a grip: the hand's pointing
// carries it about the eyes and that hand's stick sizes it. No OpenXR calls here (unit tested in
// src/tests/frame_xr/kiu_panel_hold.cpp). Everything is in the head's frame (x right, y up, -z
// ahead): the panel goes with the head.
//
// The off hand's grip holds it from anywhere, so a hand that only closes must move nothing: the
// panel follows once the beam has turned kMoveDeg from where it was when the grip closed, and
// then from its place (no jump).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "frame_xr/input/geometry.h"
#include "frame_xr/vis/stats_place.h"

namespace FrameXr::Vis {

class PanelHold {
public:
    /// How far the beam turns before the panel follows it.
    static constexpr float kMoveDeg = 2.5f;
    /// The stick past this sizes it, at most kSizeRate (per second, as a natural logarithm).
    static constexpr float kStick = 0.5f;
    static constexpr float kSizeRate = 0.6f;
    static constexpr float kMinPercent = 50.0f, kMaxPercent = 200.0f;

    /// The grip closed: the panel's pose, the hand's beam, the panel's distance and its size.
    void Begin(const Input::Posef& panel, const Input::Ray& ray, float distance, float percent) {
        panel_ = panel;
        distance_ = distance;
        percent_ = std::clamp(percent, kMinPercent, kMaxPercent);
        start_ = Toward(ray);
        moved_ = sized_ = false;
    }

    /// Each frame held: the direction its middle goes in (its own until moved()).
    Input::Vec3f Update(const Input::Ray& ray, float stick_x, float stick_y, float dt) {
        const float s = std::abs(stick_x) >= std::abs(stick_y) ? stick_x : stick_y;
        if (std::abs(s) > kStick) {
            const float before = percent_;
            percent_ = std::clamp(percent_ * std::exp(s * kSizeRate * std::clamp(dt, 0.0f, 0.1f)),
                                  kMinPercent, kMaxPercent);
            sized_ = sized_ || percent_ != before;
        }
        const Input::Vec3f now = Toward(ray);
        if (!moved_) {
            const float c = std::clamp(Input::Dot(start_, now), -1.0f, 1.0f);
            if (std::acos(c) * 57.29578f < kMoveDeg) {
                return Input::Normalized(panel_.position);
            }
            moved_ = true;
            grab_.Begin({}, panel_, now * distance_);
        }
        return grab_.Direction({}, ray, distance_);
    }

    bool moved() const {
        return moved_;
    }
    bool sized() const {
        return sized_;
    }
    uint32_t SizePercent() const {
        return static_cast<uint32_t>(std::lround(percent_));
    }

private:
    /// The direction from the eyes to where the beam meets the panel's sphere.
    Input::Vec3f Toward(const Input::Ray& ray) const {
        const float b = Input::Dot(ray.origin, ray.direction);
        const float c = Input::Dot(ray.origin, ray.origin) - distance_ * distance_;
        const float disc = b * b - c;
        const float far = disc >= 0.0f ? -b + std::sqrt(disc) : -1.0f;
        return Input::Normalized(ray.origin + ray.direction * (far > 0.0f ? far : distance_));
    }

    Input::Posef panel_{};
    Input::Vec3f start_{0.0f, 0.0f, -1.0f};
    StatsGrab grab_;
    float distance_ = 0.8f;
    float percent_ = 100.0f;
    bool moved_ = false;
    bool sized_ = false;
};

} // namespace FrameXr::Vis
