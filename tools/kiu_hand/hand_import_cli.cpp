// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// The hand importer (src/frame_xr/vis/hand_import.h) from a shell, as the app's menu would call
// it: ZIP is a model's zip the person already has, DIR stands in for the user directory (the
// hand is saved as DIR/kiu_hand/pit_hand.bin).
//
//   g++ -std=c++20 -O2 -Isrc -Iexternals/lodepng/lodepng tools/kiu_hand/hand_import_cli.cpp
//       src/frame_xr/vis/hand_import.cpp src/frame_xr/vis/pit_model.cpp src/frame_xr/vis/pit_hand.cpp
//       externals/lodepng/lodepng/lodepng.cpp -o /tmp/hand_import && /tmp/hand_import ZIP DIR

#include <chrono>
#include <cstdio>
#include <string>

#include "frame_xr/vis/hand_import.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s ZIP DIR\n", argv[0]);
        return 2;
    }
    const auto start = std::chrono::steady_clock::now();
    std::string last;
    const std::string error = FrameXr::Vis::ImportHandFile(argv[1], argv[2], [&last](std::string_view doing, float done) {
        if (doing != last) {
            last = doing;
            std::printf("%3.0f%% %.*s\n", done * 100.0f, static_cast<int>(doing.size()), doing.data());
        }
        return true;
    });
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (!error.empty()) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    std::printf("Done in %.2f s.\n", seconds);
    return 0;
}
