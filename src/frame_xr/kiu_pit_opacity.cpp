// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstdlib>
#include "common/logging/log.h"
#include "frame_xr/input/kiu_model.h"
#include "frame_xr/kiu_pit_opacity.h"
#include "video_core/kiu_view_tap.h"

namespace FrameXr::Kiu::PitOpacity {

namespace {

namespace KH = Input::KiuHooks;
namespace KM = Input::KiuModel;

/// VBlanks: before the first look (the view settles), and each phase (two game frames at 30).
constexpr int kSettle = 6;
constexpr int kPhase = 4;
constexpr int kHiddenSkip = 3; ///< VBlanks hidden before the draws are looked at (+2 a retry)
constexpr int kTries = 3;

enum class Phase { Idle, Settle, Drawn, Hidden };

struct State {
    Phase phase = Phase::Idle;
    int left = 0;
    uint32_t model = 0;      ///< the model the phases are for
    uint32_t found_for = 0;  ///< the model whose draws are known (0: none)
    bool cleared = false;    ///< this app cleared his drawn byte (phase Hidden)
    uint32_t tried_for = 0;  ///< the model the failed tries are for
    int tries = 0;           ///< failed tries for it (each skips longer; kTries at most)
};
State s;

void Collect(int phase) {
    auto& d = VideoCore::Immersive::g_pit_draws;
    if (phase == 1) {
        std::scoped_lock lock{d.mutex};
        d.seen[0].clear();
        d.seen[1].clear();
    }
    d.collect.store(phase, std::memory_order_relaxed);
}

void PutBack(KH::GameMemory& mem) {
    uint32_t word = 0;
    if (s.cleared && s.model != 0 && mem.Read32(s.model + KH::kModelDrawnWord, word)) {
        if (const auto w = KH::ModelDrawWrite(word, false, true)) {
            mem.WriteData(s.model + KH::kModelDrawnWord, *w);
        }
    }
    s.cleared = false;
}

} // namespace

uint32_t PercentFor(const char* switch_text, uint32_t setting) {
    long percent = static_cast<long>(setting);
    if (switch_text && *switch_text) {
        char* end = nullptr;
        const long v = std::strtol(switch_text, &end, 10);
        if (end != switch_text) {
            percent = v;
        }
    }
    return static_cast<uint32_t>(std::clamp(percent, 20L, 100L));
}

void OnVBlank(KH::GameMemory& mem, uint32_t model, bool outside, uint32_t percent) {
    auto& d = VideoCore::Immersive::g_pit_draws;
    const bool want = outside && model != 0 && percent < 100;
    if (!want || (s.phase != Phase::Idle && model != s.model)) {
        if (s.phase != Phase::Idle) { // left mid-way: his byte back, nothing learnt
            Collect(0);
            PutBack(mem);
            s.phase = Phase::Idle;
        }
        d.alpha.store(1.0f, std::memory_order_relaxed);
        if (!want) {
            return;
        }
    }
    if (s.found_for == model && s.phase == Phase::Idle) {
        d.alpha.store(static_cast<float>(percent) / 100.0f, std::memory_order_relaxed);
        return;
    }
    d.alpha.store(1.0f, std::memory_order_relaxed);
    uint32_t word = 0;
    switch (s.phase) {
    case Phase::Idle:
        s.phase = Phase::Settle;
        s.left = kSettle;
        s.model = model;
        return;
    case Phase::Settle:
        if (--s.left <= 0) {
            Collect(1);
            s.phase = Phase::Drawn;
            s.left = kPhase;
        }
        return;
    case Phase::Drawn:
        if (--s.left <= 0) {
            Collect(0); // the frames already on their way still have him: not looked at
            s.phase = Phase::Hidden;
            s.left = kPhase + kHiddenSkip + 2 * (s.tried_for == model ? s.tries : 0);
        }
        break; // the byte is cleared from here on (below)
    case Phase::Hidden:
        if (s.left == kPhase) {
            Collect(2);
        }
        if (--s.left <= 0) {
            Collect(0);
            PutBack(mem);
            std::vector<uint32_t> drawn, hidden;
            {
                std::scoped_lock lock{d.mutex};
                drawn = d.seen[0];
                hidden = d.seen[1];
            }
            std::vector<uint32_t> pit = KM::PitDrawsFrom(drawn, hidden);
            const bool ok = !pit.empty() && pit.size() <= KM::kPitDrawsMax;
            LOG_INFO(Frontend,
                     "xr: Kid Icarus: Pit Opacity: {} of his draws found ({} draws with him, {} "
                     "without){}",
                     pit.size(), drawn.size(), hidden.size(), ok ? "" : "; not used");
            if (!ok) {
                pit.clear();
                s.tries = s.tried_for == model ? s.tries + 1 : 1;
                s.tried_for = model;
            }
            {
                std::scoped_lock lock{d.mutex};
                d.pit = std::move(pit);
            }
            if (ok || s.tries >= kTries) { // else tried again from the next VBlank
                s.found_for = model;
            }
            s.phase = Phase::Idle;
            return;
        }
        break;
    }
    // Hidden: his drawn byte held clear (on land the game sets it again each game frame).
    if (s.phase == Phase::Hidden && mem.Read32(model + KH::kModelDrawnWord, word)) {
        if (const auto w = KH::ModelDrawWrite(word, true, false)) {
            if (mem.WriteData(model + KH::kModelDrawnWord, *w)) {
                s.cleared = true;
            }
        }
    }
}

} // namespace FrameXr::Kiu::PitOpacity
