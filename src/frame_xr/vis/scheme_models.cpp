// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharVR pcdiag: a PC controller scheme's model files for the controls help's drawings
// (controller_diagram.h ReadSchemeModelsFrom), converted from the player's own SteamVR into the
// cache (core/frame_controller_models.h EnsureCachedModel). No OpenXR, Vulkan or Qt here.

#include "core/frame_controller_models.h"
#include "frame_xr/input/default_bindings.h"
#include "frame_xr/vis/controller_diagram.h"

namespace FrameXr::Vis {

bool ReadSchemeModelsFrom(ControllerScheme scheme, const std::vector<std::string>& roots,
                          const std::string& cache_dir, std::array<ModelFile, 2>& files,
                          std::string& error) {
    if (scheme == ControllerScheme::Frame) {
        error = "the Steam Frame's models are ControllerModelDir's";
        return false;
    }
    const SchemeModelFolders& folders = SchemeModels(scheme);
    const char* names[2] = {folders.left, folders.right};
    for (int h = 0; h < 2; h++) {
        std::string path, why;
        if (!FrameControllerModels::EnsureCachedModel(roots, folders.subdir, names[h], cache_dir,
                                                      &path, &why)) {
            error = why;
            return false;
        }
        if (!LoadModelFile(path, files[h], why)) {
            error = path + ": " + why;
            return false;
        }
    }
    return true;
}

} // namespace FrameXr::Vis
