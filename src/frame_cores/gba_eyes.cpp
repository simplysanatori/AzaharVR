// Adapter for mGBA (endrift, MPL-2.0)
// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/gba_eyes.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstring>
#include <mutex>

#include "common/layer_depth.h"
#include "common/logging/log.h"

extern "C" {
#include <mgba/core/core.h>
#include <mgba/internal/gba/gba.h>
// The last one's second guard opens where it should close: this block is its guard.
#undef CXX_GUARD_START
#undef CXX_GUARD_END
#define CXX_GUARD_START
#define CXX_GUARD_END
#include <mgba/internal/gba/renderers/video-software.h>
}

namespace FrameCores {
namespace {

constexpr uint32_t kWidth = GBA_VIDEO_HORIZONTAL_PIXELS;
constexpr uint32_t kHeight = GBA_VIDEO_VERTICAL_PIXELS;
constexpr uint32_t kOpaque = 0xFF000000u;
constexpr uint32_t kPriorities = 4;
constexpr uint32_t kFarthest = kPriorities - 1;
constexpr uint32_t kObjPriority = 1; ///< the sprites are one layer, at this priority's depth
constexpr int kShown = 4;            ///< of a background's `enabled`: mGBA's ENABLED_MAX

using Backgrounds = std::array<GBAVideoSoftwareBackground, 4>;

/// The backgrounds a video mode has, a bit each.
constexpr std::array<uint32_t, 8> kModeBackgrounds{0b1111, 0b0111, 0b1100, 0b0100, 0b0100, 0b0100};

using DrawLine = void (*)(GBAVideoRenderer*, int);

/// mGBA's own, which it does not export.
DrawLine SoftwareLine() {
    static const DrawLine line = [] {
        const auto renderer = std::make_unique<GBAVideoSoftwareRenderer>();
        GBAVideoSoftwareRendererCreate(renderer.get());
        return renderer->d.drawScanline;
    }();
    return line;
}

/// Whether mGBA is about to draw line `y` again, or leaves it as it was: what its drawScanline
/// goes by.
bool LineChanges(const GBAVideoSoftwareRenderer& game, int y) {
    const auto& cache = game.cache[y];
    if ((game.scanlineDirty[y >> 5] >> (y & 31)) & 1u ||
        std::memcmp(game.nextIo, cache.io, sizeof(game.nextIo)) != 0) {
        return true;
    }
    if ((game.dispcnt & 7u) != 0) {
        for (int i = 0; i < 2; ++i) {
            if (cache.scale[i][0] != game.bg[2 + i].sx || cache.scale[i][1] != game.bg[2 + i].sy) {
                return true;
            }
        }
    }
    for (int i = 0; i < 2; ++i) {
        const auto& window = game.winN[i];
        bool on = window.on;
        if (y == window.v.start + window.offsetY) {
            on = true;
        }
        if (y == window.v.end + window.offsetY) {
            on = false;
        }
        if (cache.windowOn[i] != on) {
            return true;
        }
    }
    return false;
}

using Common::LayerDepth::Eye;

/// An eye's renderer and its picture.
struct EyeRenderer {
    Eye eye{};
    GBAVideoSoftwareRenderer own{};
    std::array<uint32_t, kWidth * kHeight> pixels{}; ///< XBGR8888
    std::array<uint32_t, kWidth> within{};           ///< DrawWithin's line
    std::array<int32_t, kPriorities> shifts{};
    bool ends = false;    ///< the layers end in it, as in the game's own picture
    bool at_left = false; ///< where; else at the right

    /// `depth`: between the eyes.
    void SetShifts(const std::array<int32_t, kPriorities>& depth) {
        for (std::size_t i = 0; i < shifts.size(); ++i) {
            shifts[i] = Common::LayerDepth::EyeShift(depth[i], eye);
        }
        ends = Common::LayerDepth::EdgeColumns(depth[kFarthest]) != 0;
        at_left = eye == Eye::Right;
    }

    int32_t Columns(uint32_t priority) const {
        return ends ? std::abs(shifts[priority]) : 0;
    }

    /// With all the game's renderer has at this line (what a driver hid too), but the line cache
    /// and the pictures.
    void Draw(const GBAVideoSoftwareRenderer& game, int y) {
        std::memcpy(&own, &game, offsetof(GBAVideoSoftwareRenderer, cache));
        own.cache[y] = game.cache[y];
        own.nextY = game.nextY;
        own.lastHighlightAmount = game.lastHighlightAmount;
        own.d.cache = nullptr;
        own.temporaryBuffer = nullptr;
        own.outputBuffer = pixels.data();
        own.outputBufferStride = static_cast<int>(kWidth);
        own.scanlineDirty[y >> 5] |= 1u << (y & 31);
        for (auto& bg : own.bg) {
            bg.yCache = -1;
            bg.offsetX = shifts[bg.priority];
        }
        own.objOffsetX = static_cast<int16_t>(shifts[kObjPriority]);
        const Backgrounds before = std::to_array(own.bg);
        SoftwareLine()(&own.d, y);
        EndLayers(game, before, y);
    }

    /// The columns each of a line's layers leaves at its edge (0: the layer is not there).
    struct Edges {
        std::array<int32_t, 4> backgrounds{};
        int32_t sprites = 0;
        int32_t backdrop = 0; ///< nothing moves it: it ends with the farthest background
    };

    /// Of the line just drawn. `before`: its backgrounds as they were then.
    Edges LineEdges(const Backgrounds& before) const {
        Edges edges;
        for (std::size_t i = 0; i < before.size(); ++i) {
            if ((kModeBackgrounds[own.dispcnt & 7u] >> i & 1u) != 0 &&
                before[i].enabled == kShown && own.bg[i].enabled == kShown && !own.d.disableBG[i]) {
                edges.backgrounds[i] = Columns(before[i].priority);
                edges.backdrop = std::max(edges.backdrop, edges.backgrounds[i]);
            }
        }
        if (GBARegisterDISPCNTIsObjEnable(own.dispcnt) && !own.d.disableOBJ) {
            edges.sprites = Columns(kObjPriority);
        }
        return edges;
    }

    /// Each layer of the line just drawn ends where it ends in the game's own picture: the columns
    /// beyond are drawn again without it.
    void EndLayers(const GBAVideoSoftwareRenderer& game, const Backgrounds& before, int y) {
        if (Columns(kFarthest) == 0 || GBARegisterDISPCNTIsForcedBlank(own.dispcnt)) {
            return;
        }
        const Edges edges = LineEdges(before);
        for (int32_t column = 0; column < std::max(edges.backdrop, edges.sprites);) {
            int32_t end = std::max(edges.backdrop, edges.sprites);
            for (const int32_t edge : {edges.backgrounds[0], edges.backgrounds[1],
                                       edges.backgrounds[2], edges.backgrounds[3], edges.sprites}) {
                if (edge > column) {
                    end = std::min(end, edge);
                }
            }
            DrawWithin(game, before, edges, y, column, end);
            column = end;
        }
    }

    /// From `column` of the edge to before `end` of the line, with the layers that have something
    /// there.
    void DrawWithin(const GBAVideoSoftwareRenderer& game, const Backgrounds& before,
                    const Edges& edges, int y, int32_t column, int32_t end) {
        std::ranges::copy(before, own.bg);
        // What a layer that is beyond would have been blended with is blended with nothing instead
        bool blends = game.target2Bd != 0;
        for (std::size_t i = 0; i < before.size(); ++i) {
            const bool beyond = column < edges.backgrounds[i];
            own.d.disableBG[i] = game.d.disableBG[i] || beyond;
            blends |= beyond && before[i].target2;
        }
        const bool no_sprites = column < edges.sprites;
        own.d.disableOBJ = game.d.disableOBJ || no_sprites;
        const bool no_backdrop = column < edges.backdrop;
        own.normalPalette[0] = no_backdrop ? 0 : game.normalPalette[0];
        own.target1Bd = no_backdrop ? 0 : game.target1Bd;
        own.target2Bd = no_backdrop ? blends : game.target2Bd;
        own.outputBuffer = within.data();
        own.outputBufferStride = 0;
        own.scanlineDirty[y >> 5] |= 1u << (y & 31);
        SoftwareLine()(&own.d, y);
        uint32_t* const line = pixels.data() + static_cast<std::size_t>(y) * kWidth;
        const auto width = static_cast<int32_t>(kWidth);
        const int32_t from = at_left ? column : width - end;
        const int32_t to = at_left ? end : width - column;
        for (int32_t x = from; x < to; ++x) {
            const bool beyond = no_backdrop && (own.row[x] & FLAG_UNWRITTEN) == FLAG_UNWRITTEN;
            line[x] = beyond ? Common::LayerDepth::kBeyond : within[x];
        }
    }
};

} // namespace

struct GbaEyes::Impl {
    /// mGBA's renderer has no room for a context: its hook finds the eyes by the renderer. Read on
    /// any thread a game runs on, written while that game stands still.
    struct Slot {
        std::atomic<const GBAVideoRenderer*> renderer{nullptr};
        Impl* eyes = nullptr;
    };
    static std::array<Slot, 8> slots;
    static std::mutex slots_mutex;

    GBAVideoRenderer* renderer = nullptr;
    std::array<uint32_t, kWidth * kHeight> game_pixels{}; ///< XBGR8888
    std::bitset<kHeight> redraw; ///< lines the eyes have not, or not with these shifts
    std::array<int32_t, kPriorities> shifts{}; ///< between the eyes
    std::array<EyeRenderer, 2> eyes{EyeRenderer{Eye::Left}, EyeRenderer{Eye::Right}};
    uint32_t depth = 0;
    float max_shift = Common::LayerDepth::kNoLimit;
    bool on = false; ///< drew the last frame
    bool drawing = false;

    bool Wanted() const {
        return renderer && depth > 0 && max_shift > 0.0f;
    }

    static void Hook(GBAVideoRenderer* renderer, int y) {
        for (const Slot& slot : slots) {
            if (slot.renderer.load(std::memory_order_acquire) == renderer) {
                slot.eyes->Line(*reinterpret_cast<GBAVideoSoftwareRenderer*>(renderer), y);
                return;
            }
        }
        SoftwareLine()(renderer, y);
    }

    void Line(GBAVideoSoftwareRenderer& game, int y) {
        const auto line = static_cast<std::size_t>(y);
        if (!drawing) {
            redraw.set(line);
            SoftwareLine()(&game.d, y);
            return;
        }
        const bool draw = redraw[line] || LineChanges(game, y);
        if (draw) {
            for (EyeRenderer& eye : eyes) {
                eye.Draw(game, y);
            }
        }
        SoftwareLine()(&game.d, y);
        if (draw) {
            std::copy_n(game.outputBuffer +
                            static_cast<std::ptrdiff_t>(y) * game.outputBufferStride,
                        kWidth, game_pixels.begin() + static_cast<std::ptrdiff_t>(line * kWidth));
            redraw.reset(line);
        }
    }

    void UpdateShifts() {
        const bool inverted = Common::LayerDepth::Get().inverted.load(std::memory_order_relaxed);
        std::array<int32_t, kPriorities> now;
        for (uint32_t i = 0; i < kPriorities; ++i) {
            now[i] = Common::LayerDepth::Directed(
                Common::LayerDepth::LayerShift(depth, i, kFarthest, kWidth, max_shift), inverted);
        }
        if (now != shifts) {
            shifts = now;
            for (EyeRenderer& eye : eyes) {
                eye.SetShifts(now);
            }
            redraw.set();
        }
    }

    /// The lines the game's renderer leaves as they are, until it draws them.
    void Begin() {
        const auto& game = *reinterpret_cast<const GBAVideoSoftwareRenderer*>(renderer);
        for (uint32_t y = 0; y < kHeight; ++y) {
            std::copy_n(game.outputBuffer +
                            static_cast<std::ptrdiff_t>(y) * game.outputBufferStride,
                        kWidth, game_pixels.begin() + static_cast<std::ptrdiff_t>(y * kWidth));
        }
        for (EyeRenderer& eye : eyes) {
            eye.pixels = game_pixels;
        }
        redraw.set();
    }
};

std::array<GbaEyes::Impl::Slot, 8> GbaEyes::Impl::slots;
std::mutex GbaEyes::Impl::slots_mutex;

GbaEyes::GbaEyes() : impl(std::make_unique<Impl>()) {}

GbaEyes::~GbaEyes() {
    Detach();
}

bool GbaEyes::Attach(mCore* game) {
    Detach();
    if (game->platform(game) != mPLATFORM_GBA || game->videoLogger) {
        return false;
    }
    GBAVideoRenderer* renderer = static_cast<GBA*>(game->board)->video.renderer;
    if (!renderer || renderer->drawScanline != SoftwareLine()) {
        return false;
    }
    std::scoped_lock lock{Impl::slots_mutex};
    for (auto& slot : Impl::slots) {
        if (!slot.renderer.load(std::memory_order_relaxed)) {
            slot.eyes = impl.get();
            slot.renderer.store(renderer, std::memory_order_release);
            impl->renderer = renderer;
            renderer->drawScanline = &Impl::Hook;
            return true;
        }
    }
    LOG_ERROR(Core, "GbaEyes: too many games");
    return false;
}

void GbaEyes::Detach() {
    if (!impl->renderer) {
        return;
    }
    std::scoped_lock lock{Impl::slots_mutex};
    for (auto& slot : Impl::slots) {
        if (slot.renderer.load(std::memory_order_relaxed) == impl->renderer) {
            slot.renderer.store(nullptr, std::memory_order_release);
        }
    }
    if (impl->renderer->drawScanline == &Impl::Hook) { // else what is above it goes with the core
        impl->renderer->drawScanline = SoftwareLine();
    }
    impl->renderer = nullptr;
    impl->on = impl->drawing = false;
}

void GbaEyes::SetDepth(uint32_t level) {
    impl->depth = std::min(level, Common::LayerDepth::kMaxLevel);
    impl->on = impl->on && impl->Wanted();
}

void GbaEyes::SetMaxShift(float share) {
    impl->max_shift = share;
    impl->on = impl->on && impl->Wanted();
}

void GbaEyes::BeginFrame() {
    if (!impl->Wanted()) {
        return;
    }
    impl->UpdateShifts();
    if (!impl->on) {
        impl->Begin();
        impl->on = true;
    }
    impl->drawing = true;
}

void GbaEyes::EndFrame() {
    if (!impl->drawing) {
        return;
    }
    impl->drawing = false;
    for (EyeRenderer& eye : impl->eyes) {
        for (uint32_t& pixel : eye.pixels) {
            pixel |= kOpaque;
        }
    }
}

DriverPicture GbaEyes::Picture(Eye eye) const {
    const EyeRenderer& of = impl->eyes[static_cast<std::size_t>(eye)];
    return impl->on ? DriverPicture{of.pixels.data(), kWidth, kHeight, kWidth} : DriverPicture{};
}

DriverPicture GbaEyes::GamePicture() const {
    return impl->on ? DriverPicture{impl->game_pixels.data(), kWidth, kHeight, kWidth}
                    : DriverPicture{};
}

float GbaEyes::EdgeShift() const {
    return impl->on ? Common::LayerDepth::EdgeShift(impl->shifts[kFarthest], kWidth) : 0.0f;
}

} // namespace FrameCores
