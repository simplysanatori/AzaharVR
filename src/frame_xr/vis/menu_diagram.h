// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame tabmenu: the controller diagram (the ctrldiag help panel's, panels.h) on the menu's
// Controls tab, drawn with the menu's Material kit (menu/md3.h): the same drawings, labels and
// leader lines laid out by LayoutHelpDiagram in help panel pixels, scaled into the tab's area,
// in the menu's colours and fonts. The drawings are made `k` times the help panel's resolution
// so they stay sharp on the menu's denser texture.

#pragma once

#include <array>
#include <optional>
#include <string>

#include "frame_xr/menu/md3.h"
#include "frame_xr/menu/tab_menu.h"
#include "frame_xr/vis/controller_diagram.h"
#include "frame_xr/vis/help_content.h"
#include "frame_xr/vis/model_file.h"

namespace FrameXr::Vis {

struct MenuDiagramArt {
    float k = 1.0f; ///< image pixels per help panel pixel
    std::array<DiagramImage, 2> top;   ///< left, right: from above (straight alpha)
    std::array<DiagramImage, 2> front; ///< left, right: from the front, cut under the trigger
};

/// The drawings (a few hundred milliseconds of work: off the XR thread). AzaharVR: of `scheme`'s
/// controllers (controller_diagram.h ReadSchemeModels: `left` and `right` its models).
MenuDiagramArt MakeMenuDiagramArt(const ModelFile& left, const ModelFile& right, float k,
                                  ControllerScheme scheme = ControllerScheme{});
/// AzaharVR pcdiag: ReadSchemeModels, then MakeMenuDiagramArt; nullopt (the reason in `error`)
/// when the scheme has no models here. Off the XR thread (it may convert them first). Not in the
/// menu dump (frame_xr_menu_dump), which has no core library: inline, so only its callers link
/// ReadSchemeModels.
inline std::optional<MenuDiagramArt> MakeSchemeMenuDiagramArt(ControllerScheme scheme, float k,
                                                              std::string* error = nullptr) {
    std::array<ModelFile, 2> files;
    std::string why;
    if (!ReadSchemeModels(scheme, files, why)) {
        if (error != nullptr) {
            *error = why;
        }
        return std::nullopt;
    }
    return MakeMenuDiagramArt(files[0], files[1], k, scheme);
}

/// Draws `page` of `diagram` into `area` (dp).
void DrawMenuDiagram(Menu::Kit& kit, const Menu::Rect& area, const HelpDiagram& diagram,
                     DiagramPageId page, const MenuDiagramArt& art);

} // namespace FrameXr::Vis
