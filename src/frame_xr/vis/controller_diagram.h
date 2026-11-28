// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame ctrldiag
// Line drawings of the Steam Frame controllers for the controls help, made on the device from
// the same model files the in-hand controllers use (model_file.h: SteamVR's own models,
// converted at install time and never shipped). A small software rasteriser draws a model from
// a fixed angle (orthographic, supersampled) and keeps, for each pixel, the part it shows, its
// depth and its facing; the outlines are where those change (silhouette, part edges, creases),
// in the style of CitraVR's controller pages. Each labelled part's visible pixels give the point
// its leader line starts from. No OpenXR, Vulkan or Qt here (unit tested).
//
// AzaharVR pcdiag: the same drawings for every controller scheme (Input::ControllerScheme):
//
//   std::array<ModelFile, 2> files; // left, right
//   std::string error;
//   if (ReadSchemeModels(scheme, files, error)) {           // off the XR thread (may convert)
//       const SchemeHands hands = DrawSchemeHands(files[0], files[1], scheme, sizes, style);
//       // hands.top[h], hands.front[h]: DiagramImage, anchor[CtrlPart] / bounds as before
//   } else {
//       // no drawings for this scheme (no SteamVR or no such models found): show the list
//   }
//
// or, for the menu's Controls tab, MakeSchemeMenuDiagramArt (menu_diagram.h). ReadSchemeModels:
// the Frame's models from ControllerModelDir as always; on a PC build (AZAHARVR_PC) a PC
// controller's from the player's own SteamVR (FrameControllerModels::SteamVrRoots), converted once
// into the user cache dir (controller-models/, again when SteamVR's files change), never shipped;
// the Frame build has only the Frame's. The parts each scheme's drawings have anchors for, by our
// mapping (the help page chooses the words; "top" and "front" as DiagramPartInFront):
//
//   Frame   left: Stick Dpad View System Bumper Trigger Grip;  right: Stick A B X Y Menu System
//           Bumper Trigger Grip
//   Touch   left: Stick X Y Menu (the three-line button) Trigger Grip;  right: Stick A B System
//           (Meta) Trigger Grip
//   Index   both: Stick Trackpad System Trigger Grip;  left: X (its lower button, SteamVR's a)
//           Y (upper, b);  right: A B
//   Focus3  as Touch: left Menu is SteamVR's left "system" button; right System (Vive)

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "frame_xr/vis/model_file.h"

namespace FrameXr::Input {
enum class ControllerScheme : uint8_t; // default_bindings.h
}

namespace FrameXr::Vis {

using Input::ControllerScheme;

/// The parts of a Frame controller the help points at. A hand has the ones its model has (the
/// left one has the D-pad and View, the right one the face buttons and Menu).
enum class CtrlPart : int {
    Stick,
    Dpad,
    View,
    Menu,
    System,
    A,
    B,
    X,
    Y,
    Bumper,
    Trigger,
    Grip,
    Trackpad, ///< AzaharVR: the Index's
    Count,
};
constexpr std::size_t kCtrlParts = static_cast<std::size_t>(CtrlPart::Count);

/// The part a model component is (by its input path), if the help uses it. AzaharVR: as our
/// mapping names it for `scheme`'s `hand` (0 left, 1 right): SteamVR's left "system" button is
/// the Menu on Touch and Focus 3, the Index's left a and b are X and Y.
std::optional<CtrlPart> PartOfComponent(const ModelComponent& component,
                                        ControllerScheme scheme = ControllerScheme{}, int hand = 0);

/// The angle a hand is drawn from: the model rolled by `roll` about its forward axis (positive
/// lifts its right side), then turned by `pitch` about its right axis (degrees). Pitch 90 looks
/// straight down at the face buttons with the front of the controller at the top; more than 90
/// comes round to the front, so the bumper and the trigger show above the face.
struct DiagramAngle {
    float roll = 0.0f;
    float pitch = 90.0f;
    /// AzaharVR: first of all, a turn about the model's up axis (positive turns its front left).
    float yaw = 0.0f;
};

struct DiagramPoint {
    float x = 0.0f, y = 0.0f;
};

/// One hand's drawing: premultiplied RGBA8, rows top down, and where each part shows.
struct DiagramImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
    /// Image pixels of the middle of each part's visible pixels, if any are visible.
    std::array<std::optional<DiagramPoint>, kCtrlParts> anchor{};
    /// The visible bounds of each part (x0, y0, x1, y1), for the pointer's end.
    std::array<std::array<float, 4>, kCtrlParts> bounds{};
};

struct DiagramStyle {
    uint32_t line = 0xFFE6E6EAu;      ///< outlines, 0xAARRGGBB
    uint32_t body = 0xFF26262Au;      ///< the controller's fill
    uint32_t part = 0xFF323238u;      ///< the fill of the parts the help points at
    float line_width = 2.2f;          ///< outline width, output pixels
    float crease_degrees = 38.0f;     ///< a fold sharper than this is drawn
};

/// The scale that fits every model in `models` drawn at `angle` into width x height (pixels per
/// metre), so the two hands come out the same size.
float DiagramScale(const std::vector<const ModelFile*>& models, const DiagramAngle& angle,
                   int width, int height);

/// Draws `model` at `angle`, `scale` pixels a metre, centred in width x height. AzaharVR: its
/// parts named for `scheme`'s `hand` (PartOfComponent).
DiagramImage DrawDiagram(const ModelFile& model, const DiagramAngle& angle, float scale, int width,
                         int height, const DiagramStyle& style = {},
                         ControllerScheme scheme = ControllerScheme{}, int hand = 0);

/// The rows [y0, y1) of `image`, the last `fade` of them fading out (a drawing cut short).
DiagramImage CropDiagram(const DiagramImage& image, int y0, int y1, int fade);

// ---- AzaharVR pcdiag ----

/// The angles a scheme's left hand is drawn from (the right one mirrored: roll and yaw turned the
/// other way), so each reads as the Frame's: from above with the face buttons, the stick and the
/// grip showing, and from the front with the trigger (and the Frame's bumper).
struct SchemeView {
    DiagramAngle top{0.0f, 90.0f};
    DiagramAngle front{180.0f, 165.0f};
    float front_scale = 0.9f; ///< the front drawing's scale, the top one's times this
    /// More than 1: the top drawing that much bigger than fits, kept from its top and centred
    /// on its parts, the cut sides fading out (the Index's strap would leave its face small).
    float top_zoom = 1.0f;
};
SchemeView DiagramView(ControllerScheme scheme);
/// `angle` for `hand` (0 left, 1 right).
DiagramAngle HandAngle(const DiagramAngle& angle, int hand);

/// The drawings' sizes (pixels).
struct SchemeHandSizes {
    int top_w = 0, top_h = 0;
    int front_w = 0, front_h = 0; ///< before the front drawing is cut under the trigger
    int crop_extra = 40;          ///< rows kept under the trigger (and the bumper)
    int crop_fade = 36;           ///< of them, fading out
};
/// Both hands, from the top and from the front (premultiplied), as the Frame's help draws them:
/// one scale for both hands, the front drawing cut a little under its trigger.
struct SchemeHands {
    std::array<DiagramImage, 2> top;
    std::array<DiagramImage, 2> front;
};
SchemeHands DrawSchemeHands(const ModelFile& left, const ModelFile& right, ControllerScheme scheme,
                            const SchemeHandSizes& sizes, const DiagramStyle& style = {});

/// The SteamVR render model folders a scheme's drawings come from: `subdir` of a SteamVR install.
struct SchemeModelFolders {
    const char* subdir;
    const char* left;
    const char* right;
};
const SchemeModelFolders& SchemeModels(ControllerScheme scheme);

/// Reads a scheme's two model files (left, right). False with the reason in `error` when it has
/// none here. Blocking, up to about a second the first time a PC scheme's are converted: never on
/// the XR thread. Defined in controller_models.cpp (the frame_xr library only).
bool ReadSchemeModels(ControllerScheme scheme, std::array<ModelFile, 2>& files, std::string& error);
/// ReadSchemeModels for a PC scheme with its places given: SteamVR installs `roots`, the cache
/// `cache_dir` (scheme_models.cpp; unit tested). False for the Frame's.
bool ReadSchemeModelsFrom(ControllerScheme scheme, const std::vector<std::string>& roots,
                          const std::string& cache_dir, std::array<ModelFile, 2>& files,
                          std::string& error);

} // namespace FrameXr::Vis
