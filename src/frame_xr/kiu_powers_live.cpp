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
    P::Settings s;
    s.powers = value("AZAHARFRAME_KIU_POWERS") == "1";
    s.dots = value("AZAHARFRAME_KIU_POWER_DOTS") == "1";
    if (!s.powers && !s.dots) {
        return s;
    }
    s.gold = value("AZAHARFRAME_KIU_POWERS_GOLD") != "0";
    s.at = P::AtFrom(value("AZAHARFRAME_KIU_POWERS_AT"));
    s.powers_opacity = P::OpacityFrom(value("AZAHARFRAME_KIU_POWERS_OPACITY"));
    s.dots_opacity = P::OpacityFrom(value("AZAHARFRAME_KIU_POWER_DOTS_OPACITY"));
    s.stay_s = P::NumberFrom(value("AZAHARFRAME_KIU_POWERS_S"), 0.5f, 30.0f, s.stay_s);
    s.width_deg = P::NumberFrom(value("AZAHARFRAME_KIU_POWERS_DEG"), 6.0f, 30.0f, s.width_deg);
    s.up_m = P::NumberFrom(value("AZAHARFRAME_KIU_POWERS_UP"), 0.0f, 0.3f, s.up_m);
    return s;
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
