// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/frame_product.h"
#include "common/settings.h"
#include "frame_xr/input/controller_choice.h"
#include "frame_xr/input/controls.h"
#include "frame_xr/input/pc_scheme.h"
#include "frame_xr/vis/controller_diagram.h"
#include "frame_xr/vis/help_content.h"
#include "frame_xr/vis/help_layout.h"

// AzaharVR pchelp: the controls help for the PC controllers (help_content.cpp BuildPcHelp,
// BuildPcHelpDiagram), the Controllers setting (controller_choice.h) and the Frame's help as it
// was.
using namespace FrameXr;
using namespace FrameXr::Vis;
using Input::ControllerScheme;
namespace NB = Settings::NativeButton;

namespace {

std::string ActionFor(const std::vector<HelpRow>& rows, const std::string& control) {
    for (const auto& r : rows) {
        if (r.kind == HelpRow::Kind::Row && r.control == control) {
            return r.action;
        }
    }
    return "(missing)";
}

bool AnyControlHas(const std::vector<HelpRow>& rows, const std::string& word) {
    return std::any_of(rows.begin(), rows.end(), [&](const HelpRow& r) {
        return r.kind == HelpRow::Kind::Row && r.control.find(word) != std::string::npos;
    });
}

HelpContext Ctx(ControllerScheme scheme, bool in_menu = false) {
    HelpContext ctx;
    ctx.game_running = true;
    ctx.in_menu = in_menu;
    ctx.scheme = scheme;
    return ctx;
}

std::string Bound(Input::Control c) {
    return "engine:frame_xr,button:" + std::to_string(static_cast<int>(c));
}

constexpr ControllerScheme kPcSchemes[] = {ControllerScheme::Touch, ControllerScheme::Index,
                                           ControllerScheme::Focus3};

} // namespace

TEST_CASE("PC help: Touch and Focus 3 rows follow the scheme's table", "[vishelp][pchelp]") {
    for (const ControllerScheme scheme : {ControllerScheme::Touch, ControllerScheme::Focus3}) {
        INFO("scheme " << static_cast<int>(scheme));
        const HelpPage page = BuildHelp(DefaultHelpBindings(), Ctx(scheme));
        const auto& g = page.game;
        // By label: A is A (Face Buttons does not apply).
        CHECK(ActionFor(g, "A") == "3DS A");
        CHECK(ActionFor(g, "B") == "3DS B");
        CHECK(ActionFor(g, "X") == "3DS X");
        CHECK(ActionFor(g, "Y") == "3DS Y");
        CHECK(ActionFor(g, "Left Grip") == "3DS L");
        CHECK(ActionFor(g, "Right Grip") == "3DS R");
        CHECK(ActionFor(g, "Left Trigger") == "3DS ZL, Touch on Secondary Screen");
        CHECK(ActionFor(g, "Right Trigger") == "3DS ZR, Touch on Secondary Screen");
        CHECK(ActionFor(g, "Left Stick") == "Circle Pad");
        CHECK(ActionFor(g, "Right Stick") == "C-Stick");
        CHECK(ActionFor(g, "Left Stick Click") == "Switch Circle Pad / D-Pad");
        CHECK(ActionFor(g, "Left Stick in D-Pad Mode") == "3DS D-Pad");
        CHECK(ActionFor(g, "Right Stick Click") == "3DS Select");
        CHECK(ActionFor(g, "Menu") == "3DS Start");
        CHECK(ActionFor(g, "Both Stick Clicks") == "Same as Menu");
        CHECK(ActionFor(g, "Hold Menu (1 s)") == "In-Game Menu");
        CHECK(ActionFor(g, "Hold Menu + Right Grip") == "Quick Save");
        CHECK(ActionFor(g, "Hold Menu + Left Grip (0.5 s)") == "Quick Load");
        CHECK(ActionFor(g, "Hold Menu + Right Trigger") == "Fast Forward On / Off");
        // Move Screens
        CHECK(ActionFor(g, "Grip on a Screen") == "Move Screen");
        CHECK(ActionFor(g, "Hold Grip + Other Stick Up or Down") == "Screen Curve");
        CHECK(ActionFor(g, "B or Menu") == "Finish");
        // Nothing the PC controllers do not have.
        CHECK(ActionFor(g, "View") == "(missing)");
        for (const char* frame_only :
             {"Bumper", "Hold View", "+ View", "D-Pad Up", "Hold Left Stick Click"}) {
            INFO(frame_only);
            CHECK_FALSE(AnyControlHas(g, frame_only));
            CHECK_FALSE(AnyControlHas(page.window, frame_only));
        }
        CHECK(ActionFor(page.window, "Y") == "Keyboard");
        CHECK(ActionFor(page.window, "Hold Menu (1 s)") == "Back to Game");
        CHECK(page.footer.rfind(Input::ControllersDeviceName(scheme, true), 0) == 0);
    }
}

TEST_CASE("PC help: Index's trackpads and left-hand A and B", "[vishelp][pchelp]") {
    const HelpPage page = BuildHelp(DefaultHelpBindings(), Ctx(ControllerScheme::Index));
    const auto& g = page.game;
    CHECK(ActionFor(g, "Right A") == "3DS A");
    CHECK(ActionFor(g, "Right B") == "3DS B");
    CHECK(ActionFor(g, "Left A") == "3DS X");
    CHECK(ActionFor(g, "Left B") == "3DS Y");
    CHECK(ActionFor(g, "Left Trackpad Press") == "3DS D-Pad");
    CHECK(ActionFor(g, "Right Trackpad Press") == "3DS Start");
    CHECK(ActionFor(g, "Hold Right Trackpad (1 s)") == "In-Game Menu");
    CHECK(ActionFor(g, "Hold Right Trackpad + Left Grip (0.5 s)") == "Quick Load");
    CHECK(ActionFor(g, "Right Stick Click") == "3DS Select");
    CHECK(ActionFor(g, "Right B or Right Trackpad Press") == "Finish");
    // No D-pad mode, no both-clicks Menu on Index.
    CHECK_FALSE(AnyControlHas(g, "Left Stick Click"));
    CHECK_FALSE(AnyControlHas(g, "Both Stick Clicks"));
    CHECK(ActionFor(page.window, "Left B") == "Keyboard");
    CHECK(ActionFor(page.window, "Trigger or Right A") == "Click");
}

TEST_CASE("PC help: Edit Controls remaps and the thresholds show", "[vishelp][pchelp]") {
    HelpBindings b = DefaultHelpBindings();
    b.buttons[NB::L] = Bound(Input::Control::TriggerL); // L moved to the left trigger
    b.buttons[NB::Up] = "engine:sdl,button:3";          // D-pad up off the VR controllers
    b.profile_name = "Mine";
    for (const ControllerScheme scheme : kPcSchemes) {
        INFO("scheme " << static_cast<int>(scheme));
        const HelpPage page = BuildHelp(b, Ctx(scheme));
        CHECK(ActionFor(page.game, "Left Grip") == "Nothing");
        CHECK(ActionFor(page.game, "Left Trigger") == "3DS L + ZL, Touch on Secondary Screen");
        const std::string up = scheme == ControllerScheme::Index
                                   ? "Left Trackpad Up"
                                   : "Left Stick Up (D-Pad Mode)";
        CHECK(ActionFor(page.game, up) == "Nothing");
        CHECK(page.footer.find("Profile: Mine") != std::string::npos);
        // Face Buttons is the input's own on PC: by label whatever the setting.
        HelpContext pos = Ctx(scheme);
        pos.face_by_position = false;
        const HelpPage by_label = BuildHelp(b, pos);
        CHECK(by_label.game.size() == page.game.size());
        for (std::size_t i = 0; i < page.game.size(); i++) {
            CHECK(by_label.game[i].action == page.game[i].action);
        }
    }
    // The holds come from PcSchemeConfig.
    const Input::PcSchemeConfig config;
    CHECK(config.menu_hold_ns == 1'000'000'000);
    CHECK(config.quick_load_hold_ns == 500'000'000);
}

TEST_CASE("PC help: the diagram's labels point at the right parts", "[vishelp][pchelp]") {
    for (const ControllerScheme scheme : kPcSchemes) {
        INFO("scheme " << static_cast<int>(scheme));
        const bool index = scheme == ControllerScheme::Index;
        const HelpDiagram d = BuildHelpDiagram(DefaultHelpBindings(), Ctx(scheme));
        const DiagramPage& g = d.pages[static_cast<std::size_t>(DiagramPageId::Game)];
        const DiagramLabel* lgrip = FindDiagramLabel(g, 0, CtrlPart::Grip);
        const DiagramLabel* rgrip = FindDiagramLabel(g, 1, CtrlPart::Grip);
        REQUIRE(lgrip != nullptr);
        REQUIRE(rgrip != nullptr);
        CHECK(lgrip->lines.front().key == "Left Grip");
        CHECK(lgrip->lines.front().action == "3DS L");
        CHECK(rgrip->lines.front().action == "3DS R");
        // The Menu role: the left Menu button, or Index's right trackpad.
        const DiagramLabel* menu = index ? FindDiagramLabel(g, 1, DiagramTrackpadPart(1))
                                         : FindDiagramLabel(g, 0, CtrlPart::Menu);
        REQUIRE(menu != nullptr);
        CHECK(menu->lines.front().key == (index ? "Right Trackpad Press" : "Menu"));
        CHECK(menu->lines.front().action == "3DS Start");
        // The left hand's face buttons are the pad's X and Y.
        // (Index's left a and b are anchored as X and Y; the label keeps their letters.)
        const DiagramLabel* left_faces = FindDiagramLabel(g, 0, CtrlPart::X);
        REQUIRE(left_faces != nullptr);
        CHECK(left_faces->lines.front().key == (index ? "A" : "X"));
        CHECK(left_faces->lines.front().action == "3DS X");
        if (index) {
            const DiagramLabel* pad = FindDiagramLabel(g, 0, DiagramTrackpadPart(0));
            REQUIRE(pad != nullptr);
            CHECK(pad->lines.front().key == "Left Trackpad Press");
        }
        // No Frame-only part on any page.
        for (const DiagramPage& p : d.pages) {
            for (const DiagramLabel& l : p.labels) {
                CHECK_FALSE(l.parts.empty());
                for (const CtrlPart part : l.parts) {
                    CHECK(part != CtrlPart::Bumper);
                    CHECK(part != CtrlPart::View);
                    CHECK(part != CtrlPart::System);
                }
            }
        }
        // Move Screens with the grips on Azahar's window's page.
        const DiagramPage& w = d.pages[static_cast<std::size_t>(DiagramPageId::Window)];
        const DiagramLabel* wgrip = FindDiagramLabel(w, 0, CtrlPart::Grip);
        REQUIRE(wgrip != nullptr);
        CHECK(std::any_of(wgrip->lines.begin(), wgrip->lines.end(),
                          [](const DiagramLine& l) { return l.key == "Move Screens"; }));
        CHECK(d.footer == BuildHelp(DefaultHelpBindings(), Ctx(scheme)).footer);
    }
}

TEST_CASE("PC help: the list fits its columns, in VR and in the menu", "[vishelp][pchelp]") {
    const TextMeasure measure = [](float size, bool bold, const std::string& text) {
        return static_cast<float>(text.size()) * size * (bold ? 0.64f : 0.58f);
    };
    const HelpColumnSpan game{44.0f, 640.0f}, window{684.0f, 1196.0f}; // as panels.cpp
    for (const ControllerScheme scheme : kPcSchemes) {
        for (const bool in_menu : {false, true}) {
            INFO("scheme " << static_cast<int>(scheme) << " in menu " << in_menu);
            const HelpPage page = BuildHelp(DefaultHelpBindings(), Ctx(scheme, in_menu));
            const HelpTextLayout layout = LayoutHelpText(page, game, window, 20.0f, measure);
            CHECK(layout.fits);
            // No more rows than the Frame's list, which the panel was made for.
            const HelpPage frame = BuildHelp(DefaultHelpBindings(), Ctx(ControllerScheme::Frame));
            CHECK(page.game.size() <= frame.game.size());
            CHECK(page.window.size() <= frame.game.size());
            // The menu's two columns: a heading never ends the first.
            for (const auto* rows : {&page.game, &page.window}) {
                const std::size_t split = HelpColumnBreak(*rows);
                REQUIRE(split <= rows->size());
                REQUIRE(split > 0);
                CHECK((*rows)[split - 1].kind == HelpRow::Kind::Row);
                CHECK(std::max(split, rows->size() - split) <= (rows->size() + 1) / 2 + 2);
            }
        }
    }
    SECTION("the break goes to a heading near the middle") {
        std::vector<HelpRow> rows(10);
        rows[0].kind = HelpRow::Kind::Heading;
        rows[6].kind = HelpRow::Kind::Heading;
        CHECK(HelpColumnBreak(rows) == 6);
        rows[6].kind = HelpRow::Kind::Row;
        CHECK(HelpColumnBreak(rows) == 5);
        rows[4].kind = HelpRow::Kind::Heading; // it would end the first column
        CHECK(HelpColumnBreak(rows) == 4);
        CHECK(HelpColumnBreak({}) == 0);
    }
}

TEST_CASE("PC help: the Controllers setting forces the scheme", "[pchelp]") {
    using Input::ChosenScheme;
    const uint32_t kAutomatic = 0, kFrame = 1, kQuest = 2, kIndex = 3, kFocus3 = 4;
    // Automatic: the profile's.
    for (const ControllerScheme detected :
         {ControllerScheme::Frame, ControllerScheme::Touch, ControllerScheme::Index,
          ControllerScheme::Focus3}) {
        CHECK(ChosenScheme(kAutomatic, detected, true) == detected);
        CHECK(ChosenScheme(99, detected, true) == detected); // an unknown value
        CHECK(ChosenScheme(kFrame, detected, true) == ControllerScheme::Frame);
        CHECK(ChosenScheme(kQuest, detected, true) == ControllerScheme::Touch);
        CHECK(ChosenScheme(kIndex, detected, true) == ControllerScheme::Index);
        CHECK(ChosenScheme(kFocus3, detected, true) == ControllerScheme::Focus3);
        // The Frame's build: always the Frame's.
        for (uint32_t type = 0; type <= kFocus3; type++) {
            CHECK(ChosenScheme(type, detected, false) == ControllerScheme::Frame);
        }
    }
    // The setting: global, 0 to 4, Automatic by default.
    CHECK(Settings::values.vr_controller_type.GetDefault() == 0);
    CHECK(Settings::values.vr_controller_type.GetLabel() == "vr_controller_type");
    CHECK(Input::kControllerTypeNames.size() == 5);
    CHECK(std::string{Input::ControllerTypeName(kIndex)} == "Valve Index");
    CHECK(std::string{Input::ControllerTypeName(7)} == "Automatic");
    // Forced, the help follows: Meta Quest's rows with Index detected.
    const HelpPage forced = BuildHelp(
        DefaultHelpBindings(), Ctx(ChosenScheme(kQuest, ControllerScheme::Index, true)));
    CHECK(ActionFor(forced.game, "Menu") == "3DS Start");
    CHECK(ActionFor(forced.game, "Left Trackpad Press") == "(missing)");
}

TEST_CASE("PC help: names that follow the controllers", "[pchelp]") {
    using Input::ControllersDeviceName;
    CHECK(std::string{ControllersDeviceName(ControllerScheme::Frame, false)} ==
          "Frame Controllers");
    CHECK(std::string{ControllersDeviceName(ControllerScheme::Touch, false)} ==
          "Frame Controllers");
    CHECK(std::string{ControllersDeviceName(ControllerScheme::Frame, true)} ==
          "Steam Frame Controllers");
    CHECK(std::string{ControllersDeviceName(ControllerScheme::Touch, true)} ==
          "Meta Quest Controllers");
    CHECK(std::string{ControllersDeviceName(ControllerScheme::Index, true)} ==
          "Valve Index Controllers");
    CHECK(std::string{ControllersDeviceName(ControllerScheme::Focus3, true)} ==
          "Vive XR Elite Controllers");
    // The chords' log lines: the Frame's as before, the PC's own buttons.
    CHECK(Input::ChordLogName(ControllerScheme::Frame, 1, false) == "View, then Right Bumper");
    CHECK(Input::ChordLogName(ControllerScheme::Frame, 0, false) == "View, then Left Bumper");
    CHECK(Input::ChordLogName(ControllerScheme::Frame, 1, true) == "View, then Right Trigger");
    CHECK(Input::ChordLogName(ControllerScheme::Touch, 1, false) == "Menu, then Right Grip");
    CHECK(Input::ChordLogName(ControllerScheme::Focus3, 0, false) == "Menu, then Left Grip");
    CHECK(Input::ChordLogName(ControllerScheme::Index, 1, true) ==
          "Right Trackpad, then Right Trigger");
    CHECK(DiagramTrackpadPart(0) == CtrlPart::Trackpad);
}

TEST_CASE("PC help: the Frame's help is as it was", "[vishelp]") {
    HelpContext def;
    def.game_running = true;
    HelpContext frame = def;
    frame.scheme = ControllerScheme::Frame;
    const HelpPage a = BuildHelp(DefaultHelpBindings(), def);
    const HelpPage b = BuildHelp(DefaultHelpBindings(), frame);
    REQUIRE(a.game.size() == b.game.size());
    REQUIRE(a.window.size() == b.window.size());
    for (std::size_t i = 0; i < a.game.size(); i++) {
        CHECK(a.game[i].control == b.game[i].control);
        CHECK(a.game[i].action == b.game[i].action);
    }
    // The Frame's own controls, chords and footer.
    CHECK(ActionFor(a.game, "Left Bumper") == "3DS L");
    CHECK(ActionFor(a.game, "View") == "3DS Select");
    CHECK(ActionFor(a.game, "Hold View + Left Bumper (0.5 s)") == "Quick Load");
    CHECK(ActionFor(a.game, "Grip on a Screen") == "Move Screens");
    CHECK(ActionFor(a.window, "Both Bumpers + View + Menu") == "Switch VR / Flat");
    CHECK(a.footer.rfind("Face Buttons: ", 0) == 0);
    CHECK_FALSE(AnyControlHas(a.game, "Left Grip"));
    const HelpDiagram d = BuildHelpDiagram(DefaultHelpBindings(), frame);
    const DiagramPage& g = d.pages[static_cast<std::size_t>(DiagramPageId::Game)];
    REQUIRE(FindDiagramLabel(g, 0, CtrlPart::Bumper) != nullptr);
    CHECK(FindDiagramLabel(g, 0, CtrlPart::Bumper)->lines.front().key == "Left Bumper");
    REQUIRE(FindDiagramLabel(g, 0, CtrlPart::View) != nullptr);
    CHECK(d.footer == a.footer);
}

// Hidden: the labels laid out on this PC's own SteamVR drawings (lane/pc-diagrams), as the help
// panel places them: every label shown, inside the panel, none over another.
// AZAHARVR_PCDIAG_STEAMVR=DIR picks the SteamVR install, AZAHARVR_PCHELP_CACHE the cache.
TEST_CASE("PC help: the labels on this PC's drawings", "[.pchelplocal]") {
    const char* steamvr = std::getenv("AZAHARVR_PCDIAG_STEAMVR");
    const char* cache = std::getenv("AZAHARVR_PCHELP_CACHE");
    if (steamvr == nullptr || cache == nullptr) {
        WARN("AZAHARVR_PCDIAG_STEAMVR and AZAHARVR_PCHELP_CACHE not set: nothing checked");
        return;
    }
    const TextMeasure measure = [](float size, bool bold, const std::string& text) {
        return static_cast<float>(text.size()) * size * (bold ? 0.51f : 0.48f);
    };
    for (const ControllerScheme scheme : kPcSchemes) {
        INFO("scheme " << static_cast<int>(scheme));
        std::array<ModelFile, 2> files;
        std::string error;
        REQUIRE(ReadSchemeModelsFrom(scheme, {steamvr}, cache, files, error));
        const SchemeHands hands = DrawSchemeHands(
            files[0], files[1], scheme, {kDiagramTopW, kDiagramTopH, kDiagramFrontW, 440, 40, 36});
        DiagramAnchors an; // as panels.cpp HelpArtAnchors
        for (int h = 0; h < 2; h++) {
            for (std::size_t p = 0; p < kCtrlParts; p++) {
                const bool front = DiagramPartInFront(static_cast<CtrlPart>(p));
                const DiagramImage& img = front ? hands.front[h] : hands.top[h];
                const float ox = front ? DiagramFrontX(h) : DiagramTopX(h);
                const float oy = front ? DiagramFrontY(img.height) : kDiagramTopY;
                if (const auto& at = img.anchor[p]) {
                    an.at[h][p] = DiagramPoint{at->x + ox, at->y + oy};
                }
                const auto& b = img.bounds[p];
                an.bounds[h][p] = {b[0] + ox, b[1] + oy, b[2] + ox, b[3] + oy};
            }
        }
        const HelpDiagram d = BuildHelpDiagram(DefaultHelpBindings(), Ctx(scheme));
        for (const DiagramPage& page : d.pages) {
            INFO(page.name);
            const auto placed = LayoutHelpDiagram(page, an, measure);
            for (const DiagramLabel& l : page.labels) {
                const bool shown = std::any_of(placed.begin(), placed.end(),
                                               [&](const DiagramPlaced& p) { return p.label == &l; });
                INFO("label " << l.lines.front().key << " hand " << l.hand);
                CHECK(shown);
            }
            for (std::size_t i = 0; i < placed.size(); i++) {
                const DiagramPlaced& p = placed[i];
                INFO("label " << p.label->lines.front().key << " at " << p.x0 << "," << p.y0
                              << " " << p.x1 << "," << p.y1);
                CHECK(p.x0 >= 30.0f);
                CHECK(p.x1 <= kDiagramPanelW - 30.0f);
                CHECK(p.y0 >= kDiagramTop - 0.01f);
                CHECK(p.y1 <= kDiagramBottom + 0.01f);
                for (std::size_t j = i + 1; j < placed.size(); j++) {
                    const DiagramPlaced& q = placed[j];
                    INFO("and " << q.label->lines.front().key);
                    CHECK((p.x1 <= q.x0 || q.x1 <= p.x0 || p.y1 <= q.y0 || q.y1 <= p.y0));
                }
            }
        }
    }
}
