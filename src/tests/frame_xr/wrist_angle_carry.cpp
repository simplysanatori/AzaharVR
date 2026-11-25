// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuaim: Wrist Angle counts from the back of the wrist; a file's angle from Wrist
// Side's place is carried over once (citra_qt/configuration/frame_per_game_ini.h, as
// QtConfig::ReadVrValues and SaveVrValues use it: read, carry, save with the mark).

#include <initializer_list>
#include <optional>
#include <utility>
#include <QSettings>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include "citra_qt/configuration/frame_per_game_ini.h"

namespace {

const QString kAngle = QStringLiteral("vr_kiu_wrist_angle");
const QString kSide = QStringLiteral("vr_wrist_side");
const QString kMark = QString::fromLatin1(FrameConfig::kWristAngleFromKey);

/// What the app holds after reading a file, as QtConfig does.
struct Memory {
    unsigned global_angle = 0; ///< the global value
    unsigned global_mark = 0;  ///< vr_kiu_wrist_angle_from
    std::optional<unsigned> game_angle; ///< the open game's own value
    unsigned Angle() const {
        return game_angle ? *game_angle : global_angle;
    }
};

/// QtConfig::ReadVrValues' part: the angle, then the carry.
void Read(const QString& path, bool global, Memory& mem) {
    QSettings ini(path, QSettings::IniFormat);
    ini.beginGroup(QStringLiteral("VR"));
    if (global) {
        mem.global_angle = ini.value(kAngle + QStringLiteral("/default"), false).toBool()
                               ? 0u
                               : ini.value(kAngle, 0u).toUInt();
    } else {
        mem.game_angle.reset();
        if (!ini.value(kAngle + QStringLiteral("/use_global"), true).toBool()) {
            mem.game_angle = ini.value(kAngle, 0u).toUInt();
        }
    }
    const FrameConfig::WristAngleCarry carry =
        FrameConfig::CarriedWristAngle(ini, global, mem.global_mark);
    if (carry.angle) {
        (global ? mem.global_angle : *mem.game_angle) = *carry.angle;
    }
    if (global) {
        mem.global_mark = carry.mark;
    }
}

/// QtConfig::SaveVrValues' part.
void Save(const QString& path, bool global, const Memory& mem) {
    QSettings ini(path, QSettings::IniFormat);
    ini.beginGroup(QStringLiteral("VR"));
    if (global) {
        ini.setValue(kAngle + QStringLiteral("/default"), mem.global_angle == 0u);
        ini.setValue(kAngle, mem.global_angle);
        ini.setValue(kMark, mem.global_mark < 1u ? 1u : mem.global_mark);
    } else {
        if (mem.game_angle) {
            ini.setValue(kAngle + QStringLiteral("/use_global"), false);
            ini.setValue(kAngle, *mem.game_angle);
        } else {
            FrameConfig::WriteUsesGlobal(ini, kAngle);
        }
        ini.setValue(kMark, 1u);
    }
    ini.endGroup();
    ini.sync();
}

void Write(const QString& path, std::initializer_list<std::pair<QString, QVariant>> values) {
    QSettings ini(path, QSettings::IniFormat);
    ini.beginGroup(QStringLiteral("VR"));
    for (const auto& [key, value] : values) {
        ini.setValue(key, value);
    }
    ini.endGroup();
    ini.sync();
}

} // namespace

TEST_CASE("Wrist Angle: the global file is carried once", "[frame_xr][kiuaim]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    int n = 0;
    const auto file = [&] { return dir.filePath(QStringLiteral("g%1.ini").arg(n++)); };
    { // A config that never had the setting, or the side: nothing changes.
        const QString path = file();
        Memory mem;
        Read(path, true, mem);
        CHECK(mem.global_angle == 0u);
        CHECK(mem.global_mark == 1u);
        Save(path, true, mem);
        Memory again;
        Read(path, true, again);
        CHECK(again.global_angle == 0u);
        CHECK(again.global_mark == 1u);
    }
    { // Back (the default, marked so, or chosen): the angle stays.
        const QString path = file();
        Write(path, {{kSide, 0}, {kSide + QStringLiteral("/default"), true}, {kAngle, 90},
                     {kAngle + QStringLiteral("/default"), false}});
        Memory mem;
        Read(path, true, mem);
        CHECK(mem.global_angle == 90u);
        CHECK(mem.global_mark == 1u);
    }
    { // Inside, the angle never set: 0 there is 180 from the back.
        const QString path = file();
        Write(path, {{kSide, 1}, {kSide + QStringLiteral("/default"), false}});
        Memory mem;
        Read(path, true, mem);
        CHECK(mem.global_angle == 180u);
        CHECK(mem.global_mark == 2u);
        Write(path, {{kAngle, 0}, {kAngle + QStringLiteral("/default"), true}});
        Memory marked_default;
        Read(path, true, marked_default);
        CHECK(marked_default.global_angle == 180u);
    }
    // Inside with an angle: 180 more, round the full turn; twice is once.
    for (const auto& [was, now] : {std::pair{90u, 270u}, std::pair{270u, 90u},
                                   std::pair{180u, 0u}, std::pair{360u, 180u}}) {
        const QString path = file();
        Write(path, {{kSide, 1}, {kSide + QStringLiteral("/default"), false}, {kAngle, was},
                     {kAngle + QStringLiteral("/default"), false}});
        Memory mem;
        Read(path, true, mem);
        CHECK(mem.global_angle == now);
        Memory before_save; // a second read before any save: the same, from the file alone
        Read(path, true, before_save);
        CHECK(before_save.global_angle == now);
        Save(path, true, mem);
        Memory again;
        Read(path, true, again);
        CHECK(again.global_angle == now);
        CHECK(again.global_mark == 2u);
        // Set after the carry, saved, read again: unchanged. Wrist Side changed after: unchanged.
        again.global_angle = 45u;
        Save(path, true, again);
        Write(path, {{kSide, 0}});
        Memory later;
        Read(path, true, later);
        CHECK(later.global_angle == 45u);
        CHECK(later.global_mark == 2u); // the side as it was when carried
    }
    { // A value over the range is taken as 360.
        const QString path = file();
        Write(path, {{kSide, 1}, {kAngle, 999}});
        Memory mem;
        Read(path, true, mem);
        CHECK(mem.global_angle == 180u);
    }
}

TEST_CASE("Wrist Angle: a game's file is carried once, by the side the global one had",
          "[frame_xr][kiuaim]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString global = dir.filePath(QStringLiteral("qt-config.ini"));
    const QString a = dir.filePath(QStringLiteral("0004000000030200.ini"));
    const QString b = dir.filePath(QStringLiteral("0004000000030100.ini"));
    const QString c = dir.filePath(QStringLiteral("0004000000033500.ini"));
    Write(global, {{kSide, 1}, {kSide + QStringLiteral("/default"), false}, {kAngle, 30},
                   {kAngle + QStringLiteral("/default"), false}});
    Write(a, {{kAngle + QStringLiteral("/use_global"), false}, {kAngle, 100}}); // its own
    Write(b, {{kAngle + QStringLiteral("/use_global"), true}});                 // follows
    // (c never existed)
    Memory mem;
    Read(global, true, mem);
    CHECK(mem.global_angle == 210u);
    Save(global, true, mem);
    // Wrist Side changed for the stats before any game is opened: the games still get 180.
    Write(global, {{kSide, 0}});
    Read(global, true, mem);
    CHECK(mem.global_angle == 210u);
    REQUIRE(mem.global_mark == 2u);
    // Game a, then b, then c: each alone; nothing of a's is left for b.
    Read(a, false, mem);
    CHECK(mem.Angle() == 280u);
    Save(a, false, mem);
    Read(b, false, mem);
    CHECK_FALSE(mem.game_angle);
    CHECK(mem.Angle() == 210u);
    Save(b, false, mem);
    Read(c, false, mem);
    CHECK_FALSE(mem.game_angle);
    Save(c, false, mem);
    // Again: nothing more is added.
    Read(a, false, mem);
    CHECK(mem.Angle() == 280u);
    // b gets its own angle after the carry (from the back already): kept as set.
    Read(b, false, mem);
    mem.game_angle = 15u;
    Save(b, false, mem);
    Read(b, false, mem);
    CHECK(mem.Angle() == 15u);
    // The menu's way (a value set, the file read before the save, the value put back): a game
    // not yet carried keeps the new value, and is marked.
    const QString d = dir.filePath(QStringLiteral("0004000000030300.ini"));
    Write(d, {{kAngle + QStringLiteral("/use_global"), false}, {kAngle, 100}});
    Read(d, false, mem);
    CHECK(mem.Angle() == 280u);
    mem.game_angle = 60u;
    Save(d, false, mem);
    Read(d, false, mem);
    CHECK(mem.Angle() == 60u);
    // With the global file carried from Back, a game's own angle stays.
    const QString e = dir.filePath(QStringLiteral("0004000000030400.ini"));
    Write(e, {{kAngle + QStringLiteral("/use_global"), false}, {kAngle, 100}});
    Memory back;
    back.global_mark = 1u;
    Read(e, false, back);
    CHECK(back.Angle() == 100u);
}
