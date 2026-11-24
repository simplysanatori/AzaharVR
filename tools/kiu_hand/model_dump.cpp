// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Pit's arm from a hand file (src/frame_xr/vis/pit_model.h), as the app's own code poses it,
// written out for the desktop previews (preview/shots.mjs):
//
//   g++ -std=c++20 -O2 -Isrc tools/kiu_hand/model_dump.cpp src/frame_xr/vis/pit_model.cpp
//       src/frame_xr/vis/pit_hand.cpp -o /tmp/model_dump && /tmp/model_dump HAND_FILE OUT_DIR
//
// OUT_DIR gets: <pose>.verts (12 float32 a vertex) for every named pose with the forearm turned
// to where a player's elbow is, bindpose.verts (the file's own), free_half.verts,
// indices.u32, colour.rgba, surface.rgba, and info.json (counts, sizes, the fits).

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "frame_xr/vis/pit_model.h"

using namespace FrameXr::Vis;

namespace {

template <typename T>
void Write(const std::string& path, const std::vector<T>& data) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::perror(path.c_str());
        return;
    }
    std::fwrite(data.data(), sizeof(T), data.size(), f);
    std::fclose(f);
}

void Matrix(std::FILE* f, const char* name, const PitMat4& m) {
    std::fprintf(f, "  \"%s\": [", name);
    for (std::size_t i = 0; i < 16; i++) {
        std::fprintf(f, "%s%.9g", i ? ", " : "", m[i]);
    }
    std::fprintf(f, "],\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s HAND_FILE OUT_DIR\n", argv[0]);
        return 2;
    }
    PitModel model;
    std::string error;
    if (!LoadPitModel(argv[1], model, error)) {
        std::fprintf(stderr, "%s: %s\n", argv[1], error.empty() ? "no such file" : error.c_str());
        return 1;
    }
    const std::string dir = argv[2];
    // Where a player's elbow is with the controller held level in front: below and behind the
    // hand, a little outward (the left arm), taken into arm space by the fit turned back.
    const PitMat4 fit = PitModelControllerFit(model, true);
    PitVec3 elbow{};
    {
        const float aim[3] = {-0.013f - 0.05f, -0.0444f - 0.11f, 0.0871f + 0.23f};
        for (std::size_t k = 0; k < 3; k++) {
            float sum = 0.0f;
            for (std::size_t r = 0; r < 3; r++) {
                sum += fit[k * 4 + r] * (aim[r] - fit[12 + r]);
            }
            elbow[k] = sum;
        }
    }
    std::vector<float> out(std::size_t{model.vertex_count} * kPitVertexFloats);
    const auto pose = [&](const std::string& name, const PitHandPose* p, const PitVec3* e) {
        PitModelSkin(model, PitModelEvaluate(model, p, e), out);
        Write(dir + "/" + name + ".verts", out);
    };
    pose("bindpose", nullptr, nullptr);
    for (int i = 0; i < static_cast<int>(PitHandNamed::Count); i++) {
        const auto name = static_cast<PitHandNamed>(i);
        pose(std::string(PitHandNamedLabel(name)), &PitHandNamedPose(name), &elbow);
        pose(std::string(PitHandNamedLabel(name)) + "_straight", &PitHandNamedPose(name), nullptr);
        // the pose as it is, the handle not minded (a fist closed tight)
        PitModelSkin(model, PitModelEvaluate(model, &PitHandNamedPose(name), nullptr, false), out);
        Write(dir + "/" + std::string(PitHandNamedLabel(name)) + "_plain.verts", out);
    }
    {
        PitHandInputs in; // as the app: nothing touched, then the trigger pulled
        const PitHandPose idle = PitHandPoseFromInputs(in, PitHandGrasp::Free);
        pose("free_idle", &idle, &elbow);
        in.trigger_touch = true;
        in.trigger = 1.0f;
        const PitHandPose trigger = PitHandPoseFromInputs(in, PitHandGrasp::Free);
        pose("free_trigger", &trigger, &elbow);
        in.grip_touch = true;
        in.grip = 1.0f;
        in.thumb_rest = true;
        const PitHandPose fist = PitHandPoseFromInputs(in, PitHandGrasp::Free);
        pose("free_fist", &fist, &elbow);
    }
    Write(dir + "/indices.u32", model.indices);
    Write(dir + "/colour.rgba", model.colour);
    Write(dir + "/surface.rgba", model.surface);
    std::FILE* f = std::fopen((dir + "/info.json").c_str(), "w");
    if (!f) {
        std::perror("info.json");
        return 1;
    }
    std::fprintf(f, "{\n  \"vertices\": %u,\n  \"body_indices\": %u,\n  \"plate_indices\": %u,\n", model.vertex_count,
                 model.body_index_count, model.plate_index_count);
    std::fprintf(f, "  \"texture\": [%u, %u],\n  \"hand_length\": %.6g,\n  \"forearm_radius\": %.6g,\n", model.texture_width,
                 model.texture_height, model.hand_length, model.forearm_radius);
    std::fprintf(f, "  \"wrist_radius\": %.6g,\n  \"elbow\": [%.6g, %.6g, %.6g],\n  \"bind_elbow\": [%.6g, %.6g, %.6g],\n",
                 model.wrist_radius, elbow[0], elbow[1], elbow[2], model.elbow[0], model.elbow[1], model.elbow[2]);
    std::fprintf(f, "  \"handle\": [%.6g, %.6g, %.6g, %.6g, %.6g, %.6g, %.6g],\n", model.handle.centre[0], model.handle.centre[1],
                 model.handle.centre[2], model.handle.axis[0], model.handle.axis[1], model.handle.axis[2], model.handle.radius);
    // How far each held pose's skin comes inside the handle's cylinder (metres; negative: inside).
    for (const PitHandNamed name : {PitHandNamed::Grip, PitHandNamed::Trigger, PitHandNamed::GripStick, PitHandNamed::GripOpen,
                                    PitHandNamed::Fist, PitHandNamed::Relaxed}) {
        PitModelSkin(model, PitModelEvaluate(model, &PitHandNamedPose(name), nullptr), out);
        float least = 1e9f;
        for (std::size_t v = 0; v < model.vertex_count; v++) {
            const float* p = &out[v * kPitVertexFloats];
            float q[3], along = 0.0f;
            for (std::size_t k = 0; k < 3; k++) {
                q[k] = p[k] - model.handle.centre[k];
                along += q[k] * model.handle.axis[k];
            }
            if (std::abs(along) > 0.05f) {
                continue; // past the handle's ends
            }
            float d2 = 0.0f;
            for (std::size_t k = 0; k < 3; k++) {
                const float off = q[k] - along * model.handle.axis[k];
                d2 += off * off;
            }
            least = std::min(least, std::sqrt(d2) - model.handle.radius);
        }
        const std::string label(PitHandNamedLabel(name));
        std::fprintf(f, "  \"handle_gap_%s\": %.5f,\n", label.c_str(), least);
    }
    // On the controller (through the fit, the left one's aim space, metres): how far the index
    // finger's tip is from the trigger's face and the thumb's from the stick's top in each pose
    // the app can make, and how far the wrist is bent to reach the elbow.
    {
        const float trigger[3] = {-0.0055f, -0.0395f, 0.0100f}, stick[3] = {0.0089f, 0.0113f, 0.0401f};
        const auto to_aim = [&fit](const std::array<float, 3>& p) {
            return std::array<float, 3>{fit[0] * p[0] + fit[4] * p[1] + fit[8] * p[2] + fit[12],
                                        fit[1] * p[0] + fit[5] * p[1] + fit[9] * p[2] + fit[13],
                                        fit[2] * p[0] + fit[6] * p[1] + fit[10] * p[2] + fit[14]};
        };
        const auto tip = [&](const PitHandPose& p, int role) {
            const std::vector<PitMat34> bones = PitModelEvaluate(model, &p, nullptr);
            for (std::size_t j = 0; j < model.joints.size(); j++) {
                if (model.joints[j].role == role) {
                    const PitMat34& b = bones[j];
                    const auto& q = model.joints[j].position;
                    return to_aim({b[0] * q[0] + b[1] * q[1] + b[2] * q[2] + b[3], b[4] * q[0] + b[5] * q[1] + b[6] * q[2] + b[7],
                                   b[8] * q[0] + b[9] * q[1] + b[10] * q[2] + b[11]});
                }
            }
            return std::array<float, 3>{};
        };
        const auto apart = [](const std::array<float, 3>& a, const float* b) {
            return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
        };
        PitHandInputs in;
        in.trigger_touch = true;
        const PitHandPose touch = PitHandPoseFromInputs(in, PitHandGrasp::Free);
        in.trigger = 1.0f;
        in.thumb_stick = true;
        in.grip_touch = true;
        in.grip = 1.0f;
        const PitHandPose pulled = PitHandPoseFromInputs(in, PitHandGrasp::Free);
        std::fprintf(f, "  \"index_tip_to_trigger_free_touch\": %.4f,\n", apart(tip(touch, kPitIndexTip), trigger));
        std::fprintf(f, "  \"index_tip_to_trigger_free_pulled\": %.4f,\n", apart(tip(pulled, kPitIndexTip), trigger));
        std::fprintf(f, "  \"index_tip_to_trigger_held_grip\": %.4f,\n", apart(tip(PitHandNamedPose(PitHandNamed::Grip), kPitIndexTip), trigger));
        std::fprintf(f, "  \"thumb_tip_to_stick_free_fist\": %.4f,\n", apart(tip(pulled, kPitThumbTip), stick));
        std::fprintf(f, "  \"thumb_tip_to_stick_held\": %.4f,\n", apart(tip(PitHandNamedPose(PitHandNamed::GripStick), kPitThumbTip), stick));
        const float el = std::hypot(elbow[0], elbow[1], elbow[2]), bl = std::hypot(model.elbow[0], model.elbow[1], model.elbow[2]);
        const float c = (elbow[0] * model.elbow[0] + elbow[1] * model.elbow[1] + elbow[2] * model.elbow[2]) / (el * bl);
        std::fprintf(f, "  \"wrist_bend_degrees\": %.1f,\n", std::acos(std::clamp(c, -1.0f, 1.0f)) * 57.29578f);
    }
    Matrix(f, "fit_left", fit);
    Matrix(f, "fit_right", PitModelControllerFit(model, false));
    // The cost of a pose.
    double best = 1e9;
    for (int run = 0; run < 500; run++) {
        const auto t0 = std::chrono::steady_clock::now();
        PitModelSkin(model, PitModelEvaluate(model, &PitHandNamedPose(PitHandNamed::Relaxed), &elbow), out);
        best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    std::fprintf(f, "  \"pose_and_skin_ms\": %.5f\n}\n", best);
    std::fclose(f);
    std::printf("%u vertices, %u + %u indices, %ux%u, pose and skin %.4f ms\n", model.vertex_count, model.body_index_count,
                model.plate_index_count, model.texture_width, model.texture_height, best);
    return 0;
}
