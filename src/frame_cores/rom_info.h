// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common/alt_core_types.h"

namespace FrameCores {

struct RomInfo {
    Common::AltCore::CoreKind kind = Common::AltCore::CoreKind::N3DS;
    uint64_t title_id = 0;
    std::string title;
    std::string game_code;
    uint32_t crc32 = 0;
    bool dsi_enhanced = false;
    bool dsi_ware = false;
    bool is_gbc = false; ///< Game Boy kind only: the header marks a Game Boy Color game
    std::vector<uint8_t> icon_rgba; ///< icon_w * icon_h * 4 RGBA8888 pixels
    uint32_t icon_w = 0;
    uint32_t icon_h = 0;
};

/// What the game list calls a game's system (a DSi game is a DS game there).
enum class GameSystem { N3DS, DS, GBA, GB, GBC };

/// The list's system of a probed ROM.
GameSystem SystemOf(const RomInfo& info);

/// The list's filter choices; Game Boy covers Game Boy Color too.
enum class SystemFilter { All = 0, N3DS, DS, GBA, GB };

/// Whether a game of `system` shows under `filter`.
bool FilterMatches(SystemFilter filter, GameSystem system);

/// Fast detection of non-3DS ROM type from extension or ZIP directory entries.
/// Supports .nds, .dsi, .gba, .gb, .gbc (case-insensitive) and .zip archives
/// containing any of those ROM types. Returns CoreKind::N3DS if unrecognized.
Common::AltCore::CoreKind DetectCoreKind(const std::string& path);

/// Fast ROM header probe returning title, game code, reserved per-game key,
/// DSi flags, and 32x32 RGBA8 icon (DS banner icon or deterministic cartridge tile).
/// Returns std::nullopt if the file is not a supported non-3DS ROM or cannot be read.
std::optional<RomInfo> ProbeRom(const std::string& path);

/// The folder of a system's files under the user data folder: "ds" (DSi too), "gba" or "gb".
const char* CoreFolder(Common::AltCore::CoreKind kind);

/// One game's own folder, by its key: its battery save, save states and cheat choices. Two games
/// with the same file name never share one.
std::string GameFolder(const std::string& data_dir, const RomInfo& info);

/// Extracts the first supported ROM (.nds, .dsi, .gba, .gb, .gbc) from a .zip archive.
/// If `max_bytes > 0`, reads at most `max_bytes` from the decompressed ROM stream.
bool ExtractRomFromZip(const std::string& zip_path, std::vector<uint8_t>& out_data,
                       std::string* inner_name = nullptr, size_t max_bytes = 0);

/// Standard IEEE 802.3 CRC32 helper.
uint32_t ComputeCrc32(const uint8_t* data, size_t size, uint32_t seed = 0);

} // namespace FrameCores
