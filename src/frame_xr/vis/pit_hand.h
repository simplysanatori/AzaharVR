// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuhand: Pit's hand: a left forearm and hand of our own for Kid Icarus: Uprising's
// first person, with fingers that move: in place of the game's closed fist (weapon_fit.h
// kiuarm). The mesh is ours, made by tools/kiu_hand (gen.py writes pit_hand_data.inc): no game
// mesh, texture or artwork is in it; the player's own game can give its colours at run time
// (PitHandColoursFrom). No OpenXR or Vulkan here (unit tested in
// src/tests/frame_xr/pit_hand.cpp).
//
//   Space   The arm's, as the game's arm has it (weapon_fit.h): the wrist at the origin, the
//           forearm toward -x, the fingers +x, the back of the hand +y, the thumb +z, in arm
//           units of kPitHandUnit metres (weapon_fit.h kArmScale), so FitOffArm and
//           FitHoldingArm place it as they place the game's arm. A left hand; the right is its
//           mirror (x turned over), as the game's arm is drawn on the right hand.
//   Parts   The skin of the hand, the cloth wrap (round the wrist, up the back of the hand, a
//           loop on the middle finger), five nails, the forearm's skin toward the elbow, the
//           leather vambrace, its two bands and its stitched seam (PitHandParts). Each is closed.
//   Bones   OpenXR's hand joints (XR_HAND_JOINT_*_EXT, the same order and axes: -Z toward the
//           finger tip, +Y the back of the hand, +X to the right), then the forearm (at the
//           wrist) and the elbow. A vertex hangs on at most four.
//   Poses   A pose is every joint's turn in its parent's frame (PitHandPose). Named ones
//           (PitHandNamedPose), ones from the controller's touches and pulls
//           (PitHandCurlsFromInputs, PitHandPoseFromCurls), and ones from hand tracking's 26
//           joint poses (PitHandPoseFromJoints: their turns on our bones, whose lengths stay
//           ours).
//   Drawing PitHandEvaluate gives the bones' matrices, PitHandSkin the vertices (12 floats
//           each, as model_file.h: position, normal, tangent, uv) to draw with PitHandIndices
//           and the palette (PitHandPalette: a small image of our own, a row of shades for
//           each material; the uv picks the material and its baked shade). Skin again only
//           when the pose changed (PitHandPoseDiffers).
//
// Two families of poses, by whether something is drawn in the hand:
//   Free   nothing in the hand: open, relaxed, fist, point, thumb up;
//   Held   round the Steam Frame controller's handle (a cylinder kPitHeldRadius thick lying
//          under the knuckles: PitHandHeld), the index finger on the trigger, the thumb on the
//          stick, the buttons or the rest behind them, or lifted. PitHandControllerFit places
//          the hand on the controller for these (the handle through the fist, the trigger under
//          the index finger); FitOffArm's Point frame with PitHandHeld().centre as the fist
//          works too, with the handle then crossing the fingers at an angle.

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace FrameXr::Vis {

/// Metres an arm unit (weapon_fit.h kArmScale).
inline constexpr float kPitHandUnit = 0.075f;

/// The joints: OpenXR's 26 in XR_HAND_JOINT_*_EXT order, then ours.
enum PitHandJoint : int {
    kPitPalm = 0,
    kPitWrist,
    kPitThumbMetacarpal,
    kPitThumbProximal,
    kPitThumbDistal,
    kPitThumbTip,
    kPitIndexMetacarpal,
    kPitIndexProximal,
    kPitIndexIntermediate,
    kPitIndexDistal,
    kPitIndexTip,
    kPitMiddleMetacarpal,
    kPitMiddleProximal,
    kPitMiddleIntermediate,
    kPitMiddleDistal,
    kPitMiddleTip,
    kPitRingMetacarpal,
    kPitRingProximal,
    kPitRingIntermediate,
    kPitRingDistal,
    kPitRingTip,
    kPitLittleMetacarpal,
    kPitLittleProximal,
    kPitLittleIntermediate,
    kPitLittleDistal,
    kPitLittleTip,
    kPitXrJoints,                 ///< 26: how many are OpenXR's
    kPitForearm = kPitXrJoints,   ///< at the wrist, turned toward the elbow
    kPitElbow,                    ///< the forearm's far end
    kPitHandJoints,
};
/// A joint's parent (-1: none: the wrist and the forearm).
int PitHandParent(int joint);

struct PitQuat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};
using PitVec3 = std::array<float, 3>;
/// Row major 3x4: a rotation and a translation (arm units).
using PitMat34 = std::array<float, 12>;
/// Column major 4x4 (as weapon_fit.h FitMat).
using PitMat4 = std::array<float, 16>;

/// A pose: each joint's rotation in its parent's frame (the wrist's and the forearm's in arm
/// space). The bones' lengths are not part of it.
struct PitHandPose {
    std::array<PitQuat, kPitHandJoints> local{};
};

enum class PitHandNamed : uint8_t {
    Bind,        ///< the pose the mesh was made in: open, the fingers a little apart
    Open,        ///< free: flat and spread
    Relaxed,     ///< free: at rest, the fingers half curled
    Fist,        ///< free: closed, the thumb across the fingers
    Point,       ///< free: the index finger out, the rest a fist
    ThumbUp,     ///< free: a fist with the thumb up
    Grip,        ///< held: round the handle, the index on the trigger, the thumb on its rest
    Trigger,     ///< held: Grip with the trigger pulled
    GripThumbUp, ///< held: Grip with the thumb lifted off the controller
    GripStick,   ///< held: Grip with the thumb on the stick
    GripButtons, ///< held: Grip with the thumb on the face buttons
    GripOpen,    ///< held: the fingers lifted off the handle and the trigger, the thumb lifted
    Count,
};
const PitHandPose& PitHandNamedPose(PitHandNamed name);
std::string_view PitHandNamedLabel(PitHandNamed name);

/// What the Steam Frame controller tells of the hand on it.
struct PitHandInputs {
    bool trigger_touch = false;
    float trigger = 0.0f; ///< 0 to 1
    bool grip_touch = false;
    float grip = 0.0f; ///< 0 to 1
    bool thumb_stick = false;   ///< the thumb touches the stick
    bool thumb_buttons = false; ///< ... a face button
    bool thumb_rest = false;    ///< ... the thumb rest (or the trackpad)
};
enum class PitHandGrasp : uint8_t { Free, Held };
enum class PitHandThumb : uint8_t { Up, Rest, Stick, Buttons };

/// How far each finger is closed (0 open to 1 closed) and how far the fingers are apart (1
/// spread, 0 together), from the controller. The table:
///
///   finger            not touched         touched             pulled or squeezed (value v)
///   index             0.5 v               0.5 + 0.5 v         (the trigger's touch and value)
///   middle to little  v                   0.7 + 0.3 v         (the grip's touch and value)
///   thumb             0 (Up)              1, on what it touches: the stick, else a button,
///                                         else the rest
///   spread            1 less the mean of the four fingers' curls
///
/// Free takes the index from Point (0) through Relaxed (0.5) to Fist (1), the other fingers
/// from Relaxed to Fist, and the thumb between four poses: lifted it goes from Open's to Thumb
/// Up's as the other fingers close, down from Relaxed's to Fist's. Held takes the index from
/// lifted off the trigger (0) through resting on it (0.5) to pulled (1), the other fingers from
/// Grip Open to Grip, and the thumb to where it touches.
struct PitHandCurls {
    float thumb = 0.0f;
    float index = 0.0f, middle = 0.0f, ring = 0.0f, little = 0.0f;
    float spread = 1.0f;
    PitHandThumb thumb_on = PitHandThumb::Up;
};
PitHandCurls PitHandCurlsFromInputs(const PitHandInputs& in);
PitHandPose PitHandPoseFromCurls(const PitHandCurls& curls, PitHandGrasp grasp);
inline PitHandPose PitHandPoseFromInputs(const PitHandInputs& in, PitHandGrasp grasp) {
    return PitHandPoseFromCurls(PitHandCurlsFromInputs(in), grasp);
}

/// Between two poses (t 0: a, 1: b), joint by joint.
PitHandPose PitHandBlend(const PitHandPose& a, const PitHandPose& b, float t);
/// `previous` eased toward `target` (time constant `tau_s`, 0.05 s: fingers that follow a
/// touch without snapping).
PitHandPose PitHandEase(const PitHandPose& previous, const PitHandPose& target, float dt_s,
                        float tau_s = 0.05f);
/// Whether any joint differs by more than `degrees`: skin again only then.
bool PitHandPoseDiffers(const PitHandPose& a, const PitHandPose& b, float degrees = 0.05f);

/// A hand tracking joint (XrHandJointLocationEXT's pose): any space, metres.
struct PitJointPose {
    PitQuat orientation{};
    PitVec3 position{};
};
/// The pose of a tracked hand (XR_EXT_hand_tracking's 26 joints, in its order): every joint's
/// orientation as seen from the wrist is put on our joint, so our bones point as the hand's do
/// while keeping their own lengths; positions are not used but the wrist's (PitHandTrackedFit).
/// `right_hand`: the joints are a right hand's: mirrored first (our hand is a left one; draw
/// it mirrored). `metacarpal_follow` (0 to 1): how far the four fingers' metacarpals take the
/// tracked hand's turn rather than keep ours (1: all of it; less keeps our palm's shape
/// when a runtime's palm bones differ from ours), the fingers beyond them pointing as tracked
/// either way.
PitHandPose PitHandPoseFromJoints(std::span<const PitJointPose, kPitXrJoints> joints,
                                  bool right_hand, float metacarpal_follow = 1.0f);
/// Where to draw the hand for a tracked wrist: arm space to the joints' space (the wrist on
/// the tracked wrist, turned as it is; `scale` on kPitHandUnit). For a right hand the matrix
/// mirrors (draw the left mesh with it, culling turned over, or the mirrored mesh without the
/// mirror).
PitMat4 PitHandTrackedFit(const PitJointPose& wrist, bool right_hand, float scale = 1.0f);

/// The joints of a pose, placed.
struct PitHandSkeleton {
    /// Each joint's frame in arm space (its axes as OpenXR's, its place in arm units).
    std::array<PitMat34, kPitHandJoints> joint{};
    /// Each bone's matrix for the mesh: from the bind pose to this one.
    std::array<PitMat34, kPitHandJoints> bone{};
};
/// `elbow`: where the elbow is, in arm space (arm units): the forearm turns about the wrist to
/// point at it (its length stays ours; the vambrace goes with it, the wrap bends). None: the
/// forearm straight back along -x.
PitHandSkeleton PitHandEvaluate(const PitHandPose& pose, const PitVec3* elbow = nullptr);
/// A skeleton's joints as a tracked left hand would report them (arm space taken as metres:
/// positions times kPitHandUnit), for tests and for a mock of hand tracking.
std::array<PitJointPose, kPitXrJoints> PitHandJointsOf(const PitHandSkeleton& s);

// ---- the mesh ----

enum class PitHandMaterial : uint8_t { Skin, Wrap, Leather, Band, Nail, Thread, Gold, Strap, Count };

struct PitHandPart {
    std::string_view name; ///< skin, wrap, nail, forearm, vambrace, band, seam
    PitHandMaterial material{};
    uint32_t first_index = 0;
    uint32_t index_count = 0;
};
std::span<const PitHandPart> PitHandParts();
uint32_t PitHandVertexCount();
/// Triangles (counter-clockwise from outside). `mirrored`: for the mirrored vertices
/// (PitHandSkin's `mirrored`), wound the other way so they still face out.
std::span<const uint32_t> PitHandIndices(bool mirrored = false);

/// The bind pose's vertex data, for tests and tools: positions (arm units) and normals, 3
/// floats a vertex; the bones (4 a vertex) and their weights (4 a vertex, summing to 255).
struct PitHandBind {
    std::span<const float> positions;
    std::span<const float> normals;
    std::span<const uint8_t> bones;
    std::span<const uint8_t> weights;
    std::span<const uint8_t> materials; ///< one a vertex (PitHandMaterial)
};
PitHandBind PitHandBindData();

inline constexpr uint32_t kPitVertexFloats = 12; ///< model_file.h kModelVertexFloats
/// The vertices in a pose: position 3 (arm units), normal 3, tangent 4, uv 2 (into the
/// palette), PitHandVertexCount() of them into `out`. `mirrored`: a right hand (x turned
/// over; use PitHandIndices(true)). About 0.05 ms on a desktop core.
void PitHandSkin(const PitHandSkeleton& skeleton, bool mirrored, std::span<float> out);

// ---- colours ----

struct PitRgb {
    float r = 0.0f, g = 0.0f, b = 0.0f; ///< sRGB, 0 to 1
};
/// Each material's colour: in the light, where the skin is rosy (a material without it: the
/// same as lit), and in a fold's shadow.
struct PitHandColours {
    struct Entry {
        PitRgb lit, warm, shadow;
    };
    std::array<Entry, static_cast<std::size_t>(PitHandMaterial::Count)> of{};
};
/// Ours, chosen by eye.
PitHandColours PitHandDefaultColours();
/// The defaults with the skin, the wrap, the leather and the bands taken from the player's
/// game: each a colour sampled from its own arm texture (PitHandSampleColour); the warm and
/// shadow tones follow each.
PitHandColours PitHandColoursFrom(const PitRgb& skin, const PitRgb& wrap, const PitRgb& leather,
                                  const PitRgb& band);
/// The mean colour of a region of an RGBA8 image (u, v from 0 to 1, v down; texels with alpha
/// under half left out). The default skin when nothing is there.
PitRgb PitHandSampleColour(std::span<const uint8_t> rgba, uint32_t width, uint32_t height,
                           float u0, float v0, float u1, float v1);

inline constexpr uint32_t kPitPaletteWidth = 32;  ///< shade: u 0 the shadow, 1 lit
inline constexpr uint32_t kPitPaletteHeight = 64; ///< 8 rows a material, down them lit to warm
/// The palette image (RGBA8, sRGB, alpha 255 as model_file.h's ambient occlusion), to sample
/// with clamp to edge and no mip levels.
std::vector<uint8_t> PitHandPalette(const PitHandColours& colours);
/// The surface image to go with it (model_file.h: a flat normal, roughness in alpha): the
/// bands and the wreath smoother than cloth, leather and skin.
std::vector<uint8_t> PitHandSurface();
/// A vertex's colour as the palette gives it (for tests and software previews).
PitRgb PitHandColourAt(const PitHandColours& colours, float u, float v);

// ---- where things are (arm space, arm units) ----

/// A handle the hand closes round: the cylinder's centre, its axis (toward the thumb's side)
/// and its radius.
struct PitHandHandle {
    PitVec3 centre{};
    PitVec3 axis{};
    float radius = 0.0f;
};
/// The controller's handle in the Held poses.
PitHandHandle PitHandHeld();
/// What the Fist closes round (a thin rod): its centre is the fist's.
PitHandHandle PitHandFist();
/// The bones as rounded cones in a pose, for tests of what touches what: from joint `a`
/// (radius `ra`) to `b` (radius `rb`), arm units. The fingers' phalanges, the thumb's, then
/// the palm's metacarpals.
struct PitHandCapsule {
    PitVec3 a{}, b{};
    float ra = 0.0f, rb = 0.0f;
    int joint = 0; ///< the bone it hangs on
};
std::vector<PitHandCapsule> PitHandCapsules(const PitHandSkeleton& s);

/// The forearm: the elbow's place in the bind pose, the vambrace's ends along -x, and its
/// half thickness (y) and half width (z) in its middle.
inline constexpr PitVec3 kPitElbowAt{-0.164f / kPitHandUnit, 0.0f, 0.0f};
inline constexpr float kPitVambraceWristX = -0.020f / kPitHandUnit;
inline constexpr float kPitVambraceElbowX = -0.140f / kPitHandUnit;
/// Where a wrist panel sits on the vambrace, in the forearm bone's frame of the bind pose (so
/// times PitHandSkeleton::bone[kPitForearm] in a pose): on the vambrace's two bands, midway
/// between them, on the back of the forearm (+y) or its inside (-y): the place, the outward
/// normal there, the forearm's line toward the hand, the bands' section there (half thickness
/// along the normal, half width across) and how far along the forearm each band's middle is
/// from the place (where the frame's straps go: laurel_frame.h).
struct PitHandSeat {
    PitVec3 centre{};
    PitVec3 normal{};
    PitVec3 toward_hand{1.0f, 0.0f, 0.0f};
    float arm_radius = 0.0f;
    float arm_half_width = 0.0f;
    float band_offset = 0.0f;
};
PitHandSeat PitHandPanelSeat(bool inside);

/// The hand on the Steam Frame controller for the Held poses: arm space to the controller's
/// OpenXR aim space (metres; `scale` on kPitHandUnit, about the handle's centre): the
/// handle's cylinder through the fist, the back of the hand outward, the trigger under the
/// index finger. For the right hand it mirrors (as FitOffArm's does).
PitMat4 PitHandControllerFit(bool left_hand, float scale = 1.0f);

} // namespace FrameXr::Vis
