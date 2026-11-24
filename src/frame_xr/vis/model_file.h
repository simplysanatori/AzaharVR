// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame
// The Steam Frame controller models: the files `azahar --frame-controller-models` makes
// at install time from SteamVR's own render models (never shipped: Valve's), and the moving parts
// posed from the controls. The format is Moonlight XR Native's MXFM (same author; the reader and
// the posing follow its rooms/room_gpu.c), version 2, with flags 1: the vertices are in the
// controller's OpenXR aim space (the converter's --space aim), so a model is drawn at the aim
// pose this port already locates. No OpenXR or Vulkan here (unit tested).
//
//   header      'MXFM', version, vertexCount, indexCount, componentCount, texWidth, texHeight, flags
//   components  208 bytes each: name[32], input path[32], firstIndex, indexCount, motion type
//               (0 none, 1 translate, 2 rotate, 3 joystick, 4 d-pad), 0, 32 floats of motion
//   vertices    12 floats each: position 3, normal 3, tangent 4, uv 2
//   indices     uint32 triangles
//   textures    texWidth x texHeight RGBA8 twice: colour (sRGB rgb, AO a), surface (normal rgb,
//               OpenGL green, roughness a)

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace FrameXr::Vis {

/// What drives a moving part (Moonlight XR's MLXR_IN_*).
enum ModelInput : int {
    kInTrigger,
    kInGrip,
    kInStickX,
    kInStickY,
    kInStickClick,
    kInA,
    kInB,
    kInX,
    kInY,
    kInMenu,
    kInView,
    kInSystem,
    kInBumper,
    kInDpadUp,
    kInDpadDown,
    kInDpadLeft,
    kInDpadRight,
    kModelInputs,
};
using ModelInputs = std::array<float, kModelInputs>;

constexpr uint32_t kModelVertexFloats = 12;
constexpr uint32_t kModelFlagAimSpace = 1;
/// AzaharFrame kiuguns: a game's model (core/frame_weapon_models.h: Kid Icarus: Uprising's
/// weapons): the vertices in the space of the weapon's anchor bone, in the game's units, fitted
/// to the hand when drawn (frame_xr/vis/weapon_fit.h). Bits 8-10 and 12-14 of the flags are the
/// texture's wrap modes across and down (PICA's: 0 clamp to edge, 1 clamp to border, 2 repeat,
/// 3 mirrored repeat). The controller reader refuses these files, and the weapon reader the
/// controllers'.
constexpr uint32_t kModelFlagWeaponSpace = 2;
constexpr uint32_t kModelFlagSpaceMask = 0xFF;
/// AzaharFrame kiuguns: weapon space only: the surface image is one texel (a flat normal and one
/// roughness for the whole model), not texWidth x texHeight.
constexpr uint32_t kModelFlagFlatSurface = 1u << 16;
/// AzaharFrame kiuflash: weapon space only: a muzzle flash's cards (core/frame_weapon_models.h
/// FlashValue: each component a card, its values in the motion floats); the colour image's alpha
/// is the texture's own, not ambient occlusion.
constexpr uint32_t kModelFlagFlash = 1u << 17;
/// AzaharFrame kiuflash: weapon space only: a projectile's bullet (core/frame_weapon_models.h
/// BulletValue: each component a kind, rapid or charged, its look in the motion floats; the vertex
/// colour in the tangent's place); the colour image's alpha is the texture's own.
constexpr uint32_t kModelFlagBullet = 1u << 18;
constexpr uint32_t ModelWrapU(uint32_t flags) {
    return (flags >> 8) & 7u;
}
constexpr uint32_t ModelWrapV(uint32_t flags) {
    return (flags >> 12) & 7u;
}

struct ModelComponent {
    std::string name;
    std::string path;
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    uint32_t motion_type = 0;
    int input = -1; ///< ModelInput, or -1
    std::array<float, 32> motion{};
};

struct ModelFile {
    uint32_t version = 0;
    uint32_t flags = 0;
    std::vector<float> vertices; ///< kModelVertexFloats per vertex
    std::vector<uint32_t> indices;
    std::vector<ModelComponent> components;
    uint32_t tex_width = 0, tex_height = 0;
    std::vector<uint8_t> colour;  ///< tex_width * tex_height * 4
    std::vector<uint8_t> surface; ///< the same size (one texel with kModelFlagFlatSurface)
    /// AzaharFrame kiuguns: the surface image's size (texWidth x texHeight, or 1 x 1).
    uint32_t SurfaceWidth() const {
        return flags & kModelFlagFlatSurface ? 1 : tex_width;
    }
    uint32_t SurfaceHeight() const {
        return flags & kModelFlagFlatSurface ? 1 : tex_height;
    }
};

/// Reads and checks one hand's file. False with the reason in `error`. AzaharFrame kiuguns:
/// `space` the kind wanted: kModelFlagAimSpace (a controller: flags exactly 1, as always) or
/// kModelFlagWeaponSpace (a game's model: the space 2, wrap bits allowed).
bool LoadModelFile(const std::string& path, ModelFile& out, std::string& error,
                   uint32_t space = kModelFlagAimSpace);

/// AzaharFrame kiu140w: a weapon part's see-through file (core/frame_weapon_models.h
/// WriteSeeThrough), beside its model: how see-through each spot of it is.
enum class SeeFileKind : uint8_t { Solid, Cut, Blend, Add }; ///< FrameWeapons::SeeKind
struct SeeFile {
    SeeFileKind kind = SeeFileKind::Solid;
    bool unlit = false;
    bool depth_write = true;
    bool slides = false;
    float reference = 0.0f;
    uint32_t width = 0, height = 0;
    uint32_t wrap_u = 2, wrap_v = 2;
    std::vector<float> vertices; ///< kSeeVertexFloats per vertex: u, v, factor, gain, colour
    std::vector<uint8_t> image;  ///< width * height * 4: the colour's factor, the alpha
};
constexpr uint32_t kSeeVertexFloats = 7;
/// Reads and checks one; `vertex_count`: its model's. False with the reason in `error`.
bool LoadSeeFile(const std::string& path, std::size_t vertex_count, SeeFile& out,
                 std::string& error);
/// The model's vertices as a see-through draw takes them (model_shaders.h, flag +64): the image's
/// coordinates, the alpha's factor and its gain in the tangent's place, and, unlit, the colour's
/// factor in the normal's.
std::vector<float> SeeVertices(const std::vector<float>& model_vertices, const SeeFile& see);

/// One draw of a model: an index range and its transform (column major 4x4, in model space).
struct ModelPart {
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    std::array<float, 16> transform{};
};

/// The parts posed by the controls (SteamVR's motion rules), the ones at rest merged into as few
/// draws as their index ranges allow.
std::vector<ModelPart> PoseModel(const ModelFile& model, const ModelInputs& in);

} // namespace FrameXr::Vis
