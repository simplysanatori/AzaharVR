// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiu140: Kid Icarus: Uprising's cutscenes drawn by the game itself, told from play in
// Angel's Aim. In a battle the game's camera has first person's angle (the "wide camera"); a
// cutscene draws through a camera of its own (33 to 45 degrees high was seen against 112), while
// the player, the playing flag and the scene camera's angle all stay as in play (the Frame's log,
// 2026-11-24). Shown in first person it is magnified to fill the view and Pit, whose model first
// person hides, is a head alone. Others (Chapter 2's opening) hand their camera over through the
// player's own node, so first person's eye and angle reach them and they play from inside his
// head; for those there is the game's own object (EventWatch, kiu_hooks.h CutsceneObject). Either
// is shown on the Primary Screen instead, as the in-game
// menu is (immersive.cpp Decision), with him drawn. Pure, unit tested
// (src/tests/frame_xr/kiu_cutscene.cpp).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>

namespace FrameXr::Kiu {

/// The game's cutscene object there or not, two VBlanks in a row (a single read can miss).
class EventWatch {
public:
    bool Update(bool there) {
        run_ = there != on_ ? run_ + 1 : 0;
        if (run_ >= 2) {
            on_ = there;
            run_ = 0;
        }
        return on_;
    }

private:
    int run_ = 0;
    bool on_ = false;
};

class CutsceneWatch {
public:
    static constexpr std::size_t kWindow = 6; ///< VBlanks counted together (0.1 s)
    static constexpr uint32_t kMinDraws = 6;  ///< fewer in a window say nothing
    static constexpr int kSettle = 45;        ///< VBlanks the wide camera is in before it counts
    static constexpr int kEnter = 4;          ///< VBlanks in a row
    static constexpr int kLeave = 2;

    /// Once a VBlank: `wide`: first person in a battle with the wide camera asked for and in; the
    /// camera uploads since the last one with its angle and with another. True: a cutscene.
    bool Update(bool wide, uint32_t match, uint32_t other) {
        if (!wide) {
            *this = {};
            return false;
        }
        match_[at_] = match;
        other_[at_] = other;
        at_ = (at_ + 1) % kWindow;
        if (settled_ < kSettle) {
            ++settled_;
            return false;
        }
        const uint32_t m = std::accumulate(match_.begin(), match_.end(), 0u);
        const uint32_t o = std::accumulate(other_.begin(), other_.end(), 0u);
        if (m + o < kMinDraws) {
            return on_; // a fade to black, a loading picture: as it was
        }
        const bool says = on_ ? o * 2 > m + o : o * 10 > (m + o) * 8;
        run_ = says != on_ ? run_ + 1 : 0;
        if (run_ >= (on_ ? kLeave : kEnter)) {
            on_ = says;
            run_ = 0;
        }
        return on_;
    }

private:
    std::array<uint32_t, kWindow> match_{}, other_{};
    std::size_t at_ = 0;
    int settled_ = 0;
    int run_ = 0;
    bool on_ = false;
};

} // namespace FrameXr::Kiu
