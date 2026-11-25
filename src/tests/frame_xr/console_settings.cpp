// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Each console group's own Internal Resolution, Texture Filter, Layer Depth and Invert Depth
// (settings.h Graphics): one group's value never moves another's, saved and read back by the real
// QtConfig, Global and Per-Game; the 3DS's keys are as they were.

#include <array>
#include <string>
#include <QFile>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include "citra_qt/configuration/config.h"
#include "common/alt_core_types.h"
#include "common/file_util.h"
#include "common/settings.h"

namespace {

using Common::AltCore::CoreKind;
using Settings::ConsoleGroup;
using Settings::TextureFilter;

constexpr std::array<ConsoleGroup, 3> kGroups{ConsoleGroup::N3DS, ConsoleGroup::DS,
                                              ConsoleGroup::GBA};

/// Every group's settings, as plain values (the 3DS has no Layer Depth: its two stay as they are).
struct Group {
    u32 resolution{};
    TextureFilter filter{};
    u32 depth{};
    bool invert{};
    u32 layout{};
    u32 gap{};
    bool hide{};
    u32 curve{};
    bool operator==(const Group&) const = default;
};
using Snapshot = std::array<Group, 3>;

Snapshot Take() {
    Snapshot s;
    for (std::size_t i = 0; i < kGroups.size(); i++) {
        const auto g = Settings::Graphics(kGroups[i]);
        s[i] = {g.resolution_factor.GetValue(), g.texture_filter.GetValue(),
                g.layer_depth.GetValue(),       g.invert_depth.GetValue(),
                g.screen_layout.GetValue(),     g.screen_gap.GetValue(),
                g.hide_bottom_screen.GetValue(), g.screen_curve.GetValue()};
    }
    return s;
}

/// Every group's own value, not the default, not another group's.
void SetOwn(ConsoleGroup group, bool per_game, u32 n) {
    const auto g = Settings::Graphics(group);
    const auto set = [per_game](auto& setting, auto value) {
        if (per_game) {
            setting.SetGlobal(false);
        }
        setting.SetValue(value);
    };
    set(g.resolution_factor, 5 + n);
    set(g.texture_filter, n == 0 ? TextureFilter::xBRZ : TextureFilter::MMPX);
    set(g.screen_layout, 1u);
    set(g.screen_gap, 3 + n);
    set(g.hide_bottom_screen, true);
    if (group != ConsoleGroup::N3DS) {
        set(g.layer_depth, 1 + n);
        set(g.invert_depth, true);
    }
    set(g.screen_curve, 30 + 15 * n);
}

/// Every group back to its defaults, global.
void Reset() {
    Settings::SetActiveConsole(ConsoleGroup::N3DS);
    for (const ConsoleGroup group : kGroups) {
        const auto g = Settings::Graphics(group);
        const auto reset = [](auto& setting) {
            setting.SetGlobal(true);
            setting.SetValue(setting.GetDefault());
        };
        reset(g.resolution_factor);
        reset(g.texture_filter);
        reset(g.layer_depth);
        reset(g.invert_depth);
        reset(g.screen_layout);
        reset(g.screen_gap);
        reset(g.hide_bottom_screen);
        reset(g.screen_curve);
    }
}

QString Text(const std::string& path) {
    QFile f(QString::fromStdString(path));
    REQUIRE(f.open(QIODevice::ReadOnly));
    return QString::fromUtf8(f.readAll());
}

/// The line `key=...` of a file, "" when it has none.
QString Line(const QString& text, const QString& key) {
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        if (line.startsWith(key + QLatin1Char('='))) {
            return line.trimmed();
        }
    }
    return {};
}

} // namespace

TEST_CASE("Console settings: the groups are told apart", "[frame_xr][consolesettings]") {
    CHECK(Common::AltCore::GroupOf(CoreKind::N3DS) == ConsoleGroup::N3DS);
    CHECK(Common::AltCore::GroupOf(CoreKind::NDS) == ConsoleGroup::DS);
    CHECK(Common::AltCore::GroupOf(CoreKind::DSi) == ConsoleGroup::DS);
    CHECK(Common::AltCore::GroupOf(CoreKind::GBA) == ConsoleGroup::GBA);
    CHECK(Common::AltCore::GroupOf(CoreKind::GB) == ConsoleGroup::GBA); // Game Boy Color too
    // The 3DS's are the settings it always had
    CHECK(&Settings::Graphics(ConsoleGroup::N3DS).resolution_factor ==
          &Settings::values.resolution_factor);
    CHECK(&Settings::Graphics(ConsoleGroup::N3DS).texture_filter ==
          &Settings::values.texture_filter);
    // and no two groups have one in common
    for (const ConsoleGroup a : kGroups) {
        for (const ConsoleGroup b : kGroups) {
            if (a == b) {
                continue;
            }
            CHECK(&Settings::Graphics(a).resolution_factor !=
                  &Settings::Graphics(b).resolution_factor);
            CHECK(&Settings::Graphics(a).texture_filter != &Settings::Graphics(b).texture_filter);
            CHECK(&Settings::Graphics(a).layer_depth != &Settings::Graphics(b).layer_depth);
            CHECK(&Settings::Graphics(a).invert_depth != &Settings::Graphics(b).invert_depth);
        }
    }
    Settings::SetActiveConsole(ConsoleGroup::GBA);
    CHECK(&Settings::RunningGraphics().resolution_factor ==
          &Settings::values.gba_resolution_factor);
    Settings::SetActiveConsole(ConsoleGroup::N3DS);
    CHECK(&Settings::RunningGraphics().resolution_factor == &Settings::values.resolution_factor);
}

TEST_CASE("Console settings: one group's value does not move another's, through QtConfig",
          "[frame_xr][consolesettings]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    FileUtil::SetUserPath(dir.path().toStdString() + "/");
    for (const ConsoleGroup changed : kGroups) {
        INFO("changed group " << static_cast<int>(changed));
        QFile::remove(QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) +
                                             "/qt-config.ini"));
        Reset();
        const Snapshot before = Take();
        {
            QtConfig global("qt-config", QtConfig::ConfigType::GlobalConfig); // reads the file
            SetOwn(changed, false, 1);
            global.Save();
        }
        const Snapshot set = Take();
        // Read back by a fresh QtConfig after everything went elsewhere
        Reset();
        { QtConfig global("qt-config", QtConfig::ConfigType::GlobalConfig); }
        CHECK(Take() == set);
        // only the changed group differs from what it was
        for (const ConsoleGroup other : kGroups) {
            if (other != changed) {
                const std::size_t i = static_cast<std::size_t>(other);
                CHECK(set[i] == before[i]);
            }
        }
        const Group& mine = set[static_cast<std::size_t>(changed)];
        CHECK(mine.resolution == 6);
        CHECK(mine.filter == TextureFilter::MMPX);
        CHECK(mine.layout == 1);
        CHECK(mine.gap == 4);
        CHECK(mine.hide);
        CHECK(mine.curve == 45);
        if (changed != ConsoleGroup::N3DS) {
            CHECK(mine.depth == 2);
            CHECK(mine.invert);
        }
    }
}

TEST_CASE("Console settings: a game's own value is its group's alone, through QtConfig",
          "[frame_xr][consolesettings]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    FileUtil::SetUserPath(dir.path().toStdString() + "/");
    const std::string game = "0004000000099999";
    const QString game_file = QString::fromStdString(
        FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "/custom/" + game + ".ini");
    for (const ConsoleGroup changed : kGroups) {
        INFO("changed group " << static_cast<int>(changed));
        Reset();
        { QtConfig global("qt-config", QtConfig::ConfigType::GlobalConfig); }
        const Snapshot global_values = Take();
        {
            QtConfig per_game(game, QtConfig::ConfigType::PerGameConfig);
            SetOwn(changed, true, 0);
            per_game.Save();
        }
        // the game's file is read again by a fresh run
        Reset();
        { QtConfig global("qt-config", QtConfig::ConfigType::GlobalConfig); }
        { QtConfig per_game(game, QtConfig::ConfigType::PerGameConfig); }
        for (const ConsoleGroup group : kGroups) {
            const auto g = Settings::Graphics(group);
            const bool mine = group == changed;
            INFO("group " << static_cast<int>(group));
            CHECK(g.resolution_factor.UsingGlobal() == !mine);
            CHECK(g.texture_filter.UsingGlobal() == !mine);
            CHECK(g.screen_layout.UsingGlobal() == !mine);
            CHECK(g.screen_gap.UsingGlobal() == !mine);
            CHECK(g.hide_bottom_screen.UsingGlobal() == !mine);
            if (group != ConsoleGroup::N3DS) {
                CHECK(g.layer_depth.UsingGlobal() == !mine);
                CHECK(g.invert_depth.UsingGlobal() == !mine);
            }
            CHECK(g.screen_curve.UsingGlobal() == !mine);
        }
        const Snapshot read = Take();
        const Group& mine = read[static_cast<std::size_t>(changed)];
        CHECK(mine.resolution == 5);
        CHECK(mine.filter == TextureFilter::xBRZ);
        CHECK(mine.layout == 1);
        CHECK(mine.gap == 3);
        CHECK(mine.hide);
        for (const ConsoleGroup other : kGroups) {
            if (other != changed) {
                const std::size_t i = static_cast<std::size_t>(other);
                CHECK(read[i] == global_values[i]);
            }
        }
        CHECK(mine.curve == 30);
        // the next group's loop starts without this game's file
        QFile::remove(game_file);
    }
}

TEST_CASE("Console settings: the 3DS's keys and values are as they were",
          "[frame_xr][consolesettings]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    FileUtil::SetUserPath(dir.path().toStdString() + "/");
    const QString config = QString::fromStdString(
        FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "/qt-config.ini");
    // 3DS values, and nothing else changed
    Reset();
    {
        QtConfig global("qt-config", QtConfig::ConfigType::GlobalConfig);
        SetOwn(ConsoleGroup::N3DS, false, 2);
        global.Save();
    }
    const QString text = Text(config.toStdString());
    const std::array<const char*, 7> keys{"resolution_factor", "texture_filter", "vr_screen_layout",
                                          "vr_screen_gap",     "vr_hide_bottom_screen",
                                          "vr_screen_curve",   "layer_depth"};
    std::array<QString, 7> lines;
    for (std::size_t i = 0; i < keys.size(); i++) {
        lines[i] = Line(text, QString::fromLatin1(keys[i]));
        CHECK(!lines[i].isEmpty());
    }
    // Every other group moves: the 3DS's lines are the same bytes
    {
        QtConfig global("qt-config", QtConfig::ConfigType::GlobalConfig);
        SetOwn(ConsoleGroup::DS, false, 0);
        SetOwn(ConsoleGroup::GBA, false, 1);
        Settings::values.layer_depth.SetValue(0); // the key that was already there stays out
        global.Save();
    }
    const QString text2 = Text(config.toStdString());
    for (std::size_t i = 0; i < 6; i++) {
        CHECK(Line(text2, QString::fromLatin1(keys[i])) == lines[i]);
    }
    {
        QSettings now(config, QSettings::IniFormat);
        const auto value_of = [&now](const QString& key) {
            for (const QString& name : now.allKeys()) {
                if (name.endsWith(QLatin1Char('/') + key)) {
                    return now.value(name).toInt();
                }
            }
            return -1;
        };
        CHECK(value_of(QStringLiteral("gba_resolution_factor")) == 6);
        CHECK(value_of(QStringLiteral("ds_resolution_factor")) == 5);
    }
    // An older build reads the same file for its own keys: it finds the 3DS's, the rest is no key
    // of its.
    {
        QSettings old(config, QSettings::IniFormat);
        old.beginGroup(QStringLiteral("Renderer"));
        CHECK(old.value(QStringLiteral("resolution_factor")).toInt() == 7);
    }
    // A new build reads an older file (no key of the groups): the 3DS's value stays, the others
    // start at their defaults
    {
        const QString path = QString::fromStdString(
            FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "/old-style.ini");
        {
            QSettings old(path, QSettings::IniFormat);
            old.beginGroup(QStringLiteral("Renderer"));
            old.setValue(QStringLiteral("resolution_factor"), 3);
            old.setValue(QStringLiteral("resolution_factor/default"), false);
            old.endGroup();
            old.beginGroup(QStringLiteral("VR"));
            old.setValue(QStringLiteral("vr_screen_layout"), 1);
            old.setValue(QStringLiteral("vr_screen_layout/default"), false);
            old.endGroup();
        }
        Reset();
        { QtConfig read("old-style", QtConfig::ConfigType::GlobalConfig); }
        const Snapshot s = Take();
        CHECK(s[0].resolution == 3);
        CHECK(s[0].layout == 1);
        CHECK(s[1].resolution == Settings::kDsResolutionDefault);
        CHECK(s[2].resolution == Settings::kGbaResolutionDefault);
        CHECK(s[1].layout == 0);
        CHECK(s[2].layout == 0);
    }
}
