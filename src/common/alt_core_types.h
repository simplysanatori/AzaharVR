// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstdint>
#include <functional>
#include "common/console_group.h"

namespace Common::AltCore {

enum class CoreKind : uint8_t {
    N3DS = 0,
    GBA = 1,
    GB = 2,
    NDS = 3,
    DSi = 4,
};

struct ScreenMetrics {
    float top_aspect = 400.0f / 240.0f;    ///< Native primary screen aspect (w / h)
    float bottom_aspect = 320.0f / 240.0f; ///< Native secondary screen aspect (w / h)
    bool has_bottom = true;                ///< Secondary screen exists / active
    bool equal_screens = false;            ///< DS: secondary same normal width as primary
    bool top_stereo = true;                ///< False for mono cores (GBA/GB/DS)
    uint32_t touch_width = 320;            ///< Touch coordinate width in core pixels
    uint32_t touch_height = 240;           ///< Touch coordinate height in core pixels
    int rotation_deg = 0;                  ///< 0, 90, 270 (DS Book Mode)
    bool operator==(const ScreenMetrics&) const = default;
};

/// What draws a GBA or Game Boy game's Secondary Screen and has options of its own.
enum class DriverName : uint8_t { None, PokeDaisy, GbaDs };

/// Those options, as the VR menu has them: on or off (PokeDaisy's, in its settings.txt), a value
/// (GBA DS's, in the settings) or a press (TurnPage).
enum class DriverOption : uint32_t {
    RotatePages, ///< timed pages turn by themselves
    TurnPage,    ///< BtnTurnPage
    MapRefresh,  ///< an index of kMapRefreshFrames
    // PokeDaisy's that edit the game (Allow Game Editing)
    AutoRun,
    RunIndoors,
    WildEncounters,
    ReusableTms,
    InstantText,
    TradeEvolutions,
    // and that do not
    Hud,
    DarkMode,
    BattleInfo,
    BattleHints,
    OneTapMoves,
    BattleGen5, ///< else Gen 4
    QuickGrid,  ///< else a ring
    Count,
};

constexpr uint32_t DriverOptionBit(DriverOption option) {
    return 1u << static_cast<uint32_t>(option);
}
constexpr bool OptionEditsGame(DriverOption option) {
    return option >= DriverOption::AutoRun && option <= DriverOption::TradeEvolutions;
}

/// Settings::values.gba_map_refresh: frames from one capture of a map to the next.
inline constexpr uint32_t kMapRefreshFrames[] = {30, 60, 90, 180};

struct Capabilities {
    CoreKind kind = CoreKind::N3DS;
    bool has_stereo_3d = true;
    bool has_immersive_modes = true;
    bool has_touch = true;
    bool has_gyro = true;
    bool has_mods = true;
    bool has_texture_packs = true;
    bool has_3ds_cheats = true;
    bool has_alt_cheats = false;
    bool has_flat_mode = true;
    bool has_bfi = true;
    bool has_internal_resolution = true;
    bool has_texture_filter = true;
    bool has_dynamic_resolution = true;
    bool has_render_margin = true;
    bool has_solar_sensor = false;
    bool has_mic_blow = false;
    bool has_lid_close = false;
    bool has_dsi_mode = false;
    bool has_screen_rotation = false;
    bool has_save_import = false;
    bool has_gba_slot = false;
    /// GBA and Game Boy games: the Second Screen choices the game has, a bit for each
    /// (1 << FrameCores::SecondScreen); none for every other game.
    uint32_t second_screen_choices = 0;
    bool can_edit_game = false; ///< Allow Game Editing means something for this game
    bool has_quick_menu = false; ///< BtnQuickMenu opens PokeDaisy's quick menu
    /// Layer Depth: the Primary Screen can have a right eye's picture of its own.
    bool layer_depth = false;
    bool depth_per_screen = false; ///< of either screen, or both
    bool has_pages = false;      ///< a GBA game whose Secondary Screen BtnTurnPage may turn
    DriverName driver = DriverName::None; ///< the one running, whatever the choice is
    uint32_t driver_options = 0;    ///< DriverOptionBit of those it has for this game now
    uint32_t driver_options_on = 0; ///< of those, the ones that are on
    ScreenMetrics screens{};
    bool operator==(const Capabilities&) const = default;

    static constexpr Capabilities ForN3DS() {
        return Capabilities{};
    }

    static constexpr Capabilities ForGBA() {
        Capabilities c{};
        c.kind = CoreKind::GBA;
        c.has_stereo_3d = false;
        c.has_immersive_modes = false;
        c.has_touch = false;
        c.has_gyro = false;
        c.has_mods = false;
        c.has_texture_packs = false;
        c.has_3ds_cheats = false;
        c.has_alt_cheats = true;
        c.has_bfi = false;
        c.has_dynamic_resolution = false;
        c.has_render_margin = false;
        c.has_solar_sensor = true;
        c.has_save_import = true;
        c.screens.top_aspect = 240.0f / 160.0f;
        c.screens.bottom_aspect = 240.0f / 160.0f;
        c.screens.has_bottom = false;
        c.screens.equal_screens = false;
        c.screens.top_stereo = false;
        c.screens.touch_width = 240;
        c.screens.touch_height = 160;
        return c;
    }

    static constexpr Capabilities ForGB() {
        Capabilities c = ForGBA();
        c.kind = CoreKind::GB;
        c.has_solar_sensor = false;
        c.has_alt_cheats = false;
        c.screens.top_aspect = 160.0f / 144.0f;
        c.screens.bottom_aspect = 160.0f / 144.0f;
        c.screens.has_bottom = false;
        c.screens.touch_width = 160;
        c.screens.touch_height = 144;
        return c;
    }

    static constexpr Capabilities ForNDS(bool dsi = false) {
        Capabilities c{};
        c.kind = dsi ? CoreKind::DSi : CoreKind::NDS;
        c.has_stereo_3d = false;
        c.has_immersive_modes = false;
        c.has_touch = true;
        c.has_gyro = false;
        c.has_mods = false;
        c.has_texture_packs = false;
        c.has_3ds_cheats = false;
        c.has_alt_cheats = true;
        c.has_bfi = false;
        c.has_internal_resolution = false;
        c.has_texture_filter = false;
        c.has_dynamic_resolution = false;
        c.has_render_margin = false;
        c.has_mic_blow = true;
        c.has_lid_close = true;
        c.has_dsi_mode = true;
        c.has_screen_rotation = true;
        c.has_save_import = true;
        c.has_gba_slot = !dsi;
        c.screens.top_aspect = 256.0f / 192.0f;
        c.screens.bottom_aspect = 256.0f / 192.0f;
        c.screens.has_bottom = true;
        c.screens.equal_screens = true;
        c.screens.top_stereo = false;
        c.screens.touch_width = 256;
        c.screens.touch_height = 192;
        return c;
    }
};

/// A screen in the console's own pixels, upright (the 3DS's Primary Screen).
struct ScreenPixels {
    int width = 0;
    int height = 0;
};

constexpr ScreenPixels OwnPixels(CoreKind kind) {
    switch (kind) {
    case CoreKind::GBA:
        return {240, 160};
    case CoreKind::GB:
        return {160, 144};
    case CoreKind::NDS:
    case CoreKind::DSi:
        return {256, 192};
    default:
        return {400, 240};
    }
}

constexpr bool IsDs(CoreKind kind) {
    return kind == CoreKind::NDS || kind == CoreKind::DSi;
}

/// The group of settings of this console's games (Settings::Graphics).
constexpr Settings::ConsoleGroup GroupOf(CoreKind kind) {
    switch (kind) {
    case CoreKind::GBA:
    case CoreKind::GB:
        return Settings::ConsoleGroup::GBA;
    case CoreKind::NDS:
    case CoreKind::DSi:
        return Settings::ConsoleGroup::DS;
    default:
        return Settings::ConsoleGroup::N3DS;
    }
}

/// Whether the Secondary Screen takes touches. Touch Screen Input is the DS's alone.
constexpr bool TouchEnabled(const Capabilities& caps, bool ds_touch_enabled) {
    return caps.has_touch && (!IsDs(caps.kind) || ds_touch_enabled);
}

/// Capabilities::second_screen_choices: the bit of Settings::values.gba_second_screen's value.
constexpr uint32_t SecondScreenBit(uint32_t choice) {
    return 1u << choice;
}

/// Shown once, the first time Allow Game Editing is turned on (UISettings game_editing_warning).
inline constexpr const char* kGameEditingTitle = "PokeDaisy Edits Your Game";
inline constexpr const char* kGameEditingBody =
    "Fly, the PC, items and wild encounters edit your game as you play. This can corrupt your "
    "save. Your save is backed up first.";
inline constexpr const char* kGameEditingConfirm = "Turn On";
inline constexpr const char* kGameEditingCancel = "Cancel";

/// Settings::values.ds_console_mode
enum DsConsole : uint32_t {
    DsConsoleAutomatic = 0, ///< the console the game was made for
    DsConsoleDS = 1,
    DsConsoleDSi = 2,
};

/// Settings::values.ds_screen_rotation (0, 1, 2) in degrees.
constexpr int RotationDegrees(uint32_t index) {
    return index == 1 ? 90 : (index == 2 ? 270 : 0);
}

/// Neutral controller + touch snapshot delivered from FrameXr to an active AltCoreHost.
enum ButtonBit : uint32_t {
    BtnA      = 1u << 0,
    BtnB      = 1u << 1,
    BtnSelect = 1u << 2,
    BtnStart  = 1u << 3,
    BtnRight  = 1u << 4,
    BtnLeft   = 1u << 5,
    BtnUp     = 1u << 6,
    BtnDown   = 1u << 7,
    BtnR      = 1u << 8,
    BtnL      = 1u << 9,
    BtnX      = 1u << 10,
    BtnY      = 1u << 11,
    BtnMic    = 1u << 12,
    BtnQuickMenu = 1u << 13, ///< PokeDaisy's quick menu
    BtnTurnPage = 1u << 14,  ///< Turn Secondary Screen Page
};

// What BtnX and BtnY are in a GBA or Game Boy game (frame_cores/auto_fire.h).
inline constexpr const char* kAutoFireALabel = "Auto Fire A";
inline constexpr const char* kAutoFireBLabel = "Auto Fire B";

// BtnQuickMenu's strings: in the Qt settings, the VR menu and the driver.
inline constexpr const char* kQuickMenuLabel = "PokeDaisy Quick Menu";
inline constexpr const char* kQuickMenuTooltip =
    "Opens PokeDaisy's quick menu over the game. Needs Allow Game Editing.";
inline constexpr const char* kQuickMenuNeedsEditing = "Turn on Allow Game Editing first.";

struct InputState {
    uint32_t buttons = 0;   ///< Bitmask of ButtonBit
    bool touching = false;
    uint16_t touch_x = 0;   ///< Core pixels [0, touch_width - 1]
    uint16_t touch_y = 0;   ///< Core pixels [0, touch_height - 1]
};

using InputSink = std::function<void(const InputState&)>;

/// Reserved 64-bit per-game key encoding so non-3DS titles never collide with 3DS title IDs
/// (3DS title IDs always start with 0x0004....).
constexpr uint64_t kTagNDS = 0xD5ULL << 56;
constexpr uint64_t kTagDSi = 0xD1ULL << 56;
constexpr uint64_t kTagGBA = 0xBAULL << 56;
constexpr uint64_t kTagGB  = 0x6BULL << 56;

constexpr uint64_t MakeKey(uint64_t tag, uint32_t game_code_be, uint32_t crc32) {
    // Tag (8 bits) | the code's last three letters (24 bits) | CRC (32 bits; it covers the whole code)
    return tag | (static_cast<uint64_t>(game_code_be & 0x00FFFFFFU) << 32) |
           static_cast<uint64_t>(crc32);
}

constexpr bool IsAltKey(uint64_t key) {
    const uint64_t tag = key & (0xFFULL << 56);
    return tag == kTagNDS || tag == kTagDSi || tag == kTagGBA || tag == kTagGB;
}

} // namespace Common::AltCore
