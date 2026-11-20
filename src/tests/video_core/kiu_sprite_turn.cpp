// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiu2d: mode 3's turn for the draws shaded on the CPU (video_core/vr_view_matrix.h
// ClipCorrection, ApplyClipCorrection): Kid Icarus: Uprising's lock-on marks and hit numbers.

#include <cmath>
#include <catch2/catch_test_macros.hpp>
#include "video_core/vr_view_matrix.h"

namespace {

using VideoCore::Immersive::ApplyClipCorrection;
using VideoCore::Immersive::ClipCorrection;

// The European game's world projection in first person as mode 3 reads it (registers 86 to 89,
// the wide camera: 111.5 degrees up and down, 1.336 across, the picture turned 90 degrees for
// the screen; the right eye's shear).
constexpr float kProj[4][4] = {{0.0f, 0.6807f, 0.0f, 0.0f},
                               {-0.5096f, 0.0f, 0.0011f, 0.0f},
                               {0.0f, 0.0f, 1.0f, 10.0f},
                               {0.0f, 0.0f, -1.0f, 0.0f}};

void Rot(double yaw, double pitch, double out[3][3]) {
    const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch),
                 sp = std::sin(pitch);
    // R = Ry(yaw) * Rx(pitch)
    const double ry[3][3] = {{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}};
    const double rx[3][3] = {{1, 0, 0}, {0, cp, -sp}, {0, sp, cp}};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            out[i][j] = 0;
            for (int k = 0; k < 3; ++k) {
                out[i][j] += ry[i][k] * rx[k][j];
            }
        }
    }
}

// The game's camera rows (a turn, the eye at the origin as this game draws) and mode 3's patch:
// the head's turn on the left plus a move.
void Views(double head_yaw, double head_pitch, float view[3][4], float patched[3][4],
           float move_x = 0.0f) {
    double cam[3][3], head[3][3];
    Rot(0.35, -0.05, cam);
    Rot(head_yaw, head_pitch, head);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            view[i][j] = static_cast<float>(cam[i][j]);
            double sum = 0;
            for (int k = 0; k < 3; ++k) {
                sum += head[i][k] * cam[k][j];
            }
            patched[i][j] = static_cast<float>(sum);
        }
        view[i][3] = i == 0 ? 0.11f : 0.0f;
        patched[i][3] = 0.0f;
    }
    // The patch's move (the eyes' half distance, the lean) on the camera's own.
    double t = 0;
    for (int k = 0; k < 3; ++k) {
        t += head[0][k] * (k == 0 ? 0.11 : 0.0);
    }
    for (int i = 0; i < 3; ++i) {
        double ti = 0;
        for (int k = 0; k < 3; ++k) {
            ti += head[i][k] * (k == 0 ? 0.11 : 0.0);
        }
        patched[i][3] = static_cast<float>(ti) + (i == 0 ? move_x : 0.0f);
    }
}

void Clip(const float view[3][4], const float world[3], float out[4]) {
    float v[4];
    for (int i = 0; i < 3; ++i) {
        v[i] = view[i][0] * world[0] + view[i][1] * world[1] + view[i][2] * world[2] + view[i][3];
    }
    v[3] = 1.0f;
    for (int i = 0; i < 4; ++i) {
        out[i] = kProj[i][0] * v[0] + kProj[i][1] * v[1] + kProj[i][2] * v[2] + kProj[i][3] * v[3];
    }
}

} // namespace

TEST_CASE("kiu2d: no turn leaves a sprite where it is", "[video_core][kiu2d]") {
    float view[3][4], patched[3][4], h[4][4];
    Views(0.0, 0.0, view, patched);
    REQUIRE(ClipCorrection(kProj, view, patched, h));
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            REQUIRE(std::abs(h[i][j] - (i == j ? 1.0f : 0.0f)) < 1e-5f);
        }
    }
    float pos[4] = {3.0f, -2.0f, -30.0f, 40.0f};
    ApplyClipCorrection(h, pos);
    REQUIRE(std::abs(pos[0] - 3.0f) < 1e-4f);
    REQUIRE(std::abs(pos[1] + 2.0f) < 1e-4f);
    REQUIRE(std::abs(pos[3] - 40.0f) < 1e-4f);
}

TEST_CASE("kiu2d: a sprite turns with the world it marks", "[video_core][kiu2d]") {
    const double poses[][2] = {{0.35, 0.0}, {-0.35, 0.0}, {0.0, 0.26}, {0.0, -0.26},
                               {-0.44, 0.17}, {0.6, -0.4}};
    const float points[][3] = {{0.0f, 0.0f, -50.0f}, {12.0f, 5.0f, -45.0f}, {-20.0f, -8.0f, -60.0f},
                               {3.0f, 30.0f, -400.0f}, {-90.0f, 2.0f, -150.0f}};
    for (const auto& pose : poses) {
        float view[3][4], patched[3][4], h[4][4];
        Views(pose[0], pose[1], view, patched);
        REQUIRE(ClipCorrection(kProj, view, patched, h));
        for (const auto& p : points) {
            float game[4], want[4];
            Clip(view, p, game);
            Clip(patched, p, want);
            // The world draw puts the point at `want`; the sprite was drawn at `game`.
            ApplyClipCorrection(h, game);
            if (want[3] <= 0.0f) {
                continue; // behind the turned eye
            }
            // On the picture: the same place.
            const float tol = 1e-4f;
            REQUIRE(std::abs(game[0] / game[3] - want[0] / want[3]) < tol);
            REQUIRE(std::abs(game[1] / game[3] - want[1] / want[3]) < tol);
        }
    }
}

TEST_CASE("kiu2d: a sprite moves with the lean and each eye's offset", "[video_core][kiu2d]") {
    // The patch's move (the head's lean, an eye's half distance) moves a sprite as it moves its
    // point: near points most.
    const float points[][3] = {{0.0f, 0.0f, -12.0f}, {4.0f, -3.0f, -20.0f}, {-6.0f, 2.0f, -60.0f}};
    for (const float move : {2.5f, -0.25f}) {
        float view[3][4], patched[3][4], h[4][4];
        Views(0.2, -0.1, view, patched, move);
        REQUIRE(ClipCorrection(kProj, view, patched, h));
        for (const auto& p : points) {
            float game[4], want[4];
            Clip(view, p, game);
            Clip(patched, p, want);
            ApplyClipCorrection(h, game);
            REQUIRE(std::abs(game[0] / game[3] - want[0] / want[3]) < 1e-4f);
            REQUIRE(std::abs(game[1] / game[3] - want[1] / want[3]) < 1e-4f);
        }
    }
}

TEST_CASE("kiu2d: a projection that cannot be inverted gives no turn", "[video_core][kiu2d]") {
    float view[3][4], patched[3][4], h[4][4];
    Views(0.2, 0.0, view, patched);
    const float flat[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 1}};
    REQUIRE_FALSE(ClipCorrection(flat, view, patched, h));
}
