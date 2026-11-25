// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <filesystem>
#include <string>

#ifdef _WIN32
#include "common/string_util.h"
#endif

namespace FrameCores {

// A std::string that names a file is UTF-8. On Windows std::filesystem and the streams take a
// std::string for the ANSI code page, so it goes through these on its way in and out. Elsewhere
// they change nothing.

#ifdef _WIN32 // AzaharVR

inline std::filesystem::path FsPath(const std::string& utf8) {
    return std::filesystem::path(Common::UTF8ToUTF16W(utf8));
}

inline std::string FsUtf8(const std::filesystem::path& path) {
    return Common::UTF16ToUTF8(path.generic_wstring()); // gba-ds looks for '/'
}

#else

inline std::filesystem::path FsPath(const std::string& utf8) {
    return std::filesystem::path(utf8);
}

inline std::string FsUtf8(const std::filesystem::path& path) {
    return path.string();
}

#endif

} // namespace FrameCores
