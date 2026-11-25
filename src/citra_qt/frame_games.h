// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// The VR menu's Games tab, the Qt side: publishes the desktop game list's games to the menu
// (FrameXr::SetInGameMenuGames) and starts the one pressed: its file is checked before the running
// game closes, and the player is asked.

#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <QObject>
#include <QString>
#include <QTimer>

#include "core/frame_covers.h"
#include "core/mods/transfer.h"
#include "frame_xr/menu/games_snapshot.h"

class FrameGames : public QObject {
public:
    struct Hooks {
        std::function<std::vector<FrameXr::Menu::GameEntry>()> rows; ///< the game list's
        std::function<std::uint64_t(std::uint64_t key)> played; ///< seconds
        std::function<void(std::uint64_t key)> toggle_favourite; ///< the game list's
        std::function<QString()> running_path;  ///< "" no game
        std::function<QString()> running_title;
        std::function<bool()> can_save;         ///< the running game can have save states
        std::function<bool()> autosaves;        ///< it is saved as it stops
        std::function<void()> save;             ///< Quick Save (its first time asks; see `asking`)
        std::function<bool(quint64 since)> saved; ///< the quick save was written since then
        std::function<int()> save_failures;       ///< counts the ones that failed
        std::function<bool()> asking;             ///< a question of the menu's is open
        /// The menu's dialog; `answer`: 1 confirm, 0 cancel, 2 extra ("" none).
        std::function<void(const QString& title, const QString& body, const QString& confirm,
                           const QString& cancel, const QString& extra,
                           std::function<void(int)> answer)>
            ask;
        std::function<void()> close_menu;
        std::function<void(const QString& path)> boot; ///< BootGame (stops the running game first)
        std::function<bool()> covers_on;          ///< Download Game Covers
        std::string covers_dir;                   ///< the covers folder (core/frame_covers.h)
        Core::Covers::Get covers_get;             ///< empty: the network; a test's own
        /// FrameAsk: the open menu's dialog, else the window's.
        std::function<void(const QString& title, const QString& body, const QString& confirm,
                           const QString& cancel, std::function<void(bool)> answer)>
            confirm;
        std::function<void(const QString& text)> toast;
    };

    FrameGames(Hooks hooks, QObject* parent);
    ~FrameGames() override;

    /// The game list's scan is done: its games from now on. The last scan's show until then.
    void Listed();
    /// A game started, or the menu opened: the tab with the favourites, the games played last and
    /// the running game of now.
    void Publish();
    /// A tile was pressed.
    void Play(const QString& path);
    void ToggleFavourite(const std::string& path);

    /// Download Thumbnails: asks, then downloads the covers these games have not.
    void DownloadThumbnails(const std::vector<std::string>& paths);
    /// Choose Another Cover's pick (asks when Download Game Covers is off and it is not on disk).
    void ChooseCover(const std::string& path, Core::Covers::Art art);
    void NoCover(const std::string& path);
    void ResetCover(const std::string& path);
    /// Use My Own Image: the picked file.
    void UseOwnImage(const std::string& path, const QString& file);
    /// Download Game Covers was turned on or off.
    void CoverSettingChanged();

private:
    enum class File { Good, Missing, Unreadable };
    static File Check(const QString& path);
    void Start(const QString& path);
    void SaveThenStart(const QString& path);

    // ---- Covers: a thread downloads and decodes them, one at a time ----
    /// One press's downloads, counted on the GUI thread.
    struct CoverBatch {
        int left = 0, found = 0;
        std::atomic_bool stopped = false; ///< no network or a limit: the rest is left out
    };
    struct CoverJob {
        std::string path;
        Core::Covers::Game game;
        Core::Covers::Art art = Core::Covers::Art::Box;
        bool fetch = false;     ///< else only from disk
        bool automatic = false; ///< by Download Game Covers: not once it is off
        bool choose = false;    ///< `art` is the game's cover from now on
        std::shared_ptr<CoverBatch> batch;
    };
    static Core::Covers::Game GameOf(const FrameXr::Menu::GameEntry& row);
    const FrameXr::Menu::GameEntry* RowOf(const std::string& path) const;
    /// The covers on disk of the rows not loaded yet, and with Download Game Covers the others.
    void FindCovers();
    void Queue(std::vector<CoverJob> jobs, bool first);
    /// The cover on disk again, of every row that is this game.
    void ReloadCover(const Core::Covers::Game& game);
    void StartDownload(const std::vector<std::string>& paths);
    void CoverThread();
    void CoverFound(const std::string& path, std::shared_ptr<const FrameXr::Menu::GameIcon> cover);
    void BatchStep(const std::shared_ptr<CoverBatch>& batch, bool found);

    Hooks hooks;
    Core::Covers::Library library;
    std::map<std::string, std::shared_ptr<const FrameXr::Menu::GameIcon>> covers; ///< by path
    QTimer covers_publish; ///< found covers show together, at most once a second
    std::set<std::string> covers_loaded;    ///< games (id and path) looked for on disk this run
    std::set<std::string> covers_automatic; ///< and asked for by themselves
    std::mutex cover_mutex;
    std::condition_variable cover_cv;
    std::deque<CoverJob> cover_jobs;
    bool cover_stop = false;
    std::atomic_bool covers_enabled = false;
    std::atomic_bool cover_cancel = false;
    std::unique_ptr<Mods::Transfer::Stopper> cover_stopper; ///< ends a request at once on quit
    std::thread cover_thread;
    std::vector<FrameXr::Menu::GameEntry> rows; ///< the last whole scan's
    FrameXr::Menu::GamesList published;
    std::set<std::string> missing; ///< paths found gone when they were pressed
};
