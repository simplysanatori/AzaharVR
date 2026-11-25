// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "citra_qt/frame_games.h"

#include <memory>
#include <utility>
#include <QDateTime>
#include <QFileInfo>
#include <QTimer>

#include "citra_qt/uisettings.h"
#include "common/logging/log.h"
#include "core/loader/loader.h"
#include "frame_cores/rom_info.h"
#include "frame_xr/frame_xr.h"

using FrameXr::Menu::GameEntry;
using FrameXr::Menu::GamesSnapshot;

FrameGames::FrameGames(Hooks hooks_, QObject* parent) : QObject{parent}, hooks{std::move(hooks_)} {}

void FrameGames::Listed() {
    rows = hooks.rows();
    missing.clear();
    Publish();
}

void FrameGames::Publish() {
    std::vector<GameEntry> marked = rows;
    for (GameEntry& g : marked) {
        g.favorite = UISettings::values.favorited_ids.contains(g.key);
        g.missing = missing.contains(g.path);
        g.played_s = hooks.played(g.key);
    }
    std::vector<std::string> recent;
    for (const QString& path : std::as_const(UISettings::values.recent_files)) {
        recent.push_back(path.toStdString());
    }
    GamesSnapshot list = FrameXr::Menu::MakeSnapshot(std::move(marked), recent);

    // The running game's row, whichever way it was started
    const QString running = hooks.running_path();
    const std::string path = running.toStdString();
    const std::string real =
        running.isEmpty() ? std::string{} : QFileInfo(running).canonicalFilePath().toStdString();
    std::string row;
    for (const GameEntry& g : list.games) {
        if (!path.empty() && (g.path == path || (!g.real_path.empty() && g.real_path == real))) {
            row = g.path;
            break;
        }
    }
    if (!published || !(*published == list)) { // the same list: nothing to draw again
        published = std::make_shared<const GamesSnapshot>(std::move(list));
    }
    FrameXr::SetInGameMenuGames(published, row);
}

FrameGames::File FrameGames::Check(const QString& path) {
    if (!QFileInfo::exists(path)) {
        return File::Missing;
    }
    const std::string file = path.toStdString();
    if (FrameCores::DetectCoreKind(file) != Common::AltCore::CoreKind::N3DS) {
        return FrameCores::ProbeRom(file) ? File::Good : File::Unreadable;
    }
    const auto loader = Loader::GetLoader(file);
    bool executable = false;
    return loader && loader->IsExecutable(executable) == Loader::ResultStatus::Success && executable
               ? File::Good
               : File::Unreadable;
}

void FrameGames::Play(const QString& path) {
    if (path.isEmpty() || path == hooks.running_path()) {
        hooks.close_menu();
        return;
    }
    const File file = Check(path);
    LOG_INFO(Frontend, "xr: in-game menu: Games: {} ({})", path.toStdString(),
             file == File::Good ? "found" : file == File::Missing ? "not there" : "not readable");
    if (file == File::Missing) {
        missing.insert(path.toStdString());
        Publish();
        return;
    }
    missing.erase(path.toStdString());
    if (file == File::Unreadable) { // as BootAltGame's message
        hooks.ask(tr("Unable to Load ROM"), tr("Could not read ROM header from:\n%1").arg(path),
                  tr("OK"), {}, {}, {});
        return;
    }
    if (hooks.running_path().isEmpty()) {
        Start(path);
        return;
    }
    QString title = QFileInfo(path).completeBaseName();
    if (published) {
        for (const GameEntry& g : published->games) {
            if (g.path == path.toStdString()) {
                title = QString::fromStdString(g.title);
                break;
            }
        }
    }
    QString body = tr("%1 will close.").arg(hooks.running_title());
    if (!hooks.autosaves()) {
        body += QLatin1Char(' ') + tr("Progress not saved in the game is lost.");
    }
    hooks.ask(tr("Start %1?").arg(title), body, tr("Start"), tr("Cancel"),
              hooks.can_save() ? tr("Save State First") : QString{}, [this, path](int choice) {
                  if (choice == 1) {
                      Start(path);
                  } else if (choice == 2) {
                      SaveThenStart(path);
                  }
              });
}

void FrameGames::ToggleFavourite(const std::string& path) {
    for (const GameEntry& g : rows) {
        if (g.path == path && g.key != 0) {
            hooks.toggle_favourite(g.key);
            Publish();
            return;
        }
    }
}

void FrameGames::Start(const QString& path) {
    hooks.close_menu();
    QTimer::singleShot(0, this, [this, path] { hooks.boot(path); });
}

// The menu closes after the answer, so the game's next frame saves; then the new game starts. With
// no save written (it failed, or its question was closed) the running game goes on.
void FrameGames::SaveThenStart(const QString& path) {
    const auto since = static_cast<quint64>(QDateTime::currentSecsSinceEpoch());
    const int failures = hooks.save_failures();
    const QString from = hooks.running_path();
    hooks.save();
    auto* timer = new QTimer(this);
    timer->setInterval(150);
    connect(timer, &QTimer::timeout, this, [this, timer, path, from, since, failures, tries = 0]() mutable {
        const bool done = hooks.saved(since);
        if (!done && hooks.asking()) {
            return; // About Save States, the first time
        }
        if (!done && hooks.save_failures() == failures && hooks.running_path() == from &&
            ++tries < 60) {
            return;
        }
        timer->deleteLater();
        timer->stop();
        if (done && hooks.running_path() == from) {
            hooks.boot(path);
        } else {
            LOG_WARNING(Frontend, "xr: in-game menu: Games: no state saved, the game goes on");
        }
    });
    timer->start();
}
