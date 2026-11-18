// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <bit>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include "common/frame_test_switches.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "frame_xr/input/kiu_hooks.h"
#include "frame_xr/input/kiu_turn.h"
#include "frame_xr/kiu_first_person.h"

// AzaharFrame kiuvr (kiu_first_person.h)
namespace FrameXr::Kiu::FirstPerson {

namespace {

constexpr const char* kSwitch = "AZAHARFRAME_KIU_FIRST_PERSON";
constexpr const char* kSwitchScale = "AZAHARFRAME_KIU_WORLD_SCALE";
constexpr const char* kSwitchEyeM = "AZAHARFRAME_KIU_PLAYER_EYE_M";
constexpr const char* kSwitchAimNoMod = "AZAHARFRAME_KIU_AIM_NO_MOD";
constexpr const char* kSwitchAim6Dof = "AZAHARFRAME_KIU_AIM_6DOF";
constexpr const char* kSwitchShots6Dof = "AZAHARFRAME_KIU_SHOTS_6DOF";
/// The aim object's world position (it is a node of the scene: the game puts it on the scene
/// camera's ray through the reticle each frame).
constexpr uint32_t kAimWorldPos = 0x54;

bool SwitchOn(const char* name) {
    const auto v = Common::FrameTestSwitches::Value(name);
    return v && *v == "1";
}
constexpr int64_t kStaleNs = 250'000'000;

std::mutex g_mutex;
Sample g_sample; ///< under g_mutex

int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

bool On() {
    return SwitchOn(kSwitch);
}

bool AimNoModWanted() {
    return SwitchOn(kSwitchAimNoMod) || SwitchOn(kSwitchAim6Dof);
}

bool Aim6DofOn() {
    return SwitchOn(kSwitchAim6Dof) && On();
}

bool Shots6DofOn() {
    return SwitchOn(kSwitchShots6Dof) && On();
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
                        const Input::KiuRoom::ReticleScreen& screen, const Sample& sample) {
    namespace KH = Input::KiuHooks;
    namespace KR = Input::KiuRoom;
    AimNow now;
    const Scene scene = ReadScene(mem);
    const WorldRay ray = HandRay(sample, scene);
    if (!ray.ok || !screen.Ok() || aim == 0 ||
        !KH::ReadVec3(mem, aim + kAimWorldPos, now.game_point)) {
        return now;
    }
    now.depth = KR::CameraDepth(scene.camera, now.game_point);
    now.pixel = KR::AimPixel(scene.camera, scene.projection, screen, ray.origin, ray.direction,
                             now.depth, &now.ahead, &now.point);
    now.valid = std::isfinite(now.pixel.x) && std::isfinite(now.pixel.y);
    return now;
}

bool ForTitle(uint64_t title) {
    return title == Input::KiuTurn::kTitleEur && On();
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

void OnGameBoot() {
    if (!On()) {
        return;
    }
    // For this run, as a preset gives them (frame_xr/presets.cpp), but whatever the game's own
    // values are: the switch asks for exactly this. In memory only; they reach the game's file
    // only if its settings are saved during the run.
    auto& v = Settings::values;
    v.vr_immersive_mode.SetGlobal(false);
    v.vr_immersive_mode.SetValue(3);
    v.vr_si_mode_register_offset.SetGlobal(false);
    v.vr_si_mode_register_offset.SetValue(kViewRegister);
    v.vr_immersive_eye_indicator.SetGlobal(false);
    v.vr_immersive_eye_indicator.SetValue(kEyeIndicator);
    LOG_INFO(Frontend,
             "xr: Kid Icarus first person: this run plays in Immersive Mode 3 with View Matrix "
             "Register {} and Eye Indicator Register {} (the test switch, read as the game boots)",
             kViewRegister, kEyeIndicator);
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
