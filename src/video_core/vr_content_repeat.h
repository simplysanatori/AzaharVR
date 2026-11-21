// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame bfikit: which top screen swaps carry a new game picture. The VR side counts a
// picture per top screen swap (GPU::SetBufferSwap, common/xr_pacer.h game_swaps), and Black Frame
// Insertion (frame_xr/motion_clarity.h) lights each swap once. A game that redraws its world at 60
// with its logic at 30 (a 60 fps mod that renders every VBlank but moves the world every other
// one) swaps 60 times a second with 30 pictures: each picture is lit twice, 8.3 ms apart, and
// anything the eye follows is seen twice (the doubling of report 30's H2, never measured: the
// frame trace's uniform hash, AZAHARFRAME_IMMERSIVE_FRAME_TRACE, was off in every log).
//
// Here, always (the immersive modes only: the world uploads are read where the view is patched,
// rasterizer_accelerated.cpp ApplyVRDataToPicaVSUniforms): each world upload's own uniforms, before
// the patch, are hashed (64-bit words, about 0.1 us an upload), twice: all of them, and all but the
// camera's rows (the view matrix register and the 3 after it), as the game may take a head-driven
// camera every frame while its world moves at 30. At each top screen swap the frame that ended is
// compared with the one before: the same uniforms are the same picture again. Counted for the perf
// line (app.cpp, every 10 s); and a content serial per swap, kept in a ring by the swap's number,
// so the XR thread can key Black Frame Insertion to new pictures
// (AZAHARFRAME_XR_FLASH_BY_CONTENT, motion_clarity.h ContentKey). Pure but for the globals at
// the end (unit-tested: src/tests/video_core/vr_content_repeat.cpp).

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace VideoCore::Immersive {

/// A fast hash of `bytes` (8-byte words, then the tail): equality is all it is used for.
inline uint64_t HashWords(const void* data, std::size_t bytes) noexcept {
    const auto* p = static_cast<const unsigned char*>(data);
    uint64_t h = 0x9E3779B97F4A7C15ull ^ bytes;
    std::size_t i = 0;
    for (; i + 8 <= bytes; i += 8) {
        uint64_t w;
        std::memcpy(&w, p + i, 8);
        h ^= w;
        h *= 0xFF51AFD7ED558CCDull;
        h ^= h >> 32;
    }
    for (; i < bytes; ++i) {
        h = (h ^ p[i]) * 0x100000001B3ull;
    }
    return h;
}

/// The counts of a window (the perf line's 10 s).
struct ContentCounts {
    uint32_t swaps = 0; ///< top screen swaps
    uint32_t read = 0;  ///< of them with world uploads read (else not known: counted as new)
    uint32_t same = 0;  ///< the same uniforms as the swap before
    uint32_t same_alt = 0; ///< of `same`, right after a swap with new ones (30 in 60: every other)
    uint32_t scene_same = 0;     ///< the same without the camera's rows
    uint32_t scene_same_alt = 0; ///< of `scene_same`, right after one with new ones
};

/// Which uniforms decide a new picture.
enum class ContentBy : uint8_t {
    All,   ///< all of the game's uniforms
    Scene, ///< all but the camera's rows
};

class ContentRepeat {
public:
    /// One world upload (the video thread): `all` the hash of all its uniforms, `scene` without
    /// the camera's rows.
    void Upload(uint64_t all, uint64_t scene) noexcept {
        sum_ += all;
        scene_sum_ += scene;
        ++uploads_;
    }

    /// A top screen swap ended the frame (the emulation thread): its uploads against the swap
    /// before's. A frame without uploads is not known: a new picture, and the next one is not
    /// compared with it.
    void Swap() noexcept {
        ++counts_.swaps;
        bool same = false, scene_same = false;
        if (uploads_ > 0) {
            ++counts_.read;
            same = have_last_ && sum_ == last_sum_;
            scene_same = have_last_ && scene_sum_ == last_scene_;
            if (same) {
                ++counts_.same;
                counts_.same_alt += last_new_ ? 1 : 0;
            }
            if (scene_same) {
                ++counts_.scene_same;
                counts_.scene_same_alt += last_scene_new_ ? 1 : 0;
            }
            last_sum_ = sum_;
            last_scene_ = scene_sum_;
            have_last_ = true;
        } else {
            have_last_ = false;
        }
        last_new_ = !same;
        last_scene_new_ = !scene_same;
        serial_ += same ? 0 : 1;
        scene_serial_ += scene_same ? 0 : 1;
        sum_ = scene_sum_ = 0;
        uploads_ = 0;
    }

    /// The picture's serial after the last Swap (a new one at each new picture).
    uint64_t Serial(ContentBy by) const noexcept {
        return by == ContentBy::All ? serial_ : scene_serial_;
    }

    /// The window's counts, and a new window.
    ContentCounts Take() noexcept {
        const ContentCounts c = counts_;
        counts_ = {};
        return c;
    }

private:
    uint64_t sum_ = 0, scene_sum_ = 0;
    uint32_t uploads_ = 0;
    uint64_t last_sum_ = 0, last_scene_ = 0;
    bool have_last_ = false;
    bool last_new_ = true, last_scene_new_ = true;
    uint64_t serial_ = 0, scene_serial_ = 0;
    ContentCounts counts_;
};

/// The serials by the swap's number (common/xr_pacer.h game_swaps after the swap), for the XR
/// thread: a release records the game_swaps it showed, so its picture's serial is found here while
/// it is among the last kRing swaps. Written by one thread, read by another: each entry carries its
/// swap number, checked before and after the values are read.
class ContentRing {
public:
    static constexpr std::size_t kRing = 64;

    void Put(uint64_t swap, uint64_t all, uint64_t scene) noexcept {
        Entry& e = ring_[swap % kRing];
        e.swap.store(0, std::memory_order_release);
        e.all.store(all, std::memory_order_relaxed);
        e.scene.store(scene, std::memory_order_relaxed);
        e.swap.store(swap, std::memory_order_release);
    }
    std::optional<uint64_t> Get(uint64_t swap, ContentBy by) const noexcept {
        if (swap == 0) {
            return std::nullopt;
        }
        const Entry& e = ring_[swap % kRing];
        if (e.swap.load(std::memory_order_acquire) != swap) {
            return std::nullopt;
        }
        const uint64_t v = by == ContentBy::All ? e.all.load(std::memory_order_relaxed)
                                                : e.scene.load(std::memory_order_relaxed);
        if (e.swap.load(std::memory_order_acquire) != swap) {
            return std::nullopt;
        }
        return v;
    }

private:
    struct Entry {
        std::atomic<uint64_t> swap{0}, all{0}, scene{0};
    };
    std::array<Entry, kRing> ring_{};
};

/// The process's: uploads from the video thread, the swap from the emulation thread (the same
/// thread in this build: the rasterizer runs on the emulation thread), the counts taken by the XR
/// thread under the mutex-free exchange below.
inline ContentRepeat g_content_repeat;
inline ContentRing g_content_ring;
inline std::atomic<uint32_t> g_content_swaps{0}, g_content_read{0}, g_content_same{0},
    g_content_same_alt{0}, g_content_scene_same{0}, g_content_scene_same_alt{0};

/// GPU::SetBufferSwap for the top screen, after game_swaps counted it (`swap`: its new value).
inline void OnContentSwap(uint64_t swap) noexcept {
    g_content_repeat.Swap();
    g_content_ring.Put(swap, g_content_repeat.Serial(ContentBy::All),
                       g_content_repeat.Serial(ContentBy::Scene));
    const ContentCounts c = g_content_repeat.Take();
    g_content_swaps.fetch_add(c.swaps, std::memory_order_relaxed);
    g_content_read.fetch_add(c.read, std::memory_order_relaxed);
    g_content_same.fetch_add(c.same, std::memory_order_relaxed);
    g_content_same_alt.fetch_add(c.same_alt, std::memory_order_relaxed);
    g_content_scene_same.fetch_add(c.scene_same, std::memory_order_relaxed);
    g_content_scene_same_alt.fetch_add(c.scene_same_alt, std::memory_order_relaxed);
}

/// The XR thread: the counts since the last call.
inline ContentCounts TakeContentCounts() noexcept {
    ContentCounts c;
    c.swaps = g_content_swaps.exchange(0, std::memory_order_relaxed);
    c.read = g_content_read.exchange(0, std::memory_order_relaxed);
    c.same = g_content_same.exchange(0, std::memory_order_relaxed);
    c.same_alt = g_content_same_alt.exchange(0, std::memory_order_relaxed);
    c.scene_same = g_content_scene_same.exchange(0, std::memory_order_relaxed);
    c.scene_same_alt = g_content_scene_same_alt.exchange(0, std::memory_order_relaxed);
    return c;
}

} // namespace VideoCore::Immersive
