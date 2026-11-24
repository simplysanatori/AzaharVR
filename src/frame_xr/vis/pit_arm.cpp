// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <iterator>
#include <tuple>

#include <lodepng.h>

#include "frame_xr/vis/pit_arm.h"

namespace FrameXr::Vis {

namespace {

#include "frame_xr/vis/pit_arm_data.inc"
#include "pit_arm_images.inc" // kPitArmColourPng, kPitArmSurfacePng (CMakeModules/EmbedBytes.cmake)

struct Writer {
    std::vector<uint8_t> out;
    template <typename T>
    void Put(T v) {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        out.insert(out.end(), p, p + sizeof(T));
    }
    void Floats(const float* p, std::size_t n) {
        for (std::size_t i = 0; i < n; i++) {
            Put(p[i]);
        }
    }
};

} // namespace

std::vector<uint8_t> PitArmFile() {
    std::vector<uint8_t> colour, surface;
    for (const auto& [png, size, pixels] :
         {std::tuple{kPitArmColourPng, std::size(kPitArmColourPng), &colour},
          std::tuple{kPitArmSurfacePng, std::size(kPitArmSurfacePng), &surface}}) {
        unsigned w = 0, h = 0;
        if (lodepng::decode(*pixels, w, h, png, size) != 0 || w != kPitArmTextureSize ||
            h != kPitArmTextureSize) {
            return {};
        }
    }
    Writer w;
    w.out.reserve(256 + kPitArmVertexCount * 56 + std::size(kPitArmIndex) * 4 + colour.size() * 2);
    const char magic[9] = "PITHAND1";
    w.out.insert(w.out.end(), magic, magic + 8);
    const uint32_t plate = kPitArmPlateTriangleCount * 3;
    w.Put<uint32_t>(1);
    w.Put(kPitArmVertexCount);
    w.Put(kPitArmTriangleCount * 3 - plate);
    w.Put(plate);
    w.Put(kPitArmJointCount);
    w.Put(kPitArmTextureSize);
    w.Put(kPitArmTextureSize);
    w.Put<uint32_t>(0);
    w.Put(kPitArmHandLength);
    w.Put(kPitArmForearmRadius);
    w.Put(kPitArmWristRadius);
    w.Put(0.0f);
    w.Floats(kPitArmElbow, 3);
    w.Floats(kPitArmHandle, 7);
    w.Floats(kPitArmFit, 16);
    w.Floats(kPitArmFitPivot, 3);
    for (uint32_t j = 0; j < kPitArmJointCount; j++) {
        w.Put<int32_t>(kPitArmJointParent[j]);
        w.Put<int32_t>(kPitArmJointRole[j]);
        w.Put<uint32_t>(kPitArmJointFlags[j]);
        w.Floats(&kPitArmJointPosition[j * 3], 3);
        w.Floats(&kPitArmJointFrame[j * 9], 9);
        w.Put(kPitArmJointRadius[j]);
    }
    for (uint32_t v = 0; v < kPitArmVertexCount; v++) {
        for (uint32_t a = 0; a < 3; a++) {
            w.Put(static_cast<float>(kPitArmPos[v * 3 + a]) * kPitArmPosScale[a] +
                  kPitArmPosMin[a]);
        }
        for (uint32_t a = 0; a < 3; a++) {
            w.Put(static_cast<float>(kPitArmNormal[v * 3 + a]) / 127.0f);
        }
        for (uint32_t a = 0; a < 4; a++) {
            w.Put(static_cast<float>(kPitArmTangent[v * 4 + a]) / 127.0f);
        }
        for (uint32_t a = 0; a < 2; a++) {
            w.Put(static_cast<float>(kPitArmUv[v * 2 + a]) / 65535.0f);
        }
    }
    for (uint32_t v = 0; v < kPitArmVertexCount; v++) {
        w.out.insert(w.out.end(), &kPitArmBone[v * 4], &kPitArmBone[v * 4] + 4);
        w.out.insert(w.out.end(), &kPitArmWeight[v * 4], &kPitArmWeight[v * 4] + 4);
    }
    for (const uint16_t i : kPitArmIndex) {
        w.Put<uint32_t>(i);
    }
    w.out.insert(w.out.end(), colour.begin(), colour.end());
    w.out.insert(w.out.end(), surface.begin(), surface.end());
    return std::move(w.out);
}

bool BuiltInPitModel(PitModel& out, std::string& error) {
    return ParsePitModel(PitArmFile(), out, error);
}

} // namespace FrameXr::Vis
