// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharVR: which opener a link gets (citra_qt/frame_open_link.h), with Steam and the browser
// replaced by recorders: nothing real is started.

#include <catch2/catch_test_macros.hpp>
#include "citra_qt/frame_open_link.h"

using OpenLink::Opener;
using OpenLink::Result;

TEST_CASE("Open link: the choice of opener", "[openlink]") {
    CHECK(OpenLink::ChooseOpener(false, true) == Opener::Steam);
    CHECK(OpenLink::ChooseOpener(false, false) == Opener::Browser);
    CHECK(OpenLink::ChooseOpener(true, true) == Opener::None);
    CHECK(OpenLink::ChooseOpener(true, false) == Opener::None);
}

namespace {
struct Recorder {
    bool low = false;
    bool steam = false;
    bool steam_starts = true;
    int steam_calls = 0;
    int browser_calls = 0;
    QString steam_arg;
    QString browser_arg;

    OpenLink::Hooks Hooks() {
        OpenLink::Hooks h;
        h.low_memory = [this] { return low; };
        h.steam_running = [this] { return steam; };
        h.start_steam = [this](const QString& u) {
            ++steam_calls;
            steam_arg = u;
            return steam_starts;
        };
        h.open_browser = [this](const QString& u) {
            ++browser_calls;
            browser_arg = u;
        };
        return h;
    }
};
const QString kUrl =
    QStringLiteral("https://github.com/simplysanatori/AzaharVR/releases/tag/v1.4.0");
} // namespace

TEST_CASE("Open link: Steam running opens in Steam", "[openlink]") {
    Recorder r;
    r.steam = true;
    CHECK(OpenLink::Open(kUrl, r.Hooks()) == Result::OpenedInSteam);
    CHECK(r.steam_calls == 1);
    CHECK(r.steam_arg == QStringLiteral("steam://openurl/") + kUrl);
    CHECK(r.browser_calls == 0);
}

TEST_CASE("Open link: Steam not running opens in the browser", "[openlink]") {
    Recorder r;
    CHECK(OpenLink::Open(kUrl, r.Hooks()) == Result::OpenedInBrowser);
    CHECK(r.steam_calls == 0);
    CHECK(r.browser_calls == 1);
    CHECK(r.browser_arg == kUrl);
}

TEST_CASE("Open link: Steam that does not start falls back to the browser", "[openlink]") {
    Recorder r;
    r.steam = true;
    r.steam_starts = false;
    CHECK(OpenLink::Open(kUrl, r.Hooks()) == Result::OpenedInBrowser);
    CHECK(r.steam_calls == 1);
    CHECK(r.browser_calls == 1);
}

TEST_CASE("Open link: little memory opens nothing", "[openlink]") {
    for (const bool steam : {true, false}) {
        Recorder r;
        r.low = true;
        r.steam = steam;
        CHECK(OpenLink::Open(kUrl, r.Hooks()) == Result::LowMemory);
        CHECK(r.steam_calls == 0);
        CHECK(r.browser_calls == 0);
    }
}
