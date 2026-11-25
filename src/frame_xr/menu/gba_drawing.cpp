// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <array>

#include "frame_xr/menu/ds_drawing.h"

namespace FrameXr::Menu {

namespace {

// A Game Boy Advance from the front in its own units (145 across, about a millimetre), as the 3DS
// is drawn (ds_drawing.cpp): the wide shell with the shoulder buttons behind its top edge, the dark
// surround with the screen, the D-pad over Start and Select on the left, B and A on the right with
// the power light over them and the speaker's slots under them.
constexpr float kLine = 1.5f;   // dp: outlines
constexpr float kDetail = 1.0f; // dp: bevels, frames
constexpr uint32_t kInk = Tok::kOnSurfaceVariant;
constexpr uint32_t kShell = Tok::kSurfaceContainer;
constexpr uint32_t kPart = Tok::kSurfaceContainerHigh;
constexpr uint32_t kBezel = 0x15181C;
constexpr uint32_t kGlass = 0x0B0E12;

struct U {
    float x0, y0, x1, y1;
};

constexpr U kBody{0.5f, 5.5f, 144.5f, 81.5f};
constexpr float kBodyTop = 20.0f, kBodyBottom = 31.0f; // the corners
constexpr U kShoulderL{9.0f, 1.0f, 48.0f, 16.0f};
constexpr U kBezelAt{32.6f, 9.6f, 112.4f, 71.0f};
constexpr float kBezelTop = 3.0f, kBezelBottom = 12.0f;
constexpr U kScreen{41.0f, 15.5f, 104.0f, 57.5f}; // 3:2
constexpr DsPoint kDpad{18.6f, 37.0f};
constexpr float kDpadArm = 10.0f, kDpadHalf = 3.3f;
constexpr DsPoint kA{133.0f, 35.0f};
constexpr DsPoint kB{120.0f, 39.4f};
constexpr float kFaceR = 4.9f;
constexpr DsPoint kStart{26.4f, 56.2f};
constexpr DsPoint kSelect{26.4f, 64.4f};
constexpr float kSmallR = 2.3f;
constexpr U kSpeaker{117.0f, 54.5f, 131.0f, 66.5f};
constexpr int kSpeakerSlots = 6;

constexpr U Mirror(const U& u) {
    return {kGbaUnitsW - u.x1, u.y0, kGbaUnitsW - u.x0, u.y1};
}
constexpr DsPoint Shoulder(const U& u) { // what shows of it
    return {(u.x0 + u.x1) * 0.5f, (u.y0 + kBody.y0) * 0.5f};
}

} // namespace

DsPoint GbaUnitAnchor(int target) {
    switch (target) {
    case DsButton::A:
    case DsButton::X:
        return kA;
    case DsButton::B:
    case DsButton::Y:
        return kB;
    case DsButton::Up:
        return {kDpad.x, kDpad.y - kDpadArm + 3.2f};
    case DsButton::Down:
        return {kDpad.x, kDpad.y + kDpadArm - 3.2f};
    case DsButton::Left:
        return {kDpad.x - kDpadArm + 3.2f, kDpad.y};
    case DsButton::Right:
        return {kDpad.x + kDpadArm - 3.2f, kDpad.y};
    case DsButton::L:
        return Shoulder(kShoulderL);
    case DsButton::R:
        return Shoulder(Mirror(kShoulderL));
    case DsButton::Start:
        return kStart;
    case DsButton::Select:
        return kSelect;
    default:
        return {kGbaUnitsW * 0.5f, (kScreen.y0 + kScreen.y1) * 0.5f};
    }
}

float GbaUnitRadius(int target) {
    switch (target) {
    case DsButton::A:
    case DsButton::B:
    case DsButton::X:
    case DsButton::Y:
        return kFaceR;
    case DsButton::Start:
    case DsButton::Select:
        return kSmallR;
    default:
        return 3.3f;
    }
}

void DrawGba(Kit& kit, const DsDrawing& d) {
    const float s = d.s;
    const auto X = [&](float x) { return d.box.x0 + x * s; };
    const auto Y = [&](float y) { return d.box.y0 + y * s; };
    const auto R = [&](const U& u) { return Rect{X(u.x0), Y(u.y0), X(u.x1), Y(u.y1)}; };
    const auto shape = [&](const U& u, float radius, uint32_t fill) {
        kit.Fill(R(u), radius * s, fill);
        kit.Stroke(R(u), radius * s, kLine, kInk);
    };
    const auto circle = [&](DsPoint c, float r) {
        shape({c.x - r, c.y - r, c.x + r, c.y + r}, r, kPart);
    };
    // Corners of two sizes: the outline is the ink's shape with the fill's inside it.
    const auto shell = [&](const U& u, float m, float top, float bottom, float line, uint32_t fill,
                           float ink) {
        Rect r = R({u.x0 + m, u.y0 + m, u.x1 - m, u.y1 - m});
        float t = (top - m) * s, b = (bottom - m) * s;
        kit.FillCorners(r, t, t, b, b, kInk, ink);
        r = {r.x0 + line, r.y0 + line, r.x1 - line, r.y1 - line};
        t -= line;
        b -= line;
        kit.FillCorners(r, t, t, b, b, fill);
    };

    shape(kShoulderL, 6.0f, kPart);
    shape(Mirror(kShoulderL), 6.0f, kPart);
    shell(kBody, 0.0f, kBodyTop, kBodyBottom, kLine, kShell, 1.0f);
    shell(kBody, 2.0f, kBodyTop, kBodyBottom, kDetail, kShell, 0.3f);
    shell(kBezelAt, 0.0f, kBezelTop, kBezelBottom, kDetail, kBezel, 0.5f);
    kit.Fill(R(kScreen), 0.8f * s, kGlass);
    kit.Stroke(R(kScreen), 0.8f * s, kDetail, kInk, 0.55f);

    {
        const float a = kDpadArm, h = kDpadHalf, cx = kDpad.x, cy = kDpad.y;
        kit.Fill(R({cx - a, cy - h, cx + a, cy + h}), 0.8f * s, kPart);
        kit.Fill(R({cx - h, cy - a, cx + h, cy + a}), 0.8f * s, kPart);
        const std::array<DsPoint, 12> p{{{cx - h, cy - a},
                                         {cx + h, cy - a},
                                         {cx + h, cy - h},
                                         {cx + a, cy - h},
                                         {cx + a, cy + h},
                                         {cx + h, cy + h},
                                         {cx + h, cy + a},
                                         {cx - h, cy + a},
                                         {cx - h, cy + h},
                                         {cx - a, cy + h},
                                         {cx - a, cy - h},
                                         {cx - h, cy - h}}};
        for (std::size_t i = 0; i < p.size(); i++) {
            const DsPoint& q = p[i];
            const DsPoint& n = p[(i + 1) % p.size()];
            kit.Line(X(q.x), Y(q.y), X(n.x), Y(n.y), kLine, kInk);
        }
        kit.Stroke(R({cx - 1.4f, cy - 1.4f, cx + 1.4f, cy + 1.4f}), 1.4f * s, kDetail, kInk, 0.35f);
    }
    circle(kA, kFaceR);
    circle(kB, kFaceR);
    circle(kStart, kSmallR);
    circle(kSelect, kSmallR);
    kit.Dot(X(120.0f), Y(17.5f), 1.1f * s, kInk, 0.7f);
    for (int i = 0; i < kSpeakerSlots; i++) {
        const float y = kSpeaker.y0 + (kSpeaker.y1 - kSpeaker.y0) * static_cast<float>(i) /
                                          static_cast<float>(kSpeakerSlots - 1);
        kit.Line(X(kSpeaker.x0), Y(y), X(kSpeaker.x1), Y(y), kLine, kInk, 0.65f);
    }
}

} // namespace FrameXr::Menu
