// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <string_view>
#include "common/frame_flat.h" // AzaharFrame kiumode: frame_flat_mono
#include "common/frame_test_switches.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "frame_xr/immersive_layout.h" // AzaharFrame kiufov
#include "frame_xr/input/kiu_hooks.h"
#include "frame_xr/input/kiu_turn.h"
#include "frame_xr/kiu_first_person.h"

#include "frame_xr/menu/angels_aim_tab.h" // AzaharFrame kiu132: the C-Stick mods' toasts
#include "frame_xr/toast_fit.h"            // AzaharFrame kiu132: ToastTime

namespace FrameXr {
uint64_t GameTitleId(); // frame_xr.h (AzaharFrame kiumode: the running game)
bool IsStarted();       // frame_xr.h (AzaharFrame kiumode: an XR runtime)
void ShowToastText(const std::string& text, ToastTime time); // frame_xr.h (AzaharFrame kiu132)
} // namespace FrameXr

// AzaharFrame kiuvr (kiu_first_person.h)
namespace FrameXr::Kiu::FirstPerson {

namespace {

constexpr const char* kSwitch = "AZAHARFRAME_KIU_FIRST_PERSON";
constexpr const char* kSwitchScale = "AZAHARFRAME_KIU_WORLD_SCALE";
constexpr const char* kSwitchEyeM = "AZAHARFRAME_KIU_PLAYER_EYE_M";
constexpr const char* kSwitchAimNoMod = "AZAHARFRAME_KIU_AIM_NO_MOD";
constexpr const char* kSwitchAim6Dof = "AZAHARFRAME_KIU_AIM_6DOF";
constexpr const char* kSwitchShots6Dof = "AZAHARFRAME_KIU_SHOTS_6DOF";
constexpr const char* kSwitchFactor = "AZAHARFRAME_KIU_FP_FACTOR"; // AzaharFrame kiufov
constexpr const char* kSwitchWide = "AZAHARFRAME_KIU_FP_WIDE";     // AzaharFrame kiuwide
// AzaharFrame kiuview
constexpr const char* kSwitchField = "AZAHARFRAME_KIU_FP_FIELD";
constexpr const char* kSwitchEdge = "AZAHARFRAME_KIU_FP_EDGE";
constexpr const char* kSwitchTalk = "AZAHARFRAME_KIU_FP_TALK";
constexpr const char* kSwitchTalkAt = "AZAHARFRAME_KIU_FP_TALK_AT";
/// The aim object's world position (it is a node of the scene: the game puts it on the scene
/// camera's ray through the reticle each frame).
constexpr uint32_t kAimWorldPos = 0x54;
/// AzaharFrame kiuwide: how near (a share) the game's aim, as measured, must be to the shape known
/// from the projection for that one to be taken (AimThroughCamera).
constexpr float kKnownShapeShare = 0.03f;

bool SwitchOn(const char* name) {
    const auto v = Common::FrameTestSwitches::Value(name);
    return v && *v == "1";
}
constexpr int64_t kStaleNs = 250'000'000;
/// AzaharFrame kiuwide: the scene camera's angle and shape are kept this long, so a stalled frame
/// (a slow VBlank, a picture dump) doesn't send the screen back to the live angle for a moment.
constexpr int64_t kSceneStaleNs = 2'000'000'000;

std::mutex g_mutex;
Sample g_sample; ///< under g_mutex
float g_scene_fov_deg = 0.0f; ///< under g_mutex (AzaharFrame kiufov)
float g_scene_aspect = 0.0f;
int64_t g_scene_fov_ns = 0;
// AzaharFrame kiuwide, under g_mutex.
float g_wide_fov_deg = 0.0f, g_wide_field = 0.0f;
int64_t g_wide_ns = 0;
float g_drawn_shape = 0.0f;
int64_t g_drawn_ns = 0;
std::atomic<float> g_game_aspect{0.0f};

int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// AzaharFrame kiumode: the title whose code the hooks found to be the mapped one (0: none).
std::atomic<uint64_t> g_code_title{0};
// AzaharFrame kiu132: this run's C-Stick mod (SetCStickMod), and the toasts shown for it.
std::atomic<bool> g_cstick_loaded{false};
std::atomic<bool> g_cstick_left_out{false};
std::atomic<bool> g_cstick_left_out_said{false};
std::atomic<bool> g_cstick_block_said{false};
std::atomic<bool> g_registers{false};
// What the renderer is given for this run (constants: they outlive every reader).
const int32_t kViewRegisterValue = kViewRegister;
const std::string kEyeIndicatorValue = kEyeIndicator;

uint32_t StoredMode() {
    return Settings::values.vr_immersive_mode.GetValue();
}

/// The game is on view in VR: an XR runtime with a session (in Flat the window shows one eye).
bool InVr() {
    return IsStarted() && !Settings::frame_flat_mono.load(std::memory_order_relaxed);
}

/// Aiming is Direct Aim (vr_gyro_aim 2, Light Gun).
bool DirectAim() {
    return Settings::values.vr_gyro_aim.GetValue() == 2;
}

} // namespace

bool AngelsAim() {
    const uint64_t title = GameTitleId();
    // AzaharFrame kiu132: never with a C-Stick mod's code loaded (CStickModBlocksFor).
    return AngelsAimFor(StoredMode(), title,
                        title != 0 && g_code_title.load(std::memory_order_relaxed) == title,
                        InVr()) &&
           !g_cstick_loaded.load(std::memory_order_relaxed);
}

void SetCStickMod(bool loaded, bool left_out) {
    g_cstick_loaded.store(loaded, std::memory_order_relaxed);
    g_cstick_left_out.store(left_out, std::memory_order_relaxed);
    g_cstick_left_out_said.store(false, std::memory_order_relaxed);
    g_cstick_block_said.store(false, std::memory_order_relaxed);
}

bool CStickModLoaded() {
    return g_cstick_loaded.load(std::memory_order_relaxed);
}

void NoteCStickMod() {
    if (g_cstick_left_out.load(std::memory_order_relaxed) &&
        !g_cstick_left_out_said.exchange(true, std::memory_order_relaxed)) {
        LOG_INFO(Frontend, "xr: Angel's Aim: the C-Stick mod is not loaded in this run (toast: "
                           "\"{}\")",
                 Menu::kCStickModLeftOutToast);
        ShowToastText(Menu::kCStickModLeftOutToast, ToastTime::Long);
    }
    const bool blocks = CStickModBlocksFor(g_cstick_loaded.load(std::memory_order_relaxed),
                                           StoredMode(), GameTitleId(), InVr());
    if (blocks != g_cstick_block_said.exchange(blocks, std::memory_order_relaxed) && blocks) {
        LOG_INFO(Frontend, "xr: Angel's Aim: off: a C-Stick mod's code is loaded in this run; "
                           "Immersive Mode plays as Other Games until the game restarts (toast: "
                           "\"{}\")",
                 Menu::kCStickModRestartToast);
        ShowToastText(Menu::kCStickModRestartToast, ToastTime::Long);
    }
}

bool On() {
    return SwitchOn(kSwitch) || AngelsAim();
}

bool ShoulderOn() {
    if (!On() || Common::FrameTestSwitches::Value("AZAHARFRAME_KIU_EYE_OFFSET") ||
        GameCameraOn()) { // AzaharFrame kiu133: the Game's Camera wins
        return false;
    }
    const auto text = Common::FrameTestSwitches::Value("AZAHARFRAME_KIU_SHOULDER");
    float back = 0.0f, up = 0.0f, side = 0.0f;
    return ShoulderFor(text ? text->c_str() : nullptr,
                       Settings::values.vr_kiu_over_shoulder.GetValue(), back, up, side);
}

bool GameCameraOn() {
    if (!On() || Common::FrameTestSwitches::Value("AZAHARFRAME_KIU_EYE_OFFSET")) {
        return false;
    }
    const auto text = Common::FrameTestSwitches::Value("AZAHARFRAME_KIU_GAME_CAMERA");
    return GameCameraFor(text ? text->c_str() : nullptr,
                         Settings::values.vr_kiu_game_camera.GetValue());
}

bool OutsideViewOn() {
    return GameCameraOn() || ShoulderOn();
}

bool Wanted() {
    return WantedFor(SwitchOn(kSwitch), StoredMode(), GameTitleId());
}

void SetGameCode(uint64_t title, bool code_ok) {
    g_code_title.store(code_ok ? title : 0, std::memory_order_relaxed);
}

bool CodeMapped() {
    const uint64_t title = GameTitleId();
    return title != 0 && g_code_title.load(std::memory_order_relaxed) == title;
}

void SupplyRegisters(bool on) {
    if (g_registers.exchange(on, std::memory_order_relaxed) == on) {
        return;
    }
    auto& v = Settings::values;
    v.vr_si_mode_register_offset.Supply(on ? &kViewRegisterValue : nullptr);
    v.vr_immersive_eye_indicator.Supply(on ? &kEyeIndicatorValue : nullptr);
    if (on) {
        LOG_INFO(Frontend,
                 "xr: Kid Icarus first person: View Matrix Register {} and Eye Indicator "
                 "Register {} for this run (in memory only, never saved)",
                 kViewRegister, kEyeIndicator);
    } else {
        LOG_INFO(Frontend, "xr: Kid Icarus first person: the registers are the settings' again");
    }
}

bool AimNoModWanted() {
    return SwitchOn(kSwitchAimNoMod) || SwitchOn(kSwitchAim6Dof) || (AngelsAim() && DirectAim());
}

bool Aim6DofOn() {
    return (SwitchOn(kSwitchAim6Dof) && On()) || (AngelsAim() && DirectAim());
}

bool Shots6DofOn() {
    return ShotsFrom6DofFor(SwitchOn(kSwitchShots6Dof), On(), AngelsAim(), DirectAim());
}

Scene ReadScene(const Input::KiuHooks::Reader& mem) {
    namespace KH = Input::KiuHooks;
    namespace KR = Input::KiuRoom;
    Scene scene;
    KH::Vec3 eye, look_at, up;
    uint32_t fov = 0, aspect = 0;
    if (!KH::ReadVec3(mem, KH::kSceneEye, eye) || !KH::ReadVec3(mem, KH::kSceneLookAt, look_at) ||
        !KH::ReadVec3(mem, KH::kSceneUp, up) || !(KR::Length(look_at - eye) > 1e-3f) ||
        !(KR::Length(up) > 1e-3f) || !mem.Read32(KH::kSceneFovY, fov) ||
        !mem.Read32(KH::kSceneAspect, aspect)) {
        return scene;
    }
    scene.camera = KR::CameraFromLookAt(eye, look_at, up);
    scene.projection = {std::bit_cast<float>(fov), std::bit_cast<float>(aspect)};
    scene.ok = scene.projection.Ok();
    return scene;
}

WorldRay HandRay(const Sample& sample, const Scene& scene) {
    namespace KR = Input::KiuRoom;
    WorldRay ray;
    const View& view = sample.view;
    if (!scene.ok || !view.valid || !sample.hand.valid || !(view.units_per_metre > 0.0f)) {
        return ray;
    }
    const KR::Pose in_rest = KR::RoomToRest(view.rest, {sample.hand.origin, {}});
    ray.from_eye = KR::RestToEyeOffset(scene.camera, view.units_per_metre, in_rest.p);
    ray.origin = scene.camera.eye + ray.from_eye;
    ray.direction = KR::Unit(KR::RestToWorldDirection(
        scene.camera, Input::KiuHooks::Rotate(Input::KiuHooks::Conj(view.rest.orientation),
                                              sample.hand.direction)));
    ray.ok = Input::KiuHooks::Finite(ray.origin) && Input::KiuHooks::Finite(ray.direction);
    return ray;
}

AimNow AimThroughCamera(const Input::KiuHooks::Reader& mem, uint32_t aim,
                        const Input::KiuRoom::ReticleScreen& screen, const Sample& sample,
                        bool feeds_shape) {
    namespace KH = Input::KiuHooks;
    namespace KR = Input::KiuRoom;
    AimNow now;
    const Scene scene = ReadScene(mem);
    const WorldRay ray = HandRay(sample, scene);
    if (!ray.ok || !screen.Ok() || aim == 0 ||
        !KH::ReadVec3(mem, aim + kAimWorldPos, now.game_point)) {
        return now;
    }
    // The shape the game aims with, from its own aim: the reticle's pixel and its point.
    static KR::AimShape shape; // emulation thread only
    static uint32_t shape_aim = 0;
    if (feeds_shape && aim != shape_aim) {
        shape_aim = aim;
        shape.Reset();
    }
    uint32_t px = 0, py = 0;
    if (feeds_shape && mem.Read32(aim + 0xA4, px) && mem.Read32(aim + 0xA8, py)) {
        shape.Update(scene.camera, scene.projection.fov_y, screen,
                     {std::bit_cast<float>(px), std::bit_cast<float>(py)}, now.game_point);
    }
    // AzaharFrame kiuwide: a new field (the camera widened, or back) is a new shape: measured
    // afresh, from the shape the screen solve takes as drawn.
    static float shape_field = 0.0f;
    if (feeds_shape && std::abs(scene.projection.aspect - shape_field) > 0.001f * shape_field) {
        shape_field = scene.projection.aspect;
        shape.Reset();
    }
    KR::Projection projection = scene.projection;
    const float drawn = DrawnShape();
    const float known = drawn > 0.5f                          ? drawn
                        : sample.view.measured_aspect > 0.5f ? sample.view.measured_aspect
                                                              : scene.projection.aspect;
    projection.aspect = shape.Get(known);
    // AzaharFrame kiuwide: the readings are noisy (the game's point is a frame older than the
    // pixel) and, with the reticle near a wide camera's edge, they feed back through the pixel
    // into the next reading (seen: the shape swinging 1.5 % every few frames, the reticle 4 px).
    // So a measured shape near the one known from the projection gives way to that one: the
    // readings only correct a shape that is plainly not the game's (a widescreen mod's unknown).
    if (std::abs(projection.aspect - known) < kKnownShapeShare * known) {
        projection.aspect = known;
    }
    now.aspect = projection.aspect;
    now.aspect_readings = shape.Count();
    now.depth = KR::CameraDepth(scene.camera, now.game_point);
    now.pixel = KR::AimPixel(scene.camera, projection, screen, ray.origin, ray.direction,
                             now.depth, &now.ahead, &now.point);
    now.valid = std::isfinite(now.pixel.x) && std::isfinite(now.pixel.y);
    return now;
}

bool ForTitle(uint64_t title) {
    return Input::KiuTurn::IsMappedTitle(title) && On(); // AzaharFrame kiuusa
}

float UnitsPerMetre() {
    namespace KR = Input::KiuRoom;
    if (const auto text = Common::FrameTestSwitches::Value(kSwitchScale)) {
        float v = 0.0f;
        if (Input::KiuHooks::ParseFloats(text->c_str(), &v, 1) == 1 &&
            v >= KR::kUnitsPerMetreMin && v <= KR::kUnitsPerMetreMax) {
            return v;
        }
    }
    float eye_m = KR::kEyeMetresDefault;
    if (const auto text = Common::FrameTestSwitches::Value(kSwitchEyeM)) {
        float v = 0.0f;
        if (Input::KiuHooks::ParseFloats(text->c_str(), &v, 1) == 1 && v > 0.0f) {
            eye_m = v;
        }
    }
    return KR::LifeSizeUnitsPerMetre(KR::kEyeUnitsLand, eye_m);
}

float FactorMax() {
    if (const auto text = Common::FrameTestSwitches::Value(kSwitchFactor)) {
        float v = 0.0f;
        if (Input::KiuHooks::ParseFloats(text->c_str(), &v, 1) == 1 && v >= 1.0f &&
            v <= Immersive::Layout::kKiuFpFactorCeiling) {
            return v;
        }
    }
    return Immersive::Layout::kKiuFpFactorMax;
}

void PublishProjection(const Input::KiuRoom::Projection& projection) {
    if (!projection.Ok()) {
        return;
    }
    std::scoped_lock lock{g_mutex};
    g_scene_fov_deg = projection.fov_y * 180.0f / 3.14159265358979f;
    g_scene_aspect = projection.aspect;
    g_scene_fov_ns = NowNs();
}

bool SceneProjection(float& fov_y_deg, float& aspect) {
    const int64_t now = NowNs();
    std::scoped_lock lock{g_mutex};
    if (g_scene_fov_ns == 0 || now - g_scene_fov_ns > kSceneStaleNs) {
        return false;
    }
    fov_y_deg = g_scene_fov_deg;
    aspect = g_scene_aspect;
    return true;
}

void PublishGameAspect(float aspect) {
    g_game_aspect.store(std::isfinite(aspect) && aspect > 0.0f ? aspect : 0.0f,
                        std::memory_order_relaxed);
}

float GameAspect() {
    return g_game_aspect.load(std::memory_order_relaxed);
}

// AzaharFrame kiuview

Talk TalkNow() {
    if (!On()) {
        Talk off;
        off.on = false;
        return off;
    }
    const auto on = Common::FrameTestSwitches::Value(kSwitchTalk);
    const auto at = Common::FrameTestSwitches::Value(kSwitchTalkAt);
    Talk t = ParseTalk(on ? on->c_str() : nullptr, at ? at->c_str() : nullptr);
    // AzaharFrame kiutoggles: Talk Panel's setting when the switch is not set.
    t.on = TalkOnFor(on ? on->c_str() : nullptr, Settings::values.vr_kiu_talk_panel.GetValue());
    return t;
}


// AzaharFrame kiu131
namespace {
std::atomic<bool> g_cutscene{false};
std::atomic<float> g_cutscene_tan{0.0f};
std::atomic<bool> g_battle{false};
} // namespace

void SetCutscene(bool on, float tan_half_y) {
    g_cutscene.store(on, std::memory_order_relaxed);
    g_cutscene_tan.store(on ? tan_half_y : 0.0f, std::memory_order_relaxed);
}
float CutsceneAngleDeg() {
    const float t = g_cutscene_tan.load(std::memory_order_relaxed);
    if (!std::isfinite(t) || !(t > 0.0f) || CutscenesOnScreen()) {
        return 0.0f;
    }
    return std::clamp(2.0f * std::atan(t) * 57.2957795f, 15.0f, 110.0f);
}
void SetBattle(bool on) {
    g_battle.store(on, std::memory_order_relaxed);
}
bool Battle() {
    return g_battle.load(std::memory_order_relaxed);
}
bool Cutscene() {
    return g_cutscene.load(std::memory_order_relaxed);
}
bool CutscenesOnScreen() {
    const auto text = Common::FrameTestSwitches::Value("AZAHARFRAME_KIU_CUTSCENES");
    return text ? *text != "0" : Settings::values.vr_kiu_cutscenes.GetValue() != 0;
}

bool HudInView() {
    const auto text = Common::FrameTestSwitches::Value("AZAHARFRAME_KIU_HUD");
    return text ? *text != "0" : Settings::values.vr_kiu_hud.GetValue() != 0;
}

BottomNow BottomScreenNow() {
    BottomNow b;
    if (!AngelsAim() && !On()) {
        return b;
    }
    const auto& v = Settings::values;
    const auto wrist = Common::FrameTestSwitches::Value("AZAHARFRAME_KIU_WRIST_SCREEN");
    const auto on = Common::FrameTestSwitches::Value(kSwitchTalk);
    const auto at = Common::FrameTestSwitches::Value(kSwitchTalkAt);
    b.mode = BottomScreenWithSwitches(
        BottomScreenModeFor(v.vr_kiu_bottom_screen.GetValue(), v.vr_kiu_wrist_screen.GetValue(),
                            v.vr_kiu_talk_panel.GetValue()),
        wrist ? wrist->c_str() : nullptr, on ? on->c_str() : nullptr);
    b.scale = BottomScaleFor(v.vr_kiu_bottom_size.GetValue());
    b.alpha = BottomAlphaFor(v.vr_kiu_bottom_opacity.GetValue());
    b.place = ParseTalk(nullptr, at ? at->c_str() : nullptr);
    b.place.on = b.mode == BottomScreen::Persistent;
    b.place.width_deg = std::min(b.place.width_deg * b.scale, 100.0f);
    b.right_deg = std::clamp(v.vr_kiu_bottom_x.GetValue(), -kBottomMaxRightDeg, kBottomMaxRightDeg);
    b.down_deg = std::clamp(v.vr_kiu_bottom_y.GetValue(), -kBottomMaxUpDeg, kBottomMaxDownDeg);
    return b;
}


float FieldShare() {
    if (!On()) {
        return 1.0f;
    }
    const auto text = Common::FrameTestSwitches::Value(kSwitchField);
    return ParseFieldShare(text ? text->c_str() : nullptr);
}


float EdgeShare() {
    if (!On()) {
        return 0.0f;
    }
    const auto text = Common::FrameTestSwitches::Value(kSwitchEdge);
    return ParseEdgeShare(text ? text->c_str() : nullptr);
}


int WideMode() {
    const auto v = Common::FrameTestSwitches::Value(kSwitchWide);
    if (v && *v == "0") {
        return 0;
    }
    if (v && *v == "2") {
        return 2;
    }
    return 1;
}

void PublishWideTarget(float fov_y_deg, float field_aspect) {
    if (!std::isfinite(fov_y_deg) || !std::isfinite(field_aspect) || !(fov_y_deg > 0.0f) ||
        !(field_aspect > 0.0f)) {
        return;
    }
    std::scoped_lock lock{g_mutex};
    g_wide_fov_deg = fov_y_deg;
    g_wide_field = field_aspect;
    g_wide_ns = NowNs();
}

bool WideTarget(float& fov_y_rad, float& field_aspect) {
    if (WideMode() == 0) {
        return false;
    }
    // Kept however old: it follows the view's numbers alone, which do not change in a session, and
    // a display frame that stalls (seen: 0.3 s writing a picture) must not drop the game's camera
    // back to its own for a frame.
    std::scoped_lock lock{g_mutex};
    if (g_wide_ns == 0) {
        return false;
    }
    fov_y_rad = g_wide_fov_deg * 3.14159265358979f / 180.0f;
    field_aspect = g_wide_field;
    return true;
}

void PublishDrawnShape(float aspect) {
    if (!std::isfinite(aspect) || !(aspect > 0.0f)) {
        return;
    }
    std::scoped_lock lock{g_mutex};
    g_drawn_shape = aspect;
    g_drawn_ns = NowNs();
}

float DrawnShape() {
    std::scoped_lock lock{g_mutex};
    return g_drawn_ns != 0 ? g_drawn_shape : 0.0f; // the last one (as WideTarget)
}

void PublishView(const View& view) {
    std::scoped_lock lock{g_mutex};
    g_sample.view = view;
    g_sample.view.host_ns = NowNs();
}

void PublishHand(const Hand& hand) {
    std::scoped_lock lock{g_mutex};
    g_sample.hand = hand;
    g_sample.hand.host_ns = NowNs();
}

namespace {
std::atomic<uint32_t> g_run_count{0};
} // namespace

uint32_t RunCount() {
    return g_run_count.load(std::memory_order_relaxed);
}

void OnGameBoot() {
    g_run_count.fetch_add(1, std::memory_order_relaxed);
    // AzaharFrame kiumode: a new run: its code is checked afresh and nothing is supplied until
    // then (before the presets, which read the registers).
    SetGameCode(0, false);
    SupplyRegisters(false);
    // AzaharFrame kiuwide: the last run's camera and shape are not this one's.
    std::scoped_lock lock{g_mutex};
    g_wide_ns = g_drawn_ns = g_scene_fov_ns = 0;
    g_game_aspect.store(0.0f, std::memory_order_relaxed);
}

Sample Latest(int64_t host_now_ns) {
    Sample s;
    {
        std::scoped_lock lock{g_mutex};
        s = g_sample;
    }
    if (host_now_ns - s.view.host_ns > kStaleNs) {
        s.view.valid = false;
    }
    if (host_now_ns - s.hand.host_ns > kStaleNs) {
        s.hand.valid = false;
    }
    return s;
}

} // namespace FrameXr::Kiu::FirstPerson
