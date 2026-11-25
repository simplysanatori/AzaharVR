// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiufix: the Mods tab's Angel's Aim segment (angels_aim_tab.h); the Required section's
// catalog rows are Install Mods' own (DrawOfferList).

#include <algorithm>
#include <cmath>

#include "frame_xr/immersive_names.h"
#include "frame_xr/menu/angels_aim_tab.h"
#include "frame_xr/menu/md3_fonts.h"
#include "frame_xr/menu/tab_menu.h"

namespace FrameXr::Menu {

namespace Sym = ::FrameXr::Menu::Icon;

namespace {

constexpr float kPad = 24.0f;
constexpr float kBody = 16.0f;
constexpr float kBodyLine = 24.0f;
constexpr float kSmall = 14.0f;
constexpr float kButtonH = 40.0f;
constexpr float kItemH = 56.0f;  ///< a one-line list item with its trailing text
constexpr float kTitleH = 40.0f; ///< a section's title
constexpr float kDividerH = 16.0f;
constexpr float kNoteLine = 20.0f; ///< AzaharFrame kiu132: a line of the C-Stick mods' note
// AzaharFrame kiuaim: the page. The front view is a bar and eight rows in the segment's 452 dp
// (the sketch's 56 dp rows would need 496); a group's page is a bar, five rows and More.
constexpr float kPageBarH = 48.0f;  ///< the front view's title and scope control
constexpr float kFrontRowH = 49.0f; ///< a front view's row
constexpr float kBackBarH = 40.0f;  ///< a page's title; Back's target is 48, 4 over it each way
constexpr float kMoreRowH = 48.0f;  ///< More, under a page's rows
constexpr float kPageEnd = 4.0f;    ///< a page's rows end this far over the pane's edge


std::vector<std::string> Wrap(const Kit& kit, const std::string& s, float size, float width) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t at = 0;
    while (at < s.size()) {
        std::size_t end = s.find(' ', at);
        if (end == std::string::npos) {
            end = s.size();
        }
        const std::string word = s.substr(at, end - at);
        const std::string next = line.empty() ? word : line + " " + word;
        if (!line.empty() && kit.TextWidthSized(size, false, next) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = next;
        }
        at = end + 1;
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

std::string Percent(const std::string& s) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const unsigned char ch : s) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '.' || ch == '_' || ch == '~') {
            out += static_cast<char>(ch);
        } else {
            out += '%';
            out += kHex[ch >> 4];
            out += kHex[ch & 15];
        }
    }
    return out;
}

} // namespace

std::string AngelsAimReportUrl(const std::string& app_version, const std::string& game,
                               bool mod_installed) {
    const auto clip = [](const std::string& s) { return s.substr(0, 40); };
    const std::string body = "Azahar for Frame " + clip(app_version.empty() ? "?" : app_version) +
                             "\nGame: " + clip(game.empty() ? "?" : game) +
                             "\nC-Stick & Gyro mod: " + (mod_installed ? "installed" : "not installed") +
                             "\n\n";
    return std::string(kAngelsAimIssuesUrl) + "&body=" + Percent(body);
}

bool IsAngelsAimReportUrl(const std::string& url) {
    return url.rfind(kAngelsAimIssuesUrl, 0) == 0;
}

std::vector<int> AngelsAimModRows(const ModsListModel& m) {
    std::vector<int> out;
    for (std::size_t i = 0; i < m.available.size(); i++) {
        // AzaharFrame kiu132: and hosiniku's region builds of kiu-cstick-aim (the catalogue's
        // "kiu-cstick-aim-usa", "-eur", "-jpn"), as FrameXr::IsCStickModId.
        // (Only this game's region of those: the other regions' entries stay in Install Mods.)
        const std::string& id = m.available[i].id;
        bool cstick = id.rfind("kiu-cstick-aim-", 0) == 0 && !m.available[i].mismatch;
        for (const char* known : kAngelsAimModIds) {
            cstick = cstick || id == known;
        }
        if (cstick) {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}

// ---- AzaharFrame kiuaim ----
bool AimPageRowShown(const MenuModel& model, Id id) {
    switch (id) {
    case Id::KiuBottomReset:
        return model.kiu_bottom_screen == 1 && model.kiu_bottom_moved;
    case Id::GyroSensitivity:
        return model.gyro_aim == 1;
    default:
        return true;
    }
}

const char* AimPageTip(Id id) {
    switch (id) {
    case Id::GyroAim:
        return kAimTipAiming;
    case Id::GyroSensitivity:
        return kAimTipGyroSensitivity;
    case Id::Recoil:
        return kAimTipRecoil;
    case Id::RecoilCharged:
        return kAimTipRecoilCharged;
    case Id::RecoilRapid:
        return kAimTipRecoilRapid;
    default:
        return nullptr;
    }
}

const char* AimGroupName(AimGroup group) {
    switch (group) {
    case AimGroup::Aiming:
        return kAngelsAimGroupAiming;
    case AimGroup::View:
        return kAngelsAimGroupView;
    case AimGroup::Hud:
        return kAngelsAimGroupHud;
    case AimGroup::BottomScreen:
        return kAngelsAimGroupBottomScreen;
    case AimGroup::Weapon:
        return kAngelsAimGroupWeapon;
    case AimGroup::ModsAndPack:
        return kAngelsAimGroupModsAndPack;
    case AimGroup::About:
    case AimGroup::Count:
        break;
    }
    return kAngelsAimTitle;
}

std::string AimMoreTitle(AimGroup group) {
    return std::string(AimGroupName(group)) + " \xC2\xB7 " + kAimMore;
}

namespace {

struct AimPlace {
    AimGroup group;
    bool more;
    Id id;
};
/// Each row's place, in the order drawn.
const std::vector<AimPlace>& AimPlaces() {
    using G = AimGroup;
    static const std::vector<AimPlace> kPlaces{
        {G::Aiming, false, Id::GyroAim},
        {G::Aiming, false, Id::TriggerLayout},
        {G::Aiming, false, Id::KiuReticleSize},
        {G::Aiming, false, Id::LaserSight},
        {G::Aiming, false, Id::GyroSensitivity},
        {G::Aiming, true, Id::KeepOnScreen},
        {G::Aiming, true, Id::GripFreesAim},
        {G::Aiming, true, Id::TurnZoneFrom},
        {G::Aiming, true, Id::TurnZone},
        {G::Aiming, true, Id::TurnSpeed},
        {G::Aiming, true, Id::TurnSpeedVertical},
        {G::Aiming, true, Id::CalibrateAim},
        {G::View, false, Id::KiuOverShoulder},
        {G::View, false, Id::KiuPitOpacity}, // under Camera, the row that shows it
        {G::View, false, Id::KiuSnapTurn},
        {G::View, false, Id::KiuCutscenes},
        {G::View, false, Id::KiuStickClick},
        {G::View, true, Id::Kiu60Fps}, // AzaharFrame kiu60
        {G::Hud, false, Id::KiuHud},
        {G::Hud, false, Id::KiuHudSize},
        {G::Hud, false, Id::KiuSubtitleSize},
        {G::Hud, false, Id::KiuPowerMenu},
        {G::Hud, true, Id::KiuHudLinked},
        {G::Hud, true, Id::KiuHudAcross},
        {G::Hud, true, Id::KiuHudHeight},
        {G::Hud, true, Id::KiuBarAcross},
        {G::Hud, true, Id::KiuBarHeight},
        {G::Hud, true, Id::KiuScoreAcross},
        {G::Hud, true, Id::KiuScoreHeight},
        {G::Hud, true, Id::KiuSubtitleHeight},
        {G::Hud, true, Id::KiuPowerMenuAcross},
        {G::Hud, true, Id::KiuPowerMenuHeight},
        {G::Hud, true, Id::KiuPowerMenuOpacity},
        // (HUD Position, HUD Opacity)
        {G::BottomScreen, false, Id::KiuBottomScreen},
        {G::BottomScreen, false, Id::KiuBottomSize},
        {G::BottomScreen, false, Id::KiuBottomOpacity},
        {G::BottomScreen, false, Id::KiuWristAngle},
        {G::BottomScreen, false, Id::KiuWristAlong},
        {G::BottomScreen, false, Id::KiuBottomReset},
        // Under More: the page has five rows with Wrist already.
        {G::BottomScreen, true, Id::KiuTalkPortraits},
        {G::BottomScreen, true, Id::KiuTalkPortraitsOpacity},
        {G::BottomScreen, true, Id::KiuPortraitsLinked},
        {G::BottomScreen, true, Id::KiuPortraitsAcross},
        {G::BottomScreen, true, Id::KiuPortraitsHeight},
        {G::Weapon, false, Id::WeaponInHand},
        {G::Weapon, false, Id::MuzzleFlash},
        {G::Weapon, false, Id::Projectiles},
        {G::Weapon, false, Id::Recoil},
        {G::Weapon, false, Id::KiuHideOffHand}, // Free Hand
        {G::Weapon, true, Id::WeaponOpacity},
        {G::Weapon, true, Id::WeaponSize},
        {G::Weapon, true, Id::WeaponScale},
        {G::Weapon, true, Id::MuzzleFlashSize},
        {G::Weapon, true, Id::ProjectileOpacity},
        {G::Weapon, true, Id::ProjectileSize},
        {G::Weapon, true, Id::ProjectileSpeed},
        {G::Weapon, true, Id::RecoilCharged},
        {G::Weapon, true, Id::RecoilRapid},
        {G::Weapon, true, Id::KiuArm}, // not offered yet (kPitArmOffered)
    };
    return kPlaces;
}

bool Placed(Id id) {
    const auto& places = AimPlaces();
    return std::any_of(places.begin(), places.end(), [&](const AimPlace& p) { return p.id == id; });
}

/// The rows there are now, each list's own conditions kept: {from AimingRows, first person's}.
std::pair<std::vector<Id>, std::vector<Id>> AimRowsNow(const MenuModel& model) {
    std::pair<std::vector<Id>, std::vector<Id>> now;
    if (!model.mods.angels_aim.shown) {
        return now;
    }
    for (const Id id : AimingRows(model)) {
        if (AimPageRowShown(model, id)) {
            now.first.push_back(id);
        }
    }
    for (const Id id : AngelsAimFirstPersonRows(model)) {
        if (AimPageRowShown(model, id)) {
            now.second.push_back(id);
        }
    }
    return now;
}

bool In(const std::vector<Id>& ids, Id id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

AimPage AimPageRows(const MenuModel& model, AimGroup group) {
    AimPage page;
    const auto now = AimRowsNow(model);
    for (const AimPlace& p : AimPlaces()) {
        if (p.group == group && (In(now.first, p.id) || In(now.second, p.id))) {
            (p.more ? page.more : page.rows).push_back(p.id);
        }
    }
    const std::vector<Id>* loose = group == AimGroup::Aiming ? &now.first
                                   : group == AimGroup::View ? &now.second
                                                             : nullptr;
    if (loose) {
        for (const Id id : *loose) {
            if (!Placed(id)) {
                page.more.push_back(id);
            }
        }
    }
    return page;
}

std::vector<Id> AimPageUnplaced(const MenuModel& model) {
    std::vector<Id> out;
    const auto now = AimRowsNow(model);
    for (const auto* list : {&now.first, &now.second}) {
        for (const Id id : *list) {
            if (!Placed(id)) {
                out.push_back(id);
            }
        }
    }
    return out;
}

std::vector<Id> AimScopeRows(const MenuModel& model) {
    std::vector<Id> rows;
    if (!model.mods.angels_aim.shown) {
        return rows;
    }
    rows.push_back(Id::ImmersiveMode);
    const auto now = AimRowsNow(model);
    for (const AimPlace& p : AimPlaces()) {
        if (In(now.first, p.id) || In(now.second, p.id) || KiuOnlyControlsRow(p.id)) {
            rows.push_back(p.id);
        }
    }
    for (const Id id : AimPageUnplaced(model)) {
        rows.push_back(id);
    }
    return rows;
}

std::vector<AimGroup> AimFrontGroups(const MenuModel& model) {
    std::vector<AimGroup> groups;
    if (!model.mods.angels_aim.shown) {
        return groups;
    }
    for (const AimGroup g : {AimGroup::Aiming, AimGroup::View, AimGroup::Hud,
                             AimGroup::BottomScreen, AimGroup::Weapon}) {
        const AimPage page = AimPageRows(model, g);
        if (!page.rows.empty() || !page.more.empty()) {
            groups.push_back(g);
        }
    }
    groups.push_back(AimGroup::ModsAndPack);
    groups.push_back(AimGroup::About);
    return groups;
}

std::string AimGroupValue(const MenuModel& model, AimGroup group) {
    const auto option = [](Id id, int index) {
        const auto& opts = OptionsFor(id);
        return opts.empty() ? std::string()
                            : opts[static_cast<std::size_t>(
                                  std::clamp(index, 0, static_cast<int>(opts.size()) - 1))];
    };
    switch (group) {
    case AimGroup::Aiming:
        return option(Id::GyroAim, AimingIndexOf(model.gyro_aim));
    case AimGroup::View:
        return option(Id::KiuOverShoulder, model.kiu_view) + " \xC2\xB7 " +
               option(Id::KiuSnapTurn, model.kiu_snap_turn);
    case AimGroup::Hud:
        return option(Id::KiuHud, model.kiu_hud);
    case AimGroup::BottomScreen:
        return option(Id::KiuBottomScreen, model.kiu_bottom_screen);
    case AimGroup::Weapon:
        if (!In(AimPageRows(model, group).rows, Id::WeaponInHand)) {
            return {}; // Aiming is Off: Free Hand alone
        }
        return model.weapon_in_hand ? kAimWeaponInHand : kAimWeaponOff;
    case AimGroup::ModsAndPack: {
        const AngelsAimModel& m = model.mods.angels_aim;
        const bool hand = m.mapped && kPitArmOffered;
        const int all = hand ? 3 : 2;
        const int have = (m.mod_installed ? 1 : 0) + (m.textures_pack ? 1 : 0) +
                         (hand && m.hand_installed ? 1 : 0);
        return have == all ? std::string(kAngelsAimInstalled)
               : have == 0 ? std::string(kAngelsAimNotInstalled)
                           : std::to_string(have) + " of " + std::to_string(all) + " " +
                                 kAngelsAimInstalled;
    }
    case AimGroup::About:
    case AimGroup::Count:
        break;
    }
    return {};
}

AimShown AimPageShown(const MenuModel& model, const MenuView& view) {
    AimShown at;
    const std::vector<AimGroup> front = AimFrontGroups(model);
    if (view.aim_group < 0 || view.aim_group >= static_cast<int>(AimGroup::Count) ||
        std::find(front.begin(), front.end(), static_cast<AimGroup>(view.aim_group)) ==
            front.end()) {
        return at;
    }
    at.group = view.aim_group;
    at.more = view.aim_more &&
              !AimPageRows(model, static_cast<AimGroup>(view.aim_group)).more.empty();
    return at;
}

namespace {
bool AimPageGo(MenuView& view, int group, bool more) {
    if (view.aim_group == group && view.aim_more == more) {
        return false;
    }
    view.aim_group = group;
    view.aim_more = more;
    view.scroll[static_cast<std::size_t>(Tab::Mods)] = 0.0f;
    view.open_menu = Id::None;
    return true;
}
} // namespace

void AimPageOpen(MenuView& view, int group) {
    view.tab = Tab::Mods;
    view.page = Page::Tabs;
    view.mods_segment = ModsSegment::AngelsAim;
    view.aim_group = group;
    view.aim_more = false;
    view.scroll[static_cast<std::size_t>(Tab::Mods)] = 0.0f;
    view.open_menu = Id::None;
}

bool AimPagePress(const MenuModel& model, MenuView& view, int index) {
    const AimShown at = AimPageShown(model, view);
    if (index == kAimBackIndex) {
        return AimPageBack(model, view);
    }
    if (index == kAimMoreIndex) {
        return at.group >= 0 && !at.more &&
               !AimPageRows(model, static_cast<AimGroup>(at.group)).more.empty() &&
               AimPageGo(view, at.group, true);
    }
    const std::vector<AimGroup> front = AimFrontGroups(model);
    if (at.group < 0 && index >= 0 && index < static_cast<int>(AimGroup::Count) &&
        std::find(front.begin(), front.end(), static_cast<AimGroup>(index)) != front.end()) {
        return AimPageGo(view, index, false);
    }
    return false;
}

bool AimPageBack(const MenuModel& model, MenuView& view) {
    const AimShown at = AimPageShown(model, view);
    if (at.group < 0) {
        AimPageGo(view, -1, false); // fields that show nothing: the front view's
        return false;
    }
    return AimPageGo(view, at.more ? at.group : -1, false);
}

void AimPageKeep(const MenuModel& model, MenuView& view) {
    if (!model.mods.angels_aim.shown && view.mods_segment == ModsSegment::AngelsAim) {
        view.mods_segment = ModsSegment::Cheats;
    }
    const bool on_view = view.tab == Tab::Mods && view.mods_segment == ModsSegment::AngelsAim;
    const AimShown at = on_view ? AimPageShown(model, view) : AimShown{};
    if (view.aim_group != at.group || view.aim_more != at.more) {
        view.aim_group = at.group;
        view.aim_more = at.more;
        if (on_view) {
            view.scroll[static_cast<std::size_t>(Tab::Mods)] = 0.0f;
        }
    }
}

bool AimBottomPageOpen(const MenuModel& model, const MenuView& view) {
    return view.tab == Tab::Mods && view.mods_segment == ModsSegment::AngelsAim &&
           !(model.mods.zip.open && model.mods.zip.target == ZipTarget::Hand) &&
           AimPageShown(model, view).group == static_cast<int>(AimGroup::BottomScreen);
}

void TabMenu::DrawAngelsAim(Kit& kit, const MenuModel& model, const MenuView& view,
                            const Rect& area) {
    const AngelsAimModel& m = model.mods.angels_aim;
    // AzaharFrame kiu140: Install from Zip's page, for a hand model, over the segment.
    if (model.mods.zip.open && model.mods.zip.target == ZipTarget::Hand) {
        DrawZip(kit, model.mods.zip, view, area, area.y1 - 8.0f);
        return;
    }
    const Rect c{area.x0 + kPad, area.y0, area.x1 - kPad, area.y1 - kPad};
    const Rect shown{area.x0, area.y0, area.x1, area.y1 - 8.0f};
    const std::vector<int> rows = AngelsAimModRows(model.mods.mods);
    OfferListOptions opt;
    opt.manage = true;
    opt.in_page = true;
    const auto paras = [&] {
        std::vector<std::vector<std::string>> out;
        for (const char* p : {kAngelsAimAbout1, kAngelsAimAbout2}) {
            out.push_back(Wrap(kit, p, kBody, c.W()));
        }
        return out;
    }();
    // The About text under its title, the two buttons and their line.
    float about_h = 28.0f + kButtonH + 24.0f;
    for (const auto& p : paras) {
        about_h += kBodyLine * static_cast<float>(p.size()) + 8.0f;
    }
    // AzaharFrame kiu132: the C-Stick mods' note under Required (single-screen modes only).
    const std::vector<std::string> mod_note = Wrap(kit, kAngelsAimModNote, kSmall, c.W());
    // Required, its catalog rows, Suggested, the pack, the hand model's row (kiu140).
    float mods_h = kTitleH + kNoteLine * static_cast<float>(mod_note.size()) + kItemH;
    if (!rows.empty()) {
        mods_h += OfferListHeight(kit, model.mods.mods, c, rows, opt);
    }
    mods_h += kDividerH + kTitleH + kItemH + 16.0f;
    if (m.mapped && kPitArmOffered) {
        mods_h += kItemH;
    }
    float y = 0.0f;

    const auto title = [&](const char* text) {
        kit.TextMid(c.x0, y + 16.0f, Type::Title, Tok::kOnSurface, text);
        y += kTitleH;
    };
    const auto divider = [&] {
        kit.Line(c.x0, y + 8.0f, c.x1, y + 8.0f, 1.0f, Tok::kOutlineVariant);
        y += kDividerH;
    };
    // The Alpha chip after a title (as the Mods list's version chips).
    const auto alpha_chip = [&](float x, float cy) {
        constexpr float kChipH = 32.0f, kChipPad = 12.0f;
        const float cw = kit.TextWidth(Type::Label, kAngelsAimAlpha) + kChipPad * 2.0f;
        const Rect chip{x, cy - kChipH * 0.5f, x + cw, cy + kChipH * 0.5f};
        kit.Fill(chip, 8.0f, Tok::kSecondaryContainer);
        kit.TextMid(chip.Cx(), chip.Cy(), Type::Label, Tok::kOnSecondaryContainer, kAngelsAimAlpha, 1);
    };
    // A one-line item: its name, and Installed / Not Installed at the end.
    const auto status_item = [&](const char* name, bool installed, float right = 0.0f,
                                 const char* other = kAngelsAimNotInstalled) {
        const float cy = y + kItemH * 0.5f, x1 = c.x1 - right;
        const char* state = installed ? kAngelsAimInstalled : other;
        const float sw = kit.TextWidth(Type::Body, state);
        if (installed) {
            kit.Icon(x1 - sw - 16.0f, cy, 20.0f, Sym::kCheck, false, Tok::kPrimary);
        }
        kit.TextMid(x1 - sw, cy, Type::Body, installed ? Tok::kPrimary : Tok::kOnSurfaceVariant,
                    state);
        kit.TextMid(c.x0, cy, Type::Body, Tok::kOnSurface,
                    kit.Fit(Type::Body, name, x1 - c.x0 - sw - 48.0f));
        y += kItemH;
    };
    // The status line, the paragraphs, Send Report (to the developer, Qt's dialog) and Report an
    // Issue (GitHub), then their line.
    const auto about = [&] {
        kit.TextSized(c.x0, y + 10.0f + kit.CapHeightSized(kSmall, true) * 0.5f, kSmall, true,
                      Tok::kPrimary, kAngelsAimStatus);
        y += 28.0f;
        for (const auto& p : paras) {
            for (const std::string& l : p) {
                kit.TextSized(c.x0, y + kBodyLine * 0.5f + kit.CapHeightSized(kBody, false) * 0.5f,
                              kBody, false, Tok::kOnSurfaceVariant, l);
                y += kBodyLine;
            }
            y += 8.0f;
        }
        const std::string send = LabelFor(Id::AngelsAimSend);
        const float sw = kit.TextWidth(Type::Label, send) + 48.0f;
        const Rect sb{c.x0, y, c.x0 + sw, y + kButtonH};
        kit.Button(sb, ButtonStyle::Tonal, 0, send, StateOf(view, {Id::AngelsAimSend}));
        AddHit({sb.x0, sb.Cy() - 24.0f, sb.x1, sb.Cy() + 24.0f}, {Id::AngelsAimSend}, shown);
        const std::string label = LabelFor(Id::AngelsAimReport);
        const float w = kit.TextWidth(Type::Label, label) + 28.0f + 24.0f;
        const Rect b{sb.x1 + 8.0f, y, sb.x1 + 8.0f + w, y + kButtonH};
        const bool can = !m.report_url.empty();
        kit.Button(b, ButtonStyle::Text, Sym::kOpenInNew, label,
                   StateOf(view, {Id::AngelsAimReport}, can));
        if (can) {
            AddHit({b.x0, b.Cy() - 24.0f, b.x1, b.Cy() + 24.0f}, {Id::AngelsAimReport}, shown);
        }
        y += kButtonH;
        kit.TextSized(c.x0, y + 10.0f + kit.CapHeightSized(kSmall, false) * 0.5f, kSmall, false,
                      Tok::kOnSurfaceVariant, kAngelsAimReportLine);
        y += 24.0f;
    };
    // Required (in the single-screen modes): the mod, the catalog's rows. Suggested: the
    // catalog's weapons and effects texture pack, one row; a hand model from the player's zip.
    const auto mods_and_pack = [&] {
        title(kAngelsAimRequired);
        for (const std::string& l : mod_note) {
            kit.TextSized(c.x0, y + kNoteLine * 0.5f + kit.CapHeightSized(kSmall, false) * 0.5f,
                          kSmall, false, Tok::kOnSurfaceVariant, l);
            y += kNoteLine;
        }
        status_item(kAngelsAimModName, m.mod_installed);
        if (!rows.empty()) {
            opt.page_top = y;
            DrawOfferList(kit, model, view, area, c, shown, rows, opt);
            y += OfferListHeight(kit, model.mods.mods, c, rows, opt);
        }
        divider();
        title(kAngelsAimSuggested);
        status_item(kAngelsAimTexturesPack, m.textures_pack);
        if (m.mapped && kPitArmOffered) { // AzaharFrame kiu140
            const bool found = !m.hand_installed && !m.hand_zip.empty();
            const std::string zip =
                found ? std::string(kAngelsAimHandInstall) : LabelFor(Id::InstallFromZip);
            const Target zt{Id::InstallFromZip,
                            found ? kAngelsAimHandFoundIndex : static_cast<int>(ZipTarget::Hand)};
            const float w = kit.TextWidth(Type::Label, zip) + 8.0f + 18.0f + 48.0f;
            const Rect b{c.x1 - w, y + (kItemH - kButtonH) * 0.5f, c.x1,
                         y + (kItemH + kButtonH) * 0.5f};
            kit.Button(b, ButtonStyle::Tonal, Sym::kFolderOpen, zip, StateOf(view, zt));
            AddHit({b.x0, y, b.x1, y + kItemH}, zt, shown);
            status_item(kAngelsAimHandModel, m.hand_installed, w + 16.0f,
                        found ? kAngelsAimHandFound : kAngelsAimNotInstalled);
        }
        y += 16.0f;
    };
    // The page's own tips over ExplainFor's, for the anchors from `first` on.
    const auto own_tips = [&](std::size_t first) {
        for (std::size_t i = first; i < anchors.size(); i++) {
            if (const char* tip = AimPageTip(anchors[i].id); tip && anchors[i].index < 0) {
                anchors[i].text = tip;
            }
        }
    };
    Rect open_anchor{};
    int open_selected = -1;

    // ---- AzaharFrame kiuaim: the front view, or the open group's page ----
    const AimShown at = AimPageShown(model, view);
    layout.max_scroll = 0.0f;
    y = c.y0;
    // A row to press: its name, a value, a chevron.
    const auto press_row = [&](float h, const std::string& name, const std::string& value,
                               int index, const Rect& clip) {
        const Rect row{c.x0 - 12.0f, y, c.x1 + 12.0f, y + h};
        const Target t{Id::AngelsAimGroup, index};
        kit.StateLayer({row.x0, row.y0 + 2.0f, row.x1, row.y1 - 2.0f}, 16.0f, Tok::kOnSurface,
                       StateOf(view, t));
        AddHit(row, t, clip);
        kit.TextMid(c.x0, row.Cy(), Type::Body, Tok::kOnSurface, name);
        kit.Icon(c.x1 - 8.0f, row.Cy(), 24.0f, Sym::kChevronRight, false,
                 Tok::kOnSurfaceVariant);
        if (!value.empty()) {
            const float room = c.x1 - 32.0f - (c.x0 + kit.TextWidth(Type::Body, name) + 24.0f);
            kit.TextMid(c.x1 - 32.0f, row.Cy(), Type::Body, Tok::kOnSurfaceVariant,
                        kit.Fit(Type::Body, value, room), 2);
        }
        y += h;
    };
    if (at.group < 0) {
        // The title with its Alpha chip, and the scope control at its right: the same
        // toggle as on the Settings, Controls and Position tabs (one scope for the menu).
        kit.TextMid(c.x0, y + kPageBarH * 0.5f, Type::Title, Tok::kOnSurface, kAimPageTitle);
        alpha_chip(c.x0 + kit.TextWidth(Type::Title, kAimPageTitle) + 16.0f,
                   y + kPageBarH * 0.5f);
        DrawScope(kit, model, view, {c.x0, y, c.x1, y + kPageBarH}, shown, &shown);
        y += kPageBarH;
        { // Immersive Mode, a menu button as on the Settings tab, in a front row's height
            const Id id = Id::ImmersiveMode;
            const Rect row{c.x0 - 12.0f, y, c.x1 + 12.0f, y + kFrontRowH};
            const bool enabled = model.locked[static_cast<std::size_t>(id)] == Lock::None;
            AddHit(row, {id, kLabelIndex}, shown);
            anchors.push_back({id, c.x0, row.y0 + 6.0f,
                               model.locked[static_cast<std::size_t>(id)]});
            kit.TextMid(c.x0, row.Cy(), Type::Body, Tok::kOnSurface, LabelFor(id), 0,
                        enabled ? 1.0f : kDisabledContent);
            const auto& opts = OptionsFor(id);
            const int sel = std::clamp(
                Immersive::Names::ModeIndexOf(static_cast<std::uint32_t>(model.immersive_mode)),
                0, static_cast<int>(opts.size()) - 1);
            const Rect mb{c.x1 - 420.0f, row.Cy() - 20.0f, c.x1, row.Cy() + 20.0f};
            kit.MenuButton(mb, opts[static_cast<std::size_t>(sel)], view.open_menu == id,
                           StateOf(view, {id}, enabled), sel == RecommendedModeIndex(model));
            if (enabled) {
                AddHit({mb.x0, row.Cy() - 24.0f, mb.x1, row.Cy() + 24.0f}, {id}, shown);
            }
            if (view.open_menu == id) {
                open_anchor = mb;
                open_selected = sel;
            }
            y += kFrontRowH;
        }
        // (Single Screen's row comes here with its lane, while the mode is not Angel's Aim.)
        for (const AimGroup g : AimFrontGroups(model)) {
            kit.Line(c.x0, y, c.x1, y, 1.0f, Tok::kOutlineVariant);
            press_row(kFrontRowH, AimGroupName(g), AimGroupValue(model, g),
                      static_cast<int>(g), shown);
        }
    } else {
        const AimGroup g = static_cast<AimGroup>(at.group);
        // Back and the page's title.
        const float bar_cy = y + kBackBarH * 0.5f;
        const Rect back{c.x0 - 12.0f, bar_cy - 24.0f, c.x0 + 36.0f, bar_cy + 24.0f};
        const Target bt{Id::AngelsAimGroup, kAimBackIndex};
        kit.StateLayer(back, 24.0f, Tok::kOnSurfaceVariant, StateOf(view, bt));
        kit.Icon(back.Cx(), back.Cy(), 24.0f, Sym::kArrowBack, false, Tok::kOnSurface);
        AddHit(back, bt, back);
        const std::string name = at.more ? AimMoreTitle(g) : std::string(AimGroupName(g));
        kit.TextMid(back.x1 + 12.0f, back.Cy(), Type::Title, Tok::kOnSurface, name);
        if (g == AimGroup::About) {
            alpha_chip(back.x1 + 12.0f + kit.TextWidth(Type::Title, name) + 16.0f, back.Cy());
        }
        const bool is_rows = g != AimGroup::ModsAndPack && g != AimGroup::About;
        y += is_rows ? kBackBarH : kPageBarH;
        const float room = area.y1 - kPageEnd - y;
        // A More page shows whole rows only.
        const float list_h = !is_rows ? shown.y1 - y
                             : at.more ? std::floor(room / kRowH) * kRowH
                                       : room;
        const Rect list{area.x0, y, area.x1, y + list_h};
        const AimPage rows_now = AimPageRows(model, g);
        const std::vector<Id>& ids = at.more ? rows_now.more : rows_now.rows;
        const bool more_row = !at.more && !rows_now.more.empty();
        const float content = g == AimGroup::ModsAndPack ? mods_h
                              : g == AimGroup::About     ? about_h
                                                         : kRowH * static_cast<float>(ids.size()) +
                                                               (more_row ? kMoreRowH : 0.0f);
        // Rows scroll in whole rows (as the Settings tab's), the rest as the page did.
        const float over = content - list.H();
        const float max_scroll =
            over <= 0.0f ? 0.0f : is_rows ? std::ceil(over / kRowH) * kRowH : over;
        const float scroll =
            std::clamp(view.scroll[static_cast<std::size_t>(view.tab)], 0.0f, max_scroll);
        layout.max_scroll = max_scroll;
        layout.scroll_step = is_rows ? kRowH : 48.0f;
        kit.Clip(list);
        y -= scroll;
        if (g == AimGroup::ModsAndPack) {
            mods_and_pack();
        } else if (g == AimGroup::About) {
            about();
        } else {
            const std::size_t first_anchor = anchors.size();
            y = DrawAimRows(kit, model, view, ids, c, list, y, open_anchor, open_selected);
            own_tips(first_anchor);
            if (more_row) {
                kit.Line(c.x0, y, c.x1, y, 1.0f, Tok::kOutlineVariant);
                press_row(kMoreRowH, kAimMore, {}, kAimMoreIndex, list);
            }
        }
        kit.ClearClip();
        DrawScrollbar(kit, view, list, area.x1, content, max_scroll, scroll);
    }
    DrawOpenMenu(kit, model, view, area, open_anchor, open_selected);
    DrawScopeMenu(kit, model, view, area);
    DrawModsDialog(kit, ModsDialogFor(model, view), view);
}

} // namespace FrameXr::Menu
