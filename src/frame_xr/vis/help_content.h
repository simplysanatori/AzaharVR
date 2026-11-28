// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame
// What the controls help panel says: one row per Frame control, worked out from the controls
// profile in use (the 3DS buttons bound to frame_xr controls), the Face Buttons setting and the
// fixed rules of the mapper, the home panel and the in-game menu (holds, chords, grips). No
// OpenXR, Vulkan or Qt here, so it is unit tested (src/tests/frame_xr/vis_help.cpp).

#pragma once

#include <array>
#include <string>
#include <vector>

#include "common/alt_core_types.h"
#include "common/settings.h"
#include "frame_xr/input/default_bindings.h" // AzaharVR pchelp
#include "frame_xr/vis/controller_diagram.h"

namespace FrameXr::Vis {

/// The 3DS side of the controls profile: Settings::InputProfile::buttons and ::analogs, as the
/// Qt side copied them (param strings such as "engine:frame_xr,button:0").
struct HelpBindings {
    std::array<std::string, Settings::NativeButton::NumButtons> buttons;
    std::array<std::string, Settings::NativeAnalog::NumAnalogs> analogs;
    std::string profile_name;
    bool valid = false; ///< false: the default profile is shown
};

/// Where the player is, for the rows and the "Now" mark.
struct HelpContext {
    bool face_by_position = true; ///< Face Buttons: By Position (Nintendo)
    bool in_menu = false;         ///< the in-game menu is open
    bool game_running = false;
    Common::AltCore::CoreKind console = Common::AltCore::CoreKind::N3DS; ///< the game's
    /// AzaharVR pchelp: the controllers the help is for (controller_choice.h ChosenScheme). The
    /// Frame's is the Frame's help as before; Touch, Index and Focus 3 have their own rows, made
    /// from kControllerSchemes and PcSchemeConfig (pc_scheme.h). Face Buttons is ignored there
    /// (by label, as the input does).
    Input::ControllerScheme scheme = Input::ControllerScheme::Frame;
};

struct HelpRow {
    enum class Kind { Row, Heading };
    Kind kind = Kind::Row;
    std::string control; ///< Heading: the heading's text
    std::string action;
    bool now = false; ///< Heading: what the controls do right now
};

struct HelpPage {
    std::vector<HelpRow> game;   ///< "In Game"
    std::vector<HelpRow> window; ///< "Azahar's Window", the in-game menu, everywhere
    std::string footer;          ///< the Face Buttons setting and the profile
};

/// The bindings of the default VR profile (frame_xr/input/default_bindings.h).
HelpBindings DefaultHelpBindings();

HelpPage BuildHelp(const HelpBindings& bindings, const HelpContext& context);

// AzaharFrame ctrldiag: the same, as labels on drawings of the controllers (panels.h), in the
// style of CitraVR's controller pages. The list above stays for when the models are missing.

/// One line of a label: `key` (dim, may be empty) and what it does.
struct DiagramLine {
    std::string key;
    std::string action;
    bool title = false;   ///< the control's own line (bold)
    bool heading = false; ///< the head of a sub-list ("Hold View +"): `key` only
    bool sub = false;     ///< in a sub-list (indented)
};

/// A label, and the parts of one controller its leader line points at (the middle of them).
struct DiagramLabel {
    int hand = 0; ///< 0 left, 1 right
    std::vector<CtrlPart> parts;
    std::vector<DiagramLine> lines;
};

enum class DiagramPageId : int { Game, Window, Count };

struct DiagramPage {
    std::string name;
    std::vector<DiagramLabel> labels;
};

struct HelpDiagram {
    std::array<DiagramPage, 2> pages;
    DiagramPageId now = DiagramPageId::Game; ///< the page for where the player is
    std::string footer;
};

HelpDiagram BuildHelpDiagram(const HelpBindings& bindings, const HelpContext& context);

/// The page for where the player is: In Game while a game runs, else Azahar's Window.
DiagramPageId HelpDiagramPageFor(const HelpContext& context);

/// The label of `part` of `hand` on `page`, or null (unit tests).
const DiagramLabel* FindDiagramLabel(const DiagramPage& page, int hand, CtrlPart part);

// ---- AzaharVR pchelp: MERGE POINT with lane/pc-diagrams ----------------------------------
// lane/pc-diagrams draws the PC controllers (keyed by Input::ControllerScheme) and adds
// CtrlPart::Trackpad. Until that is merged:
//  - DiagramTrackpadPart: the part an Index trackpad's label points at. Replace the body with
//    `return CtrlPart::Trackpad;` (both hands).
//  - SchemeHasDrawings: whether drawings exist for a scheme. Replace the body with that lane's
//    "diagram images for a scheme, or none" check. Where it is false the help shows its rows
//    as text (the VR panel's list, the in-game menu's Controls tab), and the help panel and
//    the menu ask for the drawings through it (visuals.cpp, menu_panel.cpp: "pchelp adapter").
constexpr CtrlPart DiagramTrackpadPart(int hand) {
    return hand == 0 ? CtrlPart::Dpad : CtrlPart::Menu;
}
constexpr bool SchemeHasDrawings(Input::ControllerScheme scheme) {
    return scheme == Input::ControllerScheme::Frame;
}

} // namespace FrameXr::Vis
