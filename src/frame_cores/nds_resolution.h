// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "GPU.h"

#include "video_core/renderer_vulkan/alt_xr_renderer.h"

namespace FrameCores {

/// The most a DS game is drawn at, as Internal Resolution's menu
inline constexpr uint32_t kMaxNdsScale = 8;

/// A renderer that draws a DS game over the console's 256x192, on both screens. Which graphics it
/// draws with is its own business. All of it on the thread that runs the game.
class NdsHiresRenderer : public melonDS::Renderer {
public:
    using melonDS::Renderer::Renderer;

    /// Between two frames, 2 to kMaxNdsScale; what is in VRAM by display capture stays. False: it
    /// cannot (no memory), and is not used any more.
    virtual bool SetScale(uint32_t scale) = 0;

    /// The last frame's screens. Their pixels stay until the next frame's are ready, also while
    /// the game is paused or its console is off. False: it cannot draw any more.
    virtual bool GetScreens(Vulkan::AltScreenImage& top, Vulkan::AltScreenImage& bottom) = 0;
};

/// Null with why when there is none here.
using NdsHiresMaker =
    std::function<std::unique_ptr<NdsHiresRenderer>(melonDS::NDS& nds, std::string& why)>;

/**
 * Which renderer a DS game has: melonDS's software renderer at 1x, so that 1x is always the same
 * picture, and over it a NdsHiresRenderer, made when first needed. If that cannot be made or stops
 * working the game goes on at 1x, with the reason in the log, and is not asked again.
 */
class NdsResolution {
public:
    explicit NdsResolution(NdsHiresMaker maker = {}) : maker{std::move(maker)} {}

    /// A resolution can be chosen
    bool Available() const {
        return static_cast<bool>(maker) && !failed;
    }

    /// The factor from the next frame on, and the highest it may become without the player's doing
    /// (Dynamic Resolution's ceiling).
    void SetFactor(uint32_t factor, uint32_t ceiling);

    /// Between two frames. `threaded` is the software renderer's.
    void Apply(melonDS::NDS& nds, bool threaded);

    /// What the last frame was drawn at
    uint32_t Scale() const {
        return scale;
    }

    /// What Dynamic Resolution may raise it to
    uint32_t Ceiling() const {
        return Available() ? ceiling : 1;
    }

    /// The last frame's screens over 1x. False: they are in melonDS's framebuffers.
    bool GetScreens(Vulkan::AltScreenImage& top, Vulkan::AltScreenImage& bottom);

    /// The console, and with it the renderer, is gone.
    void Detach() {
        hires = nullptr;
        scale = 1;
    }

private:
    void Fail(const std::string& why);

    NdsHiresMaker maker;
    NdsHiresRenderer* hires = nullptr; ///< the console's renderer
    uint32_t wanted = 1;
    uint32_t ceiling = 1;
    uint32_t scale = 1;
    bool failed = false;
    bool leave = false; ///< `hires` does not work any more
};

/// This build's renderer over 1x; empty: none.
NdsHiresMaker DefaultNdsHires();

} // namespace FrameCores
