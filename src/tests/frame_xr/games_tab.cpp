// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// The Games tab: its list (menu/games_snapshot.h), its focus (menu/grid_focus.h) and its drawing
// (menu/games_tab.h).

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "frame_xr/menu/cover_art.h"
#include "frame_xr/menu/grid_focus.h"
#include "frame_xr/menu/md3.h"
#include "frame_xr/menu/tab_menu.h"
#include "frame_xr/ui/ui_canvas.h"

using namespace FrameXr::Menu;
using Common::GameSystem;
using Common::SystemFilter;

namespace {

GameEntry Game(const std::string& title, GameSystem system, const std::string& path = {}) {
    GameEntry g;
    g.title = title;
    g.file_name = title + " (File)";
    g.path = path.empty() ? "/games/" + title : path;
    g.system = system;
    return g;
}

std::vector<std::string> Titles(const GamesSnapshot& s) {
    std::vector<std::string> out;
    for (const GameEntry& g : s.games) {
        out.push_back(g.title);
    }
    return out;
}

GamesList Listed(std::vector<GameEntry> rows, const std::vector<std::string>& recent = {}) {
    return std::make_shared<const GamesSnapshot>(MakeSnapshot(std::move(rows), recent));
}

struct Drawn {
    Kit kit{1.0f};
    frame_xr::ui::Canvas canvas;
    TabMenu menu;
    std::vector<DrawnText> text;
    void Draw(const MenuModel& m, const MenuView& v) {
        text.clear();
        kit.RecordText(&text);
        canvas.Init(static_cast<int>(kPanelW), static_cast<int>(kPanelH));
        kit.Begin(canvas);
        menu.Draw(kit, m, v);
    }
    std::vector<Target> Targets() const {
        std::vector<Target> out;
        for (const auto& h : menu.Hits()) {
            out.push_back(h.t);
        }
        return out;
    }
    std::vector<MenuLayout::Hit> Of(Id id) const {
        std::vector<MenuLayout::Hit> out;
        for (const auto& h : menu.Hits()) {
            if (h.t.id == id) {
                out.push_back(h);
            }
        }
        return out;
    }
    bool Says(const std::string& s) const {
        return std::any_of(text.begin(), text.end(),
                           [&s](const DrawnText& t) { return t.text == s; });
    }
};

} // namespace

TEST_CASE("Games list: recently played first, then by title", "[frame_xr][games]") {
    const auto s = MakeSnapshot({Game("banana", GameSystem::GBA), Game("Apple", GameSystem::N3DS),
                                 Game("cherry", GameSystem::DS), Game("Date", GameSystem::GB)},
                                {"/games/Date", "/games/gone", "/games/banana"});
    CHECK(Titles(s) == std::vector<std::string>{"Date", "banana", "Apple", "cherry"});
}

TEST_CASE("Games list: one row for each file", "[frame_xr][games]") {
    GameEntry a = Game("A", GameSystem::GBA, "/one/a.gba");
    GameEntry link = Game("A", GameSystem::GBA, "/two/a.gba");
    a.real_path = link.real_path = "/one/a.gba";
    GameEntry other = Game("B", GameSystem::N3DS, "/one/b.3ds"); // no real path read
    const auto s = MakeSnapshot({a, link, other, other, GameEntry{}}, {});
    CHECK(Titles(s) == std::vector<std::string>{"A", "B"});
    CHECK(s.games[0].path == "/one/a.gba"); // the first found
}

TEST_CASE("Games list: titles the menu cannot draw, and shared ones", "[frame_xr][games]") {
    CHECK(NameDrawable("Pokémon – Ł"));
    CHECK(NameDrawable(""));
    CHECK_FALSE(NameDrawable("ゼルダ"));
    CHECK_FALSE(NameDrawable("broken \xe3\x82"));
    GameEntry jp = Game("ゼルダ", GameSystem::DS);
    jp.file_name = "Zelda (Japan)";
    GameEntry hack = Game("Emerald", GameSystem::GBA, "/hack.gba");
    hack.file_name = "My Hack";
    GameEntry retail = Game("emerald", GameSystem::GBA, "/retail.gba");
    retail.file_name = "Emerald (USA)";
    GameEntry ds = Game("Emerald", GameSystem::DS, "/other.nds"); // another system: its title stays
    GameEntry none = Game("", GameSystem::GB, "/x.gb");
    none.file_name = "X";
    const auto s = MakeSnapshot({jp, hack, retail, ds, none}, {});
    CHECK(Titles(s) == std::vector<std::string>{"Emerald", "Emerald (USA)", "My Hack", "X",
                                                "Zelda (Japan)"});
}

TEST_CASE("Games list: the tab shows only with another system than the 3DS", "[frame_xr][games]") {
    CHECK_FALSE(GamesTabApplies(nullptr));
    CHECK_FALSE(GamesTabApplies(Listed({})));
    CHECK_FALSE(GamesTabApplies(Listed({Game("A", GameSystem::N3DS), Game("B", GameSystem::N3DS)})));
    for (const GameSystem system :
         {GameSystem::DS, GameSystem::GBA, GameSystem::GB, GameSystem::GBC}) {
        CHECK(GamesTabApplies(Listed({Game("A", GameSystem::N3DS), Game("B", system)})));
    }
}

TEST_CASE("Games list: chips and what they show", "[frame_xr][games]") {
    using F = SystemFilter;
    CHECK(GameChips(nullptr).empty());
    CHECK(GameChips(Listed({Game("A", GameSystem::DS), Game("B", GameSystem::DS)})).empty());
    const auto list = Listed({Game("A", GameSystem::N3DS), Game("B", GameSystem::GB),
                              Game("C", GameSystem::GBC), Game("D", GameSystem::GBA)});
    CHECK(GameChips(list) == std::vector<F>{F::All, F::N3DS, F::GBA, F::GB});
    const auto under = [&list](F chip) {
        return GamesShown(list, chip, GamesSort::RecentlyPlayed, {});
    };
    CHECK(under(F::All) == std::vector<int>{0, 1, 2, 3});
    CHECK(under(F::GB) == std::vector<int>{1, 2});
    CHECK(under(F::DS).empty());
    CHECK(GamesShown(nullptr, F::All, GamesSort::Title, {}).empty());
    CHECK(GameChip(list, F::GB) == F::GB);
    CHECK(GameChip(list, F::DS) == F::All); // its chip went with a new list
    CHECK(std::string(ChipName(F::GB)) == "Game Boy / Color");
}

TEST_CASE("Games list: the small line", "[frame_xr][games]") {
    using Lines = std::vector<std::string>;
    GameEntry g = Game("A", GameSystem::GBC);
    CHECK(GameLines(g, false) == Lines{"Game Boy Color"});
    CHECK(GameLines(g, true) == Lines{"Game Boy Color · Running", "Running"});
    g.region = "Europe";
    CHECK(GameLines(g, false) == Lines{"Game Boy Color · Europe", "Game Boy Color"});
    g.missing = true;
    CHECK(GameLines(g, false) == Lines{"File Not Found"});
    CHECK(GameLines(g, true) == Lines{"File Not Found"});
}

TEST_CASE("Games list: sorts and the search", "[frame_xr][games]") {
    GameEntry a = Game("Apple", GameSystem::DS), b = Game("Pokémon", GameSystem::GBA),
              c = Game("cherry", GameSystem::GB);
    a.played_s = 10;
    c.favorite = true;
    c.played_s = 500;
    const auto list = Listed({a, b, c}, {"/games/Pokémon"}); // Pokémon, Apple, cherry
    const auto by = [&list](GamesSort sort, const std::string& search = {}) {
        return GamesShown(list, SystemFilter::All, sort, search);
    };
    CHECK(by(GamesSort::RecentlyPlayed) == std::vector<int>{0, 1, 2});
    CHECK(by(GamesSort::FavouritesFirst) == std::vector<int>{2, 1, 0});
    CHECK(by(GamesSort::Title) == std::vector<int>{1, 2, 0});
    CHECK(by(GamesSort::PlayTime) == std::vector<int>{2, 1, 0});
    CHECK(by(GamesSort::Title, "pokemon") == std::vector<int>{0}); // accents aside
    CHECK(by(GamesSort::Title, "POKÉ") == std::vector<int>{0});
    CHECK(by(GamesSort::Title, "(file") == std::vector<int>{1, 2, 0}); // the file's name too
    CHECK(by(GamesSort::Title, "nothing").empty());
}

TEST_CASE("Games focus: moves over the grid, the top row and the rail", "[frame_xr][games]") {
    using namespace Focus;
    Shape s{.columns = 4, .games = 10, .top = 3, .above = 2, .rail = 7, .rail_active = 1};
    GridFocus f;
    f.Move(Dir::Right, s); // the first one only shows it
    CHECK(f.shown);
    CHECK(f.tile == 0);
    f.Move(Dir::Right, s);
    f.Move(Dir::Down, s);
    CHECK(f.tile == 5);
    f.Move(Dir::Right, s);
    f.Move(Dir::Right, s);
    f.Move(Dir::Right, s); // the row's end
    CHECK(f.tile == 7);
    f.Move(Dir::Down, s); // the short last row's last game
    CHECK(f.tile == 9);
    f.Move(Dir::Down, s);
    f.Move(Dir::Right, s);
    CHECK(f.tile == 9);

    SECTION("up to the top row and back") {
        f.Move(Dir::Up, s);
        f.Move(Dir::Up, s);
        CHECK(f.tile == 1);
        f.Move(Dir::Up, s);
        CHECK(f.area == Area::Top);
        CHECK(f.top == 2); // the one above
        f.Move(Dir::Right, s);
        CHECK(f.top == 2);
        f.Move(Dir::Left, s);
        CHECK(f.top == 1);
        f.Move(Dir::Down, s);
        CHECK(f.area == Area::Grid);
        CHECK(f.tile == 1);
    }
    SECTION("left to the rail and back") {
        f.Move(Dir::Left, s);
        CHECK(f.tile == 8);
        f.Move(Dir::Left, s);
        CHECK(f.area == Area::Rail);
        CHECK(f.rail == 1);
        f.Move(Dir::Up, s);
        f.Move(Dir::Up, s);
        CHECK(f.rail == 0);
        for (int i = 0; i < 9; i++) {
            f.Move(Dir::Down, s);
        }
        CHECK(f.rail == 6);
        f.Move(Dir::Right, s);
        CHECK(f.area == Area::Grid);
        CHECK(f.tile == 8);
    }
    SECTION("the list got shorter") {
        s.games = 3;
        f.Move(Dir::Left, s);
        CHECK(f.tile == 1);
        s.games = 0;
        f.Move(Dir::Right, s);
        CHECK(f.tile == 0);
        f.Move(Dir::Down, s);
        CHECK(f.tile == 0);
    }
    SECTION("nothing above: up stays") {
        s.top = 0;
        f.tile = 2;
        f.Move(Dir::Up, s);
        CHECK(f.area == Area::Grid);
        CHECK(f.tile == 2);
    }
}

TEST_CASE("Games focus: scrolling no further than needed", "[frame_xr][games]") {
    using Focus::ScrollToShow;
    CHECK(ScrollToShow(0.0f, 0.0f, 200.0f, 420.0f, 600.0f) == 0.0f);
    CHECK(ScrollToShow(0.0f, 424.0f, 624.0f, 420.0f, 600.0f) == 204.0f);
    CHECK(ScrollToShow(300.0f, 212.0f, 412.0f, 420.0f, 600.0f) == 212.0f);
    CHECK(ScrollToShow(0.0f, 900.0f, 1100.0f, 420.0f, 600.0f) == 600.0f);
    CHECK(ScrollToShow(50.0f, 0.0f, 200.0f, 420.0f, -5.0f) == 0.0f); // all in view
}

TEST_CASE("Games tab: scrolling rests on a row's edge", "[frame_xr][games]") {
    CHECK(GamesRestScroll(0.0f, 636.0f) == 0.0f);
    CHECK(GamesRestScroll(100.0f, 636.0f) == 0.0f);
    CHECK(GamesRestScroll(130.0f, 636.0f) == kGameRow);
    CHECK(GamesRestScroll(630.0f, 636.0f) == 636.0f);
    CHECK(GamesRestScroll(50.0f, 0.0f) == 0.0f);
}

TEST_CASE("Games focus: a held direction repeats", "[frame_xr][games]") {
    using namespace Focus;
    DirRepeat r;
    std::int64_t t = 0;
    const auto at = [&](std::int64_t ms, Dir dpad, float x = 0.0f, float y = 0.0f) {
        t = ms * 1'000'000;
        return r.Update(true, dpad, x, y, t);
    };
    CHECK(r.Update(false, Dir::Down, 0, 0, 0) == Dir::None);
    CHECK(at(10, Dir::Down) == Dir::None); // held from before
    CHECK(at(900, Dir::Down) == Dir::None);
    CHECK(at(910, Dir::None) == Dir::None);
    CHECK(at(920, Dir::Down) == Dir::Down);
    CHECK(at(1300, Dir::Down) == Dir::None);
    CHECK(at(1321, Dir::Down) == Dir::Down);
    CHECK(at(1400, Dir::Down) == Dir::None);
    CHECK(at(1432, Dir::Down) == Dir::Down);
    CHECK(at(1440, Dir::None) == Dir::None);
    // the stick: past 0.6, until below 0.4 along the same way
    CHECK(at(1500, Dir::None, 0.5f, 0.0f) == Dir::None);
    CHECK(at(1510, Dir::None, 0.7f, 0.2f) == Dir::Right);
    CHECK(at(1520, Dir::None, 0.45f, -0.9f) == Dir::None); // still right
    CHECK(at(1530, Dir::None, 0.3f, -0.9f) == Dir::Down);
    CHECK(at(1540, Dir::None, 0.0f, 0.0f) == Dir::None);
    CHECK(at(1550, Dir::Left, 0.9f, 0.0f) == Dir::Left); // the D-pad first
}

namespace {

GameEntry With(GameEntry g, void (*change)(GameEntry&)) {
    change(g);
    return g;
}

/// A list with every state of a tile, on the Games tab.
MenuModel TabModel(int more = 0) {
    std::vector<GameEntry> rows{
        Game("Super Princess Adventure Deluxe International Edition of the Century", GameSystem::N3DS, "/3ds"),
        With(Game("Emerald", GameSystem::GBA, "/gba"),
             [](GameEntry& g) {
                 g.region = "North America";
                 g.two_screens = g.pokedaisy = g.favorite = true;
             }),
        With(Game("Crystal", GameSystem::GBC, "/gbc"), [](GameEntry& g) { g.region = "Europe"; }),
        With(Game("Gone", GameSystem::DS, "/ds"), [](GameEntry& g) { g.missing = true; }),
    };
    for (int i = 0; i < more; i++) {
        rows.push_back(Game("More " + std::to_string(i), GameSystem::GB));
    }
    MenuModel m;
    m.game_title = "Emerald";
    m.games.list = Listed(std::move(rows));
    m.games.running = "/gbc";
    return m;
}

MenuView TabView() {
    MenuView v;
    v.tab = Tab::Games;
    return v;
}

int PlaceOf(const MenuModel& m, const std::string& path) {
    const auto& games = m.games.list->games;
    for (std::size_t i = 0; i < games.size(); i++) {
        if (games[i].path == path) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool Inside(const Rect& r, const Rect& in) {
    return r.x0 >= in.x0 - 0.01f && r.x1 <= in.x1 + 0.01f && r.y0 >= in.y0 - 0.01f &&
           r.y1 <= in.y1 + 0.01f;
}

} // namespace

TEST_CASE("Games tab: the rail is as it was without other systems' games", "[frame_xr][games]") {
    Drawn d;
    MenuModel m;
    m.game_title = "Game";
    d.Draw(m, {});
    const auto none = d.Of(Id::TabButton);
    REQUIRE(none.size() == 7);
    CHECK(none[0].r.H() == 72.0f);
    CHECK(none[1].t.index == static_cast<int>(Tab::Position));
    const auto targets = d.Targets();
    const auto pixels = d.canvas.px;

    m.games.list = Listed({Game("A", GameSystem::N3DS), Game("B", GameSystem::N3DS)});
    d.Draw(m, {});
    CHECK(d.Targets() == targets);
    CHECK(d.canvas.px == pixels);

    m.games.list = Listed({Game("A", GameSystem::N3DS), Game("B", GameSystem::DS)});
    d.Draw(m, {});
    const auto tabs = d.Of(Id::TabButton);
    REQUIRE(tabs.size() == 8);
    CHECK(tabs[1].t.index == static_cast<int>(Tab::Games));
    for (const auto& t : tabs) {
        CHECK(t.r.H() == 64.0f);
    }
    CHECK(tabs[0].r.y0 == 24.0f);
    CHECK(tabs[7].r.y1 == kPanelH - 24.0f);
}

TEST_CASE("Games tab: tiles, chips and buttons", "[frame_xr][games]") {
    Drawn d;
    const MenuModel m = TabModel(8);
    d.Draw(m, TabView());
    const auto tiles = d.Of(Id::GameTile);
    REQUIRE(tiles.size() >= 8);
    CHECK(tiles[0].r.x0 == 128.0f);
    CHECK(tiles[0].r.y0 == 112.0f);
    CHECK(tiles[0].r.W() == 160.0f);
    CHECK(tiles[0].r.H() == 200.0f);
    CHECK(tiles[3].r.x0 == 644.0f);
    CHECK(tiles[4].r.y0 == 324.0f);
    const auto chips = d.Of(Id::GameChip);
    REQUIRE(chips.size() == 5);
    CHECK(chips[0].r.W() == 64.0f);
    const auto search = d.Of(Id::GamesSearch), sort = d.Of(Id::GamesSort);
    REQUIRE((search.size() == 1 && sort.size() == 1));
    CHECK(chips[4].r.x1 < search[0].r.x0);
    CHECK(search[0].r.x1 <= sort[0].r.x0);
    for (const auto& h : d.menu.Hits()) {
        if (h.t.id == Id::Scrollbar) {
            continue; // every list's
        }
        CHECK(h.r.W() >= 48.0f);
        if (h.t.id != Id::GameTile) { // those in view partly are cut
            CHECK(h.r.H() >= 48.0f);
        }
    }
    CHECK(d.menu.Layout().games == m.games.list);
    CHECK(d.menu.Layout().max_scroll == 212.0f * 3.0f - 12.0f - 412.0f);
}

TEST_CASE("Games tab: the texts fit", "[frame_xr][games]") {
    Drawn d;
    MenuModel m = TabModel();
    d.Draw(m, TabView());
    for (const auto& t : d.text) {
        if (t.box.x0 >= kGamesX0) {
            CHECK(Inside(t.box, {kGamesX0, 30.0f, kGamesX1, kGamesY1}));
        }
    }
    CHECK(d.Says("Two Screens"));
    CHECK(d.Says("File Not Found"));
    CHECK(d.Says("Running")); // "Game Boy Color · Running" does not fit
    CHECK(d.Says("4 Games · Recently Played") == kGamesCountLine);
    for (const auto& t : d.text) { // the small line is at one height on every tile
        if (t.box.y0 < kGamesY0) {
            continue; // the chips
        }
        if (t.text == "File Not Found" || t.text == "Running" || t.text.starts_with("GBA")) {
            CHECK(std::abs(t.box.y1 - (112.0f + 190.0f)) < 0.01f);
        }
    }

    CHECK(GameTooltip(m.games, PlaceOf(m, "/gbc"), false) == "Continue");
    CHECK(GameTooltip(m.games, PlaceOf(m, "/gba"), false) == "In Favourites · PokeDaisy");
    CHECK(GameTooltip(m.games, 99, true).empty());
}

TEST_CASE("Games tab: titles in three lines", "[frame_xr][games]") {
    Drawn d;
    const auto lines = [&d](const std::string& title, bool* cut) {
        return GameTitleLines(d.kit, title, kGameTextW, cut);
    };
    bool cut = false;
    for (const char* title :
         {"Dragon Quest IX: Sentinels of the Starry Skies",
          "Pokémon Yellow Version: Special Pikachu Edition",
          "The Legend of Zelda: A Link to the Past & Four Swords",
          "Transformers: War for Cybertron Autobots", "Harry Potter and the Sorcerer's Stone",
          "Dragon Ball Z: Attack of the Saiyans", "Ni no Kuni: Shikkoku no Madoushi",
          "Pokémon Mystery Dungeon: Blue Rescue Team", "The Legend of Zelda: The Minish Cap",
          "Pokémon Heart and Soul", "Mario Kart DS"}) {
        const auto l = lines(title, &cut);
        INFO(title);
        CHECK_FALSE(cut);
        CHECK(l.size() <= 3);
        std::string joined;
        for (const std::string& part : l) {
            CHECK(d.kit.TextWidth(Type::RailLabel, part) <= kGameTextW);
            joined += (joined.empty() ? "" : " ") + part;
        }
        CHECK(joined == title); // broken at spaces only
    }
    using Lines = std::vector<std::string>;
    for (const std::string& part : lines("Metroid - Zero Mission - The Other One - X", &cut)) {
        CHECK_FALSE(part.starts_with("-")); // a dash stays with the word before it
    }
    CHECK(lines("Emerald", &cut) == Lines{"Emerald"});
    CHECK(lines("", &cut) == Lines{""});

    // Cut between words
    const std::string longest = "Harry Potter and the Sorcerer's Stone: Extra Long Title of a Special Edition Here";
    const auto l = lines(longest, &cut);
    CHECK(cut);
    REQUIRE(l.size() == 3);
    REQUIRE(l[2].ends_with(" …"));
    const std::string kept = l[0] + " " + l[1] + " " + l[2].substr(0, l[2].size() - 4);
    CHECK(longest.starts_with(kept + " "));
    for (const std::string& part : l) {
        CHECK(d.kit.TextWidth(Type::RailLabel, part) <= kGameTextW);
    }
    const auto whole = GameTitleWrapped(d.kit, longest, kGameTextW);
    CHECK(whole.size() > 3);
    CHECK(whole.back().ends_with("Here"));

    // A word longer than a line, and one longer than the tile
    for (const std::string& word :
         {std::string(30, 'm'), std::string(200, 'm'), "é" + std::string(40, 'W')}) {
        for (const std::string& part : lines(word, &cut)) {
            CHECK(d.kit.TextWidth(Type::RailLabel, part) <= kGameTextW);
        }
    }
}

TEST_CASE("Games tab: a cut title scrolls by", "[frame_xr][games]") {
    CHECK(TitleScroll(5.0f, 0.0f) == 0.0f);
    CHECK(TitleScroll(0.0f, 36.0f) == 0.0f);
    CHECK(TitleScroll(1.0f, 36.0f) == 0.0f); // waits first
    const float mid = TitleScroll(2.5f, 36.0f);
    CHECK((mid > 0.0f && mid < 36.0f));
    CHECK(TitleScroll(4.5f, 36.0f) == 36.0f); // and at the end
    CHECK(TitleScroll(1.2f + 36.0f / 14.0f + 1.2f + 0.1f, 36.0f) == 0.0f);

    Drawn d;
    MenuModel m = TabModel();
    MenuView v = TabView();
    d.Draw(m, v);
    const auto& cut = d.menu.Layout().cut_titles;
    REQUIRE(cut.size() == 1);
    CHECK(cut[0].first == PlaceOf(m, "/3ds"));
    v.hover = {Id::GameTile, cut[0].first};
    v.games.title_scroll = cut[0].second;
    d.Draw(m, v);
    const auto whole = GameTitleWrapped(d.kit, m.games.list->games[static_cast<std::size_t>(cut[0].first)].title,
                                        kGameTextW);
    CHECK(d.Says(whole.back()));
}

TEST_CASE("Games tab: Options of a tile", "[frame_xr][games]") {
    Drawn d;
    const MenuModel m = TabModel();
    MenuView v = TabView();
    d.Draw(m, v);
    REQUIRE(d.Of(Id::GameOptions).size() == 4);
    for (const auto& o : d.Of(Id::GameOptions)) { // over its tile, with the pointer on it or not
        CHECK(d.menu.Layout().HitTest(o.r.Cx(), o.r.Cy()) == o.t);
        CHECK((o.r.W() == 48.0f && o.r.H() == 48.0f));
    }
    CHECK(GamesMenuItems(m.games, v).empty());
    v.open_menu = Id::GameOption;
    v.games.menu_game = "/gba";
    using Items = std::vector<std::string>;
    const Items covers{"Choose Another Cover", "No Cover", "Use My Own Image", "Reset Cover"};
    const auto with_covers = [&covers](Items items) {
        items.insert(items.end(), covers.begin(), covers.end());
        return items;
    };
    CHECK(GamesMenuItems(m.games, v) == with_covers({"Play", "Favourite"}));
    for (const auto& g : m.games.list->games) { // the first row's and the second row's
        v.games.menu_game = g.path;
        d.Draw(m, v);
        const auto items = d.Of(Id::GameOption);
        REQUIRE(items.size() == 6);
        for (const auto& h : items) {
            CHECK(d.menu.Layout().HitTest(h.r.Cx(), h.r.Cy()) == h.t);
            CHECK(Inside(h.r, {kGamesX0, 0, kPanelW, kPanelH}));
            const std::string text =
                GamesMenuItems(m.games, v)[static_cast<std::size_t>(h.t.index)];
            CHECK(d.kit.Fit(Type::Label, text, h.r.W() - 72.0f) == text); // as Kit::Menu fits it
        }
    }
    v.games.menu_game = "/gba";
    v.games.menu_covers = true; // Choose Another Cover's
    CHECK(GamesMenuItems(m.games, v) == Items{"Box Art", "Title Screen", "Screenshot"});
    d.Draw(m, v);
    CHECK(d.Of(Id::GameOption).size() == 3);
    v.games.menu_covers = false;
    v.games.menu_game = "/gbc"; // the running one
    CHECK(GamesMenuItems(m.games, v) == with_covers({"Continue", "Favourite"}));
    GamesMove(Focus::Dir::Down, m, v, d.menu.Layout());
    CHECK(GamesFocused(m, v) == Target{Id::GameOption, 1});
    for (int i = 0; i < 8; i++) {
        GamesMove(Focus::Dir::Down, m, v, d.menu.Layout());
    }
    CHECK(GamesFocused(m, v) == Target{Id::GameOption, 5});
    v.games.menu_game = "/gone"; // the list changed under it
    d.Draw(m, v);
    CHECK(d.Of(Id::GameOption).empty());
}

TEST_CASE("Games tab: the covers' rows in Settings", "[frame_xr][games]") {
    Drawn d;
    MenuModel m = TabModel();
    const auto drawn = [&d, &m](Id id) {
        MenuView v;
        v.tab = Tab::Settings;
        for (float scroll = 0.0f; scroll <= 3000.0f; scroll += kRowH) {
            v.scroll[static_cast<std::size_t>(Tab::Settings)] = scroll;
            d.Draw(m, v);
            if (!d.Of(id).empty()) {
                return true;
            }
        }
        return false;
    };
    CHECK(drawn(Id::DownloadGameCovers));
    CHECK(drawn(Id::DownloadThumbnails));
    m.games = {}; // no Games tab, as with 3DS games alone
    CHECK_FALSE(drawn(Id::GamesHeader));
    CHECK_FALSE(drawn(Id::DownloadGameCovers));
    CHECK_FALSE(drawn(Id::DownloadThumbnails));
    CHECK(drawn(Id::MoreSettings));
}

TEST_CASE("Games tab: a cover in its art area", "[frame_xr][games]") {
    CHECK(ComposeCover({}).rgba.empty());
    GameIcon broken;
    broken.w = broken.h = 10;
    CHECK(ComposeCover(broken).rgba.empty());

    GameIcon tall; // red over green, half as wide as high
    tall.w = 100;
    tall.h = 200;
    for (int y = 0; y < tall.h; y++) {
        for (int x = 0; x < tall.w; x++) {
            const bool top = y < 100;
            tall.rgba.insert(tall.rgba.end(), {static_cast<uint8_t>(top ? 200 : 0),
                                               static_cast<uint8_t>(top ? 0 : 200), 0, 255});
        }
    }
    const auto at = [](const GameIcon& i, int x, int y) {
        const std::size_t p = static_cast<std::size_t>(y * i.w + x) * 4;
        return std::vector<int>{i.rgba[p], i.rgba[p + 1], i.rgba[p + 2], i.rgba[p + 3]};
    };
    const GameIcon whole = ComposeCover(tall);
    REQUIRE((whole.w == kCoverPxW && whole.h == kCoverPxH));
    CHECK(at(whole, kCoverPxW / 2, 2) == std::vector<int>{200, 0, 0, 255}); // all of it
    CHECK(at(whole, kCoverPxW / 2, kCoverPxH - 2) == std::vector<int>{0, 200, 0, 255});
    const auto side = at(whole, 4, 4); // behind: dimmer, opaque
    CHECK(side[3] == 255);
    CHECK((side[0] > 0x1D && side[0] < 160));
}

TEST_CASE("Games tab: a search", "[frame_xr][games]") {
    Drawn d;
    MenuModel m = TabModel();
    m.games.search = "cry";
    MenuView v = TabView();
    d.Draw(m, v);
    CHECK(d.Of(Id::GamesSearch).empty());
    CHECK(d.Of(Id::GamesSearchChip).size() == 1);
    CHECK(d.Of(Id::GamesSearchClear).size() == 1);
    CHECK(d.Of(Id::GameChip).size() == 4); // no All
    REQUIRE(d.Of(Id::GameTile).size() == 1);
    CHECK(d.Of(Id::GameTile)[0].t.index == PlaceOf(m, "/gbc"));
    CHECK(d.Says("1 Match · Recently Played") == kGamesCountLine);
    const auto close = d.Of(Id::GamesSearchClear)[0].r;
    CHECK(d.menu.Layout().HitTest(close.Cx(), close.Cy()) == Target{Id::GamesSearchClear});
    for (const auto& h : d.Of(Id::GameChip)) {
        CHECK(h.r.x1 < d.Of(Id::GamesSort)[0].r.x0);
    }

    m.games.search = "a very long search that has to be cut";
    d.Draw(m, v);
    CHECK(d.Of(Id::GamesSearchChip)[0].r.W() <= 137.0f);
    CHECK(d.Says("No Matches"));
    CHECK(d.Says("Clear Search"));
    CHECK(d.Of(Id::GameTile).empty());
}

TEST_CASE("Games tab: Sort's menu", "[frame_xr][games]") {
    Drawn d;
    const MenuModel m = TabModel();
    MenuView v = TabView();
    v.open_menu = Id::GamesSort;
    d.Draw(m, v);
    int items = 0;
    for (const auto& h : d.Of(Id::GamesSort)) {
        items += h.t.index >= 0 ? 1 : 0;
        if (h.t.index >= 0) {
            CHECK(d.menu.Layout().HitTest(h.r.Cx(), h.r.Cy()) == h.t);
        }
    }
    CHECK(items == 4);
    CHECK(d.Says("Play Time"));
}

TEST_CASE("Games tab: the focus stays on its game", "[frame_xr][games]") {
    Drawn d;
    MenuModel m = TabModel(20);
    MenuView v = TabView();
    GamesOpened(m.games, v.games);
    CHECK(GamesFocused(m, v) == Target{}); // hidden until a direction
    d.Draw(m, v);
    GamesMove(Focus::Dir::Down, m, v, d.menu.Layout());
    CHECK(GamesFocused(m, v) == Target{Id::GameTile, PlaceOf(m, "/gbc")}); // the running game
    const int row = v.games.focus.tile / kGamesColumns;
    for (int i = 0; i < 5; i++) {
        GamesMove(Focus::Dir::Down, m, v, d.menu.Layout());
    }
    CHECK(v.games.focus.tile / kGamesColumns > row);
    CHECK(v.scroll[static_cast<std::size_t>(Tab::Games)] > 0.0f);
    const Target was = GamesFocused(m, v);
    const std::string path = m.games.list->games[static_cast<std::size_t>(was.index)].path;

    v.games.sort = GamesSort::Title;
    GamesChanged(m.games, v.games);
    CHECK(GamesFocused(m, v) == was);

    v.games.chip = SystemFilter::GB;
    GamesChanged(m.games, v.games);
    const Target now = GamesFocused(m, v);
    REQUIRE(now.id == Id::GameTile);
    if (Common::FilterMatches(SystemFilter::GB,
                              m.games.list->games[static_cast<std::size_t>(was.index)].system)) {
        CHECK(m.games.list->games[static_cast<std::size_t>(now.index)].path == path);
    }

    m.games.list = Listed({Game("Only", GameSystem::DS)}); // the chip went
    GamesChanged(m.games, v.games);
    CHECK(v.games.chip == SystemFilter::All);
    CHECK(GamesFocused(m, v) == Target{Id::GameTile, 0});
}

TEST_CASE("Games tab: up goes to what is above", "[frame_xr][games]") {
    Drawn d;
    const MenuModel m = TabModel();
    MenuView v = TabView();
    d.Draw(m, v);
    v.games.focus.shown = true;
    v.games.focus.tile = 3; // under Search and Sort
    GamesMove(Focus::Dir::Up, m, v, d.menu.Layout());
    CHECK(GamesFocused(m, v) == Target{Id::GamesSort});
    GamesMove(Focus::Dir::Right, m, v, d.menu.Layout());
    CHECK(GamesFocused(m, v) == Target{Id::GamesAddFolder});
    GamesMove(Focus::Dir::Left, m, v, d.menu.Layout());
    GamesMove(Focus::Dir::Left, m, v, d.menu.Layout());
    CHECK(GamesFocused(m, v) == Target{Id::GamesSearch});
    GamesMove(Focus::Dir::Down, m, v, d.menu.Layout());
    v.games.focus.tile = 0;
    GamesMove(Focus::Dir::Up, m, v, d.menu.Layout());
    CHECK(GamesFocused(m, v) ==
          Target{Id::GameChip, static_cast<int>(SystemFilter::N3DS)}); // All ends before its middle
    for (int i = 0; i < 2; i++) {
        GamesMove(Focus::Dir::Left, m, v, d.menu.Layout());
    }
    CHECK(GamesFocused(m, v).id == Id::TabButton);
}
