// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharVR: how a link is opened from the app. On the Frame the desktop's browser does not show in
// the headset, so a page opens in Steam's own browser when Steam runs; the desktop's browser only
// when it does not. With little memory left nothing is opened (the browser's start can get the
// game killed, frame_xr/mem_guard.h). Used by the Mods tab's links and the update prompt.

#pragma once

#include <functional>
#include <QString>

namespace OpenLink {

enum class Opener { Steam, Browser, None };

/// Which opener to try: nothing with little memory; Steam when it runs; else the browser.
Opener ChooseOpener(bool low_memory, bool steam_running);

enum class Result { OpenedInSteam, OpenedInBrowser, LowMemory };

/// What Open does, replaceable so a test never starts Steam or a browser.
struct Hooks {
    std::function<bool()> low_memory;
    std::function<bool()> steam_running;
    std::function<bool(const QString& steam_url)> start_steam; ///< true when started
    std::function<void(const QString& url)> open_browser;
};

/// Opens `url`: with Steam running in Steam (the browser when Steam does not start), else in the
/// browser; LowMemory (nothing opened) when little memory is left. The caller has checked that
/// `url` may be opened at all.
Result Open(const QString& url, const Hooks& hooks);
/// The same with the real checks and openers.
Result Open(const QString& url);

} // namespace OpenLink
