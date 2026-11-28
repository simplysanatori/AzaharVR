// Copyright Azahar for Frame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame cpphelpers: converts SteamVR's Steam Frame controller render models into the files
// Azahar for Frame draws in VR (the C++ port of steam-frame/tools/frame-controller-models.py, which
// was Moonlight XR Native's converter, GPLv3, by the same author). Valve's models are only read from
// the player's own SteamVR install, never redistributed.
//
//   --frame-controller-models OUTDIR [SteamVR rendermodels dir]
//
// writes OUTDIR/frame_controller_left.model and frame_controller_right.model (format: see
// frame_xr/vis/model_file.cpp). The vertices are in the controller's OpenXR aim space. Textures are
// scaled to 1024 x 1024 (AZAHARFRAME_MODEL_TEXTURE=0 keeps SteamVR's size). No Qt: the PNGs are
// decoded by the caller's `ImageLoader`.

#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>
#include "common/common_types.h"

namespace FrameControllerModels {

constexpr const char* kRenderModels = "/opt/steamvr/drivers/frame_controller/resources/rendermodels";

/// A decoded image: tight RGB rows from the top.
struct Image {
    int width = 0, height = 0;
    std::vector<u8> rgb;
};
/// Decodes a PNG file (any channels; the result is RGB). false when it cannot.
using ImageLoader = std::function<bool(const std::string& path, Image* out)>;

/// `src` scaled to w x h by area averaging.
Image Scale(const Image& src, int w, int h);

struct Model {
    std::string bytes; ///< the whole .model file
    int triangles = 0, vertices = 0, components = 0, tex_w = 0, tex_h = 0;
    int full_triangles = 0; ///< AzaharVR: the triangles of the meshes read
};
/// One hand ("left"/"right"); false with `error` on a missing file or a bad model.
bool Convert(const std::string& hand, const std::string& rendermodels, int texture,
             const ImageLoader& load, Model* out, std::string* error);

/// AzaharVR: `texture` for no textures (one white texel and a flat surface: the controls help's
/// drawings need only the shapes, and the cache then holds none of Valve's images).
constexpr int kNoTextures = -1;

/// AzaharVR: any SteamVR controller's render model: `directory`/`name`.json and the meshes it
/// lists (the colour image `name`_color.png, or the Index's `name`_diff.png). The Frame's are
/// Convert's. A "trackpad" part keeps its input path but does not move.
bool ConvertFolder(const std::string& directory, const std::string& name, int texture,
                   const ImageLoader& load, Model* out, std::string* error);

/// AzaharVR: the SteamVR installs on this PC that exist, the likeliest first: the "runtime" list
/// of OpenVR's openvrpaths.vrpath (Linux: $XDG_CONFIG_HOME or ~/.config/openvr; Windows:
/// %LOCALAPPDATA%\openvr), then Steam's default places.
std::vector<std::string> SteamVrRoots();

/// AzaharVR: `cache_dir`/`folder`.model (no textures), converted from `subdir`/`folder` of the
/// first of `roots` that has it, unless it is already there from the same source files (their
/// sizes and times, kept in `folder`.stamp beside it). Safe from several threads; a conversion
/// takes a few hundred milliseconds, so not on a frame's thread. false with `error` when no
/// install has the folder or it cannot be converted; the path in `path` when true.
bool EnsureCachedModel(const std::vector<std::string>& roots, const std::string& subdir,
                       const std::string& folder, const std::string& cache_dir, std::string* path,
                       std::string* error);

/// Both hands into `out_dir` (both or neither). Prints the per-hand summary line.
int Run(const std::vector<std::string>& args, const ImageLoader& load);

} // namespace FrameControllerModels
