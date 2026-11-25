// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiupowers: power_layers.h.

#include <algorithm>
#include <cmath>

#include "common/logging/log.h"
#include "frame_xr/kiu_powers_live.h"
#include "frame_xr/power_layers.h"
#include "frame_xr/session.h"
#include "video_core/kiu_powers.h"

namespace FrameXr {

namespace {

namespace P = Vis::Powers;
namespace KP = VideoCore::KiuPowers;

Input::Posef ToInput(const XrPosef& p) {
    return {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
            {p.position.x, p.position.y, p.position.z}};
}
XrPosef ToXr(const Input::Posef& p) {
    return {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
            {p.position.x, p.position.y, p.position.z}};
}

/// With the HUD: seen this far away (the HUD is at the far screen's depth).
constexpr float kHudDistance = 8.0f;

/// First person's screen, as a head sees it.
struct Screen {
    Input::Posef eye[2]{}; ///< its pose for each eye, LOCAL
    Input::Vec3f head{};   ///< between the eyes
    float half_x = 0.0f, half_y = 0.0f; ///< metres
    float tan_x = 1.0f, tan_y = 1.0f;

    /// What is at these screen coordinates, seen from between the eyes.
    P::Place At(const P::Rect& r) const {
        const Input::Vec3f at{0.5f * (r.left + r.right) * half_x, 0.5f * (r.down + r.up) * half_y,
                              0.0f};
        const Input::Vec3f mid =
            (Input::Transform(eye[0], at) + Input::Transform(eye[1], at)) * 0.5f;
        const Input::Vec3f to = mid - head;
        const float d = Input::Length(to);
        const float k = d > 0.01f ? std::max(1.0f, kHudDistance / d) : 1.0f;
        P::Place p;
        p.pose = {eye[0].orientation, head + to * k};
        p.width = (r.right - r.left) * half_x * k;
        p.height = (r.up - r.down) * half_y * k;
        return p;
    }
};

Screen ScreenFrom(const PowerLayers::In& in) {
    Screen s;
    const Input::Posef head = ToInput(in.head.head);
    Input::Vec3f eyes[2];
    for (int e = 0; e < 2; ++e) {
        const Input::Posef p = ToInput(in.top_pose[e]);
        s.eye[e] = in.top_in_view ? Input::Posef{Input::Multiply(head.orientation, p.orientation),
                                                 Input::Transform(head, p.position)}
                                  : p;
        eyes[e] = in.head.views_valid ? ToInput(in.head.eye[e]).position : head.position;
    }
    s.head = (eyes[0] + eyes[1]) * 0.5f;
    s.half_x = 0.5f * in.top_size.width;
    s.half_y = 0.5f * in.top_size.height;
    const Input::Vec3f front = Input::Rotate(s.eye[0].orientation, {0.0f, 0.0f, 1.0f});
    const float d = std::abs(Input::Dot(s.eye[0].position - eyes[0], front));
    s.tan_x = s.half_x / std::max(d, 0.01f);
    s.tan_y = s.half_y / std::max(d, 0.01f);
    return s;
}

} // namespace

void PowerLayers::Destroy() {
    dots_chain_.Destroy();
    dots_failed_ = false;
    dots_key_ = UINT32_MAX;
    KP::g_alpha.store(0.0f, std::memory_order_relaxed);
}

std::vector<Quad> PowerLayers::Update(Runtime& rt, const In& in) {
    struct {
        Quad powers, dots;
    } out;
    const P::Settings settings = Kiu::Powers::SettingsNow();
    const float dt =
        time_ != 0 ? static_cast<float>(std::min<XrTime>(in.time - time_, 100'000'000)) * 1e-9f : 0;
    time_ = in.time;

    P::Frame f;
    bool playing = false;
    f.game = Kiu::Powers::Game(&playing);
    const VideoCore::KiuHud::Place hud_now = VideoCore::KiuHud::g_live.Get();
    f.battle = in.playing && playing && !f.game.paused && in.head.valid;
    const Kiu::Powers::Hands hands = Kiu::Powers::HandsNow();
    f.tracked = hands.tracked;
    f.bottom_up = in.bottom_shown;
    f.bottom_taken = in.bottom_shown && !in.bottom_persistent;
    f.aimed = aimed_;
    f.dt_s = dt;
    const P::Show::Out show = show_.Update(settings, f);
    aimed_ = false;

    if (settings.dots && !dots_chain_.Valid() && !dots_failed_ && f.game.known) {
        dots_failed_ = !dots_chain_.Create(rt.session, rt.vulkan->Device(),
                                           rt.vulkan->PhysicalDevice(), rt.vulkan->QueueFamily(),
                                           rt.queue_lock, rt.swapchain_format, P::kDotsWide,
                                           P::kDotsHigh);
    }
    if (dots_chain_.Valid()) {
        if (show.dots > 0.0f && !dots_chain_.Busy()) {
            const uint32_t key = P::DotsKey(f.game.charge, show.dots);
            if (key != dots_key_) {
                P::PaintDots(f.game.charge, show.dots, pixels_);
                dots_chain_.Upload(pixels_.data());
                dots_key_ = key;
            }
        }
        dots_chain_.Poll();
    }

    // The powers' picture is the bottom screen's; it may lack the strip for a few frames.
    const KP::PixelRect strip = in.bottom != XR_NULL_HANDLE
                                    ? KP::StripIn(in.bottom_width, in.bottom_height)
                                    : KP::PixelRect{};
    const auto set = [](Quad& q, XrSwapchain chain, XrRect2Di rect) {
        q.swapchain = chain;
        q.image_rect = rect;
        q.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        q.visible = false;
    };
    if (strip.width != 0) {
        set(out.powers, in.bottom,
            {{static_cast<int32_t>(strip.x), static_cast<int32_t>(strip.y)},
             {static_cast<int32_t>(strip.width), static_cast<int32_t>(strip.height)}});
    }
    if (dots_chain_.HasImage()) {
        set(out.dots, dots_chain_.Handle(), {{0, 0}, {P::kDotsWide, P::kDotsHigh}});
    }
    // Drawn by the game's next picture: at once at the opacity it starts with.
    KP::g_alpha.store(show.powers, std::memory_order_relaxed);

    const bool powers_up = show.powers > 0.0f && out.powers.swapchain != XR_NULL_HANDLE;
    const bool dots_up = show.dots > 0.0f && out.dots.swapchain != XR_NULL_HANDLE;
    const Input::Posef head = ToInput(in.head.head);
    const Screen screen = powers_up || dots_up ? ScreenFrom(in) : Screen{};
    VideoCore::KiuHud::Place hud = hud_now;
    if (!hud.on || hud.page) { // the HUD is not in the view: where it would be
        hud = VideoCore::KiuHud::PlaceFor({}, screen.tan_x, screen.tan_y, 0.0f);
    }
    const auto put = [&](Quad& q, const P::Place& p) {
        q.pose = ToXr(p.pose);
        q.size = {p.width, p.height};
        q.visible = true;
        aimed_ |= settings.at == P::At::Hand && hands.aiming &&
                  P::AngleTo(hands.aim, p) < P::kAimedDeg;
    };
    if (powers_up) {
        constexpr float kAspect = static_cast<float>(KP::kWide) / static_cast<float>(KP::kHigh);
        P::Place p;
        if (settings.at == P::At::Hand) {
            p = P::OverHand(hands.off, head.position, settings.up_m, settings.width_deg, kAspect,
                            powers_width_);
        } else {
            p = screen.At(P::WithHud(settings.at, hud, screen.tan_x, screen.tan_y,
                                     static_cast<float>(KP::kWide),
                                     static_cast<float>(KP::kHigh)));
        }
        powers_width_ = p.width;
        put(out.powers, p);
    } else {
        powers_width_ = 0.0f;
    }
    if (dots_up) {
        // The dots are at the image's left: the part that has them in the middle over the hand, at
        // the right on the right.
        const float part = static_cast<float>(P::DotsWidthPx(P::DotsFor(f.game.charge))) /
                           static_cast<float>(P::kDotsWide);
        constexpr float kAspect =
            static_cast<float>(P::kDotsWide) / static_cast<float>(P::kDotsHigh);
        constexpr float kWide = static_cast<float>(P::kDotsWide) / static_cast<float>(P::kDotPx);
        P::Place p;
        float shift = 0.0f; // of the image's width, to the right
        if (settings.at == P::At::Hand) {
            p = P::OverHand(hands.off, head.position, settings.up_m,
                            P::kDotDeg * settings.width_deg / 13.6f * kWide, kAspect, dots_width_);
            shift = 0.5f * (1.0f - part);
        } else {
            constexpr float kDot = P::kHudDotPx / P::kHudScale;
            p = screen.At(P::WithHud(settings.at, hud, screen.tan_x, screen.tan_y, kDot * kWide,
                                     kDot * kWide / kAspect));
            shift = settings.at == P::At::Right ? 1.0f - part : 0.0f;
        }
        dots_width_ = p.width;
        p.pose.position =
            p.pose.position + Input::Rotate(p.pose.orientation, {shift * p.width, 0.0f, 0.0f});
        put(out.dots, p);
    } else {
        dots_width_ = 0.0f;
    }

    const auto say = [&](bool up, bool& logged, const char* what, const Quad& q) {
        if (up == logged) {
            return;
        }
        logged = up;
        if (!up) {
            LOG_INFO(Frontend, "xr: Angel's Aim: the {} away", what);
            return;
        }
        const Input::Vec3f to = ToInput(q.pose).position - screen.head;
        const float d = std::max(Input::Length(to), 0.01f);
        LOG_INFO(Frontend, "xr: Angel's Aim: the {} up ({}; {:.1f} x {:.1f} deg, {:.2f} m away)",
                 what,
                 settings.at == P::At::Hand  ? "over the hand"
                 : settings.at == P::At::Bar ? "under the bar"
                                             : "at the right",
                 2.0f * std::atan(0.5f * q.size.width / d) * 57.2958f,
                 2.0f * std::atan(0.5f * q.size.height / d) * 57.2958f, d);
    };
    say(powers_up, logged_powers_, "powers", out.powers);
    say(dots_up, logged_dots_, "dots", out.dots);
    std::vector<Quad> quads;
    for (const Quad* q : {&out.powers, &out.dots}) {
        if (q->swapchain != XR_NULL_HANDLE) {
            quads.push_back(*q);
        }
    }
    return quads;
}

} // namespace FrameXr
