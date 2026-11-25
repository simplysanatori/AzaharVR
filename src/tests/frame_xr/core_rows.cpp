// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Which rows of the in-game menu each console has (frame_xr/menu/core_rows.h).

#include <algorithm>
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
    Id::AboutApp,         Id::ExperimentalHeader};

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
    for (const SettingsSection section : kSectionOrder) {
        CHECK(SectionShown(section, m) == (section != SettingsSection::Console));
    }
    for (int i = 0; i < static_cast<int>(Tab::Count); i++) {
        CHECK(TabApplies(static_cast<Tab>(i), m.core));
    }
    CHECK(MenuModel{}.core == CoreCaps::ForN3DS());
}

TEST_CASE("The rail: its order, whatever the tabs' values", "[frame_xr][tabmenu]") {
    MenuModel m = Game(CoreCaps::ForN3DS());
    CHECK(RailTabs(m) == std::vector<Tab>{Tab::Main, Tab::Position, Tab::Controls, Tab::Mods,
                                          Tab::Homebrew, Tab::Stats, Tab::Settings});
    // As drawn: each tab's target keeps its own value
    Kit kit{1.0f};
    frame_xr::ui::Canvas canvas;
    TabMenu menu;
    canvas.Init(static_cast<int>(kPanelW), static_cast<int>(kPanelH));
    kit.Begin(canvas);
    menu.Draw(kit, m, MenuView{});
    std::vector<Tab> drawn;
    float y = -1.0f;
    for (const auto& h : menu.Hits()) {
        if (h.t.id == Id::TabButton) {
            CHECK(h.r.y0 > y);
            y = h.r.y0;
            drawn.push_back(static_cast<Tab>(h.t.index));
        }
    }
    CHECK(drawn == RailTabs(m));
    CHECK(static_cast<int>(Tab::Main) == 0);
    CHECK(static_cast<int>(Tab::Stats) == 2);
    CHECK(static_cast<int>(Tab::Settings) == 6);
    m.core = CoreCaps::ForNDS();
    std::vector<Tab> ds{Tab::Main, Tab::Position, Tab::Controls};
    if (m.core.has_alt_cheats) {
        ds.push_back(Tab::Mods);
    }
    ds.insert(ds.end(), {Tab::Stats, Tab::Settings});
    CHECK(RailTabs(m) == ds);
}

TEST_CASE("The Position tab's rows", "[frame_xr][tabmenu]") {
    CHECK(RowIds(Tab::Position, Game(CoreCaps::ForN3DS())) ==
          std::vector<Id>{Id::ScreenLayout, Id::HideBottomScreen, Id::MoveScreens, Id::ScreenShape,
                          Id::ScreenCurve, Id::ImmersiveOffset});
    CHECK(RowIds(Tab::Position, Game(CoreCaps::ForNDS())) ==
          std::vector<Id>{Id::ScreenLayout, Id::DsScreenRotation, Id::HideBottomScreen,
                          Id::MoveScreens, Id::ScreenCurve});
    CHECK(RowIds(Tab::Position, Game(CoreCaps::ForGB())) == std::vector<Id>{Id::ScreenCurve});
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
        const auto console = Drawn(Game(touch), SettingsView(SettingsSection::Console));
        CHECK(console.contains(Id::ImportSave));
        CHECK(!console.contains(Id::DsTouchEnabled));
        touch.has_save_import = false;
        CHECK(!RowApplies(Id::CoreHeader, touch));
        CHECK(!SectionShown(SettingsSection::Console, Game(touch)));
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
        for (const SettingsSection section : {SettingsSection::Console, SettingsSection::General}) {
            for (const Id id : Drawn(m, SettingsView(section))) {
                if (PerGameSetting(id) || kOthersOnly.contains(id)) {
                    INFO("id " << static_cast<int>(id));
                    CHECK(RowApplies(id, core));
                    CHECK(SectionOf(id) == section);
                }
            }
        }
        const auto general = Drawn(m, SettingsView(SettingsSection::General));
        CHECK(general.contains(Id::Background));
        CHECK(!general.contains(Id::FaceButtons)); // on the Controls tab
        CHECK(general.contains(Id::Autosave) == kAltAutosave);
        const auto console = Drawn(m, SettingsView(SettingsSection::Console));
        CHECK(console.contains(Id::ImportSave));
        CHECK(console.contains(Id::DsMicBlow) == core.has_mic_blow);
        CHECK(console.contains(Id::DsConsoleMode) == core.has_dsi_mode);
        CHECK(SectionShown(SettingsSection::Console, m));
        CHECK(SectionShown(SettingsSection::General, m));
        CHECK(!SectionShown(SettingsSection::Graphics, m));
        CHECK(!SectionShown(SettingsSection::Immersive, m));
        CHECK(general.contains(Id::SettingsSection));
        // It opens on Console when the console changed to one that has it
        for (const SettingsSection before : kSectionOrder) {
            CHECK(OpeningSection(before, m, true) == SettingsSection::Console);
            const bool has = before == SettingsSection::Console;
            CHECK(OpeningSection(before, m, false) ==
                  (has ? SettingsSection::Console : SettingsSection::General));
        }
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
    CHECK(OpeningSection(SettingsSection::Console, ds3, true) == SettingsSection::General);
    CHECK(OpeningSection(SettingsSection::Graphics, ds3, true) == SettingsSection::Graphics);
    CHECK(Drawn(ds3, SettingsView(SettingsSection::General)).contains(Id::SettingsSection));
    CHECK(Drawn(ds3, SettingsView(SettingsSection::Graphics)).contains(Id::InternalResolution));
    MenuView load;
    load.page = Page::LoadSlots;
    CHECK(Drawn(ds3, load).contains(Id::AutosaveSettings));
}

TEST_CASE("Core rows: a section's target keeps its own value", "[frame_xr][tabmenu]") {
    CoreCaps core = CoreCaps::ForNDS();
    core.has_bfi = true; // Console, General and Graphics
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
    CHECK(sections == std::vector<int>{static_cast<int>(SettingsSection::Console), // first
                                       static_cast<int>(SettingsSection::General),
                                       static_cast<int>(SettingsSection::Graphics)});
}

namespace {
std::vector<Id> General(bool saves = kAltAutosave) {
    std::vector<Id> ids{Id::Background, Id::WindowScale, Id::FastForwardSpeed,
                        Id::FastForwardAudio};
    if (saves) {
        ids.insert(ids.end(),
                   {Id::SavesHeader, Id::Autosave, Id::AutosaveEvery, Id::AutosaveKeep});
    }
    ids.insert(ids.end(), {Id::MoreSettings, Id::HelpHeader, Id::ReportProblem, Id::AboutApp});
    return ids;
}

/// The Controls tab's own rows, by section, each row's Id found as it is drawn.
std::set<Id> ControlsRows(const MenuModel& m, ControlsSection section) {
    MenuView v;
    v.tab = Tab::Controls;
    v.controls_section = section;
    std::set<Id> ids = Drawn(m, v);
    for (const Id id : {Id::TabButton, Id::Scope, Id::ScopeMore, Id::ControlsSection,
                        Id::ControlsPage, Id::EditControls, Id::Scrollbar, Id::None}) {
        ids.erase(id);
    }
    return ids;
}
const std::set<Id> kButtons{Id::FaceButtons, Id::ShowControllers, Id::ControllersInFront};
const std::set<Id> kPointing{Id::TouchpadTouch, Id::BottomScreenReticle, Id::ReticleSpeed,
                             Id::ReticleAtEdge, Id::ReticleCenter,       Id::ReticleStick};
} // namespace

TEST_CASE("Sections: a 3DS game's rows, in order", "[frame_xr][tabmenu]") {
    MenuModel m = Game(CoreCaps::ForN3DS());
    m.immersive_mode = 2;
    m.motion_clarity_immersive = m.motion_clarity_normal = true;
    m.bottom_screen_reticle = 2;
    m.scale_mode = 0;
    CHECK(RowIds(Tab::Settings, m, SettingsSection::Console).empty());
    CHECK(RowIds(Tab::Settings, m, SettingsSection::General) == General(true));
    CHECK(RowIds(Tab::Settings, m, SettingsSection::Graphics) ==
          std::vector<Id>{Id::Depth3d, Id::InternalResolution, Id::DynamicResolution,
                          Id::TextureFilter, Id::AsyncShaders, Id::AccurateImageCopies,
                          Id::ExperimentalHeader, Id::MotionClarityNormal, Id::MatchBottomNormal});
    const std::vector<Id> immersive{
        Id::ImmersiveMode,  Id::FillView,      Id::WorldScale,         Id::FollowScreen,
        Id::ScaleMode,      Id::ScaleX,        Id::ScaleY,             Id::MarginX,
        Id::MarginY,        Id::GameFov,       Id::MatchCamera,        Id::ExperimentalHeader,
        Id::ScreenAsPanel,  Id::HudSpacingX,   Id::HudSpacingY,        Id::MotionClarity,
        Id::MatchBottom};
    CHECK(RowIds(Tab::Settings, m, SettingsSection::Immersive) == immersive);
    CHECK(ControlsRows(m, ControlsSection::Buttons) == kButtons);
    CHECK(ControlsRows(m, ControlsSection::Pointing) == kPointing);
    CHECK(ControlsRows(m, ControlsSection::Motion) ==
          std::set<Id>{Id::MotionControls, Id::GyroAim, Id::GyroSensitivity});
    // Kid Icarus: Uprising, out of Angel's Aim mode: its weapon rows are Aiming's, Immersive the
    // same; in it, the first-person rows right under Immersive Mode.
    m.mods.angels_aim.shown = m.mods.angels_aim.recommended = true;
    m.gyro_aim = 2;
    CHECK(RowIds(Tab::Settings, m, SettingsSection::Immersive) == immersive);
    const std::vector<Id> aiming = AimingRows(m);
    std::set<Id> motion{aiming.begin(), aiming.end()};
    motion.insert(Id::MotionControls);
    std::set<Id> drawn = ControlsRows(m, ControlsSection::Motion);
    drawn.erase(Id::CalibrateAimReset);
    CHECK(drawn == motion);
    CHECK(motion.contains(Id::WeaponInHand));
    CHECK(ControlsRows(m, ControlsSection::Buttons) == kButtons);
    CHECK(ControlsRows(m, ControlsSection::Pointing) == kPointing);
    m.immersive_mode = 4;
    std::vector<Id> angels = immersive;
    const std::vector<Id> first = AngelsAimFirstPersonRows(m);
    REQUIRE(!first.empty());
    angels.insert(angels.begin() + 1, first.begin(), first.end());
    CHECK(RowIds(Tab::Settings, m, SettingsSection::Immersive) == angels);
}

TEST_CASE("Sections: a DS, a Game Boy Advance and a Game Boy game's rows, in order",
          "[frame_xr][tabmenu]") {
    const MenuModel ds = Game(CoreCaps::ForNDS());
    CHECK(RowIds(Tab::Settings, ds, SettingsSection::Console) ==
          std::vector<Id>{Id::DsMicBlow, Id::DsLidToggle, Id::DsTouchEnabled, Id::DsConsoleMode,
                          Id::ImportSave});
    const MenuModel gba = Game(CoreCaps::ForGBA());
    CHECK(RowIds(Tab::Settings, gba, SettingsSection::Console) ==
          std::vector<Id>{Id::GbaSolarLevel, Id::ImportSave});
    const MenuModel gb = Game(CoreCaps::ForGB());
    CHECK(RowIds(Tab::Settings, gb, SettingsSection::Console) == std::vector<Id>{Id::ImportSave});
    for (const MenuModel& m : {ds, gba, gb}) {
        INFO("console " << static_cast<int>(m.core.kind));
        CHECK(RowIds(Tab::Settings, m, SettingsSection::General) == General());
        // Neither section is there for the header's sake alone
        CHECK(RowIds(Tab::Settings, m, SettingsSection::Graphics).empty());
        CHECK(RowIds(Tab::Settings, m, SettingsSection::Immersive).empty());
        CHECK(ControlsRows(m, ControlsSection::Buttons) == kButtons);
        // A section the console does not have is drawn as Buttons
        CHECK(ControlsRows(m, ControlsSection::Motion) == kButtons);
        CHECK(!ControlsSectionShown(ControlsSection::Motion, m.core));
        CHECK(ControlsSectionShown(ControlsSection::Buttons, m.core));
        CHECK(ControlsSectionShown(ControlsSection::Pointing, m.core) == m.core.has_touch);
    }
    MenuModel reticle = ds;
    reticle.bottom_screen_reticle = 1;
    CHECK(ControlsRows(reticle, ControlsSection::Pointing) == kPointing);
    CHECK(!ControlsSectionShown(ControlsSection::Count, CoreCaps::ForN3DS()));
}

TEST_CASE("Sections: what links to a row goes to it", "[frame_xr][tabmenu]") {
    // Load State's Autosave: Off, Settings: the Saves group's own header, in every console
    for (const CoreCaps& core : {CoreCaps::ForN3DS(), CoreCaps::ForNDS(), CoreCaps::ForGBA(),
                                 CoreCaps::ForGB()}) {
        const MenuModel m = Game(core);
        const auto rows = RowIds(Tab::Settings, m, SettingsSection::General);
        const int before = RowsBefore(Id::SavesHeader, SettingsSection::General, m);
        REQUIRE(before < static_cast<int>(rows.size()));
        CHECK(rows[static_cast<std::size_t>(before)] == Id::SavesHeader);
        CHECK(before == 4);
        Kit kit{1.0f};
        frame_xr::ui::Canvas canvas;
        TabMenu menu;
        canvas.Init(static_cast<int>(kPanelW), static_cast<int>(kPanelH));
        kit.Begin(canvas);
        MenuView v = SettingsView(SettingsSection::General);
        v.scroll[static_cast<std::size_t>(Tab::Settings)] = static_cast<float>(before) * kRowH;
        menu.Draw(kit, m, v);
        std::set<Id> shown;
        for (const auto& h : menu.Hits()) {
            shown.insert(h.t.id);
        }
        CHECK(shown.contains(Id::Autosave));
        CHECK(shown.contains(Id::AutosaveKeep));
    }
    // Angel's Aim's Aiming Settings: the Motion section, Aiming at the top of the list
    MenuModel m = Game(CoreCaps::ForN3DS());
    m.mods.angels_aim.shown = true;
    m.gyro_aim = 2;
    m.recoil = m.weapon_in_hand = true;
    Kit kit{1.0f};
    frame_xr::ui::Canvas canvas;
    TabMenu menu;
    canvas.Init(static_cast<int>(kPanelW), static_cast<int>(kPanelH));
    MenuView v;
    v.tab = Tab::Controls;
    kit.Begin(canvas);
    menu.Draw(kit, m, v);
    CHECK(!menu.Layout().controls); // Buttons: no scroll to take from it
    v.controls_section = ControlsSection::Motion;
    kit.Begin(canvas);
    menu.Draw(kit, m, v);
    REQUIRE(menu.Layout().controls);
    v.scroll[static_cast<std::size_t>(Tab::Controls)] = menu.Layout().aiming_scroll;
    kit.Begin(canvas);
    menu.Draw(kit, m, v);
    bool aiming = false, motion = false;
    for (const auto& h : menu.Hits()) {
        if (h.t.id == Id::GyroAim && h.t.index == kLabelIndex) {
            aiming = h.r.y0 <= menu.Layout().bar.list.y0 + 8.0f;
        }
        motion |= h.t.id == Id::MotionControls;
    }
    CHECK(aiming);
    CHECK(!motion);
}

TEST_CASE("Sections: the scope's count follows the rows", "[frame_xr][tabmenu][scope]") {
    MenuModel m = Game(CoreCaps::ForN3DS());
    for (int i = 0; i < kIdCount; i++) {
        m.game_value[static_cast<std::size_t>(i)] = PerGameSetting(static_cast<Id>(i));
    }
    m.immersive_mode = 2;
    m.motion_clarity_immersive = m.motion_clarity_normal = true;
    m.scale_mode = 0;
    CHECK(GameValueRows(m, Tab::Position) ==
          std::vector<Id>{Id::ScreenLayout, Id::HideBottomScreen, Id::ScreenShape});
    CHECK(GameValueRows(m, Tab::Settings, SettingsSection::General) ==
          std::vector<Id>{Id::FastForwardAudio, Id::Autosave, Id::AutosaveEvery,
                          Id::AutosaveKeep});
    const auto controls = GameValueRows(m, Tab::Controls);
    for (const Id id : {Id::ShowControllers, Id::ControllersInFront, Id::MotionControls,
                        Id::TouchpadTouch, Id::GyroAim}) {
        INFO(LabelFor(id));
        CHECK(std::count(controls.begin(), controls.end(), id) == 1);
    }
    CHECK(std::count(controls.begin(), controls.end(), Id::FaceButtons) == 0); // global
    // DS: the Console section's own, and no row of Motion's
    m.core = CoreCaps::ForNDS();
    for (const Id id : GameValueRows(m, Tab::Controls)) {
        CHECK(id != Id::MotionControls);
        CHECK(id != Id::GyroAim);
    }
    CHECK(GameValueRows(m, Tab::Settings, SettingsSection::Console) ==
          std::vector<Id>{Id::DsTouchEnabled, Id::DsConsoleMode});
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
          std::vector<Id>{Id::ScreenLayout, Id::DsScreenRotation, Id::HideBottomScreen});
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
    // Only the Console section's rows follow; a game with neither has just what it had
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
    // Drawn in the Console section
    CoreCaps gba = CoreCaps::ForGBA();
    gba.second_screen_choices = kPoke | kGbaDs | kOff;
    gba.can_edit_game = true;
    const auto drawn = Drawn(Game(gba), SettingsView(SettingsSection::Console));
    CHECK(drawn.contains(Id::SecondScreen));
    CHECK(drawn.contains(Id::AllowGameEditing));
    gba.second_screen_choices = kOff;
    gba.can_edit_game = false;
    const auto none = Drawn(Game(gba), SettingsView(SettingsSection::Console));
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
          "Fly, the PC, items and wild encounters edit your game as you play. "
          "This can corrupt your save. Your save is backed up first.");
    CHECK(std::string(A::kGameEditingConfirm) == "Turn On");
    CHECK(std::string(A::kGameEditingCancel) == "Cancel");
}
