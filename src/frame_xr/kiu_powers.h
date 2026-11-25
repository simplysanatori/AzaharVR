// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiupowers: Kid Icarus: Uprising's powers (on land) and the special attack's charge
// (in the air), read from the game's memory each VBlank (test switches only, KIU_HOOKS.md
// "Powers"). Both are parts of the bottom screen's power panel, one object in the battle UI's 2D
// context (kiu_ui.h).
//
// Pure, unit tested (src/tests/frame_xr/kiu_powers.cpp).

#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

#include "frame_xr/kiu_ui.h"

namespace FrameXr::Kiu::Powers {

using Input::KiuTurn::CodeWord;

/// The code that says what the fields are; any other word and nothing is read.
inline constexpr std::array<CodeWord, 7> kCodeEur{{
    {0xA17FC0, 0xE3500002}, // cmp r0, #2 (the command: the next power)
    {0xA17F14, 0xE5940118}, // ldr r0, [r4, #0x118] (the place in the list)
    {0xA16AB4, 0xED940A6D}, // vldr s0, [r4, #0x1B4] (the charge)
    {0xA16AAC, 0xE3100901}, // tst r0, #0x4000 (in the air)
    {0xA16BDC, 0xE3110907}, // tst r1, #0x1C000
    {0x513C38, 0xED800A6D}, // vstr s0, [r0, #0x1B4] (the charge given)
    {0x513C44, 0x40400000}, // 3.0, its most
}};
inline constexpr std::array<CodeWord, 7> kCodeUsaRev1{{
    {0xA17D3C, 0xE3500002},
    {0xA17C90, 0xE5940118},
    {0xA16830, 0xED940A6D},
    {0xA16828, 0xE3100901},
    {0xA16958, 0xE3110907},
    {0x513B68, 0xED800A6D},
    {0x513B74, 0x40400000},
}};

/// The panel in the battle UI's 2D context, and its fields.
inline constexpr uint32_t kPanel = 0xB64;
inline constexpr uint32_t kFlags = 0x0, kAir = 0x4000;
inline constexpr uint32_t kShown = 0x4, kName = 0x20000, kUsed = 0x10000000;
inline constexpr uint32_t kNameTimer = 0x104, kPlace = 0x118, kEntries = 0x11C, kCount = 0x184;
inline constexpr uint32_t kCharge = 0x1B4;
inline constexpr uint32_t kMaxEntries = 13;
inline constexpr float kMostCharge = 3.0f;

struct State {
    bool known = false; ///< a battle UI there, its fields make sense
    bool air = false;   ///< the dots; else the powers
    uint32_t count = 0; ///< powers in the list
    uint32_t place = 0; ///< the selected one
    uint32_t uses = 0;  ///< of it
    bool name = false;  ///< the name bubble is up
    bool used = false;  ///< one was just used
    float name_timer = 0.0f;
    float charge = 0.0f; ///< 0 to 3 dots
    bool paused = false; ///< the pause screen, or a page instead of the HUD
    // The 2D context's draws (Ui::State's).
    uint32_t vertex_base = 0, index_offset = 0, quads = 0;
    bool operator==(const State&) const = default;
};

template <class Reader>
bool CodeMatches(const Reader& m, bool usa) {
    for (const CodeWord& c : usa ? kCodeUsaRev1 : kCodeEur) {
        uint32_t w = 0;
        if (!m.Read32(c.addr, w) || w != c.word) {
            return false;
        }
    }
    return Ui::CodeMatches(m, usa);
}

template <class Reader>
State Read(const Reader& m) {
    const Ui::State ui = Ui::Read(m);
    State s;
    if (!ui.known || !ui.shown || ui.quads == 0) {
        return s;
    }
    const uint32_t p = ui.context + kPanel;
    uint32_t flags = 0, shown = 0, place = 0, count = 0, timer = 0, charge = 0, entry = 0;
    if (!m.Read32(p + kFlags, flags) || !m.Read32(p + kShown, shown) ||
        !m.Read32(p + kPlace, place) || !m.Read32(p + kCount, count) ||
        !m.Read32(p + kNameTimer, timer) || !m.Read32(p + kCharge, charge) ||
        count > kMaxEntries || (count != 0 && place >= count)) {
        return s;
    }
    s.charge = std::bit_cast<float>(charge);
    s.name_timer = std::bit_cast<float>(timer);
    if (!(s.charge >= 0.0f && s.charge <= kMostCharge) || !std::isfinite(s.name_timer)) {
        return s;
    }
    if (count != 0 && m.Read32(p + kEntries + 8 * place, entry)) {
        s.uses = entry & 0xFF;
    }
    s.known = true;
    s.air = (flags & kAir) != 0;
    s.count = count;
    s.place = place;
    s.name = (shown & kName) != 0;
    s.used = (shown & kUsed) != 0;
    s.paused = ui.paused || ui.page;
    s.vertex_base = ui.vertex_base;
    s.index_offset = ui.index_offset;
    s.quads = ui.quads;
    return s;
}

/// Whether the player did something with the powers between two reads (what shows them over the
/// hand): another one selected, the name asked for again, one used.
constexpr bool Touched(const State& before, const State& now) {
    if (!before.known || !now.known || before.air || now.air) {
        return false;
    }
    return now.place != before.place || now.name_timer > before.name_timer + 0.5f ||
           (now.used && !before.used);
}
/// A dot filled or was spent.
constexpr bool DotChanged(const State& before, const State& now) {
    return before.known && now.known && before.air && now.air &&
           static_cast<int>(before.charge) != static_cast<int>(now.charge);
}

} // namespace FrameXr::Kiu::Powers
