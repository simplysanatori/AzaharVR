// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiu140: Kid Icarus: Uprising's HUD in Angel's Aim, whole pieces moved into the view.
//
// First person's picture is the game's frame over the whole view (126 x 112 degrees), so the HUD
// the game puts at its frame's edges was 40 to 60 degrees aside and as flat as the frame is
// tall (the game's 5:3 frame drawn 1.32 wide); HUD Spacing scaled vertex by vertex and left those
// on the frame's edge behind (vr_hud_spacing.h), which stretched the pieces that touch it.
//
// The game's flat 2D draws on the top screen (a desktop census, Chapter 1 air: ten small draws an
// eye, no depth test, source alpha blending, textures from its own memory) are shaded on the CPU
// and drawn as a smaller frame of the game's own shape, placed in the view: straight pieces,
// nothing pinned. A draw that covers the frame (a fade), or copies a picture the GPU drew (its
// texture in VRAM: wipes, the game's glow passes), and one with any vertex not flat (marks in the
// world) stays as it is.
//
// Clip space as the vertex shader has it: x is up and down, y across (the top screen is drawn
// turned); +x up and +y to the LEFT (a census, Chapter 2: the wing of the health bar at y 0.8 to 1,
// the score and the battery at -1). Here across is counted to the right: -y.
//
// Pure, unit tested (src/tests/frame_xr/kiu_hud.cpp); the XR thread publishes the place
// (frame_xr/immersive.cpp) and the rasterizer moves the vertices (TurnSoftwareVertices).

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace VideoCore::KiuHud {

/// What the settings ask for: degrees to the right and up from straight ahead.
struct Wanted {
    int size = 100;
    bool linked = true;
    float across = 0.0f, height = 22.0f; ///< linked
    float bar_across = -20.0f, bar_height = 22.0f;
    float score_across = 20.0f, score_height = 22.0f;
    float subtitle_height = -18.0f;
    int subtitle_size = 130;
};

/// How the HUD's frame is drawn, in the screen's own coordinates (-1 to 1 over the picture shown):
/// half sizes, its middle's height, and each top group's offset (where the frame's middle would
/// be if it were all that group).
struct Place {
    bool on = false;
    bool page = false; ///< not the HUD: a page's 2D, whole, over the game's own frame (PagePlace)
    bool cards = false; ///< the hub's: its cards go with its 2D (MayBeCard)
    float half_across = 1.0f, half_up = 1.0f;
    float middle_up = 0.0f;
    float bar_across = 0.0f, bar_up = 0.0f;
    float score_across = 0.0f, score_up = 0.0f;
    float subtitle_up = 0.0f, subtitle_half_across = 0.0f, subtitle_half_up = 0.0f;
};

/// Degrees across at HUD Size 100 %, and the height of the frame's middle.
inline constexpr float kWidthDeg = 60.0f, kMiddleDeg = 6.0f;
inline constexpr float kFrameAspect = 400.0f / 240.0f;
/// The two top groups in the frame, across to the right (a desktop picture: the bar from -1 to -0.33, the score from
/// 0.58 to 1, both about 0.86 up), and what a draw is told from them by.
inline constexpr float kTopUp = 0.6f, kBarAside = -0.2f, kScoreAside = 0.4f;
inline constexpr float kBarLeft = -1.0f, kBarRight = -0.33f;
inline constexpr float kScoreLeft = 0.58f, kScoreRight = 1.0f;
inline constexpr float kGroupUp = 0.86f, kLinkGap = 0.08f;
/// Pieces in the frame's lower part go this much (half frames) further down: clear of the reticle.
inline constexpr float kLow = -0.35f, kLowDown = 0.7f;

/// The place for the settings and the screen first person shows (half angles' tangents, and its
/// own pitch, up positive).
inline Place PlaceFor(const Wanted& w, float tan_half_x, float tan_half_y, float pitch_rad) {
    Place p;
    if (!(tan_half_x > 0.05f) || !(tan_half_y > 0.05f) || !std::isfinite(pitch_rad)) {
        return p;
    }
    constexpr float kDeg = 3.14159265f / 180.0f;
    const float s = static_cast<float>(std::clamp(w.size, 40, 200)) / 100.0f;
    const float t = std::tan(0.5f * kWidthDeg * kDeg) * s;
    const auto across_at = [&](float deg) {
        return std::tan(std::clamp(deg, -50.0f, 50.0f) * kDeg) / tan_half_x;
    };
    const auto up_at = [&](float deg) {
        return std::tan(std::clamp(std::clamp(deg, -50.0f, 50.0f) * kDeg - pitch_rad, -1.2f, 1.2f)) /
               tan_half_y;
    };
    p.on = true;
    p.half_across = t / tan_half_x;
    p.half_up = t / kFrameAspect / tan_half_y;
    p.middle_up = up_at(kMiddleDeg);
    const float st = std::tan(0.5f * kWidthDeg * kDeg) *
                     static_cast<float>(std::clamp(w.subtitle_size, 50, 250)) / 100.0f;
    p.subtitle_half_across = st / tan_half_x;
    p.subtitle_half_up = st / kFrameAspect / tan_half_y;
    p.subtitle_up = up_at(w.subtitle_height);
    constexpr float kBarMiddle = 0.5f * (kBarLeft + kBarRight);
    constexpr float kScoreMiddle = 0.5f * (kScoreLeft + kScoreRight);
    if (w.linked) {
        // One unit, its middle where it is asked for: the bar, the gap, the score.
        constexpr float kBarWide = kBarRight - kBarLeft, kScoreWide = kScoreRight - kScoreLeft;
        constexpr float kLeft = -0.5f * (kBarWide + kLinkGap + kScoreWide);
        p.bar_across = across_at(w.across) + p.half_across * (kLeft - kBarLeft);
        p.score_across =
            across_at(w.across) + p.half_across * (kLeft + kBarWide + kLinkGap - kScoreLeft);
        p.bar_up = p.score_up = up_at(w.height) - p.half_up * kGroupUp;
    } else {
        p.bar_across = across_at(w.bar_across) - p.half_across * kBarMiddle;
        p.bar_up = up_at(w.bar_height) - p.half_up * kGroupUp;
        p.score_across = across_at(w.score_across) - p.half_across * kScoreMiddle;
        p.score_up = up_at(w.score_height) - p.half_up * kGroupUp;
    }
    return p;
}

/// Outside a battle (the results, menus): the 2D is one page, drawn over the frame the game's own
/// camera has in a picture `factor` times past it. Each piece as the others, so none is left at
/// the edges of the whole view.
inline Place PagePlace(float factor_across, float factor_up) {
    Place p;
    if (!(factor_across >= 1.0f) || !(factor_up >= 1.0f)) {
        return p;
    }
    p.on = p.page = true;
    p.half_across = 1.0f / factor_across;
    p.half_up = 1.0f / factor_up;
    return p;
}

/// AzaharFrame kiu1s: a page while the battle's camera is still in (the results as they start): the
/// game's frame is then the whole view, so the page gets the frame the game's own camera has (45
/// degrees high), as PagePlace gives it once that camera is out.
inline constexpr float kPageHighDeg = 45.0f;
inline Place PagePlaceInBattle(float tan_half_x, float tan_half_y) {
    Place p;
    if (!(tan_half_x > 0.05f) || !(tan_half_y > 0.05f)) {
        return p;
    }
    const float t = std::tan(0.5f * kPageHighDeg * 3.14159265f / 180.0f);
    p.on = p.page = true;
    p.half_up = std::min(t / tan_half_y, 1.0f);
    p.half_across = std::min(t * kFrameAspect / tan_half_x, 1.0f);
    return p;
}

/// AzaharFrame kiuhud: a menu's 2D (the hub's hearts, words and battery): the game's own layout,
/// whole, in the frame the battle's HUD has (HUD Size), its middle straight ahead at the HUD's
/// height: in the view and readable, where the game's own frame is 2.8 to 5 times smaller there.
inline Place PageInView(const Wanted& w, float tan_half_x, float tan_half_y, float pitch_rad) {
    Place p = PlaceFor(w, tan_half_x, tan_half_y, pitch_rad);
    p.page = p.cards = p.on;
    return p;
}

/// The state of a draw that may be the HUD's.
struct Draw {
    bool top_viewport = false; ///< 240 x 400
    bool depth_test = false;
    bool blended = false; ///< source alpha, one minus source alpha
    bool textured = false;
    uint32_t texture = 0; ///< physical address
    uint32_t vertices = 0;
    uint32_t width = 0, height = 0, format = 0; ///< texture 0's
};
inline constexpr uint32_t kVramStart = 0x18000000, kVramEnd = 0x18600000;
inline constexpr uint32_t kMaxVertices = 1024;
constexpr bool MayBeHud(const Draw& d) {
    return d.top_viewport && !d.depth_test && d.blended && d.textured && d.vertices != 0 &&
           d.vertices <= kMaxVertices && !(d.texture >= kVramStart && d.texture < kVramEnd);
}

/// The subtitles: two quads of an image drawn into video memory first (a desktop draw log,
/// Chapter 2's climb: 512 x 128, RGBA8, the only blended draw of one).
constexpr bool IsSubtitle(const Draw& d) {
    return d.top_viewport && !d.depth_test && d.blended && d.textured && d.vertices == 12 &&
           d.texture >= kVramStart && d.texture < kVramEnd && d.width == 512 && d.height == 128 &&
           d.format == 0;
}

/// The same texture drawn as other than one line and its shadow (the results' list): a piece.
constexpr bool IsText(const Draw& d) {
    return d.top_viewport && !d.depth_test && d.blended && d.textured && d.vertices >= 6 &&
           d.vertices <= kMaxVertices && d.vertices % 6 == 0 && d.texture >= kVramStart &&
           d.texture < kVramEnd && d.width == 512 && d.height == 128 && d.format == 0;
}
/// A viewport text is drawn into a texture with: wide, where the screens' (turned) are tall.
constexpr bool IsTextTarget(float width, float height) {
    return width > height && height > 0.0f;
}

/// AzaharFrame kiu1s: the battle UI's own 2D batch (frame_xr/kiu_ui.h): every draw of the game's
/// 2D context names its first quad by its index offset, off one vertex base, so a draw of it is
/// known exactly, whatever its blend or texture. Off: not known, the guess above decides.
struct Batch {
    uint32_t vertex_base = 0, index_offset = 0; ///< physical; bytes from the base
    uint32_t quads = 0;                         ///< 0: not known
};
constexpr bool InBatch(const Batch& b, uint32_t vertex_base, uint32_t index_offset,
                       uint32_t vertices) {
    if (b.quads == 0 || vertex_base != b.vertex_base || index_offset < b.index_offset ||
        vertices == 0 || vertices % 6 != 0) {
        return false;
    }
    const uint32_t at = index_offset - b.index_offset; // 6 indices of 2 bytes a quad
    return at % 12 == 0 && at / 12 + vertices / 6 <= b.quads;
}

/// The console's battery (a desktop draw log, air and land: one quad, 256 x 128, format 9): the
/// dialogs' 2D, not the battle's, so never in the batch. The guess still moves it with the score, else
/// it would be left large in the corner of the whole view.
constexpr bool IsBattery(const Draw& d) {
    return MayBeHud(d) && d.vertices == 6 && d.width == 256 && d.height == 128 && d.format == 9;
}

/// AzaharFrame kiuhud: what a draw of the top screen is, by what is known of the game's 2D.
enum class As : uint8_t {
    Not,       ///< not the 2D's: as the game draws it (the world, a pass over the whole picture)
    Guess,     ///< the batch is not known: a quad at a time, by where each is (the old way)
    Piece,     ///< a whole draw of the battle UI's batch: one piece, never apart
    WithScore, ///< whole, with the score (the console's battery)
    Subtitle,
    Page,      ///< a page's (the results, a menu): the page's frame, every quad alike
    Card,      ///< a menu's, drawn as a card in its room (the hub's veil and heart bag): as Page
};
/// `in_batch`: the draw is the battle UI's own (InBatch); `batch_known`: its batch is known at all;
/// `page`: the 2D on view is a page, not the HUD; `reticle`: the hand's reticle, never the HUD's.
constexpr As Decide(const Draw& d, bool in_batch, bool batch_known, bool page, bool reticle) {
    if (reticle) {
        return As::Not;
    }
    if (page) {
        // The battle UI's page whatever its blend or texture (its lines have none); another
        // context's (a menu's words, the battery) by the guess.
        return in_batch || IsText(d) || MayBeHud(d) ? As::Page : As::Not;
    }
    if (batch_known) {
        return IsSubtitle(d) ? As::Subtitle
               : in_batch    ? As::Piece
               : IsBattery(d) ? As::WithScore
                              : As::Not;
    }
    return IsSubtitle(d)               ? As::Subtitle
           : IsBattery(d)              ? As::WithScore
           : IsText(d) || MayBeHud(d) ? As::Guess
                                       : As::Not;
}

struct Vertex {
    float x, y, z, w; ///< clip: x up, y across to the left
};
/// Whether these vertices (a triangle list) are a piece to move: all flat, not the whole frame.
inline bool Movable(const Vertex* v, std::size_t n) {
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (std::size_t i = 0; i < n; ++i) {
        if (!(std::abs(v[i].w - 1.0f) < 1e-4f) || !std::isfinite(v[i].x) ||
            !std::isfinite(v[i].y)) {
            return false;
        }
        x0 = std::min(x0, v[i].x);
        x1 = std::max(x1, v[i].x);
        y0 = std::min(y0, v[i].y);
        y1 = std::max(y1, v[i].y);
    }
    return n >= 3 && !(x1 - x0 > 1.9f && y1 - y0 > 1.9f);
}
/// Where a piece goes: with the health bar, with the score, the frame's lower part (further
/// down), or the frame's middle part.
enum class Group : uint8_t { Middle, Bar, Score, Low };
/// By a middle (the guess: a quad's; the batch: a whole draw's).
constexpr Group GroupAt(float up, float across) {
    return up > kTopUp && across < kBarAside     ? Group::Bar
           : up > kTopUp && across > kScoreAside ? Group::Score
           : up < kLow                           ? Group::Low
                                                 : Group::Middle;
}
/// A group's vertices moved; a page's all go as one (the group is not asked).
inline void MoveAs(Vertex* t, std::size_t n, const Place& p, float factor_across, float factor_up,
                   Group group) {
    float at_across = 0.0f, at_up = p.middle_up, rise = 0.0f; // a page's middle_up: PagePlace 0
    if (p.page) {
    } else if (group == Group::Bar) {
        at_across = p.bar_across;
        at_up = p.bar_up;
    } else if (group == Group::Score) {
        at_across = p.score_across;
        at_up = p.score_up;
    } else if (group == Group::Low) {
        rise = -kLowDown;
    }
    for (std::size_t i = 0; i < n; ++i) {
        Vertex& v = t[i];
        const float u = at_up + p.half_up * (v.x + rise);
        const float a = at_across - p.half_across * v.y; // to the right
        v = {2.0f * u * factor_up, -2.0f * a * factor_across, 2.0f * v.z, 2.0f};
    }
}
/// `up`, `across`: the middle that decides its group.
inline void MoveBy(Vertex* t, std::size_t n, const Place& p, float factor_across, float factor_up,
                   float up, float across) {
    MoveAs(t, n, p, factor_across, factor_up, GroupAt(up, across));
}
/// One piece moved: a quad (two triangles in a row) or a triangle, by its middle. `factor`s: what
/// the vertex shader divides by (the game drawing past its frame). The result has w = 2, so the
/// shader's rule for flat vertices leaves it alone.
inline void Move(Vertex* t, std::size_t n, const Place& p, float factor_across, float factor_up) {
    float up = 0.0f, across = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        up += t[i].x / static_cast<float>(n);
        across -= t[i].y / static_cast<float>(n);
    }
    MoveBy(t, n, p, factor_across, factor_up, up, across);
}

/// AzaharFrame kiu1s: a whole draw of the battle UI's batch moved as one (the game draws a piece, or
/// a part of one, a draw): its group by its own middle, so no quad of it goes elsewhere than the rest.
inline void MovePiece(Vertex* v, std::size_t n, const Place& p, float factor_across,
                      float factor_up) {
    float u0 = 1e9f, u1 = -1e9f, a0 = 1e9f, a1 = -1e9f;
    for (std::size_t i = 0; i < n; ++i) {
        u0 = std::min(u0, v[i].x);
        u1 = std::max(u1, v[i].x);
        a0 = std::min(a0, v[i].y);
        a1 = std::max(a1, v[i].y);
    }
    MoveBy(v, n, p, factor_across, factor_up, 0.5f * (u0 + u1), -0.5f * (a0 + a1));
}

/// AzaharFrame kiuhud: a page's draw: each quad (or triangle) that is flat and not the whole frame
/// goes to the page's frame; one over the whole frame (the dim behind the page) stays over the
/// whole view: its corners exactly at the frame's, where the shader keeps a flat vertex (VrFactor).
/// All go the same way, so a draw of a band and of small pieces stays right.
/// Returns how many vertices were placed.
inline std::size_t MovePage(Vertex* v, std::size_t n, const Place& p, float factor_across,
                            float factor_up) {
    const std::size_t step = n % 6 == 0 ? 6 : 3;
    std::size_t moved = 0;
    for (std::size_t i = 0; i + step <= n; i += step) {
        if (Movable(v + i, step)) {
            MoveAs(v + i, step, p, factor_across, factor_up, Group::Middle);
            moved += step;
            continue;
        }
        bool frame = step == 6; // flat, every corner near the frame's
        for (std::size_t k = i; frame && k < i + step; ++k) {
            frame = std::abs(v[k].w - 1.0f) < 1e-4f && std::abs(v[k].x) > 0.9f &&
                    std::abs(v[k].y) > 0.9f && std::isfinite(v[k].x) && std::isfinite(v[k].y);
        }
        for (std::size_t k = i; frame && k < i + step; ++k) {
            v[k].x = std::copysign(1.0f, v[k].x);
            v[k].y = std::copysign(1.0f, v[k].y);
            ++moved;
        }
    }
    return moved;
}

/// The subtitles' quads (the line and its shadow) moved whole, their middle straight ahead at the
/// height asked for, in the size asked for. They may cover the whole frame. False (nothing moved)
/// for quads that are not flat.
inline bool MoveSubtitle(Vertex* v, std::size_t n, const Place& p, float factor_across,
                         float factor_up) {
    float u0 = 1e9f, u1 = -1e9f, a0 = 1e9f, a1 = -1e9f;
    for (std::size_t i = 0; i < n; ++i) {
        if (!(v[i].w > 1e-4f) || std::abs(v[i].w - v[0].w) > 1e-3f * v[0].w ||
            !std::isfinite(v[i].x) || !std::isfinite(v[i].y)) {
            return false;
        }
        u0 = std::min(u0, v[i].x / v[i].w);
        u1 = std::max(u1, v[i].x / v[i].w);
        a0 = std::min(a0, v[i].y / v[i].w);
        a1 = std::max(a1, v[i].y / v[i].w);
    }
    const float mid_up = 0.5f * (u0 + u1), mid_across = 0.5f * (a0 + a1);
    for (std::size_t i = 0; i < n; ++i) {
        const float up = v[i].x / v[i].w - mid_up, across = v[i].y / v[i].w - mid_across;
        const float z = v[i].z / v[i].w;
        v[i] = {2.0f * (p.subtitle_up + p.subtitle_half_up * up) * factor_up,
                2.0f * p.subtitle_half_across * across * factor_across, 2.0f * z, 2.0f};
    }
    return n != 0;
}

/// AzaharFrame kiuhud: the hub's veil and heart bag: cards in front of the room's camera (a census
/// of the hub: 64 x 64, RGBA4, blended, depth tested, 6 or 12 vertices at one distance, 40 units;
/// nothing else of the room is), where their words and number are flat 2D.
constexpr bool MayBeCard(const Draw& d) {
    return d.top_viewport && d.depth_test && d.blended && d.textured && d.width == 64 &&
           d.height == 64 && d.format == 4 && (d.vertices == 6 || d.vertices == 12);
}
/// Its vertices: in front, all at one distance, not flat.
inline bool IsCard(const Vertex* v, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (!(v[i].w > 1.5f) || std::abs(v[i].w - v[0].w) > 0.02f * v[0].w ||
            !std::isfinite(v[i].x) || !std::isfinite(v[i].y)) {
            return false;
        }
    }
    return n != 0;
}
/// To the page's frame. `nearest`: the depth (z over w) nothing is in front of; the room must not
/// hide a card that follows the view, and what was in front of what stays so.
inline constexpr float kCardDepth = 0.02f;
inline void MoveCard(Vertex* t, std::size_t n, const Place& p, float factor_across,
                     float factor_up, float nearest) {
    for (std::size_t i = 0; i < n; ++i) {
        Vertex& v = t[i];
        const float w = v.w;
        const float z = nearest + (v.z / w - nearest) * kCardDepth;
        const float u = p.middle_up + p.half_up * (v.x / w);
        const float a = p.half_across * (v.y / w);
        v = {w * u * factor_up, w * a * factor_across, w * z, w};
    }
}
/// A card across more than the frame (the veil): its ends and its foot are off the game's screen.
inline bool IsVeil(const Vertex* v, std::size_t n) {
    float a0 = 1e9f, a1 = -1e9f;
    for (std::size_t i = 0; i < n; ++i) {
        a0 = std::min(a0, v[i].y / v[i].w);
        a1 = std::max(a1, v[i].y / v[i].w);
    }
    return n == 12 && a0 < -1.0f && a1 > 1.0f;
}
/// The veil again, with no border: as wide, its middle as dark, all four sides fading to nothing.
/// Its height is about the words' (0.7 to 0.9 down): the game's hangs off the screen's foot. 54 vertices (a triangle list); `alpha`: 0 on the rim, 1 inside.
inline constexpr float kVeilAbove = 0.12f, kVeilHigh = 0.5f;
struct VeilVertex {
    Vertex v;
    float alpha;
};
inline std::array<VeilVertex, 54> SoftVeil(const Vertex* in, std::size_t n) {
    float u0 = 1e9f, u1 = -1e9f, a0 = 1e9f, a1 = -1e9f;
    for (std::size_t i = 0; i < n; ++i) {
        u0 = std::min(u0, in[i].x / in[i].w);
        u1 = std::max(u1, in[i].x / in[i].w);
        a0 = std::min(a0, in[i].y / in[i].w);
        a1 = std::max(a1, in[i].y / in[i].w);
    }
    const float z = in[0].z / in[0].w, w = in[0].w;
    constexpr float kSide = 0.3f, kEnd = 0.3f; // of the width, of the height
    u1 += kVeilAbove;
    u0 = std::max(u0, u1 - kVeilHigh);
    const float us[4] = {u0, u0 + kEnd * (u1 - u0), u1 - kEnd * (u1 - u0), u1};
    const float as[4] = {a0, a0 + kSide * (a1 - a0), a1 - kSide * (a1 - a0), a1};
    const auto at = [&](int i, int j) {
        const bool rim = i == 0 || i == 3 || j == 0 || j == 3;
        return VeilVertex{{us[i] * w, as[j] * w, z * w, w}, rim ? 0.0f : 1.0f};
    };
    std::array<VeilVertex, 54> out;
    std::size_t k = 0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            // A corner's cut runs from its inner point out, so it fades round.
            const bool other = (i == 0) == (j == 2) && i != 1 && j != 1;
            const int tri[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
            const int alt[6][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 0}, {1, 1}, {0, 1}};
            for (int c = 0; c < 6; ++c) {
                const auto& q = other ? alt[c] : tri[c];
                out[k++] = at(i + q[0], j + q[1]);
            }
        }
    }
    return out;
}

/// The place in force. One writer (the XR thread, once a frame), read by the emulation thread a
/// draw; a read across a write mixes two frames' numbers, which are the same or next to it.
struct Live {
    std::array<std::atomic<float>, 10> f{};
    std::atomic<bool> on{false}, page{false}, cards{false};

    void Set(const Place& p) {
        const float v[10] = {p.half_across, p.half_up,      p.middle_up, p.bar_across,
                            p.bar_up,      p.score_across, p.score_up,  p.subtitle_up,
                             p.subtitle_half_across, p.subtitle_half_up};
        for (int i = 0; i < 10; ++i) {
            f[i].store(v[i], std::memory_order_relaxed);
        }
        page.store(p.page, std::memory_order_relaxed);
        cards.store(p.cards, std::memory_order_relaxed);
        on.store(p.on, std::memory_order_release);
    }
    Place Get() const {
        Place p;
        p.on = on.load(std::memory_order_acquire);
        p.page = page.load(std::memory_order_relaxed);
        p.cards = cards.load(std::memory_order_relaxed);
        float* const v[10] = {&p.half_across, &p.half_up,      &p.middle_up, &p.bar_across,
                             &p.bar_up,      &p.score_across, &p.score_up,  &p.subtitle_up,
                             &p.subtitle_half_across, &p.subtitle_half_up};
        for (int i = 0; i < 10; ++i) {
            *v[i] = f[i].load(std::memory_order_relaxed);
        }
        return p;
    }
};
inline Live g_live;

/// AzaharFrame kiu1s: the batch in force (AZAHARFRAME_KIU_HUD_EXACT=1; the emulation thread sets it
/// each VBlank and reads it at each draw).
struct LiveBatch {
    std::atomic<uint32_t> vertex_base{0}, index_offset{0}, quads{0};

    void Set(const Batch& b) {
        vertex_base.store(b.vertex_base, std::memory_order_relaxed);
        index_offset.store(b.index_offset, std::memory_order_relaxed);
        quads.store(b.quads, std::memory_order_relaxed);
    }
    Batch Get() const {
        return {vertex_base.load(std::memory_order_relaxed),
                index_offset.load(std::memory_order_relaxed),
                quads.load(std::memory_order_relaxed)};
    }
};
inline LiveBatch g_batch;

} // namespace VideoCore::KiuHud
