// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiucam: Angel's Aim: a tap of the right stick's click in a battle switches the camera
// (First Person, Third Person) in place of Swap Screens, with a short fade round the jump.
//
// Pure, unit tested (src/tests/frame_xr/kiu_camera_tap.cpp); the atomics are the game's UI
// for the XR thread.

#pragma once

#include <atomic>
#include <cstdint>

namespace FrameXr::Kiu::CameraTap {

/// Settings::values.vr_kiu_stick_click.
inline constexpr uint32_t kSwitchCamera = 0, kSwapScreens = 1;

struct Now {
    bool first_person = false; ///< Angel's Aim plays the game (FirstPerson::On), not the flat mode
    bool battle = false;       ///< FirstPerson::Battle
    bool cutscene = false;
    bool paused = false;   ///< the game's own pause screen
    bool game_menu = false; ///< one of the game's menus
    bool swapped = false;  ///< the 3DS's bottom screen in focus: the tap swaps it back
    uint32_t setting = kSwitchCamera;
};
/// The tap switches the camera; else it is Swap Screens, as everywhere.
constexpr bool Switches(const Now& n) {
    return n.setting == kSwitchCamera && n.first_person && n.battle && !n.cutscene && !n.paused &&
           !n.game_menu && !n.swapped;
}
/// In a cutscene the tap does nothing (the owner, 2026-11-25): a swap there leaves the view for a
/// small flat picture. With Swap Screens chosen it still swaps, and it always swaps back.
constexpr bool Nothing(const Now& n) {
    return n.setting == kSwitchCamera && n.first_person && n.cutscene && !n.swapped;
}
/// The next camera (FirstPerson::ViewFor's numbers): First Person and Third Person in turn,
/// First Person from the Game's Camera.
constexpr int NextView(int view) {
    return view == 0 ? 1 : 0;
}

/// What the game's UI shows that Battle does not say. Set each VBlank by the hooks; any thread.
struct GameUi {
    std::atomic<bool> paused{false}, menu{false};
};
inline GameUi g_ui;
inline void SetGameUi(bool paused, bool menu) {
    g_ui.paused.store(paused, std::memory_order_relaxed);
    g_ui.menu.store(menu, std::memory_order_relaxed);
}

/// The fade round the jump: to black, the camera switched, black for as long as the game takes to
/// draw from the new place and show it, and back. Only where the projection layer draws the
/// picture (a factor in a shader that runs anyway: no layer comes, goes or changes).
inline constexpr int64_t kFadeNs = 50'000'000, kBlackNs = 150'000'000;
/// How bright the picture is, 0 to 1, this long after the tap.
constexpr float Brightness(int64_t since_ns) {
    if (since_ns < 0 || since_ns >= 2 * kFadeNs + kBlackNs) {
        return 1.0f;
    }
    const float t = since_ns < kFadeNs
                        ? 1.0f - static_cast<float>(since_ns) / static_cast<float>(kFadeNs)
                    : since_ns < kFadeNs + kBlackNs
                        ? 0.0f
                        : static_cast<float>(since_ns - kFadeNs - kBlackNs) /
                              static_cast<float>(kFadeNs);
    return t * t * (3.0f - 2.0f * t);
}
/// A switch under way. XR thread; the times are the frames' display times.
class Switch {
public:
    /// A tap: false while one is under way (until the picture is coming back).
    bool Begin(int view, int64_t now_ns, bool fades) {
        if (waiting_ || (fades && since_ns_ != 0 && Since(now_ns) >= 0 &&
                         Since(now_ns) < kFadeNs + kBlackNs)) {
            return false;
        }
        view_ = view;
        waiting_ = true;
        fades_ = fades;
        since_ns_ = now_ns;
        return true;
    }
    /// Every frame: true once, when the camera is to be switched to `View()`.
    bool Due(int64_t now_ns) {
        if (!waiting_ || (fades_ && Since(now_ns) >= 0 && Since(now_ns) < kFadeNs)) {
            return false;
        }
        waiting_ = false;
        return true;
    }
    int View() const {
        return view_;
    }
    float Brightness(int64_t now_ns) const {
        return since_ns_ != 0 && fades_ ? CameraTap::Brightness(Since(now_ns)) : 1.0f;
    }

private:
    int64_t Since(int64_t now_ns) const {
        return now_ns - since_ns_;
    }
    int64_t since_ns_ = 0;
    int view_ = 0;
    bool waiting_ = false, fades_ = false;
};

} // namespace FrameXr::Kiu::CameraTap
