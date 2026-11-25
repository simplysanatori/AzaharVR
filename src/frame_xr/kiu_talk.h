// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiutalk: what Kid Icarus: Uprising's bottom screen draws of the talk's portraits and
// illustrations, from the game's own state, so the cut-out (video_core/kiu_talk.h) is shown only
// when what it found is that. Read each VBlank (kiu_hooks.cpp).
//
// Pure but for Publish and what follows it (unit tested: src/tests/frame_xr/kiu_talk.cpp).

#pragma once

#include <array>
#include <bit>
#include <cstdint>

#include "frame_xr/kiu_first_person.h"
#include "frame_xr/kiu_ui.h"
#include "video_core/kiu_talk.h"

namespace FrameXr::Kiu::Talk {

using Input::KiuTurn::CodeWord;

static_assert(FirstPerson::kPortraitCut ==
              FirstPerson::Cut{0, VideoCore::KiuTalk::kCardTop, 320,
                               240 - VideoCore::KiuTalk::kCardTop});

/// The code that says what the fields are (the portraits' draw, the illustration's).
inline constexpr std::array<CodeWord, 15> kCodeEur{{
    {0x9E2E54, 0xE2800010}, // add r0, r0, #16 (a place: 36 bytes each)
    {0x9E2E58, 0xE5901020}, // ldr r1, [r0, #0x20] (its colour)
    {0x9E2E88, 0xE1B02C21}, // lsrs r2, r1, #24 (nothing with no alpha)
    {0x9E2E90, 0xE590201C}, // ldr r2, [r0, #0x1C] (its wipe)
    {0x9E30F0, 0x00AAAAAA}, // the TEV's constant
    {0x9E30F4, 0x3F7D70A4}, // 0.99
    {0x9E3110, 0x42800000}, // 64
    {0x9E3114, 0x43980000}, // 304
    {0xA0FA98, 0xE5956200}, // ldr r6, [r5, #0x200] (the illustration's pieces)
    {0xA0FAB0, 0xE2855F81}, // add r5, r5, #0x204
    {0xA0FAF0, 0xED950A08}, // vldr s0, [r5, #0x20] (a piece's alpha)
    {0xA0FC78, 0xE2855024}, // add r5, r5, #36
    {0xA0FD78, 0x41600000}, // 14
    {0xA0FD8C, 0x41200000}, // 10
    {0xA0FD90, 0x3E800000}, // 0.25
}};
inline constexpr std::array<CodeWord, 15> kCodeUsaRev1{{
    {0x9E2CBC, 0xE2800010},
    {0x9E2CC0, 0xE5901020},
    {0x9E2CF0, 0xE1B02C21},
    {0x9E2CF8, 0xE590201C},
    {0x9E2F58, 0x00AAAAAA},
    {0x9E2F5C, 0x3F7D70A4},
    {0x9E2F78, 0x42800000},
    {0x9E2F7C, 0x43980000},
    {0xA0F814, 0xE5956200},
    {0xA0F82C, 0xE2855F81},
    {0xA0F86C, 0xED950A08},
    {0xA0F9F4, 0xE2855024},
    {0xA0FAF4, 0x41600000},
    {0xA0FB08, 0x41200000},
    {0xA0FB0C, 0x3E800000},
}};
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

// The portraits' object: four places.
inline constexpr uint32_t kFirstPlace = 0x10, kPlaceSize = 36;
inline constexpr uint32_t kPlaceWipe = 0x1C, kPlaceColor = 0x20;
// The illustration's pieces, four too, after the bits that say which are there.
inline constexpr uint32_t kFirstPiece = Ui::kPieces + 4, kPieceSize = 36, kPieceAlpha = 0x20;

/// A place's portrait is drawn (with at least one quad): some alpha, also after the share an
/// illustration leaves it, and not wiped away.
inline bool PlaceDrawn(uint32_t color, float wipe, float share) {
    float alpha = static_cast<float>(color >> 24);
    if (share < 0.99f) {
        alpha = alpha / 255.0f * share * 255.0f;
    }
    return alpha >= 1.0f && !(wipe > 0.001f && wipe * 304.0f - 64.0f >= 239.9f);
}

/// `ctx`: a battle UI's context whose bottom pass draws.
template <class Reader>
VideoCore::KiuTalk::Expected ExpectedOf(const Reader& m, uint32_t ctx, bool pictures,
                                        bool illustration) {
    VideoCore::KiuTalk::Expected e;
    const uint32_t ui = ctx + Ui::kBottom;
    if (!pictures) {
        e.known = true;
        return e;
    }
    float share = 0.0f;
    uint32_t v = 0, pieces = 0;
    if (illustration) {
        if (!m.Read32(ui + Ui::kIllustration, v) || !m.Read32(ui + Ui::kPieces, pieces)) {
            return e;
        }
        share = std::bit_cast<float>(v);
        const float fade = (14.0f * share - 10.0f) * 0.25f;
        for (uint32_t i = 0; i < 4 && fade > 0.0f; ++i) {
            if ((pieces >> i & 1) != 0 && m.Read32(ui + kFirstPiece + kPieceSize * i + kPieceAlpha, v)) {
                e.illustration |= std::bit_cast<float>(v) * fade * 255.0f >= 1.0f;
            }
        }
    }
    uint32_t portraits = 0, state = 0, places = 0;
    if (!m.Read32(ui + Ui::kPortraits, portraits)) {
        return e;
    }
    if (share < 0.996f && portraits >= 0x08000000u && portraits < 0x40000000u &&
        (portraits & 3u) == 0 && m.Read32(portraits, state) &&
        (state & Ui::kPortraitsHidden) == 0 && m.Read32(portraits + Ui::kPlaces, places)) {
        for (uint32_t i = 0; i < 4; ++i) {
            const uint32_t place = portraits + kFirstPlace + kPlaceSize * i;
            uint32_t color = 0, wipe = 0;
            if ((places >> i & 1) != 0 && m.Read32(place + kPlaceColor, color) &&
                m.Read32(place + kPlaceWipe, wipe) &&
                PlaceDrawn(color, std::bit_cast<float>(wipe), 1.0f - share)) {
                ++e.portraits;
            }
        }
    }
    e.known = true;
    return e;
}

/// The cut-out: with Character Portraits, unless the test switch AZAHARFRAME_KIU_TALK_CUTOUT is 0.
constexpr bool CutoutFor(const char* switch_text, bool portraits) {
    return portraits && !(switch_text && *switch_text == '0');
}
bool CutoutWanted();
/// A test aid: AZAHARFRAME_KIU_TALK_CUTOUT=card, as if the game's code were another (the card, in
/// the strip).
bool CardForced();

/// Each VBlank while the game runs: the strip in the bottom screen's image, by the setting alone
/// (so the image is not remade as battles, cutscenes and menus come and go).
void Strip(bool game);
/// Each VBlank of a battle's view, and when there is none.
template <class Reader>
void Publish(const Reader& m, const Ui::State& ui, int& code, bool usa, bool trace) {
    auto& live = VideoCore::KiuTalk::g_live;
    if (!ui.known || !ui.shown || ui.quads == 0 || !CutoutWanted()) {
        live.SetExpected({});
        live.Set({});
        return;
    }
    if (code < 0) {
        code = CodeMatches(m, usa) ? 1 : 0;
    }
    const VideoCore::KiuTalk::Expected e =
        code != 0 && !CardForced() ? ExpectedOf(m, ui.context, ui.pictures, ui.illustration)
                                   : VideoCore::KiuTalk::Expected{};
    live.SetExpected(e);
    VideoCore::KiuHud::Batch batch;
    if (e.known) {
        batch = {ui.vertex_base, ui.index_offset, ui.quads};
    }
    live.Set(batch);
    live.trace.store(trace, std::memory_order_relaxed);
}
void Off();

} // namespace FrameXr::Kiu::Talk
