// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// AzaharVR pchelp: the Controllers setting's values and texts (controller_choice.h), with no
// other dependency (the in-game menu's library has no settings).
namespace FrameXr::Input {

/// The Controllers setting's values, in the order of its choices.
enum class ControllerType : uint32_t {
    Automatic,
    SteamFrame,
    MetaQuest,
    ValveIndex,
    ViveFocus3,
    Count,
};

/// The Controllers row's choices (in-game menu and Qt), in ControllerType's order.
inline constexpr std::array<const char*, static_cast<std::size_t>(ControllerType::Count)>
    kControllerTypeNames{"Automatic", "Steam Frame", "Meta Quest", "Valve Index",
                         "Vive XR Elite / Focus 3"};
inline constexpr const char* kControllerTypeLabel = "Controllers";
inline constexpr const char* kControllerTypeTip =
    "Which controllers the controls, pictures and buttons are for. Automatic uses the ones "
    "SteamVR reports.";

} // namespace FrameXr::Input
