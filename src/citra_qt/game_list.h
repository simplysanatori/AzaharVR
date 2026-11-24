// Copyright 2015-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <QMenu>
#include <QPushButton>
#include <QString>
#include <QVector>
#include <QWidget>
#include "citra_qt/compatibility_list.h"
#include "common/common_types.h"
#include "common/play_time_manager.h"
#include "uisettings.h"
#ifdef ENABLE_ALT_CORES
#include "frame_cores/rom_info.h"
#endif

namespace Service::FS {
enum class MediaType : u32;
}

class GameListWorker;
class GameListDir;
class GameListSearchField;
class GameListTreeView;
class GMainWindow;
class QFileSystemWatcher;
class QHBoxLayout;
class QLabel;
class QLineEdit;
template <typename>
class QList;
class QModelIndex;
class QStandardItem;
class QStandardItemModel;
class QTreeView;
class QToolButton;
class QVBoxLayout;

enum class GameListOpenTarget {
    SAVE_DATA = 0,
    EXT_DATA = 1,
    APPLICATION = 2,
    UPDATE_DATA = 3,
    TEXTURE_DUMP = 4,
    TEXTURE_LOAD = 5,
    MODS = 6,
    DLC_DATA = 7,
    SHADER_CACHE = 8
};

enum class GameListShortcutTarget {
    Desktop,
    Applications,
};

class GameList : public QWidget {
    Q_OBJECT

public:
    enum {
        COLUMN_NAME,
        COLUMN_COMPATIBILITY,
        COLUMN_REGION,
        COLUMN_FILE_TYPE,
        COLUMN_SIZE,
        COLUMN_PLAY_TIME,
#ifdef ENABLE_ALT_CORES
        COLUMN_SYSTEM, // last, so a saved column layout still fits
#endif
        COLUMN_COUNT, // Number of columns
    };

    explicit GameList(PlayTime::PlayTimeManager& play_time_manager_, GMainWindow* parent = nullptr);
    ~GameList() override;

    QString GetLastFilterResultItem() const;
    void ClearFilter();
    void SetFilterFocus();
    void SetFilterVisible(bool visibility);
    void SetDirectoryWatcherEnabled(bool enabled);
    bool IsEmpty() const;

    void LoadCompatibilityList();
    void PopulateAsync(QVector<UISettings::GameDir>& game_dirs);

    void SaveInterfaceLayout();
    void LoadInterfaceLayout();

    QStandardItemModel* GetModel() const;

    QString FindGameByProgramID(u64 program_id, int role);

    void RefreshGameDirectory();
    /// AzaharFrame ctgp7b: the scan a folder change asked for while a game ran (or a row's .app
    /// was found gone), once the game has stopped; `force` scans anyway. Returns whether it scans.
    bool RefreshIfPending(bool force = false);

#ifdef ENABLE_ALT_CORES
    /// A DS, GBA or Game Boy game runs: folder changes wait for it, as they do for a 3DS game.
    void SetAltGameRunning(bool running);
    /// That game stopped: its play time in the rows again, and the scan a folder change asked for.
    void AltGameStopped(u64 program_id);
#endif

    void ToggleFavorite(u64 program_id);
    void AddFavorite(u64 program_id);
    void RemoveFavorite(u64 program_id);

    static const QStringList supported_file_extensions;

signals:
    void GameChosen(const QString& game_path);
    void ShouldCancelWorker();
    void OpenFolderRequested(u64 program_id, GameListOpenTarget target);
    void CreateShortcut(u64 program_id, const std::string& game_path,
                        GameListShortcutTarget target);
    void RemovePlayTimeRequested(u64 program_id);
    void OpenPerGameGeneralRequested(const QString file);
    void DumpRomFSRequested(QString game_path, u64 program_id);
    void OpenDirectory(const QString& directory);
    void AddDirectory();
    void ShowList(bool show);
    void PopulatingCompleted();
    void GameDirsChanged(); // AzaharFrame: a folder removed or its subfolder scan toggled
#ifdef ENABLE_DEVELOPER_OPTIONS
    void StartingLaunchStressTest(const QString& game_path);
#endif

private slots:
    void OnItemExpanded(const QModelIndex& item);
    void OnTextChanged(const QString& new_text);
    void OnFilterCloseClicked();
    void OnUpdateThemedIcons();

private:
    void AddDirEntry(GameListDir* entry_items);
    void AddEntry(const QList<QStandardItem*>& entry_items, GameListDir* parent);
    void ValidateEntry(const QModelIndex& item);
    void DonePopulating(const QStringList& watch_list);

    void PopupContextMenu(const QPoint& menu_location);
    void PopupHeaderContextMenu(const QPoint& menu_location);
    void AddGamePopup(QMenu& context_menu, const QString& path, const QString& name, u64 program_id,
                      u64 extdata_id, Service::FS::MediaType media_type, bool can_insert);
    void AddCustomDirPopup(QMenu& context_menu, QModelIndex selected);
    void AddPermDirPopup(QMenu& context_menu, QModelIndex selected);
    void AddFavoritesPopup(QMenu& context_menu);
    void UpdateColumnVisibility();
    bool AltGameRunning() const {
#ifdef ENABLE_ALT_CORES
        return alt_game_running;
#else
        return false;
#endif
    }
#ifdef ENABLE_ALT_CORES
    void BuildSystemChips();
    void UpdateSystemChips();
    void ApplySystemFilter();
    bool PassesSystemFilter(const QStandardItem* item) const;
#endif

    QString FindGameByProgramID(QStandardItem* current_item, u64 program_id, int role);

    void changeEvent(QEvent*) override;
    void RetranslateUI();

    GameListSearchField* search_field;
    GMainWindow* main_window = nullptr;
    QVBoxLayout* layout = nullptr;
    GameListTreeView* tree_view = nullptr;
    QStandardItemModel* item_model = nullptr;
    GameListWorker* current_worker = nullptr;
    QFileSystemWatcher* watcher = nullptr;
    CompatibilityList compatibility_list;

    friend class GameListSearchField;

    const PlayTime::PlayTimeManager& play_time_manager;

    std::chrono::time_point<std::chrono::steady_clock> time_last_refresh;
    bool refresh_pending = false; ///< AzaharFrame ctgp7b: RefreshIfPending
#ifdef ENABLE_ALT_CORES
    QWidget* chip_bar = nullptr;
    QVector<QToolButton*> chips; ///< by FrameCores::SystemFilter
    FrameCores::SystemFilter system_filter = FrameCores::SystemFilter::All; ///< in effect
    bool has_alt_games = false;
    bool alt_game_running = false;
#endif
};

class GameListPlaceholder : public QWidget {
    Q_OBJECT
public:
    explicit GameListPlaceholder(GMainWindow* parent = nullptr);
    ~GameListPlaceholder();

signals:
    void AddDirectory();

private slots:
    void onUpdateThemedIcons();

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QVBoxLayout* layout = nullptr;
    QLabel* image = nullptr;
    QLabel* text = nullptr;
};
