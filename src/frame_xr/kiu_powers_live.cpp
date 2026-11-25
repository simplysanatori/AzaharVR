// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiupowers: kiu_powers_live.h.

#include <mutex>

#include "common/frame_test_switches.h"
#include "common/settings.h"
#include "frame_xr/input/kiu_trigger_layout.h"
#include "frame_xr/kiu_first_person.h"
#include "frame_xr/kiu_powers_live.h"

namespace FrameXr::Kiu::Powers {

namespace {
std::mutex g_mutex;
State g_state;
bool g_playing = false;
Hands g_hands;
} // namespace

Vis::Powers::Settings SettingsNow() {
    namespace P = Vis::Powers;
    const auto value = [](const char* name) { return Common::FrameTestSwitches::Value(name); };
    // The settings are Angel's Aim's: without it no strip in the bottom screen's image either.
    uint32_t menu = Settings::values.vr_kiu_power_menu.GetValue();
    if (menu != 0 && !Kiu::FirstPerson::AngelsAim()) {
        menu = 0;
    }
    P::Switches sw;
    sw.powers = value("AZAHARFRAME_KIU_POWERS");
    sw.dots = value("AZAHARFRAME_KIU_POWER_DOTS");
    if (menu != 0 || sw.powers == "1" || sw.dots == "1") {
        sw.gold = value("AZAHARFRAME_KIU_POWERS_GOLD");
        sw.at = value("AZAHARFRAME_KIU_POWERS_AT");
        sw.powers_opacity = value("AZAHARFRAME_KIU_POWERS_OPACITY");
        sw.dots_opacity = value("AZAHARFRAME_KIU_POWER_DOTS_OPACITY");
        sw.stay_s = value("AZAHARFRAME_KIU_POWERS_S");
        sw.width_deg = value("AZAHARFRAME_KIU_POWERS_DEG");
        sw.up_m = value("AZAHARFRAME_KIU_POWERS_UP");
    }
    P::Settings s =
        P::SettingsFor(menu, Settings::values.vr_kiu_power_menu_opacity.GetValue(), sw);
    s.across_deg = static_cast<float>(Settings::values.vr_kiu_power_menu_across.GetValue());
    s.height_deg = static_cast<float>(Settings::values.vr_kiu_power_menu_height.GetValue());
    return s;
}

float HudGapNow() {
    return Vis::Powers::HudGap(SettingsNow());
}

bool AirNoBumpersWanted() {
    return Settings::values.vr_kiu_trigger_layout.GetValue() == Input::KiuTriggers::kFireAndPower &&
           Kiu::FirstPerson::AngelsAim() &&
           Common::FrameTestSwitches::Value("AZAHARFRAME_KIU_SKY_BUMPERS") != "1";
}

bool AirNoBumpers() {
    if (!AirNoBumpersWanted()) {
        return false;
    }
    std::scoped_lock lock{g_mutex};
    return g_state.known && g_state.air;
}

void SetGame(const State& state, bool playing) {
    std::scoped_lock lock{g_mutex};
    g_state = state;
    g_playing = playing;
}

State Game(bool* playing) {
    std::scoped_lock lock{g_mutex};
    if (playing) {
        *playing = g_playing;
    }
    return g_state;
}

void SetHands(const Hands& hands) {
    std::scoped_lock lock{g_mutex};
    g_hands = hands;
}

Hands HandsNow() {
    std::scoped_lock lock{g_mutex};
    return g_hands;
}

} // namespace FrameXr::Kiu::Powers
