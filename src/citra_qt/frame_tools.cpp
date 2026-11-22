// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharFrame cpphelpers: see frame_tools.h.

#include "citra_qt/frame_tools.h"

#include <cstring>
#include <string>
#include <vector>
#include "core/homebrew/emerald3ds_build.h"
#include <QImage>
#ifdef AZAHARVR_PC // AzaharVR: EnsureControllerModels
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include "common/logging/log.h"
#endif
#include "core/frame_controller_models.h"
#include "core/frame_dock_icon.h"
#include "core/frame_weapon_models.h" // AzaharFrame kiuguns
#include "core/steam_cef.h"

namespace FrameTools {

namespace {

bool LoadRgb(const std::string& path, FrameControllerModels::Image* out) {
    QImage image(QString::fromStdString(path));
    if (image.isNull()) {
        return false;
    }
    image = image.convertToFormat(QImage::Format_RGB888);
    out->width = image.width();
    out->height = image.height();
    out->rgb.resize(static_cast<std::size_t>(image.width()) * image.height() * 3);
    for (int y = 0; y < image.height(); ++y) { // tight rows
        std::memcpy(out->rgb.data() + static_cast<std::size_t>(y) * image.width() * 3, image.constScanLine(y),
                    static_cast<std::size_t>(image.width()) * 3);
    }
    return true;
}

} // namespace

std::optional<int> Run(int argc, char* argv[]) {
    if (argc < 2 || std::strncmp(argv[1], "--frame-", 8) != 0) {
        return std::nullopt;
    }
    const std::string mode = argv[1];
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    if (mode == "--frame-emerald-build") {
        return Homebrew::Emerald3ds::Build::RunCli(args);
    }
    if (mode == "--frame-cef") { // steam-cef.py's command line
        return SteamCef::RunCli(args);
    }
    if (mode == "--frame-steam-art") { // APPID DIR [--dry-run]
        args.insert(args.begin(), "--art");
        return SteamCef::RunCli(args);
    }
    if (mode == "--frame-controller-models") { // OUTDIR [SteamVR rendermodels dir]
        return FrameControllerModels::Run(args, LoadRgb);
    }
    if (mode == "--frame-weapon-models") { // AzaharFrame kiuguns: GAME [OUTDIR]
        return FrameWeapons::RunCli(args);
    }
    if (mode == "--frame-dock-icon") {
        return FrameDockIcon::RunCli(args);
    }
    if (mode == "--frame-steam-shortcut") {
        return SteamCef::RunShortcutCli(args);
    }
    return std::nullopt;
}

#ifdef AZAHARVR_PC // AzaharVR: no installer makes the controller models on a PC
void EnsureControllerModels(const std::string& out_dir) {
    const QString left = QString::fromStdString(out_dir + "frame_controller_left.model");
    const QString right = QString::fromStdString(out_dir + "frame_controller_right.model");
    if (QFileInfo::exists(left) && QFileInfo::exists(right)) {
        return;
    }
    // SteamVR's own record of where it is installed (the same file on Windows and Linux).
#ifdef _WIN32
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#else
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
#endif
    QFile file(config + QStringLiteral("/openvr/openvrpaths.vrpath"));
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_WARNING(Frontend, "xr: controller models: SteamVR's openvrpaths.vrpath not found in {}",
                    config.toStdString());
        return;
    }
    const QJsonArray runtimes = QJsonDocument::fromJson(file.readAll()).object().value(
        QStringLiteral("runtime")).toArray();
    for (const QJsonValue& runtime : runtimes) {
        const QString src = QDir(runtime.toString()).filePath(
            QStringLiteral("drivers/frame_controller/resources/rendermodels"));
        if (!QFileInfo(src).isDir()) {
            continue;
        }
        const int code = FrameControllerModels::Run(
            {out_dir, QDir::toNativeSeparators(src).toStdString()}, LoadRgb);
        if (code == 0) {
            LOG_INFO(Frontend, "xr: controller models: made from {}", src.toStdString());
        } else {
            LOG_WARNING(Frontend, "xr: controller models: could not be made from {} ({})",
                        src.toStdString(), code);
        }
        return;
    }
    LOG_WARNING(Frontend, "xr: controller models: no SteamVR with the Steam Frame controllers' "
                          "models in {}",
                file.fileName().toStdString());
}
#endif

} // namespace FrameTools
