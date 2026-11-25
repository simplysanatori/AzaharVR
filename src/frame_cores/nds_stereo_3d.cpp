// Adapter for melonDS (melonDS team, GPL-3.0-or-later)
// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/nds_stereo_3d.h"

#include <algorithm>
#include <cstring>
#include <type_traits>

#include "GPU.h"
#include "GPU_Soft.h"

namespace FrameCores {
namespace {

using melonDS::s32;
using melonDS::u32;

static_assert(std::is_trivially_copyable_v<melonDS::Vertex>);
static_assert(std::is_trivially_copyable_v<melonDS::Polygon>);

} // namespace

NdsRight3D::NdsRight3D(melonDS::GPU3D& game, melonDS::SoftRenderer& parent)
    : melonDS::Renderer3D(game), owner{parent} {}

NdsRight3D::~NdsRight3D() {
    Stop();
}

void NdsRight3D::Create(bool threaded) {
    // Once, with no polygons: the thread draws a frame as soon as it is made
    scene = std::make_unique<melonDS::GPU3D>(GPU);
    static_assert(sizeof(scene->VertexRAM) / sizeof(melonDS::Vertex) == kVertices);
    soft = std::make_unique<melonDS::SoftRenderer3D>(*scene, owner);
    soft->Reset(); // not threaded yet; its buffers start undefined
    soft->SetThreaded(threaded);
    running = threaded;
    taken = threaded ? 0 : kLines;
}

void NdsRight3D::Stop() {
    soft.reset(); // joins its thread
    scene.reset();
    running = false;
    taken = kLines;
}

void NdsRight3D::Reset() {
    // Not SoftRenderer3D::Reset: it can wait for a frame that was already waited for
    Stop();
}

void NdsRight3D::Wait() {
    if (running) {
        soft->FinishRendering();
        running = false;
    }
}

void NdsRight3D::Drop() {
    Wait(); // all its lines are there now, so the loop does not block
    for (; taken < kLines; ++taken) {
        soft->GetLine(0);
    }
}

bool NdsRight3D::HasLine() const {
    // The game wrote VCOUNT: the left eye's 3D is gone for this frame as well
    return soft && taken < kLines && !GPU3D.AbortFrame;
}

u32* NdsRight3D::GetLine(int line) {
    if (!HasLine()) {
        return empty.data();
    }
    scene->RenderXPos = GPU3D.RenderXPos; // BG0's scroll, which changes between lines
    u32* row = nullptr;
    // Also takes the lines that were not drawn, so that this one is finished
    do {
        row = soft->GetLine(line);
        ++taken;
    } while (taken <= line && taken < kLines);
    return row;
}

void NdsRight3D::Start(const NdsDepth::Depth3D& depth, bool threaded) {
    if (depth.strength == 0) {
        Stop();
        return;
    }
    if (soft && soft->IsThreaded() != threaded) {
        Stop();
    }
    if (!soft) {
        Create(threaded);
    }
    Drop(); // the thread is idle from here on
    FillScene(depth);
    soft->RenderFrame();
    running = threaded;
    taken = 0;
}

void NdsRight3D::FillScene(const NdsDepth::Depth3D& depth) {
    const melonDS::GPU3D& game = GPU3D;

    scene->RenderDispCnt = game.RenderDispCnt;
    scene->RenderAlphaRef = game.RenderAlphaRef;
    std::memcpy(scene->RenderToonTable, game.RenderToonTable, sizeof(scene->RenderToonTable));
    std::memcpy(scene->RenderEdgeTable, game.RenderEdgeTable, sizeof(scene->RenderEdgeTable));
    scene->RenderFogColor = game.RenderFogColor;
    scene->RenderFogOffset = game.RenderFogOffset;
    scene->RenderFogShift = game.RenderFogShift;
    std::memcpy(scene->RenderFogDensityTable, game.RenderFogDensityTable,
                sizeof(scene->RenderFogDensityTable));
    scene->RenderClearAttr1 = game.RenderClearAttr1;
    scene->RenderClearAttr2 = game.RenderClearAttr2;
    // Ours never is: the texture memory may have changed, and only the left eye's renderer knows
    scene->RenderFrameIdentical = false;

    ++frame;
    vertex_count = 0;
    u32 count = 0;
    // In the same order: opaque first, sorted by y, which does not change
    const u32 game_count = std::min<u32>(game.RenderNumPolygons, kPolygons);
    for (u32 i = 0; i < game_count; ++i) {
        const melonDS::Polygon* from = game.RenderPolygonRAM[i];
        if (!from || from->Degenerate) {
            continue; // not drawn, and a state load can leave its vertices unset
        }
        melonDS::Polygon& to = scene->PolygonRAM[count];
        if (CopyPolygon(*from, to, depth)) {
            scene->RenderPolygonRAM[count++] = &to;
        }
    }
    scene->RenderNumPolygons = count;
}

bool NdsRight3D::CopyPolygon(const melonDS::Polygon& from, melonDS::Polygon& to,
                             const NdsDepth::Depth3D& depth) {
    const std::size_t count = from.NumVertices;
    if (count == 0 || count > kMaxPolygonVertices || from.VTop >= count || from.VBottom >= count) {
        return false;
    }
    std::array<s32, kMaxPolygonVertices> w{};
    for (std::size_t i = 0; i < count; ++i) {
        w[i] = from.Vertices[i]->Position[3];
    }
    const bool moves = !NdsDepth::IsOnScreen(w.data(), count);

    to = from;
    for (std::size_t i = 0; i < count; ++i) {
        const auto game_index = static_cast<std::size_t>(from.Vertices[i] - &GPU3D.VertexRAM[0]);
        if (game_index >= kVertices) {
            return false;
        }
        Slot& slot = slots[game_index][moves ? 1 : 0];
        if (slot.frame != frame) {
            if (vertex_count == kVertices) {
                return false;
            }
            slot = {frame, static_cast<u32>(vertex_count++)};
            melonDS::Vertex& copy = scene->VertexRAM[slot.index];
            copy = *from.Vertices[i];
            if (moves) {
                copy.FinalPosition[0] = NdsDepth::ShiftVertexX(
                    copy.FinalPosition[0], NdsDepth::VertexShift(depth, copy.Position[3]));
            }
        }
        to.Vertices[i] = &scene->VertexRAM[slot.index];
    }

    // The software rasteriser does not read these; y, and so VTop and VBottom, stay. Between
    // vertices at the same bottom y it never matters which one VBottom is.
    to.XTop = to.Vertices[to.VTop]->FinalPosition[0];
    to.XBottom = to.Vertices[to.VBottom]->FinalPosition[0];
    return true;
}

} // namespace FrameCores
