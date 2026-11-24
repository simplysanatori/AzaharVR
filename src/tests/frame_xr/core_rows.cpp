// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Which rows of the in-game menu each console has (frame_xr/menu/core_rows.h).

#include <set>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "frame_xr/menu/core_rows.h"
#include "frame_xr/menu/tab_menu.h"
#include "frame_xr/ui/ui_canvas.h"

using namespace FrameXr::Menu;

namespace {

const std::set<Id> kOthersOnly{Id::CoreHeader,     Id::GbaSolarLevel, Id::DsConsoleMode,
                               Id::DsTouchEnabled, Id::DsMicBlow,     Id::DsLidToggle,
                               Id::ImportSave,     Id::DsScreenRotation, Id::SecondScreen,
                               Id::AllowGameEditing};

/// Every console's.
const std::set<Id> kCommon{
    Id::CoreHeader,       Id::ImportSave,         Id::ScreenCurve,   Id::Background,
    Id::WindowScale,      Id::FaceButtons,        Id::ShowControllers,
    Id::ControllersInFront, Id::FastForwardSpeed, Id::FastForwardAudio,
    Id::MoreSettings,     Id::SwitchToFlat,       Id::HelpHeader,    Id::ReportProblem,
    Id::AboutApp};

std::set<Id> With(std::set<Id> ids, const std::set<Id>& more) {
    ids.insert(more.begin(), more.end());
    return ids;
}

const std::set<Id> kTwoScreens{Id::HideBottomScreen, Id::ScreenLayout, Id::MoveScreens,
                               Id::SwapScreens};
const std::set<Id> kSaves{Id::SavesHeader, Id::Autosave, Id::AutosaveEvery, Id::AutosaveKeep,
                          Id::AutosaveCard};

std::set<Id> Expected(const std::set<Id>& own) {
    std::set<Id> ids = With(kCommon, own);
    return kAltAutosave ? With(ids, kSaves) : ids;
}

std::set<Id> Table(const CoreCaps& core) {
    std::set<Id> ids;
    for (int i = 0; i < kIdCount; i++) {
        if (RowApplies(static_cast<Id>(i), core)) {
            ids.insert(static_cast<Id>(i));
        }
    }
    return ids;
}

const std::set<Id> kTouch{Id::TouchpadTouch, Id::BottomScreenReticle, Id::ReticleSpeed,
                          Id::ReticleAtEdge, Id::ReticleCenter, Id::ReticleStick,
                          Id::ReticleEdgeCStick};
const std::set<Id> kDs =
    With(With(kTwoScreens, kTouch), {Id::DsConsoleMode, Id::DsTouchEnabled, Id::DsMicBlow,
                                     Id::DsLidToggle, Id::DsScreenRotation});

std::vector<CoreCaps> OtherConsoles() {
    return {CoreCaps::ForGBA(), CoreCaps::ForGB(), CoreCaps::ForNDS(), CoreCaps::ForNDS(true)};
}

MenuModel Game(const CoreCaps& core) {
    MenuModel m;
    m.game_title = "Game";
    m.core = core;
    return m;
}

/// The Ids drawn as targets on a view, scrolled from top to end.
std::set<Id> Drawn(const MenuModel& m, MenuView v) {
    std::set<Id> ids;
    Kit kit{1.0f};
    frame_xr::ui::Canvas canvas;
    TabMenu menu;
    for (float scroll = 0.0f; scroll <= 3000.0f; scroll += kRowH) {
        v.scroll[static_cast<std::size_t>(v.tab)] = scroll;
        canvas.Init(static_cast<int>(kPanelW), static_cast<int>(kPanelH));
        kit.Begin(canvas);
        menu.Draw(kit, m, v);
        for (const auto& h : menu.Hits()) {
            ids.insert(h.t.id);
        }
    }
    return ids;
}

MenuView SettingsView(SettingsSection section) {
    MenuView v;
    v.tab = Tab::Settings;
    v.section = section;
    return v;
}

} // namespace

TEST_CASE("Core rows: a 3DS game has every row but the other consoles'", "[frame_xr][tabmenu]") {
    for (int i = 0; i < kIdCount; i++) {
        INFO("id " << i);
        const Id id = static_cast<Id>(i);
        CHECK(RowApplies(id, CoreCaps::ForN3DS()) == !kOthersOnly.contains(id));
    }
    const MenuModel m = Game(CoreCaps::ForN3DS());
    for (int i = 0; i < static_cast<int>(SettingsSection::Count); i++) {
        CHECK(SectionShown(static_cast<SettingsSection>(i), m));
    }
    for (int i = 0; i < static_cast<int>(Tab::Count); i++) {
        CHECK(TabApplies(static_cast<Tab>(i), m.core));
    }
    CHECK(MenuModel{}.core == CoreCaps::ForN3DS());
}

TEST_CASE("Core rows: the table for each other console", "[frame_xr][tabmenu]") {
    CHECK(Table(CoreCaps::ForGB()) == Expected({}));
    CHECK(Table(CoreCaps::ForGBA()) == Expected({Id::GbaSolarLevel}));
    CHECK(Table(CoreCaps::ForNDS()) == Expected(kDs));
    CHECK(Table(CoreCaps::ForNDS(true)) == Expected(kDs));
    // A GBA game without the sensor, and one whose map is on the Secondary Screen
    CoreCaps map = CoreCaps::ForGBA();
    map.has_solar_sensor = false;
    map.screens.has_bottom = true;
    CHECK(Table(map) == Expected(kTwoScreens));
    // GBA and Game Boy games with a Secondary Screen to touch: how to touch it, none of the DS's
    for (CoreCaps touch : {CoreCaps::ForGBA(), CoreCaps::ForGB()}) {
        touch.has_solar_sensor = false;
        touch.screens.has_bottom = true;
        touch.has_touch = true;
        CHECK(Table(touch) == Expected(With(kTwoScreens, kTouch)));
        const auto general = Drawn(Game(touch), SettingsView(SettingsSection::General));
        CHECK(!general.contains(Id::DsTouchEnabled));
        touch.has_save_import = false;
        CHECK(!RowApplies(Id::CoreHeader, touch));
    }
    // Black Frame Insertion brings its rows and Graphics back.
    map.has_bfi = true;
    CHECK(RowApplies(Id::MotionClarityNormal, map));
    CHECK(SectionShown(SettingsSection::Graphics, Game(map)));
}

TEST_CASE("Core rows: the menu draws what the table says", "[frame_xr][tabmenu]") {
    for (const CoreCaps& core : OtherConsoles()) {
        INFO("console " << static_cast<int>(core.kind));
        const MenuModel m = Game(core);
        for (const Id id : Drawn(m, SettingsView(SettingsSection::General))) {
            if (PerGameSetting(id) || kOthersOnly.contains(id)) {
                INFO("id " << static_cast<int>(id));
                CHECK(RowApplies(id, core));
            }
        }
        const auto general = Drawn(m, SettingsView(SettingsSection::General));
        CHECK(general.contains(Id::FaceButtons));
        CHECK(general.contains(Id::ImportSave));
        CHECK(general.contains(Id::Autosave) == kAltAutosave);
        CHECK(general.contains(Id::DsConsoleMode) == core.has_dsi_mode);
        // General alone: no section to pick
        CHECK(SectionShown(SettingsSection::General, m));
        CHECK(!SectionShown(SettingsSection::Graphics, m));
        CHECK(!SectionShown(SettingsSection::Immersive, m));
        CHECK(!general.contains(Id::SettingsSection));
        CHECK(!TabApplies(Tab::Homebrew, core));
        CHECK(TabApplies(Tab::Mods, core) == core.has_alt_cheats);
        MenuView position;
        position.tab = Tab::Position;
        const auto placed = Drawn(m, position);
        CHECK(placed.contains(Id::DsScreenRotation) == core.has_screen_rotation);
        CHECK(placed.contains(Id::SwapScreens) == core.screens.has_bottom);
        CHECK(!placed.contains(Id::ScreenShape));
        // Load State has no Autosave card
        MenuModel off = m;
        off.autosave = false;
        MenuView load;
        load.page = Page::LoadSlots;
        CHECK(Drawn(off, load).contains(Id::AutosaveSettings) == kAltAutosave);
        CHECK(Drawn(off, load).contains(Id::SlotsBack));
    }
    // The 3DS keeps its sections and the card.
    const MenuModel ds3 = Game(CoreCaps::ForN3DS());
    CHECK(Drawn(ds3, SettingsView(SettingsSection::General)).contains(Id::SettingsSection));
    CHECK(Drawn(ds3, SettingsView(SettingsSection::Graphics)).contains(Id::InternalResolution));
    MenuView load;
    load.page = Page::LoadSlots;
    CHECK(Drawn(ds3, load).contains(Id::AutosaveSettings));
}

TEST_CASE("Core rows: a section's target keeps its own value", "[frame_xr][tabmenu]") {
    CoreCaps core = CoreCaps::ForNDS();
    core.has_bfi = true; // General and Graphics
    const MenuModel m = Game(core);
    Kit kit{1.0f};
    frame_xr::ui::Canvas canvas;
    TabMenu menu;
    canvas.Init(static_cast<int>(kPanelW), static_cast<int>(kPanelH));
    kit.Begin(canvas);
    menu.Draw(kit, m, SettingsView(SettingsSection::Graphics));
    std::vector<int> sections;
    for (const auto& h : menu.Hits()) {
        if (h.t.id == Id::SettingsSection) {
            sections.push_back(h.t.index);
        }
    }
    CHECK(sections == std::vector<int>{static_cast<int>(SettingsSection::General),
                                       static_cast<int>(SettingsSection::Graphics)});
}

TEST_CASE("Core rows: Use Global Values counts only the console's rows", "[frame_xr][tabmenu]") {
    MenuModel m = Game(CoreCaps::ForNDS());
    for (int i = 0; i < kIdCount; i++) {
        m.game_value[static_cast<std::size_t>(i)] = PerGameSetting(static_cast<Id>(i));
    }
    for (const Tab tab : {Tab::Settings, Tab::Position, Tab::Controls}) {
        for (const Id id : GameValueRows(m, tab)) {
            INFO("id " << static_cast<int>(id));
            CHECK(RowApplies(id, m.core));
        }
    }
    CHECK(GameValueRows(m, Tab::Position) ==
          std::vector<Id>{Id::HideBottomScreen, Id::DsScreenRotation});
}

TEST_CASE("Core rows: the text", "[frame_xr][tabmenu]") {
    CHECK(OptionsFor(Id::DsConsoleMode) == std::vector<std::string>{"Automatic", "DS", "DSi"});
    const CoreCaps n3ds = CoreCaps::ForN3DS();
    for (int i = 0; i < kIdCount; i++) {
        const Id id = static_cast<Id>(i);
        CHECK(ExplainFor(id, n3ds) == ExplainFor(id));
    }
    for (const CoreCaps& core : OtherConsoles()) {
        for (const Id id : Table(core)) {
            const std::string text = ExplainFor(id, core);
            INFO(text);
            CHECK(text.find("3DS") == std::string::npos);
        }
    }
    for (const Id id : kOthersOnly) {
        CHECK(ExplainFor(id).find("ottom") == std::string::npos);
    }
}

TEST_CASE("DS settings: degrees and consoles", "[frame_xr]") {
    using namespace Common::AltCore;
    CHECK(RotationDegrees(0) == 0);
    CHECK(RotationDegrees(1) == 90);
    CHECK(RotationDegrees(2) == 270);
    CHECK(RotationDegrees(3) == 0);
    CHECK(TouchEnabled(CoreCaps::ForN3DS(), false)); // the DS's switch is not the 3DS's
    CHECK(!TouchEnabled(CoreCaps::ForNDS(), false));
    CHECK(TouchEnabled(CoreCaps::ForNDS(), true));
    CHECK(!TouchEnabled(CoreCaps::ForGBA(), true));
    CoreCaps touch = CoreCaps::ForGBA();
    touch.has_touch = true;
    CHECK(TouchEnabled(touch, false)); // the DS's switch is not a GBA game's
}

// GBA and Game Boy games: Second Screen and Allow Game Editing, by what the game has.
TEST_CASE("Core rows: Second Screen and Allow Game Editing", "[frame_xr][tabmenu]") {
    using Common::AltCore::SecondScreenBit;
    const std::set<Id> both{Id::SecondScreen, Id::AllowGameEditing};
    const auto has = [](const CoreCaps& core, Id id) { return Table(core).contains(id); };
    // The capabilities default to none: no row, whatever the console
    for (const CoreCaps& core : {CoreCaps::ForGBA(), CoreCaps::ForGB(), CoreCaps::ForNDS(),
                                 CoreCaps::ForNDS(true), CoreCaps::ForN3DS()}) {
        CHECK(core.second_screen_choices == 0u);
        CHECK(!core.can_edit_game);
        CHECK(!has(core, Id::SecondScreen));
        CHECK(!has(core, Id::AllowGameEditing));
    }
    const uint32_t kPoke = SecondScreenBit(0), kGbaDs = SecondScreenBit(1), kOff = SecondScreenBit(2);
    for (CoreCaps core : {CoreCaps::ForGBA(), CoreCaps::ForGB()}) {
        // A choice alone is no choice: hidden; two or three show the row
        for (const uint32_t mask : {kPoke, kOff, kPoke | kGbaDs, kPoke | kOff, kGbaDs | kOff,
                                    kPoke | kGbaDs | kOff}) {
            core.second_screen_choices = mask;
            INFO("mask " << mask);
            CHECK(has(core, Id::SecondScreen) == (mask != kPoke && mask != kOff));
            CHECK(has(core, Id::CoreHeader));
        }
        core.second_screen_choices = 0;
        core.can_edit_game = true;
        CHECK(has(core, Id::AllowGameEditing));
        CHECK(!has(core, Id::SecondScreen));
    }
    // Only the Console group's rows follow; a game with neither has just what it had
    CoreCaps gb = CoreCaps::ForGB();
    CHECK(Table(gb) == Expected({Id::ImportSave}));
    gb.second_screen_choices = kPoke | kOff;
    gb.can_edit_game = true;
    CHECK(Table(gb) == Expected(With({Id::ImportSave}, both)));
    // Another console's capabilities never bring them: the 3DS has neither row
    CoreCaps n3ds = CoreCaps::ForN3DS();
    n3ds.second_screen_choices = kPoke | kOff;
    n3ds.can_edit_game = true;
    CHECK(!has(n3ds, Id::SecondScreen));
    CHECK(!has(n3ds, Id::AllowGameEditing));
    // Drawn in the Console group, on the general section
    CoreCaps gba = CoreCaps::ForGBA();
    gba.second_screen_choices = kPoke | kGbaDs | kOff;
    gba.can_edit_game = true;
    const auto drawn = Drawn(Game(gba), SettingsView(SettingsSection::General));
    CHECK(drawn.contains(Id::SecondScreen));
    CHECK(drawn.contains(Id::AllowGameEditing));
    gba.second_screen_choices = kOff;
    gba.can_edit_game = false;
    const auto none = Drawn(Game(gba), SettingsView(SettingsSection::General));
    CHECK(!none.contains(Id::SecondScreen));
    CHECK(!none.contains(Id::AllowGameEditing));
    // The options: the game's own, in order; the row's index counts them
    CHECK(OptionsFor(Id::SecondScreen) == std::vector<std::string>{"PokeDaisy", "GBA DS", "Off"});
    gba.second_screen_choices = kPoke | kGbaDs | kOff;
    CHECK(SecondScreenOptions(gba) == OptionsFor(Id::SecondScreen));
    gba.second_screen_choices = kPoke | kOff;
    CHECK(SecondScreenOptions(gba) == std::vector<std::string>{"PokeDaisy", "Off"});
    CHECK(SecondScreenValue(gba, 1) == 2);
    CHECK(SecondScreenIndex(gba, 2) == 1);
    CHECK(SecondScreenIndex(gba, 1) == 0); // GBA DS not there: the first it has, as ChooseDriver
    gba.second_screen_choices = kGbaDs | kOff;
    CHECK(SecondScreenIndex(gba, 0) == 0);
    CHECK(SecondScreenValue(gba, 0) == 1);
    CHECK(LabelFor(Id::SecondScreen) == "Second Screen");
    CHECK(LabelFor(Id::AllowGameEditing) == "Allow Game Editing");
    CHECK(PerGameSetting(Id::SecondScreen));
    CHECK(PerGameSetting(Id::AllowGameEditing));
}

// The notice shown the first time Allow Game Editing is turned on.
TEST_CASE("Core rows: the game editing notice", "[frame_xr][tabmenu]") {
    namespace A = Common::AltCore;
    CHECK(std::string(A::kGameEditingTitle) == "PokeDaisy Edits Your Game");
    CHECK(std::string(A::kGameEditingBody) ==
          "Features such as Fly, the PC, items and wild encounters change the game while it runs. "
          "This can corrupt your save. Azahar VR backs up the save first.");
    CHECK(std::string(A::kGameEditingConfirm) == "Turn On");
    CHECK(std::string(A::kGameEditingCancel) == "Cancel");
}
