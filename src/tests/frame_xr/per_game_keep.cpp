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
    for (int i = 0; i < FrameXr::Menu::kIdCount; i++) {
        const Id id = static_cast<Id>(i);
        const std::vector<std::string> keys = FrameXr::PerGame::KeysOf(id);
        CHECK(keys.empty() == !FrameXr::Menu::PerGameSetting(id));
        for (const std::string& key : keys) {
            INFO("row " << i << " key " << key);
            CHECK(std::find(labels.begin(), labels.end(), key) != labels.end());
        }
    }
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
                                                       {Id::KiuSnapTurn, "vr_kiu_snap_turn"}};
    for (const auto& [id, key] : rows) {
        INFO(key);
        CHECK(std::find(labels.begin(), labels.end(), key) != labels.end());
        CHECK(FrameXr::PerGame::KeysOf(id) == std::vector<std::string>{key});
    }
    // AzaharFrame kiu133: Camera is two keys: Over the Shoulder's, as before, and the Game's
    // Camera's.
    CHECK(FrameXr::PerGame::KeysOf(Id::KiuOverShoulder) ==
          std::vector<std::string>{"vr_kiu_over_shoulder", "vr_kiu_game_camera"});
    CHECK(std::find(labels.begin(), labels.end(), "vr_kiu_game_camera") != labels.end());
    CHECK_FALSE(Settings::values.vr_kiu_game_camera.GetDefault());
    CHECK(Settings::values.vr_kiu_pit_opacity.GetDefault() == 100u);
    CHECK_FALSE(Settings::values.vr_kiu_wrist_screen.GetDefault());
    CHECK_FALSE(Settings::values.vr_kiu_talk_panel.GetDefault());
    CHECK_FALSE(Settings::values.vr_kiu_over_shoulder.GetDefault());
    CHECK(Settings::values.vr_kiu_snap_turn.GetDefault() == 0u);
    CHECK(Settings::values.vr_kiu_bottom_screen.GetDefault() == 0u);
    CHECK(Settings::values.vr_kiu_bottom_size.GetDefault() == 100u);
    CHECK(Settings::values.vr_kiu_bottom_opacity.GetDefault() == 100u);
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
    CHECK(FrameXr::PerGame::KeysOf(Id::LayerDepth) == std::vector<std::string>{"layer_depth"});
    CHECK(FrameXr::PerGame::KeysOf(Id::SecondScreen) ==
          std::vector<std::string>{"gba_second_screen"});
    CHECK(FrameXr::PerGame::KeysOf(Id::AllowGameEditing) ==
          std::vector<std::string>{"gba_allow_editing"});
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
    own(v.layer_depth, 2u);
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
    CHECK(v.layer_depth.GetValue() == 2u);
    CHECK(v.layer_depth.GetValue(true) == 0u);
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
    own(v.layer_depth, 3u);
    Settings::RestoreGlobalState(false);
    CHECK(v.ds_console_mode.GetValue() == 0u);
    CHECK(v.ds_touch_enabled.GetValue());
    CHECK(v.ds_screen_rotation.GetValue() == 0u);
    CHECK(v.gba_solar_level.GetValue() == 6u);
    CHECK(v.gba_second_screen.GetValue() == 0u);
    CHECK(!v.gba_allow_editing.GetValue());
    CHECK(v.layer_depth.GetValue() == 0u);
}
