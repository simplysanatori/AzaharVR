// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame hudmove: Move HUD In (video_core/vr_hud_move.h): which vertices move (the vertex
// shader's 2D rule), how far, when the XR thread turns it on, and the plumbing to the shader.

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "common/settings.h"
#include "video_core/shader/generator/glsl_shader_gen.h"
#include "video_core/shader/generator/shader_uniforms.h"
#include "video_core/vr_hud_move.h"
#include "video_core/vr_immersive.h"

using Catch::Approx;
using namespace VideoCore::HudMove;

namespace {
// The vertex shader's rule before Move HUD In (immersive_scale.cpp pins the same text).
Clip Before(Clip p, float factor, float factor_y) {
    float fx = factor_y, fy = factor;
    if (std::abs(p.w - 1.0f) < 0.0001f) {
        fx = std::abs(p.x) >= 0.999f ? 1.0f : fx;
        fy = std::abs(p.y) >= 0.999f ? 1.0f : fy;
    }
    return {p.x / fx, p.y / fy, p.w};
}
} // namespace

TEST_CASE("FrameXr hudmove: percent to scale", "[frame_xr][hudmove]") {
    REQUIRE(ScaleForPercent(0) == 1.0f);
    REQUIRE(ScaleForPercent(10) == Approx(0.9f));
    REQUIRE(ScaleForPercent(-10) == Approx(1.1f));
    // The range ends: +40 % in, 20 % out; past them clamped.
    REQUIRE(ScaleForPercent(kMaxPercent) == Approx(0.6f));
    REQUIRE(ScaleForPercent(kMinPercent) == Approx(1.2f));
    REQUIRE(ScaleForPercent(1000) == ScaleForPercent(kMaxPercent));
    REQUIRE(ScaleForPercent(-1000) == ScaleForPercent(kMinPercent));
    // Every value of the range is one the rasterizer takes.
    for (int p = kMinPercent; p <= kMaxPercent; ++p) {
        REQUIRE(UsableScale(ScaleForPercent(p)) == ScaleForPercent(p));
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
        REQUIRE(ForFrame(true, mode, 20, 10, 0.0f).IsIdentity());
    }
    REQUIRE(ForFrame(false, 2, 20, 10, 0.0f).IsIdentity());
    REQUIRE(ForFrame(false, 3, 20, 10, 0.0f).IsIdentity());
    // The Full modes: each axis its own value.
    for (unsigned mode : {2u, 3u}) {
        const Scales s = ForFrame(true, mode, 20, 10, 0.0f);
        REQUIRE(s.across == Approx(0.8f));
        REQUIRE(s.up_down == Approx(0.9f));
    }
    // Both 0 (the default): exactly 1, so the shader divides by exactly 1.
    REQUIRE(ForFrame(true, 3, 0, 0, 0.0f).IsIdentity());
    // Mode 2's menu rule eases it back: halfway at weight 0.5, none at 1.
    const Scales half = ForFrame(true, 2, 20, -10, 0.5f);
    REQUIRE(half.across == Approx(0.9f));
    REQUIRE(half.up_down == Approx(1.05f));
    REQUIRE(ForFrame(true, 2, 20, -10, 1.0f).IsIdentity());
    // A weight out of range or not a number is taken in range or as 0.
    REQUIRE(ForFrame(true, 2, 20, 0, 3.0f).IsIdentity());
    REQUIRE(ForFrame(true, 2, 20, 0, -1.0f).across == Approx(0.8f));
    REQUIRE(ForFrame(true, 2, 20, 0, std::numeric_limits<float>::quiet_NaN()).across ==
            Approx(0.8f));
}

TEST_CASE("FrameXr hudmove: which vertices move", "[frame_xr][hudmove]") {
    const Scales none{};
    const Scales in{0.8f, 0.9f}; // 20 % across, 10 % up and down
    const float factor = 1.55f, factor_y = 1.7f;

    SECTION("at 0 % every vertex is exactly where it was") {
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
        // Negative values move it out.
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
    SECTION("an element keeps its order and its middle moves by the scale") {
        // A 40 x 24 pixel element at the right edge of a 400 x 240 picture (clip y 0.8 to 1.0
        // would touch the edge; this one stops at 0.98): it moves in by 20 % of its distance.
        const float left = 0.78f, right = 0.98f;
        const Clip l = ApplyVertex({0.1f, left, 1.0f}, 1.0f, 1.0f, in);
        const Clip r = ApplyVertex({0.1f, right, 1.0f}, 1.0f, 1.0f, in);
        REQUIRE(l.y < r.y);
        REQUIRE((l.y + r.y) / 2.0f == Approx((left + right) / 2.0f * 0.8f));
        REQUIRE(r.y - l.y == Approx((right - left) * 0.8f)); // and gets that much smaller
        // In 3DS pixels (200 from the middle to the side): +10 % moves the edge 20 pixels in.
        const Clip ten = ApplyVertex({0.0f, 0.998f, 1.0f}, 1.0f, 1.0f, {ScaleForPercent(10), 1.0f});
        REQUIRE((0.998f - ten.y) * 200.0f == Approx(19.96f).margin(0.01f));
    }
}

// The shader's text and the uniform block it reads: the two scales after the up and down
// factor, defaulting to 1, the block's size unchanged.
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
    REQUIRE(vs.find("gl_Position.xy /= VrFactor(gl_Position);") != std::string::npos);

    using Pica::Shader::Generator::VSUniformData;
    const VSUniformData data{};
    REQUIRE(data.vr_hud_up_down == 1.0f);
    REQUIRE(data.vr_hud_across == 1.0f);
    // std140: plain floats packed after the factors (as the GLSL block declares them).
    REQUIRE(offsetof(VSUniformData, vr_hud_up_down) ==
            offsetof(VSUniformData, vr_immersive_mode_factor_y) + 4);
    REQUIRE(offsetof(VSUniformData, vr_hud_across) ==
            offsetof(VSUniformData, vr_hud_up_down) + 4);
}

TEST_CASE("FrameXr hudmove: the scales reach the emulation thread", "[frame_xr][hudmove]") {
    using VideoCore::Immersive::Mailbox;
    using VideoCore::Immersive::VRData;
    Mailbox box;
    VRData d;
    REQUIRE(d.hud.IsIdentity()); // CitraVR's "off" values move nothing
    d.mode = 3;
    d.hud = ForFrame(true, 3, 15, 5, 0.0f);
    box.Publish(d);
    VRData out;
    u32 serial = 0;
    REQUIRE(box.Read(out, serial));
    REQUIRE(out.hud == d.hud);
    box.Publish(VRData{});
    REQUIRE(box.Read(out, serial));
    REQUIRE(out.hud.IsIdentity());
}

TEST_CASE("FrameXr hudmove: the settings", "[frame_xr][hudmove]") {
    // Off by default, per game, the range of vr_hud_move.h.
    auto& across = Settings::values.vr_immersive_hud_across;
    auto& up_down = Settings::values.vr_immersive_hud_up_down;
    REQUIRE(across.GetDefault() == 0);
    REQUIRE(up_down.GetDefault() == 0);
    for (auto* setting : {&across, &up_down}) {
        const s32 kept = setting->GetValue();
        setting->SetValue(1000);
        REQUIRE(setting->GetValue() == kMaxPercent);
        setting->SetValue(-1000);
        REQUIRE(setting->GetValue() == kMinPercent);
        setting->SetValue(kept);
    }
}
