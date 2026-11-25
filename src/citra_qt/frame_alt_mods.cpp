// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "citra_qt/frame_alt_mods.h"

#include <cstdint>
#include <string>
#include <thread>

#include <QApplication>

#include "citra_qt/frame_open_link.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/cheats/online.h"
#include "core/mods/library.h"
#include "frame_cores/alt_core_host.h"
#include "frame_cores/widescreen.h"
#include "frame_xr/frame_xr.h"
#include "frame_xr/menu/mods_segments.h"

namespace FrameAltMods {

namespace {

namespace Menu = FrameXr::Menu;
namespace Wide = FrameCores::Widescreen;

constexpr const char* kId = "widescreen";
constexpr const char* kName = "Widescreen (16:10)";
constexpr const char* kAuthor = "TWiLight Menu++ Contributors";
/// As its README and its list of games credit them.
constexpr const char* kNotes =
    "By RadDude McCoolguy, fintogive, KazoWAR, Vague Rant, gamemasterplc, Rocket Robz, "
    "ChampionLeake, DeadSkullzJr, spellboundtriangle, LedyBacer, CheatFreak47, deisuke1234 and "
    "icup321.";

std::uint64_t g_key = 0; ///< the game of what follows
bool g_listed = false;   ///< Install Mods was opened
bool g_installing = false;
std::string g_failed; ///< why the last install failed

struct Game {
    std::uint64_t key = 0;
    std::string dir;
    const Wide::Entry* entry = nullptr; ///< null: the game has no mods
    bool running_wide = false;          ///< the mod is on in this run
};

Game GameOf(FrameCores::AltCoreHost& host) {
    const FrameCores::RomInfo& info = host.GetRomInfo();
    const auto caps = host.GetCapabilities();
    Game game;
    game.key = info.title_id;
    if (caps.has_mods) {
        game.dir = FrameCores::GameFolder(FileUtil::GetUserPath(FileUtil::UserPath::UserDir), info);
        game.entry = Wide::Find(Wide::BuiltIn(), info.game_code, info.header_crc16);
        game.running_wide = caps.screens.top_aspect == Wide::kAspect;
    }
    return game;
}

} // namespace

void Publish(FrameCores::AltCoreHost& host) {
    const Game game = GameOf(host);
    if (game.key != g_key) {
        g_key = game.key;
        g_listed = g_installing = false;
        g_failed.clear();
    }
    Menu::ModsListModel m;
    if (game.entry) {
        const Wide::State state = Wide::StateOf(game.dir, *game.entry);
        if (state != Wide::State::None) {
            Menu::ModRow row;
            row.id = kId;
            row.name = kName;
            row.author = kAuthor;
            row.url = Wide::kSourceUrl;
            row.managed = true;
            row.active = state == Wide::State::On;
            m.loaded.push_back(std::move(row));
        }
        if (g_listed) {
            Menu::ModOffer offer;
            offer.id = kId;
            offer.name = kName;
            offer.author = kAuthor;
            offer.url = Wide::kSourceUrl;
            offer.notes = kNotes;
            if (g_installing) {
                offer.state = Menu::OfferState::Installing;
                offer.note = "Downloading";
            } else if (state == Wide::State::On) {
                offer.state = Menu::OfferState::Installed;
            } else if (!g_failed.empty()) {
                offer.note = "Install Failed: " + g_failed;
                offer.error = true;
            }
            m.available.push_back(std::move(offer));
        }
        m.needs_restart = (state == Wide::State::On) != game.running_wide;
    }
    m.catalog = g_listed ? Menu::CatalogState::Ready : Menu::CatalogState::Idle;
    m.busy = g_installing;
    FrameXr::SetInGameMenuModsList(m);
}

void Command(const FrameXr::MenuRequest& r, FrameCores::AltCoreHost& host,
             std::function<FrameCores::AltCoreHost*()> running) {
    using C = FrameXr::MenuCommand;
    const Game game = GameOf(host);
    switch (r.command) {
    case C::LoadModCatalog:
        g_listed = true;
        Publish(host);
        return;
    case C::OpenUrl:
        if (r.text == Wide::kSourceUrl &&
            OpenLink::Open(QString::fromStdString(r.text)) == OpenLink::Result::LowMemory) {
            FrameXr::ShowToastText("Link Not Opened: Low Memory", FrameXr::ToastTime::Long);
        }
        return;
    case C::SetModActive:
    case C::RemoveMod:
        if (game.entry && r.text == kId && !g_installing) {
            const bool ok = r.command == C::RemoveMod
                                ? Wide::Remove(game.dir, *game.entry)
                                : Wide::SetOn(game.dir, *game.entry, r.value != 0);
            LOG_INFO(Frontend, "xr: mods: widescreen {}{}",
                     r.command == C::RemoveMod ? "removed"
                     : r.value != 0            ? "on"
                                               : "off",
                     ok ? "" : " failed");
            g_failed.clear();
            Publish(host);
        }
        return;
    case C::InstallMod:
        break;
    default:
        return;
    }
    if (!game.entry || r.text != kId || g_installing) {
        return;
    }
    if (Wide::StateOf(game.dir, *game.entry) == Wide::State::Off) { // here already
        Wide::SetOn(game.dir, *game.entry, true);
        Publish(host);
        return;
    }
    g_installing = true;
    g_failed.clear();
    LOG_INFO(Frontend, "xr: mods: install {}", game.entry->file);
    Publish(host);
    std::thread([game, running = std::move(running)] {
        std::string error =
            Wide::Install(Cheats::Online::HttpsGet, Wide::BuiltIn(), *game.entry, game.dir);
        QMetaObject::invokeMethod(
            qApp,
            [game, running, error = std::move(error)] {
                LOG_INFO(Frontend, "xr: mods: install {}: {}", game.entry->file,
                         error.empty() ? "done" : error);
                if (g_key != game.key) {
                    return; // another game runs now
                }
                g_installing = false;
                g_failed = error.empty() ? std::string{} : Mods::ShortFailure(error);
                if (FrameCores::AltCoreHost* now = running()) {
                    Publish(*now);
                }
            },
            Qt::QueuedConnection);
    }).detach();
}

} // namespace FrameAltMods
