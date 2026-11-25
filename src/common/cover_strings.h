// Copyright 2026 AzaharVR contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// What the player reads about covers (core/frame_covers.h), in Azahar's window and in the VR menu.

#pragma once

#include <cstddef>
#include <string>

namespace Common::CoverText {

// Approved by the owner
inline constexpr const char* kSetting = "Download Game Covers"; ///< automatic, off by default
inline constexpr const char* kDownload = "Download Thumbnails";
inline constexpr const char* kAnother = "Choose Another Cover";
inline constexpr const char* kNone = "No Cover";
inline constexpr const char* kOwn = "Use My Own Image";
inline constexpr const char* kReset = "Reset Cover";

// Proposals
inline constexpr const char* kAskTitle = "Download Thumbnails?";
inline constexpr const char* kAskYes = "Download";
inline constexpr const char* kAskNo = "Not Now";
/// What the one source sees; `found`: after a directory was added, else of the games shown.
inline std::string AskBody(std::size_t games, bool found) {
    const std::string what = games == 1 ? (found ? "The cover for the game found" : "The cover for 1 game")
                                        : "Covers for the " + std::to_string(games) + " games" +
                                              (found ? " found" : "");
    return what + " can be downloaded from libretro-thumbnails on GitHub. It sees your IP address "
                  "and the name" +
           (games == 1 ? " of the game." : "s of the games.");
}
inline constexpr const char* kSettingTip =
    "Covers of the games in your list for the VR menu's Games tab, downloaded from "
    "libretro-thumbnails on GitHub. It sees your IP address and the names of the games.";
inline constexpr const char* kDownloadTip = "For the games the Games tab shows";
inline constexpr const char* kBoxArt = "Box Art"; ///< Choose Another Cover's items
inline constexpr const char* kTitleScreen = "Title Screen";
inline constexpr const char* kScreenshot = "Screenshot";
inline constexpr const char* kNothingToAsk = "No Covers to Download"; ///< toasts
inline constexpr const char* kNoneFound = "No Covers Found";
inline constexpr const char* kUnreachable = "Could Not Reach GitHub";
inline constexpr const char* kOwnTooLarge = "Image Too Large";
inline constexpr const char* kOwnNotImage = "Not a PNG or JPEG Image";
inline constexpr const char* kOwnPick = "Select Image";
inline std::string Downloaded(int covers) {
    return covers == 1 ? "1 Cover Downloaded" : std::to_string(covers) + " Covers Downloaded";
}

} // namespace Common::CoverText
