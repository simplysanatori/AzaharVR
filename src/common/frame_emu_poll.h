// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuguns: a hook the HID service calls on the emulation thread at each pad update
// (about 234 times a second of the game's time, always, whatever the game enables), with the
// emulated clock. frame_xr reads a few values of a game's memory there (frame_xr/kiu_weapon.h):
// on the emulation thread, so never while the game writes them. Unset: nothing.
// AzaharFrame kiuaim2: a second hook, Direct Aim's (frame_xr/kiu_probe.h), run at each pad update
// too and at each VBlank (video_core/gpu.cpp), where it writes the game's reticle between the
// game's frames.
// AzaharFrame kiuturn: a third, Right Stick turning's (frame_xr/kiu_turn.h), at the same two places.

#pragma once

#include <atomic>
#include <cstdint>

namespace Common::FrameEmuPoll {

using Hook = void (*)(int64_t ticks);

inline std::atomic<Hook>& Slot() {
    static std::atomic<Hook> hook{nullptr};
    return hook;
}

inline void Set(Hook hook) {
    Slot().store(hook, std::memory_order_release);
}

inline std::atomic<Hook>& AimSlot() {
    static std::atomic<Hook> hook{nullptr};
    return hook;
}

inline void SetAim(Hook hook) {
    AimSlot().store(hook, std::memory_order_release);
}

/// AzaharFrame kiuaim2: Direct Aim's hook alone (the VBlank).
inline void RunAim(int64_t ticks) {
    if (const Hook hook = AimSlot().load(std::memory_order_acquire)) {
        hook(ticks);
    }
}

/// AzaharFrame kiuturn: Right Stick turning's hook (frame_xr/kiu_turn.h): at each VBlank after
/// Direct Aim's (`vblank` true: it writes the camera's turn) and at each pad update (false: its
/// trace only). Set only while its test switches are on.
using TurnHook = void (*)(int64_t ticks, bool vblank);

inline std::atomic<TurnHook>& TurnSlot() {
    static std::atomic<TurnHook> hook{nullptr};
    return hook;
}

inline void SetTurn(TurnHook hook) {
    TurnSlot().store(hook, std::memory_order_release);
}

inline void RunTurn(int64_t ticks, bool vblank) {
    if (const TurnHook hook = TurnSlot().load(std::memory_order_acquire)) {
        hook(ticks, vblank);
    }
}

/// The pad update: Direct Aim's hook, then the reads (AzaharFrame kiuturn: then the turn's trace).
inline void Run(int64_t ticks) {
    RunAim(ticks);
    if (const Hook hook = Slot().load(std::memory_order_acquire)) {
        hook(ticks);
    }
    RunTurn(ticks, false);
}

} // namespace Common::FrameEmuPoll
