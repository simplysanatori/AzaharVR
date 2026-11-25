// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "citra_qt/frame_alt_cheats.h"

#include <filesystem>
#include <string>
#include <thread>

#include <QApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QWidget>

#include "common/logging/log.h"
#include "core/cheats/online.h"
#include "frame_cores/alt_core_host.h"
#include "frame_cores/cheat_db.h"
#include "frame_cores/utf8_path.h"
#include "frame_xr/frame_xr.h"
#include "frame_xr/menu/cheat_entry.h"
#include "frame_xr/swkbd.h"

namespace FrameAltCheats {

namespace {

using FrameXr::Menu::DownloadState;
namespace Online = Cheats::Online;
namespace Db = FrameCores::CheatDb;

std::uint64_t g_key = 0;
DownloadState g_state = DownloadState::Idle;
std::string g_status;

Db::System CheatSystemOf(const FrameCores::RomInfo& info) {
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

bool g_adding = false; ///< Add Cheat's keyboard is open

using Getter = std::function<FrameCores::AltCoreHost*()>;

/// The cheat goes into the running game's list (on, as typed in to be used) when that game is
/// still the one it was typed for.
void Store(const Getter& running, const std::function<void()>& changed, std::uint64_t key,
           const std::string& name_in, const std::string& code) {
    FrameCores::AltCoreHost* host = running ? running() : nullptr;
    if (!host || host->GetRomInfo().title_id != key) {
        return;
    }
    std::string name = name_in;
    while (!name.empty() && (name.front() == ' ' || name.back() == ' ')) {
        name = name.front() == ' ' ? name.substr(1) : name.substr(0, name.size() - 1);
    }
    if (!host->AddCheat(name, code)) {
        return;
    }
    LOG_INFO(Frontend, "xr: in-game menu: cheat added (alt core)");
    FrameXr::ShowToastText(FrameXr::Menu::CheatsAddedText(1), FrameXr::ToastTime::Long);
    if (changed) {
        changed();
    }
}

void AskCode(Getter running, std::function<void()> changed, std::uint64_t key, Db::System system,
             std::string name) {
    FrameXr::Swkbd::Spec spec;
    spec.hint = "Code (XXXXXXXX YYYYYYYY)";
    spec.max_length = 4000;
    spec.multiline = true;
    spec.buttons = {{"Cancel", 0, false}, {"Add", 1, true}};
    spec.validate = [system](const std::string& text) {
        std::string code;
        return Db::NormalizeTyped(text, system, code);
    };
    spec.over_menu = true;
    g_adding = FrameXr::OpenGameKeyboard(
        std::move(spec), [=](const FrameXr::SwkbdOutcome& o) {
            QMetaObject::invokeMethod(
                qApp,
                [=] {
                    g_adding = false;
                    std::string code;
                    if (!o.shown || o.button != 1 ||
                        !Db::NormalizeTyped(o.text, system, code).empty()) {
                        return;
                    }
                    Store(running, changed, key, name, code);
                },
                Qt::QueuedConnection);
        });
}

void AskName(Getter running, std::function<void()> changed, std::uint64_t key, Db::System system) {
    FrameXr::Swkbd::Spec spec;
    spec.hint = "Cheat Name";
    spec.max_length = 60;
    spec.buttons = {{"Cancel", 0, false}, {"Next", 1, true}};
    spec.validate = [](const std::string& text) { return FrameXr::Menu::CheckCheatName(text); };
    spec.over_menu = true;
    g_adding = FrameXr::OpenGameKeyboard(
        std::move(spec), [=](const FrameXr::SwkbdOutcome& o) {
            QMetaObject::invokeMethod(
                qApp,
                [=] {
                    g_adding = false;
                    if (!o.shown || o.button != 1) {
                        return;
                    }
                    FrameCores::AltCoreHost* host = running ? running() : nullptr;
                    if (!host || host->GetRomInfo().title_id != key) {
                        return;
                    }
                    AskCode(running, changed, key, system, o.text);
                },
                Qt::QueuedConnection);
        });
}

void AddFlat(const Getter& running, const std::function<void()>& changed, std::uint64_t key,
             Db::System system) {
    QWidget* owner = QApplication::activeWindow();
    QString name;
    for (;;) {
        bool ok = false;
        name = QInputDialog::getText(owner, QStringLiteral("Add Cheat"),
                                     QStringLiteral("Cheat Name"), QLineEdit::Normal, name, &ok);
        if (!ok) {
            return;
        }
        const std::string why = FrameXr::Menu::CheckCheatName(name.toStdString());
        if (why.empty()) {
            break;
        }
        QMessageBox::warning(owner, QStringLiteral("Add Cheat"), QString::fromStdString(why));
    }
    QString text;
    for (;;) {
        bool ok = false;
        text = QInputDialog::getMultiLineText(owner, QStringLiteral("Add Cheat"),
                                              QStringLiteral("Code (XXXXXXXX YYYYYYYY)"), text, &ok);
        if (!ok) {
            return;
        }
        std::string code;
        const std::string why = Db::NormalizeTyped(text.toStdString(), system, code);
        if (why.empty()) {
            Store(running, changed, key, name.trimmed().toStdString(), code);
            return;
        }
        QMessageBox::warning(owner, QStringLiteral("Add Cheat"), QString::fromStdString(why));
    }
}

} // namespace

void Add(FrameCores::AltCoreHost& host, std::function<FrameCores::AltCoreHost*()> running,
         std::function<void()> changed) {
    if (g_adding) {
        return;
    }
    const FrameCores::RomInfo info = host.GetRomInfo();
    const Db::System system = CheatSystemOf(info);
    if (!FrameXr::VrShowing()) {
        AddFlat(running, changed, info.title_id, system);
        return;
    }
    AskName(std::move(running), std::move(changed), info.title_id, system);
}

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
    const Db::System system = CheatSystemOf(info);
    const std::string code = info.game_code;
    const std::string stem = FrameCores::FsUtf8(FrameCores::FsPath(rom_path).stem());
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
