// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Layer Depth's arithmetic for DS games: how far things are to the right in the right eye's
// picture.

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#include "common/layer_depth.h"

namespace FrameCores::NdsDepth {

constexpr int kWidth = 256;

/// How many pixels to the right a layer of a priority is. Priority 0 (text, HUD) stays at the
/// screen; nothing comes in front of it. With Invert Depth all are to the left, so everything does.
using LayerShifts = std::array<int, 4>;

/// A background's shift: by the priority in its BGxCNT.
constexpr int BackgroundShift(uint16_t bgcnt, const LayerShifts& shifts) {
    return shifts[bgcnt & 3];
}

/// BGxHOFS of a text background moved right; the hardware wraps it.
constexpr uint16_t ShiftScroll(uint16_t scroll, int shift) {
    return static_cast<uint16_t>(scroll - shift);
}

struct AffinePoint {
    int32_t x;
    int32_t y;
    constexpr bool operator==(const AffinePoint&) const = default;
};

/// The line's reference point of a rotated or scaled background moved right on the screen. One
/// pixel to the right is (RotA, RotC) in the background, so both coordinates change.
constexpr AffinePoint ShiftAffine(AffinePoint ref, int16_t rot_a, int16_t rot_c, int shift) {
    return {ref.x - shift * rot_a, ref.y - shift * rot_c};
}

/// A sprite's second attribute with its x moved by its own priority. x is 9 bits: past the right
/// edge it wraps to -256 and up, and no sprite is wide enough (128 at most) to reach the screen
/// again from there; -256 and 256 are the same, and to the left of it is the right edge. Window
/// sprites stay, as the windows do.
constexpr uint16_t ShiftSprite(uint16_t attr0, uint16_t attr1, uint16_t attr2,
                               const LayerShifts& shifts) {
    const bool disabled = ((attr0 >> 8) & 3) == 2;
    const bool window = ((attr0 >> 10) & 3) == 2;
    if (disabled || window) {
        return attr1;
    }
    const int shift = shifts[(attr2 >> 10) & 3];
    return static_cast<uint16_t>((attr1 & ~0x1FF) | ((attr1 + shift) & 0x1FF));
}

/// Vertex::Position[3] is 20.12 fixed point, cut to 24 bits by the DS: 1.0 is 0x1000, and the most
/// is just under 4096.0.
constexpr int32_t kOneW = 0x1000;

/// x on the screen is 9 bits. Past 256 the rasteriser drops pixels; below 0 it would write into
/// the row above.
constexpr int32_t kMaxX = 0x1FF;

/// The 3D scene's depth, Baiji's (Dolphin's) way: x += strength * (1 - convergence / w). What is
/// at the convergence stands at `base`, what is far away `strength` behind it, what is nearer comes
/// forward; nothing leaves the scene's room.
struct SceneShift {
    int strength;        ///< for what is far away, from `base`, in 16ths of a pixel to the right
    int32_t convergence; ///< the w that stands at `base`, 20.12
    int nearest;         ///< the room's ends, in pixels to the right
    int farthest;
    int base = 0;
    constexpr bool operator==(const SceneShift&) const = default;
};

/// The w that stands at the room's near end, whatever the view. Nothing has less.
constexpr int32_t kNearestW = 1;

/// How far to the right a vertex is, in parts of a pixel: base + strength * (1 - convergence / w),
/// rounded, within the room. Never less for a larger w. `units` to a pixel: 1 for the software
/// renderer; 16 for melonDS's HiresPosition, times the scale for a picture drawn larger.
constexpr int VertexShiftUnits(const SceneShift& scene, int32_t w, int units) {
    if (w == kNearestW) {
        return scene.nearest * units;
    }
    int64_t shift = int64_t{scene.base} * units;
    if (w > 0) {
        const int64_t full = int64_t{scene.strength} * units;
        const int64_t units_w = full * (int64_t{w} - scene.convergence);
        const int64_t one = int64_t{16} * w;
        shift += (units_w + (units_w < 0 ? -(one / 2) : one / 2)) / one;
    }
    return static_cast<int>(std::clamp<int64_t>(shift, int64_t{scene.nearest} * units,
                                                int64_t{scene.farthest} * units));
}

/// The same in whole pixels.
constexpr int VertexShift(const SceneShift& scene, int32_t w) {
    return VertexShiftUnits(scene, w, 1);
}

/// The part of the depth the 3D scene has. It is drawn as BG0, over the 2D backgrounds of BG0's
/// priority and behind, and under those in front: it is never behind a background it is drawn over,
/// and never in front of one that is drawn over it.
struct SceneRoom {
    int nearest = 0;
    int base = 0; ///< where the convergence stands
    int farthest = 0;
    constexpr bool operator==(const SceneRoom&) const = default;
};

/// `dispcnt` and `bgcnt` (BG0 to BG3) are engine A's: which backgrounds are on, and their
/// priorities. Without one behind the scene, it ends at the backmost layer's depth; without one in
/// front, at the screen. The convergence stands where BG0's priority has it, or a layer further
/// forward if the scene had no room behind it there. With Invert Depth the layers behind it are its
/// near end: the scene is between the same layers, moved there as it is, and far is still far.
constexpr SceneRoom MakeRoom(const LayerShifts& layers, uint32_t dispcnt,
                             const std::array<uint16_t, 4>& bgcnt) {
    const int own = bgcnt[0] & 3;
    int front = 0;
    int behind = 3;
    for (int bg = 1; bg < 4; ++bg) {
        const int priority = bgcnt[static_cast<std::size_t>(bg)] & 3;
        if ((dispcnt & (0x100u << bg)) == 0) {
            continue;
        }
        if (priority < own) {
            front = std::max(front, priority);
        } else {
            behind = std::min(behind, priority);
        }
    }
    const int base = std::clamp(std::min(own, behind - 1), front, behind);
    const auto [nearest, farthest] = std::minmax(layers[static_cast<std::size_t>(front)],
                                                 layers[static_cast<std::size_t>(behind)]);
    const int shift = layers[static_cast<std::size_t>(base)];
    const bool inverted = layers[3] < 0;
    return {nearest, inverted ? nearest + farthest - shift : shift, farthest};
}

/// What is moved left would be squeezed against the picture's left edge (ShiftVertexX) instead of
/// going past it. So the scene is drawn this much further right, where nothing is left of where
/// the game has it, and its lines are scrolled back by as much.
constexpr int SceneOffset(const LayerShifts& layers) {
    return std::max(0, -layers[3]);
}

constexpr SceneShift Offset(SceneShift scene, int offset) {
    scene.nearest += offset;
    scene.farthest += offset;
    scene.base += offset;
    return scene;
}

/// The scroll of the scene's lines for BG0's: 9 bits, from 256 on to the left.
constexpr uint16_t ScrollScene(uint16_t scroll, int offset) {
    return static_cast<uint16_t>((scroll + offset) & 0x1FF);
}

/// How far left a scroll of 9 bits moves a line; below 0: right.
constexpr int ScrollOf(uint16_t scroll) {
    return (scroll & 0x100) != 0 ? (scroll & 0x1FF) - 0x200 : scroll & 0x1FF;
}

/// The columns of a line scrolled so that what is behind the scene has nothing for: it is moved by
/// `far_shift`, and begins where the game has it.
constexpr int SceneEdge(int far_shift, int scroll) {
    return std::clamp(std::max(far_shift, far_shift - scroll), 0, kWidth);
}

/// CLEAR_DEPTH and CLRIMAGE_OFFSET with the picture behind the scene moved right; it wraps.
constexpr uint32_t ShiftClearImage(uint32_t clear_attr2, int shift) {
    const uint32_t x = (clear_attr2 >> 16) & 0xFF;
    return (clear_attr2 & ~0x00FF0000u) | ((x - static_cast<uint32_t>(shift)) & 0xFF) << 16;
}

/// What of the scene is measured, frame by frame.
struct SceneView {
    int32_t convergence = 0; ///< the w that stands at the room's base
    int32_t far = 0;         ///< the w where the scene is as far back as its room goes; 0: infinity
    constexpr bool operator==(const SceneView&) const = default;
};

/// A view's depth fills its room: a town seen from high up is at w 480 to 720, and the formula's
/// far end is at infinity. At most four times as deep as it is.
constexpr SceneShift MakeScene(const SceneRoom& room, const SceneView& view) {
    constexpr int64_t kMost = 4;
    const int64_t full = int64_t{room.farthest - room.base} * 16;
    int64_t strength = full;
    if (view.far > 0) {
        const int64_t span = int64_t{view.far} - view.convergence;
        strength = span * kMost > view.far ? full * view.far / span : full * kMost;
    }
    return {static_cast<int>(strength), view.convergence, room.nearest, room.farthest, room.base};
}

/// log2 of a w in 256ths: the whole part from its highest bit, the rest straight between.
constexpr int32_t Log2W(int32_t w) {
    if (w <= 0) {
        return 0;
    }
    const auto value = static_cast<uint32_t>(w);
    const int high = std::bit_width(value) - 1;
    const uint32_t part = high >= 8 ? value >> (high - 8) : value << (8 - high); // 256 to 511
    return high * 256 + static_cast<int32_t>(part - 256);
}

/// The w of a Log2W.
constexpr int32_t ExpW(int32_t log) {
    if (log <= 0) {
        return 1;
    }
    const int high = log >> 8;
    const auto part = static_cast<uint32_t>(256 + (log & 255));
    return static_cast<int32_t>(high >= 8 ? part << (high - 8) : part >> (8 - high));
}

/// Games' w has no common scale (a kart's road is 2 to 200, a side-scroller's planes 130 to 360,
/// a town seen from above 500 to 700), so the view is the scene's own: of the w of every vertex of
/// the world, the mean of log w less one standard deviation is the convergence (most of the scene
/// is behind its base, the nearest sixth comes forward), and one more is the far end.
struct SceneStats {
    int64_t count = 0;
    int64_t sum = 0;
    int64_t squares = 0;

    constexpr void Add(int32_t w) {
        if (w <= 0) {
            return;
        }
        const int64_t log = Log2W(w);
        ++count;
        sum += log;
        squares += log * log;
    }

    /// Zeros: nothing in the scene.
    constexpr SceneView View() const {
        if (count == 0) {
            return {};
        }
        const int64_t mean = sum / count;
        const int64_t variance = std::max<int64_t>(squares / count - mean * mean, 0);
        int64_t deviation = 0;
        while ((deviation + 1) * (deviation + 1) <= variance) {
            ++deviation;
        }
        return {ExpW(static_cast<int32_t>(mean - deviation)),
                ExpW(static_cast<int32_t>(mean + deviation))};
    }
};

/// A w of the view from frame to frame. Every change of it moves edges, so it holds still while the
/// scene's own stays within a half more or a third less; past that it follows, at most
/// kSlew a frame (a factor of two in two seconds). At a cut, at once.
struct SlowW {
    static constexpr int32_t kSlew = 2; ///< of Log2W
    int32_t value = 0;                  ///< 0: none yet
    int32_t goal = 0;

    /// `scene` 0 leaves it. Returns the frame's value.
    constexpr int32_t Follow(int32_t scene, bool cut) {
        if (scene <= 0) {
            return value;
        }
        if (value == 0 || int64_t{scene} * 2 > int64_t{goal} * 3 ||
            int64_t{scene} * 3 < int64_t{goal} * 2) {
            goal = scene;
        }
        if (value == 0 || cut) {
            value = goal;
            return value;
        }
        const int32_t from = Log2W(value);
        const int32_t to = Log2W(goal);
        value = to - from > kSlew   ? ExpW(from + kSlew)
                : from - to > kSlew ? ExpW(from - kSlew)
                                    : goal;
        return value;
    }
};

/// The depth buffer of the world, which a perspective projection makes: depth = a + b / w. Nearer
/// is less.
struct DepthLine {
    bool known = false;
    double a = 0.0;
    double b = 0.0;
    double span = kLeastSpan; ///< of the polygon it is from: its largest w by its smallest
    int32_t least_w = kMostW; ///< the nearest of the world's polygons comes

    static constexpr int32_t kMostW = 0xFFFFFF;
    static constexpr double kLeastSpan = 1.02;
    static constexpr double kWhole = 16777216.0; ///< the depth buffer's range
    /// What the buffer's 15 bits and clipped vertices make of it, and then some
    static constexpr double kTolerance = kWhole / 256;

    /// The two vertices of a polygon of the least and the largest w, z-buffered. The polygon with
    /// the largest span is the world's. A polygon whose depth does not change with w is none.
    constexpr void Add(int32_t w_near, int32_t depth_near, int32_t w_far, int32_t depth_far) {
        if (w_near <= 0 || w_far <= w_near || depth_far - depth_near < kTolerance / 8) {
            return;
        }
        least_w = std::min(least_w, w_near);
        const double this_span = static_cast<double>(w_far) / w_near;
        if (this_span <= span) {
            return;
        }
        span = this_span;
        known = true;
        b = (static_cast<double>(depth_near) - depth_far) / (1.0 / w_near - 1.0 / w_far);
        a = depth_far - b / w_far;
    }

    /// Where a polygon that faces the camera (one w for all of it) is in the world. One on the line
    /// is part of it (a billboard, a wall). One that is not was projected another way, such as a
    /// text box drawn over the world, and its w says nothing; its depth does, as the DS sorts by
    /// it. In front of the whole world it is at the near end, like a HUD, and holds still there.
    constexpr int32_t WorldW(int32_t w, int32_t depth) const {
        if (!known || w <= 0) {
            return w;
        }
        const double off = depth - (a + b / w);
        if (off <= kTolerance && off >= -kTolerance) {
            return w;
        }
        const double at = (a - depth) / -b; // 1 / w
        if (at * least_w > 1.0) {
            return kNearestW;
        }
        return at <= 1.0 / kMostW ? kMostW
                                  : static_cast<int32_t>(std::clamp(1.0 / at, 1.0, 1.0 * kMostW));
    }
};

constexpr int32_t ShiftVertexX(int32_t x, int shift) {
    return std::clamp<int32_t>(x + shift, 0, kMaxX);
}

/// Where a polygon's vertices stand.
struct PolygonPlace {
    bool each = true;  ///< each vertex by its own w
    int32_t w = 0;     ///< or all by this one
    bool world = true; ///< part of the scene's perspective, so of its view
    constexpr bool operator==(const PolygonPlace&) const = default;
};

/// A polygon with one w for all its vertices faces the camera: in the world (a billboard, a wall),
/// or it is no part of the world's perspective (a text box or a HUD drawn over it, a backdrop). In
/// a w-buffer its w is its depth, so that is right either way. In a z-buffer the world's line
/// tells. Without one, w 1 (an orthographic projection) is drawn on the screen.
constexpr PolygonPlace PlacePolygon(const int32_t* w, std::size_t count, int32_t depth,
                                    bool w_buffer, const DepthLine& line) {
    if (std::any_of(w, w + count, [w](int32_t value) { return value != w[0]; })) {
        return {};
    }
    const int32_t at = w_buffer ? w[0] : line.WorldW(w[0], depth);
    if (at != w[0]) {
        return {false, at, false};
    }
    if (w[0] == kOneW && (w_buffer || !line.known)) {
        return {false, kNearestW, false};
    }
    return {};
}

/// What the view is held to from frame to frame. Nothing in a DS says that a game cuts to another
/// scene. These do, where they occur: the picture is all black or all white (the fade between two
/// scenes), there was no 3D to show, the layers were set up anew, or nothing is where it was the
/// frame before (IsElsewhere).
struct SceneSetup {
    uint32_t layers = 0; ///< DISPCNT's mode, 3D and background bits
    uint16_t bg0cnt = 0;
    constexpr bool operator==(const SceneSetup&) const = default;
};

constexpr SceneSetup MakeSetup(uint32_t dispcnt, uint16_t bg0cnt) {
    return {dispcnt & 0x00030F0Fu, bg0cnt};
}

/// All of one frame's view is past one end of the frame's before, by more than the view is held
/// within (SlowW). No camera moves that far in a frame.
constexpr bool IsElsewhere(const SceneView& last, const SceneView& now) {
    return last.convergence > 0 && now.convergence > 0 &&
           (int64_t{now.convergence} * 2 > int64_t{last.far} * 3 ||
            int64_t{now.far} * 3 < int64_t{last.convergence} * 2);
}

/// MASTER_BRIGHT has the picture all white or all black.
constexpr bool IsBlank(uint16_t master_bright) {
    const uint16_t mode = master_bright >> 14;
    return (mode == 1 || mode == 2) && (master_bright & 0x1F) >= 16;
}

/// The view from frame to frame, of the 3D that one screen shows.
struct ViewTrack {
    SlowW convergence{};
    SlowW far{};
    SceneSetup setup{};
    SceneView last{}; ///< the frame's before, as measured
    bool cut = true;  ///< the next view is taken at once

    /// A frame without 3D on this screen.
    constexpr void Nothing() {
        cut = true;
    }

    constexpr SceneView Follow(const SceneView& scene, const SceneSetup& now, bool blank) {
        cut = cut || blank || now != setup || IsElsewhere(last, scene);
        setup = now;
        const SceneView view{convergence.Follow(scene.convergence, cut),
                             far.Follow(scene.far, cut)};
        if (scene.convergence > 0) { // held while there is nothing to take it from
            last = scene;
            cut = false;
        }
        return view;
    }
};

/// DISPCNT of engine A shows the 3D scene in BG0 (bit 3), and BG0 is on (bit 8).
constexpr bool ShowsScene(uint32_t dispcnt) {
    return (dispcnt & (1u << 3)) != 0 && (dispcnt & (1u << 8)) != 0;
}

/// The same for a line being drawn: `bg0_on` is BG0 as the DS has it on for this line (a layer
/// comes on some lines after its bit is set).
constexpr bool LineShowsScene(uint32_t dispcnt, bool bg0_on) {
    return (dispcnt & (1u << 3)) != 0 && bg0_on;
}

/// Engine A's line is the same in both eyes: a display mode without layers (off, VRAM or memory
/// display), or the scene in BG0 without a right eye's scene to go with it (the whole line is the
/// left eye's then, layers too, so that nothing stands behind a scene that is on the screen).
/// Display capture never is a reason: the right eye is drawn, and only the left eye's picture is
/// captured into the game's memory.
constexpr bool IsFlatLine(uint32_t dispcnt, bool bg0_on, bool right_has_scene) {
    const bool layers = ((dispcnt >> 16) & 3) == 1;
    return !layers || (LineShowsScene(dispcnt, bg0_on) && !right_has_scene);
}

/// Which engine is on the top screen, frame by frame. A game with 3D on both screens draws it for
/// one screen a frame: engine A changes screen every frame, the other screen shows engine B with
/// the picture captured the frame before.
struct TopWatch {
    static constexpr int kRecent = 8;
    bool a_on_top = false; ///< this frame
    bool early = false;    ///< the game changes screens before line 215
    bool at_215 = false;
    int since_change = kRecent; ///< frames, up to kRecent

    /// At the frame's start (line 262 of the one before).
    constexpr void BeginFrame(bool a_on_top_now) {
        if (a_on_top_now != a_on_top) {
            early = at_215 == a_on_top_now;
            since_change = 0;
        } else if (since_change < kRecent) {
            ++since_change;
        }
        a_on_top = a_on_top_now;
    }

    /// At line 215, where the DS draws the next frame's scene: whether the right eye's is drawn
    /// too, that is, whether engine A may be on the top screen then. A game that changes screens
    /// after this line has not said so yet: while it changes them, always.
    constexpr bool SceneNeeded(bool a_on_top_now) {
        at_215 = a_on_top_now;
        return a_on_top_now || (!early && since_change < kRecent);
    }

    /// At line 215, after SceneNeeded: whether the next scene is the top screen's.
    constexpr bool NextOnTop() const {
        return !early && since_change == 0 ? !a_on_top : at_215;
    }
};

/// Whether two pictures (8 bits a colour) are one to the eye: the same in the 5 bits a captured
/// picture keeps, but for an eighth of the pixels (what the other engine draws over it).
constexpr bool SamePicture(const uint32_t* a, const uint32_t* b, std::size_t count) {
    std::size_t other = 0;
    for (std::size_t i = 0; i < count; ++i) {
        other += ((a[i] ^ b[i]) & 0x00F8F8F8u) != 0;
    }
    return other * 8 <= count;
}

/// A screen in a frame with engine B on it (`engine_a` false), in a game with 3D on both screens:
/// it shows the picture captured from engine A a frame before, for which the right eye already has
/// one. That one stays (`has_scene_picture`: the right eye's last picture of this screen is of a
/// frame with engine A on it, and `same`: SamePicture of this frame's left picture and that
/// frame's). Drawing engine B's layers instead would move the captured picture as one flat layer
/// every other frame.
constexpr bool KeepsRightPicture(bool engine_a, bool has_scene_picture, bool same) {
    return !engine_a && has_scene_picture && same;
}

/// The right eye's left edge, where what is moved right leaves room. False: nothing is there
/// (black): every layer ends where it ends in the left eye's picture, which stays whole. The edge
/// the eyes see is the layer's own, at its depth, and a HUD on the screen is whole in both. True:
/// the headset cuts both eyes' pictures (Common::LayerDepth::Cut), with what the HUD has there.
/// Inverted, they are cut either way: the layers stand in front of the screen's edges.
constexpr bool kCutsEdge = false;

/// FrameOutput::layer_shift of a picture whose farthest layer is `farthest` pixels to the right.
constexpr float EdgeShift(int farthest) {
    return kCutsEdge || farthest < 0 ? static_cast<float>(farthest) / kWidth : 0.0f;
}

/// The shifts to blank the right eye's left edge by.
constexpr LayerShifts EdgeLayers(const LayerShifts& layers) {
    return kCutsEdge || layers[3] < 0 ? LayerShifts{} : layers;
}

/// The columns that are the shift's alone: what the game has in them is beyond its picture.
constexpr bool IsBeyond(int x, int shift) {
    return x < shift;
}

/// The shift of what a pixel of the 2D renderer is from: `flags` is its upper byte, of an engine
/// with these BGxCNT. The backdrop, which nothing moves, ends with the farthest layer. 0 for what
/// has nothing beyond the picture: the 3D, and sprites, cut as they are drawn. A pixel is on top
/// of farther ones only, so where it is beyond, they all are.
constexpr int PixelShift(uint32_t flags, const uint16_t* bgcnt, const LayerShifts& shifts) {
    if ((flags & 0xD0) != 0) {
        return 0;
    }
    for (int bg = 0; bg < 4; ++bg) {
        if ((flags & (1u << bg)) != 0) {
            return BackgroundShift(bgcnt[bg], shifts);
        }
    }
    return shifts[3];
}

/// What the 3D renderer has where no polygon is (6 bits a colour, 5 of alpha), without fog:
/// CLEAR_COLOR as it draws it.
constexpr uint32_t ClearPixel(uint32_t clear_attr1) {
    const auto expand = [](uint32_t five) { return five == 0 ? 0 : five * 2 + 1; };
    return expand(clear_attr1 & 0x1F) | expand((clear_attr1 >> 5) & 0x1F) << 8 |
           expand((clear_attr1 >> 10) & 0x1F) << 16 | ((clear_attr1 >> 16) & 0x1F) << 24;
}

/// A pixel of the 3D renderer with fog of this density (0 to 128) over it.
constexpr uint32_t FogPixel(uint32_t pixel, uint32_t fog_color, uint32_t density, bool alpha_only) {
    const uint32_t fog = ClearPixel(fog_color);
    uint32_t out = 0;
    for (int at = 0; at < 32; at += 8) {
        const uint32_t from = (pixel >> at) & 0x3F;
        const uint32_t to = (fog >> at) & 0x3F;
        out |= (alpha_only && at < 24 ? from : (to * density + from * (128 - density)) >> 7) << at;
    }
    return out;
}

/// Until a game has a scene to take its view from.
constexpr int32_t kConvergence = 4 * kOneW;

struct Depth {
    bool on = false; ///< the right eye is drawn
    LayerShifts layers{};
    int32_t convergence = 0; ///< a fixed one for the game; 0: the scene's own view (ViewTrack)
    constexpr bool operator==(const Depth&) const = default;
};

/// The one place that turns the level (0 to 3), the limit from the IPD and the screen's width
/// (Common::LayerDepth::MaxShift), Invert Depth and a game's fixed convergence, if it has one, into
/// shifts.
constexpr Depth MakeDepth(uint32_t level, float max_shift, bool inverted = false,
                          int32_t convergence = 0) {
    Depth depth;
    depth.on = level > 0 && max_shift > 0.0f;
    if (depth.on) {
        for (uint32_t priority = 0; priority < 4; ++priority) {
            depth.layers[priority] = Common::LayerDepth::Directed(
                Common::LayerDepth::LayerShift(level, priority, 3, kWidth, max_shift), inverted);
        }
        depth.convergence = convergence;
    }
    return depth;
}

/// Lines of a right picture: drawn at all, not copied from the left one, and of those with 3D of
/// its own.
struct Lines {
    int drawn = 0;
    int moved = 0;
    int with_3d = 0;
    int32_t convergence = 0; ///< the scene's, of the frame; 0 without one
};

} // namespace FrameCores::NdsDepth
