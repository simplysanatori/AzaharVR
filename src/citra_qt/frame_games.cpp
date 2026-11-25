// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "citra_qt/frame_games.h"

#include <algorithm>
#include <ctime>
#include <iterator>
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
#include "common/cover_strings.h"
#include "common/logging/log.h"
#include "core/frame_covers.h"
#include "core/loader/loader.h"
#include "frame_cores/rom_info.h"
#include "frame_xr/frame_xr.h"
#include "frame_xr/menu/cover_art.h"

using FrameXr::Menu::GameEntry;
using FrameXr::Menu::GamesSnapshot;
namespace Text = Common::CoverText;

FrameGames::FrameGames(Hooks hooks_, QObject* parent)
    : QObject{parent}, hooks{std::move(hooks_)}, library{hooks.covers_dir} {
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
    std::vector<Core::Covers::NewGames::Listed> listed;
    for (const GameEntry& row : rows) {
        listed.push_back({row.path, GameOf(row)});
    }
    new_games.Scanned(listed, library, std::time(nullptr));
    AskForNewGames();
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

Core::Covers::Game FrameGames::GameOf(const GameEntry& row) {
    // completeBaseName: "Game (USA).gba" of "Game (USA).gba.zip"
    const QString name = QFileInfo(QString::fromStdString(row.path)).completeBaseName();
    return {row.system, row.key, StemOf(name.toStdString())};
}

const GameEntry* FrameGames::RowOf(const std::string& path) const {
    const auto it = std::find_if(rows.begin(), rows.end(),
                                 [&path](const GameEntry& g) { return g.path == path; });
    return it != rows.end() ? &*it : nullptr;
}

void FrameGames::Queue(std::vector<CoverJob> jobs, bool first) {
    if (jobs.empty()) {
        return;
    }
    {
        std::scoped_lock lock{cover_mutex};
        cover_jobs.insert(first ? cover_jobs.begin() : cover_jobs.end(),
                          std::make_move_iterator(jobs.begin()),
                          std::make_move_iterator(jobs.end()));
    }
    if (!cover_thread.joinable()) {
        cover_thread = std::thread([this] { CoverThread(); });
    }
    cover_cv.notify_one();
}

void FrameGames::FindCovers() {
    covers_enabled = hooks.covers_on && hooks.covers_on();
    std::vector<CoverJob> jobs;
    for (const GameEntry& row : rows) {
        const bool load = covers_loaded.insert(GameOf(row).Id() + row.path).second;
        const bool fetch = covers_enabled && covers_automatic.insert(GameOf(row).Id() + row.path).second;
        if (load || fetch) {
            jobs.push_back({.path = row.path, .game = GameOf(row), .fetch = fetch, .automatic = true});
        }
    }
    Queue(std::move(jobs), false);
}

void FrameGames::CoverSettingChanged() {
    covers_automatic.clear(); // turned on: every game without one, again
    FindCovers();
}

void FrameGames::ReloadCover(const Core::Covers::Game& game) {
    std::vector<CoverJob> jobs;
    for (const GameEntry& row : rows) {
        if (GameOf(row) == game) {
            jobs.push_back({.path = row.path, .game = game});
        }
    }
    Queue(std::move(jobs), true);
}

void FrameGames::DownloadThumbnails(const std::vector<std::string>& paths) {
    std::vector<std::string> wanted;
    for (const std::string& path : paths) {
        const GameEntry* row = RowOf(path);
        if (row != nullptr && library.Wanted(GameOf(*row), std::time(nullptr))) {
            wanted.push_back(path);
        }
    }
    if (wanted.empty()) {
        hooks.toast(QString::fromUtf8(Text::kNothingToAsk));
        return;
    }
    const QString body = QString::fromStdString(Text::AskBody(wanted.size(), false));
    hooks.confirm(QString::fromUtf8(Text::kAskTitle), body, QString::fromUtf8(Text::kAskYes),
                  QString::fromUtf8(Text::kAskNo), [this, wanted](bool yes) {
                      if (yes) {
                          StartDownload(wanted);
                      }
                  });
}

void FrameGames::GameStopped() {
    // Not when another game starts right away
    QTimer::singleShot(1000, this, [this] { AskForNewGames(); });
}

void FrameGames::AskForNewGames() {
    const auto games = new_games.Take(library, {.automatic = hooks.covers_on(),
                                                .running = !hooks.running_path().isEmpty(),
                                                .online = hooks.online(),
                                                .time = std::time(nullptr)});
    if (games.empty()) {
        return;
    }
    std::vector<std::string> paths;
    for (const auto& game : games) {
        paths.push_back(game.path);
    }
    QTimer::singleShot(0, this, [this, paths] { // after the list's own
        hooks.confirm(QString::fromUtf8(Text::kAskTitle),
                      QString::fromStdString(Text::AskBody(paths.size(), true)),
                      QString::fromUtf8(Text::kAskYes), QString::fromUtf8(Text::kAskNo),
                      [this, paths](bool yes) {
                          if (yes) {
                              StartDownload(paths);
                          }
                      });
    });
}

void FrameGames::StartDownload(const std::vector<std::string>& paths) {
    auto batch = std::make_shared<CoverBatch>();
    std::vector<CoverJob> jobs;
    for (const std::string& path : paths) {
        if (const GameEntry* row = RowOf(path)) {
            jobs.push_back({.path = path, .game = GameOf(*row), .fetch = true, .batch = batch});
        }
    }
    batch->left = static_cast<int>(jobs.size());
    Queue(std::move(jobs), true);
}

void FrameGames::ChooseCover(const std::string& path, Core::Covers::Art art) {
    const GameEntry* row = RowOf(path);
    if (row == nullptr) {
        return;
    }
    const Core::Covers::Game game = GameOf(*row);
    if (library.Has(game, art)) {
        library.Choose(game, Core::Covers::ChoiceFor(art));
        ReloadCover(game);
        return;
    }
    if (game.key == 0 || !Core::Covers::SourceFor(game.system, game.stem, art) ||
        library.KnownMissing(game, art, std::time(nullptr))) { // it has none: not asked for
        hooks.toast(QString::fromUtf8(Text::kNoneFound));
        return;
    }
    const auto start = [this, path, game, art](bool yes) {
        if (yes) {
            auto batch = std::make_shared<CoverBatch>();
            batch->left = 1;
            Queue({{.path = path, .game = game, .art = art, .fetch = true, .choose = true,
                    .batch = batch}},
                  true);
        }
    };
    if (hooks.covers_on()) {
        start(true);
    } else {
        hooks.confirm(QString::fromUtf8(Text::kAskTitle),
                      QString::fromStdString(Text::AskBody(1, false)),
                      QString::fromUtf8(Text::kAskYes), QString::fromUtf8(Text::kAskNo), start);
    }
}

void FrameGames::NoCover(const std::string& path) {
    if (const GameEntry* row = RowOf(path)) {
        library.Choose(GameOf(*row), Core::Covers::Choice::None);
        ReloadCover(GameOf(*row));
    }
}

void FrameGames::ResetCover(const std::string& path) {
    if (const GameEntry* row = RowOf(path)) {
        library.Choose(GameOf(*row), Core::Covers::Choice::Automatic);
        ReloadCover(GameOf(*row));
    }
}

void FrameGames::UseOwnImage(const std::string& path, const QString& file) {
    const GameEntry* row = RowOf(path);
    QFile f(file);
    if (row == nullptr || file.isEmpty()) {
        return;
    }
    QString why;
    if (f.size() > static_cast<qint64>(Core::Covers::kMaxOwnBytes)) {
        why = QString::fromUtf8(Text::kOwnTooLarge);
    } else {
        const QByteArray bytes = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{};
        const Core::Covers::Game game = GameOf(*row);
        if (Core::Covers::ImageOf(bytes.toStdString()) == Core::Covers::Image::None ||
            !LoadCover(bytes) || !library.SetOwn(game, bytes.toStdString()).empty()) {
            why = QString::fromUtf8(Text::kOwnNotImage);
        } else {
            ReloadCover(game);
        }
    }
    if (!why.isEmpty()) {
        hooks.toast(why);
    }
}

void FrameGames::CoverThread() {
    const Core::Covers::Get get =
        hooks.covers_get ? hooks.covers_get
                         : [this](const std::string& host, const std::string& path,
                                  std::size_t max_bytes) {
                               return Core::Covers::HttpsGet(host, path, max_bytes,
                                                             cover_stopper.get());
                           };
    const auto is_image = [](const std::string& bytes) {
        return LoadCover(QByteArray::fromStdString(bytes)) != nullptr;
    };
    bool online = true; // the automatic ones: false after no reply or a limit, this run
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
        }
        using Core::Covers::Outcome;
        bool got = false;
        const std::time_t now = std::time(nullptr);
        const bool may = job.automatic ? covers_enabled && online && library.Wanted(job.game, now)
                                       : job.batch && !job.batch->stopped;
        if (job.fetch && may) {
            const Outcome outcome =
                Core::Covers::Download(get, library, job.game, job.art, now, is_image);
            got = outcome == Outcome::Got;
            if (outcome == Outcome::Stop) {
                online = false;
                if (job.batch) {
                    job.batch->stopped = true;
                }
            } else if (got) {
                online = true;
                if (job.choose) {
                    library.Choose(job.game, Core::Covers::ChoiceFor(job.art));
                }
            }
        }
        std::shared_ptr<const FrameXr::Menu::GameIcon> cover;
        if (const std::string shown = library.Shown(job.game); !shown.empty()) {
            QFile f(QString::fromStdString(shown));
            if (f.size() <= static_cast<qint64>(Core::Covers::kMaxOwnBytes) &&
                f.open(QIODevice::ReadOnly)) {
                cover = LoadCover(f.readAll());
            }
        }
        QMetaObject::invokeMethod(
            this,
            [this, job, cover, got] {
                CoverFound(job.path, cover);
                if (got && job.choose) {
                    ReloadCover(job.game);
                }
                if (job.batch) {
                    BatchStep(job.batch, got);
                }
            },
            Qt::QueuedConnection);
    }
}

void FrameGames::BatchStep(const std::shared_ptr<CoverBatch>& batch, bool found) {
    batch->found += found ? 1 : 0;
    if (--batch->left > 0) {
        return;
    }
    hooks.toast(batch->found > 0 ? QString::fromStdString(Text::Downloaded(batch->found))
                : batch->stopped ? QString::fromUtf8(Text::kUnreachable)
                                 : QString::fromUtf8(Text::kNoneFound));
}

void FrameGames::CoverFound(const std::string& path,
                            std::shared_ptr<const FrameXr::Menu::GameIcon> cover) {
    if (const auto it = covers.find(path); it == covers.end() ? !cover : it->second == cover) {
        return; // as before
    }
    covers[path] = std::move(cover);
    if (!covers_publish.isActive()) {
        covers_publish.start();
    }
}

void FrameGames::Publish() {
    FindCovers(); // Download Game Covers turned on or off since
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
    FrameXr::SetInGameMenuGames(published, row, covers_enabled);
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
