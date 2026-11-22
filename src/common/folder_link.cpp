// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharVR: junctions on Windows (folder_link.h). Built on Windows only.

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include <winioctl.h>
#include "common/folder_link.h"

namespace Common::FS {

namespace {

// REPARSE_DATA_BUFFER's mount point form (ntifs.h is a driver header)
struct MountPointHeader {
    DWORD reparse_tag;
    WORD reparse_data_length;
    WORD reserved;
    WORD substitute_name_offset;
    WORD substitute_name_length;
    WORD print_name_offset;
    WORD print_name_length;
    // then PathBuffer: the substitute name and the print name, each NUL-terminated
};

std::error_code LastError() {
    return {static_cast<int>(GetLastError()), std::system_category()};
}

} // namespace

bool CreateJunction(const std::filesystem::path& target_in, const std::filesystem::path& link,
                    std::error_code& ec) {
    namespace fs = std::filesystem;
    ec.clear();
    fs::path target = target_in;
    if (target.is_relative()) {
        target = link.parent_path() / target;
    }
    target = fs::absolute(target, ec);
    if (ec) {
        return false;
    }
    target = target.lexically_normal().make_preferred();
    std::wstring print = target.wstring();
    while (print.size() > 3 && (print.back() == L'\\' || print.back() == L'/')) {
        print.pop_back();
    }
    // A junction points at a local drive's path ("C:\..."), not at a share
    if (print.size() < 3 || print[1] != L':' || print[2] != L'\\') {
        ec = std::make_error_code(std::errc::not_supported);
        return false;
    }
    const std::wstring substitute = L"\\??\\" + print;

    const fs::path link_path = fs::path(link).make_preferred();
    if (!CreateDirectoryW(link_path.c_str(), nullptr)) {
        ec = LastError();
        return false;
    }
    HANDLE h = CreateFileW(link_path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        ec = LastError();
        RemoveDirectoryW(link_path.c_str());
        return false;
    }
    const std::size_t sub_bytes = substitute.size() * sizeof(wchar_t);
    const std::size_t print_bytes = print.size() * sizeof(wchar_t);
    const std::size_t path_bytes = sub_bytes + sizeof(wchar_t) + print_bytes + sizeof(wchar_t);
    std::vector<char> buffer(sizeof(MountPointHeader) + path_bytes, 0);
    auto* header = reinterpret_cast<MountPointHeader*>(buffer.data());
    header->reparse_tag = IO_REPARSE_TAG_MOUNT_POINT;
    // The data length counts from the substitute name's offset field onwards
    header->reparse_data_length = static_cast<WORD>(8 + path_bytes);
    header->substitute_name_offset = 0;
    header->substitute_name_length = static_cast<WORD>(sub_bytes);
    header->print_name_offset = static_cast<WORD>(sub_bytes + sizeof(wchar_t));
    header->print_name_length = static_cast<WORD>(print_bytes);
    char* const names = buffer.data() + sizeof(MountPointHeader);
    std::memcpy(names, substitute.data(), sub_bytes);
    std::memcpy(names + sub_bytes + sizeof(wchar_t), print.data(), print_bytes);
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, buffer.data(),
                                    static_cast<DWORD>(buffer.size()), nullptr, 0, &returned,
                                    nullptr);
    if (!ok) {
        ec = LastError();
    }
    CloseHandle(h);
    if (!ok) {
        RemoveDirectoryW(link_path.c_str());
        return false;
    }
    return true;
}

} // namespace Common::FS

#endif
