// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiutalk: kiu_talk.h.

#include "common/frame_test_switches.h"
#include "common/logging/log.h"
#include "frame_xr/kiu_talk.h"

namespace FrameXr::Kiu::Talk {

bool CutoutWanted() {
    const auto text = Common::FrameTestSwitches::Value("AZAHARFRAME_KIU_TALK_CUTOUT");
    return CutoutFor(text ? text->c_str() : nullptr, FirstPerson::PortraitsWanted());
}

bool CardForced() {
    return Common::FrameTestSwitches::Value("AZAHARFRAME_KIU_TALK_CUTOUT") == "card";
}

void Strip(bool game) {
    const bool on = game && CutoutWanted();
    if (VideoCore::KiuTalk::g_live.strip.exchange(on, std::memory_order_relaxed) != on) {
        LOG_INFO(Frontend, "xr: Kid Icarus hooks: the talk's strip under the bottom screen {}",
                 on ? "on" : "off");
    }
}

void Off() {
    VideoCore::KiuTalk::g_live.SetExpected({});
    VideoCore::KiuTalk::g_live.Set({});
}

} // namespace FrameXr::Kiu::Talk
