// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "citra_qt/frame_alt_cheats.h"

#include <filesystem>
#include <string>
#include <thread>

#include <QApplication>

#include "common/logging/log.h"
#include "core/cheats/online.h"
#include "frame_cores/alt_core_host.h"
#include "frame_cores/cheat_db.h"

namespace FrameAltCheats {

namespace {

using FrameXr::Menu::DownloadState;
namespace Online = Cheats::Online;
namespace Db = FrameCores::CheatDb;

std::uint64_t g_key = 0;
DownloadState g_state = DownloadState::Idle;
std::string g_status;

Db::System SystemOf(const FrameCores::RomInfo& info) {
    using Common::AltCore::CoreKind;
    switch (info.kind) {
    case CoreKind::GBA:
        return Db::System::Gba;
    case CoreKind::GB:
        return info.is_gbc ? Db::System::Gbc : Db::System::Gb;
    default:
        return Db::System::Nds; // DS and DSi
    }
}

} // namespace

void Start(FrameCores::AltCoreHost& host, std::function<FrameCores::AltCoreHost*()> running,
           std::function<void()> changed) {
    const FrameCores::RomInfo info = host.GetRomInfo();
    if (g_state == DownloadState::Downloading && g_key == info.title_id) {
        return;
    }
    const std::string rom_path = host.RomPath();
    if (rom_path.empty()) {
        return;
    }
    g_key = info.title_id;
    g_state = DownloadState::Downloading;
    g_status.clear();
    LOG_INFO(Frontend, "xr: in-game menu: downloading cheats (alt core)");
    const std::uint64_t key = g_key;
    const Db::System system = SystemOf(info);
    const std::string code = info.game_code;
    const std::string stem = std::filesystem::path(rom_path).stem().string();
    std::thread([=] {
        const auto crc = Db::RomCrc32(rom_path);
        Db::Result result = Db::Download(Online::HttpsGet, system, code, crc, stem);
        QMetaObject::invokeMethod(
            qApp,
            [=, result = std::move(result)] {
                FrameCores::AltCoreHost* now = running ? running() : nullptr;
                if (g_key != key || !now || now->GetRomInfo().title_id != key) {
                    return; // another game runs now
                }
                // The same status lines as the 3DS download
                Online::DownloadResult shown;
                shown.error = result.error;
                shown.found_any = result.found;
                if (result.found) {
                    shown.developer_added = now->ImportCheats(result.cheats);
                    shown.warning = result.by_name ? "Matched by Name" : "";
                }
                const std::string text = Online::StatusText(shown);
                if (!shown.error.empty()) {
                    g_state = DownloadState::Failed;
                    g_status = FrameXr::Menu::FailureReason(text);
                } else {
                    g_state = DownloadState::Done;
                    g_status = text;
                }
                LOG_INFO(Frontend, "xr: in-game menu: cheats download: {} ({} not valid)", text,
                         result.skipped);
                if (changed) {
                    changed();
                }
            },
            Qt::QueuedConnection);
    }).detach();
    if (changed) {
        changed();
    }
}

void Fill(std::uint64_t key, FrameXr::Menu::CheatsModel& model) {
    if (key != 0 && key == g_key) {
        model.download = g_state;
        model.status = g_status;
    }
}

} // namespace FrameAltCheats
