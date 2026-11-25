// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_xr/menu/core_rows.h"

#include <bit>

#include "frame_xr/menu/tab_menu.h"

namespace FrameXr::Menu {

namespace {

enum class Need {
    Any,
    Bottom, ///< a Secondary Screen now
    Touch,
    Gyro,
    Stereo,
    Flat,
    Bfi,
    DynamicResolution,
    InternalResolution,
    TextureFilter,
    Autosave,
    // Rows a 3DS game never has
    ConsoleGroup, ///< any of the Console group's rows
    SolarSensor,
    ConsoleMode,
    DsTouch,
    MicBlow,
    Lid,
    Rotation,
    SaveImport,
    SecondScreen, ///< two choices or more
    EditGame,
};

struct Rule {
    Id id;
    Need need;
};

constexpr Rule kRules[] = {
    // Position
    {Id::HideBottomScreen, Need::Bottom},
    {Id::ScreenLayout, Need::Bottom},
    {Id::MoveScreens, Need::Bottom},
    {Id::SwapScreens, Need::Bottom},
    {Id::ScreenCurve, Need::Any},
    {Id::DsScreenRotation, Need::Rotation},
    // Settings, General
    {Id::CoreHeader, Need::ConsoleGroup},
    {Id::GbaSolarLevel, Need::SolarSensor},
    {Id::DsConsoleMode, Need::ConsoleMode},
    {Id::SecondScreen, Need::SecondScreen},
    {Id::AllowGameEditing, Need::EditGame},
    {Id::DsTouchEnabled, Need::DsTouch},
    {Id::DsMicBlow, Need::MicBlow},
    {Id::DsLidToggle, Need::Lid},
    {Id::ImportSave, Need::SaveImport},
    {Id::Background, Need::Any},
    {Id::WindowScale, Need::Any},
    {Id::FaceButtons, Need::Any},
    {Id::MotionControls, Need::Gyro},
    {Id::ShowControllers, Need::Any},
    {Id::ControllersInFront, Need::Any},
    {Id::FastForwardSpeed, Need::Any},
    {Id::FastForwardAudio, Need::Any},
    {Id::SavesHeader, Need::Autosave},
    {Id::Autosave, Need::Autosave},
    {Id::AutosaveEvery, Need::Autosave},
    {Id::AutosaveKeep, Need::Autosave},
    {Id::AutosaveCard, Need::Autosave}, // Load State's
    {Id::GamesHeader, Need::Any}, // the covers' rows, while there is a Games tab
    {Id::DownloadGameCovers, Need::Any},
    {Id::DownloadThumbnails, Need::Any},
    {Id::MoreSettings, Need::Any},
    {Id::SwitchToFlat, Need::Flat},
    {Id::HelpHeader, Need::Any},
    {Id::ReportProblem, Need::Any},
    {Id::AboutApp, Need::Any},
    // Settings, Graphics
    {Id::Depth3d, Need::Stereo},
    {Id::InternalResolution, Need::InternalResolution},
    {Id::DynamicResolution, Need::DynamicResolution},
    {Id::TextureFilter, Need::TextureFilter},
    {Id::MotionClarityNormal, Need::Bfi},
    {Id::MatchBottomNormal, Need::Bfi},
    // Controls
    {Id::TouchpadTouch, Need::Touch},
    {Id::BottomScreenReticle, Need::Touch},
    {Id::ReticleSpeed, Need::Touch},
    {Id::ReticleAtEdge, Need::Touch},
    {Id::ReticleCenter, Need::Touch},
    {Id::ReticleStick, Need::Touch},
    {Id::ReticleEdgeCStick, Need::Touch},
    {Id::GyroAim, Need::Gyro},
    {Id::GyroSensitivity, Need::Gyro},
};

constexpr bool ForOthersOnly(Need need) {
    return need >= Need::ConsoleGroup;
}

bool Has(Need need, const CoreCaps& core) {
    switch (need) {
    case Need::Any:
        return true;
    case Need::Bottom:
        return core.screens.has_bottom;
    case Need::Touch:
        return core.has_touch;
    case Need::DsTouch:
        return core.has_touch && Common::AltCore::IsDs(core.kind);
    case Need::Gyro:
        return core.has_gyro;
    case Need::Stereo:
        return core.has_stereo_3d;
    case Need::Flat:
        return core.has_flat_mode;
    case Need::Bfi:
        return core.has_bfi;
    case Need::DynamicResolution:
        return core.has_dynamic_resolution;
    case Need::InternalResolution:
        return kAltInternalResolution;
    case Need::TextureFilter:
        return kAltTextureFilter;
    case Need::Autosave:
        return kAltAutosave;
    case Need::ConsoleGroup:
        return core.has_solar_sensor || core.has_dsi_mode || Has(Need::DsTouch, core) ||
               core.has_mic_blow || core.has_lid_close || core.has_save_import ||
               Has(Need::SecondScreen, core) || core.can_edit_game;
    case Need::SolarSensor:
        return core.has_solar_sensor;
    case Need::ConsoleMode:
        return core.has_dsi_mode;
    case Need::MicBlow:
        return core.has_mic_blow;
    case Need::Lid:
        return core.has_lid_close;
    case Need::Rotation:
        return core.has_screen_rotation;
    case Need::SaveImport:
        return core.has_save_import;
    case Need::SecondScreen:
        return std::popcount(core.second_screen_choices) >= 2;
    case Need::EditGame:
        return core.can_edit_game;
    }
    return false;
}

} // namespace

bool RowApplies(Id id, const CoreCaps& core) {
    for (const Rule& rule : kRules) {
        if (rule.id == id) {
            return Is3ds(core) ? !ForOthersOnly(rule.need) : Has(rule.need, core);
        }
    }
    return Is3ds(core);
}

bool TabApplies(Tab tab, const CoreCaps& core) {
    switch (tab) {
    case Tab::Homebrew:
        return Is3ds(core);
    case Tab::Mods: // the other consoles' Cheats
        return Is3ds(core) || core.has_alt_cheats;
    default:
        return true;
    }
}

} // namespace FrameXr::Menu
