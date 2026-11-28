// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <functional>

#include "common/alt_core_types.h"
#include "common/param_package.h"
#include "frame_xr/immersive_names.h" // AzaharFrame kiu
#include "frame_xr/input/controls.h"
#include "frame_xr/input/controller_choice.h" // AzaharVR pchelp
#include "frame_xr/input/default_bindings.h"
#include "frame_xr/input/pc_scheme.h" // AzaharVR pchelp
#include "frame_xr/vis/help_content.h"

namespace FrameXr::Vis {

namespace {

using Input::AnalogControl;
using Input::Control;
namespace NB = Settings::NativeButton;
namespace NA = Settings::NativeAnalog;
namespace ImNames = ::FrameXr::Immersive::Names; // AzaharFrame kiu

using Common::AltCore::CoreKind;

/// The bindings, and the console they are read for.
struct Bindings : HelpBindings {
    CoreKind console = CoreKind::N3DS;
    bool GameBoy() const {
        return console == CoreKind::GB;
    }
    /// A GBA or Game Boy game.
    bool Handheld() const {
        return console == CoreKind::GBA || GameBoy();
    }
    std::string Console() const {
        return GameBoy() ? "Game Boy " : Handheld() ? "GBA " : "3DS ";
    }
};

// Whether the game's console does anything with a 3DS button.
bool Has(const Bindings& b, int button) {
    if (!b.Handheld()) {
        return true;
    }
    switch (button) {
    case NB::L:
    case NB::R:
        return !b.GameBoy();
    case NB::ZL:
    case NB::ZR:
    case NB::Home:
    case NB::Power:
    case NB::Debug:
    case NB::Gpio14:
    case NB::MicBlow:
        return false;
    default:
        return true;
    }
}

bool IsAutoFire(const Bindings& b, int button) {
    return b.Handheld() && (button == NB::X || button == NB::Y);
}

const char* ButtonName(int button) {
    switch (button) {
    case NB::A:
        return "A";
    case NB::B:
        return "B";
    case NB::X:
        return "X";
    case NB::Y:
        return "Y";
    case NB::Up:
        return "D-Pad Up";
    case NB::Down:
        return "D-Pad Down";
    case NB::Left:
        return "D-Pad Left";
    case NB::Right:
        return "D-Pad Right";
    case NB::L:
        return "L";
    case NB::R:
        return "R";
    case NB::Start:
        return "Start";
    case NB::Select:
        return "Select";
    case NB::Debug:
        return "Debug";
    case NB::Gpio14:
        return "GPIO14";
    case NB::ZL:
        return "ZL";
    case NB::ZR:
        return "ZR";
    case NB::Home:
        return "Home";
    case NB::Power:
        return "Power";
    case NB::MicBlow:
        return "Microphone Blow";
    case NB::QuickMenu:
        return Common::AltCore::kQuickMenuLabel;
    case NB::TurnPage:
        return "Turn Secondary Screen Page";
    default:
        return "?";
    }
}

// The 3DS buttons a frame_xr control drives in the profile, in the 3DS's order.
std::vector<int> BoundTo(const Bindings& b, Control control) {
    std::vector<int> out;
    for (int i = 0; i < NB::NumButtons; i++) {
        const Common::ParamPackage p(b.buttons[static_cast<std::size_t>(i)]);
        if (p.Get("engine", "") == "frame_xr" && p.Get("button", -1) == static_cast<int>(control) &&
            Has(b, i)) {
            out.push_back(i);
        }
    }
    return out;
}

std::string Names(const Bindings& b, const std::vector<int>& buttons) {
    std::string s;
    for (const int button : buttons) {
        if (!s.empty()) {
            s += " + ";
        }
        s += !IsAutoFire(b, button) ? ButtonName(button)
             : button == NB::X      ? Common::AltCore::kAutoFireALabel
                                    : Common::AltCore::kAutoFireBLabel;
    }
    return s;
}

// "3DS B", "3DS L + R", "GBA B", "Auto Fire A", or empty for none.
std::string ThreeDs(const Bindings& b, Control control) {
    const std::vector<int> bound = BoundTo(b, control);
    if (bound.empty()) {
        return {};
    }
    const std::string names = Names(b, bound);
    return IsAutoFire(b, bound.front()) ? names : b.Console() + names;
}

// The 3DS stick a frame_xr stick drives ("Circle Pad", "C-Stick"), or empty.
std::string StickTo(const Bindings& b, AnalogControl control) {
    std::string s;
    for (int i = 0; i < NA::NumAnalogs; i++) {
        const Common::ParamPackage p(b.analogs[static_cast<std::size_t>(i)]);
        if (b.Handheld() && i != NA::CirclePad) {
            continue;
        }
        if (p.Get("engine", "") == "frame_xr" && p.Get("analog", -1) == static_cast<int>(control)) {
            if (!s.empty()) {
                s += " + ";
            }
            s += b.Handheld()         ? b.Console() + "D-Pad"
                 : i == NA::CirclePad ? std::string("Circle Pad")
                                      : std::string("C-Stick");
        }
    }
    return s;
}

// A fixed action, and the 3DS buttons the profile also put on the control.
std::string With(const std::string& fixed, const std::string& bound) {
    if (bound.empty()) {
        return fixed;
    }
    return bound + ", " + fixed;
}

std::string OrNothing(const std::string& s) {
    return s.empty() ? std::string{"Nothing"} : s;
}

void Row(std::vector<HelpRow>& rows, std::string control, std::string action) {
    rows.push_back({HelpRow::Kind::Row, std::move(control), std::move(action), false});
}

void Heading(std::vector<HelpRow>& rows, std::string text, bool now) {
    rows.push_back({HelpRow::Kind::Heading, std::move(text), {}, now});
}

// The Frame's face buttons, and the pad control each drives (mapper.cpp: by position, the
// Frame's A is the pad's B, and so on).
struct Face {
    const char* frame;
    Control pad;
};
std::array<Face, 4> Faces(bool by_position) {
    return {{
        {"A", by_position ? Control::B : Control::A},
        {"B", by_position ? Control::A : Control::B},
        {"X", by_position ? Control::Y : Control::X},
        {"Y", by_position ? Control::X : Control::Y},
    }};
}

// The D-pad drives the 3DS's D-pad, one direction each.
bool DpadPlain(const Bindings& b) {
    return BoundTo(b, Control::DpadUp) == std::vector<int>{NB::Up} &&
           BoundTo(b, Control::DpadDown) == std::vector<int>{NB::Down} &&
           BoundTo(b, Control::DpadLeft) == std::vector<int>{NB::Left} &&
           BoundTo(b, Control::DpadRight) == std::vector<int>{NB::Right};
}

// The 3DS buttons on the grips ("3DS L + R"), or empty.
std::string Grips(const Bindings& b) {
    std::string grips = Names(b, BoundTo(b, Control::GripL));
    if (const std::string r = Names(b, BoundTo(b, Control::GripR)); !r.empty()) {
        grips += (grips.empty() ? "" : " + ") + r;
    }
    return grips.empty() ? grips : b.Console() + grips;
}

std::string Footer(const HelpBindings& given, bool by_position) {
    std::string footer = std::string{"Face Buttons: "} +
                         (by_position ? "By Position (Nintendo)" : "By Label (Xbox)");
    if (!given.valid) {
        footer += "  \xC2\xB7  Default Controls";
    } else if (!given.profile_name.empty()) {
        footer += "  \xC2\xB7  Profile: " + given.profile_name;
    }
    return footer;
}

// AzaharVR pchelp: a PC scheme's footer: the controllers (as Edit Controls names them) and the
// profile; Face Buttons does not apply to them.
std::string PcFooter(const HelpBindings& given, Input::ControllerScheme scheme) {
    std::string footer = Input::ControllersDeviceName(scheme, true);
    if (!given.valid) {
        footer += "  \xC2\xB7  Default Controls";
    } else if (!given.profile_name.empty()) {
        footer += "  \xC2\xB7  Profile: " + given.profile_name;
    }
    return footer;
}

// ---- The diagram's labels ----

constexpr int kLeft = 0;
constexpr int kRight = 1;
constexpr int kBoth = 2;

DiagramLine Title(std::string key, std::string action) {
    return {std::move(key), std::move(action), true, false, false};
}
DiagramLine Line(std::string key, std::string action) {
    return {std::move(key), std::move(action), false, false, false};
}
DiagramLine Sub(std::string key, std::string action) {
    return {std::move(key), std::move(action), false, false, true};
}
DiagramLine SubHeading(std::string text) {
    return {std::move(text), {}, false, true, false};
}

void Label(DiagramPage& page, int hand, std::vector<CtrlPart> parts, std::vector<DiagramLine> lines) {
    page.labels.push_back({hand, std::move(parts), std::move(lines)});
}

// A trigger: the 3DS button it is, and the touch on the bottom screen, which it always is.
std::vector<DiagramLine> TriggerLines(const char* name, const std::string& bound) {
    if (bound.empty()) {
        return {Title(name, "Touch on Secondary Screen")};
    }
    return {Title(name, bound), Line("", "Touch on Secondary Screen")};
}

// ---- AzaharVR pchelp: the PC controllers (Touch, Index, Focus 3) ----
// What pc_scheme.cpp does with each scheme, read from its table (kControllerSchemes) and its
// thresholds (PcSchemeConfig): the grips are L and R (BumperL, BumperR), the Menu role's tap is
// Start (Control::Menu), the right stick click Select (Control::View), the D-pad from the left
// stick's D-pad mode or the left trackpad; the Menu role's hold and chords; Move Screens
// (MoveScreensInput). The 3DS side comes from the profile, as on the Frame.

using Input::ControllerScheme;
using Input::ControllerSchemeTable;
using Input::DpadSource;
using Input::MenuSource;

/// "1 s", "0.5 s".
std::string Secs(int64_t ns) {
    const int64_t tenths = (ns + 50'000'000) / 100'000'000;
    return tenths % 10 == 0 ? std::to_string(tenths / 10) + " s"
                            : std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " s";
}

struct PcNames {
    const ControllerSchemeTable& t;
    Input::PcSchemeConfig config{};

    /// The physical button behind the InputFrame's a, b, x, y ("A", or on Index the left
    /// hand's A and B, which are X and Y).
    std::string Face(const char* frame) const {
        if (t.scheme != ControllerScheme::Index) {
            return frame;
        }
        const std::string f{frame};
        return f == "A" ? "Right A" : f == "B" ? "Right B" : f == "X" ? "Left A" : "Left B";
    }
    bool Trackpads() const {
        return t.menu == MenuSource::RightTrackpadPress;
    }
    /// The Menu role: "Menu" (Touch, Focus 3), "Right Trackpad" (Index).
    std::string Menu() const {
        return Input::MenuRoleName(t.scheme);
    }
    /// Its tap.
    std::string MenuPress() const {
        return Trackpads() ? "Right Trackpad Press" : "Menu";
    }
    std::string MenuHold() const {
        return "Hold " + Menu() + " (" + Secs(config.menu_hold_ns) + ")";
    }
    std::string QuickLoadHold() const {
        return "Left Grip (" + Secs(config.quick_load_hold_ns) + ")";
    }
};

// The D-pad rows: one when it is the 3DS's D-pad, else one per direction. `name` is what drives
// it ("Left Trackpad Press"), `dir` how one direction of it is named.
void PcDpadRows(std::vector<HelpRow>& g, const Bindings& b, const std::string& name,
                const std::function<std::string(const char*)>& dir) {
    if (DpadPlain(b)) {
        Row(g, name, b.Console() + "D-Pad");
        return;
    }
    Row(g, dir("Up"), OrNothing(ThreeDs(b, Control::DpadUp)));
    Row(g, dir("Down"), OrNothing(ThreeDs(b, Control::DpadDown)));
    Row(g, dir("Left"), OrNothing(ThreeDs(b, Control::DpadLeft)));
    Row(g, dir("Right"), OrNothing(ThreeDs(b, Control::DpadRight)));
}

HelpPage BuildPcHelp(const HelpBindings& given, const HelpContext& ctx) {
    const Bindings b{given.valid ? given : DefaultHelpBindings(), ctx.console};
    const PcNames n{Input::SchemeTable(ctx.scheme)};
    const ControllerSchemeTable& t = n.t;
    HelpPage page;

    // ---- In Game (PcScheme::Game) ----
    auto& g = page.game;
    Heading(g, "In Game", ctx.game_running && !ctx.in_menu);
    for (const Face& f : Faces(!t.face_buttons_by_label)) {
        Row(g, n.Face(f.frame), OrNothing(ThreeDs(b, f.pad)));
    }
    Row(g, "Left Stick", OrNothing(StickTo(b, AnalogControl::StickL)));
    if (t.dpad == DpadSource::LeftStickToggle) {
        if (t.stick_clicks_for_game) {
            Row(g, "Left Stick Click", "Switch Circle Pad / D-Pad");
        }
        PcDpadRows(g, b, "Left Stick in D-Pad Mode", [](const char* d) {
            return std::string("Left Stick ") + d + " (D-Pad Mode)";
        });
    } else if (t.dpad == DpadSource::LeftTrackpadPress) {
        PcDpadRows(g, b, "Left Trackpad Press",
                   [](const char* d) { return std::string("Left Trackpad ") + d; });
    }
    Row(g, "Right Stick", OrNothing(StickTo(b, AnalogControl::StickR)));
    if (t.stick_clicks_for_game) {
        Row(g, "Right Stick Click", OrNothing(ThreeDs(b, Control::View))); // Select
    }
    if (t.grips_are_lr) {
        Row(g, "Left Grip", OrNothing(ThreeDs(b, Control::BumperL)));
        Row(g, "Right Grip", OrNothing(ThreeDs(b, Control::BumperR)));
    }
    Row(g, "Left Trigger", With("Touch on Secondary Screen", ThreeDs(b, Control::TriggerL)));
    Row(g, "Right Trigger", With("Touch on Secondary Screen", ThreeDs(b, Control::TriggerR)));
    Row(g, n.MenuPress(), OrNothing(ThreeDs(b, Control::Menu))); // Start
    if (t.both_clicks_menu) {
        Row(g, "Both Stick Clicks", "Same as Menu");
    }
    Row(g, n.MenuHold(), "In-Game Menu");
    Row(g, "Hold " + n.Menu() + " + Right Grip", "Quick Save");
    Row(g, "Hold " + n.Menu() + " + " + n.QuickLoadHold(), "Quick Load");
    Row(g, "Hold " + n.Menu() + " + Right Trigger", "Fast Forward On / Off");

    // ---- Move Screens (the Position tab's button; MoveScreensInput) ----
    Heading(g, "Move Screens", false);
    Row(g, "Grip on a Screen", "Move Screen");
    Row(g, "Hold Grip + Stick Up or Down", std::string("Distance or ") + ImNames::kWorldScale);
    Row(g, "Hold Grip + Stick Sideways", "Screen Size");
    Row(g, "Hold Grip + Other Stick Sideways", "Nudge Screen or Offset");
    Row(g, "Hold Grip + Other Stick Up or Down", "Screen Curve");
    Row(g, "Same in Field of View", ImNames::kDepth3d);
    Row(g, "Same in Stretch", ImNames::kRenderMargin);
    Row(g, "Both Grips + Other Stick Sideways", ImNames::kHorizontalScale);
    Row(g, "Both Grips + Other Stick Up or Down", ImNames::kVerticalScale);
    Row(g, n.Face("B") + " or " + n.MenuPress(), "Finish");

    // ---- Azahar's window (the home panel), the in-game menu ----
    auto& w = page.window;
    Heading(w, "Azahar's Window", !ctx.game_running || ctx.in_menu);
    Row(w, "Trigger or " + n.Face("A"), "Click");
    Row(w, n.Trackpads() ? "Left Trackpad or Left Stick Up / Down" : "Left Stick Up / Down",
        "Choose Game");
    Row(w, n.Face("A") + " Twice", "Start Game");
    Row(w, "Right Stick Click", "Right Click");
    Row(w, "Hold Right Stick Click (1 s)", "Bring Window Back");
    Row(w, n.Face("B"), "Back");
    Row(w, "Grip", "Move Window");
    Row(w, "Right Stick Up or Down", "Scroll");
    Row(w, n.Face("Y"), "Keyboard");
    Heading(w, "In-Game Menu", ctx.in_menu);
    Row(w, n.MenuHold(), "Back to Game");
    Row(w, n.Face("B") + " (Nothing Open)", "Back to Game");

    page.footer = PcFooter(given, ctx.scheme);
    return page;
}

HelpDiagram BuildPcHelpDiagram(const HelpBindings& given, const HelpContext& ctx) {
    const Bindings b{given.valid ? given : DefaultHelpBindings(), ctx.console};
    const PcNames n{Input::SchemeTable(ctx.scheme)};
    const ControllerSchemeTable& t = n.t;
    HelpDiagram d;
    d.now = HelpDiagramPageFor(ctx);
    d.footer = PcFooter(given, ctx.scheme);
    // The face buttons: the right hand's A and B; the left hand's X and Y (Touch, Focus 3) or
    // its A and B (Index), which are the pad's X and Y.
    const bool index = n.Trackpads();
    const std::vector<CtrlPart> left_faces =
        index ? std::vector<CtrlPart>{CtrlPart::A, CtrlPart::B}
              : std::vector<CtrlPart>{CtrlPart::X, CtrlPart::Y};
    const auto face_key = [&](const char* frame) {
        // On the drawing the hand is known: the button's own letter.
        const std::string name = n.Face(frame);
        return name.rfind("Left ", 0) == 0 ? name.substr(5)
               : name.rfind("Right ", 0) == 0 ? name.substr(6)
                                              : name;
    };
    // The Menu role's part: the left Menu button, or the right trackpad.
    const int menu_hand = index ? kRight : kLeft;
    const CtrlPart menu_part = index ? DiagramTrackpadPart(kRight) : CtrlPart::Menu;
    const std::string menu_title = index ? "Right Trackpad Press" : "Menu";

    // ---- In Game ----
    DiagramPage& g = d.pages[static_cast<std::size_t>(DiagramPageId::Game)];
    g.name = "In Game";
    Label(g, kLeft, {CtrlPart::Trigger}, TriggerLines("Left Trigger", ThreeDs(b, Control::TriggerL)));
    Label(g, kRight, {CtrlPart::Trigger}, TriggerLines("Right Trigger", ThreeDs(b, Control::TriggerR)));
    if (t.grips_are_lr) {
        Label(g, kLeft, {CtrlPart::Grip}, {Title("Left Grip", OrNothing(ThreeDs(b, Control::BumperL)))});
        Label(g, kRight, {CtrlPart::Grip}, {Title("Right Grip", OrNothing(ThreeDs(b, Control::BumperR)))});
    }
    // The D-pad's lines: the 3DS's D-pad, and a line for each direction the profile changed.
    const auto dpad_lines = [&](std::vector<DiagramLine>& lines, const std::string& key,
                                bool title) {
        const struct {
            const char* name;
            Control control;
            int button;
        } dirs[] = {{"Up", Control::DpadUp, NB::Up},
                    {"Down", Control::DpadDown, NB::Down},
                    {"Left", Control::DpadLeft, NB::Left},
                    {"Right", Control::DpadRight, NB::Right}};
        std::vector<DiagramLine> subs;
        bool any_plain = false;
        for (const auto& dir : dirs) {
            if (BoundTo(b, dir.control) == std::vector<int>{dir.button}) {
                any_plain = true;
            } else {
                subs.push_back(Sub(dir.name, OrNothing(ThreeDs(b, dir.control))));
            }
        }
        if (any_plain) {
            lines.push_back(title ? Title(key, b.Console() + "D-Pad")
                                  : Line(key, b.Console() + "D-Pad"));
        } else {
            lines.push_back(SubHeading(key));
        }
        lines.insert(lines.end(), subs.begin(), subs.end());
    };
    {
        std::vector<DiagramLine> stick{
            Title("Left Stick", OrNothing(StickTo(b, AnalogControl::StickL)))};
        if (t.dpad == DpadSource::LeftStickToggle) {
            if (t.stick_clicks_for_game) {
                stick.push_back(Line("Click", "Switch Circle Pad / D-Pad"));
            }
            dpad_lines(stick, "D-Pad Mode", false);
        }
        Label(g, kLeft, {CtrlPart::Stick}, std::move(stick));
    }
    if (t.dpad == DpadSource::LeftTrackpadPress) {
        std::vector<DiagramLine> pad;
        dpad_lines(pad, "Left Trackpad Press", true);
        Label(g, kLeft, {DiagramTrackpadPart(kLeft)}, std::move(pad));
    }
    {
        std::vector<DiagramLine> right;
        std::vector<DiagramLine> left;
        for (const Face& f : Faces(!t.face_buttons_by_label)) {
            const std::string frame{f.frame};
            auto& to = frame == "A" || frame == "B" ? right : left;
            to.push_back(Title(face_key(f.frame), OrNothing(ThreeDs(b, f.pad))));
        }
        Label(g, kRight, {CtrlPart::A, CtrlPart::B}, std::move(right));
        Label(g, kLeft, left_faces, std::move(left));
    }
    {
        std::vector<DiagramLine> stick{
            Title("Right Stick", OrNothing(StickTo(b, AnalogControl::StickR)))};
        if (t.stick_clicks_for_game) {
            stick.push_back(Line("Click", OrNothing(ThreeDs(b, Control::View))));
        }
        Label(g, kRight, {CtrlPart::Stick}, std::move(stick));
    }
    {
        std::vector<DiagramLine> menu{Title(menu_title, OrNothing(ThreeDs(b, Control::Menu))),
                                      Line("Hold (" + Secs(n.config.menu_hold_ns) + ")",
                                           "In-Game Menu"),
                                      SubHeading("Hold " + n.Menu() + " +"),
                                      Sub("Right Grip", "Quick Save"),
                                      Sub(n.QuickLoadHold(), "Quick Load"),
                                      Sub("Right Trigger", "Fast Forward On / Off")};
        if (t.both_clicks_menu) {
            menu.push_back(Line("Both Stick Clicks", "Same as Menu"));
        }
        Label(g, menu_hand, {menu_part}, std::move(menu));
    }

    // ---- Azahar's window (the home panel), the in-game menu, Move Screens ----
    DiagramPage& w = d.pages[static_cast<std::size_t>(DiagramPageId::Window)];
    w.name = "Azahar's Window";
    Label(w, kLeft, {CtrlPart::Trigger}, {Title("Left Trigger", "Click")});
    Label(w, kRight, {CtrlPart::Trigger}, {Title("Right Trigger", "Click")});
    Label(w, kLeft, {CtrlPart::Stick}, {Title("Left Stick", "Choose Game")});
    if (index) {
        Label(w, kLeft, {DiagramTrackpadPart(kLeft)}, {Title("Left Trackpad Up or Down", "Choose Game")});
    }
    Label(w, kRight, {CtrlPart::A, CtrlPart::B},
          {Title("A", "Click"), Line("Twice", "Start Game"), Title("B", "Back")});
    Label(w, kLeft, left_faces, {Title(face_key("Y"), "Keyboard")});
    Label(w, menu_hand, {menu_part},
          {SubHeading("In-Game Menu"), Sub(n.MenuHold(), "Back to Game"),
           Sub("B (Nothing Open)", "Back to Game")});
    Label(w, kRight, {CtrlPart::Stick},
          {Title("Right Stick", "Scroll"), Line("Click", "Right Click"),
           Line("Hold Click (1 s)", "Bring Window Back")});
    Label(w, kBoth, {CtrlPart::Grip},
          {Title("Grip", "Move Window"), SubHeading("Move Screens"),
           Sub("Grip on a Screen", "Move Screen"),
           Sub("Stick Up or Down", std::string("Nearer / ") + ImNames::kWorldScale),
           Sub("Stick Sideways", "Screen Size"), Sub("Other Stick Sideways", "Nudge / Offset"),
           Sub("Other Stick Up or Down", "Curve"), Sub("B", "Finish")});
    return d;
}

} // namespace

HelpBindings DefaultHelpBindings() {
    HelpBindings b;
    for (const auto& binding : Input::kDefaultButtonBindings) {
        b.buttons[binding.button] =
            "engine:frame_xr,button:" + std::to_string(static_cast<int>(binding.control));
    }
    for (const auto& binding : Input::kDefaultAnalogBindings) {
        b.analogs[binding.analog] =
            "engine:frame_xr,analog:" + std::to_string(static_cast<int>(binding.control));
    }
    b.valid = true;
    return b;
}

HelpPage BuildHelp(const HelpBindings& given, const HelpContext& ctx) {
    if (ctx.scheme != Input::ControllerScheme::Frame) { // AzaharVR pchelp
        return BuildPcHelp(given, ctx);
    }
    const Bindings b{given.valid ? given : DefaultHelpBindings(), ctx.console};
    HelpPage page;

    // ---- In Game ----
    auto& g = page.game;
    Heading(g, "In Game", ctx.game_running && !ctx.in_menu);
    const bool pos = ctx.face_by_position;
    for (const Face& f : Faces(pos)) {
        Row(g, f.frame, OrNothing(ThreeDs(b, f.pad)));
    }
    // The D-pad as one row when it is the 3DS's D-pad, else one row per direction.
    if (DpadPlain(b)) {
        Row(g, "D-Pad", b.Console() + "D-Pad");
    } else {
        Row(g, "D-Pad Up", OrNothing(ThreeDs(b, Control::DpadUp)));
        Row(g, "D-Pad Down", OrNothing(ThreeDs(b, Control::DpadDown)));
        Row(g, "D-Pad Left", OrNothing(ThreeDs(b, Control::DpadLeft)));
        Row(g, "D-Pad Right", OrNothing(ThreeDs(b, Control::DpadRight)));
    }
    Row(g, "Left Stick", OrNothing(StickTo(b, AnalogControl::StickL)));
    Row(g, "Right Stick", OrNothing(StickTo(b, AnalogControl::StickR)));
    Row(g, "Left Bumper", OrNothing(ThreeDs(b, Control::BumperL)));
    Row(g, "Right Bumper", OrNothing(ThreeDs(b, Control::BumperR)));
    // The triggers touch when aimed at the shown bottom screen, whatever the profile says.
    Row(g, "Left Trigger", With("Touch on Secondary Screen", ThreeDs(b, Control::TriggerL)));
    Row(g, "Right Trigger", With("Touch on Secondary Screen", ThreeDs(b, Control::TriggerR)));
    Row(g, "Menu", OrNothing(ThreeDs(b, Control::Menu)));
    Row(g, "View", OrNothing(ThreeDs(b, Control::View)));
    // AzaharFrame screenswap: the stick clicks are taps (on release) and 1 s holds (mapper.cpp).
    Row(g, "Left Stick Click", "Hide or Show Secondary Screen");
    Row(g, "Hold Left Stick Click (1 s)", "Screen Layout"); // AzaharFrame layout
    Row(g, "Right Stick Click", With("Swap Screens", ThreeDs(b, Control::StickClickR)));
    Row(g, "Hold Right Stick Click (1 s)", "Reset Screens and View");
    Row(g, "Grip on a Screen", With("Move Screens", Grips(b)));
    Row(g, "Grip (Immersive)", "Move Immersive Screen");
    // AzaharFrame natural: in the Full modes the distance is World Scale.
    Row(g, "Hold Grip + Stick Up or Down", std::string("Distance or ") + ImNames::kWorldScale);
    Row(g, "Hold Grip + Stick Sideways", "Screen Size");
    // AzaharFrame vrsize; rel120: on the immersive screen it is the Immersive Offset
    // (Immersive::NudgeOffset), as the grip hint says.
    Row(g, "Hold Grip + D-Pad Left or Right", "Nudge Screen or Offset");
    // AzaharFrame curve; kiu: Render Margin (was Wider View), and both grips' two scales.
    // AzaharFrame fovaxis: on the immersive screen it follows Scale Mode (Full modes only):
    // 3D Depth with Field of View, Render Margin with Stretch (Layout::GripUpDownFor).
    Row(g, "Hold Grip + D-Pad Up or Down", "Screen Curve");
    Row(g, "Same in Field of View", ImNames::kDepth3d);
    Row(g, "Same in Stretch", ImNames::kRenderMargin);
    Row(g, "Both Grips + D-Pad Left or Right", ImNames::kHorizontalScale);
    Row(g, "Both Grips + D-Pad Up or Down", ImNames::kVerticalScale);
    Row(g, "Hold Menu (1 s)", "In-Game Menu");
    Row(g, "Hold View (1.5 s)", "Stop Game");
    Row(g, "Hold View + Left Bumper (0.5 s)", "Quick Load");
    Row(g, "Hold View + Right Bumper", "Quick Save");
    Row(g, "Hold View + Right Trigger", "Fast Forward On / Off");
    Row(g, "Both Bumpers + View", "Show or Hide Secondary Screen");

    // ---- Azahar's window (the home panel), the in-game menu, everywhere ----
    auto& w = page.window;
    Heading(w, "Azahar's Window", !ctx.game_running || ctx.in_menu);
    Row(w, "Trigger or A", "Click");
    Row(w, "D-Pad or Left Stick Up / Down", "Choose Game"); // AzaharFrame gamelist
    Row(w, "A Twice", "Start Game");
    Row(w, "Right Stick Click", "Right Click");
    Row(w, "Hold Right Stick Click (1 s)", "Bring Window Back");
    Row(w, "B", "Back");
    Row(w, "Grip", "Move Window");
    Row(w, "Right Stick Up or Down", "Scroll");
    Row(w, "Y", "Keyboard");
    Heading(w, "In-Game Menu", ctx.in_menu);
    Row(w, "Hold Menu (1 s)", "Back to Game");
    Row(w, "B (Nothing Open)", "Back to Game");
    Row(w, "Hold View (1.5 s)", "Stop Game");
    Heading(w, "Everywhere", false);
    Row(w, "Both Bumpers + View + Menu", "Switch VR / Flat");

    page.footer = Footer(given, pos);
    return page;
}

HelpDiagram BuildHelpDiagram(const HelpBindings& given, const HelpContext& ctx) {
    if (ctx.scheme != Input::ControllerScheme::Frame) { // AzaharVR pchelp
        return BuildPcHelpDiagram(given, ctx);
    }
    const Bindings b{given.valid ? given : DefaultHelpBindings(), ctx.console};
    const bool pos = ctx.face_by_position;
    HelpDiagram d;
    d.now = HelpDiagramPageFor(ctx);
    d.footer = Footer(given, pos);

    // ---- In Game ----
    DiagramPage& g = d.pages[static_cast<std::size_t>(DiagramPageId::Game)];
    g.name = "In Game";
    Label(g, kLeft, {CtrlPart::Bumper}, {Title("Left Bumper", OrNothing(ThreeDs(b, Control::BumperL)))});
    Label(g, kLeft, {CtrlPart::Trigger}, TriggerLines("Left Trigger", ThreeDs(b, Control::TriggerL)));
    {
        // The 3DS's D-pad, and a line for each direction the profile changed (all four when
        // none is left).
        const struct {
            const char* name;
            Control control;
            int button;
        } dirs[] = {{"Up", Control::DpadUp, NB::Up},
                    {"Down", Control::DpadDown, NB::Down},
                    {"Left", Control::DpadLeft, NB::Left},
                    {"Right", Control::DpadRight, NB::Right}};
        std::vector<DiagramLine> lines;
        bool any_plain = false;
        for (const auto& d : dirs) {
            if (BoundTo(b, d.control) == std::vector<int>{d.button}) {
                any_plain = true;
            } else {
                lines.push_back(Sub(d.name, OrNothing(ThreeDs(b, d.control))));
            }
        }
        lines.insert(lines.begin(), any_plain ? Title("D-Pad", b.Console() + "D-Pad") : SubHeading("D-Pad"));
        Label(g, kLeft, {CtrlPart::Dpad}, std::move(lines));
    }
    Label(g, kLeft, {CtrlPart::View},
          {Title("View", OrNothing(ThreeDs(b, Control::View))), Line("Hold (1.5 s)", "Stop Game"),
           SubHeading("Hold View +"), Sub("Hold Left Bumper (0.5 s)", "Quick Load"),
           Sub("Right Bumper", "Quick Save"), Sub("Right Trigger", "Fast Forward On / Off"),
           SubHeading("Both Bumpers +"), Sub("View", "Show or Hide Secondary Screen"),
           Sub("View + Menu", "Switch VR / Flat")});
    // AzaharFrame screenswap: the stick clicks are taps (on release) and 1 s holds (mapper.cpp).
    Label(g, kLeft, {CtrlPart::Stick},
          {Title("Left Stick", OrNothing(StickTo(b, AnalogControl::StickL))),
           Line("Click", "Hide or Show Secondary Screen"),
           Line("Hold Click (1 s)", "Screen Layout")}); // AzaharFrame layout
    std::vector<DiagramLine> faces;
    for (const Face& f : Faces(pos)) {
        faces.push_back(Title(f.frame, OrNothing(ThreeDs(b, f.pad))));
    }
    Label(g, kRight, {CtrlPart::A, CtrlPart::B, CtrlPart::X, CtrlPart::Y}, std::move(faces));
    Label(g, kRight, {CtrlPart::Bumper}, {Title("Right Bumper", OrNothing(ThreeDs(b, Control::BumperR)))});
    Label(g, kRight, {CtrlPart::Trigger}, TriggerLines("Right Trigger", ThreeDs(b, Control::TriggerR)));
    Label(g, kRight, {CtrlPart::Menu},
          {Title("Menu", OrNothing(ThreeDs(b, Control::Menu))), Line("Hold (1 s)", "In-Game Menu")});
    Label(g, kRight, {CtrlPart::Stick},
          {Title("Right Stick", OrNothing(StickTo(b, AnalogControl::StickR))),
           Line("Click", With("Swap Screens", ThreeDs(b, Control::StickClickR))),
           Line("Hold Click (1 s)", "Reset Screens and View")});
    Label(g, kBoth, {CtrlPart::Grip},
          {Title("Grip on a Screen", With("Move Screens", Grips(b))), SubHeading("Hold Grip +"),
           // AzaharFrame natural: in the Full modes, World Scale. "Distance / World Scale" is a
           // pixel too wide beside the right stick's label (vis test "the diagram's labels fit").
           Sub("Stick Up or Down", std::string("Nearer / ") + ImNames::kWorldScale),
           Sub("Stick Sideways", "Screen Size"),
           Sub("D-Pad Left or Right", "Nudge / Offset"), // AzaharFrame vrsize; rel120: Offset
           // AzaharFrame fovaxis: the normal screens' Curve; the immersive screen's is in the
           // right grip's label (under the drawings there is no room for more lines).
           Sub("D-Pad Up or Down", "Curve")}); // AzaharFrame curve
    // AzaharFrame kiu: both grips' D-pad, in the right column (under the drawings there is no
    // room left for it).
    // AzaharFrame fovaxis: and one grip's D-pad up or down on the immersive screen (Full modes),
    // which Scale Mode decides: 3D Depth with Field of View, Render Margin with Stretch.
    Label(g, kRight, {CtrlPart::Grip},
          {SubHeading("Hold Both Grips +"), Sub("D-Pad Left or Right", ImNames::kHorizontalScale),
           Sub("D-Pad Up or Down", ImNames::kVerticalScale),
           SubHeading("Full Modes, Grip + D-Pad Up or Down"),
           Sub("Field of View", ImNames::kDepth3d), Sub("Stretch", ImNames::kRenderMargin)});

    // ---- Azahar's window (the home panel), the in-game menu, everywhere ----
    DiagramPage& w = d.pages[static_cast<std::size_t>(DiagramPageId::Window)];
    w.name = "Azahar's Window";
    Label(w, kLeft, {CtrlPart::Trigger}, {Title("Left Trigger", "Click")});
    Label(w, kLeft, {CtrlPart::View},
          {SubHeading("In-Game Menu"), Sub("Hold View (1.5 s)", "Stop Game"),
           SubHeading("Both Bumpers +"), Sub("View + Menu", "Switch VR / Flat")});
    // AzaharFrame gamelist: the game list (the left stick scrolls in dialogs and menus).
    Label(w, kLeft, {CtrlPart::Stick}, {Title("Left Stick", "Choose Game")});
    Label(w, kLeft, {CtrlPart::Dpad}, {Title("D-Pad Up or Down", "Choose Game")});
    Label(w, kRight, {CtrlPart::A, CtrlPart::B, CtrlPart::X, CtrlPart::Y},
          {Title("A", "Click"), Line("Twice", "Start Game"), Title("B", "Back"),
           Title("Y", "Keyboard")});
    Label(w, kRight, {CtrlPart::Trigger}, {Title("Right Trigger", "Click")});
    Label(w, kRight, {CtrlPart::Menu},
          {SubHeading("In-Game Menu"), Sub("Hold Menu (1 s)", "Back to Game"),
           Sub("B (Nothing Open)", "Back to Game")});
    Label(w, kRight, {CtrlPart::Stick},
          {Title("Right Stick", "Scroll"), Line("Click", "Right Click"),
           Line("Hold Click (1 s)", "Bring Window Back")});
    Label(w, kBoth, {CtrlPart::Grip}, {Title("Grip", "Move Window")});
    return d;
}

DiagramPageId HelpDiagramPageFor(const HelpContext& ctx) {
    return ctx.game_running ? DiagramPageId::Game : DiagramPageId::Window;
}

const DiagramLabel* FindDiagramLabel(const DiagramPage& page, int hand, CtrlPart part) {
    for (const DiagramLabel& l : page.labels) {
        if ((l.hand == hand || l.hand == kBoth) &&
            std::find(l.parts.begin(), l.parts.end(), part) != l.parts.end()) {
            return &l;
        }
    }
    return nullptr;
}

} // namespace FrameXr::Vis
