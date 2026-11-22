// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharFrame romfsload: a save state brings back where the RomFS sat in the ROM file it was made
// with. A state made with the game's NCCH at the start of its file (an installed .app, a .cxi)
// and loaded with a .3ds (the NCCH a partition 0x4000 in) read every RomFS file 0x4000 bytes
// early: Mario Kart 7 got other bytes for the next course and crashed (reports/88/memcorrupt).

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#ifdef _WIN32 // AzaharVR: getpid
#include <process.h>
#else
#include <unistd.h>
#endif
#include "common/file_derived.h"
#include "common/file_util.h"
#include "core/file_sys/romfs_reader.h"

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kPartition = 0x4000; // the NCCH's place in a .3ds (NCSD partition 0)
constexpr std::size_t kRomFS = 0x6000;     // the RomFS's place in the NCCH
constexpr std::size_t kRomFSSize = 0x10000;

u8 ByteAt(std::size_t file_offset) {
    return static_cast<u8>((file_offset * 7 + file_offset / 251) & 0xFF);
}

struct Rom {
    fs::path path;
    Rom() : path(fs::temp_directory_path() / ("azf_romfsload_" + std::to_string(getpid()))) {
        std::vector<u8> bytes(kPartition + kRomFS + kRomFSSize);
        for (std::size_t i = 0; i < bytes.size(); i++) {
            bytes[i] = ByteAt(i);
        }
        std::ofstream(path, std::ios::binary)
            .write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    ~Rom() {
        std::error_code ec;
        fs::remove(path, ec);
    }
    std::unique_ptr<FileUtil::IOFileBase> File() const {
        return std::make_unique<FileUtil::IOFile>(path.string(), "rb");
    }
};

/// What NCCHContainer::ReadRomFS makes for a .3ds: RomFS in partition in file.
std::shared_ptr<FileSys::RomFSReader> FromCci(const Rom& rom) {
    auto partition =
        std::make_unique<FileUtil::SubIOFile>(rom.File(), kPartition, kRomFS + kRomFSSize);
    return std::make_shared<FileSys::DirectRomFSReader>(
        std::make_unique<FileUtil::SubIOFile>(std::move(partition), kRomFS, kRomFSSize));
}

/// What the state made with the NCCH at the start of the file brings back for the same path.
std::shared_ptr<FileSys::RomFSReader> FromNcchAtZero(const Rom& rom) {
    return std::make_shared<FileSys::DirectRomFSReader>(
        std::make_unique<FileUtil::SubIOFile>(rom.File(), kRomFS, kRomFSSize));
}

/// The RomFS bytes at `offset` as the running ROM holds them.
bool ReadsRunningRom(FileSys::RomFSReader& reader, std::size_t offset, std::size_t length) {
    std::vector<u8> got(length);
    if (reader.ReadFile(offset, length, got.data()) != length) {
        return false;
    }
    for (std::size_t i = 0; i < length; i++) {
        if (got[i] != ByteAt(kPartition + kRomFS + offset + i)) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("romfsload: a state's RomFS from another container reads the running ROM",
          "[core][file_sys][romfsload]") {
    const Rom rom;
    const auto restored = FromNcchAtZero(rom);
    // Before: a large read (not cached) and a small one (cached) both land 0x4000 early.
    REQUIRE_FALSE(ReadsRunningRom(*restored, 0x2000, 0x3000));
    REQUIRE_FALSE(ReadsRunningRom(*restored, 0x100, 0x40));

    const auto running = FromCci(rom);
    REQUIRE(ReadsRunningRom(*running, 0x2000, 0x3000));
    REQUIRE(FileSys::UseRunningRomFS(*restored, *running));

    // The same reader object (open file sessions and the SelfNCCH archive share it) now reads
    // the running ROM, its cache included.
    CHECK(ReadsRunningRom(*restored, 0x2000, 0x3000));
    CHECK(ReadsRunningRom(*restored, 0x100, 0x40));
    CHECK(ReadsRunningRom(*restored, kRomFSSize - 0x10, 0x10));
}

TEST_CASE("romfsload: a state's RomFS that already reads the running ROM is kept",
          "[core][file_sys][romfsload]") {
    const Rom rom;
    const auto restored = FromCci(rom);
    const auto running = FromCci(rom);
    CHECK_FALSE(FileSys::UseRunningRomFS(*restored, *running));
    CHECK(ReadsRunningRom(*restored, 0x2000, 0x3000));
    // `running` keeps its file when nothing is taken.
    CHECK(ReadsRunningRom(*running, 0x100, 0x40));
}
