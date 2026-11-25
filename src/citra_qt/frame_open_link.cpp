// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <QDesktopServices>
#include <QProcess>
#include <QUrl>
#include "citra_qt/frame_open_link.h"
#include "common/logging/log.h"
#include "frame_xr/mem_guard.h" // AzaharFrame modfix

namespace OpenLink {

namespace {

/// Whether Steam runs: ~/.steam/steam.pid names a live process.
bool SteamRunning() {
    const char* home = std::getenv("HOME");
    if (!home) {
        return false;
    }
    std::ifstream pid_file(std::string(home) + "/.steam/steam.pid");
    long pid = 0;
    if (!(pid_file >> pid) || pid <= 0) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists("/proc/" + std::to_string(pid), ec);
}

/// AzaharFrame modfix: memory and swap nearly used up (see frame_xr/mem_guard.h).
bool LowMemory() {
    std::ifstream in("/proc/meminfo");
    std::stringstream text;
    text << in.rdbuf();
    return FrameXr::BrowserWouldRunOutOfMemory(text.str());
}

} // namespace

Opener ChooseOpener(bool low_memory, bool steam_running) {
    if (low_memory) {
        return Opener::None;
    }
    return steam_running ? Opener::Steam : Opener::Browser;
}

Result Open(const QString& url, const Hooks& hooks) {
    const Opener opener = ChooseOpener(hooks.low_memory(), hooks.steam_running());
    if (opener == Opener::None) {
        LOG_WARNING(Frontend, "Not opening {}: little memory left", url.toStdString());
        return Result::LowMemory;
    }
    if (opener == Opener::Steam && hooks.start_steam(QStringLiteral("steam://openurl/") + url)) {
        LOG_INFO(Frontend, "Opening {} in Steam", url.toStdString());
        return Result::OpenedInSteam;
    }
    LOG_INFO(Frontend, "Opening {} in the browser", url.toStdString());
    hooks.open_browser(url);
    return Result::OpenedInBrowser;
}

Result Open(const QString& url) {
    Hooks hooks;
    hooks.low_memory = LowMemory;
    hooks.steam_running = SteamRunning;
    hooks.start_steam = [](const QString& steam_url) {
        return QProcess::startDetached(QStringLiteral("steam"), {steam_url});
    };
    hooks.open_browser = [](const QString& u) { QDesktopServices::openUrl(QUrl(u)); };
    return Open(url, hooks);
}

} // namespace OpenLink
