// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

// AzaharVR: the product this build is. One codebase makes two: "Azahar for Frame" (the Steam
// Frame, Linux aarch64) and "Azahar VR" (PC VR: Linux x86-64 and Windows), picked at build time by
// AZAHARVR_PC (CMakeLists.txt). Only player-facing names and folders follow it; internal names
// (frame_xr, ENABLE_FRAME_XR, AZAHARFRAME_*) stay.
namespace Common::FrameProduct {

#ifdef AZAHARVR_PC
inline constexpr bool kPc = true;
/// The name players see (window title, --version, the OpenXR application name).
inline constexpr const char* kName = "Azahar VR";
/// The test-switch folder under the config dir ($XDG_CONFIG_HOME, %APPDATA% on Windows).
inline constexpr const char* kSwitchFolder = "AzaharVR";
#else
inline constexpr bool kPc = false;
inline constexpr const char* kName = "Azahar for Frame";
inline constexpr const char* kSwitchFolder = "AzaharFrame";
#endif

} // namespace Common::FrameProduct
