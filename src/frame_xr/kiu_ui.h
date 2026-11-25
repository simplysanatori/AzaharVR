// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiu1s: what Kid Icarus: Uprising's own battle UI is showing, read from the game's
// memory each VBlank (test switches only, KIU_HOOKS.md "Single screen"). The game draws the
// battle's 2D through one 2D context, the top screen's in one pass and the bottom screen's in
// another; the pass decides by the fields below what it draws, so they say which it is:
// the HUD, a page instead of it (by its drawing the results; never seen set), the pause screen.
//
// Where to look (the passes, the context, the sprite flush, the message and the pause screen) was
// learnt from UprisingHack's notes, a PC port of the game (https://github.com/patricksmill/
// UprisingHack, GPL-3.0-or-later); every address was found again in each build by its code and
// the fields read in it. No code or layout of it is used.
//
// Pure, unit tested (src/tests/frame_xr/kiu_ui.cpp).

#pragma once

#include <array>
#include <bit>
#include <cstdint>

#include "frame_xr/input/kiu_turn.h"

namespace FrameXr::Kiu::Ui {

using Input::KiuTurn::CodeWord;

/// The code that says what the fields are; any other word and the UI is not read.
inline constexpr std::array<CodeWord, 30> kCodeEur{{
    {0xA33F48, 0xE5904140}, // ldr r4, [r0, #0x140] (the battle UI's 2D context)
    {0x60D53C, 0xE2805A03}, // add r5, r0, #0x3000 (the top pass)
    {0x60D548, 0xE59501B8}, // ldr r0, [r5, #0x1B8] (the pass's flags)
    {0x60D5D8, 0xE2840A02}, // add r0, r4, #0x2000
    {0x60D5E0, 0xE2800F63}, // add r0, r0, #0x18C (the page's)
    {0x60D60C, 0x0A000017}, // beq: no page
    {0x60D678, 0xE1110000}, // tst r1, r0 (the other pages' flags)
    {0x60D904, 0x00010100}, // their mask
    {0x60D7D8, 0xE2845A03}, // add r5, r4, #0x3000
    {0x60D7DC, 0xE2855F49}, // add r5, r5, #0x124 (the message)
    {0x60D7FC, 0xE5D50003}, // ldrb r0, [r5, #3] (its screen)
    {0xB52738, 0xE2000001}, // and r0, r0, #1 (the page shown)
    {0xB529FC, 0xE0100001}, // ands r0, r0, r1 (the pause screen shown)
    {0xB52A08, 0x00000801}, // its mask
    {0x92E71C, 0xE594101C}, // ldr r1, [r4, #0x1C] (the vertices' base)
    {0x92E2A0, 0xE596A028}, // ldr r10, [r6, #0x28] (the quads' indices)
    {0x92E2A4, 0xE0855085}, // add r5, r5, r5, lsl #1 (12 bytes a quad)
    {0x92E2B0, 0xE08A4105}, // add r4, r10, r5, lsl #2
    {0x7182F4, 0xE5900064}, // ldr r0, [r0, #0x64] (the menu's holder)
    {0xA3BC90, 0xE594000C}, // ldr r0, [r4, #0xC] (the menu in it)
    {0xA3651C, 0xE5C5072C}, // strb r0, [r5, #0x72C] (its number)
    {0x60DB10, 0xE2850B02}, // add r0, r5, #0x800 (the bottom pass)
    {0x60DB14, 0xE2800FD9}, // add r0, r0, #0x364 (the bottom screen's own UI)
    {0xA18214, 0xE3120B02}, // tst r2, #0x800 (its flags: another picture instead)
    {0xA18384, 0xED948A7F}, // vldr s16, [r4, #0x1FC] (the illustration's share)
    {0xA18548, 0xE59400D8}, // ldr r0, [r4, #0xD8] (the portraits)
    {0xA1855C, 0xEE390AC8}, // vsub.f32 s0, s19, s16 (theirs)
    {0x9E2D68, 0xE3100403}, // tst r0, #0x3000000 (their flags: not drawn)
    {0x9E2D70, 0xE5970008}, // ldr r0, [r7, #8] (their four places)
    {0x9E2D74, 0xE210100F}, // ands r1, r0, #0xF
}};
inline constexpr std::array<CodeWord, 30> kCodeUsaRev1{{
    {0xA33CC4, 0xE5904140},
    {0x60D3F0, 0xE2805A03},
    {0x60D3FC, 0xE59501B8},
    {0x60D48C, 0xE2840A02},
    {0x60D494, 0xE2800F63},
    {0x60D4C0, 0x0A000017},
    {0x60D52C, 0xE1110000},
    {0x60D7B8, 0x00010100},
    {0x60D68C, 0xE2845A03},
    {0x60D690, 0xE2855F49},
    {0x60D6B0, 0xE5D50003},
    {0xB524B8, 0xE2000001},
    {0xB5277C, 0xE0100001},
    {0xB52788, 0x00000801},
    {0x92E584, 0xE594101C},
    {0x92E108, 0xE596A028},
    {0x92E10C, 0xE0855085},
    {0x92E118, 0xE08A4105},
    {0x71815C, 0xE5900064},
    {0xA3BA0C, 0xE594000C},
    {0xA36298, 0xE5C5072C},
    {0x60D9C4, 0xE2850B02},
    {0x60D9C8, 0xE2800FD9},
    {0xA17F90, 0xE3120B02},
    {0xA18100, 0xED948A7F},
    {0xA182C4, 0xE59400D8},
    {0xA182D8, 0xEE390AC8},
    {0x9E2BD0, 0xE3100403},
    {0x9E2BD8, 0xE5970008},
    {0x9E2BDC, 0xE210100F},
}};

/// Data and bss are at the same place in both builds.
inline constexpr uint32_t kBattleUi = 0xEE3098;
inline constexpr uint32_t kContext = 0x140;
// The context.
inline constexpr uint32_t kVertexBase = 0x1C; ///< physical
inline constexpr uint32_t kIndexOffset = 0x28;
inline constexpr uint32_t kWide = 0x104, kHigh = 0x108; ///< of the pass's screen, in pixels
inline constexpr uint32_t kPage = 0x218C, kPageShown = 1;
inline constexpr uint32_t kPause = 0x243C, kPauseShown = 0x801;
inline constexpr uint32_t kMessage = 0x3124;
inline constexpr uint32_t kFlags = 0x31B8, kFlagShown = 1, kFlagPages = 0x10100;
/// The pass draws the bottom screen's other UI instead (its flags).
inline constexpr uint32_t kFlagOtherBottom = 0x400;
// The bottom screen's UI in the context: flags, the portraits' object, the illustration's share of
// the picture (0 to 1; the portraits have the rest of it) and the flag that asks its pieces first.
inline constexpr uint32_t kBottom = 0xB64, kBottomOther = 0x800, kBottomPieces = 0x1000000;
inline constexpr uint32_t kPortraits = 0xD8, kIllustration = 0x1FC, kPieces = 0x200;
// The portraits' object.
inline constexpr uint32_t kPortraitsHidden = 0x3000000, kPlaces = 8, kPlacesAny = 0xF;
/// The indices come after its two vertex buffers (0x60 a quad), so their offset says how many quads
/// a buffer holds (400 in the battles seen); a list names a quad by 10 bits. The GPU's base is a
/// multiple of 16 below the first buffer, so the offset carries what was cut off (`pad`: 0 or 8 seen).
constexpr uint32_t QuadsFor(uint32_t index_offset, uint32_t pad = 0) {
    if (pad >= 16 || index_offset < pad) {
        return 0;
    }
    const uint32_t quads = (index_offset - pad) / 0xC0;
    return (index_offset - pad) % 0xC0 == 0 && quads <= 1024 ? quads : 0;
}
inline constexpr uint32_t kBuffer = 0x30; ///< its first vertex buffer (virtual)
// A menu outside a battle (the game's MENUID: 0 the title, 3 the hub, 51 the page after a fall):
// its holder (null in a battle) and body, by their classes.
inline constexpr uint32_t kMenuHolder = 0xE6F928 + 0x64, kMenuBody = 0xC, kMenuId = 0x72C;
struct MenuClasses {
    uint32_t holder, body;
};
inline constexpr MenuClasses kMenuEur{0xCFC660, 0xCFC3B4}, kMenuUsaRev1{0xCFC5C0, 0xCFC314};
inline constexpr int kNoMenu = -1;
inline constexpr int kMenuCount = 59; ///< the game's own count (its last name is MENUID_MAX)
inline constexpr int kMenuCauldron = 5;
template <class Reader>
int Menu(const Reader& m, const MenuClasses& c) {
    const auto at = [&m](uint32_t p, uint32_t vtable) {
        uint32_t v = 0;
        return p >= 0x08000000u && p < 0x40000000u && (p & 3u) == 0 && m.Read32(p, v) &&
               v == vtable;
    };
    uint32_t holder = 0, body = 0, id = 0;
    if (!m.Read32(kMenuHolder, holder) || !at(holder, c.holder) ||
        !m.Read32(holder + kMenuBody, body) || !at(body, c.body) ||
        !m.Read32(body + kMenuId, id)) {
        return kNoMenu;
    }
    id &= 0xFF;
    return id < static_cast<uint32_t>(kMenuCount) ? static_cast<int>(id) : kNoMenu;
}

/// The bottom pass draws the talk's portraits or an illustration (its own tests; `ctx` is a battle
/// UI's context, its pass drawing). The boss's card is not one of them. `illustration`: the
/// illustration's share of the picture is above 0.
template <class Reader>
bool Pictures(const Reader& m, uint32_t ctx, bool* illustration_up = nullptr) {
    const uint32_t ui = ctx + kBottom;
    uint32_t flags = 0, portraits = 0, share = 0, pieces = 0;
    if (!m.Read32(ui, flags) || (flags & kBottomOther) != 0 || !m.Read32(ui + kPortraits, portraits) ||
        !m.Read32(ui + kIllustration, share) || !m.Read32(ui + kPieces, pieces)) {
        return false;
    }
    bool asked = (flags & kBottomPieces) == 0;
    for (uint32_t i = 0; i < 4 && !asked && (pieces >> i) != 0; ++i) {
        uint32_t piece = 0;
        asked = ((pieces >> i) & 1u) != 0 && m.Read32(ui + kPieces + 4 + 36 * i, piece) &&
                (piece & 0x10000u) != 0;
    }
    const float illustration = asked ? std::bit_cast<float>(share) : 0.0f;
    if (illustration > 0.0f) {
        if (illustration_up != nullptr) {
            *illustration_up = true;
        }
        return true;
    }
    uint32_t state = 0, places = 0;
    return portraits >= 0x08000000u && portraits < 0x40000000u && (portraits & 3u) == 0 &&
           m.Read32(portraits, state) && (state & kPortraitsHidden) == 0 &&
           m.Read32(portraits + kPlaces, places) && (places & kPlacesAny) != 0;
}

struct State {
    bool known = false;    ///< a battle UI there, its fields make sense
    bool shown = false;    ///< it draws
    bool page = false;     ///< the top screen's 2D is a page, not the HUD
    bool paused = false;   ///< the pause screen is drawn
    bool talk = false;     ///< a line of the talk is on view
    bool talk_below = false; ///< on the bottom screen
    bool pictures = false;   ///< the bottom screen's portraits or illustration are drawn
    bool illustration = false; ///< an illustration among them
    int menu = kNoMenu;      ///< the menu up outside a battle
    uint32_t context = 0;
    uint32_t vertex_base = 0, index_offset = 0; ///< of the context's draws
    uint32_t quads = 0;                         ///< 0: its draws are not known
    uint32_t raw_flags = 0, raw_parts = 0, raw_page = 0; ///< +0x31B8, +0x31BC, +0x218C (the trace's)
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
    return true;
}

template <class Reader>
State Read(const Reader& m, const MenuClasses& menus = kMenuEur) {
    State s;
    s.menu = Menu(m, menus);
    uint32_t ctx = 0, wide = 0, high = 0, page = 0, pause = 0, flags = 0, shown = 0, line = 0;
    if (!m.Read32(kBattleUi + kContext, ctx) || ctx < 0x08000000u || ctx >= 0x40000000u ||
        (ctx & 3u) != 0 || !m.Read32(ctx + kWide, wide) || !m.Read32(ctx + kHigh, high) ||
        !((wide == 400 || wide == 320) && high == 240) || !m.Read32(ctx + kPage, page) ||
        !m.Read32(ctx + kPause, pause) || !m.Read32(ctx + kFlags, flags) ||
        !m.Read32(ctx + kMessage, shown) || !m.Read32(ctx + kMessage + 4, line)) {
        return s;
    }
    s.known = true;
    s.context = ctx;
    s.raw_flags = flags;
    s.raw_page = page;
    m.Read32(ctx + kFlags + 4, s.raw_parts);
    s.shown = (flags & kFlagShown) != 0;
    s.page = s.shown && ((page & kPageShown) != 0 || (flags & kFlagPages) != 0);
    s.paused = s.shown && (pause & kPauseShown) != 0;
    s.talk = s.shown && (shown & 0xFF) != 0 && (shown & 0xFF00) != 0 && line != 0;
    s.talk_below = s.talk && (shown >> 24) == 1;
    s.pictures = s.shown && (flags & kFlagOtherBottom) == 0 && Pictures(m, ctx, &s.illustration);
    uint32_t base = 0, index = 0, buffer = 0;
    if (m.Read32(ctx + kVertexBase, base) && m.Read32(ctx + kIndexOffset, index) &&
        m.Read32(ctx + kBuffer, buffer) && base >= 0x18000000u && base < 0x30000000u &&
        (base & 7u) == 0 && QuadsFor(index, buffer & 0xFu) != 0) {
        s.vertex_base = base;
        s.index_offset = index;
        s.quads = QuadsFor(index, buffer & 0xFu);
    }
    return s;
}

/// One of its answers, two VBlanks in a row (a read can fall between two of the game's writes).
class Steady {
public:
    bool Update(bool now) {
        run_ = now != on_ ? run_ + 1 : 0;
        if (run_ >= 2) {
            on_ = now;
            run_ = 0;
        }
        return on_;
    }
    bool On() const {
        return on_;
    }

private:
    int run_ = 0;
    bool on_ = false;
};

/// The menu up outside a battle: another number when it is read two VBlanks in a row, none when
/// there was none for 0.2 s. The game changes menus within a frame (one VBlank of none was seen);
/// a battle comes 0.8 s after its menu is gone, the screens dark.
class SteadyMenu {
public:
    int Update(int now) {
        run_ = now != menu_ && now == next_ ? run_ + 1 : 1;
        next_ = now;
        if (now != menu_ && run_ >= (now == kNoMenu ? 12 : 2)) {
            menu_ = now;
        }
        return menu_;
    }

private:
    int menu_ = kNoMenu, next_ = kNoMenu, run_ = 0;
};

} // namespace FrameXr::Kiu::Ui
