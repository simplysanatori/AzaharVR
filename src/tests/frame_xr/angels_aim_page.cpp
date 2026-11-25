// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuaim: the Angel's Aim page with fewer rows on view (menu/angels_aim_tab.h;
// handoff/reports/101/aim-page).

#include <algorithm>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "frame_xr/menu/angels_aim_tab.h"
#include "frame_xr/menu/tab_menu.h"

using namespace FrameXr::Menu;

namespace {

/// The page on for one test (the library's default is the page as it was).
struct PageOn {
    PageOn() {
        SetAimPage(true);
    }
    ~PageOn() {
        SetAimPage(false);
    }
};

/// The four cases: another game, a copy Angel's Aim is not mapped for, the mode off, the mode on.
enum class Case { OtherGame, Unmapped, ModeOff, ModeOn };
constexpr Case kCases[] = {Case::OtherGame, Case::Unmapped, Case::ModeOff, Case::ModeOn};

MenuModel ModelFor(Case c) {
    MenuModel m;
    m.game_title = "Kid Icarus: Uprising";
    m.scope = 1;
    m.immersive_mode = c == Case::ModeOn ? 4 : 2;
    m.mods.angels_aim.shown = c != Case::OtherGame;
    m.mods.angels_aim.mapped = c == Case::ModeOff || c == Case::ModeOn;
    m.mods.angels_aim.report_url = kAngelsAimIssuesUrl;
    m.gyro_aim = 2; // Direct Aim
    m.recoil = m.weapon_in_hand = m.muzzle_flash = m.projectiles = true;
    m.kiu_bottom_screen = 2; // Wrist
    m.kiu_hud = 1;
    return m;
}

bool Has(const std::vector<Id>& ids, Id id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

TEST_CASE("Angel's Aim page: the switch is on unless it says off", "[frame_xr][kiuaim]") {
    CHECK(AimPageSwitchOn(std::nullopt));
    CHECK(AimPageSwitchOn(std::string("1")));
    CHECK(AimPageSwitchOn(std::string("")));
    CHECK_FALSE(AimPageSwitchOn(std::string("0")));
    CHECK_FALSE(AimPageSwitchOn(std::string("Off")));
    CHECK_FALSE(AimPageSwitchOn(std::string("false")));
    CHECK(std::string(kKiuAimPageSwitch) == "AZAHARFRAME_KIU_AIM_PAGE");
    CHECK_FALSE(AimPageOn()); // the library's own default: the app turns it on
}

TEST_CASE("Angel's Aim page: rows that leave it or wait", "[frame_xr][kiuaim]") {
    MenuModel m = ModelFor(Case::ModeOn);
    CHECK_FALSE(AimPageRowShown(m, Id::WristSide));         // the Stats tab's
    CHECK_FALSE(AimPageRowShown(m, Id::AngelsAimSettings)); // its rows are here
    // Reset Bottom Screen Position: Persistent, and moved.
    for (const int mode : {0, 1, 2}) {
        for (const bool moved : {false, true}) {
            m.kiu_bottom_screen = mode;
            m.kiu_bottom_moved = moved;
            CHECK(AimPageRowShown(m, Id::KiuBottomReset) == (mode == 1 && moved));
        }
    }
    // Gyro Sensitivity: with Gyro Aim (stored 1), not with Off or Direct Aim.
    for (const int aim : {0, 1, 2}) {
        m.gyro_aim = aim;
        CHECK(AimPageRowShown(m, Id::GyroSensitivity) == (aim == 1));
    }
    for (const Id id : {Id::GyroAim, Id::TriggerLayout, Id::KiuBottomScreen, Id::KiuWristAngle,
                        Id::WeaponInHand, Id::ImmersiveMode, Id::CalibrateAim}) {
        CHECK(AimPageRowShown(m, id));
    }
    // The filter acts only while the page is on.
    std::vector<Id> rows{Id::GyroAim, Id::GyroSensitivity, Id::WristSide, Id::AngelsAimSettings};
    AimPageFilter(m, rows);
    CHECK(rows.size() == 4);
    const PageOn on;
    AimPageFilter(m, rows);
    CHECK(rows == std::vector<Id>{Id::GyroAim});
}

TEST_CASE("Angel's Aim page: the approved tips, each within two lines", "[frame_xr][kiuaim]") {
    CHECK(std::string(AimPageTip(Id::GyroAim)) ==
          "Direct Aim: the reticle goes where you point. Gyro Aim: it moves as you turn your "
          "hand. Hold the grip to free it.");
    CHECK(std::string(AimPageTip(Id::GyroSensitivity)) ==
          "How far the reticle moves as you turn your hand. Applies to every game.");
    CHECK(std::string(AimPageTip(Id::Recoil)) ==
          "Haptic feedback in the aiming controller on each shot.");
    CHECK(std::string(AimPageTip(Id::RecoilCharged)) == "How strong it is for a charged shot.");
    CHECK(std::string(AimPageTip(Id::RecoilRapid)) ==
          "How strong it is for each rapid-fire shot.");
    CHECK(AimPageTip(Id::KiuBottomScreen) == nullptr); // its own constant, everywhere
    CHECK(std::string(kBottomScreenTip) ==
          "Persistent: always in view, low in front of you; hold a grip on it to move it. Wrist: "
          "on your free hand's wrist; turn your wrist to see it, and point at it to touch.");
    Kit kit{1.0f};
    for (const char* tip : {kAimTipAiming, kAimTipGyroSensitivity, kAimTipRecoil,
                            kAimTipRecoilCharged, kAimTipRecoilRapid, kBottomScreenTip,
                            kTriggerLayoutOriginalTip, kTriggerLayoutFireAndPowerTip}) {
        INFO(tip);
        CHECK(kit.TextWidth(Type::Tooltip, tip) + 28.0f <= 2.0f * kPanelW * 0.7f);
        CHECK(std::string(tip).find("buzz") == std::string::npos);
    }
}

TEST_CASE("Angel's Aim page: the groups' lists in the four cases", "[frame_xr][kiuaim]") {
    const auto ids = [](const MenuModel& m) {
        std::vector<Id> out;
        for (const AngelsAimRowGroup& g : AngelsAimGroups(m)) {
            out.insert(out.end(), g.rows.begin(), g.rows.end());
        }
        return out;
    };
    for (const Case c : kCases) {
        INFO("case " << static_cast<int>(c));
        MenuModel m = ModelFor(c);
        m.kiu_bottom_screen = 1; // Persistent, in its place
        const std::vector<Id> before = ids(m);
        const PageOn on;
        const std::vector<Id> after = ids(m);
        if (c == Case::OtherGame) {
            CHECK(before.empty());
            CHECK(after.empty());
            continue;
        }
        // Off the page: Wrist Side and the link, in every case; Gyro Sensitivity with Direct Aim.
        CHECK(Has(before, Id::WristSide));
        CHECK(Has(before, Id::AngelsAimSettings));
        CHECK(Has(before, Id::GyroSensitivity));
        CHECK_FALSE(Has(after, Id::WristSide));
        CHECK_FALSE(Has(after, Id::AngelsAimSettings));
        CHECK_FALSE(Has(after, Id::GyroSensitivity));
        // Reset: only in the mode, and only once Persistent was moved.
        CHECK(Has(before, Id::KiuBottomReset) == (c == Case::ModeOn));
        CHECK_FALSE(Has(after, Id::KiuBottomReset));
        m.kiu_bottom_moved = true;
        CHECK(Has(ids(m), Id::KiuBottomReset) == (c == Case::ModeOn));
        // Nothing else goes: the page's rows are the old ones less those.
        std::vector<Id> expect;
        for (const Id id : before) {
            if (AimPageRowShown(m, id)) {
                expect.push_back(id);
            }
        }
        CHECK(ids(m) == expect);
        // With Gyro Aim its sensitivity is back.
        m.gyro_aim = 1;
        CHECK(Has(ids(m), Id::GyroSensitivity));
    }
}

// ---- Phase 2: the front view, the pages, More ----
namespace {

struct Drawn {
    Kit kit{1.0f};
    frame_xr::ui::Canvas canvas;
    TabMenu menu;
    std::vector<DrawnText> texts;
    void Draw(const MenuModel& m, const MenuView& v) {
        canvas.Init(static_cast<int>(kPanelW), static_cast<int>(kPanelH));
        kit.Begin(canvas);
        texts.clear();
        kit.RecordText(&texts);
        menu.Draw(kit, m, v);
        kit.RecordText(nullptr);
    }
    const MenuLayout::Hit* Hit(Id id, int index) const {
        for (const auto& h : menu.Hits()) {
            if (h.t.id == id && h.t.index == index && h.r.y1 > h.r.y0 && h.r.x1 > h.r.x0) {
                return &h;
            }
        }
        return nullptr;
    }
    bool Drew(const std::string& s) const {
        return std::any_of(texts.begin(), texts.end(), [&](const DrawnText& t) {
            return t.text == s && std::min(t.box.y1, t.clip.y1) > std::max(t.box.y0, t.clip.y0);
        });
    }
    /// Every line on view is inside the panel and whole inside its clip across.
    void CheckFit(const std::string& where) const {
        for (const auto& t : texts) {
            const float y0 = std::max(t.box.y0, t.clip.y0), y1 = std::min(t.box.y1, t.clip.y1);
            if (y1 <= y0) {
                continue;
            }
            INFO(where << ": \"" << t.text << "\"");
            CHECK(t.box.x0 >= 0.0f);
            CHECK(t.box.x1 <= kPanelW);
            CHECK(t.box.x0 >= t.clip.x0 - 0.5f);
            CHECK(t.box.x1 <= t.clip.x1 + 0.5f);
        }
    }
};

MenuView AimView() {
    MenuView v;
    v.tab = Tab::Mods;
    v.mods_segment = ModsSegment::AngelsAim;
    return v;
}

/// Every state of the rows' own conditions.
std::vector<MenuModel> Sweep(Case c) {
    std::vector<MenuModel> out;
    for (const int aim : {0, 1, 2}) {
        for (const int bottom : {0, 1, 2}) {
            for (const int bits : {0, 1, 2, 3, 4, 5, 6, 7}) {
                MenuModel m = ModelFor(c);
                m.gyro_aim = aim;
                m.kiu_bottom_screen = bottom;
                m.kiu_bottom_moved = bits & 1;
                m.kiu_view = bits & 1;
                m.kiu_hud = (bits >> 1) & 1;
                m.kiu_hud_linked = (bits >> 2) & 1;
                m.recoil = bits & 2;
                m.weapon_in_hand = bits != 0;
                m.muzzle_flash = bits & 4;
                m.projectiles = bits & 1;
                out.push_back(m);
            }
        }
    }
    return out;
}

std::vector<Id> PageIds(const MenuModel& m) {
    std::vector<Id> out;
    for (const AimGroup g : AimFrontGroups(m)) {
        const AimPage p = AimPageRows(m, g);
        out.insert(out.end(), p.rows.begin(), p.rows.end());
        out.insert(out.end(), p.more.begin(), p.more.end());
    }
    return out;
}

} // namespace

TEST_CASE("Angel's Aim page: the front view's groups in the four cases", "[frame_xr][kiuaim]") {
    using G = AimGroup;
    CHECK(AimFrontGroups(ModelFor(Case::OtherGame)).empty());
    const std::vector<G> five{G::Aiming, G::Weapon, G::ModsAndPack, G::About};
    CHECK(AimFrontGroups(ModelFor(Case::Unmapped)) == five); // 5 rows with Immersive Mode
    CHECK(AimFrontGroups(ModelFor(Case::ModeOff)) == five);
    CHECK(AimFrontGroups(ModelFor(Case::ModeOn)) ==
          std::vector<G>{G::Aiming, G::View, G::Hud, G::BottomScreen, G::Weapon, G::ModsAndPack,
                         G::About}); // 8 rows
    MenuModel off = ModelFor(Case::ModeOn);
    off.gyro_aim = 0; // Aiming off: Weapon has Free Hand alone, and no value
    CHECK(AimFrontGroups(off) == AimFrontGroups(ModelFor(Case::ModeOn)));
    CHECK(AimPageRows(off, G::Weapon).rows == std::vector<Id>{Id::KiuHideOffHand});
    CHECK(AimPageRows(off, G::Weapon).more.empty());
    CHECK(AimGroupValue(off, G::Weapon).empty());
    off.immersive_mode = 2; // and out of the mode nothing
    CHECK(AimFrontGroups(off) == std::vector<G>{G::Aiming, G::ModsAndPack, G::About});
    // Names and values (page.png).
    const MenuModel m = ModelFor(Case::ModeOn);
    CHECK(std::string(AimGroupName(G::Hud)) == "HUD");
    CHECK(std::string(AimGroupName(G::About)) == "About Angel's Aim");
    CHECK(AimGroupValue(m, G::Aiming) == "Direct Aim");
    CHECK(AimGroupValue(m, G::View) == "First Person \xC2\xB7 Smooth");
    CHECK(AimGroupValue(m, G::Hud) == "In View");
    CHECK(AimGroupValue(m, G::BottomScreen) == "Wrist");
    CHECK(AimGroupValue(m, G::Weapon) == "In Hand");
    CHECK(AimGroupValue(m, G::ModsAndPack) == "Not Installed");
    CHECK(AimGroupValue(m, G::About).empty());
    MenuModel some = m;
    some.mods.angels_aim.textures_pack = true;
    CHECK(AimGroupValue(some, G::ModsAndPack) ==
          (kPitArmOffered ? "1 of 3 Installed" : "1 of 2 Installed"));
    some.mods.angels_aim.mod_installed = some.mods.angels_aim.hand_installed = true;
    CHECK(AimGroupValue(some, G::ModsAndPack) == "Installed");
    CHECK(AimMoreTitle(G::Aiming) == "Aiming \xC2\xB7 More");
}

TEST_CASE("Angel's Aim page: counts, and every row reachable", "[frame_xr][kiuaim]") {
    for (const Case c : kCases) {
        for (const MenuModel& m : Sweep(c)) {
            INFO("case " << static_cast<int>(c) << " aim " << m.gyro_aim << " bottom "
                         << m.kiu_bottom_screen << " hud " << m.kiu_hud << m.kiu_hud_linked);
            const auto front = AimFrontGroups(m);
            CHECK(static_cast<int>(front.size()) + (c == Case::OtherGame ? 0 : 1) <=
                  kAimFrontMost);
            for (const AimGroup g : front) {
                const AimPage p = AimPageRows(m, g);
                INFO(AimGroupName(g));
                CHECK(static_cast<int>(p.rows.size()) <= kAimPageMost);
                CHECK(p.more.size() <= 10);
                if (g != AimGroup::ModsAndPack && g != AimGroup::About) {
                    CHECK_FALSE(p.rows.empty()); // More is never a page's only row
                }
            }
            CHECK(AimPageUnplaced(m).empty()); // a new row needs its place (AimPlaces)
            // The rows of the page as it was, less Wrist Side, the link, and the two that wait;
            // Immersive Mode is the front view's own.
            std::vector<Id> before;
            for (const AngelsAimRowGroup& g : AngelsAimGroups(m)) {
                for (const Id id : g.rows) {
                    if (id != Id::ImmersiveMode && AimPageRowShown(m, id)) {
                        before.push_back(id);
                    }
                }
            }
            std::vector<Id> now = PageIds(m);
            CHECK(now.size() == before.size());
            std::sort(before.begin(), before.end());
            std::sort(now.begin(), now.end());
            CHECK(now == before);
            CHECK(std::adjacent_find(now.begin(), now.end()) == now.end()); // each once
        }
    }
    // Where the approved rows are.
    const MenuModel m = ModelFor(Case::ModeOn);
    CHECK(AimPageRows(m, AimGroup::Aiming).rows ==
          std::vector<Id>{Id::GyroAim, Id::TriggerLayout, Id::KiuReticleSize, Id::LaserSight});
    CHECK(AimPageRows(m, AimGroup::Aiming).more ==
          std::vector<Id>{Id::KeepOnScreen, Id::GripFreesAim, Id::TurnZoneFrom, Id::TurnZone,
                          Id::TurnSpeed, Id::TurnSpeedVertical, Id::CalibrateAim});
    CHECK(AimPageRows(m, AimGroup::View).rows ==
          std::vector<Id>{Id::KiuOverShoulder, Id::KiuSnapTurn, Id::KiuCutscenes});
    CHECK(AimPageRows(m, AimGroup::Hud).rows ==
          std::vector<Id>{Id::KiuHud, Id::KiuHudSize, Id::KiuSubtitleSize});
    CHECK(AimPageRows(m, AimGroup::BottomScreen).rows ==
          std::vector<Id>{Id::KiuBottomScreen, Id::KiuBottomSize, Id::KiuBottomOpacity,
                          Id::KiuWristAngle, Id::KiuWristAlong});
    CHECK(AimPageRows(m, AimGroup::BottomScreen).more.empty());
    CHECK(AimPageRows(m, AimGroup::Weapon).rows ==
          std::vector<Id>{Id::WeaponInHand, Id::MuzzleFlash, Id::Projectiles, Id::Recoil,
                          Id::KiuHideOffHand}); // Free Hand
    CHECK(Has(AimPageRows(m, AimGroup::Weapon).more, Id::WeaponOpacity));
}

TEST_CASE("Angel's Aim page: presses, Back and B, and what is kept", "[frame_xr][kiuaim]") {
    const PageOn on;
    const MenuModel m = ModelFor(Case::ModeOn);
    MenuView v = AimView();
    const std::size_t mods = static_cast<std::size_t>(Tab::Mods);
    CHECK(AimPageShown(m, v).group == -1);
    CHECK_FALSE(AimPageBack(m, v)); // the front view: B goes on to close the menu
    CHECK_FALSE(AimPagePress(m, v, kAimMoreIndex));
    v.scroll[mods] = 96.0f;
    v.open_menu = Id::ImmersiveMode;
    REQUIRE(AimPagePress(m, v, static_cast<int>(AimGroup::Aiming)));
    CHECK(v.aim_group == static_cast<int>(AimGroup::Aiming));
    CHECK(v.scroll[mods] == 0.0f);
    CHECK(v.open_menu == Id::None);
    CHECK_FALSE(AimPagePress(m, v, static_cast<int>(AimGroup::View))); // no such row on a page
    REQUIRE(AimPagePress(m, v, kAimMoreIndex));
    CHECK(AimPageShown(m, v).more);
    v.scroll[mods] = 144.0f;
    REQUIRE(AimPageBack(m, v)); // More: back to its group's page
    CHECK(v.aim_group == static_cast<int>(AimGroup::Aiming));
    CHECK_FALSE(v.aim_more);
    CHECK(v.scroll[mods] == 0.0f);
    REQUIRE(AimPagePress(m, v, kAimBackIndex)); // the page: back to the front view
    CHECK(v.aim_group == -1);
    CHECK_FALSE(AimPageBack(m, v));
    // A group without More has no More page.
    REQUIRE(AimPagePress(m, v, static_cast<int>(AimGroup::BottomScreen)));
    CHECK_FALSE(AimPagePress(m, v, kAimMoreIndex));
    CHECK(AimBottomPageOpen(m, v));
    // Kept while it can show; the front view once the group has no row, the segment or the tab
    // is left (the menu opens on its Main tab), or another game is open.
    MenuView kept = v;
    AimPageKeep(m, kept);
    CHECK(kept == v);
    AimPageKeep(ModelFor(Case::ModeOff), kept);
    CHECK(kept.aim_group == -1);
    CHECK_FALSE(AimBottomPageOpen(ModelFor(Case::ModeOff), v));
    for (int way = 0; way < 3; way++) {
        MenuView left = v;
        left.aim_more = true;
        if (way == 0) {
            left.tab = Tab::Main;
        } else if (way == 1) {
            left.mods_segment = ModsSegment::Cheats;
        }
        CHECK_FALSE(AimBottomPageOpen(way == 2 ? ModelFor(Case::OtherGame) : m, left));
        AimPageKeep(way == 2 ? ModelFor(Case::OtherGame) : m, left);
        CHECK(left.aim_group == -1);
        CHECK_FALSE(left.aim_more);
    }
    // Out of range fields show the front view.
    MenuView bad = AimView();
    bad.aim_group = 99;
    CHECK(AimPageShown(m, bad).group == -1);
    // Off (the A/B): the Bottom Screen's page is never open.
    SetAimPage(false);
    CHECK_FALSE(AimBottomPageOpen(m, v));
    SetAimPage(true);
}

TEST_CASE("Angel's Aim page: drawn, the front view never scrolls and every row is a target",
          "[frame_xr][kiuaim]") {
    const PageOn on;
    Drawn d;
    for (const Case c : {Case::Unmapped, Case::ModeOff, Case::ModeOn}) {
        for (const int scope : {0, 1}) {
            MenuModel m = ModelFor(c);
            m.scope = scope;
            MenuView v = AimView();
            v.scroll[static_cast<std::size_t>(Tab::Mods)] = 500.0f; // left from another segment
            d.Draw(m, v);
            INFO("case " << static_cast<int>(c) << " scope " << scope);
            CHECK(d.menu.MaxScroll() == 0.0f);
            CHECK(d.Drew(kAimPageTitle));
            CHECK(d.Hit(Id::Scope, 0));
            CHECK(d.Hit(Id::Scope, 1));
            const auto* mode = d.Hit(Id::ImmersiveMode, -1);
            REQUIRE(mode);
            CHECK(mode->r.H() >= 48.0f);
            float last = mode->r.y1;
            const auto front = AimFrontGroups(m);
            for (const AimGroup g : front) {
                INFO(AimGroupName(g));
                const auto* h = d.Hit(Id::AngelsAimGroup, static_cast<int>(g));
                REQUIRE(h);
                CHECK(h->r.H() >= 48.0f);
                CHECK(h->r.y0 >= last - 4.0f); // in order, none over another
                CHECK(h->r.y1 <= kPanelH - 16.0f); // whole, on view
                last = h->r.y1;
                CHECK(d.Drew(AimGroupName(g)));
                if (!AimGroupValue(m, g).empty()) {
                    CHECK(d.Drew(AimGroupValue(m, g))); // whole: not cut to fit
                }
            }
            CHECK_FALSE(d.Hit(Id::AngelsAimGroup, kAimBackIndex));
            CHECK_FALSE(d.Hit(Id::WristSide, 0));
            CHECK_FALSE(d.Hit(Id::AngelsAimSettings, -1));
            CHECK_FALSE(d.Hit(Id::Scrollbar, -1));
            d.CheckFit("front");
        }
    }
}

TEST_CASE("Angel's Aim page: each group's page by the beam, its rows targets, B back",
          "[frame_xr][kiuaim]") {
    const PageOn on;
    Drawn d;
    const std::size_t mods = static_cast<std::size_t>(Tab::Mods);
    for (const Case c : {Case::Unmapped, Case::ModeOn}) {
        MenuModel m = ModelFor(c);
        for (const int bottom : {1, 2}) {
            m.kiu_bottom_screen = bottom;
            m.kiu_bottom_moved = true;
            for (const AimGroup g : AimFrontGroups(m)) {
                INFO("case " << static_cast<int>(c) << " " << AimGroupName(g));
                MenuView v = AimView();
                d.Draw(m, v);
                // The beam on the group's row: the target under its middle is the row's.
                const auto* row = d.Hit(Id::AngelsAimGroup, static_cast<int>(g));
                REQUIRE(row);
                const Target t = d.menu.HitTest(row->r.Cx(), row->r.Cy());
                REQUIRE(t.id == Id::AngelsAimGroup);
                REQUIRE(AimPagePress(m, v, t.index));
                const AimPage page = AimPageRows(m, g);
                const bool is_rows = g != AimGroup::ModsAndPack && g != AimGroup::About;
                for (const bool more : {false, true}) {
                    if (more) {
                        if (page.more.empty()) {
                            break;
                        }
                        const auto* mr = d.Hit(Id::AngelsAimGroup, kAimMoreIndex);
                        REQUIRE(mr);
                        CHECK(mr->r.H() >= 48.0f);
                        CHECK(mr->r.y1 <= kPanelH - 16.0f);
                        REQUIRE(AimPagePress(m, v, d.menu.HitTest(mr->r.Cx(), mr->r.Cy()).index));
                    }
                    d.Draw(m, v);
                    CHECK(d.Hit(Id::AngelsAimGroup, kAimBackIndex));
                    CHECK(d.Drew(more ? AimMoreTitle(g) : std::string(AimGroupName(g))));
                    CHECK_FALSE(d.Hit(Id::Scope, 0)); // the scope is the front view's
                    if (!more && is_rows) {
                        CHECK(d.menu.MaxScroll() == 0.0f); // a group's page never scrolls
                        CHECK((d.Hit(Id::AngelsAimGroup, kAimMoreIndex) != nullptr) ==
                              !page.more.empty());
                    }
                    if (is_rows) { // no row is cut off, at the top of the page or scrolled
                        for (const float s : {0.0f, d.menu.MaxScroll()}) {
                            v.scroll[mods] = s;
                            d.Draw(m, v);
                            for (const auto& t : d.texts) {
                                const bool out = t.box.y1 <= t.clip.y0 || t.box.y0 >= t.clip.y1;
                                INFO(t.text);
                                CHECK((out || (t.box.y0 >= t.clip.y0 && t.box.y1 <= t.clip.y1)));
                            }
                        }
                        v.scroll[mods] = 0.0f;
                        d.Draw(m, v);
                    }
                    // Every row is on view at some scroll (the stick scrolls a More page).
                    std::vector<Id> seen;
                    const float max_scroll = d.menu.MaxScroll();
                    for (float s = 0.0f;; s += kRowH) {
                        v.scroll[mods] = std::min(s, max_scroll);
                        d.Draw(m, v);
                        for (const auto& h : d.menu.Hits()) {
                            if (h.r.y1 - h.r.y0 >= 40.0f) {
                                seen.push_back(h.t.id);
                            }
                        }
                        d.CheckFit(AimGroupName(g));
                        if (s >= max_scroll) {
                            break;
                        }
                    }
                    for (const Id id : more ? page.more : page.rows) {
                        INFO(LabelFor(id));
                        CHECK(Has(seen, id));
                        CHECK((d.Drew(LabelFor(id)) || max_scroll > 0.0f));
                    }
                    v.scroll[mods] = 0.0f;
                }
                // B: More to the page, the page to the front view, then nothing.
                if (!page.more.empty()) {
                    REQUIRE(AimPageBack(m, v));
                    CHECK(AimPageShown(m, v).group == static_cast<int>(g));
                }
                REQUIRE(AimPageBack(m, v));
                CHECK(AimPageShown(m, v).group == -1);
                d.Draw(m, v);
                CHECK(d.Hit(Id::AngelsAimGroup, static_cast<int>(g))); // the group's row again
                CHECK_FALSE(AimPageBack(m, v));
            }
        }
    }
    // About and Mods & Pack keep their buttons and text.
    const MenuModel m = ModelFor(Case::ModeOn);
    MenuView v = AimView();
    v.aim_group = static_cast<int>(AimGroup::About);
    d.Draw(m, v);
    CHECK(d.Hit(Id::AngelsAimSend, -1));
    CHECK(d.Hit(Id::AngelsAimReport, -1));
    CHECK(d.Drew(kAngelsAimStatus));
    CHECK(d.menu.MaxScroll() == 0.0f);
    v.aim_group = static_cast<int>(AimGroup::ModsAndPack);
    d.Draw(m, v);
    CHECK(d.Drew(kAngelsAimRequired));
    CHECK(d.Drew(kAngelsAimModName));
    CHECK(d.Drew(kAngelsAimTexturesPack));
    // The page's own tips on its rows.
    v.aim_group = static_cast<int>(AimGroup::Weapon);
    v.tooltip = Id::Recoil;
    d.Draw(m, v);
    CHECK(d.Drew(kAimTipRecoil));
}

TEST_CASE("Angel's Aim page: off, the page as it was; other games as before",
          "[frame_xr][kiuaim]") {
    Drawn d;
    const MenuModel m = ModelFor(Case::ModeOn);
    MenuView v = AimView();
    v.aim_group = static_cast<int>(AimGroup::Aiming); // ignored while off
    d.Draw(m, v);
    CHECK(d.Drew(kAngelsAimSettings));
    CHECK_FALSE(d.Hit(Id::AngelsAimGroup, static_cast<int>(AimGroup::Aiming)));
    CHECK(d.menu.MaxScroll() > 2000.0f);
    // Another game: no segment with the page on or off, and its other tabs draw the same.
    const MenuModel other = ModelFor(Case::OtherGame);
    for (const Tab tab : {Tab::Settings, Tab::Controls, Tab::Mods, Tab::Stats}) {
        MenuView ov;
        ov.tab = tab;
        ov.section = SettingsSection::Immersive;
        ov.mods_segment = ModsSegment::AngelsAim; // falls back to Cheats
        d.Draw(other, ov);
        const auto before = d.menu.Hits();
        const auto texts_before = d.texts.size();
        const PageOn on;
        d.Draw(other, ov);
        INFO("tab " << static_cast<int>(tab));
        REQUIRE(d.menu.Hits().size() == before.size());
        for (std::size_t i = 0; i < before.size(); i++) {
            CHECK(d.menu.Hits()[i].t == before[i].t);
        }
        CHECK(d.texts.size() == texts_before);
        CHECK_FALSE(d.Hit(Id::AngelsAimGroup, 0));
    }
    // Kid Icarus's own Settings and Controls rows are not the page's: the same with it on.
    for (const Tab tab : {Tab::Settings, Tab::Controls, Tab::Stats}) {
        MenuView kv;
        kv.tab = tab;
        kv.section = SettingsSection::Immersive;
        d.Draw(m, kv);
        const std::size_t before = d.menu.Hits().size();
        const PageOn on;
        d.Draw(m, kv);
        CHECK(d.menu.Hits().size() == before);
    }
    CHECK(d.Hit(Id::WristSide, 0)); // Wrist Side stays on the Stats tab
}
