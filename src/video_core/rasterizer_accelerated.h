// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include "video_core/rasterizer_interface.h"
#include "video_core/shader/generator/pica_fs_config.h"
#include "video_core/shader/generator/shader_uniforms.h"
#include "common/logging/log.h"     // AzaharFrame hudmove
#include "video_core/vr_hud_spacing.h" // AzaharFrame hudmove

namespace Memory {
class MemorySystem;
}

namespace Pica {
class PicaCore;
}

namespace VideoCore {

class RasterizerAccelerated : public RasterizerInterface {
public:
    explicit RasterizerAccelerated(Memory::MemorySystem& memory, Pica::PicaCore& pica);
    virtual ~RasterizerAccelerated() = default;

    void AddTriangle(const Pica::OutputVertex& v0, const Pica::OutputVertex& v1,
                     const Pica::OutputVertex& v2) override;

#ifdef ENABLE_FREECAM
    void SetFreeCam(const std::array<f32, 16>& matrix, bool skip_2d) override;
#endif

protected:
    /// Sync vertex and framgent uniforms from PICA registers
    void SyncDrawUniforms();
    /// AzaharFrame hudmove: HUD Spacing's scales for this draw into the vertex uniforms
    /// (vr_hud_spacing.h ForDraw: none in the bottom screen's viewport).
    void SyncHudSpacing();

    void SetVRData(const int32_t  &vrImmersiveMode, const float& immersiveModeFactor, int uoffset, const float& gamePosScaler, const float inv_view[16]) override;
    void SetVRFactorY(float factor_y) override; // AzaharFrame immersive
    void SetVRIpd(float ipd_m) override;         // AzaharFrame ipd
    /// AzaharFrame hudmove: HUD Spacing's scales for the next draws (VrFactor); one that is not
    /// usable is taken as 1 (vr_hud_spacing.h UsableScale). Each draw takes them into the vertex
    /// uniforms (SyncHudSpacing). Logged when the drawing starts or stops using them (emulation
    /// thread), not at each step of a change.
    void SetVRHudSpacing(float across, float up_down, bool far_plane) override {
        HudSpacing::Scales s{HudSpacing::UsableScale(across), HudSpacing::UsableScale(up_down),
                             far_plane};
        if (s.IsIdentity()) {
            s.far_plane = false;
        }
        if (s.IsIdentity() != hud_spacing.IsIdentity()) {
            LOG_INFO(Render,
                     "immersive: HUD Spacing {} (2D scales {:.2f} across, {:.2f} up and down{})",
                     s.IsIdentity() ? "off" : "drawing", s.across, s.up_down,
                     s.far_plane ? ", and on the far plane" : "");
        }
        hud_spacing = s;
    }

protected:
    /// Structure that the hardware rendered vertices are composed of
    struct HardwareVertex {
        HardwareVertex() = default;
        HardwareVertex(const Pica::OutputVertex& v, bool flip_quaternion);

        Common::Vec4f position;
        Common::Vec4f color;
        Common::Vec2f tex_coord0;
        Common::Vec2f tex_coord1;
        Common::Vec2f tex_coord2;
        float tex_coord0_w;
        Common::Vec4f normquat;
        Common::Vec3f view;
    };

    struct VertexArrayInfo {
        u32 vs_input_index_min;
        u32 vs_input_index_max;
        u32 vs_input_size;

        bool Invalid() const {
            return vs_input_index_min == 0 && vs_input_index_max == 0 && vs_input_size == 0;
        }
    };

    /// Retrieve the range and the size of the input vertex
    VertexArrayInfo AnalyzeVertexArray(bool is_indexed, u32 stride_alignment = 1);

protected:
    Memory::MemorySystem& memory;
    Pica::PicaCore& pica;
    Pica::RegsInternal& regs;
    std::vector<HardwareVertex> vertex_batch;
    Pica::Shader::UserConfig user_config{};
    Pica::Shader::Generator::VSUniformData vs_data{};
    Pica::Shader::Generator::FSUniformData fs_data{};
    bool vs_data_dirty = true;
    bool fs_data_dirty = true;

    // VR data injected into PICA vertex shader uniforms for immersive modes.
    u32 vr_uoffset = 0;
    u32 vr_immersive_mode = 0;
    float vr_game_pos_scaler = 0.f;
    float vr_ipd = 0.065f;        ///< AzaharFrame ipd: the headset's IPD (SetVRIpd)
    float vr_ipd_logged = 0.0f;   ///< the separation last logged (0: none yet)
    HudSpacing::Scales hud_spacing{}; ///< AzaharFrame hudmove: the scales set (SetVRHudSpacing)
    float vr_inv_view[16] = {};
    /// AzaharFrame oot: the uniforms uploaded last are the game's world camera, patched
    /// (each draw with them is noted: vr_immersive.h OnWorldDraw), and its projection (trace).
    bool vr_world_uniforms = false;
    float vr_world_tan_half_y = 0.0f;
    float vr_world_aspect = 0.0f;
    bool vr_world_far_valid = false; ///< AzaharFrame stereo: vr_world_far_ndc read
    float vr_world_far_ndc = 0.0f;   ///< AzaharFrame stereo: FarAcrossFromRows
    float vr_world_near_k = 0.0f;    ///< AzaharFrame dbl: NearTermFromRows (NaN: not read)
    float vr_world_across = 0.0f;    ///< AzaharFrame dbl: its horizontal scale
    float vr_world_p00 = 0.0f;       ///< AzaharFrame natural: the projection's scale across
    float vr_world_eye[3][4]{};      ///< AzaharFrame natural: the eye's camera before the patch
    /// AzaharFrame kiu2d: the clip-space turn (ClipCorrection) of the last patched world draw
    /// into each colour buffer (each eye's); the CPU-shaded draws into a buffer get its turn.
    struct SoftClip {
        float h[4][4]{};
        u32 buffer = 0;
        u32 serial = 0;
        bool valid = false;
    };
    std::array<SoftClip, 4> vr_soft_clip{};
    u32 vr_soft_clip_serial = 0;

    struct HeuristicResult {
        int32_t view_matrixregister = -1;
        int32_t eye_indicator_register = -1;
        int32_t eye_indicator_reg_index = -1;
        bool search_started = false; ///< the register search below began with this rasterizer
    } vr_heuristic;

public:
    void ApplyVRDataToPicaVSUniforms(Pica::Shader::Generator::VSPicaUniformData& vs_uniforms);
    /// AzaharFrame kiu2d: turns the CPU-shaded vertices about to be drawn (vertex_batch) with
    /// mode 3's world (vr_soft_clip), when g_soft_draw_turn is on and they go to the buffer the
    /// world went to. The backends call it before drawing them.
    void TurnSoftwareVertices();
    /// AzaharFrame kiu131: whether this draw is the reticle Reticle Size scales (kiu_view_tap.h):
    /// such a draw is not accelerated, so its vertices can be scaled on the CPU.
    bool ReticleDraw() const;
};

} // namespace VideoCore
