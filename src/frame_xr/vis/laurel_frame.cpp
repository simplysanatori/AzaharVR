// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_xr/vis/laurel_frame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace FrameXr::Vis {

namespace {

#include "frame_xr/vis/laurel_frame_data.inc"

struct V3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

V3 Unit(V3 v) {
    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return l > 1e-12f ? V3{v.x / l, v.y / l, v.z / l} : V3{0.0f, 0.0f, 1.0f};
}

/// A tangent square to the normal (the surface image is flat: any will do).
V3 TangentFor(V3 n) {
    const V3 a = std::abs(n.x) < 0.9f ? V3{1, 0, 0} : V3{0, 1, 0};
    return Unit({a.y * n.z - a.z * n.y, a.z * n.x - a.x * n.z, a.x * n.y - a.y * n.x});
}

void Push(std::vector<float>& out, V3 p, V3 n, PitHandMaterial material, float shade) {
    const V3 t = TangentFor(n);
    const float u = (0.5f + shade * static_cast<float>(kPitPaletteWidth - 1)) / static_cast<float>(kPitPaletteWidth);
    const float v = (static_cast<float>(material) * 8.0f + 1.0f) / static_cast<float>(kPitPaletteHeight);
    out.insert(out.end(), {p.x, p.y, p.z, n.x, n.y, n.z, t.x, t.y, t.z, 1.0f, u, v});
}

LaurelMesh Build() {
    LaurelMesh m;
    m.vertices.reserve(static_cast<std::size_t>(kLaurelVertexCount) * kPitVertexFloats);
    for (uint32_t v = 0; v < kLaurelVertexCount; v++) {
        const V3 p{kLaurelPosMin[0] + kLaurelPosScale[0] * static_cast<float>(kLaurelPos[v * 3]),
                   kLaurelPosMin[1] + kLaurelPosScale[1] * static_cast<float>(kLaurelPos[v * 3 + 1]),
                   kLaurelPosMin[2] + kLaurelPosScale[2] * static_cast<float>(kLaurelPos[v * 3 + 2])};
        const V3 n = Unit({static_cast<float>(kLaurelNormal[v * 3]), static_cast<float>(kLaurelNormal[v * 3 + 1]),
                           static_cast<float>(kLaurelNormal[v * 3 + 2])});
        Push(m.vertices, p, n, static_cast<PitHandMaterial>(kLaurelMaterial[v]),
             static_cast<float>(kLaurelShade[v]) / 255.0f);
    }
    m.indices.assign(std::begin(kLaurelIndex), std::end(kLaurelIndex));
    for (const LaurelPartData& p : kLaurelParts) {
        m.parts.push_back({p.name, static_cast<PitHandMaterial>(p.material), p.first_index, p.index_count});
    }
    return m;
}

/// One strap: a closed band round the ellipse (half axes `rz` along z, `ry` along y, centred
/// at z = `cz`) from x0 to x1, `thick` thick.
void Strap(LaurelMesh& m, float x0, float x1, float rz, float ry, float cz, float thick) {
    constexpr int kSegments = 28;
    constexpr float kTwoPi = 6.28318530718f;
    const auto base = [&m] { return static_cast<uint32_t>(m.vertices.size() / kPitVertexFloats); };
    // Four faces, each with its own vertices (hard edges): outer, inner, the two sides.
    struct Face {
        float xa, xb;   // x at the face's two rails
        float ra, rb;   // 0 inner radius, 1 outer, at the two rails
        V3 n;           // the normal: x part, and how much of the radial direction (y)
        float shade;
    };
    const std::array<Face, 4> faces{{{x0, x1, 1, 1, {0, 1, 0}, 1.0f},
                                     {x1, x0, 0, 0, {0, -1, 0}, 0.7f},
                                     {x0, x0, 0, 1, {-1, 0, 0}, 0.85f},
                                     {x1, x1, 1, 0, {1, 0, 0}, 0.85f}}};
    for (const Face& f : faces) {
        const uint32_t first = base();
        for (int i = 0; i < kSegments; i++) {
            const float a = kTwoPi * static_cast<float>(i) / static_cast<float>(kSegments);
            const float c = std::cos(a), s = std::sin(a);
            // The ellipse's outward normal.
            const V3 radial = Unit({0.0f, c / ry, s / rz});
            const auto at = [&](float x, float r) {
                return V3{x, (ry + r * thick) * c, cz + (rz + r * thick) * s};
            };
            const V3 n = Unit({f.n.x, radial.y * f.n.y, radial.z * f.n.y});
            Push(m.vertices, at(f.xa, f.ra), n, PitHandMaterial::Strap, f.shade);
            Push(m.vertices, at(f.xb, f.rb), n, PitHandMaterial::Strap, f.shade);
        }
        for (int i = 0; i < kSegments; i++) {
            const uint32_t a0 = first + static_cast<uint32_t>(i) * 2, b0 = a0 + 1;
            const uint32_t a1 = first + static_cast<uint32_t>((i + 1) % kSegments) * 2, b1 = a1 + 1;
            m.indices.insert(m.indices.end(), {a0, b1, b0, a0, a1, b1});
        }
    }
}

} // namespace

const LaurelMesh& LaurelFrame() {
    static const LaurelMesh mesh = Build();
    return mesh;
}

LaurelMesh LaurelFrameWorn(float arm_half_thickness, float arm_half_width, float strap_offset) {
    LaurelMesh m = LaurelFrame();
    const float rz = std::max(arm_half_thickness, 0.05f);
    const float ry = std::max(arm_half_width, 0.05f);
    const auto first = static_cast<uint32_t>(m.indices.size());
    // The arm's top touches the plate's back; the straps lie on the arm, under the plate.
    const float cz = kLaurelBack - rz;
    constexpr float kThick = 0.022f;
    constexpr float kLift = 0.004f; // clear of the leather under it
    constexpr float kHalf = 0.075f; // half a strap's width
    const float at = std::clamp(strap_offset, kHalf + 0.01f, kLaurelPlateHalfWidth - kHalf - 0.02f);
    for (const float x : {-at, at}) {
        Strap(m, x - kHalf, x + kHalf, rz + kLift, ry + kLift, cz, kThick);
    }
    m.parts.push_back({"strap", PitHandMaterial::Strap, first, static_cast<uint32_t>(m.indices.size()) - first});
    return m;
}

} // namespace FrameXr::Vis
