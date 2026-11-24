// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuhand: the fragment shader for Pit's arm (pit_model.h): drawn the way the
// character is painted rather than as the controllers' plastic. It takes the place of
// model_shaders.h kModelFrag for that draw alone and has the same inputs: the push constants
// (mvp, eye, key, up, params: z the fade, w + 1 a mirrored model), the two images and the four
// vertex inputs of kModelVert, with OUT_LINEAR defined as there. Its output is premultiplied by
// its alpha (the fade times the surface image's opacity), so it wants the blended pipeline; drawn
// without blending the arm's faded end is simply solid.
//
//   Colour image   sRGB colour, ambient occlusion in alpha.
//   Surface image  red, green: the normal's x and y in tangent space (128 flat; green up);
//                  blue: opacity (255 solid; less where the arm fades out toward the shoulder);
//                  alpha: the material, in bands, so that filtering between two neighbours
//                  never passes through a third:
//                    0.0 - 0.2 gold    a bright band, a warm middle, an olive shade, one hot
//                                      spot: reads as metal with no surroundings to mirror
//                    0.2 - 0.4 enamel  glossy colour (the plate's red)
//                    0.4 - 0.6 leather matte, a faint sheen
//                    0.6 - 0.8 skin    two soft tones, the shade warm, a rosy edge between
//                                      them, a thin light rim
//                    0.8 - 1.0 cloth   matte, a cool shade
// Two texture reads, no loops.

#pragma once

namespace FrameXr::Vis::Shaders {

inline constexpr const char* kPitHandFrag = R"(
layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 eye;
    vec4 key;
    vec4 up;
    vec4 params;
} pc;

layout(set = 0, binding = 0) uniform sampler2D u_colour;
layout(set = 0, binding = 1) uniform sampler2D u_surface;

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec4 v_tangent;
layout(location = 3) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

vec3 linearToSrgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

// One material's share of this pixel: 1 inside its band of the surface's alpha.
float band(float m, float lo, float hi) {
    return smoothstep(lo - 0.03, lo + 0.03, m) * (1.0 - smoothstep(hi - 0.03, hi + 0.03, m));
}

void main() {
    vec4 base = texture(u_colour, v_uv);
    vec4 surf = texture(u_surface, v_uv);
    vec3 albedo = base.rgb;
    float ao = base.a;

    // The normal: the vertex's, bent by the surface image.
    vec3 ng = normalize(v_normal);
    bool front = mod(pc.params.w, 2.0) > 0.5 ? !gl_FrontFacing : gl_FrontFacing;
    if (!front) ng = -ng;
    vec3 t = v_tangent.xyz - ng * dot(ng, v_tangent.xyz);
    t = dot(t, t) > 1e-10 ? normalize(t) : vec3(1.0, 0.0, 0.0);
    vec3 b = cross(ng, t) * (v_tangent.w < 0.0 ? -1.0 : 1.0);
    vec2 bump = (surf.rg - vec2(128.0 / 255.0)) * 2.0;
    vec3 n = normalize(t * bump.x + b * bump.y + ng * sqrt(max(1.0 - dot(bump, bump), 0.05)));

    vec3 V = normalize(pc.eye.xyz - v_position);
    vec3 L = normalize(pc.key.xyz);
    vec3 U = normalize(pc.up.xyz);
    float ndl = dot(n, L);
    float ndv = max(dot(n, V), 0.0);
    float sky = 0.5 + 0.5 * dot(n, U);

    float m = surf.a;
    float gold = 1.0 - smoothstep(0.17, 0.23, m);
    float enamel = band(m, 0.2, 0.4);
    float leather = band(m, 0.4, 0.6);
    float skin = band(m, 0.6, 0.8);
    float cloth = smoothstep(0.77, 0.83, m);

    // How lit: the key light, helped a little by the sky so a face turned up is never in shade,
    // and held back where the surface is hidden from the open air.
    float d = ndl * 0.85 + (sky - 0.5) * 0.3 - (1.0 - ao) * 0.35;
    float soft = 0.22 * skin + 0.30 * (cloth + leather) + 0.10 * (gold + enamel);
    float lit = smoothstep(0.08 - soft, 0.08 + soft, d);

    // Each material's shade (times its colour), and what its lit side gains.
    vec3 shade = skin * vec3(0.90, 0.63, 0.52) + cloth * vec3(0.46, 0.50, 0.62) +
                 leather * vec3(0.50, 0.42, 0.42) + gold * vec3(0.50, 0.43, 0.16) +
                 enamel * vec3(0.48, 0.30, 0.34);
    vec3 gain = skin * vec3(1.10, 1.02, 0.93) + cloth * vec3(1.02) + leather * vec3(1.06, 1.0, 0.96) +
                gold * vec3(1.06, 1.0, 0.82) + enamel * vec3(1.05);
    vec3 colour = albedo * mix(shade, gain, lit);

    // Skin: a rosy edge where light turns to shade, as light through flesh.
    float edge = lit * (1.0 - lit) * 4.0;
    colour += skin * edge * albedo * vec3(0.16, 0.02, -0.03);

    // Hidden places stay warm on skin, plainly darker elsewhere.
    float hidden = 1.0 - smoothstep(0.25, 0.95, ao);
    colour *= mix(vec3(1.0), skin * vec3(0.90, 0.72, 0.64) + (1.0 - skin) * vec3(0.62), hidden * 0.7);

    // Gold: what a mirror would show of a plain sky: bright where it looks up or at the key
    // light, olive where it looks down, and one small hot spot.
    vec3 R = reflect(-V, n);
    float above = 0.5 + 0.5 * dot(R, U);
    float glance = max(dot(R, L), 0.0);
    vec3 metal = albedo * mix(vec3(0.42, 0.37, 0.13), vec3(1.30, 1.18, 0.78), smoothstep(0.25, 0.80, above)) +
                 albedo * vec3(0.55, 0.50, 0.30) * pow(glance, 6.0) +
                 vec3(1.0, 0.95, 0.75) * pow(glance, 90.0) * 0.9;
    colour = mix(colour, metal * mix(0.55, 1.0, smoothstep(0.2, 0.9, ao)), gold * 0.75);

    // Gloss: enamel sharp, leather faint and wide, skin a breath; cloth none.
    vec3 H = normalize(L + V);
    float ndh = max(dot(n, H), 0.0);
    colour += (enamel * 0.45 * pow(ndh, 70.0) + leather * 0.07 * pow(ndh, 14.0) + skin * 0.05 * pow(ndh, 28.0)) *
              smoothstep(0.0, 0.3, ndl);

    // A thin rim of light at the outline, on the lit side: skin most, cloth and leather a little.
    float rim = pow(1.0 - ndv, 4.0) * smoothstep(-0.2, 0.5, ndl);
    colour += rim * (skin * vec3(0.30, 0.24, 0.18) + cloth * vec3(0.10, 0.12, 0.16) + leather * vec3(0.10, 0.07, 0.05) +
                     gold * vec3(0.25, 0.22, 0.10));

    float a = clamp(pc.params.z, 0.0, 1.0) * clamp(surf.b * 1.02, 0.0, 1.0);
    vec3 v = OUT_LINEAR ? clamp(colour, 0.0, 1.0) : linearToSrgb(colour);
    fragColor = vec4(v * a, a);
}
)";

} // namespace FrameXr::Vis::Shaders
