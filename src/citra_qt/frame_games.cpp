// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "citra_qt/frame_games.h"

#include <memory>
#include <utility>
#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QStringList>
#include <QTimer>

#include "citra_qt/uisettings.h"
#include "common/logging/log.h"
#include "core/frame_covers.h"
#include "core/loader/loader.h"
#include "frame_cores/rom_info.h"
#include "frame_xr/frame_xr.h"
#include "frame_xr/menu/cover_art.h"

using FrameXr::Menu::GameEntry;
using FrameXr::Menu::GamesSnapshot;

FrameGames::FrameGames(Hooks hooks_, QObject* parent) : QObject{parent}, hooks{std::move(hooks_)} {
    cover_stopper = std::make_unique<Mods::Transfer::Stopper>(cover_cancel);
    covers_publish.setSingleShot(true);
    covers_publish.setInterval(1000);
    connect(&covers_publish, &QTimer::timeout, this, &FrameGames::Publish);
}

FrameGames::~FrameGames() {
    {
        std::scoped_lock lock{cover_mutex};
        cover_stop = true;
        cover_jobs.clear();
    }
    cover_cv.notify_all();
    cover_stopper->Stop();
    if (cover_thread.joinable()) {
        cover_thread.join();
    }
}

void FrameGames::Listed() {
    rows = hooks.rows();
    missing.clear();
    FindCovers();
    Publish();
}

// ---- Covers (core/frame_covers.h) ----

namespace {

/// A cover file as the tile shows it (null: no image, or too large to be one).
std::shared_ptr<const FrameXr::Menu::GameIcon> LoadCover(const QByteArray& bytes) {
    QBuffer buffer;
    buffer.setData(bytes);
    QImageReader reader(&buffer);
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > 4096 || size.height() > 4096) {
        return nullptr;
    }
    const QImage image = reader.read().convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) {
        return nullptr;
    }
    FrameXr::Menu::GameIcon icon;
    icon.w = image.width();
    icon.h = image.height();
    for (int y = 0; y < image.height(); y++) {
        const auto* row = image.constScanLine(y);
        icon.rgba.insert(icon.rgba.end(), row, row + image.width() * 4);
    }
    auto cover = std::make_shared<FrameXr::Menu::GameIcon>(FrameXr::Menu::ComposeCover(icon));
    return cover->rgba.empty() ? nullptr : cover;
}

/// A file's name without its extension, and a zip's ROM extension: as No-Intro names it.
std::string StemOf(const std::string& file_name) {
    const std::size_t dot = file_name.rfind('.');
    if (dot != std::string::npos) {
        const QString ext = QString::fromStdString(file_name.substr(dot)).toLower();
        static const QStringList roms{QStringLiteral(".gba"), QStringLiteral(".gb"),
                                      QStringLiteral(".gbc"), QStringLiteral(".nds"),
                                      QStringLiteral(".dsi"), QStringLiteral(".srl")};
        if (roms.contains(ext)) {
            return file_name.substr(0, dot);
        }
    }
    return file_name;
}

} // namespace

void FrameGames::FindCovers() {
    covers_enabled = hooks.covers_on && hooks.covers_on();
    if (!covers_enabled) {
        return;
    }
    {
        std::scoped_lock lock{cover_mutex};
        for (const GameEntry& g : rows) {
            if (g.system != Common::GameSystem::N3DS && !covers.contains(g.path) &&
                covers_asked.insert(g.path).second) {
                // completeBaseName: "Game (USA).gba" of "Game (USA).gba.zip"
                cover_jobs.push_back({g.path, g.system, StemOf(QFileInfo(QString::fromStdString(
                                                                    g.path)).completeBaseName()
                                                                  .toStdString())});
            }
        }
    }
    if (!cover_thread.joinable()) {
        cover_thread = std::thread([this] { CoverThread(); });
    }
    cover_cv.notify_one();
}

void FrameGames::CoverThread() {
    Core::Covers::Cache cache(hooks.covers_dir);
    const Core::Covers::Get get = [this](const std::string& host, const std::string& path,
                                         std::size_t max_bytes) {
        return Core::Covers::HttpsGet(host, path, max_bytes, cover_stopper.get());
    };
    bool online = true; // false after no reply or a limit: the cached ones only, this run
    for (;;) {
        CoverJob job;
        {
            std::unique_lock lock{cover_mutex};
            cover_cv.wait(lock, [this] { return cover_stop || !cover_jobs.empty(); });
            if (cover_stop) {
                return;
            }
            job = std::move(cover_jobs.front());
            cover_jobs.pop_front();
            if (!covers_enabled) {
                covers_asked.erase(job.path); // asked for again once it is on
                continue;
            }
        }
        const auto source = Core::Covers::SourceFor(job.system, job.stem);
        if (!source || !covers_enabled) { // (turned off since)
            continue;
        }
        std::shared_ptr<const FrameXr::Menu::GameIcon> cover;
        if (const std::string file = cache.FileOf(*source); !file.empty()) {
            QFile f(QString::fromStdString(file));
            if (f.size() <= static_cast<qint64>(Core::Covers::kMaxBytes) &&
                f.open(QIODevice::ReadOnly)) {
                cover = LoadCover(f.readAll());
            }
        } else if (online && !cache.KnownMissing(*source, std::time(nullptr))) {
            const auto fetched = Core::Covers::Fetch(get, *source);
            online = !fetched.stop;
            if (fetched.result == Core::Covers::Fetched::Result::NotThere) {
                cache.MarkMissing(*source, std::time(nullptr));
            } else if (fetched.result == Core::Covers::Fetched::Result::Failed) {
                LOG_INFO(Frontend, "Games tab: no cover for {}: {}", job.stem, fetched.why);
            } else {
                cover = LoadCover(QByteArray::fromStdString(fetched.bytes));
                if (cover) {
                    cache.Store(*source, fetched.bytes);
                } else { // a PNG's or JPEG's signature, but no image: as if not there
                    cache.MarkMissing(*source, std::time(nullptr));
                }
            }
        }
        if (cover) {
            QMetaObject::invokeMethod(
                this, [this, path = job.path, cover] { CoverFound(path, cover); },
                Qt::QueuedConnection);
        }
    }
}

void FrameGames::CoverFound(const std::string& path,
                            std::shared_ptr<const FrameXr::Menu::GameIcon> cover) {
    covers[path] = std::move(cover);
    if (!covers_publish.isActive()) {
        covers_publish.start();
    }
}

void FrameGames::Publish() {
    FindCovers(); // Download Cover Art turned on since
    std::vector<GameEntry> marked = rows;
    for (GameEntry& g : marked) {
        g.favorite = UISettings::values.favorited_ids.contains(g.key);
        g.missing = missing.contains(g.path);
        g.played_s = hooks.played(g.key);
        if (const auto it = covers.find(g.path); it != covers.end()) {
            g.cover = it->second;
        }
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
