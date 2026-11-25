// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// The VR menu's Games tab, the Qt side: publishes the desktop game list's games to the menu
// (FrameXr::SetInGameMenuGames) and starts the one pressed: its file is checked before the running
// game closes, and the player is asked.

#pragma once

#include <functional>
#include <set>
#include <string>
#include <vector>
#include <QObject>
#include <QString>

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
    };

    FrameGames(Hooks hooks, QObject* parent);

    /// The game list's scan is done: its games from now on. The last scan's show until then.
    void Listed();
    /// A game started, or the menu opened: the tab with the favourites, the games played last and
    /// the running game of now.
    void Publish();
    /// A tile was pressed.
    void Play(const QString& path);
    void ToggleFavourite(const std::string& path);

private:
    enum class File { Good, Missing, Unreadable };
    static File Check(const QString& path);
    void Start(const QString& path);
    void SaveThenStart(const QString& path);

    Hooks hooks;
    std::vector<FrameXr::Menu::GameEntry> rows; ///< the last whole scan's
    FrameXr::Menu::GamesList published;
    std::set<std::string> missing; ///< paths found gone when they were pressed
};
