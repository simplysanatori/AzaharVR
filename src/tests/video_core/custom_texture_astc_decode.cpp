// Copyright Azahar for Frame contributors
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

// AzaharVR: a GPU that samples no ASTC (desktop GPUs) gets an ASTC pack, such as Optimize for
// Frame's copy, decoded to RGBA8 as it is read (material.cpp LoadDDS). The file is made by the
// app's own converter, as the pack converter writes it.

#ifdef AZAHARVR_PC

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "core/custom_tex/ktx_encode.h"
#include "core/frontend/image_interface.h"
#include "video_core/custom_textures/custom_format.h"
#include "video_core/custom_textures/material.h"

namespace {

std::string WriteAstcKtx(const std::string& name, u32 size, const u8 rgba[4]) {
    CustomTex::Convert::AstcEncoder encoder({4, 4}, 60.0f);
    REQUIRE(encoder.Error().empty());
    std::vector<u8> pixels(std::size_t{size} * size * 4);
    for (std::size_t i = 0; i < pixels.size(); i++) {
        pixels[i] = rgba[i % 4];
    }
    std::string error;
    const std::vector<u8> file = encoder.EncodeRgba(size, size, pixels, false, &error);
    REQUIRE(error.empty());
    REQUIRE(!file.empty());
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return path.string();
}

} // namespace

TEST_CASE("Custom textures: an ASTC pack is decoded to RGBA8 for a GPU that cannot sample it",
          "[pcvr][custom_tex]") {
    const u8 colour[4] = {200, 120, 40, 255};
    const std::string path = WriteAstcKtx("azaharvr-astc-decode.ktx", 16, colour);
    Frontend::ImageInterface images;

    {
        VideoCore::SetDecodeAstc(false);
        VideoCore::CustomTexture probe(images);
        probe.path = path;
        probe.file_format = VideoCore::CustomFileFormat::KTX;
        probe.LoadFromDisk(false);
        INFO("without decoding: loaded " << probe.IsLoaded() << " format "
                                         << static_cast<int>(probe.format) << " levels "
                                         << probe.NumLevels());
        CHECK(probe.IsLoaded());
    }
    VideoCore::SetDecodeAstc(true);
    VideoCore::CustomTexture decoded(images);
    decoded.path = path;
    decoded.file_format = VideoCore::CustomFileFormat::KTX;
    decoded.LoadFromDisk(false);
    INFO("file size " << std::filesystem::file_size(path));
    REQUIRE(decoded.IsLoaded());
    CHECK(decoded.format == VideoCore::CustomPixelFormat::RGBA8);
    CHECK(decoded.width == 16);
    CHECK(decoded.height == 16);
    REQUIRE(decoded.NumLevels() >= 1);
    const auto level0 = decoded.LevelData(0);
    REQUIRE(level0.size() == 16 * 16 * 4);
    for (std::size_t i = 0; i < level0.size(); i++) {
        const int want = colour[i % 4];
        CHECK(std::abs(static_cast<int>(level0[i]) - want) <= 2);
    }

    // A GPU that samples ASTC keeps the file's blocks as they are
    VideoCore::SetDecodeAstc(false);
    VideoCore::CustomTexture kept(images);
    kept.path = path;
    kept.file_format = VideoCore::CustomFileFormat::KTX;
    kept.LoadFromDisk(false);
    REQUIRE(kept.IsLoaded());
    CHECK(kept.format == VideoCore::CustomPixelFormat::ASTC4);
    CHECK(kept.LevelData(0).size() == (16 / 4) * (16 / 4) * 16);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

#endif
