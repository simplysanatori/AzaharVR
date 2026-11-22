// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// AzaharVR: the few things the OpenXR frontend does that differ between Linux (the Steam Frame and
// PC) and Windows, in one place. The Linux side is what the frontend always did; the Windows side
// is plain Win32 and has not been compiled yet (no Windows SDK on the desktop; CI's windows-msvc
// job is its first build).
//
// Elsewhere, by file:
//  - the test-switch file: common/frame_test_switches.h (%APPDATA%\AzaharVR on Windows);
//  - the watchdog (watchdog.cpp), the crash handler (common/frame_crash.cpp), evdev input
//    (input/devices.cpp, input/phys_keyboard.cpp) and the memory guard (mem_watch.h,
//    mem_guard.h): Linux-only, compiled out on Windows (kHasWatchdog and the rest below), where
//    SDL's gamepads already join the VR controllers.
namespace FrameXr::Platform {

#ifdef _WIN32
inline constexpr bool kWindows = true;
/// Khronos's loader, shipped beside the program (SteamVR's runtime is found through the registry).
inline constexpr const char* kOpenXrLoaders[] = {"openxr_loader.dll"};
inline constexpr const char* kVulkanLoaders[] = {"vulkan-1.dll"};
#else
inline constexpr bool kWindows = false;
/// SteamVR's own loader on the Frame first, then the system's (PC Linux: the tarball's or the
/// distribution's).
inline constexpr const char* kOpenXrLoaders[] = {"/opt/steamvr/bin/linuxarm64/libopenxr_loader.so",
                                                 "libopenxr_loader.so.1", "libopenxr_loader.so"};
inline constexpr const char* kVulkanLoaders[] = {"libvulkan.so.1", "libvulkan.so"};
#endif

/// Linux-only parts (see above): false on Windows, where they are stubs.
inline constexpr bool kHasWatchdog = !kWindows;
inline constexpr bool kHasEvdev = !kWindows;
inline constexpr bool kHasMemoryGuard = !kWindows;

/// A shared library, opened now with its symbols kept local; null on failure (LibraryError says why).
inline void* OpenLibrary(const char* path) {
#ifdef _WIN32
    return reinterpret_cast<void*>(LoadLibraryA(path));
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

inline void* LibrarySymbol(void* library, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}

/// Why the last OpenLibrary failed.
inline std::string LibraryError() {
#ifdef _WIN32
    return "Windows error " + std::to_string(GetLastError());
#else
    const char* e = dlerror();
    return e ? e : "unknown";
#endif
}

} // namespace FrameXr::Platform
