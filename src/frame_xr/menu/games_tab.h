// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// The Games tab: the games of every console as tiles (handoff GAMES-TAB-LAYOUT-FABLE.md, its tiles as in
// GAMES-TAB-LAYOUT-3.svg). Its list
// comes from the Qt side (citra_qt/frame_games.cpp, FrameXr::SetInGameMenuGames); a tile sends
// MenuCommand::PlayGame with the game's path, Search opens the menu's keyboard there. This file is
// its values, its focus (grid_focus.h) and its texts; games_tab.cpp draws it (TabMenu::DrawGames).
//
// The hooks: Tab::Games and Id::Game* in tab_menu.h, TabShown and the rail in tab_menu.cpp, the
// clicks and GamesInput in menu_panel.cpp.

#pragma once

#include <string>
#include <vector>

#include "frame_xr/menu/games_snapshot.h"
#include "frame_xr/menu/grid_focus.h"

namespace FrameXr::Menu {

class Kit;
struct MenuModel;
struct MenuView;
struct MenuLayout;
struct Target;

/// Texts of the layout the owner has not approved yet (its section 9): kept here only.
namespace GamesProposed {
inline constexpr const char* kSearch = "Search"; ///< the button's tooltip, the keyboard's button
inline constexpr const char* kSort = "Sort";     ///< the button's tooltip
std::string GameCount(std::size_t n, bool searching); ///< "42 Games", "1 Game", "2 Matches"
} // namespace GamesProposed

/// The line under the chips ("42 Games · Recently Played"): an open question; false leaves it out.
inline constexpr bool kGamesCountLine = true;

/// The values the tab shows.
struct GamesTabModel {
    GamesList list;
    std::string running; ///< the running game's path in the list ("" none)
    std::string search;  ///< "" none
    bool operator==(const GamesTabModel&) const = default;
};

/// What the tab looks like now.
struct GamesView {
    Common::SystemFilter chip = Common::SystemFilter::All;
    GamesSort sort = GamesSort::RecentlyPlayed;
    Focus::GridFocus focus;
    std::string focus_game; ///< the focused tile's path: the focus stays on it in a changed list
    int menu_item = 0;      ///< the focused item of the open menu (Sort's or a tile's Options)
    std::string menu_game;  ///< whose Options is open: its path
    float title_scroll = 0.0f; ///< how far the pointed at tile's cut title has moved up (dp)
    bool operator==(const GamesView&) const = default;
};

// Layout (dp; tile-relative where it says so).
constexpr int kGamesColumns = 4;
constexpr float kGameTileW = 160.0f, kGameTileH = 200.0f, kGameGap = 12.0f;
constexpr float kGamesX0 = 128.0f, kGamesX1 = 804.0f, kGamesY0 = 112.0f, kGamesY1 = 524.0f;
/// A row of tiles and its gap: what a stick's tap scrolls.
constexpr float kGameRow = kGameTileH + kGameGap;
constexpr int kGameTitleLines = 3;
constexpr float kGameTitlePitch = 18.0f;
constexpr float kGameTextW = kGameTileW - 24.0f;
/// Options' items, as indices of {GameOption}.
enum class GameAction : int { Play, Favourite };

/// The games that show now, as indices of the list.
std::vector<int> GamesShown(const GamesTabModel& m, const GamesView& v);
/// The chips and buttons over the grid, left to right: {GameChip, filter}, {GamesSearchChip},
/// {GamesSearch}, {GamesSort}, {GamesAddFolder}.
std::vector<Target> GamesTopRow(const GamesTabModel& m);
/// What the focus is on ({Id::None} while it is hidden).
Target GamesFocused(const MenuModel& model, const MenuView& view);
/// What there is to move over, with the top row as last drawn.
Focus::Shape GamesShape(const MenuModel& model, const MenuView& view, const MenuLayout& layout);
/// A direction: moves the focus (or Sort's open menu's) and scrolls its tile into view.
void GamesMove(Focus::Dir dir, const MenuModel& model, MenuView& view, const MenuLayout& layout);
/// The list, the chip, the sort or the search changed: the focus stays on its game, else at its
/// place. A chip that is gone is All.
void GamesChanged(const GamesTabModel& m, GamesView& v);
/// The tab was opened: the focus, still hidden, waits on the running game.
void GamesOpened(const GamesTabModel& m, GamesView& v);
/// A tile's tooltip: the whole title when `cut`, Continue on the running game, its marks' words.
std::string GameTooltip(const GamesTabModel& m, int index, bool cut);
/// The whole title in lines of `width`, broken at spaces; a word too long for a line goes on in the
/// next.
std::vector<std::string> GameTitleWrapped(const Kit& kit, const std::string& title, float width);
/// Its first lines, as many as a tile has, the last ending with " …" after a whole word when there
/// are more.
std::vector<std::string> GameTitleLines(const Kit& kit, const std::string& title, float width,
                                        bool* cut = nullptr);
/// Where a list at rest at `scroll` goes on to: the nearest row's edge.
float GamesRestScroll(float scroll, float max_scroll);
/// How far a title `overflow` dp too high has moved after `t` seconds: it waits, moves up slowly,
/// waits, starts again.
float TitleScroll(float t, float overflow);
/// The items of the open menu: Sort's, or Options' of the game it is open for.
std::vector<std::string> GamesMenuItems(const GamesTabModel& m, const MenuView& view);
/// The game of `path` as an index of the list, -1 not there.
int GameAt(const GamesList& list, const std::string& path);

/// The tab's icon on the rail (our own drawing of four tiles), 24 dp.
void DrawGamesIcon(Kit& kit, float cx, float cy, bool active);
/// The PokeDaisy seal, `size` dp (badge_seal.h).
void PokeDaisySeal(Kit& kit, float cx, float cy, float size, float opacity = 1.0f);

} // namespace FrameXr::Menu
