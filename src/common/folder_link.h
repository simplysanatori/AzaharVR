// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharVR: the link to a folder that switches a game's texture pack (load/textures/<title>).
// On Linux it is a symlink, as before. On Windows a symlink needs Developer Mode or admin, so
// the link is a junction, which any user may make (an absolute target on a local drive), and a
// symlink only when a junction cannot be made (a target on a network share). Windows' C++
// library reports a junction as file_type::junction, not as a symlink: IsLink counts both.
// Removing a junction (remove, remove_all) removes the link only, as with a symlink.

#pragma once

#include <filesystem>
#include <system_error>

namespace Common::FS {

/// A link to somewhere else: a symlink, or (Windows) a junction.
inline bool IsLink(const std::filesystem::file_status& status) {
#ifdef _WIN32
    return std::filesystem::is_symlink(status) ||
           status.type() == std::filesystem::file_type::junction;
#else
    return std::filesystem::is_symlink(status);
#endif
}

inline bool IsLink(const std::filesystem::path& path, std::error_code& ec) {
    return IsLink(std::filesystem::symlink_status(path, ec));
}

#ifdef _WIN32
/// A junction at `link` (which must not exist) to the folder `target` (absolute, or relative to
/// the link's folder). False with `ec` set when Windows refuses it.
bool CreateJunction(const std::filesystem::path& target, const std::filesystem::path& link,
                    std::error_code& ec);
#endif

/// A link at `link` to the folder `target`: a symlink on Linux (the target as given); on Windows
/// a junction, or a folder symlink when no junction can be made.
inline void CreateFolderLink(const std::filesystem::path& target,
                             const std::filesystem::path& link, std::error_code& ec) {
#ifdef _WIN32
    if (CreateJunction(target, link, ec)) {
        return;
    }
    std::error_code junction_ec = ec;
    ec.clear();
    std::filesystem::create_directory_symlink(std::filesystem::path(target).make_preferred(), link,
                                              ec);
    if (ec) {
        ec = junction_ec; // the junction's reason: the symlink is the rarer case
    }
#else
    std::filesystem::create_directory_symlink(target, link, ec);
#endif
}

} // namespace Common::FS
