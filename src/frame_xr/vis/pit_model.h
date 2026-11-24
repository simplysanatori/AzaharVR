// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuhand: Pit's arm from a file of the player's own: a skinned forearm and hand
// with its textures, in this app's own small format, read from the user directory
// (kPitModelFile). Nothing here knows where such a file comes from, and none ships: without one
// Pit's Arm is simply not drawn. The file is not trusted: every size, count, index and number
// is checked before use (ParsePitModel), and a file that fails is refused whole.
// No OpenXR or Vulkan here (unit tested in src/tests/frame_xr/pit_model.cpp, with a tiny model
// made in the test).
//
//   Space    The arm's (pit_hand.h): the wrist at the origin, the forearm toward -x, the
//            fingers +x, the back of the hand +y, the thumb +z; in metres, a left arm. The right
//            arm is its mirror, drawn with a mirroring fit and the culling turned over, and
//            without the plate (the last `plate_index_count` indices: what only the left arm
//            wears).
//   Drawing  As Pit's hand of our own: 12 floats a vertex (kPitVertexFloats: position, normal,
//            tangent, uv), 32 bit indices, one colour image (sRGB, ambient occlusion in alpha)
//            and one surface image, with the shader of pit_hand_shader.h.
//   Posing   The app's poses (pit_hand.h PitHandPose: from the controller's touches, eased) put
//            on the file's joints by their roles (PitHandJoint): a finger's or the thumb's joint
//            takes our joint's own orientation, a joint marked relative (the palm's) only its
//            turn from the flat hand, a joint without a role goes with its parent. Bone lengths
//            are the file's. The forearm turns about the wrist toward the elbow. The
//            controller's handle lies in the palm: no finger closes through it.
//
// ---- The hand file (version 1), little endian, no padding ----
//
//   bytes     what
//   8         "PITHAND1"
//   8 x u32   version (1); vertices V (3 to 200 000); body indices B (a multiple of 3, above
//             0); plate indices P (a multiple of 3, may be 0; B + P at most 1 200 000); joints
//             J (1 to 255); texture width W; height H (each 1 to 4096); 0
//   4 x f32   hand length (the wrist to the middle finger's tip, metres, 0.02 to 1); forearm
//             radius; its radius at the wrist end (the largest distance of anything that hangs
//             on the forearm from the forearm's line, all of it and its last 6 cm: what a
//             panel on the wrist must stay outside of); 0
//   3 x f32   elbow: the forearm's far end in the bind pose
//   7 x f32   handle: the Steam Frame controller's handle where it lies in the palm: centre,
//             axis (toward the thumb's side), radius. Radius 0: none (no finger is held off it)
//   16 x f32  fit: arm space to the LEFT controller's OpenXR aim space, column major, a
//             rotation and a translation (no scale, last row 0 0 0 1)
//   3 x f32   pivot: the point of aim space a scale keeps (on the handle)
//   J x 68    joints, a parent always before its children:
//               i32 parent (-1: none)
//               i32 role: -1, or the pit_hand.h PitHandJoint this joint is posed as (each at
//                   most once): 0 palm, 1 wrist, 2-5 thumb metacarpal / proximal / distal /
//                   tip, 6-10 index metacarpal / proximal / intermediate / distal / tip, 11-15
//                   middle, 16-20 ring, 21-25 little (the same five), 26 forearm, 27 elbow
//               u32 flags: 1 = relative (see Posing)
//               3 x f32 position in the bind pose
//               9 x f32 frame, row major: the joint's axes in the bind pose as OpenXR's hand
//                   joints have them (columns: +X to the right, +Y the back of the hand or
//                   finger, +Z back along the bone, away from the finger tip), a rotation
//               f32 thickness: the flesh's radius round the joint (0: not known; 0 to 0.2)
//   V x 48    vertices in the bind pose: position 3, normal 3, tangent 4 (xyz the direction
//             the texture's u grows in, w -1 or 1: which way its v-up lies), uv 2 (v down:
//             0 the image's first row). f32 each; within 2 m of the origin
//   V x 8     skin: 4 x u8 joint numbers, 4 x u8 weights in falling order summing to 255
//   (B+P) x u32  triangles, counter-clockwise from outside: the body first (drawn on both
//             arms), then the plate (what the left arm alone wears; the mirrored right arm is
//             drawn without it)
//   W x H x 4 colour image: sRGB red green blue, ambient occlusion in alpha (255 open)
//   W x H x 4 surface image, linear: red, green the normal's x and y in tangent space (128
//             flat); blue the opacity (255 solid; lower only where the arm fades out toward
//             the shoulder); alpha the material: 26 gold, 77 enamel, 128 leather, 179 skin,
//             230 cloth (pit_hand_shader.h: bands 51 wide; a texture may blend two neighbours
//             in that order, never two that are further apart)
//   The file's size must be exactly the sum of these. Images are sampled linear, clamped,
//   without smaller levels, so each material's island needs a few texels of its own colour
//   round it.
//
// Space and pose: arm space as above, metres, the size it is drawn at with a scale of 1. The
// bind pose is whatever the mesh was made in: a flat or nearly flat open hand, the forearm
// along -x, serves best (a joint that is not relative is turned from its own frame to our
// joint's, however the bind pose has it).
//
// A file from memory (a hand built into the app) is read by ParsePitModel just as one on disk
// by LoadPitModel.

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "frame_xr/vis/pit_hand.h"

namespace FrameXr::Vis {

/// Under the user directory.
inline constexpr const char* kPitModelFile = "kiu_hand/pit_hand.bin";

/// The most a file may hold (a real one is far under these).
inline constexpr uint32_t kPitModelMaxVertices = 200000;
inline constexpr uint32_t kPitModelMaxIndices = 1200000;
inline constexpr uint32_t kPitModelMaxJoints = 255;
inline constexpr uint32_t kPitModelMaxTexture = 4096;

struct PitModelJoint {
    int parent = -1;
    int role = -1;         ///< PitHandJoint, or -1
    bool relative = false; ///< takes only the turn from the flat hand
    PitVec3 position{};
    std::array<float, 9> frame{1, 0, 0, 0, 1, 0, 0, 0, 1}; ///< row major
    float radius = 0.0f; ///< the flesh's radius round the joint, metres
};

struct PitModel {
    uint32_t vertex_count = 0;
    uint32_t body_index_count = 0;  ///< the first indices: both arms
    uint32_t plate_index_count = 0; ///< the rest: the left arm only
    std::vector<float> vertices;    ///< the bind pose, kPitVertexFloats each
    std::vector<uint8_t> bones;     ///< 4 a vertex
    std::vector<uint8_t> weights;   ///< 4 a vertex, summing to 255, in falling order
    std::vector<uint32_t> indices;
    std::vector<PitModelJoint> joints;
    uint32_t texture_width = 0, texture_height = 0;
    std::vector<uint8_t> colour;  ///< RGBA8, sRGB, ambient occlusion in alpha
    std::vector<uint8_t> surface; ///< RGBA8, linear
    float hand_length = 0.0f;     ///< metres
    float forearm_radius = 0.0f;  ///< the drawn forearm's largest radius about its bone, metres
    float wrist_radius = 0.0f;    ///< the same for its wrist end
    PitVec3 elbow{};              ///< where the forearm's far end is in the bind pose
    PitHandHandle handle{};       ///< the controller's handle in the palm
    PitMat4 fit{};                ///< arm space to the left controller's aim space
    PitVec3 fit_pivot{};
};

/// Reads and checks a file's bytes. False with the reason in `error` (and `out` left empty).
bool ParsePitModel(std::span<const uint8_t> file, PitModel& out, std::string& error);
/// The same from a path. A missing file is false with an empty `error`: nothing to report.
bool LoadPitModel(const std::string& path, PitModel& out, std::string& error);

/// How much of a finger's thickness may sink into the handle before the finger is opened: flesh
/// gives where it presses.
inline constexpr float kPitModelPress = 0.92f;

/// Each joint's matrix for the mesh in a pose (from the bind pose to it). `elbow`: where the
/// elbow is, in arm space (metres): the forearm turns about the wrist to point at it; none: as
/// the file has it. `pose` none: the file's own bind pose (every matrix the identity).
/// `clear_handle`: the controller's handle is in the palm (the file's `handle`), so a finger or
/// the thumb that the pose would close through it is opened until it lies on it: a fist is a
/// fist round the handle. False: the pose as it is.
std::vector<PitMat34> PitModelEvaluate(const PitModel& model, const PitHandPose* pose,
                                       const PitVec3* elbow = nullptr, bool clear_handle = true);
/// The vertices in a pose: kPitVertexFloats each, model.vertex_count of them into `out`.
void PitModelSkin(const PitModel& model, std::span<const PitMat34> bones, std::span<float> out);
/// The hand on the Steam Frame controller: arm space to the controller's aim space; `scale`
/// about the handle (the fit's pivot). For the right hand it mirrors (pit_hand.h: Right hands).
PitMat4 PitModelControllerFit(const PitModel& model, bool left_hand, float scale = 1.0f);

} // namespace FrameXr::Vis
