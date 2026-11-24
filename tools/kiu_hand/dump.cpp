// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Pit's hand: writes what the app's own code makes (src/frame_xr/vis/pit_hand.cpp,
// laurel_frame.cpp) for the desktop previews (sheets.py), and times the skinning:
//
//   g++ -std=c++20 -O2 -Isrc tools/kiu_hand/dump.cpp src/frame_xr/vis/pit_hand.cpp \
//       src/frame_xr/vis/laurel_frame.cpp -o /tmp/kiu_hand_dump && /tmp/kiu_hand_dump OUT_DIR
//
// Each OUT_DIR/<name>.mesh: "KHM1", vertex count, index count (uint32), then per vertex 9
// floats (position, normal, colour as the palette gives it), then uint32 indices. Positions
// are in arm units unless said. OUT_DIR/info.txt: the fits and places as text.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "frame_xr/vis/laurel_frame.h"
#include "frame_xr/vis/pit_hand.h"

using namespace FrameXr::Vis;

namespace {

void Write(const std::string& dir, const std::string& name, std::span<const float> vertices,
           std::span<const uint32_t> indices, const PitHandColours& colours) {
    const std::string path = dir + "/" + name + ".mesh";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::perror(path.c_str());
        return;
    }
    const auto vc = static_cast<uint32_t>(vertices.size() / kPitVertexFloats);
    const auto ic = static_cast<uint32_t>(indices.size());
    std::fwrite("KHM1", 1, 4, f);
    std::fwrite(&vc, 4, 1, f);
    std::fwrite(&ic, 4, 1, f);
    for (uint32_t v = 0; v < vc; v++) {
        const float* p = &vertices[static_cast<std::size_t>(v) * kPitVertexFloats];
        const PitRgb c = PitHandColourAt(colours, p[10], p[11]);
        const float row[9] = {p[0], p[1], p[2], p[3], p[4], p[5], c.r, c.g, c.b};
        std::fwrite(row, 4, 9, f);
    }
    std::fwrite(indices.data(), 4, ic, f);
    std::fclose(f);
}

std::vector<float> Skin(const PitHandSkeleton& s, bool mirrored = false) {
    std::vector<float> out(static_cast<std::size_t>(PitHandVertexCount()) * kPitVertexFloats);
    PitHandSkin(s, mirrored, out);
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s OUT_DIR\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const PitHandColours colours = PitHandDefaultColours();
    std::FILE* info = std::fopen((dir + "/info.txt").c_str(), "w");
    if (!info) {
        std::perror("info.txt");
        return 1;
    }
    for (int i = 0; i < static_cast<int>(PitHandNamed::Count); i++) {
        const auto name = static_cast<PitHandNamed>(i);
        const PitHandSkeleton s = PitHandEvaluate(PitHandNamedPose(name));
        Write(dir, std::string(PitHandNamedLabel(name)), Skin(s), PitHandIndices(), colours);
    }
    // The right hand, the forearm turned to an elbow, a tracked hand, halfway inputs.
    const PitHandSkeleton grip = PitHandEvaluate(PitHandNamedPose(PitHandNamed::Grip));
    Write(dir, "grip_right", Skin(grip, true), PitHandIndices(true), colours);
    // The forearm turned to where a player's elbow is with the controller held level in front:
    // below and behind the hand, a little outward (the left arm), taken into arm space by the
    // controller's fit turned back.
    PitVec3 elbow{};
    {
        const PitMat4 fit = PitHandControllerFit(true);
        const float aim[3] = {-0.013f - 0.05f, -0.0444f - 0.11f, 0.0871f + 0.23f};
        const float s2 = kPitHandUnit * kPitHandUnit;
        for (int k = 0; k < 3; k++) {
            float sum = 0.0f;
            for (int r = 0; r < 3; r++) {
                sum += fit[static_cast<std::size_t>(k * 4 + r)] * (aim[r] - fit[static_cast<std::size_t>(12 + r)]);
            }
            elbow[static_cast<std::size_t>(k)] = sum / s2;
        }
        std::fprintf(info, "elbow %g %g %g\n", elbow[0], elbow[1], elbow[2]);
    }
    for (int i = static_cast<int>(PitHandNamed::Grip); i < static_cast<int>(PitHandNamed::Count); i++) {
        const auto name = static_cast<PitHandNamed>(i);
        Write(dir, std::string(PitHandNamedLabel(name)) + "_elbow",
              Skin(PitHandEvaluate(PitHandNamedPose(name), &elbow)), PitHandIndices(), colours);
    }
    const PitHandSkeleton relaxed = PitHandEvaluate(PitHandNamedPose(PitHandNamed::Relaxed));
    Write(dir, "tracked_relaxed", Skin(PitHandEvaluate(PitHandPoseFromJoints(PitHandJointsOf(relaxed), false))),
          PitHandIndices(), colours);
    {
        PitHandInputs in;
        in.trigger_touch = true;
        in.trigger = 0.5f;
        in.grip = 0.5f;
        Write(dir, "free_half", Skin(PitHandEvaluate(PitHandPoseFromInputs(in, PitHandGrasp::Free))),
              PitHandIndices(), colours);
        Write(dir, "held_half", Skin(PitHandEvaluate(PitHandPoseFromInputs(in, PitHandGrasp::Held))),
              PitHandIndices(), colours);
    }
    // The colours as a game's texture might give them (made up here: paler skin, a bluer wrap).
    const PitHandColours other = PitHandColoursFrom({0.93f, 0.72f, 0.60f}, {0.16f, 0.18f, 0.36f},
                                                    {0.30f, 0.18f, 0.12f}, {0.86f, 0.66f, 0.22f});
    Write(dir, "relaxed_other_colours", Skin(relaxed), PitHandIndices(), other);

    // The laurel frame: alone (panel widths), and worn on the vambrace's back and inside
    // (arm units, in the bind pose's forearm frame), for a 10 cm panel.
    const LaurelMesh& frame = LaurelFrame();
    Write(dir, "laurel", frame.vertices, frame.indices, colours);
    const float width = 0.10f / kPitHandUnit;
    for (const bool inside : {false, true}) {
        const PitHandSeat seat = PitHandPanelSeat(inside);
        LaurelMesh worn = LaurelFrameWorn(seat.arm_radius / width, seat.arm_half_width / width,
                                          seat.band_offset / width);
        // The panel's x along the forearm toward the hand, its z the seat's normal.
        const float side = inside ? -1.0f : 1.0f;
        for (std::size_t v = 0; v < worn.vertices.size(); v += kPitVertexFloats) {
            float* p = &worn.vertices[v];
            const float x = p[0], y = p[1], z = p[2] - kLaurelBack;
            p[0] = seat.centre[0] + x * width;
            p[1] = seat.centre[1] + side * z * width;
            p[2] = seat.centre[2] - side * y * width;
            const float nx = p[3], ny = p[4], nz = p[5];
            p[3] = nx;
            p[4] = side * nz;
            p[5] = -side * ny;
        }
        Write(dir, inside ? "worn_inside" : "worn_back", worn.vertices, worn.indices, colours);
        std::fprintf(info, "seat %s %g %g %g normal %g %g %g arm_radius %g panel_width_units %g back %g\n",
                     inside ? "inside" : "back", seat.centre[0], seat.centre[1], seat.centre[2], seat.normal[0],
                     seat.normal[1], seat.normal[2], seat.arm_radius, width, kLaurelBack);
    }
    for (const bool left : {true, false}) {
        const PitMat4 fit = PitHandControllerFit(left);
        std::fprintf(info, "controller_fit %s", left ? "left" : "right");
        for (const float f : fit) {
            std::fprintf(info, " %.9g", f);
        }
        std::fprintf(info, "\n");
    }
    const PitHandHandle held = PitHandHeld(), fist = PitHandFist();
    std::fprintf(info, "held %g %g %g axis %g %g %g radius %g\n", held.centre[0], held.centre[1], held.centre[2],
                 held.axis[0], held.axis[1], held.axis[2], held.radius);
    std::fprintf(info, "fist %g %g %g axis %g %g %g radius %g\n", fist.centre[0], fist.centre[1], fist.centre[2],
                 fist.axis[0], fist.axis[1], fist.axis[2], fist.radius);
    std::fprintf(info, "vertices %u triangles %zu\n", PitHandVertexCount(), PitHandIndices().size() / 3);
    for (const PitHandPart& part : PitHandParts()) {
        std::fprintf(info, "part %.*s %u\n", static_cast<int>(part.name.size()), part.name.data(), part.index_count / 3);
    }

    // The cost of a pose: evaluate and skin, the best of many runs.
    std::vector<float> out(static_cast<std::size_t>(PitHandVertexCount()) * kPitVertexFloats);
    double best_skin = 1e9, best_eval = 1e9;
    PitHandInputs in;
    for (int run = 0; run < 2000; run++) {
        in.trigger = static_cast<float>(run % 100) / 100.0f;
        in.grip = 1.0f - in.trigger;
        const auto t0 = std::chrono::steady_clock::now();
        const PitHandSkeleton s = PitHandEvaluate(PitHandPoseFromInputs(in, PitHandGrasp::Held));
        const auto t1 = std::chrono::steady_clock::now();
        PitHandSkin(s, (run & 1) != 0, out);
        const auto t2 = std::chrono::steady_clock::now();
        best_eval = std::min(best_eval, std::chrono::duration<double, std::milli>(t1 - t0).count());
        best_skin = std::min(best_skin, std::chrono::duration<double, std::milli>(t2 - t1).count());
    }
    std::fprintf(info, "pose_ms %.5f skin_ms %.5f\n", best_eval, best_skin);
    std::printf("pose %.5f ms, skin %.5f ms (%u vertices)\n", best_eval, best_skin, PitHandVertexCount());
    std::fclose(info);
    return 0;
}
