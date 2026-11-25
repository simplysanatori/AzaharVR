// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// The Games tab (games_tab.h), Material 3:
//
//   [v All] [3DS] [DS] [GBA] [Game Boy / Color]        (search) (sort)
//   42 Games - Recently Played
//   +----------+ +----------+ +----------+ +----------+
//   |(*) art   | |      (:) | |          | |          |   the cover or icon, the favourite's star,
//   |  (pill)  | |          | |          | |          |   Options while pointed at, Two Screens
//   | Title    | |          | |          | |          |   three lines at most
//   | GBA - .. | |          | |          | |          |   the system and the region
//   +----------+ +----------+ +----------+ +----------+
//
// Scanning is a later step: its place stays free.

#include <algorithm>
#include <cmath>
#include <utility>

#include "frame_xr/menu/badge_seal.h"
#include "frame_xr/menu/games_tab.h"
#include "frame_xr/menu/md3_fonts.h"
#include "frame_xr/menu/tab_menu.h"
#include "frame_xr/ui/ui_ttf.h"

namespace FrameXr::Menu {

namespace Sym = ::FrameXr::Menu::Icon;
using Common::SystemFilter;

namespace {

constexpr float kTopY0 = 36.0f, kTopY1 = 84.0f;   ///< the chips and buttons
constexpr float kCountCy = 97.0f;                 ///< the count line
constexpr float kChipGap = 8.0f, kChipMinW = 64.0f, kChipRoom = 44.0f, kChipRadius = 12.0f;
constexpr float kSearchChipRoom = 64.0f, kSearchChipMaxW = 137.0f, kSearchChipClose = 48.0f;
constexpr float kSearchCx = 668.0f, kSortCx = 724.0f, kAddFolderCx = 780.0f; ///< round buttons
constexpr float kRoundR = 24.0f, kRoundTarget = 28.0f;
constexpr float kSortMenuW = 220.0f;
constexpr float kBarRight = 822.0f;  ///< what the scrollbar hangs off
constexpr Type kTitle = Type::RailLabel; ///< 14, Medium
constexpr float kRing = 3.0f, kRingGap = 2.0f; ///< the focus ring and its distance
constexpr float kRingRoom = kRing + kRingGap + 1.0f; ///< the grid's clip is this much wider

// In a tile
constexpr float kTileRadius = 16.0f;
constexpr float kArtX = 8.0f, kArtY = 8.0f, kArtW = 144.0f, kArtH = 104.0f, kArtRadius = 10.0f;
constexpr float kIcon = 64.0f, kIconRadius = 6.0f;
constexpr float kMarkAt = 24.0f;       ///< the star's circle: from the left and the top
constexpr float kMarkR = 13.0f, kStar = 18.0f;
constexpr float kOptionsR = 14.0f, kOptionsTarget = 48.0f;
constexpr float kMarkInk = 0.86f;      ///< the circles' opacity
constexpr float kPillY0 = 90.0f, kPillY1 = 114.0f, kPillEdge = 2.0f;
constexpr float kPillSymbol = 16.0f;   ///< the symbol's centre from the pill's start
constexpr float kPillText = 30.0f, kPillEnd = 9.0f, kPillSeal = 18.0f, kPillGlyph = 14.0f;
constexpr float kTitleBase = 132.0f, kSmallBase = 190.0f, kSmall = 13.0f;
constexpr float kOptionsMenuW = 160.0f; ///< as the tile
constexpr const char* kTitleCut = " \u2026";

Rect Grown(const Rect& r, float by) {
    return {r.x0 - by, r.y0 - by, r.x1 + by, r.y1 + by};
}

/// The focus ring off a rounded `r`.
void FocusRing(Kit& kit, const Rect& r, float radius) {
    const float off = kRing + kRingGap;
    kit.Stroke(Grown(r, off), radius + off, kRing, Tok::kPrimary);
}

/// Symbols of our own drawing, until md3_fonts.h has search, sort and grid_view.
void SearchGlyph(Kit& kit, float cx, float cy, uint32_t rgb, float size = 24.0f) {
    const float u = size / 24.0f;
    const float x = cx - 2.5f * u, y = cy - 2.5f * u, r = 6.5f * u;
    kit.Stroke({x - r, y - r, x + r, y + r}, r, 2.0f * u, rgb);
    kit.Line(x + 4.8f * u, y + 4.8f * u, cx + 8.0f * u, cy + 8.0f * u, 2.0f * u, rgb);
}

void SortGlyph(Kit& kit, float cx, float cy, uint32_t rgb) {
    kit.Line(cx - 8.0f, cy - 5.0f, cx + 8.0f, cy - 5.0f, 2.0f, rgb);
    kit.Line(cx - 8.0f, cy, cx + 3.0f, cy, 2.0f, rgb);
    kit.Line(cx - 8.0f, cy + 5.0f, cx - 3.0f, cy + 5.0f, 2.0f, rgb);
}

void AddFolderGlyph(Kit& kit, float cx, float cy, uint32_t rgb) {
    kit.Stroke({cx - 10.0f, cy - 6.0f, cx + 10.0f, cy + 8.0f}, 2.0f, 2.0f, rgb);
    kit.Fill({cx - 10.0f, cy - 8.0f, cx - 1.0f, cy - 4.0f}, 2.0f, rgb); // its tab
    kit.Line(cx + 1.5f, cy + 1.0f, cx + 6.5f, cy + 1.0f, 2.0f, rgb);
    kit.Line(cx + 4.0f, cy - 1.5f, cx + 4.0f, cy + 3.5f, 2.0f, rgb);
}

/// Two stacked rectangles: the Two Screens mark.
void TwoScreensGlyph(Kit& kit, float x, float cy, uint32_t rgb, float opacity) {
    const float w = kPillGlyph, h = 6.0f;
    kit.Stroke({x, cy - 7.0f, x + w, cy - 7.0f + h}, 1.5f, 1.5f, rgb, opacity);
    kit.Stroke({x, cy + 1.0f, x + w, cy + 1.0f + h}, 1.5f, 1.5f, rgb, opacity);
}

/// The first letters of a title's words, three at most.
std::string Initials(const std::string& title) {
    std::string out;
    int letters = 0;
    bool word = true;
    for (std::size_t i = 0; i < title.size() && letters < 3;) {
        const char32_t cp = frame_xr::ui::NextUtf8(title, i);
        const bool letter = (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') ||
                            (cp >= 'a' && cp <= 'z') || cp >= 0xC0;
        if (letter && word) {
            out += frame_xr::ui::Utf8Of(cp >= 'a' && cp <= 'z' ? cp - 32 : cp);
            letters++;
        }
        word = !letter && cp != '\'';
    }
    return out;
}

float ChipWidth(const Kit& kit, SystemFilter chip) {
    return std::max(kChipMinW, std::ceil(kit.TextWidth(Type::Label, ChipName(chip)) + kChipRoom));
}

/// Where the chips are: {GameChip, filter} and {GamesSearchChip}.
std::vector<std::pair<Target, Rect>> PlaceChips(const Kit& kit, const GamesTabModel& m) {
    std::vector<std::pair<Target, Rect>> out;
    float x = kGamesX0;
    for (const Target& t : GamesTopRow(m)) {
        float w = 0.0f;
        if (t.id == Id::GameChip) {
            w = ChipWidth(kit, static_cast<SystemFilter>(t.index));
        } else if (t.id == Id::GamesSearchChip) {
            w = std::min(kSearchChipMaxW,
                         std::ceil(kit.TextWidth(Type::Label, m.search) + kSearchChipRoom));
        } else {
            continue;
        }
        out.emplace_back(t, Rect{x, kTopY0, x + w, kTopY1});
        x += w + kChipGap;
    }
    return out;
}

} // namespace

std::string GamesProposed::GameCount(std::size_t n, bool searching) {
    const std::string one = searching ? " Match" : " Game", many = searching ? " Matches" : " Games";
    return std::to_string(n) + (n == 1 ? one : many);
}

std::vector<int> GamesShown(const GamesTabModel& m, const GamesView& v) {
    return GamesShown(m.list, GameChip(m.list, v.chip), v.sort, m.search);
}

std::vector<Target> GamesTopRow(const GamesTabModel& m) {
    std::vector<Target> out;
    if (!m.search.empty()) { // in All's place, and Search's
        out.push_back({Id::GamesSearchChip});
    }
    for (const SystemFilter chip : GameChips(m.list)) {
        if (chip != SystemFilter::All || m.search.empty()) {
            out.push_back({Id::GameChip, static_cast<int>(chip)});
        }
    }
    if (m.search.empty()) {
        out.push_back({Id::GamesSearch});
    }
    out.push_back({Id::GamesSort});
    out.push_back({Id::GamesAddFolder});
    return out;
}

Target GamesFocused(const MenuModel& model, const MenuView& view) {
    const Focus::GridFocus& f = view.games.focus;
    if (!f.shown || view.tab != Tab::Games) {
        return {};
    }
    const auto at = [](const auto& list, int i) {
        return i >= 0 && i < static_cast<int>(list.size()) ? &list[static_cast<std::size_t>(i)]
                                                           : nullptr;
    };
    if (view.open_menu == Id::GamesSort || view.open_menu == Id::GameOption) {
        return {view.open_menu, view.games.menu_item};
    }
    if (f.area == Focus::Area::Rail) {
        const auto tabs = RailTabs(model);
        const Tab* tab = at(tabs, f.rail);
        return tab ? Target{Id::TabButton, static_cast<int>(*tab)} : Target{};
    }
    if (f.area == Focus::Area::Top) {
        const auto top = GamesTopRow(model.games);
        const Target* t = at(top, f.top);
        return t ? *t : Target{};
    }
    const auto shown = GamesShown(model.games, view.games);
    const int* i = at(shown, f.tile);
    return i ? Target{Id::GameTile, *i} : Target{};
}

Focus::Shape GamesShape(const MenuModel& model, const MenuView& view, const MenuLayout& layout) {
    Focus::Shape s;
    s.columns = kGamesColumns;
    s.games = static_cast<int>(GamesShown(model.games, view.games).size());
    const auto top = GamesTopRow(model.games);
    s.top = static_cast<int>(top.size());
    const auto tabs = RailTabs(model);
    s.rail = static_cast<int>(tabs.size());
    s.rail_active = static_cast<int>(std::find(tabs.begin(), tabs.end(), Tab::Games) - tabs.begin());
    // the chip or button over the middle of the tile
    const float x = kGamesX0 + kGameTileW * 0.5f +
                    static_cast<float>(view.games.focus.tile % kGamesColumns) *
                        (kGameTileW + kGameGap);
    float best = 1e9f;
    for (std::size_t i = 0; i < top.size(); i++) {
        for (const auto& h : layout.hits) {
            if (h.t == top[i]) {
                const float off = std::max({h.r.x0 - x, x - h.r.x1, 0.0f});
                if (off < best) {
                    best = off;
                    s.above = static_cast<int>(i);
                }
            }
        }
    }
    return s;
}

void GamesMove(Focus::Dir dir, const MenuModel& model, MenuView& view, const MenuLayout& layout) {
    GamesView& v = view.games;
    if (const auto items = GamesMenuItems(model.games, view); !items.empty()) {
        const int step = dir == Focus::Dir::Up ? -1 : dir == Focus::Dir::Down ? 1 : 0;
        v.menu_item = std::clamp(v.menu_item + step, 0, static_cast<int>(items.size()) - 1);
        v.focus.shown = true;
        return;
    }
    v.focus.Move(dir, GamesShape(model, view, layout));
    if (v.focus.area != Focus::Area::Grid) {
        return;
    }
    const auto shown = GamesShown(model.games, v);
    if (shown.empty()) {
        return;
    }
    const auto place = static_cast<std::size_t>(v.focus.tile);
    v.focus_game = model.games.list->games[static_cast<std::size_t>(shown[place])].path;
    const float y = static_cast<float>(v.focus.tile / kGamesColumns) * kGameRow;
    float& scroll = view.scroll[static_cast<std::size_t>(Tab::Games)];
    scroll = Focus::ScrollToShow(scroll, y, y + kGameTileH, kGamesY1 - kGamesY0, layout.max_scroll);
}

void GamesChanged(const GamesTabModel& m, GamesView& v) {
    v.chip = GameChip(m.list, v.chip);
    const auto shown = GamesShown(m, v);
    v.focus.top = std::min(v.focus.top, static_cast<int>(GamesTopRow(m).size()) - 1);
    for (std::size_t i = 0; i < shown.size(); i++) {
        if (m.list->games[static_cast<std::size_t>(shown[i])].path == v.focus_game) {
            v.focus.tile = static_cast<int>(i);
            return;
        }
    }
    v.focus.tile = std::clamp(v.focus.tile, 0, std::max(0, static_cast<int>(shown.size()) - 1));
}

void GamesOpened(const GamesTabModel& m, GamesView& v) {
    v.focus = {};
    v.focus_game = m.running;
    GamesChanged(m, v);
}

std::string GameTooltip(const GamesTabModel& m, int index, bool cut) {
    if (!m.list || index < 0 || index >= static_cast<int>(m.list->games.size())) {
        return {};
    }
    const GameEntry& g = m.list->games[static_cast<std::size_t>(index)];
    std::string out;
    const auto add = [&out](const std::string& part) {
        out += (out.empty() ? "" : " · ") + part;
    };
    if (cut) {
        add(g.title);
    }
    if (!m.running.empty() && g.path == m.running) {
        add("Continue");
    }
    if (g.favorite) {
        add("In Favourites");
    }
    if (g.pokedaisy) {
        add("PokeDaisy");
    }
    return out;
}

std::vector<std::string> GameTitleWrapped(const Kit& kit, const std::string& title, float width) {
    const auto fits = [&kit, width](const std::string& s) {
        return kit.TextWidth(kTitle, s) <= width;
    };
    std::vector<std::string> lines;
    std::string line;
    for (std::size_t at = 0; at < title.size();) {
        std::size_t end = std::min(title.find(' ', at), title.size());
        if (title.compare(end, 3, " - ") == 0) {
            end += 2; // a dash stays with the word before it
        }
        std::string word = title.substr(at, end - at);
        at = end + 1;
        if (word.empty()) {
            continue;
        }
        if (!line.empty() && fits(line + " " + word)) {
            line += " " + word;
            continue;
        }
        if (!line.empty()) {
            lines.push_back(std::exchange(line, {}));
        }
        while (!fits(word)) { // longer than a line: as much of it as fits, at least a character
            std::size_t cut = 0;
            for (std::size_t i = 0; i < word.size();) {
                frame_xr::ui::NextUtf8(word, i);
                if (cut > 0 && !fits(word.substr(0, i))) {
                    break;
                }
                cut = i;
            }
            if (cut >= word.size()) {
                break;
            }
            lines.push_back(word.substr(0, cut));
            word.erase(0, cut);
        }
        line = word;
    }
    if (!line.empty() || lines.empty()) {
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> GameTitleLines(const Kit& kit, const std::string& title, float width,
                                        bool* cut) {
    std::vector<std::string> lines = GameTitleWrapped(kit, title, width);
    const bool more = lines.size() > kGameTitleLines;
    if (more) {
        lines.resize(kGameTitleLines);
        std::string& last = lines.back();
        while (kit.TextWidth(kTitle, last + kTitleCut) > width && last.find(' ') != std::string::npos) {
            last.erase(last.rfind(' '));
        }
        last += kTitleCut;
        last = kit.Fit(kTitle, last, width); // one long word: the dots alone do not fit after it
    }
    if (cut) {
        *cut = more;
    }
    return lines;
}

float GamesRestScroll(float scroll, float max_scroll) {
    return std::clamp(std::round(scroll / kGameRow) * kGameRow, 0.0f, std::max(0.0f, max_scroll));
}

float TitleScroll(float t, float overflow) {
    constexpr float kWait = 1.2f, kSpeed = 14.0f; // s, dp a second
    if (overflow <= 0.0f) {
        return 0.0f;
    }
    const float move = overflow / kSpeed;
    const float at = std::fmod(std::max(0.0f, t), kWait + move + kWait);
    return std::round(std::clamp((at - kWait) * kSpeed, 0.0f, overflow));
}

int GameAt(const GamesList& list, const std::string& path) {
    for (std::size_t i = 0; list && i < list->games.size(); i++) {
        if (list->games[i].path == path) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<std::string> GamesMenuItems(const GamesTabModel& m, const MenuView& view) {
    if (view.open_menu == Id::GamesSort) {
        std::vector<std::string> out;
        for (int i = 0; i < static_cast<int>(GamesSort::Count); i++) {
            out.push_back(SortName(static_cast<GamesSort>(i)));
        }
        return out;
    }
    if (view.open_menu == Id::GameOption && GameAt(m.list, view.games.menu_game) >= 0) {
        const bool running = !m.running.empty() && view.games.menu_game == m.running;
        return {running ? "Continue" : "Play", "Favourite"}; // GameAction
    }
    return {};
}

void DrawGamesIcon(Kit& kit, float cx, float cy, bool active) {
    for (const float dx : {-5.0f, 5.0f}) {
        for (const float dy : {-5.0f, 5.0f}) {
            const Rect r{cx + dx - 4.0f, cy + dy - 4.0f, cx + dx + 4.0f, cy + dy + 4.0f};
            if (active) {
                kit.Fill(r, 1.5f, Tok::kOnSecondaryContainer);
            } else {
                kit.Stroke(r, 1.5f, 2.0f, Tok::kOnSurfaceVariant);
            }
        }
    }
}

// The owner has not chosen between this and a daisy: the whole badge is drawn here.
void PokeDaisySeal(Kit& kit, float cx, float cy, float size, float opacity) {
    const float r = size * 0.5f;
    kit.Dot(cx, cy, r * Seal::kBody, Tok::kPrimary, opacity);
    for (int i = 0; i < Seal::kLobes; i++) {
        const auto [x, y] = Seal::Lobe(i);
        kit.Dot(cx + r * x, cy + r * y, r * Seal::kLobe, Tok::kPrimary, opacity);
    }
    const float band = r * Seal::kBody;
    kit.Line(cx - band, cy, cx + band, cy, r * Seal::kBand, Tok::kOnPrimary, opacity);
    kit.Dot(cx, cy, r * Seal::kButtonGap, Tok::kOnPrimary, opacity);
    kit.Dot(cx, cy, r * Seal::kButton, Tok::kPrimary, opacity);
}

namespace {

/// The art area: the cover over it (cover_art.h), else the icon in its middle.
void DrawArt(Kit& kit, const GameEntry& g, const Rect& box, float opacity) {
    kit.Fill(box, kArtRadius, Tok::kSurfaceContainer);
    const auto drawable = [](const std::shared_ptr<const GameIcon>& i) {
        return i && i->w > 0 && i->h > 0 &&
               i->rgba.size() >=
                   static_cast<std::size_t>(i->w) * static_cast<std::size_t>(i->h) * 4;
    };
    if (drawable(g.cover)) {
        kit.ImageFit(g.cover->rgba.data(), g.cover->w, g.cover->h, box, kArtRadius, opacity);
        return;
    }
    if (drawable(g.icon)) {
        const Rect icon{box.Cx() - kIcon * 0.5f, box.Cy() - kIcon * 0.5f, box.Cx() + kIcon * 0.5f,
                        box.Cy() + kIcon * 0.5f};
        kit.ImageWhole(g.icon->rgba.data(), g.icon->w, g.icon->h, icon, kIconRadius, opacity);
    }
    // GBA and Game Boy games have no picture of their own: the cartridge says whose it is
    const bool cartridge = g.system == Common::GameSystem::GBA ||
                           g.system == Common::GameSystem::GB ||
                           g.system == Common::GameSystem::GBC;
    if (!drawable(g.icon) || cartridge) {
        constexpr float kSize = 18.0f;
        kit.TextSized(box.Cx(), box.Cy() + kit.CapHeightSized(kSize, true) * 0.5f, kSize, true,
                      drawable(g.icon) ? 0xFFFFFF : Tok::kOnSurfaceVariant, Initials(g.title), 1,
                      opacity);
    }
}

/// Two Screens on the bottom of the art; a PokeDaisy game's has the seal for a symbol.
void DrawPill(Kit& kit, const GameEntry& g, const Rect& tile, uint32_t tile_rgb, bool running,
              float opacity) {
    const std::string text = "Two Screens";
    const float w = kPillText + kit.TextWidthSized(kSmall, true, text) + kPillEnd;
    const Rect r{tile.Cx() - w * 0.5f, tile.y0 + kPillY0, tile.Cx() + w * 0.5f, tile.y0 + kPillY1};
    const uint32_t ink = running ? Tok::kOnSurface : Tok::kOnSecondaryContainer;
    kit.Fill(Grown(r, kPillEdge * 0.5f), r.H() * 0.5f + kPillEdge * 0.5f, tile_rgb);
    // (the running tile has the pill's colour)
    kit.Fill(Grown(r, -kPillEdge * 0.5f), r.H() * 0.5f - kPillEdge * 0.5f,
             running ? Tok::kSurfaceContainerHigh : Tok::kSecondaryContainer);
    if (g.pokedaisy) {
        PokeDaisySeal(kit, r.x0 + kPillSymbol, r.Cy(), kPillSeal, opacity);
    } else {
        TwoScreensGlyph(kit, r.x0 + kPillSymbol - kPillGlyph * 0.5f, r.Cy(), ink, opacity);
    }
    kit.TextSized(r.x0 + kPillText, r.Cy() + kit.CapHeightSized(kSmall, true) * 0.5f, kSmall, true,
                  ink, text, 0, opacity);
}

/// The star's and Options' dark circle.
void MarkCircle(Kit& kit, float cx, float cy, float r) {
    kit.Dot(cx, cy, r, Tok::kSurface, kMarkInk);
}

} // namespace

void TabMenu::DrawGameTile(Kit& kit, const GamesTabModel& m, const MenuView& view, int index,
                           const Target& focused, const Rect& r, const Rect& clip) {
    const GameEntry& g = m.list->games[static_cast<std::size_t>(index)];
    const Target t{Id::GameTile, index}, options{Id::GameOptions, index};
    const bool running = !m.running.empty() && g.path == m.running;
    const bool menu_open = view.open_menu == Id::GameOption && view.games.menu_game == g.path;
    const bool pointed = view.hover == t || view.hover == options || focused == t || menu_open;
    const float op = g.missing ? kDisabledContent : 1.0f;
    const uint32_t tile_rgb = running ? Tok::kSecondaryContainer : Tok::kSurfaceContainerHigh;
    kit.Fill(r, kTileRadius, tile_rgb);
    kit.StateLayer(r, kTileRadius, Tok::kOnSurface,
                   view.hover == options ? State::Hover : StateOf(view, t));
    AddHit(r, t, clip);

    const Rect art{r.x0 + kArtX, r.y0 + kArtY, r.x0 + kArtX + kArtW, r.y0 + kArtY + kArtH};
    DrawArt(kit, g, art, op);
    if (g.favorite) {
        MarkCircle(kit, r.x0 + kMarkAt, r.y0 + kMarkAt, kMarkR);
        kit.Icon(r.x0 + kMarkAt, r.y0 + kMarkAt, kStar, Sym::kStar, true, Tok::kOnSurface, op);
    }
    if (g.two_screens) {
        DrawPill(kit, g, r, tile_rgb, running, op);
    }
    if (pointed) {
        const float cx = r.x1 - kMarkAt, cy = r.y0 + kMarkAt;
        MarkCircle(kit, cx, cy, kOptionsR);
        kit.StateLayer({cx - kOptionsR, cy - kOptionsR, cx + kOptionsR, cy + kOptionsR}, kOptionsR,
                       Tok::kOnSurface, menu_open ? State::Pressed : StateOf(view, options));
        for (const float dy : {-5.0f, 0.0f, 5.0f}) {
            kit.Dot(cx, cy + dy, 1.7f, Tok::kOnSurface);
        }
    }
    const Rect o{r.x1 - kOptionsTarget, r.y0, r.x1, r.y0 + kOptionsTarget};
    AddHit(o, options, clip);
    anchors.push_back({Id::GameOptions, o.x0, std::max(o.y0, clip.y0), Lock::None, index, o.Cx(),
                       "Options"});
    if (menu_open) {
        games_menu_at_ = o;
    }

    // The title; one that does not fit scrolls by while its tile is pointed at
    bool cut = false;
    const uint32_t title_ink = running ? Tok::kOnSecondaryContainer : Tok::kOnSurface;
    auto lines = GameTitleLines(kit, g.title, kGameTextW, &cut);
    const auto count = static_cast<float>(lines.size());
    float scrolled = 0.0f;
    if (cut) {
        auto whole = GameTitleWrapped(kit, g.title, kGameTextW);
        layout.cut_titles.emplace_back(
            index, static_cast<float>(whole.size() - kGameTitleLines) * kGameTitlePitch);
        if (pointed && view.games.title_scroll > 0.0f) {
            scrolled = view.games.title_scroll;
            lines = std::move(whole);
        }
    }
    const float top = r.y0 + kTitleBase - kGameTitlePitch + 4.0f;
    if (scrolled > 0.0f) {
        kit.Clip({clip.x0, std::max(clip.y0, top), clip.x1,
                  std::min(clip.y1, top + count * kGameTitlePitch)});
    }
    for (std::size_t i = 0; i < lines.size(); i++) {
        kit.Text(r.Cx(), r.y0 + kTitleBase + static_cast<float>(i) * kGameTitlePitch - scrolled,
                 kTitle, title_ink, lines[i], 1, op);
    }
    if (scrolled > 0.0f) {
        kit.Clip(clip);
    }

    // The small line, at the same place on every tile
    const float base = r.y0 + kSmallBase;
    const bool medium = running && !g.missing;
    const float before = medium ? 16.0f : 0.0f; // the play arrow
    std::string small;
    for (const std::string& line : GameLines(g, running)) {
        small = line;
        if (kit.TextWidthSized(kSmall, medium, line) <= kGameTextW - before) {
            break;
        }
    }
    const uint32_t ink = g.missing ? Tok::kError
                         : running ? Tok::kOnSecondaryContainer
                                   : Tok::kOnSurfaceVariant;
    const float left = r.Cx() - (kit.TextWidthSized(kSmall, medium, small) + before) * 0.5f;
    if (medium) {
        kit.Icon(left + 6.0f, base - 4.7f, 16.0f, Sym::kPlayArrow, true, ink);
    }
    kit.TextSized(left + before, base, kSmall, medium, ink, small);

    if (focused == t) {
        FocusRing(kit, r, kTileRadius);
    }
    anchors.push_back({Id::GameTile, r.x0, std::max(r.y0, clip.y0), Lock::None, index, r.Cx(),
                       GameTooltip(m, index, cut)});
}

void TabMenu::DrawGames(Kit& kit, const MenuModel& model, const MenuView& view, const Rect& pane) {
    const GamesTabModel& m = model.games;
    if (!m.list) {
        return;
    }
    layout.games = m.list;
    const Target focused = GamesFocused(model, view);
    const SystemFilter chosen = GameChip(m.list, view.games.chip);
    const bool searching = !m.search.empty();

    // ---- Chips ----
    const auto chips = PlaceChips(kit, m);
    for (const auto& [t, r] : chips) {
        const bool on = t.id == Id::GamesSearchChip || static_cast<int>(chosen) == t.index;
        const uint32_t ink = on ? Tok::kOnSecondaryContainer : Tok::kOnSurfaceVariant;
        if (on) {
            kit.Fill(r, kChipRadius, Tok::kSecondaryContainer);
        } else {
            kit.Stroke(r, kChipRadius, 1.0f, Tok::kOutline);
        }
        kit.StateLayer(r, kChipRadius, ink, StateOf(view, t));
        AddHit(r, t, pane);
        if (t.id == Id::GamesSearchChip) {
            const Target close{Id::GamesSearchClear};
            const Rect c{r.x1 - kSearchChipClose, r.y0, r.x1, r.y1};
            kit.StateLayer(Grown(c, -8.0f), 16.0f, ink, StateOf(view, close));
            kit.Icon(c.Cx() - 4.0f, c.Cy(), 18.0f, Sym::kClose, false, ink);
            AddHit(c, close, pane);
            kit.TextMid(r.x0 + 16.0f, r.Cy(), Type::Label, ink,
                        kit.Fit(Type::Label, m.search, r.W() - kSearchChipRoom));
        } else {
            const std::string name = ChipName(static_cast<SystemFilter>(t.index));
            const float tw = kit.TextWidth(Type::Label, name);
            const float x0 = r.Cx() - (tw + (on ? 22.0f : 0.0f)) * 0.5f;
            if (on) {
                kit.Icon(x0 + 9.0f, r.Cy(), 18.0f, Sym::kCheck, false, ink);
            }
            kit.TextMid(x0 + (on ? 22.0f : 0.0f), r.Cy(), Type::Label, ink, name);
        }
        if (focused == t) {
            FocusRing(kit, r, kChipRadius);
        }
    }

    // ---- Buttons ----
    const auto round = [&](Id id, float cx) {
        const float cy = (kTopY0 + kTopY1) * 0.5f;
        const Rect r{cx - kRoundR, cy - kRoundR, cx + kRoundR, cy + kRoundR};
        kit.Fill(r, kRoundR, Tok::kSurfaceContainerHigh);
        kit.StateLayer(r, kRoundR, Tok::kOnSurfaceVariant,
                       view.open_menu == id ? State::Pressed : StateOf(view, {id}));
        if (id == Id::GamesSearch) {
            SearchGlyph(kit, cx, cy, Tok::kOnSurfaceVariant);
        } else if (id == Id::GamesSort) {
            SortGlyph(kit, cx, cy, Tok::kOnSurfaceVariant);
        } else {
            AddFolderGlyph(kit, cx, cy, Tok::kOnSurfaceVariant);
        }
        AddHit({cx - kRoundTarget, cy - kRoundTarget, cx + kRoundTarget, cy + kRoundTarget}, {id},
               pane);
        anchors.push_back({id, r.x0, r.y1, Lock::None, -1, cx, {}, true});
        if (focused == Target{id}) {
            FocusRing(kit, r, kRoundR);
        }
    };
    if (!searching) {
        round(Id::GamesSearch, kSearchCx);
    }
    round(Id::GamesSort, kSortCx);
    round(Id::GamesAddFolder, kAddFolderCx);

    // ---- Count line ----
    const auto shown = GamesShown(m, view.games);
    if (kGamesCountLine) {
        const float cy = chips.empty() ? (kTopY0 + kTopY1) * 0.5f : kCountCy;
        kit.TextMid(kGamesX0, cy, Type::Tooltip, Tok::kOnSurfaceVariant,
                    GamesProposed::GameCount(shown.size(), searching) + " · " +
                        SortName(view.games.sort));
    }

    // ---- Tiles ----
    const Rect list{kGamesX0, kGamesY0, kGamesX1, kGamesY1};
    const int rows = (static_cast<int>(shown.size()) + kGamesColumns - 1) / kGamesColumns;
    const float content_h = rows > 0 ? static_cast<float>(rows) * kGameRow - kGameGap : 0.0f;
    layout.max_scroll = std::max(0.0f, content_h - list.H());
    layout.scroll_step = kGameRow;
    const float scroll =
        std::clamp(view.scroll[static_cast<int>(Tab::Games)], 0.0f, layout.max_scroll);
    if (shown.empty()) { // only a search finds nothing: without games there is no tab
        SearchGlyph(kit, list.Cx(), list.Cy() - 64.0f, Tok::kOnSurfaceVariant, 48.0f);
        kit.TextMid(list.Cx(), list.Cy() - 8.0f, Type::Title, Tok::kOnSurface, "No Matches", 1);
        const std::string label = LabelFor(Id::GamesSearchClear);
        const float w = kit.TextWidth(Type::Label, label) + 48.0f;
        const Target t{Id::GamesSearchClear};
        const Rect b{list.Cx() - w * 0.5f, list.Cy() + 20.0f, list.Cx() + w * 0.5f,
                     list.Cy() + 68.0f};
        kit.Button(b, ButtonStyle::Tonal, 0, label, StateOf(view, t));
        AddHit(b, t, pane);
    }
    games_menu_at_ = list;
    const Rect clip = Grown(list, kRingRoom);
    kit.Clip(clip);
    for (std::size_t i = 0; i < shown.size(); i++) {
        const auto row = static_cast<float>(i / kGamesColumns);
        const auto column = static_cast<float>(i % kGamesColumns);
        const float x = list.x0 + column * (kGameTileW + kGameGap);
        const float y = list.y0 + row * kGameRow - scroll;
        if (y + kGameTileH <= clip.y0 || y >= clip.y1) {
            continue; // out of view
        }
        DrawGameTile(kit, m, view, shown[i], focused, {x, y, x + kGameTileW, y + kGameTileH},
                     clip);
    }
    kit.ClearClip();
    DrawScrollbar(kit, view, list, kBarRight, content_h, layout.max_scroll, scroll);

    // ---- Sort's menu and a tile's Options, over the rest ----
    const auto items = GamesMenuItems(m, view);
    if (!items.empty()) {
        const bool sort = view.open_menu == Id::GamesSort;
        const float h = Kit::MenuItemHeight() * static_cast<float>(items.size()) + 16.0f;
        Rect at{kGamesX1 - kSortMenuW, kTopY1 + 4.0f, kGamesX1, kTopY1 + 4.0f};
        int selected = static_cast<int>(view.games.sort);
        if (!sort) {
            const Rect& o = games_menu_at_; // under the three dots, above them at the bottom
            const float y = o.y1 + h <= kPanelH - 16.0f ? o.y1 : o.y0 - h;
            at = {o.x1 - kOptionsMenuW, y, o.x1, y};
            const int i = GameAt(m.list, view.games.menu_game);
            selected = m.list->games[static_cast<std::size_t>(i)].favorite
                           ? static_cast<int>(GameAction::Favourite)
                           : -1;
        }
        const int hover = view.hover.id == view.open_menu ? view.hover.index
                          : view.games.focus.shown        ? view.games.menu_item
                                                          : -1;
        const Rect box = kit.Menu(at, items, selected, hover);
        const Rect everything{0, 0, kPanelW, kPanelH};
        AddHit(box, {Id::None, kMenuAreaIndex}, everything); // the menu's own area eats clicks
        for (int i = 0; i < static_cast<int>(items.size()); i++) {
            const float y = box.y0 + 8.0f + Kit::MenuItemHeight() * static_cast<float>(i);
            AddHit({box.x0, y, box.x1, y + Kit::MenuItemHeight()}, {view.open_menu, i}, everything);
        }
    }
}

} // namespace FrameXr::Menu
