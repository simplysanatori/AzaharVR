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

constexpr u32 kMaxVertices = 10; // of a polygon
constexpr int kFine = 16;        // of a pixel: HiresPosition's

/// Not a degenerate one, which a state load can leave with its vertices unset.
bool IsDrawn(const melonDS::Polygon* polygon) {
    return polygon && !polygon->Degenerate && polygon->NumVertices > 0 &&
           polygon->NumVertices <= kMaxVertices && polygon->VTop < polygon->NumVertices &&
           polygon->VBottom < polygon->NumVertices;
}

} // namespace

NdsRight3D::NdsRight3D(melonDS::GPU3D& game, melonDS::SoftRenderer& parent)
    : melonDS::Renderer3D(game), owner{&parent} {}

NdsRight3D::NdsRight3D(melonDS::GPU3D& game) : melonDS::Renderer3D(game) {}

NdsRight3D::~NdsRight3D() {
    Stop();
}

melonDS::GPU3D& NdsRight3D::Scene() {
    if (!scene) {
        scene = std::make_unique<melonDS::GPU3D>(GPU);
        static_assert(sizeof(scene->VertexRAM) / sizeof(melonDS::Vertex) == kVertices);
    }
    return *scene;
}

void NdsRight3D::Create(bool threaded) {
    // Once, with no polygons: the thread draws a frame as soon as it is made
    soft = std::make_unique<melonDS::SoftRenderer3D>(Scene(), *owner);
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
    convergence = 0;
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
    // BG0's scroll, which changes between lines
    scene->RenderXPos = NdsDepth::ScrollScene(GPU3D.RenderXPos, offset);
    u32* row = nullptr;
    // Also takes the lines that were not drawn, so that this one is finished
    do {
        row = soft->GetLine(line);
        ++taken;
    } while (taken <= line && taken < kLines);
    EndAtEdge(row, line);
    return row;
}

void NdsRight3D::Start(const NdsDepth::Depth& depth, int screen, bool threaded) {
    if (soft && soft->IsThreaded() != threaded) {
        Stop();
    }
    if (!soft) {
        Create(threaded);
    }
    Drop(); // the thread is idle from here on
    Prepare(depth, screen);
    soft->RenderFrame();
    running = threaded;
    taken = 0;
}

NdsDepth::SceneShift NdsRight3D::Prepare(const NdsDepth::Depth& depth, int screen) {
    const melonDS::GPU2D& engine = GPU.GPU2D_A;
    const NdsDepth::SceneView seen = Measure();
    NdsDepth::SceneView view{depth.convergence, 0};
    if (depth.convergence == 0) {
        const bool blank = NdsDepth::IsBlank(GPU.MasterBrightnessA);
        view = tracks[screen].Follow(seen, NdsDepth::MakeSetup(engine.DispCnt, engine.BGCnt[0]),
                                     blank);
        if (view.convergence == 0) {
            view.convergence = NdsDepth::kConvergence;
        }
    }
    convergence = view.convergence;
    const NdsDepth::SceneRoom room =
        NdsDepth::MakeRoom(depth.layers, engine.DispCnt,
                           {engine.BGCnt[0], engine.BGCnt[1], engine.BGCnt[2], engine.BGCnt[3]});
    offset = NdsDepth::SceneOffset(depth.layers);
    far_shift = room.farthest;
    Scene();
    const NdsDepth::SceneShift shift = NdsDepth::Offset(NdsDepth::MakeScene(room, view), offset);
    FillScene(shift);
    return shift;
}

/// What is behind the scene has no more to the left than the left eye has either; it would be in
/// the columns it is moved by.
void NdsRight3D::EndAtEdge(u32* row, int line) const {
    const int end = NdsDepth::SceneEdge(far_shift, NdsDepth::ScrollOf(GPU3D.RenderXPos));
    for (int x = 0; x < end; ++x) {
        if (row[x] == ClearPixel(x + NdsDepth::ScrollOf(scene->RenderXPos), line)) {
            row[x] = 0;
        }
    }
}

/// As SoftRenderer3D's ClearBuffers and CalculateFogDensity have it.
u32 NdsRight3D::ClearPixel(int x, int line) const {
    u32 attr = scene->RenderClearAttr1;
    u32 depth = scene->RenderClearAttr2 & 0x7FFF;
    if ((scene->RenderDispCnt & (1 << 14)) != 0) {
        const u32 at = ((((scene->RenderClearAttr2 >> 24) + line) & 0xFF) << 9) +
                       ((((scene->RenderClearAttr2 >> 16) + x) & 0xFF) << 1);
        const u32 color = GPU.ReadVRAMFlat_Texture<melonDS::u16>(0x40000 + at);
        depth = GPU.ReadVRAMFlat_Texture<melonDS::u16>(0x60000 + at);
        attr = (color & 0x7FFF) | (depth & 0x8000) | ((color & 0x8000) != 0 ? 0x1F0000 : 0);
        depth &= 0x7FFF;
    }
    const u32 pixel = NdsDepth::ClearPixel(attr);
    if ((scene->RenderDispCnt & (1 << 7)) == 0 || (attr & (1 << 15)) == 0) {
        return pixel;
    }
    const u32 z = depth * 0x200 + 0x1FF;
    u32 id = 0;
    u32 part = 0;
    if (z >= scene->RenderFogOffset) {
        const u32 from = ((z - scene->RenderFogOffset) >> 2) << scene->RenderFogShift;
        id = std::min<u32>(from >> 17, 32);
        part = id < 32 ? from & 0x1FFFF : 0;
    }
    u32 density = (scene->RenderFogDensityTable[id] * (0x20000 - part) +
                   scene->RenderFogDensityTable[id + 1] * part) >>
                  17;
    density = density >= 127 ? 128 : density;
    return NdsDepth::FogPixel(pixel, scene->RenderFogColor, density,
                              (scene->RenderDispCnt & (1 << 6)) != 0);
}

void NdsRight3D::Cut() {
    Drop();
    tracks = {};
}

void NdsRight3D::NoScene(int screen) {
    Drop();
    tracks[screen].Nothing();
}

/// The world's depth line, then the view: the w of every vertex of its polygons, once per polygon
/// it is in.
NdsDepth::SceneView NdsRight3D::Measure() {
    const u32 count = std::min<u32>(GPU3D.RenderNumPolygons, kPolygons);
    line = {};
    for (u32 i = 0; i < count; ++i) {
        const melonDS::Polygon* polygon = GPU3D.RenderPolygonRAM[i];
        if (!IsDrawn(polygon) || polygon->WBuffer) {
            continue;
        }
        u32 nearest = 0;
        u32 farthest = 0;
        for (u32 v = 1; v < polygon->NumVertices; ++v) {
            const s32 w = polygon->Vertices[v]->Position[3];
            nearest = w < polygon->Vertices[nearest]->Position[3] ? v : nearest;
            farthest = w > polygon->Vertices[farthest]->Position[3] ? v : farthest;
        }
        line.Add(polygon->Vertices[nearest]->Position[3], polygon->FinalZ[nearest],
                 polygon->Vertices[farthest]->Position[3], polygon->FinalZ[farthest]);
    }

    NdsDepth::SceneStats stats;
    for (u32 i = 0; i < count; ++i) {
        const melonDS::Polygon* polygon = GPU3D.RenderPolygonRAM[i];
        if (!IsDrawn(polygon) || !Place(*polygon).world) {
            continue;
        }
        for (u32 v = 0; v < polygon->NumVertices; ++v) {
            stats.Add(polygon->Vertices[v]->Position[3]);
        }
    }
    return stats.View();
}

NdsDepth::PolygonPlace NdsRight3D::Place(const melonDS::Polygon& polygon) const {
    std::array<s32, kMaxVertices> w{};
    for (u32 v = 0; v < polygon.NumVertices; ++v) {
        w[v] = polygon.Vertices[v]->Position[3];
    }
    return NdsDepth::PlacePolygon(w.data(), polygon.NumVertices, polygon.FinalZ[0], polygon.WBuffer,
                                  line);
}

void NdsRight3D::FillScene(const NdsDepth::SceneShift& shift) {
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
    scene->RenderClearAttr2 = NdsDepth::ShiftClearImage(game.RenderClearAttr2, shift.farthest);
    // Ours never is: the texture memory may have changed, and only the left eye's renderer knows
    scene->RenderFrameIdentical = false;

    ++frame;
    vertex_count = 0;
    u32 count = 0;
    // In the same order: opaque first, sorted by y, which does not change
    const u32 game_count = std::min<u32>(game.RenderNumPolygons, kPolygons);
    for (u32 i = 0; i < game_count; ++i) {
        const melonDS::Polygon* from = game.RenderPolygonRAM[i];
        if (!IsDrawn(from)) {
            continue;
        }
        melonDS::Polygon& to = scene->PolygonRAM[count];
        if (CopyPolygon(*from, to, shift)) {
            scene->RenderPolygonRAM[count++] = &to;
        }
    }
    scene->RenderNumPolygons = count;
}

bool NdsRight3D::CopyPolygon(const melonDS::Polygon& from, melonDS::Polygon& to,
                             const NdsDepth::SceneShift& shift) {
    const std::size_t count = from.NumVertices;
    const NdsDepth::PolygonPlace place = Place(from);
    const int placed = NdsDepth::VertexShift(shift, place.w);

    to = from;
    for (std::size_t i = 0; i < count; ++i) {
        const auto game_index = static_cast<std::size_t>(from.Vertices[i] - &GPU3D.VertexRAM[0]);
        if (game_index >= kVertices) {
            return false;
        }
        const int32_t w = place.each ? from.Vertices[i]->Position[3] : place.w;
        const int by = place.each ? NdsDepth::VertexShift(shift, w) : placed;
        // HiresPosition's, which only a renderer over 1x reads
        const int fine = NdsDepth::VertexShiftUnits(shift, w, kFine);
        Slot& slot = slots[game_index][place.each ? 0 : 1];
        if (slot.frame != frame || slot.shift != by || slot.fine != fine) {
            if (vertex_count == kVertices) {
                return false;
            }
            slot = {frame, static_cast<u32>(vertex_count++), by, fine};
            melonDS::Vertex& copy = scene->VertexRAM[slot.index];
            copy = *from.Vertices[i];
            copy.FinalPosition[0] = NdsDepth::ShiftVertexX(copy.FinalPosition[0], by);
            copy.HiresPosition[0] =
                std::clamp<s32>(copy.HiresPosition[0] + fine, 0, NdsDepth::kMaxX * kFine);
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
