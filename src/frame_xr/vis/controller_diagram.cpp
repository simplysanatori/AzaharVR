// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cmath>
#include <limits>

#include "frame_xr/input/default_bindings.h" // AzaharVR: ControllerScheme
#include "frame_xr/vis/controller_diagram.h"

namespace FrameXr::Vis {

namespace {

// Supersampling: the drawing is made at kSs x kSs the output size and averaged down.
constexpr int kSs = 3;

struct V3 {
    float x, y, z;
};

V3 Sub(const V3& a, const V3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

// The model rolled about +Z, then pitched about +X; the camera looks down -Z of the result.
struct Turn {
    float m[9];
    explicit Turn(const DiagramAngle& a) {
        const float z = a.roll * (3.14159265358979f / 180.0f);
        const float p = a.pitch * (3.14159265358979f / 180.0f);
        const float cz = std::cos(z), sz = std::sin(z), cp = std::cos(p), sp = std::sin(p);
        // Rx(p) * Rz(z), row major.
        const float rz[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 1};
        const float rx[9] = {1, 0, 0, 0, cp, -sp, 0, sp, cp};
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                m[r * 3 + c] = rx[r * 3 + 0] * rz[0 * 3 + c] + rx[r * 3 + 1] * rz[1 * 3 + c] +
                               rx[r * 3 + 2] * rz[2 * 3 + c];
            }
        }
        if (a.yaw != 0.0f) { // AzaharVR: then * Ry(yaw), turned first
            const float y = a.yaw * (3.14159265358979f / 180.0f);
            const float cy = std::cos(y), sy = std::sin(y);
            const float ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
            float t[9];
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) {
                    t[r * 3 + c] = m[r * 3 + 0] * ry[0 * 3 + c] + m[r * 3 + 1] * ry[1 * 3 + c] +
                                   m[r * 3 + 2] * ry[2 * 3 + c];
                }
            }
            std::copy(t, t + 9, m);
        }
    }
    V3 operator()(const float* v) const {
        return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
                m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
    }
};

struct Bounds {
    float x0 = std::numeric_limits<float>::max(), y0 = std::numeric_limits<float>::max();
    float x1 = std::numeric_limits<float>::lowest(), y1 = std::numeric_limits<float>::lowest();
};

// The turned model's extent in X and Y (metres), over the vertices the components draw.
Bounds Extent(const ModelFile& model, const Turn& turn) {
    Bounds b;
    const std::size_t count = model.vertices.size() / kModelVertexFloats;
    for (std::size_t i = 0; i < count; i++) {
        const V3 v = turn(&model.vertices[i * kModelVertexFloats]);
        b.x0 = std::min(b.x0, v.x);
        b.x1 = std::max(b.x1, v.x);
        b.y0 = std::min(b.y0, v.y);
        b.y1 = std::max(b.y1, v.y);
    }
    return b;
}

float Edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

uint32_t Mix(uint32_t a, uint32_t b, float t) {
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        const float ca = static_cast<float>((a >> s) & 0xFF), cb = static_cast<float>((b >> s) & 0xFF);
        out |= static_cast<uint32_t>(std::lround(ca + (cb - ca) * t)) << s;
    }
    return out;
}

} // namespace

std::optional<CtrlPart> PartOfComponent(const ModelComponent& c, ControllerScheme scheme,
                                        int hand) {
    // AzaharVR: the PC controllers' paths, named by our mapping (actions.cpp).
    using Input::ControllerScheme;
    if (hand == 0 && (scheme == ControllerScheme::Touch || scheme == ControllerScheme::Focus3) &&
        c.path == "/input/system/click") {
        return CtrlPart::Menu; // the left three-line button; SteamVR calls it system
    }
    if (hand == 0 && scheme == ControllerScheme::Index) {
        if (c.path == "/input/a/click") {
            return CtrlPart::X;
        }
        if (c.path == "/input/b/click") {
            return CtrlPart::Y;
        }
    }
    static const struct {
        const char* path;
        CtrlPart part;
    } table[] = {
        {"/input/thumbstick", CtrlPart::Stick},     {"/input/dpad_up/click", CtrlPart::Dpad},
        {"/input/view/click", CtrlPart::View},      {"/input/menu/click", CtrlPart::Menu},
        {"/input/system/click", CtrlPart::System},  {"/input/a/click", CtrlPart::A},
        {"/input/b/click", CtrlPart::B},            {"/input/x/click", CtrlPart::X},
        {"/input/y/click", CtrlPart::Y},            {"/input/bumper/click", CtrlPart::Bumper},
        {"/input/trigger", CtrlPart::Trigger},      {"/input/trigger/value", CtrlPart::Trigger},
        {"/input/grip", CtrlPart::Grip},            {"/input/squeeze", CtrlPart::Grip},
        {"/input/squeeze/value", CtrlPart::Grip},
        // AzaharVR: the Quest's and the Focus 3's sticks, the Index's trackpad
        {"/input/joystick", CtrlPart::Stick},       {"/input/thumbstick/click", CtrlPart::Stick},
        {"/input/trackpad", CtrlPart::Trackpad},    {"/input/trackpad/touch", CtrlPart::Trackpad},
        {"/input/trackpad/click", CtrlPart::Trackpad},
        {"/input/trackpad/force", CtrlPart::Trackpad},
    };
    for (const auto& e : table) {
        if (c.path == e.path) {
            return e.part;
        }
    }
    return std::nullopt;
}

float DiagramScale(const std::vector<const ModelFile*>& models, const DiagramAngle& angle,
                   int width, int height) {
    const Turn turn(angle);
    float scale = std::numeric_limits<float>::max();
    for (const ModelFile* m : models) {
        const Bounds b = Extent(*m, turn);
        if (b.x1 <= b.x0 || b.y1 <= b.y0) {
            continue;
        }
        scale = std::min({scale, (static_cast<float>(width) - 8.0f) / (b.x1 - b.x0),
                          (static_cast<float>(height) - 8.0f) / (b.y1 - b.y0)});
    }
    return scale == std::numeric_limits<float>::max() ? 1.0f : scale;
}

DiagramImage DrawDiagram(const ModelFile& model, const DiagramAngle& angle, float scale, int width,
                         int height, const DiagramStyle& style, ControllerScheme scheme, int hand) {
    DiagramImage out;
    out.width = width;
    out.height = height;
    out.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
    const int W = width * kSs, H = height * kSs;
    const float s = scale * kSs;
    const Turn turn(angle);
    const Bounds ext = Extent(model, turn);
    const float mid_x = (ext.x0 + ext.x1) * 0.5f, mid_y = (ext.y0 + ext.y1) * 0.5f;

    // Per supersample: depth (towards the camera), component (-1 nothing), facing.
    const std::size_t n = static_cast<std::size_t>(W) * H;
    std::vector<float> depth(n, std::numeric_limits<float>::lowest());
    std::vector<int8_t> comp(n, -1);
    std::vector<std::array<int8_t, 3>> normal(n, {0, 0, 0});

    std::vector<int> part_of(model.components.size(), -1);
    for (std::size_t c = 0; c < model.components.size(); c++) {
        if (const auto p = PartOfComponent(model.components[c], scheme, hand)) {
            part_of[c] = static_cast<int>(*p);
        }
    }

    for (std::size_t c = 0; c < model.components.size() && c < 127; c++) {
        const ModelComponent& mc = model.components[c];
        for (uint32_t t = mc.first_index; t + 2 < mc.first_index + mc.index_count; t += 3) {
            V3 p[3], nm[3];
            float sx[3], sy[3];
            for (int k = 0; k < 3; k++) {
                const float* v = &model.vertices[std::size_t{model.indices[t + k]} * kModelVertexFloats];
                p[k] = turn(v);
                nm[k] = turn(v + 3);
                sx[k] = static_cast<float>(W) * 0.5f + (p[k].x - mid_x) * s;
                sy[k] = static_cast<float>(H) * 0.5f - (p[k].y - mid_y) * s;
            }
            const float area = Edge(sx[0], sy[0], sx[1], sy[1], sx[2], sy[2]);
            if (std::fabs(area) < 1e-6f) {
                continue;
            }
            const int x0 = std::max(0, static_cast<int>(std::floor(std::min({sx[0], sx[1], sx[2]}))));
            const int x1 = std::min(W - 1, static_cast<int>(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
            const int y0 = std::max(0, static_cast<int>(std::floor(std::min({sy[0], sy[1], sy[2]}))));
            const int y1 = std::min(H - 1, static_cast<int>(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
            const float inv = 1.0f / area;
            for (int y = y0; y <= y1; y++) {
                const float py = static_cast<float>(y) + 0.5f;
                for (int x = x0; x <= x1; x++) {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float w0 = Edge(sx[1], sy[1], sx[2], sy[2], px, py) * inv;
                    const float w1 = Edge(sx[2], sy[2], sx[0], sy[0], px, py) * inv;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                        continue;
                    }
                    const float z = w0 * p[0].z + w1 * p[1].z + w2 * p[2].z;
                    const std::size_t i = static_cast<std::size_t>(y) * W + x;
                    if (z <= depth[i]) {
                        continue;
                    }
                    depth[i] = z;
                    comp[i] = static_cast<int8_t>(c);
                    V3 nn{w0 * nm[0].x + w1 * nm[1].x + w2 * nm[2].x,
                          w0 * nm[0].y + w1 * nm[1].y + w2 * nm[2].y,
                          w0 * nm[0].z + w1 * nm[1].z + w2 * nm[2].z};
                    const float len = std::sqrt(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z);
                    if (len > 1e-6f) {
                        nn = {nn.x / len, nn.y / len, nn.z / len};
                    }
                    normal[i] = {static_cast<int8_t>(std::lround(nn.x * 127.0f)),
                                 static_cast<int8_t>(std::lround(nn.y * 127.0f)),
                                 static_cast<int8_t>(std::lround(nn.z * 127.0f))};
                }
            }
        }
    }

    // Outlines: where the part changes, the depth jumps or the surface folds.
    const float depth_jump = 0.0025f; // metres
    const float crease = std::cos(style.crease_degrees * (3.14159265358979f / 180.0f)) * 127.0f * 127.0f;
    std::vector<uint8_t> edge(n, 0);
    auto differs = [&](std::size_t a, std::size_t b) {
        if (comp[a] != comp[b]) {
            return true;
        }
        if (comp[a] < 0) {
            return false;
        }
        if (std::fabs(depth[a] - depth[b]) > depth_jump) {
            return true;
        }
        const auto& na = normal[a];
        const auto& nb = normal[b];
        const float dot = static_cast<float>(na[0] * nb[0] + na[1] * nb[1] + na[2] * nb[2]);
        return dot < crease;
    };
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const std::size_t i = static_cast<std::size_t>(y) * W + x;
            if (x + 1 < W && differs(i, i + 1)) {
                edge[comp[i] >= 0 ? i : i + 1] = 1;
            }
            if (y + 1 < H && differs(i, i + W)) {
                edge[comp[i] >= 0 ? i : i + W] = 1;
            }
        }
    }
    // Thicken to the line width (a disc around each edge sample).
    const float r = style.line_width * kSs * 0.5f;
    const int ri = static_cast<int>(std::ceil(r));
    std::vector<std::pair<int, int>> disc;
    for (int dy = -ri; dy <= ri; dy++) {
        for (int dx = -ri; dx <= ri; dx++) {
            if (static_cast<float>(dx * dx + dy * dy) <= r * r) {
                disc.emplace_back(dx, dy);
            }
        }
    }
    std::vector<uint8_t> ink(n, 0);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (!edge[static_cast<std::size_t>(y) * W + x]) {
                continue;
            }
            for (const auto& [dx, dy] : disc) {
                const int xx = x + dx, yy = y + dy;
                if (xx >= 0 && xx < W && yy >= 0 && yy < H) {
                    ink[static_cast<std::size_t>(yy) * W + xx] = 1;
                }
            }
        }
    }

    // Average down: the fill (shaded a little by its facing), the outlines over it.
    std::array<double, kCtrlParts> sum_x{}, sum_y{};
    std::array<std::size_t, kCtrlParts> count{};
    for (auto& b : out.bounds) {
        b = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
             std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    }
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float acc[4] = {0, 0, 0, 0}; // premultiplied, 0..255
            for (int sy2 = 0; sy2 < kSs; sy2++) {
                for (int sx2 = 0; sx2 < kSs; sx2++) {
                    const int X = x * kSs + sx2, Y = y * kSs + sy2;
                    const std::size_t i = static_cast<std::size_t>(Y) * W + X;
                    uint32_t c = 0;
                    if (comp[i] >= 0) {
                        const int part = part_of[static_cast<std::size_t>(comp[i])];
                        const float facing = std::max(0.0f, normal[i][2] / 127.0f);
                        c = Mix(part >= 0 ? style.part : style.body, 0xFF000000u,
                                0.45f * (1.0f - facing));
                        if (part >= 0) {
                            sum_x[part] += X + 0.5;
                            sum_y[part] += Y + 0.5;
                            count[part]++;
                            auto& b = out.bounds[part];
                            b[0] = std::min(b[0], static_cast<float>(X) / kSs);
                            b[1] = std::min(b[1], static_cast<float>(Y) / kSs);
                            b[2] = std::max(b[2], static_cast<float>(X + 1) / kSs);
                            b[3] = std::max(b[3], static_cast<float>(Y + 1) / kSs);
                        }
                    }
                    if (ink[i]) {
                        c = style.line;
                    }
                    const float a = static_cast<float>(c >> 24) / 255.0f;
                    acc[0] += static_cast<float>((c >> 16) & 0xFF) * a;
                    acc[1] += static_cast<float>((c >> 8) & 0xFF) * a;
                    acc[2] += static_cast<float>(c & 0xFF) * a;
                    acc[3] += a * 255.0f;
                }
            }
            uint8_t* o = &out.rgba[(static_cast<std::size_t>(y) * width + x) * 4];
            for (int k = 0; k < 4; k++) {
                o[k] = static_cast<uint8_t>(std::lround(acc[k] / (kSs * kSs)));
            }
        }
    }
    // A part needs a few pixels' worth showing to be pointed at (not a sliver past an edge).
    constexpr std::size_t kMinSamples = 12 * kSs * kSs;
    for (std::size_t p = 0; p < kCtrlParts; p++) {
        if (count[p] >= kMinSamples) {
            out.anchor[p] = DiagramPoint{static_cast<float>(sum_x[p] / count[p] / kSs),
                                         static_cast<float>(sum_y[p] / count[p] / kSs)};
        }
    }
    return out;
}

DiagramImage CropDiagram(const DiagramImage& in, int y0, int y1, int fade) {
    y0 = std::clamp(y0, 0, in.height);
    y1 = std::clamp(y1, y0, in.height);
    DiagramImage out;
    out.width = in.width;
    out.height = y1 - y0;
    const std::size_t row = static_cast<std::size_t>(in.width) * 4;
    out.rgba.assign(in.rgba.begin() + static_cast<std::ptrdiff_t>(row * y0),
                    in.rgba.begin() + static_cast<std::ptrdiff_t>(row * y1));
    for (int y = std::max(0, out.height - fade); y < out.height && fade > 0; y++) {
        const float keep = static_cast<float>(out.height - y) / static_cast<float>(fade + 1);
        for (std::size_t i = row * y; i < row * (y + 1); i++) {
            out.rgba[i] = static_cast<uint8_t>(std::lround(out.rgba[i] * keep));
        }
    }
    for (std::size_t p = 0; p < kCtrlParts; p++) {
        const auto& a = in.anchor[p];
        if (a && a->y >= static_cast<float>(y0) && a->y < static_cast<float>(y1)) {
            out.anchor[p] = DiagramPoint{a->x, a->y - static_cast<float>(y0)};
        }
        out.bounds[p] = in.bounds[p];
        out.bounds[p][1] -= static_cast<float>(y0);
        out.bounds[p][3] -= static_cast<float>(y0);
    }
    return out;
}

// ---- AzaharVR pcdiag ----

SchemeView DiagramView(ControllerScheme scheme) {
    switch (scheme) {
    case ControllerScheme::Frame:
        return {};
    // Their aim spaces tilt their faces differently from the Frame's: each top angle is the one
    // that shows the face, the stick and the grip (Focus 3: from behind its ring).
    case ControllerScheme::Touch:
        return {{0.0f, 75.0f}, {180.0f, 165.0f}, 0.9f};
    case ControllerScheme::Index:
        return {{0.0f, 60.0f}, {180.0f, 165.0f}, 0.9f, 1.5f};
    case ControllerScheme::Focus3:
        return {{0.0f, 40.0f}, {180.0f, 165.0f}, 0.9f};
    }
    return {};
}

DiagramAngle HandAngle(const DiagramAngle& angle, int hand) {
    return hand == 0 ? angle : DiagramAngle{-angle.roll, angle.pitch, -angle.yaw};
}

namespace {

// `in`'s window of `w` x `h` from (x0, y0), its anchors and bounds moved with it; the sides where
// the drawing is cut fade out over `fade` pixels.
DiagramImage Window(const DiagramImage& in, int x0, int y0, int w, int h, int fade) {
    DiagramImage out;
    out.width = w;
    out.height = h;
    out.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int sx = x + x0, sy = y + y0;
            if (sx < 0 || sy < 0 || sx >= in.width || sy >= in.height) {
                continue;
            }
            std::copy_n(&in.rgba[(static_cast<std::size_t>(sy) * in.width + sx) * 4], 4,
                        &out.rgba[(static_cast<std::size_t>(y) * w + x) * 4]);
        }
    }
    const auto cut = [&](int sx, int sy, int dx, int dy) { // anything drawn past this edge?
        for (; sx >= 0 && sy >= 0 && sx < in.width && sy < in.height; sx += dx, sy += dy) {
            if (in.rgba[(static_cast<std::size_t>(sy) * in.width + sx) * 4 + 3] > 0) {
                return true;
            }
        }
        return false;
    };
    bool cut_left = false, cut_right = false, cut_bottom = false;
    for (int y = 0; y < h; y++) {
        cut_left = cut_left || cut(x0 - 1, y0 + y, -1, 0);
        cut_right = cut_right || cut(x0 + w, y0 + y, 1, 0);
    }
    for (int x = 0; x < w; x++) {
        cut_bottom = cut_bottom || cut(x0 + x, y0 + h, 0, 1);
    }
    for (int y = 0; y < h && fade > 0; y++) {
        for (int x = 0; x < w; x++) {
            float keep = 1.0f;
            const auto ramp = [&](bool on, int d) {
                if (on && d < fade) {
                    keep = std::min(keep, static_cast<float>(d + 1) / static_cast<float>(fade + 1));
                }
            };
            ramp(cut_left, x);
            ramp(cut_right, w - 1 - x);
            ramp(cut_bottom, h - 1 - y);
            if (keep < 1.0f) {
                uint8_t* o = &out.rgba[(static_cast<std::size_t>(y) * w + x) * 4];
                for (int k = 0; k < 4; k++) {
                    o[k] = static_cast<uint8_t>(std::lround(o[k] * keep));
                }
            }
        }
    }
    for (std::size_t p = 0; p < kCtrlParts; p++) {
        const auto& a = in.anchor[p];
        const float ax = a ? a->x - static_cast<float>(x0) : 0.0f;
        const float ay = a ? a->y - static_cast<float>(y0) : 0.0f;
        if (a && ax >= 0.0f && ay >= 0.0f && ax < static_cast<float>(w) &&
            ay < static_cast<float>(h)) {
            out.anchor[p] = DiagramPoint{ax, ay};
        }
        const auto& b = in.bounds[p];
        out.bounds[p] = {b[0] - static_cast<float>(x0), b[1] - static_cast<float>(y0),
                         b[2] - static_cast<float>(x0), b[3] - static_cast<float>(y0)};
    }
    return out;
}

// The top drawing `zoom` times the scale that fits, kept from its top, centred on its parts.
DiagramImage ZoomedTop(const ModelFile& model, const DiagramAngle& angle, float scale, float zoom,
                       const SchemeHandSizes& sizes, const DiagramStyle& style,
                       ControllerScheme scheme, int hand) {
    const int big_w = static_cast<int>(std::lround(static_cast<float>(sizes.top_w) * zoom));
    const int big_h = static_cast<int>(std::lround(static_cast<float>(sizes.top_h) * zoom));
    const DiagramImage big =
        DrawDiagram(model, angle, scale * zoom, big_w, big_h, style, scheme, hand);
    float sum = 0.0f;
    int n = 0;
    for (const auto& a : big.anchor) {
        if (a) {
            sum += a->x;
            n++;
        }
    }
    const float mid = n > 0 ? sum / static_cast<float>(n) : static_cast<float>(big_w) * 0.5f;
    const int x0 = std::clamp(static_cast<int>(std::lround(mid)) - sizes.top_w / 2, 0,
                              big_w - sizes.top_w);
    int y0 = 0;
    while (y0 < big_h) {
        bool any = false;
        for (int x = 0; x < big_w && !any; x++) {
            any = big.rgba[(static_cast<std::size_t>(y0) * big_w + x) * 4 + 3] > 0;
        }
        if (any) {
            break;
        }
        y0++;
    }
    y0 = std::clamp(y0 - 4, 0, big_h - sizes.top_h);
    return Window(big, x0, y0, sizes.top_w, sizes.top_h, sizes.crop_fade);
}

} // namespace

SchemeHands DrawSchemeHands(const ModelFile& left, const ModelFile& right, ControllerScheme scheme,
                            const SchemeHandSizes& sizes, const DiagramStyle& style) {
    SchemeHands out;
    const SchemeView view = DiagramView(scheme);
    const float top =
        std::min(DiagramScale({&left}, HandAngle(view.top, 0), sizes.top_w, sizes.top_h),
                 DiagramScale({&right}, HandAngle(view.top, 1), sizes.top_w, sizes.top_h));
    const float front = top * view.front_scale;
    const std::array<const ModelFile*, 2> models{&left, &right};
    for (int h = 0; h < 2; h++) {
        out.top[h] = view.top_zoom > 1.0f
                         ? ZoomedTop(*models[h], HandAngle(view.top, h), top, view.top_zoom, sizes,
                                     style, scheme, h)
                         : DrawDiagram(*models[h], HandAngle(view.top, h), top, sizes.top_w,
                                       sizes.top_h, style, scheme, h);
        const DiagramImage full = DrawDiagram(*models[h], HandAngle(view.front, h), front,
                                              sizes.front_w, sizes.front_h, style, scheme, h);
        // From the top of the drawing to a little under the trigger and the bumper.
        int y0 = 0, y1 = 0;
        while (y0 < full.height) {
            bool any = false;
            for (int x = 0; x < full.width && !any; x++) {
                any = full.rgba[(static_cast<std::size_t>(y0) * full.width + x) * 4 + 3] > 0;
            }
            if (any) {
                break;
            }
            y0++;
        }
        for (const CtrlPart p : {CtrlPart::Bumper, CtrlPart::Trigger}) {
            if (full.anchor[static_cast<std::size_t>(p)]) {
                y1 = std::max(y1, static_cast<int>(full.bounds[static_cast<std::size_t>(p)][3]));
            }
        }
        out.front[h] = y1 > y0 ? CropDiagram(full, std::max(0, y0 - 2), y1 + sizes.crop_extra,
                                             sizes.crop_fade)
                               : CropDiagram(full, 0, full.height / 2, sizes.crop_fade);
    }
    return out;
}

const SchemeModelFolders& SchemeModels(ControllerScheme scheme) {
    static constexpr std::array<SchemeModelFolders, 4> kFolders{{
        {"drivers/frame_controller/resources/rendermodels", "frame_controller_left",
         "frame_controller_right"},
        // Quest 3's Touch Plus: SteamVR's newest Touch model (Quest 2 and Pro are much alike).
        {"resources/rendermodels", "oculus_quest_plus_controller_left",
         "oculus_quest_plus_controller_right"},
        {"drivers/indexcontroller/resources/rendermodels", "valve_controller_knu_1_0_left",
         "valve_controller_knu_1_0_right"},
        {"drivers/vrlink/resources/rendermodels", "vive_focus3_controller_left",
         "vive_focus3_controller_right"},
    }};
    const auto i = static_cast<std::size_t>(scheme);
    return kFolders[i < kFolders.size() ? i : 0];
}

} // namespace FrameXr::Vis
