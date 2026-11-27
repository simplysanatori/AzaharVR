// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame fixa: a per-game change from the in-game menu sticks (frame_xr/per_game_keys.h).
// citra_qt.cpp FrameMenuRequest sets the game's value, constructs the game's QtConfig (which reads
// the game's file back into the settings), puts the menu's values back (KeepMenuKeys) and saves.
// Here the file is read and written as QtConfig::ReadGlobalSetting and WriteGlobalSetting do for a
// game's file (the key's "use_global", and its value when the game has its own), with QSettings.

#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include "common/settings.h"
#include "frame_xr/menu/tab_menu.h"
#include "frame_xr/per_game.h"
#include "frame_xr/per_game_keys.h"
#include "frame_xr/presets.h"

namespace {

using FrameXr::Menu::Id;

template <typename T>
QVariant ToVariant(const T& value) {
    if constexpr (std::is_same_v<T, std::string>) {
        return QString::fromStdString(value);
    } else if constexpr (std::is_same_v<T, bool>) {
        return value;
    } else if constexpr (std::is_enum_v<T>) {
        return QVariant::fromValue(static_cast<qint64>(value));
    } else if constexpr (std::is_floating_point_v<T>) {
        return QVariant::fromValue(static_cast<double>(value));
    } else if constexpr (std::is_signed_v<T>) {
        return QVariant::fromValue(static_cast<qint64>(value));
    } else {
        return QVariant::fromValue(static_cast<quint64>(value));
    }
}

template <typename T>
T FromVariant(const QVariant& v) {
    if constexpr (std::is_same_v<T, std::string>) {
        return v.toString().toStdString();
    } else if constexpr (std::is_same_v<T, bool>) {
        return v.toBool();
    } else if constexpr (std::is_enum_v<T>) {
        return static_cast<T>(v.toLongLong());
    } else if constexpr (std::is_floating_point_v<T>) {
        return static_cast<T>(v.toDouble());
    } else if constexpr (std::is_signed_v<T>) {
        return static_cast<T>(v.toLongLong());
    } else {
        return static_cast<T>(v.toULongLong());
    }
}

/// QtConfig::ReadGlobalSetting for a game's file.
template <typename T, bool R>
void ReadGame(QSettings& ini, Settings::SwitchableSetting<T, R>& s) {
    const QString name = QString::fromStdString(s.GetLabel());
    const bool use_global = ini.value(name + QStringLiteral("/use_global"), true).toBool();
    s.SetGlobal(use_global);
    if (!use_global) {
        s.SetValue(FromVariant<T>(ini.value(name, ToVariant(s.GetDefault()))));
    }
}

/// QtConfig::WriteGlobalSetting for a game's file.
template <typename T, bool R>
void WriteGame(QSettings& ini, const Settings::SwitchableSetting<T, R>& s) {
    const QString name = QString::fromStdString(s.GetLabel());
    if (s.UsingGlobal()) {
        ini.remove(name);
        ini.setValue(name + QStringLiteral("/use_global"), true);
        return;
    }
    ini.setValue(name + QStringLiteral("/use_global"), false);
    ini.setValue(name, ToVariant(s.GetValue()));
}

void ReadAll(QSettings& ini) {
    FrameXr::PerGame::ForEachMenuKey([&ini](auto& s) { ReadGame(ini, s); });
}
void WriteAll(QSettings& ini) {
    FrameXr::PerGame::ForEachMenuKey([&ini](auto& s) { WriteGame(ini, s); });
}

/// A game value other than the global one (as the menu sets it: Per-Game scope), within the
/// setting's range. False when none was found.
template <typename T, bool R>
bool SetOtherGameValue(Settings::SwitchableSetting<T, R>& s) {
    const T global = s.GetValue(true);
    std::vector<T> tries;
    if constexpr (std::is_same_v<T, std::string>) {
        tries = {T("1,0"), T("2,1")};
    } else if constexpr (std::is_same_v<T, bool>) {
        tries = {!global};
    } else if constexpr (std::is_enum_v<T>) {
        using U = std::underlying_type_t<T>;
        tries = {static_cast<T>(static_cast<U>(global) + 1), static_cast<T>(static_cast<U>(global) - 1)};
    } else if constexpr (std::is_floating_point_v<T>) {
        tries = {global + T(0.5), global - T(0.5), global + T(5), global * T(2), T(1)};
    } else {
        tries = {static_cast<T>(global + 1), static_cast<T>(global - 1), T(1), T(2)};
    }
    s.SetGlobal(false);
    for (const T& t : tries) {
        s.SetValue(t);
        if (s.GetValue() != global) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> Labels() {
    std::vector<std::string> labels;
    FrameXr::PerGame::ForEachMenuKey([&labels](auto& s) { labels.push_back(s.GetLabel()); });
    return labels;
}

/// Every key follows the global value again when the test ends.
struct AllGlobal {
    ~AllGlobal() {
        FrameXr::PerGame::ForEachMenuKey([](auto& s) { s.SetGlobal(true); });
    }
};

} // namespace

TEST_CASE("Per-Game: every per-game row's key is kept across the read of the game's file",
          "[frame_xr][fixa]") {
    const std::vector<std::string> labels = Labels();
    // each console's games write their own group's keys (settings.h Graphics)
    for (const Settings::ConsoleGroup group :
         {Settings::ConsoleGroup::N3DS, Settings::ConsoleGroup::DS, Settings::ConsoleGroup::GBA}) {
        Settings::SetActiveConsole(group);
        for (int i = 0; i < FrameXr::Menu::kIdCount; i++) {
            const Id id = static_cast<Id>(i);
            if (group == Settings::ConsoleGroup::N3DS &&
                (id == Id::LayerDepth || id == Id::InvertDepth)) {
                continue; // a 3DS game has no such row
            }
            if (group != Settings::ConsoleGroup::DS &&
                (id == Id::DepthOn || id == Id::DepthFollows)) {
                continue; // the DS's alone
            }
            const std::vector<std::string> keys = FrameXr::PerGame::KeysOf(id);
            CHECK(keys.empty() == !FrameXr::Menu::PerGameSetting(id));
            for (const std::string& key : keys) {
                INFO("group " << static_cast<int>(group) << " row " << i << " key " << key);
                CHECK(std::find(labels.begin(), labels.end(), key) != labels.end());
            }
        }
    }
    Settings::SetActiveConsole(Settings::ConsoleGroup::N3DS);
    // The two that reverted in session 651, by name.
    // AzaharFrame bfidr: and Black Frame Insertion's helpers, written at its scope.
    for (const char* key : {"vr_motion_clarity_immersive", "vr_screens_in_projection",
                            "vr_dynamic_resolution", "vr_bfi_set_dynamic_resolution",
                            "vr_bfi_set_panel"}) {
        INFO(key);
        CHECK(std::find(labels.begin(), labels.end(), key) != labels.end());
    }
    // No key twice
    std::vector<std::string> sorted = labels;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
}

TEST_CASE("Per-Game: a value the menu writes sticks after the game's file is read again",
          "[frame_xr][fixa]") {
    AllGlobal reset;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("0004000000030800.ini"));
    std::size_t checked = 0;
    FrameXr::PerGame::ForEachMenuKey([&](auto& s) {
        using T = std::remove_cvref_t<decltype(s.GetValue())>;
        INFO("key " << s.GetLabel());
        // The game's file: every key follows the global value (nothing set for this game yet).
        QFile::remove(path);
        FrameXr::PerGame::ForEachMenuKey([](auto& o) { o.SetGlobal(true); });
        {
            QSettings ini(path, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("VR"));
            WriteAll(ini);
        }
        // The menu, Per-Game scope: the game's own value.
        REQUIRE(SetOtherGameValue(s));
        const T wanted = s.GetValue();
        // FrameMenuRequest: the game's QtConfig reads the file, the keeps put the menu's values
        // back, and the file is saved.
        {
            QSettings ini(path, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("VR"));
            {
                FrameXr::PerGame::KeepMenuKeys keep;
                ReadAll(ini);
            }
            CHECK(!s.UsingGlobal());
            CHECK(s.GetValue() == wanted);
            WriteAll(ini);
        }
        // The next boot reads the file afresh.
        FrameXr::PerGame::ForEachMenuKey([](auto& o) { o.SetGlobal(true); });
        {
            QSettings ini(path, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("VR"));
            ReadAll(ini);
        }
        CHECK(!s.UsingGlobal());
        CHECK(s.GetValue() == wanted);
        checked++;
    });
    CHECK(checked == Labels().size());
}

TEST_CASE("Per-Game: without the keep the read puts the file's value back (the session 651 bug)",
          "[frame_xr][fixa]") {
    AllGlobal reset;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("0004000000033500.ini"));
    auto& s = Settings::values.vr_screens_in_projection;
    s.SetGlobal(true);
    {
        QSettings ini(path, QSettings::IniFormat);
        ini.beginGroup(QStringLiteral("VR"));
        WriteGame(ini, s);
    }
    REQUIRE(SetOtherGameValue(s));
    QSettings ini(path, QSettings::IniFormat);
    ini.beginGroup(QStringLiteral("VR"));
    ReadGame(ini, s); // no keep: the file says "use the global value"
    CHECK(s.UsingGlobal());
}

// Mario Kart 7 in mode 3 went flat after a change made in the in-game menu: the preset's View
// Matrix Register (90) and Eye Indicator (87,2), set at boot and in no file, went back to Auto
// when the menu's save read the game's file, and mode 3 had no camera register to patch.
TEST_CASE("Per-Game: a preset's mode 3 registers survive a change made in the menu",
          "[frame_xr][fixa]") {
    AllGlobal reset;
    auto& v = Settings::values;
    auto& offset = v.vr_si_mode_register_offset;
    auto& eye = v.vr_immersive_eye_indicator;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("0004000000030800.ini"));
    // Boot: the game's file has nothing of its own, then the preset fills its values.
    {
        QSettings ini(path, QSettings::IniFormat);
        ini.beginGroup(QStringLiteral("VR"));
        offset.SetGlobal(true);
        eye.SetGlobal(true);
        WriteAll(ini);
        WriteGame(ini, offset);
        WriteGame(ini, eye);
        ReadAll(ini);
    }
    REQUIRE(offset.UsingGlobal());
    REQUIRE(offset.GetValue() == -1);
    REQUIRE(eye.GetValue().empty());
    FrameXr::ApplyPresetSettings(*FrameXr::FindImmersivePreset(0x0004000000030800ull));
    REQUIRE(offset.GetValue() == 90);
    REQUIRE(eye.GetValue() == "87,2");
    // Without the keep (1.2.1) the read of the file took both away.
    {
        QSettings ini(path, QSettings::IniFormat);
        ini.beginGroup(QStringLiteral("VR"));
        const auto kept_offset = offset.GetValue();
        const auto kept_eye = eye.GetValue();
        ReadGame(ini, offset);
        ReadGame(ini, eye);
        CHECK(offset.GetValue() == -1);
        CHECK(eye.GetValue().empty());
        offset.SetGlobal(false);
        offset.SetValue(kept_offset);
        eye.SetGlobal(false);
        eye.SetValue(kept_eye);
    }
    // The menu: Horizontal Scale for this game, then FrameMenuRequest's read and save.
    v.vr_immersive_scale_x.SetGlobal(false);
    v.vr_immersive_scale_x.SetValue(45);
    {
        QSettings ini(path, QSettings::IniFormat);
        ini.beginGroup(QStringLiteral("VR"));
        {
            FrameXr::PerGame::KeepMenuKeys keep;
            // QtConfig reads every key of the file, the two registers whatever the list holds.
            ReadAll(ini);
            ReadGame(ini, offset);
            ReadGame(ini, eye);
        }
        CHECK(offset.GetValue() == 90);
        CHECK(eye.GetValue() == "87,2");
        CHECK(v.vr_immersive_scale_x.GetValue() == 45);
        WriteAll(ini);
        WriteGame(ini, offset);
        WriteGame(ini, eye);
    }
    // The game's file has them now: the next boot reads them from it (before the preset).
    FrameXr::PerGame::ForEachMenuKey([](auto& s) { s.SetGlobal(true); });
    offset.SetGlobal(true);
    eye.SetGlobal(true);
    {
        QSettings ini(path, QSettings::IniFormat);
        ini.beginGroup(QStringLiteral("VR"));
        ReadAll(ini);
        ReadGame(ini, offset);
        ReadGame(ini, eye);
    }
    CHECK(!offset.UsingGlobal());
    CHECK(offset.GetValue() == 90);
    CHECK(eye.GetValue() == "87,2");
    offset.SetGlobal(true);
    eye.SetGlobal(true);
}

TEST_CASE("Per-Game: KeepMenuKeys puts the values back once, at Restore or at the end",
          "[frame_xr][fixa]") {
    AllGlobal reset;
    auto& s = Settings::values.vr_motion_clarity_immersive;
    s.SetGlobal(false);
    s.SetValue(true);
    {
        FrameXr::PerGame::KeepMenuKeys keep;
        s.SetGlobal(true);
        keep.Restore();
        CHECK(!s.UsingGlobal());
        CHECK(s.GetValue());
        s.SetGlobal(true); // after Restore: not put back again
    }
    CHECK(s.UsingGlobal());
}

namespace {

/// QtConfig::ReadBasicSetting in the global file: a value marked "\default=true" is read as the
/// build's own default.
template <typename T, bool R>
T ReadGlobalFile(QSettings& ini, const Settings::SwitchableSetting<T, R>& s) {
    const QString name = QString::fromStdString(s.GetLabel());
    if (ini.value(name + QStringLiteral("/default"), false).toBool()) {
        return s.GetDefault();
    }
    return FromVariant<T>(ini.value(name, ToVariant(s.GetDefault())));
}

/// QtConfig::WriteBasicSetting in the global file: the value, marked "\default" when it equals the
/// build's default.
template <typename T, bool R>
void WriteGlobalFile(QSettings& ini, const Settings::SwitchableSetting<T, R>& s, T value) {
    const QString name = QString::fromStdString(s.GetLabel());
    ini.setValue(name + QStringLiteral("/default"), value == s.GetDefault());
    ini.setValue(name, ToVariant(value));
}

} // namespace

// AzaharFrame fixa: Screen Shape starts on Automatic. The global file keeps an unchanged value as
// "\default=true", which each build reads as its own default, so 1.1.1 (3DS 5:3, 0 to 5 only)
// never sees Automatic from it; a game's own value is read as written, in every build.
TEST_CASE("Defaults: Screen Shape is Automatic, rollback-safe", "[fixa]") {
    auto& top = Settings::values.vr_top_aspect;
    CHECK(top.GetDefault() == 6u); // Automatic, the last choice
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings ini(dir.filePath(QStringLiteral("qt-config.ini")), QSettings::IniFormat);
    // A file an older build wrote with Screen Shape never changed (its 5:3 default): Automatic.
    ini.setValue(QStringLiteral("vr_top_aspect/default"), true);
    ini.setValue(QStringLiteral("vr_top_aspect"), 0);
    CHECK(ReadGlobalFile(ini, top) == 6u);
    // Automatic saved by this build is marked as the default: an older build reads its own.
    WriteGlobalFile(ini, top, 6u);
    CHECK(ini.value(QStringLiteral("vr_top_aspect/default")).toBool());
    // 3DS (5:3) picked in this build is saved as itself, and read back as itself.
    WriteGlobalFile(ini, top, 0u);
    CHECK_FALSE(ini.value(QStringLiteral("vr_top_aspect/default")).toBool());
    CHECK(ReadGlobalFile(ini, top) == 0u);
    // A game's own 5:3 (no "\default" in a game's file) stays 5:3.
    QSettings game(dir.filePath(QStringLiteral("game.ini")), QSettings::IniFormat);
    game.setValue(QStringLiteral("vr_top_aspect/use_global"), false);
    game.setValue(QStringLiteral("vr_top_aspect"), 0);
    const bool was_global = top.UsingGlobal();
    const u32 was = top.GetValue();
    ReadGame(game, top);
    CHECK_FALSE(top.UsingGlobal());
    CHECK(top.GetValue() == 0u);
    top.SetGlobal(was_global);
    if (!was_global) {
        top.SetValue(was);
    }
}

// AzaharFrame fixa: Internal Resolution starts at 4x (Auto followed Azahar's window: 1x in the
// headset). An unchanged global value ("\default=true") follows each build's default, so older
// builds read Auto; a value picked other than Auto is saved and read as itself.
TEST_CASE("Defaults: Internal Resolution is 4x, rollback-safe", "[fixa]") {
    auto& res = Settings::values.resolution_factor;
    CHECK(res.GetDefault() == 4u);
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings ini(dir.filePath(QStringLiteral("qt-config.ini")), QSettings::IniFormat);
    ini.setValue(QStringLiteral("resolution_factor/default"), true); // an older build's Auto
    ini.setValue(QStringLiteral("resolution_factor"), 0);
    CHECK(ReadGlobalFile(ini, res) == 4u);
    WriteGlobalFile(ini, res, 4u);
    CHECK(ini.value(QStringLiteral("resolution_factor/default")).toBool());
    WriteGlobalFile(ini, res, 3u);
    CHECK_FALSE(ini.value(QStringLiteral("resolution_factor/default")).toBool());
    CHECK(ReadGlobalFile(ini, res) == 3u);
    WriteGlobalFile(ini, res, 0u); // Auto picked in this build: saved as itself
    CHECK_FALSE(ini.value(QStringLiteral("resolution_factor/default")).toBool());
    CHECK(ReadGlobalFile(ini, res) == 0u);
}

// AzaharFrame kiufire: Trigger Layout is a per-game key (Original by default), kept like the
// others (the test above writes and reads it back with every key of ForEachMenuKey).
TEST_CASE("Per-Game: Trigger Layout's key, Original by default", "[frame_xr][kiufire]") {
    const std::vector<std::string> labels = Labels();
    CHECK(std::find(labels.begin(), labels.end(), "vr_kiu_trigger_layout") != labels.end());
    CHECK(FrameXr::PerGame::KeysOf(Id::TriggerLayout) ==
          std::vector<std::string>{"vr_kiu_trigger_layout"});
    CHECK(Settings::values.vr_kiu_trigger_layout.GetDefault() == 0u);
}

// AzaharVR: Screen Layout per game too: the same key, and a game's file without it (every file an
// older build wrote) follows Global.
TEST_CASE("Per-Game: Screen Layout's key", "[frame_xr][tabmenu]") {
    const AllGlobal restore;
    auto& layout = Settings::values.vr_screen_layout;
    CHECK(FrameXr::PerGame::KeysOf(Id::ScreenCurve) ==
          std::vector<std::string>{"vr_screen_curve"}); // AzaharVR: per game too
    CHECK(FrameXr::PerGame::KeysOf(Id::ScreenLayout) ==
          std::vector<std::string>{"vr_screen_layout"});
    CHECK(layout.GetDefault() == 0u);
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    layout.SetGlobal(true);
    const u32 before = layout.GetValue(true);
    for (const u32 global : {0u, 1u}) {
        layout.SetGlobal(true);
        layout.SetValue(global);
        QSettings old(dir.filePath(QStringLiteral("old%1.ini").arg(global)), QSettings::IniFormat);
        ReadGame(old, layout);
        CHECK(layout.UsingGlobal());
        CHECK(layout.GetValue() == global);
        // The game's own value, and back
        QSettings own(dir.filePath(QStringLiteral("own%1.ini").arg(global)), QSettings::IniFormat);
        layout.SetGlobal(false);
        layout.SetValue(1u - global);
        WriteGame(own, layout);
        layout.SetGlobal(true);
        ReadGame(own, layout);
        CHECK(layout.GetValue() == 1u - global);
        CHECK(layout.GetValue(true) == global);
        CHECK(FrameXr::PerGame::UseGlobalValue(Id::ScreenLayout));
        CHECK(layout.GetValue() == global);
        WriteGame(own, layout);
        CHECK_FALSE(own.contains(QStringLiteral("vr_screen_layout")));
    }
    layout.SetGlobal(true);
    layout.SetValue(before);
}

// AzaharVR: the rows that moved to another place of the menu write the keys they wrote before.
TEST_CASE("Per-Game: a moved row keeps its key", "[frame_xr][tabmenu]") {
    const std::vector<std::pair<Id, std::vector<std::string>>> kKeys{
        {Id::MotionControls, {"vr_motion_controls"}},
        {Id::ShowControllers, {"vr_show_controllers"}},
        {Id::ControllersInFront, {"vr_controllers_in_front"}},
        {Id::HideBottomScreen, {"vr_hide_bottom_screen"}},
        {Id::ScreenShape, {"vr_top_aspect"}},
        {Id::DsScreenRotation, {"ds_screen_rotation"}},
        {Id::GbaSolarLevel, {"gba_solar_level"}},
        {Id::SecondScreen, {"gba_second_screen"}},
        {Id::AllowGameEditing, {"gba_allow_editing"}},
        {Id::DsTouchEnabled, {"ds_touch_enabled"}},
        {Id::DsConsoleMode, {"ds_console_mode"}},
        {Id::MotionClarityNormal, {"vr_motion_clarity_normal"}},
        {Id::MatchBottomNormal, {"vr_motion_clarity_match_bottom"}},
        {Id::MotionClarity, {"vr_motion_clarity_immersive"}},
        {Id::MatchBottom, {"vr_motion_clarity_match_bottom"}},
        {Id::ScreenAsPanel, {"vr_screens_in_projection"}},
        {Id::FoveatedRendering, {"vr_foveated_rendering"}},
        {Id::HudSpacingX, {"vr_immersive_hud_spacing_x"}},
        {Id::HudSpacingY, {"vr_immersive_hud_spacing_y"}},
        {Id::GameFov, {"vr_immersive_game_fov"}},
        {Id::MatchCamera, {"vr_immersive_match_camera_mode"}},
        {Id::WorldScale, {"vr_immersive_world_scale"}},
        {Id::FollowScreen, {"vr_immersive_follow_screen"}},
        {Id::ScaleMode, {"vr_immersive_scale_mode"}},
        {Id::ScaleX, {"vr_immersive_scale_x"}},
        {Id::ScaleY, {"vr_immersive_scale_y"}},
        {Id::MarginX, {"vr_immersive_render_margin_x", "vr_immersive_wider_view_level"}},
        {Id::MarginY, {"vr_immersive_render_margin_y", "vr_immersive_wider_view_level"}},
    };
    for (const auto& [id, keys] : kKeys) {
        INFO(FrameXr::Menu::LabelFor(id));
        CHECK(FrameXr::PerGame::KeysOf(id) == keys);
    }
    // Global ones: no key of a game's file
    for (const Id id : {Id::FaceButtons, Id::MoveScreens, Id::Background}) {
        CHECK(FrameXr::PerGame::KeysOf(id).empty());
        CHECK_FALSE(FrameXr::Menu::PerGameSetting(id));
    }
}

// AzaharFrame kiumode: Angel's Aim (Immersive Mode 4) supplies Kid Icarus: Uprising's two
// registers in memory only (Settings::RunSuppliedSetting). A save during play (the in-game menu's
// FrameMenuRequest: the game's file read, the menu's keys put back, everything written with
// QtConfig::WriteGlobalSetting, which writes GetValue(false) for a game's file) stores mode 4
// and neither register, nor a mode 3.
namespace {
/// QtConfig::WriteGlobalSetting for a game's file, as it is (GetValue(global) with global false).
template <typename T, bool R>
void WriteGameAsQtConfig(QSettings& ini, const Settings::SwitchableSetting<T, R>& s) {
    const QString name = QString::fromStdString(s.GetLabel());
    if (s.UsingGlobal()) {
        ini.remove(name);
        ini.setValue(name + QStringLiteral("/use_global"), true);
        return;
    }
    ini.setValue(name + QStringLiteral("/use_global"), false);
    if constexpr (std::is_same_v<T, std::string>) {
        ini.setValue(name, QString::fromStdString(s.GetValue(false)));
    } else {
        ini.setValue(name, ToVariant(s.GetValue(false)));
    }
}
/// QtConfig::ReadGlobalSetting for a game's file, strings too.
template <typename T, bool R>
void ReadGameAny(QSettings& ini, Settings::SwitchableSetting<T, R>& s) {
    if constexpr (std::is_same_v<T, std::string>) {
        const QString name = QString::fromStdString(s.GetLabel());
        const bool use_global = ini.value(name + QStringLiteral("/use_global"), true).toBool();
        s.SetGlobal(use_global);
        if (!use_global) {
            s.SetValue(ini.value(name).toString().toStdString());
        }
    } else {
        ReadGame(ini, s);
    }
}
} // namespace

TEST_CASE("Per-Game: a save during Angel's Aim stores mode 4 and no registers",
          "[frame_xr][kiumode]") {
    AllGlobal reset;
    auto& v = Settings::values;
    auto& reg = v.vr_si_mode_register_offset;
    auto& eye = v.vr_immersive_eye_indicator;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("0004000000030200.ini"));
    static const int32_t kReg = 90;
    static const std::string kEye = "87,2";
    for (const bool own_register : {false, true}) {
        INFO("the game has its own View Matrix Register: " << own_register);
        QFile::remove(path);
        reg.SetGlobal(true);
        eye.SetGlobal(true);
        // The game's file before the run: Angel's Aim chosen, maybe a register of its own.
        {
            QSettings ini(path, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("VR"));
            v.vr_immersive_mode.SetGlobal(false);
            v.vr_immersive_mode.SetValue(4);
            WriteAll(ini);
            if (own_register) {
                reg.SetGlobal(false);
                reg.SetValue(12);
            }
            WriteGameAsQtConfig(ini, reg);
            WriteGameAsQtConfig(ini, eye);
        }
        // The run: first person supplies the registers.
        reg.Supply(&kReg);
        eye.Supply(&kEye);
        REQUIRE(reg.GetValue() == 90);
        REQUIRE(eye.GetValue() == "87,2");
        // A row changed in the in-game menu (Per-Game), then the save.
        v.vr_immersive_world_scale.SetGlobal(false);
        v.vr_immersive_world_scale.SetValue(150.0f);
        {
            QSettings ini(path, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("VR"));
            {
                FrameXr::PerGame::KeepMenuKeys keep;
                ReadAll(ini);
                ReadGameAny(ini, reg);
                ReadGameAny(ini, eye);
            }
            WriteAll(ini);
            WriteGameAsQtConfig(ini, reg);
            WriteGameAsQtConfig(ini, eye);
        }
        CHECK(reg.GetValue() == 90); // still supplied for the run
        {
            QSettings ini(path, QSettings::IniFormat);
            ini.beginGroup(QStringLiteral("VR"));
            CHECK(ini.value(QStringLiteral("vr_immersive_mode")).toUInt() == 4u);
            CHECK(!ini.value(QStringLiteral("vr_immersive_mode/use_global")).toBool());
            if (own_register) {
                CHECK(ini.value(QStringLiteral("vr_si_mode_register_offset")).toInt() == 12);
            } else {
                CHECK(ini.value(QStringLiteral("vr_si_mode_register_offset/use_global")).toBool());
                CHECK(!ini.contains(QStringLiteral("vr_si_mode_register_offset")));
            }
            CHECK(ini.value(QStringLiteral("vr_immersive_eye_indicator/use_global")).toBool());
            CHECK(!ini.contains(QStringLiteral("vr_immersive_eye_indicator")));
            for (const QString& key : ini.allKeys()) {
                INFO(key.toStdString());
                CHECK(ini.value(key).toString() != QStringLiteral("87,2"));
                if (!key.contains(QStringLiteral("use_global"))) {
                    CHECK(!(key.contains(QStringLiteral("register")) &&
                            ini.value(key).toInt() == 90));
                }
            }
        }
        reg.Supply(nullptr);
        eye.Supply(nullptr);
        reg.SetGlobal(true);
        eye.SetGlobal(true);
    }
}

// AzaharFrame kiuaim: the rows that are on the Angel's Aim page only now write the settings they wrote
// from the Controls and Settings tabs, per game and global, and the page's count and Use Global Values
// for All reach each.
TEST_CASE("Per-Game: the Angel's Aim page's own rows write the settings they always did",
          "[frame_xr][kiuaim]") {
    using namespace FrameXr::Menu;
    namespace PerGame = FrameXr::PerGame;
    AllGlobal reset;
    const std::vector<std::pair<Id, std::vector<std::string>>> rows{
        {Id::TriggerLayout, {"vr_kiu_trigger_layout"}},
        {Id::WeaponInHand, {"vr_light_gun_weapon"}},
        {Id::WeaponOpacity, {"vr_light_gun_weapon_opacity"}},
        {Id::WeaponSize, {"vr_light_gun_weapon_size"}},
        {Id::WeaponScale, {"vr_light_gun_weapon_scale"}},
        {Id::MuzzleFlash, {"vr_light_gun_flash"}},
        {Id::MuzzleFlashSize, {"vr_light_gun_muzzle_flash_size"}},
        {Id::Projectiles, {"vr_light_gun_projectile"}},
        {Id::ProjectileOpacity, {"vr_light_gun_projectile_opacity"}},
        {Id::ProjectileSize, {"vr_light_gun_projectile_size"}},
        {Id::ProjectileSpeed, {"vr_light_gun_projectile_speed"}},
        {Id::TurnZoneFrom, {"vr_light_gun_turn_zone_from"}},
        {Id::TurnZone, {"vr_light_gun_turn_zone"}},
        {Id::TurnSpeed, {"vr_light_gun_turn_speed"}},
        {Id::TurnSpeedVertical, {"vr_light_gun_turn_speed_vertical"}},
        {Id::KiuBottomScreen, {"vr_kiu_bottom_screen"}},
        {Id::KiuBottomSize, {"vr_kiu_bottom_size"}},
        {Id::KiuBottomOpacity, {"vr_kiu_bottom_opacity"}},
        {Id::KiuOverShoulder, {"vr_kiu_over_shoulder", "vr_kiu_game_camera"}},
        {Id::KiuPitOpacity, {"vr_kiu_pit_opacity"}},
        {Id::KiuSnapTurn, {"vr_kiu_snap_turn"}},
        {Id::KiuWristAngle, {"vr_kiu_wrist_angle"}},
        {Id::KiuWristAlong, {"vr_kiu_wrist_along"}},
        {Id::KiuCutscenes, {"vr_kiu_cutscenes"}},
        {Id::KiuHud, {"vr_kiu_hud"}},
        {Id::KiuHudSize, {"vr_kiu_hud_size"}},
        {Id::KiuHudOpacity, {"vr_kiu_hud_opacity"}},
        {Id::KiuHudLinked, {"vr_kiu_hud_linked"}},
        {Id::KiuHudAcross, {"vr_kiu_hud_across"}},
        {Id::KiuHudHeight, {"vr_kiu_hud_height"}},
        {Id::KiuBarAcross, {"vr_kiu_bar_across"}},
        {Id::KiuBarHeight, {"vr_kiu_bar_height"}},
        {Id::KiuScoreAcross, {"vr_kiu_score_across"}},
        {Id::KiuScoreHeight, {"vr_kiu_score_height"}},
        {Id::KiuSubtitleHeight, {"vr_kiu_subtitle_height"}},
        {Id::KiuSubtitleSize, {"vr_kiu_subtitle_size"}},
        {Id::KiuPowerMenu, {"vr_kiu_power_menu"}},
        {Id::KiuPowerMenuAcross, {"vr_kiu_power_menu_across"}},
        {Id::KiuPowerMenuHeight, {"vr_kiu_power_menu_height"}},
        {Id::KiuPowerMenuOpacity, {"vr_kiu_power_menu_opacity"}},
        {Id::KiuStickClick, {"vr_kiu_stick_click"}},
        {Id::Kiu60Fps, {"vr_kiu_60fps"}}, // AzaharFrame kiu60
        {Id::KiuHideOffHand, {"vr_kiu_hide_off_hand"}},
        {Id::KiuTalkPortraits, {"vr_kiu_talk_portraits"}},
        {Id::KiuTalkPortraitsOpacity, {"vr_kiu_talk_portraits_opacity"}},
        {Id::KiuPortraitsLinked, {"vr_kiu_portraits_linked"}},
        {Id::KiuPortraitsAcross, {"vr_kiu_portraits_across"}},
        {Id::KiuPortraitsHeight, {"vr_kiu_portraits_height"}},
        {Id::KiuReticleSize, {"vr_kiu_reticle_size"}}};
    const auto has = [](const std::vector<Id>& ids, Id id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    // The states that show every row between them.
    std::vector<MenuModel> models;
    for (const int bits : {0, 1, 2, 3}) {
        MenuModel m;
        m.scope = 1;
        m.mods.angels_aim.shown = m.mods.angels_aim.mapped = true;
        m.immersive_mode = 4;
        m.gyro_aim = 2;
        m.recoil = m.weapon_in_hand = m.muzzle_flash = m.projectiles = true;
        m.kiu_bottom_screen = 1 + (bits & 1);
        m.kiu_view = 1;
        m.kiu_hud = 1;
        m.kiu_hud_linked = bits >> 1;
        m.kiu_power_menu = 2;
        m.kiu_talk_portraits = 1;
        m.kiu_portraits_linked = bits & 1;
        models.push_back(m);
    }
    // All of the page's own are in the table, and nothing else is.
    for (const MenuModel& m : models) {
        std::vector<Id> own = AngelsAimFirstPersonRows(m);
        for (const Id id : AimingRows(m)) {
            if (KiuOnlyControlsRow(id)) {
                own.push_back(id);
            }
        }
        for (const Id id : own) {
            INFO(LabelFor(id));
            CHECK(PerGameSetting(id) == std::any_of(rows.begin(), rows.end(), [&](const auto& r) {
                      return r.first == id;
                  }));
        }
    }
    for (const auto& [id, keys] : rows) {
        INFO(LabelFor(id));
        CHECK(PerGame::KeysOf(id) == keys);
        const auto each = [&keys = keys](const auto& f) {
            PerGame::ForEachMenuKey([&](auto& s) {
                if (std::find(keys.begin(), keys.end(), s.GetLabel()) != keys.end()) {
                    f(s);
                }
            });
        };
        // Per-Game: the game's own value; the page counts it, the general tabs do not.
        PerGame::ForEachMenuKey([](auto& s) { s.SetGlobal(true); });
        each([](auto& s) {
            s.SetGlobal(false);
            SetOtherGameValue(s);
        });
        bool counted = false;
        for (MenuModel m : models) {
            PerGame::FillGameValues(m);
            CHECK(GameValueRows(m, Tab::Controls).empty());
            CHECK(GameValueRows(m, Tab::Settings, SettingsSection::Immersive).empty());
            const auto page = GameValueRows(m, Tab::Mods);
            CHECK((page.empty() || page == std::vector<Id>{id}));
            counted |= has(page, id);
        }
        CHECK(counted);
        CHECK_FALSE(PerGame::UsesGlobal(id)); // in Global: locked
        // Use Global Values for All.
        CHECK(PerGame::UseGlobalValue(id));
        each([](auto& s) { CHECK(s.UsingGlobal()); });
        for (MenuModel m : models) {
            PerGame::FillGameValues(m);
            CHECK(GameValueRows(m, Tab::Mods).empty());
        }
    }
}

// AzaharFrame kiutoggles: Angel's Aim's first-person options are per-game keys, off (Smooth) by
// default as their test switches were, kept like the others.
TEST_CASE("Per-Game: Angel's Aim's first-person keys, off by default", "[frame_xr][kiutoggles]") {
    const std::vector<std::string> labels = Labels();
    // AzaharFrame kiu131: Bottom Screen, its size and opacity, Reticle Size.
    const std::vector<std::pair<Id, const char*>> rows{{Id::KiuBottomScreen, "vr_kiu_bottom_screen"},
                                                       {Id::KiuBottomSize, "vr_kiu_bottom_size"},
                                                       {Id::KiuBottomOpacity, "vr_kiu_bottom_opacity"},
                                                       {Id::KiuReticleSize, "vr_kiu_reticle_size"},
                                                       {Id::KiuPitOpacity, "vr_kiu_pit_opacity"},
                                                       {Id::KiuHideOffHand, "vr_kiu_hide_off_hand"},
                                                       {Id::Kiu60Fps, "vr_kiu_60fps"}, // AzaharFrame kiu60
                                                       {Id::KiuSnapTurn, "vr_kiu_snap_turn"}};
    for (const auto& [id, key] : rows) {
        INFO(key);
        CHECK(std::find(labels.begin(), labels.end(), key) != labels.end());
        CHECK(FrameXr::PerGame::KeysOf(id) == std::vector<std::string>{key});
    }
    // AzaharFrame kiu133: Camera is two keys: Third Person's, as before, and the Game's
    // Camera's.
    CHECK(FrameXr::PerGame::KeysOf(Id::KiuOverShoulder) ==
          std::vector<std::string>{"vr_kiu_over_shoulder", "vr_kiu_game_camera"});
    CHECK(std::find(labels.begin(), labels.end(), "vr_kiu_game_camera") != labels.end());
    CHECK_FALSE(Settings::values.vr_kiu_game_camera.GetDefault());
    CHECK_FALSE(Settings::values.vr_kiu_hide_off_hand.GetDefault());
    CHECK_FALSE(Settings::values.vr_kiu_60fps.GetDefault()); // AzaharFrame kiu60
    CHECK(Settings::values.vr_kiu_pit_opacity.GetDefault() == 100u);
    CHECK_FALSE(Settings::values.vr_kiu_wrist_screen.GetDefault());
    CHECK_FALSE(Settings::values.vr_kiu_talk_panel.GetDefault());
    CHECK_FALSE(Settings::values.vr_kiu_over_shoulder.GetDefault());
    CHECK(Settings::values.vr_kiu_snap_turn.GetDefault() == 0u);
    CHECK(Settings::values.vr_kiu_bottom_screen.GetDefault() == 2u); // Wrist
    CHECK(Settings::values.vr_kiu_bottom_size.GetDefault() == 100u);
    CHECK(Settings::values.vr_kiu_bottom_opacity.GetDefault() == 100u);
    // AzaharFrame kiu1s: the talk's portraits, off, and their opacity.
    CHECK_FALSE(Settings::values.vr_kiu_talk_portraits.GetDefault());
    CHECK(Settings::values.vr_kiu_talk_portraits_opacity.GetDefault() == 100u);
    CHECK(Settings::values.vr_kiu_portraits_linked.GetDefault());
    CHECK(Settings::values.vr_kiu_portraits_across.GetDefault() == 0);
    CHECK(Settings::values.vr_kiu_portraits_height.GetDefault() == 0);
    // AzaharFrame kiupowers: the power menu, off, and its opacity.
    CHECK(Settings::values.vr_kiu_power_menu.GetDefault() == 0u);
    CHECK(Settings::values.vr_kiu_power_menu_opacity.GetDefault() == 100u);
    CHECK(Settings::values.vr_kiu_power_menu_across.GetDefault() == 0);
    CHECK(Settings::values.vr_kiu_power_menu_height.GetDefault() == 0);
    for (const char* key : {"vr_kiu_talk_portraits", "vr_kiu_talk_portraits_opacity",
                            "vr_kiu_portraits_linked", "vr_kiu_portraits_across",
                            "vr_kiu_portraits_height",
                            "vr_kiu_power_menu", "vr_kiu_power_menu_opacity",
                            "vr_kiu_power_menu_across", "vr_kiu_power_menu_height"}) {
        INFO(key);
        CHECK(std::find(labels.begin(), labels.end(), key) != labels.end());
    }
    CHECK(Settings::values.vr_kiu_stick_click.GetDefault() == 0u); // AzaharFrame kiucam
    CHECK(std::find(labels.begin(), labels.end(), "vr_kiu_stick_click") != labels.end());
    CHECK(Settings::values.vr_wrist_side.GetDefault() == 0u); // Back
    CHECK(Settings::values.vr_kiu_reticle_size.GetDefault() == 100u);
    for (const char* key : {"vr_kiu_bottom_x", "vr_kiu_bottom_y", "vr_kiu_wrist_screen",
                            "vr_kiu_talk_panel"}) {
        INFO(key);
        CHECK(std::find(labels.begin(), labels.end(), key) != labels.end());
    }
}

// AzaharVR: the DS and GBA rows (Console Mode, Touch Screen Input, Screen Rotation, Solar Sensor
// Level) are settings with a global value and a game's own, in the file named by the game's key.
TEST_CASE("Per-Game: the DS and GBA rows' values in two games' files", "[frame_xr][altcores]") {
    AllGlobal reset;
    auto& v = Settings::values;
    CHECK(v.ds_console_mode.GetDefault() == 0u); // Automatic
    CHECK(v.ds_touch_enabled.GetDefault());
    CHECK(v.ds_screen_rotation.GetDefault() == 0u);
    CHECK(v.gba_solar_level.GetDefault() == 6u);
    CHECK(v.gba_second_screen.GetDefault() == 0u); // PokeDaisy
    CHECK(!v.gba_allow_editing.GetDefault());
    CHECK(v.layer_depth.GetDefault() == 0u);
    // a GBA or Game Boy game's own key (Settings::Graphics), a DS game's another
    Settings::SetActiveConsole(Settings::ConsoleGroup::GBA);
    CHECK(FrameXr::PerGame::KeysOf(Id::LayerDepth) == std::vector<std::string>{"layer_depth"});
    CHECK(!v.invert_depth.GetDefault());
    CHECK(FrameXr::PerGame::KeysOf(Id::InvertDepth) == std::vector<std::string>{"invert_depth"});
    Settings::SetActiveConsole(Settings::ConsoleGroup::DS);
    CHECK(FrameXr::PerGame::KeysOf(Id::LayerDepth) == std::vector<std::string>{"ds_layer_depth"});
    CHECK(FrameXr::PerGame::KeysOf(Id::InvertDepth) ==
          std::vector<std::string>{"ds_invert_depth"});
    CHECK(v.ds_depth_on.GetDefault() == 0u); // both screens
    CHECK(FrameXr::PerGame::KeysOf(Id::DepthOn) == std::vector<std::string>{"ds_depth_on"});
    CHECK_FALSE(v.ds_depth_by_slot.GetDefault()); // the DS's screens
    CHECK(FrameXr::PerGame::KeysOf(Id::DepthFollows) ==
          std::vector<std::string>{"ds_depth_by_slot"});
    Settings::SetActiveConsole(Settings::ConsoleGroup::N3DS);
    CHECK(FrameXr::PerGame::KeysOf(Id::SecondScreen) ==
          std::vector<std::string>{"gba_second_screen"});
    CHECK(FrameXr::PerGame::KeysOf(Id::AllowGameEditing) ==
          std::vector<std::string>{"gba_allow_editing"});
    CHECK(!v.gba_rotate_pages.GetDefault());
    CHECK(v.gba_map_refresh.GetDefault() == 1u); // every second
    CHECK(FrameXr::PerGame::KeysOf(Id::RotatePages) ==
          std::vector<std::string>{"gba_rotate_pages"});
    CHECK(FrameXr::PerGame::KeysOf(Id::MapRefresh) ==
          std::vector<std::string>{"gba_map_refresh"});
    CHECK(FrameXr::PerGame::KeysOf(Id::DsConsoleMode) ==
          std::vector<std::string>{"ds_console_mode"});
    CHECK(FrameXr::PerGame::KeysOf(Id::DsTouchEnabled) ==
          std::vector<std::string>{"ds_touch_enabled"});
    CHECK(FrameXr::PerGame::KeysOf(Id::DsScreenRotation) ==
          std::vector<std::string>{"ds_screen_rotation"});
    CHECK(FrameXr::PerGame::KeysOf(Id::GbaSolarLevel) ==
          std::vector<std::string>{"gba_solar_level"});

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString first = dir.filePath(QStringLiteral("D54D534501234567.ini"));
    const QString second = dir.filePath(QStringLiteral("BA55334989ABCDEF.ini"));
    const auto write = [](const QString& path) {
        QSettings ini(path, QSettings::IniFormat);
        ini.beginGroup(QStringLiteral("VR"));
        WriteAll(ini);
    };
    const auto boot = [](const QString& path) { // after the last game's values went
        FrameXr::PerGame::ForEachMenuKey([](auto& s) { s.SetGlobal(true); });
        QSettings ini(path, QSettings::IniFormat);
        ini.beginGroup(QStringLiteral("VR"));
        ReadAll(ini);
    };
    const auto own = [](auto& s, auto value) { // the menu, Per-Game
        s.SetGlobal(false);
        s.SetValue(value);
    };
    own(v.ds_console_mode, 1u);
    own(v.ds_touch_enabled, false);
    own(v.ds_screen_rotation, 2u);
    own(v.gba_second_screen, 2u);
    own(v.gba_allow_editing, true);
    own(v.gba_rotate_pages, true);
    own(v.gba_map_refresh, 3u);
    own(v.layer_depth, 2u);
    own(v.invert_depth, true);
    write(first);
    FrameXr::PerGame::ForEachMenuKey([](auto& s) { s.SetGlobal(true); });
    own(v.gba_solar_level, 10u);
    write(second);

    boot(first);
    CHECK(v.ds_console_mode.GetValue() == 1u);
    CHECK(!v.ds_touch_enabled.GetValue());
    CHECK(v.ds_screen_rotation.GetValue() == 2u);
    CHECK(v.gba_solar_level.UsingGlobal());
    CHECK(v.gba_solar_level.GetValue() == 6u);
    CHECK(v.gba_second_screen.GetValue() == 2u);
    CHECK(v.gba_allow_editing.GetValue());
    CHECK(v.gba_rotate_pages.GetValue());
    CHECK(v.gba_map_refresh.GetValue() == 3u);
    CHECK(v.layer_depth.GetValue() == 2u);
    CHECK(v.layer_depth.GetValue(true) == 0u);
    CHECK(v.invert_depth.GetValue());
    CHECK(!v.invert_depth.GetValue(true));
    CHECK(v.gba_second_screen.GetValue(true) == 0u); // the global values are untouched
    CHECK(!v.gba_allow_editing.GetValue(true));
    CHECK(v.ds_console_mode.GetValue(true) == 0u); // the global values are untouched
    CHECK(v.ds_touch_enabled.GetValue(true));
    CHECK(v.ds_screen_rotation.GetValue(true) == 0u);

    boot(second);
    CHECK(v.ds_console_mode.UsingGlobal());
    CHECK(v.ds_touch_enabled.GetValue());
    CHECK(v.ds_screen_rotation.GetValue() == 0u);
    CHECK(v.gba_solar_level.GetValue() == 10u);
    CHECK(v.gba_second_screen.UsingGlobal());
    CHECK(!v.gba_allow_editing.GetValue());
    CHECK(v.layer_depth.UsingGlobal());
    CHECK(v.invert_depth.UsingGlobal());
    CHECK(!v.invert_depth.GetValue());

    // Use Global Value, then the save and the next start
    boot(first);
    CHECK(FrameXr::PerGame::UseGlobalValue(Id::DsScreenRotation));
    CHECK(v.ds_screen_rotation.GetValue() == 0u);
    write(first);
    boot(first);
    CHECK(v.ds_screen_rotation.UsingGlobal());
    CHECK(v.ds_console_mode.GetValue() == 1u);
    CHECK(!v.ds_touch_enabled.GetValue());
}

// The game stopped: the next one (a 3DS game too) has none of its values.
TEST_CASE("Per-Game: a stopped game's DS and GBA values go", "[frame_xr][altcores]") {
    AllGlobal reset;
    auto& v = Settings::values;
    const auto own = [](auto& s, auto value) {
        s.SetGlobal(false);
        s.SetValue(value);
    };
    own(v.ds_console_mode, 2u);
    own(v.ds_touch_enabled, false);
    own(v.ds_screen_rotation, 1u);
    own(v.gba_solar_level, 0u);
    own(v.gba_second_screen, 1u);
    own(v.gba_allow_editing, true);
    own(v.gba_rotate_pages, true);
    own(v.gba_map_refresh, 0u);
    own(v.layer_depth, 3u);
    Settings::RestoreGlobalState(false);
    CHECK(v.ds_console_mode.GetValue() == 0u);
    CHECK(v.ds_touch_enabled.GetValue());
    CHECK(v.ds_screen_rotation.GetValue() == 0u);
    CHECK(v.gba_solar_level.GetValue() == 6u);
    CHECK(v.gba_second_screen.GetValue() == 0u);
    CHECK(!v.gba_allow_editing.GetValue());
    CHECK(!v.gba_rotate_pages.GetValue());
    CHECK(v.gba_map_refresh.GetValue() == 1u);
    CHECK(v.layer_depth.GetValue() == 0u);
}
