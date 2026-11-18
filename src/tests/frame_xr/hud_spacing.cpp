// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame hudmove: HUD Spacing (video_core/vr_hud_spacing.h): which vertices move (the vertex
// shader's 2D rule), how far, when the XR thread turns it on, and the plumbing to the shader.

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "common/settings.h"
#include "video_core/shader/generator/glsl_shader_gen.h"
#include "video_core/shader/generator/shader_uniforms.h"
#include "video_core/vr_hud_spacing.h"
#include "video_core/vr_immersive.h"

using Catch::Approx;
using namespace VideoCore::HudSpacing;

namespace {
// The vertex shader's rule before HUD Spacing (immersive_scale.cpp pins the same text).
Clip Before(Clip p, float factor, float factor_y) {
    float fx = factor_y, fy = factor;
    if (std::abs(p.w - 1.0f) < 0.0001f) {
        fx = std::abs(p.x) >= 0.999f ? 1.0f : fx;
        fy = std::abs(p.y) >= 0.999f ? 1.0f : fy;
    }
    return {p.x / fx, p.y / fy, p.w};
}
} // namespace

TEST_CASE("FrameXr hudmove: spacing to scale", "[frame_xr][hudmove]") {
    // 100 % is the game's own: exactly 1, so the shader divides by exactly 1.
    REQUIRE(ScaleForSpacing(kDefaultSpacing) == 1.0f);
    REQUIRE(ScaleForSpacing(90) == Approx(0.9f));
    REQUIRE(ScaleForSpacing(110) == Approx(1.1f));
    // The range ends: 0 % (at the middle, never a scale of 0: the shader divides by it), 150 %
    // (out); past them clamped.
    REQUIRE(kMinSpacing == 0);
    REQUIRE(kMaxSpacing == 150);
    REQUIRE(ScaleForSpacing(kMinSpacing) == kZeroScale);
    REQUIRE(ScaleForSpacing(kMinSpacing) > 0.0f);
    REQUIRE(ScaleForSpacing(1) == Approx(0.01f));
    REQUIRE(ScaleForSpacing(kMaxSpacing) == Approx(1.5f));
    REQUIRE(ScaleForSpacing(-50) == ScaleForSpacing(kMinSpacing));
    REQUIRE(ScaleForSpacing(1000) == ScaleForSpacing(kMaxSpacing));
    // Every value of the range is one the rasterizer takes.
    for (int p = kMinSpacing; p <= kMaxSpacing; ++p) {
        REQUIRE(UsableScale(ScaleForSpacing(p)) == ScaleForSpacing(p));
    }
    // Anything else is no change.
    REQUIRE(UsableScale(0.0f) == 1.0f);
    REQUIRE(UsableScale(-0.9f) == 1.0f);
    REQUIRE(UsableScale(2.0f) == 1.0f);
    REQUIRE(UsableScale(std::numeric_limits<float>::quiet_NaN()) == 1.0f);
    REQUIRE(UsableScale(std::numeric_limits<float>::infinity()) == 1.0f);
}

TEST_CASE("FrameXr hudmove: only engaged Full modes move the HUD", "[frame_xr][hudmove]") {
    // Off, Wide, not engaged (paused for the Secondary screen or the in-game menu): nothing.
    for (unsigned mode : {0u, 1u, 4u, 9u}) {
        REQUIRE(ForFrame(true, mode, 80, 90, 0.0f).IsIdentity());
    }
    REQUIRE(ForFrame(false, 2, 80, 90, 0.0f).IsIdentity());
    REQUIRE(ForFrame(false, 3, 80, 90, 0.0f).IsIdentity());
    // The Full modes: each axis its own value.
    for (unsigned mode : {2u, 3u}) {
        const Scales s = ForFrame(true, mode, 80, 90, 0.0f);
        REQUIRE(s.across == Approx(0.8f));
        REQUIRE(s.up_down == Approx(0.9f));
    }
    // Both 100 % (the default): exactly 1, so the shader divides by exactly 1.
    REQUIRE(ForFrame(true, 3, 100, 100, 0.0f).IsIdentity());
    // Mode 2's menu rule eases it back: halfway at weight 0.5, none at 1.
    const Scales half = ForFrame(true, 2, 80, 110, 0.5f);
    REQUIRE(half.across == Approx(0.9f));
    REQUIRE(half.up_down == Approx(1.05f));
    REQUIRE(ForFrame(true, 2, 80, 110, 1.0f).IsIdentity());
    // A weight out of range or not a number is taken in range or as 0.
    REQUIRE(ForFrame(true, 2, 80, 100, 3.0f).IsIdentity());
    REQUIRE(ForFrame(true, 2, 80, 100, -1.0f).across == Approx(0.8f));
    REQUIRE(ForFrame(true, 2, 80, 100, std::numeric_limits<float>::quiet_NaN()).across ==
            Approx(0.8f));
}

TEST_CASE("FrameXr hudmove: which vertices move", "[frame_xr][hudmove]") {
    const Scales none{};
    const Scales in{0.8f, 0.9f}; // 80 % across, 90 % up and down
    const float factor = 1.55f, factor_y = 1.7f;

    SECTION("at 100 % every vertex is exactly where it was") {
        for (float w : {1.0f, 1.00005f, 0.5f, 3.0f, -2.0f}) {
            for (float x : {-1.2f, -1.0f, -0.9995f, -0.5f, 0.0f, 0.3f, 0.998f, 1.0f}) {
                for (float y : {-1.0f, -0.7f, 0.0f, 0.42f, 0.9991f, 1.3f}) {
                    const Clip a = ApplyVertex({x, y, w}, factor, factor_y, none);
                    const Clip b = Before({x, y, w}, factor, factor_y);
                    REQUIRE(a.x == b.x);
                    REQUIRE(a.y == b.y);
                }
            }
        }
    }
    SECTION("2D inside the view moves toward the middle, per axis") {
        // Clip y is across (the top screen is drawn turned), clip x up and down.
        const Clip p = ApplyVertex({0.5f, -0.9f, 1.0f}, factor, factor_y, in);
        const Clip b = Before({0.5f, -0.9f, 1.0f}, factor, factor_y);
        REQUIRE(p.y == Approx(b.y * 0.8f));
        REQUIRE(p.x == Approx(b.x * 0.9f));
        // The middle stays.
        const Clip mid = ApplyVertex({0.0f, 0.0f, 1.0f}, factor, factor_y, in);
        REQUIRE(mid.x == 0.0f);
        REQUIRE(mid.y == 0.0f);
        // Without a periphery factor (Render Margin Off): only the HUD scale.
        const Clip plain = ApplyVertex({0.6f, 0.95f, 1.0f}, 1.0f, 1.0f, in);
        REQUIRE(plain.x == Approx(0.54f));
        REQUIRE(plain.y == Approx(0.76f));
        // Above 100 % spreads it out.
        const Clip out = ApplyVertex({0.5f, 0.5f, 1.0f}, 1.0f, 1.0f, {1.1f, 1.2f});
        REQUIRE(out.y == Approx(0.55f));
        REQUIRE(out.x == Approx(0.6f));
    }
    SECTION("2D on or past an edge keeps its place on that axis (full-screen passes)") {
        const Clip edge_y = ApplyVertex({0.5f, 1.0f, 1.0f}, factor, factor_y, in);
        REQUIRE(edge_y.y == 1.0f);
        REQUIRE(edge_y.x == Approx(0.5f / factor_y * 0.9f));
        const Clip edge_x = ApplyVertex({-0.9995f, 0.25f, 1.0f}, factor, factor_y, in);
        REQUIRE(edge_x.x == -0.9995f);
        REQUIRE(edge_x.y == Approx(0.25f / factor * 0.8f));
        const Clip corner = ApplyVertex({-1.0f, 1.0f, 1.0f}, factor, factor_y, in);
        REQUIRE(corner.x == -1.0f);
        REQUIRE(corner.y == 1.0f);
        const Clip past = ApplyVertex({1.5f, -2.0f, 1.0f}, factor, factor_y, in);
        REQUIRE(past.x == 1.5f);
        REQUIRE(past.y == -2.0f);
    }
    SECTION("the world (perspective, w != 1) never moves") {
        for (float w : {0.5f, 0.9998f, 1.0002f, 2.0f, 40.0f}) {
            const Clip p = ApplyVertex({0.3f * w, -0.7f * w, w}, factor, factor_y, in);
            const Clip b = Before({0.3f * w, -0.7f * w, w}, factor, factor_y);
            REQUIRE(p.x == b.x);
            REQUIRE(p.y == b.y);
        }
    }
    SECTION("the far plane rule is off unless the game's preset turns it on") {
        // Mario Kart 7's HUD as traced on the desktop: w 1525.2083, z 1525.1589 after the flip.
        const Clip hud{900.0f, 1200.0f, 1525.2083f, 1525.1589f};
        const Clip a = ApplyVertex(hud, factor, factor_y, in);
        const Clip b = Before(hud, factor, factor_y);
        REQUIRE(a.x == b.x);
        REQUIRE(a.y == b.y);
    }
    SECTION("with the far plane rule a vertex on the far plane moves like 2D") {
        const Scales far{0.8f, 0.9f, true};
        const Clip hud{900.0f, 1200.0f, 1525.2083f, 1525.1589f};
        const Clip a = ApplyVertex(hud, factor, factor_y, far);
        const Clip b = Before(hud, factor, factor_y);
        REQUIRE(a.y == Approx(b.y * 0.8f));
        REQUIRE(a.x == Approx(b.x * 0.9f));
        REQUIRE(a.w == hud.w); // only x and y: it stays on the far plane
        // On or past an edge (|x| or |y| >= 0.999 w) it keeps the world's factor on that axis,
        // so a full-screen pass at the far plane stays whole.
        const Clip past = ApplyVertex({1600.0f, -700.0f, 1525.0f, 1524.95f}, factor, factor_y, far);
        REQUIRE(past.x == Approx(1600.0f / factor_y));
        REQUIRE(past.y == Approx(-700.0f / factor * 0.8f));
        const Clip edge = ApplyVertex({1523.5f, 0.0f, 1525.0f, 1524.95f}, 1.0f, 1.0f, far);
        REQUIRE(edge.x == 1523.5f);
        // At 100 % nothing moves, rule or not.
        const Clip same = ApplyVertex(hud, factor, factor_y, {1.0f, 1.0f, true});
        REQUIRE(same.x == b.x);
        REQUIRE(same.y == b.y);
    }
    SECTION("the far plane rule leaves the world alone") {
        // Mario Kart 7's world in a race (desktop census): w - z (after the flip) from 3 near the
        // camera down to 2.41 at the sky (w 9805); behind the camera (negative w) larger.
        const Scales far{0.8f, 0.9f, true};
        for (const auto& [w, z] : {std::pair{50.0f, 47.0f}, std::pair{1525.0f, 1522.1f},
                                   std::pair{9804.764f, 9802.352f}, std::pair{-574.7f, -577.7f},
                                   std::pair{-11523.0f, -11526.0f}}) {
            const Clip p{0.3f * w, -0.7f * w, w, z};
            const Clip a = ApplyVertex(p, factor, factor_y, far);
            const Clip b = Before(p, factor, factor_y);
            REQUIRE(a.x == b.x);
            REQUIRE(a.y == b.y);
        }
        REQUIRE(OnFarPlane(1525.1589f, 1525.2083f));
        REQUIRE_FALSE(OnFarPlane(9802.352f, 9804.764f));
        REQUIRE_FALSE(OnFarPlane(-1525.0f, 1525.0f)); // the near side (z = -w)
    }
    SECTION("an element keeps its order and its middle moves by the scale") {
        // A 40 x 24 pixel element at the right edge of a 400 x 240 picture (clip y 0.8 to 1.0
        // would touch the edge; this one stops at 0.98): it moves in by 20 % of its distance.
        const float left = 0.78f, right = 0.98f;
        const Clip l = ApplyVertex({0.1f, left, 1.0f}, 1.0f, 1.0f, in);
        const Clip r = ApplyVertex({0.1f, right, 1.0f}, 1.0f, 1.0f, in);
        REQUIRE(l.y < r.y);
        REQUIRE((l.y + r.y) / 2.0f == Approx((left + right) / 2.0f * 0.8f));
        REQUIRE(r.y - l.y == Approx((right - left) * 0.8f)); // and gets that much smaller
        // In 3DS pixels (200 from the middle to the side): 90 % moves the edge 20 pixels in.
        const Clip ten = ApplyVertex({0.0f, 0.998f, 1.0f}, 1.0f, 1.0f, {ScaleForSpacing(90), 1.0f});
        REQUIRE((0.998f - ten.y) * 200.0f == Approx(19.96f).margin(0.01f));
    }
}

// The shader's text and the uniform block it reads: the two scales after the up and down
// factor, defaulting to 1, the block's size unchanged.
TEST_CASE("FrameXr hudmove: the bottom screen's draws get no scale", "[frame_xr][hudmove]") {
    const Scales in{0.85f, 0.9f, true};
    // The bottom screen's viewport (240 x 320, drawn turned): none, far plane rule off too.
    REQUIRE(ForDraw(in, 240.0f, 320.0f) == Scales{});
    // The top screen's (240 x 400) and anything else: the scales as set.
    REQUIRE(ForDraw(in, 240.0f, 400.0f) == in);
    REQUIRE(ForDraw(in, 256.0f, 416.0f) == in);
    REQUIRE(ForDraw(in, 320.0f, 240.0f) == in);
    REQUIRE(ForDraw(Scales{}, 240.0f, 400.0f).IsIdentity());
}

TEST_CASE("FrameXr hudmove: the vertex shader reads the scales", "[frame_xr][hudmove]") {
    const std::string vs =
        Pica::Shader::Generator::GLSL::GenerateTrivialVertexShader(false, false);
    const std::string up_down = "float vr_hud_up_down;";
    const std::string across = "float vr_hud_across;";
    const auto factor_y = vs.find("float vr_immersive_mode_factor_y;");
    REQUIRE(factor_y != std::string::npos);
    REQUIRE(vs.find(up_down) != std::string::npos);
    REQUIRE(vs.find(across) != std::string::npos);
    REQUIRE(vs.find(up_down) > factor_y);
    REQUIRE(vs.find(across) > vs.find(up_down));
    REQUIRE(vs.find("f = mix(f / vec2(vr_hud_up_down, vr_hud_across), vec2(1.0),") !=
            std::string::npos);
    REQUIRE(vs.find("float vr_hud_far_plane;") > vs.find(across));
    REQUIRE(vs.find("} else if (vr_hud_far_plane != 0.0 && abs(pos.w - pos.z) < 0.5) {") !=
            std::string::npos);
    REQUIRE(vs.find("greaterThanEqual(abs(pos.xy), vec2(0.999 * abs(pos.w))));") !=
            std::string::npos);
    REQUIRE(vs.find("gl_Position.xy /= VrFactor(gl_Position);") != std::string::npos);

    using Pica::Shader::Generator::VSUniformData;
    const VSUniformData data{};
    REQUIRE(data.vr_hud_up_down == 1.0f);
    REQUIRE(data.vr_hud_across == 1.0f);
    REQUIRE(data.vr_hud_far_plane == 0.0f);
    // std140: plain floats packed after the factors (as the GLSL block declares them).
    REQUIRE(offsetof(VSUniformData, vr_hud_up_down) ==
            offsetof(VSUniformData, vr_immersive_mode_factor_y) + 4);
    REQUIRE(offsetof(VSUniformData, vr_hud_across) ==
            offsetof(VSUniformData, vr_hud_up_down) + 4);
    REQUIRE(offsetof(VSUniformData, vr_hud_far_plane) ==
            offsetof(VSUniformData, vr_hud_across) + 4);
}

TEST_CASE("FrameXr hudmove: the scales reach the emulation thread", "[frame_xr][hudmove]") {
    using VideoCore::Immersive::Mailbox;
    using VideoCore::Immersive::VRData;
    Mailbox box;
    VRData d;
    REQUIRE(d.hud.IsIdentity()); // CitraVR's "off" values move nothing
    d.mode = 3;
    d.hud = ForFrame(true, 3, 85, 95, 0.0f);
    d.hud.far_plane = true;
    box.Publish(d);
    VRData out;
    u32 serial = 0;
    REQUIRE(box.Read(out, serial));
    REQUIRE(out.hud == d.hud);
    REQUIRE(out.hud.far_plane);
    box.Publish(VRData{});
    REQUIRE(box.Read(out, serial));
    REQUIRE(out.hud.IsIdentity());
}

namespace {
struct FakeRasterizer final : VideoCore::RasterizerInterface {
    void AddTriangle(const Pica::OutputVertex&, const Pica::OutputVertex&,
                     const Pica::OutputVertex&) override {}
    void DrawTriangles() override {}
    void FlushAll() override {}
    void FlushRegion(PAddr, u32) override {}
    void InvalidateRegion(PAddr, u32) override {}
    void FlushAndInvalidateRegion(PAddr, u32) override {}
    void ClearAll(bool) override {}
    void SetVRHudSpacing(float across_, float up_down_, bool far_plane_) override {
        across = across_;
        up_down = up_down_;
        far_plane = far_plane_;
        calls++;
    }
    float across = 0.0f, up_down = 0.0f;
    bool far_plane = true;
    int calls = 0;
};
} // namespace

// The emulation thread's hand-off at a game frame's end gives the rasterizer what was published.
TEST_CASE("FrameXr hudmove: the rasterizer gets the scales at the latch", "[frame_xr][hudmove]") {
    using namespace VideoCore::Immersive;
    VRData d;
    d.mode = 3;
    d.hud = ForFrame(true, 3, 90, 100, 0.0f);
    d.hud.far_plane = true;
    Publish(d);
    FakeRasterizer r;
    LatchFrame(&r);
    REQUIRE(r.calls == 1);
    REQUIRE(r.across == Approx(0.9f));
    REQUIRE(r.up_down == 1.0f);
    REQUIRE(r.far_plane);
    // Paused or out of the Full modes: the XR thread publishes 1, and the rasterizer gets it.
    Publish(VRData{});
    LatchFrame(&r);
    REQUIRE(r.calls == 2);
    REQUIRE(r.across == 1.0f);
    REQUIRE(r.up_down == 1.0f);
    REQUIRE_FALSE(r.far_plane);
}

TEST_CASE("FrameXr hudmove: the settings", "[frame_xr][hudmove]") {
    // 100 % (the game's own) by default, per game, the range of vr_hud_spacing.h.
    auto& across = Settings::values.vr_immersive_hud_spacing_x;
    auto& up_down = Settings::values.vr_immersive_hud_spacing_y;
    REQUIRE(across.GetDefault() == kDefaultSpacing);
    REQUIRE(up_down.GetDefault() == kDefaultSpacing);
    for (auto* setting : {&across, &up_down}) {
        const s32 kept = setting->GetValue();
        setting->SetValue(1000);
        REQUIRE(setting->GetValue() == kMaxSpacing);
        setting->SetValue(-1000);
        REQUIRE(setting->GetValue() == kMinSpacing);
        setting->SetValue(kept);
    }
}
