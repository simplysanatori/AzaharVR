// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame useglobal: how a per-game file stores a setting that follows the global value.

#pragma once

#include <optional>
#include <QSettings>
#include <QString>

namespace FrameConfig {

/// In a per-game file (QtConfig::WriteGlobalSetting): the setting's key and its subkeys go and
/// only `use_global=true` stays, so the game's old value is not left behind and the global value
/// is never written there. QSettings saves the file whole (a temporary file, then a rename).
inline void WriteUsesGlobal(QSettings& ini, const QString& name) {
    ini.remove(name);
    ini.setValue(name + QStringLiteral("/use_global"), true);
}

/// AzaharFrame fovonce: Match Game Camera was a switch (`vr_immersive_match_camera`) and is now
/// Off (0), Set Once (1) or Always (2) (`vr_immersive_match_camera_mode`). For a file (its "VR"
/// group open) with no new key, the old switch's choice: on is Always, off is Off. Nothing
/// (std::nullopt) when the file has the new key, or the old switch was never set: the global file
/// marks a value never changed with `/default=true`, a game's file one that follows the global
/// value with `/use_global=true`.
inline std::optional<unsigned> MigratedMatchCamera(const QSettings& ini, bool global) {
    const QString now = QStringLiteral("vr_immersive_match_camera_mode");
    const QString old = QStringLiteral("vr_immersive_match_camera");
    if (ini.contains(now) || ini.contains(now + QStringLiteral("/use_global")) ||
        !ini.contains(old)) {
        return std::nullopt;
    }
    const bool unset = global ? ini.value(old + QStringLiteral("/default"), true).toBool()
                              : ini.value(old + QStringLiteral("/use_global"), true).toBool();
    if (unset) {
        return std::nullopt;
    }
    return ini.value(old, true).toBool() ? 2u : 0u;
}

/// AzaharFrame kiuaim: Wrist Angle (`vr_kiu_wrist_angle`, degrees round the wrist) counted from
/// Wrist Side's place and counts from the back of the wrist now. A file's angle is carried over
/// once; `vr_kiu_wrist_angle_from` in the file is the mark (absent: not carried yet).
/// The global file (its "VR" group open): with Wrist Side on Inside the angle gets 180 added (a
/// file that never had the angle: 0 becomes 180, the same place) and the mark is 2; else nothing
/// changes and the mark is 1. A game's file: an angle of its own gets 180 added when the global
/// file's mark is 2 (`global_mark`: the side when the global one was carried, not the side now);
/// its mark is 1. `angle` is set only when the value changes.
struct WristAngleCarry {
    std::optional<unsigned> angle;
    unsigned mark = 0;
    bool now = false; ///< carried by this read (the mark was not in the file)
};
inline constexpr const char* kWristAngleFromKey = "vr_kiu_wrist_angle_from";
inline WristAngleCarry CarriedWristAngle(const QSettings& ini, bool global, unsigned global_mark) {
    const QString mark = QString::fromLatin1(kWristAngleFromKey);
    const QString angle = QStringLiteral("vr_kiu_wrist_angle");
    const QString side = QStringLiteral("vr_wrist_side");
    WristAngleCarry out;
    if (ini.contains(mark)) {
        const unsigned m = ini.value(mark).toUInt();
        out.mark = global ? (m == 2u ? 2u : 1u) : 1u;
        return out;
    }
    out.now = true;
    const auto added = [](unsigned v) { return ((v > 360u ? 360u : v) + 180u) % 360u; };
    if (global) {
        const bool inside = !ini.value(side + QStringLiteral("/default"), false).toBool() &&
                            ini.value(side, 0u).toUInt() == 1u;
        const unsigned v = ini.value(angle + QStringLiteral("/default"), false).toBool()
                               ? 0u
                               : ini.value(angle, 0u).toUInt();
        out.mark = inside ? 2u : 1u;
        if (inside) {
            out.angle = added(v);
        }
        return out;
    }
    out.mark = 1u;
    const bool own = !ini.value(angle + QStringLiteral("/use_global"), true).toBool();
    if (own && global_mark == 2u) {
        out.angle = added(ini.value(angle, 0u).toUInt());
    }
    return out;
}

/// AzaharFrame kiu: Narrow Screen and Narrow Height (`vr_immersive_squeeze`, `_y`: percent
/// narrower, 0 to 30; 1, 2 and 3 were the 10 % steps of earlier builds) are now Horizontal and
/// Vertical Scale (`vr_immersive_scale_x`, `_y`: signed percent, negative is smaller). For a file
/// (its "VR" group open) with no new key, the old value as a scale: Narrow Screen 10 % is -10
/// (frame_xr Layout::ScaleFromNarrow). Nothing when the file has the new key (so this runs once:
/// the next save writes it), or the old value was never set (`/default=true` in the global file,
/// `/use_global=true` in a game's): those start at the new default, 0.
inline std::optional<int> MigratedScale(const QSettings& ini, bool global, const QString& old,
                                        const QString& now) {
    if (ini.contains(now) || ini.contains(now + QStringLiteral("/use_global")) ||
        !ini.contains(old)) {
        return std::nullopt;
    }
    const bool unset = global ? ini.value(old + QStringLiteral("/default"), true).toBool()
                              : ini.value(old + QStringLiteral("/use_global"), true).toBool();
    if (unset) {
        return std::nullopt;
    }
    const unsigned v = ini.value(old, 0u).toUInt();
    const unsigned percent = (v >= 1 && v <= 3) ? v * 10 : (v < 30 ? v : 30);
    return -static_cast<int>(percent);
}

} // namespace FrameConfig
