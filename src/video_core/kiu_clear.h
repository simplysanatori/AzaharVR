// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include "common/common_types.h"

/// AzaharFrame kiuhs3: what nothing draws in Angel's Aim's picture. Kid Icarus: Uprising fills each
/// eye's depth at the start of a picture, never its colour: its own camera's frame is always covered.
/// Angel's Aim draws 2.7 to 4.8 times past that frame, where the world can end (under the floor, past a
/// wall), and a colour buffer kept there what it last held: the picture before's, or the bottom screen's
/// (EUR: drawn in 18000000 after that eye's picture is copied out). An eye's picture that starts with a
/// draw of the world (through the patched camera, or shaded on the CPU and depth tested) is cleared first.
namespace VideoCore::KiuClear {

/// Set by frame_xr/kiu_hooks.cpp.
inline std::atomic<bool> g_on{false};

class Pictures {
public:
    /// A fill [start, end): the picture with its depth there begins.
    void Fill(u32 start, u32 end) {
        if (start >= end) {
            return;
        }
        Slot* to = &slots[0];
        for (Slot& s : slots) {
            if (s.start == start && s.end == end) {
                to = &s;
                break;
            }
            if (s.serial < to->serial) { // the oldest
                to = &s;
            }
        }
        *to = {start, end, ++serial, true};
    }
    /// A draw of the top screen's size that writes colour, its depth at `depth`: whether the colour is
    /// cleared first. Only the picture's first draw decides, and only if it is the world's: a picture
    /// that starts with 2D keeps what it has.
    bool First(u32 depth, bool world) {
        for (Slot& s : slots) {
            if (s.armed && depth >= s.start && depth < s.end) {
                s.armed = false;
                return world;
            }
        }
        return false;
    }

private:
    struct Slot {
        u32 start = 0, end = 0, serial = 0;
        bool armed = false;
    };
    std::array<Slot, 4> slots{};
    u32 serial = 0;
};

} // namespace VideoCore::KiuClear
