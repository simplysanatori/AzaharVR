// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiuaim: the Angel's Aim page (menu/angels_aim_tab.h), the only place of the rows that
// act in Kid Icarus: Uprising alone.

#include <algorithm>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "frame_xr/immersive_names.h"
#include "frame_xr/menu/angels_aim_tab.h"
#include "frame_xr/menu/tab_menu.h"

using namespace FrameXr::Menu;

namespace {

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

TEST_CASE("Angel's Aim page: rows that wait", "[frame_xr][kiuaim]") {
    MenuModel m = ModelFor(Case::ModeOn);
    // Reset Bottom Screen Position: In View, and moved.
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
}

TEST_CASE("Angel's Aim page: the approved tips, each within two lines", "[frame_xr][kiuaim]") {
    CHECK(std::string(AimPageTip(Id::GyroAim)) ==
          "Hold the grip to free your aim.");
    CHECK(std::string(AimPageTip(Id::GyroSensitivity)) ==
          "How far the reticle moves as you turn your hand. Applies to every game.");
    CHECK(std::string(AimPageTip(Id::Recoil)) ==
          "Haptic feedback in the aiming controller on each shot.");
    CHECK(std::string(AimPageTip(Id::RecoilCharged)) == "How strong it is for a charged shot.");
    CHECK(std::string(AimPageTip(Id::RecoilRapid)) ==
          "How strong it is for each rapid-fire shot.");
    CHECK(AimPageTip(Id::KiuBottomScreen) == nullptr); // its own constant, everywhere
    CHECK(std::string(kBottomScreenTip) ==
          "In View: low in front of you; hold a grip on it to move it. Wrist: "
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

// ---- The front view, the pages, More ----
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
                m.kiu_power_menu = bits % 5;
                m.kiu_talk_portraits = bits & 1;
                m.kiu_portraits_linked = (bits >> 1) & 1;
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
            // Every row of the two lists, less the two that wait; Immersive Mode is the front
            // view's own.
            std::vector<Id> before;
            if (c != Case::OtherGame) {
                for (const auto& list : {AimingRows(m), AngelsAimFirstPersonRows(m)}) {
                    for (const Id id : list) {
                        if (AimPageRowShown(m, id)) {
                            before.push_back(id);
                        }
                    }
                }
                if (AngelsAimRowsShown(m.mods.angels_aim.shown && m.mods.angels_aim.mapped,
                                       m.immersive_mode)) {
                    before.push_back(Id::FoveatedRendering);
                    if (m.foveated_rendering > 0) {
                        before.push_back(Id::FoveationInnerRadius);
                        before.push_back(Id::FoveationMiddleRadius);
                        before.push_back(Id::FoveationMiddleRate);
                        before.push_back(Id::FoveationOuterRate);
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
          std::vector<Id>{Id::GyroAim, Id::TriggerLayout, Id::KiuReticleSize, Id::KiuMarkerSize,
                          Id::LaserSight});
    CHECK(AimPageRows(m, AimGroup::Aiming).more ==
          std::vector<Id>{Id::KeepOnScreen, Id::GripFreesAim, Id::TurnZoneFrom, Id::TurnZone,
                          Id::TurnSpeed, Id::TurnSpeedVertical, Id::CalibrateAim});
    CHECK(AimPageRows(m, AimGroup::View).rows ==
          std::vector<Id>{Id::KiuOverShoulder, Id::KiuSnapTurn, Id::KiuCutscenes,
                          Id::KiuStickClick, Id::Kiu60Fps, Id::FoveatedRendering});
    CHECK(AimPageRows(m, AimGroup::View).more.empty());
    {
        MenuModel mf = m;
        mf.foveated_rendering = 2;
        CHECK(AimPageRows(mf, AimGroup::View).more ==
              std::vector<Id>{Id::FoveationInnerRadius, Id::FoveationMiddleRadius,
                              Id::FoveationMiddleRate, Id::FoveationOuterRate});
    }
    CHECK(AimPageRows(m, AimGroup::Hud).rows ==
          std::vector<Id>{Id::KiuHud, Id::KiuHudSize, Id::KiuHudOpacity, Id::KiuSubtitleSize,
                          Id::KiuPowerMenu});
    {
        MenuModel p = m; // HUD: Game's Own
        p.kiu_hud = 0;
        CHECK_FALSE(Has(AimPageRows(p, AimGroup::Hud).rows, Id::KiuHudOpacity));
    }
    for (const int place : {0, 1, 2, 3, 4}) { // Off, Off Hand, then the HUD's places
        MenuModel p = m;
        p.kiu_power_menu = place;
        const std::vector<Id> more = AimPageRows(p, AimGroup::Hud).more;
        CHECK(Has(more, Id::KiuPowerMenuOpacity) == (place != 0));
        CHECK(Has(more, Id::KiuPowerMenuAcross) == (place >= 2));
        CHECK(Has(more, Id::KiuPowerMenuHeight) == (place >= 2));
    }
    CHECK(AimPageRows(m, AimGroup::BottomScreen).rows ==
          std::vector<Id>{Id::KiuBottomScreen, Id::KiuBottomSize, Id::KiuBottomOpacity,
                          Id::KiuWristAngle, Id::KiuWristAlong});
    // AzaharVR: Character Portraits shown and Separate by default (the owner's defaults)
    CHECK(AimPageRows(m, AimGroup::BottomScreen).more ==
          std::vector<Id>{Id::KiuTalkPortraits, Id::KiuTalkPortraitsOpacity, Id::KiuPortraitsLinked,
                          Id::KiuPortraitsAcross, Id::KiuPortraitsHeight});
    for (const int shown : {0, 1}) {
        MenuModel p = m;
        p.kiu_talk_portraits = shown;
        CHECK(Has(AimPageRows(p, AimGroup::BottomScreen).more, Id::KiuTalkPortraitsOpacity) ==
              (shown != 0));
        for (const int hud : {0, 1}) { // Linked is with Subtitle Height: HUD: In View
            for (const int linked : {0, 1}) {
                p.kiu_hud = hud;
                p.kiu_portraits_linked = linked;
                const std::vector<Id> more = AimPageRows(p, AimGroup::BottomScreen).more;
                const bool own = shown != 0 && (hud == 0 || linked == 0);
                CHECK(Has(more, Id::KiuPortraitsLinked) == (shown != 0 && hud != 0));
                CHECK(Has(more, Id::KiuPortraitsAcross) == own);
                CHECK(Has(more, Id::KiuPortraitsHeight) == own);
            }
        }
    }
    CHECK(AimPageRows(m, AimGroup::Weapon).rows ==
          std::vector<Id>{Id::WeaponInHand, Id::MuzzleFlash, Id::Projectiles, Id::Recoil,
                          Id::KiuHideOffHand}); // Free Hand
    CHECK(Has(AimPageRows(m, AimGroup::Weapon).more, Id::WeaponOpacity));
}

TEST_CASE("Angel's Aim page: presses, Back and B, and what is kept", "[frame_xr][kiuaim]") {
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
    REQUIRE(AimPagePress(m, v, static_cast<int>(AimGroup::ModsAndPack)));
    CHECK_FALSE(AimPagePress(m, v, kAimMoreIndex));
    REQUIRE(AimPageBack(m, v));
    REQUIRE(AimPagePress(m, v, static_cast<int>(AimGroup::BottomScreen)));
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
}

TEST_CASE("Angel's Aim page: drawn, the front view never scrolls and every row is a target",
          "[frame_xr][kiuaim]") {
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
            CHECK_FALSE(d.Hit(Id::Scrollbar, -1));
            d.CheckFit("front");
        }
    }
}

TEST_CASE("Angel's Aim page: each group's page by the beam, its rows targets, B back",
          "[frame_xr][kiuaim]") {
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
                        if (d.menu.MaxScroll() > 0.0f) {
                            v.scroll[mods] = d.menu.MaxScroll();
                            d.Draw(m, v);
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
                        if (g != AimGroup::View) {
                            CHECK(d.menu.MaxScroll() == 0.0f); // other groups' pages never scroll
                        }
                        if (d.menu.MaxScroll() > 0.0f) {
                            v.scroll[mods] = d.menu.MaxScroll();
                            d.Draw(m, v);
                        }
                        CHECK((d.Hit(Id::AngelsAimGroup, kAimMoreIndex) != nullptr) ==
                              !page.more.empty());
                        v.scroll[mods] = 0.0f;
                        d.Draw(m, v);
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

TEST_CASE("Angel's Aim page: other games have none of it", "[frame_xr][kiuaim]") {
    Drawn d;
    const MenuModel other = ModelFor(Case::OtherGame);
    CHECK(AimScopeRows(other).empty());
    for (const Tab tab : {Tab::Settings, Tab::Controls, Tab::Mods, Tab::Stats}) {
        MenuView ov;
        ov.tab = tab;
        ov.section = SettingsSection::Immersive;
        ov.mods_segment = ModsSegment::AngelsAim; // falls back to Cheats
        d.Draw(other, ov);
        INFO("tab " << static_cast<int>(tab));
        CHECK_FALSE(d.Hit(Id::AngelsAimGroup, 0));
        CHECK_FALSE(d.Hit(Id::AngelsAimLink, -1));
    }
    MenuView kv;
    kv.tab = Tab::Stats;
    d.Draw(ModelFor(Case::ModeOn), kv);
    CHECK(d.Hit(Id::WristSide, 0)); // Wrist Side is the Stats tab's
}

// ---- AzaharFrame kiuaim: the rows that act in Kid Icarus: Uprising alone are on the page only ----
namespace {

/// The menu in each of the game's copies: the European one (mapped by its title), the USA one
/// before its code is found and after, and the others (never mapped).
struct Copy {
    const char* name;
    bool mapped, recommended;
};
constexpr Copy kCopies[] = {{"Europe", true, true},
                            {"USA, mapped late", true, false},
                            {"USA, not mapped yet", false, false},
                            {"Japan or Korea", false, false}};

/// Every combination of the settings that show rows: Aiming's and first person's.
struct RowSettings {
    int aim, recoil, weapon, flash, shots;                             // 3 * 2^4
    int bottom, moved, view, hud, linked, power, talk, portraits_linked; // 3 * 2^4 * 5 * 2^2
};
std::vector<RowSettings> AimSettings() {
    std::vector<RowSettings> out;
    for (int aim = 0; aim < 3; aim++) {
        for (int bits = 0; bits < 16; bits++) {
            RowSettings s{};
            s.aim = aim;
            s.recoil = bits & 1;
            s.weapon = (bits >> 1) & 1;
            s.flash = (bits >> 2) & 1;
            s.shots = (bits >> 3) & 1;
            out.push_back(s);
        }
    }
    return out;
}
std::vector<RowSettings> FirstPersonSettings() {
    std::vector<RowSettings> out;
    for (int bottom = 0; bottom < 3; bottom++) {
        for (int power = 0; power < 5; power++) {
            for (int bits = 0; bits < 64; bits++) {
                RowSettings s{};
                s.bottom = bottom;
                s.power = power;
                s.moved = bits & 1;
                s.view = (bits >> 1) & 1;
                s.hud = (bits >> 2) & 1;
                s.linked = (bits >> 3) & 1;
                s.talk = (bits >> 4) & 1;
                s.portraits_linked = (bits >> 5) & 1;
                out.push_back(s);
            }
        }
    }
    return out;
}

MenuModel ModelOf(const Copy& copy, int mode) {
    MenuModel m;
    m.game_title = "Kid Icarus: Uprising";
    m.scope = 1;
    m.mods.angels_aim.shown = true;
    m.mods.angels_aim.mapped = copy.mapped;
    m.mods.angels_aim.recommended = copy.recommended;
    m.mods.angels_aim.report_url = kAngelsAimIssuesUrl;
    m.immersive_mode = mode;
    return m;
}
void PutAim(MenuModel& m, const RowSettings& s) {
    m.gyro_aim = s.aim;
    m.recoil = s.recoil;
    m.weapon_in_hand = s.weapon;
    m.muzzle_flash = s.flash;
    m.projectiles = s.shots;
}
void PutFirstPerson(MenuModel& m, const RowSettings& s) {
    m.kiu_bottom_screen = s.bottom;
    m.kiu_bottom_moved = s.moved;
    m.kiu_view = s.view;
    m.kiu_hud = s.hud;
    m.kiu_hud_linked = s.linked;
    m.kiu_power_menu = s.power;
    m.kiu_talk_portraits = s.talk;
    m.kiu_portraits_linked = s.portraits_linked;
}

/// The rows that left the general tabs: Kid Icarus: Uprising's of Aiming's rows (Controls) and the
/// first-person rows (Settings, Immersive).
std::vector<Id> RowsThatLeft(const MenuModel& m) {
    std::vector<Id> out;
    for (const Id id : AimingRows(m)) {
        if (KiuOnlyControlsRow(id)) {
            out.push_back(id);
        }
    }
    const auto fp = AngelsAimFirstPersonRows(m);
    out.insert(out.end(), fp.begin(), fp.end());
    return out;
}
/// On the page too, as they act in any game.
const std::vector<Id> kAnyGame{Id::GyroAim,       Id::GyroSensitivity, Id::Recoil,
                               Id::RecoilCharged, Id::RecoilRapid,     Id::LaserSight,
                               Id::KeepOnScreen,  Id::GripFreesAim};

/// The targets of a tab, all the way down.
std::set<Id> TabIds(Drawn& d, const MenuModel& m, Tab tab, SettingsSection section,
                    ControlsSection controls = ControlsSection::Buttons) {
    std::set<Id> ids;
    MenuView v;
    v.tab = tab;
    v.section = section;
    v.controls_section = controls;
    for (float& scroll = v.scroll[static_cast<std::size_t>(tab)];; scroll += 200.0f) {
        d.Draw(m, v);
        for (const auto& h : d.menu.Hits()) {
            ids.insert(h.t.id);
        }
        if (scroll >= d.menu.MaxScroll()) {
            return ids;
        }
    }
}
/// The targets a group's page or its More shows, by the beam: pressed from the front view.
std::set<Id> PressedIds(Drawn& d, const MenuModel& m, AimGroup g, bool more) {
    std::set<Id> ids;
    MenuView v;
    AimPageOpen(v, -1);
    d.Draw(m, v);
    const auto* row = d.Hit(Id::AngelsAimGroup, static_cast<int>(g));
    if (!row || !AimPagePress(m, v, d.menu.HitTest(row->r.Cx(), row->r.Cy()).index)) {
        return ids;
    }
    d.Draw(m, v);
    if (more) {
        if (d.menu.MaxScroll() > 0.0f) {
            v.scroll[static_cast<std::size_t>(Tab::Mods)] = d.menu.MaxScroll();
            d.Draw(m, v);
        }
        const auto* mr = d.Hit(Id::AngelsAimGroup, kAimMoreIndex);
        if (!mr || !AimPagePress(m, v, d.menu.HitTest(mr->r.Cx(), mr->r.Cy()).index)) {
            return ids;
        }
    }
    for (float& scroll = v.scroll[static_cast<std::size_t>(Tab::Mods)];; scroll += kRowH) {
        d.Draw(m, v);
        for (const auto& h : d.menu.Hits()) {
            if (h.r.H() >= 40.0f) {
                ids.insert(h.t.id);
            }
        }
        if (scroll >= d.menu.MaxScroll()) {
            return ids;
        }
    }
}

} // namespace

TEST_CASE("Angel's Aim page: every row that left the general tabs has its place on it",
          "[frame_xr][kiuaim][kiuaimonly]") {
    Drawn d;
    const auto aims = AimSettings();
    const auto views = FirstPersonSettings();
    std::set<std::tuple<int, bool, std::vector<Id>>> drawn; // each list of rows drawn once
    const auto check = [&](const MenuModel& m, const std::string& state) {
        const std::vector<Id> left = RowsThatLeft(m);
        std::vector<Id> on_page;
        for (const AimGroup g : AimFrontGroups(m)) {
            const AimPage page = AimPageRows(m, g);
            for (const bool more : {false, true}) {
                const auto& ids = more ? page.more : page.rows;
                on_page.insert(on_page.end(), ids.begin(), ids.end());
                if (ids.empty() || !drawn.emplace(static_cast<int>(g), more, ids).second) {
                    continue;
                }
                const std::set<Id> seen = PressedIds(d, m, g, more);
                for (const Id id : ids) {
                    INFO(state << ": " << AimGroupName(g) << ", " << LabelFor(id));
                    CHECK(seen.count(id) == 1);
                }
            }
        }
        // Without a CHECK each (hundreds of thousands): what is wrong, if anything.
        std::string lost, twice, both;
        for (const Id id : left) {
            // Reset Bottom Screen Position has nothing to put back until the screen was moved.
            const bool waits = id == Id::KiuBottomReset && !m.kiu_bottom_moved;
            if (!waits && !Has(on_page, id)) {
                lost += LabelFor(id) + "; ";
            }
            if (std::count(on_page.begin(), on_page.end(), id) > 1) {
                twice += LabelFor(id) + "; ";
            }
        }
        for (const Id id : ControlsAimingRows(m)) {
            if (Has(left, id) || (Has(on_page, id) && !Has(kAnyGame, id))) {
                both += LabelFor(id) + "; ";
            }
        }
        if (!lost.empty() || !twice.empty() || !both.empty()) {
            FAIL_CHECK(state << ": lost " << lost << " twice " << twice << " both " << both);
        }
    };
    std::size_t states = 0;
    for (const Copy& copy : kCopies) {
        for (int mode = 0; mode <= 4; mode++) {
            const std::string at = std::string(copy.name) + ", mode " + std::to_string(mode);
            MenuModel m = ModelOf(copy, mode);
            const bool first_person = copy.mapped && mode == 4;
            for (const RowSettings& view : views) {
                PutFirstPerson(m, view);
                if (!first_person) { // no such row, whatever the settings
                    if (!AngelsAimFirstPersonRows(m).empty()) {
                        FAIL_CHECK(at << ": a first-person row");
                    }
                    if (&view != &views.front()) {
                        continue;
                    }
                }
                for (const RowSettings& aim : aims) {
                    PutAim(m, aim);
                    check(m, at);
                    states++;
                }
            }
        }
    }
    CHECK(states == 18 * aims.size() + 2 * aims.size() * views.size());
    CHECK(drawn.size() > 20);
}

TEST_CASE("Angel's Aim page: the general tabs have none of its rows",
          "[frame_xr][kiuaim][kiuaimonly]") {
    Drawn d;
    // Everything that brings a row on, and one setting changed at a time.
    std::vector<MenuModel> models;
    for (const Copy& copy : kCopies) {
        for (int mode = 0; mode <= 4; mode++) {
            for (const int aim : {0, 1, 2}) {
                for (const int scope : {0, 1}) {
                    if (scope == 0 && mode != 4) {
                        continue;
                    }
                    MenuModel m = ModelOf(copy, mode);
                    m.scope = scope;
                    RowSettings s{aim, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 0};
                    PutAim(m, s);
                    PutFirstPerson(m, s);
                    models.push_back(m);
                    if (copy.mapped && mode == 4 && scope == 1) {
                        for (const int bottom : {0, 2}) {
                            m.kiu_bottom_screen = bottom;
                            m.kiu_hud = bottom / 2;
                            m.kiu_hud_linked = 0;
                            models.push_back(m);
                        }
                    }
                }
            }
        }
    }
    for (const MenuModel& m : models) {
        INFO("mapped " << m.mods.angels_aim.mapped << ", mode " << m.immersive_mode << ", aim "
                       << m.gyro_aim << ", scope " << m.scope);
        std::set<Id> ids;
        for (int section = 0; section < static_cast<int>(ControlsSection::Count); section++) {
            const auto more = TabIds(d, m, Tab::Controls, SettingsSection::General,
                                     static_cast<ControlsSection>(section));
            ids.insert(more.begin(), more.end());
        }
        for (int section = 0; section < static_cast<int>(SettingsSection::Count); section++) {
            if (section != static_cast<int>(SettingsSection::Immersive) && m.gyro_aim != 2) {
                continue; // General and Graphics have no row that depends on it
            }
            const auto more = TabIds(d, m, Tab::Settings, static_cast<SettingsSection>(section));
            ids.insert(more.begin(), more.end());
        }
        for (int i = 0; i < kIdCount; i++) {
            const Id id = static_cast<Id>(i);
            if (KiuOnlyControlsRow(id)) {
                INFO(LabelFor(id));
                CHECK(ids.count(id) == 0);
            }
        }
        CHECK(ids.count(Id::CalibrateAimReset) == 0);
        for (const Id id : RowsThatLeft(m)) {
            INFO(LabelFor(id));
            CHECK(ids.count(id) == 0);
        }
        // The rows that act in any game stay on Controls, with the conditions they had.
        for (const Id id : kAnyGame) {
            INFO(LabelFor(id));
            CHECK((ids.count(id) == 1) == Has(AimingRows(m), id));
        }
        // Immersive keeps its others; the link with Angel's Aim only.
        CHECK(ids.count(Id::ImmersiveMode) == 1);
        CHECK((ids.count(Id::AngelsAimLink) == 1) == (m.immersive_mode == 4));
        CHECK(ids.count(Id::GameFov) == 1);
        CHECK(ids.count(Id::WorldScale) == 1);
        CHECK(ids.count(Id::ScreenAsPanel) == 1);
        // Check Aim's Reset is on the page, with a map of the game's own.
        if (m.gyro_aim == 2) {
            MenuModel cal = m;
            cal.aim_calibrated = cal.aim_calibration_ok = true;
            CHECK(PressedIds(d, cal, AimGroup::Aiming, true).count(Id::CalibrateAimReset) == 1);
        }
    }
}

TEST_CASE("Angel's Aim page: its scope counts what the general tabs did",
          "[frame_xr][kiuaim][kiuaimonly]") {
    const auto sorted = [](std::vector<Id> v) {
        std::sort(v.begin(), v.end());
        return v;
    };
    for (const Copy& copy : kCopies) {
        for (int mode = 0; mode <= 4; mode++) {
            for (const int aim : {0, 1, 2}) {
                for (const int on : {0, 1}) {
                    INFO(copy.name << ", mode " << mode << ", aim " << aim << ", on " << on);
                    MenuModel m = ModelOf(copy, mode);
                    RowSettings s{aim, on, on, on, on, 1, on, on, on, on, 2 * on, on, 0};
                    PutAim(m, s);
                    PutFirstPerson(m, s);
                    for (int i = 0; i < kIdCount; i++) { // the game has its own value of each
                        m.game_value[static_cast<std::size_t>(i)] = true;
                    }
                    // The general tabs: none of the page's own.
                    auto general = GameValueRows(m, Tab::Controls);
                    CHECK(general == ControlsScopeRows());
                    const auto immersive =
                        GameValueRows(m, Tab::Settings, SettingsSection::Immersive);
                    general.insert(general.end(), immersive.begin(), immersive.end());
                    for (const Id id : general) {
                        INFO(LabelFor(id));
                        CHECK_FALSE(KiuOnlyControlsRow(id));
                        CHECK_FALSE(Has(AngelsAimFirstPersonRows(m), id));
                    }
                    // The page: Immersive Mode, what is on it, and Kid Icarus: Uprising's own that
                    // Aiming or a switch hides (Controls counted them shown or not).
                    std::vector<Id> expect{Id::ImmersiveMode};
                    for (const Id id : PageIds(m)) {
                        if (PerGameSetting(id)) {
                            expect.push_back(id);
                        }
                    }
                    for (int i = 0; i < kIdCount; i++) {
                        const Id id = static_cast<Id>(i);
                        if (KiuOnlyControlsRow(id) && PerGameSetting(id) && !Has(expect, id)) {
                            expect.push_back(id);
                        }
                    }
                    const auto page = GameValueRows(m, Tab::Mods);
                    CHECK(sorted(page) == sorted(expect));
                    for (const Id id : RowsThatLeft(m)) {
                        INFO(LabelFor(id));
                        CHECK((!PerGameSetting(id) || Has(page, id)));
                    }
                    // Its toggle, its count and Use Global Values for All.
                    Drawn d;
                    MenuView v = AimView();
                    d.Draw(m, v);
                    CHECK(d.Hit(Id::Scope, 0));
                    CHECK(d.Hit(Id::ScopeMore, -1));
                    v.tooltip = Id::Scope;
                    v.tooltip_index = 1;
                    v.hover = {Id::Scope, 1};
                    d.Draw(m, v);
                    CHECK(d.Drew(GameValueCount(expect.size())));
                    v.tooltip = Id::None;
                    v.open_menu = Id::ScopeMore;
                    d.Draw(m, v);
                    CHECK(d.Hit(Id::ScopeMore, 0));
                }
            }
        }
    }
    // Global, with the game's own value: locked on the page as it was on its tab.
    MenuModel m = ModelOf(kCopies[0], 4);
    RowSettings s{2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 0};
    PutAim(m, s);
    PutFirstPerson(m, s);
    m.scope = 0;
    for (const Id id : {Id::WeaponInHand, Id::KiuSnapTurn, Id::TriggerLayout}) {
        m.locked[static_cast<std::size_t>(id)] = Lock::GameValue;
    }
    Drawn d;
    MenuView v;
    AimPageOpen(v, static_cast<int>(AimGroup::Weapon));
    d.Draw(m, v);
    CHECK_FALSE(d.Hit(Id::WeaponInHand, -1));
    CHECK(d.Hit(Id::WeaponInHand, kLabelIndex));
    CHECK(d.Hit(Id::MuzzleFlash, -1));
    AimPageOpen(v, static_cast<int>(AimGroup::View));
    d.Draw(m, v);
    CHECK_FALSE(d.Hit(Id::KiuSnapTurn, 1));
    CHECK(d.Hit(Id::KiuOverShoulder, 1));
    AimPageOpen(v, static_cast<int>(AimGroup::Aiming));
    d.Draw(m, v);
    CHECK_FALSE(d.Hit(Id::TriggerLayout, 1));
    CHECK(d.Hit(Id::GyroAim, 1));
}

TEST_CASE("Angel's Aim page: the link, the prompt and the Mods tab land on it",
          "[frame_xr][kiuaim][kiuaimonly]") {
    Drawn d;
    for (const Copy& copy : kCopies) {
        MenuModel m = ModelOf(copy, 4);
        m.gyro_aim = 2;
        INFO(copy.name);
        // The link: under Immersive Mode, its whole row a target; the front view.
        MenuView v;
        v.tab = Tab::Settings;
        v.section = SettingsSection::Immersive;
        v.scroll[static_cast<std::size_t>(Tab::Mods)] = 300.0f; // left by another segment
        v.aim_more = true;
        d.Draw(m, v);
        const auto* link = d.Hit(Id::AngelsAimLink, -1);
        REQUIRE(link);
        CHECK(d.menu.HitTest(link->r.Cx(), link->r.Cy()) == Target{Id::AngelsAimLink});
        CHECK(d.Drew("Angel's Aim"));
        AimPageOpen(v, -1);
        AimPageKeep(m, v);
        CHECK(v.tab == Tab::Mods);
        CHECK(v.page == Page::Tabs);
        CHECK(v.mods_segment == ModsSegment::AngelsAim);
        CHECK(AimPageShown(m, v).group == -1);
        CHECK(v.scroll[static_cast<std::size_t>(Tab::Mods)] == 0.0f);
        d.Draw(m, v);
        CHECK(d.Drew(kAimPageTitle));
        CHECK(d.Hit(Id::AngelsAimGroup, static_cast<int>(AimGroup::Aiming)));
        // Show Angel's Aim, from the prompt on any tab: the front view, a group open before or not.
        for (const Tab tab : {Tab::Controls, Tab::Mods}) {
            MenuView p = v;
            p.tab = tab;
            p.angels_intro = true;
            d.Draw(m, p);
            CHECK(d.Hit(Id::DialogExtra, -1));
            p.angels_intro = false;
            p.aim_group = static_cast<int>(AimGroup::Weapon);
            p.aim_more = true;
            AimPageOpen(p, -1);
            AimPageKeep(m, p);
            CHECK(AimPageShown(m, p).group == -1);
            CHECK_FALSE(AimPageShown(m, p).more);
            d.Draw(m, p);
            CHECK(d.Hit(Id::AngelsAimGroup, static_cast<int>(AimGroup::Aiming)));
            CHECK(d.Hit(Id::AngelsAimGroup, static_cast<int>(AimGroup::Weapon)));
        }
        // The Mods tab reopens on the page when it was left there: another tab, the menu closed
        // and opened (its Main tab), then Mods again.
        AimPageOpen(v, static_cast<int>(AimGroup::Weapon));
        for (const Tab tab : {Tab::Controls, Tab::Main, Tab::Mods}) {
            v.tab = tab;
            AimPageKeep(m, v);
        }
        CHECK(v.mods_segment == ModsSegment::AngelsAim);
        CHECK(AimPageShown(m, v).group == -1);
        d.Draw(m, v);
        CHECK(d.Drew(kAimPageTitle));
        // Another game has no page: Cheats, and it stays there.
        MenuView other = v;
        AimPageKeep(ModelFor(Case::OtherGame), other);
        CHECK(other.mods_segment == ModsSegment::Cheats);
        AimPageKeep(m, other);
        CHECK(other.mods_segment == ModsSegment::Cheats);
    }
}

TEST_CASE("Angel's Aim page: a copy mapped late keeps every row", "[frame_xr][kiuaim][kiuaimonly]") {
    Drawn d;
    MenuModel early = ModelOf(kCopies[2], 4), late = ModelOf(kCopies[1], 4);
    for (MenuModel* m : {&early, &late}) {
        RowSettings s{2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 0};
        PutAim(*m, s);
        PutFirstPerson(*m, s);
    }
    // Wherever the menu is when it changes, either way.
    for (int group = -1; group < static_cast<int>(AimGroup::Count); group++) {
        for (const bool more : {false, true}) {
            MenuView v;
            AimPageOpen(v, group);
            v.aim_more = more;
            v.scroll[static_cast<std::size_t>(Tab::Mods)] = 500.0f;
            for (const MenuModel* m : {&early, &late, &early, &late}) {
                d.Draw(*m, v); // the frame before the fields follow
                AimPageKeep(*m, v);
                d.Draw(*m, v);
                CHECK(d.menu.Hits().size() > 0);
                const int shown = AimPageShown(*m, v).group;
                CHECK(v.aim_group == shown);
                const auto front = AimFrontGroups(*m);
                CHECK((shown == -1 || std::find(front.begin(), front.end(),
                                                static_cast<AimGroup>(shown)) != front.end()));
            }
        }
    }
    const auto before = PageIds(early), after = PageIds(late);
    for (const Id id : before) {
        INFO(LabelFor(id));
        CHECK(Has(after, id));
    }
    for (const Id id : AngelsAimFirstPersonRows(late)) {
        INFO(LabelFor(id));
        CHECK(Has(after, id));
    }
    // The general tabs stay as they are but for HUD Spacing (with HUD: In View).
    for (const MenuModel* m : {&early, &late}) {
        const auto ids = TabIds(d, *m, Tab::Settings, SettingsSection::Immersive);
        CHECK(ids.count(Id::AngelsAimLink) == 1);
        CHECK((ids.count(Id::HudSpacingX) == 1) == !m->mods.angels_aim.mapped);
    }
}

// AzaharFrame kiuaim: Power Menu and its opacity (the power lane's settings).
TEST_CASE("Angel's Aim page: Power Menu", "[frame_xr][kiuaim]") {
    for (const Id id : {Id::KiuPowerMenu, Id::KiuPowerMenuAcross, Id::KiuPowerMenuHeight,
                        Id::KiuPowerMenuOpacity}) {
        CHECK(PerGameSetting(id));
        CHECK(SectionOf(id) == SettingsSection::Immersive);
        CHECK_FALSE(ExplainFor(id).empty());
    }
    CHECK(LabelFor(Id::KiuPowerMenu) == "Power Menu");
    CHECK(LabelFor(Id::KiuPowerMenuOpacity) == "Power Menu Opacity");
    CHECK(ExplainFor(Id::KiuPowerMenu) == "Off Hand: when you pick one. Elsewhere: always in battles.");
    CHECK(OptionsFor(Id::KiuPowerMenu) ==
          std::vector<std::string>{"Off", "Off Hand", "Health Bar", "Middle", "Score"}); // stored order
    CHECK(LabelFor(Id::KiuPowerMenuAcross) == "Power Menu Left/Right");
    CHECK(LabelFor(Id::KiuPowerMenuHeight) == "Power Menu Height");
    CHECK(MenuModel{}.kiu_power_menu == 3); // AzaharVR: the owner's defaults
    CHECK(MenuModel{}.kiu_power_menu_opacity == 50); // AzaharVR: the owner's defaults
    CHECK_FALSE(Has(AngelsAimFirstPersonRows(ModelFor(Case::OtherGame)), Id::KiuPowerMenu));
    CHECK_FALSE(Has(AngelsAimFirstPersonRows(ModelFor(Case::ModeOff)), Id::KiuPowerMenu));
    CHECK(Has(AngelsAimFirstPersonRows(ModelFor(Case::ModeOn)), Id::KiuPowerMenu));
}

// AzaharFrame kiuhs3a: HUD Opacity, after HUD Size.
TEST_CASE("Angel's Aim page: HUD Opacity", "[frame_xr][kiuaim][kiuhs3a]") {
    CHECK(PerGameSetting(Id::KiuHudOpacity));
    CHECK(SectionOf(Id::KiuHudOpacity) == SettingsSection::Immersive);
    CHECK(LabelFor(Id::KiuHudOpacity) == "HUD Opacity");
    CHECK(ExplainFor(Id::KiuHudOpacity) == "How solid the health bar and score are.");
    CHECK(SliderFor(Id::KiuHudOpacity).min == 20.0f);
    CHECK(SliderFor(Id::KiuHudOpacity).max == 100.0f);
    Kit kit{1.0f};
    CHECK(kit.TextWidth(Type::Tooltip, kKiuHudOpacityTip) + 28.0f <= 2.0f * kPanelW * 0.7f);
    MenuModel m = ModelFor(Case::ModeOn);
    CHECK(m.kiu_hud_opacity == 100);
    CHECK(Has(AimScopeRows(m), Id::KiuHudOpacity)); // the page's count
    m.kiu_hud = 0;                                  // Game's Own
    CHECK_FALSE(Has(AimScopeRows(m), Id::KiuHudOpacity));
}

// AzaharFrame kiu60: 60 FPS, under View's More.
TEST_CASE("Angel's Aim page: 60 FPS", "[frame_xr][kiuaim][kiu60]") {
    CHECK(PerGameSetting(Id::Kiu60Fps));
    CHECK(SectionOf(Id::Kiu60Fps) == SettingsSection::Immersive);
    CHECK(LabelFor(Id::Kiu60Fps) == "60 FPS");
    CHECK(OptionsFor(Id::Kiu60Fps) == std::vector<std::string>{"Off", "On"}); // stored order
    CHECK(ExplainFor(Id::Kiu60Fps) ==
          "Patches the game to 60 FPS when it starts, at normal speed. Higher CPU and GPU load.");
    Kit kit{1.0f};
    CHECK(kit.TextWidth(Type::Tooltip, kKiu60FpsTip) + 28.0f <= 2.0f * kPanelW * 0.7f);
    CHECK(MenuModel{}.kiu_60fps == 0);
    CHECK_FALSE(Has(AngelsAimFirstPersonRows(ModelFor(Case::OtherGame)), Id::Kiu60Fps));
    CHECK_FALSE(Has(AngelsAimFirstPersonRows(ModelFor(Case::ModeOff)), Id::Kiu60Fps));
    const MenuModel m = ModelFor(Case::ModeOn);
    CHECK(Has(AngelsAimFirstPersonRows(m), Id::Kiu60Fps));
    CHECK(Has(AimScopeRows(m), Id::Kiu60Fps)); // the page's count
    CHECK_FALSE(Has(AimScopeRows(ModelFor(Case::OtherGame)), Id::Kiu60Fps));
    // Both 60 FPS and Foveated Rendering sit directly on View (not hidden under More).
    MenuView v = AimView();
    REQUIRE(AimPagePress(m, v, static_cast<int>(AimGroup::View)));
    Drawn d;
    d.Draw(m, v);
    CHECK(d.Drew("View"));
    CHECK(d.Drew("60 FPS"));
    CHECK(d.Drew(FrameXr::Immersive::Names::kExperimental));
    CHECK(d.Hit(Id::Kiu60Fps, 0));
    CHECK(d.Hit(Id::Kiu60Fps, 1));
    if (d.menu.MaxScroll() > 0.0f) {
        v.scroll[static_cast<std::size_t>(Tab::Mods)] = d.menu.MaxScroll();
        d.Draw(m, v);
    }
    CHECK(d.Drew("Foveated Rendering"));
    CHECK(d.Drew(FrameXr::Immersive::Names::kExperimental));
    CHECK(d.Hit(Id::FoveatedRendering, 0));
    CHECK(d.Hit(Id::FoveatedRendering, 1));
    CHECK(d.Hit(Id::FoveatedRendering, 2));
    CHECK(d.Hit(Id::FoveatedRendering, 3));
    // With Foveated Rendering on, its four ring controls live in View · More.
    MenuModel mf = m;
    mf.foveated_rendering = 2;
    v = AimView();
    REQUIRE(AimPagePress(mf, v, static_cast<int>(AimGroup::View)));
    REQUIRE(AimPagePress(mf, v, kAimMoreIndex));
    d.Draw(mf, v);
    CHECK(d.Drew("View \xC2\xB7 More"));
    CHECK(d.Drew("Inner Ring Size"));
    CHECK(d.Drew("Middle Ring Size"));
    CHECK(d.Drew("Middle Ring Resolution"));
    CHECK(d.Drew("Outer Ring Resolution"));
}
