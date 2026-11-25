// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include <iterator>
#include <string>

#include <fmt/format.h>

#include "common/frame_test_switches.h"
#include "common/logging/log.h"
#include "frame_xr/input/hand_joints.h"
#include "frame_xr/xr_loader.h"

namespace FrameXr::Input {

static_assert(TrackedJoints::kCount == XR_HAND_JOINT_COUNT_EXT);

bool HandJoints::Wanted() {
    const auto value = Common::FrameTestSwitches::Value(kSwitch);
    return value && *value == "1";
}

void HandJoints::Create(XrInstance instance, XrSystemId system, XrSession session,
                        bool data_source, bool range) {
    Destroy();
    auto& xr = Xr();
    if (!xr.xrCreateHandTrackerEXT || !xr.xrLocateHandJointsEXT || !xr.xrGetSystemProperties) {
        LOG_INFO(Frontend, "xr: finger joints: the runtime has none");
        return;
    }
    XrSystemHandTrackingPropertiesEXT hands{XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
    XrSystemProperties system_properties{XR_TYPE_SYSTEM_PROPERTIES, &hands};
    const XrResult r = xr.xrGetSystemProperties(instance, system, &system_properties);
    if (XR_FAILED(r) || hands.supportsHandTracking != XR_TRUE) {
        LOG_INFO(Frontend, "xr: finger joints: this system has none ({})", ResultName(r));
        return;
    }
    motion_range = range;
    for (std::size_t h = 0; h < 2; h++) {
        const void* next = nullptr;
        std::array<XrHandTrackingDataSourceEXT, 1> sources{
            XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT};
        XrHandTrackingDataSourceInfoEXT source{XR_TYPE_HAND_TRACKING_DATA_SOURCE_INFO_EXT};
        if (data_source) {
            source.requestedDataSourceCount = 1;
            source.requestedDataSources = sources.data();
            next = &source;
        }
        XrHandTrackerCreateInfoEXT ci{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT, next};
        ci.hand = h == 0 ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
        ci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
        const XrResult made = xr.xrCreateHandTrackerEXT(session, &ci, &tracker[h]);
        LOG_INFO(Frontend, "xr: finger joints: {} hand {} ({})", h == 0 ? "left" : "right",
                 ResultName(made), data_source ? "from the controller" : "any source");
        if (XR_FAILED(made)) {
            tracker[h] = XR_NULL_HANDLE;
        }
    }
    source_known = data_source;
}

void HandJoints::Destroy() {
    auto& xr = Xr();
    for (auto& t : tracker) {
        if (t != XR_NULL_HANDLE && xr.xrDestroyHandTrackerEXT) {
            xr.xrDestroyHandTrackerEXT(t);
        }
        t = XR_NULL_HANDLE;
    }
    active_seen = {-1, -1};
    source_known = motion_range = false;
}

// Where the runtime cannot tell the ranges apart it is its own choice.
bool HandJoints::LocateRange(int h, XrSpace base, XrTime time, bool on_controller,
                             std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT>& joints) {
    XrHandJointsMotionRangeInfoEXT range{XR_TYPE_HAND_JOINTS_MOTION_RANGE_INFO_EXT};
    range.handJointsMotionRange = on_controller
                                      ? XR_HAND_JOINTS_MOTION_RANGE_CONFORMING_TO_CONTROLLER_EXT
                                      : XR_HAND_JOINTS_MOTION_RANGE_UNOBSTRUCTED_EXT;
    XrHandJointsLocateInfoEXT info{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT,
                                   motion_range ? &range : nullptr};
    info.baseSpace = base;
    info.time = time;
    XrHandTrackingDataSourceStateEXT source{XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT};
    XrHandJointLocationsEXT at{XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
                               source_known ? &source : nullptr};
    at.jointCount = XR_HAND_JOINT_COUNT_EXT;
    at.jointLocations = joints.data();
    return XR_SUCCEEDED(Xr().xrLocateHandJointsEXT(tracker[static_cast<std::size_t>(h)], &info, &at)) &&
           at.isActive == XR_TRUE && (!source_known || source.isActive == XR_TRUE);
}

void HandJoints::Locate(int h, XrSpace base, XrTime time, TrackedJoints& out) {
    out.active = out.placed = false;
    const auto i = static_cast<std::size_t>(h);
    if (tracker[i] == XR_NULL_HANDLE) {
        return;
    }
    const bool on_controller = Common::FrameTestSwitches::Value(kRangeSwitch).value_or("0") == "1";
    std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> joints{};
    bool ok = LocateRange(h, base, time, on_controller, joints);
    out.placed = ok;
    for (std::size_t j = 0; ok && j < joints.size(); j++) {
        ok = (joints[j].locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
        out.placed = out.placed && (joints[j].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
        const auto& p = joints[j].pose;
        out.joint[j] = {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
                        {p.position.x, p.position.y, p.position.z}};
    }
    out.active = ok;
    if (active_seen[i] != (ok ? 1 : 0)) {
        active_seen[i] = ok ? 1 : 0;
        LOG_INFO(Frontend, "xr: finger joints: {} hand {}", h == 0 ? "left" : "right",
                 ok ? "read" : "not there");
    }
}

// "xr: joints: v1 <time ns> <L|R> <flat|controller> aim <pose> trigger v t grip v t stick x y c pad t
// joints <26 x flags place turn radius>": a pose is its place (metres) then its turn (x y z w), all in
// the base space.
void HandJoints::LogRaw(int h, XrSpace base, XrTime time, const HandInput& held) {
    const auto i = static_cast<std::size_t>(h);
    if (tracker[i] == XR_NULL_HANDLE || raw_lines >= kRawLines ||
        Common::FrameTestSwitches::Value(kRawSwitch).value_or("0") != "1") {
        return;
    }
    const int touches = (held.trigger_touch ? 1 : 0) | (held.trigger > 0.5f ? 2 : 0) |
                        (held.grip_touch ? 4 : 0) | (held.grip > 0.5f ? 8 : 0) |
                        (held.thumb_touch ? 16 : 0) | (held.stick_click ? 32 : 0);
    const XrTime since = time - raw_at;
    if (since < 1000000000 && since >= 0 && (touches == raw_touches || since < 200000000)) {
        return;
    }
    raw_at = time;
    raw_touches = touches;
    for (const bool on_controller : {false, true}) {
        std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> joints{};
        if ((on_controller && !motion_range) || !LocateRange(h, base, time, on_controller, joints)) {
            continue;
        }
        std::string text;
        const auto pose = [&text](float px, float py, float pz, float x, float y, float z, float w) {
            fmt::format_to(std::back_inserter(text), " {:.5f} {:.5f} {:.5f} {:.5f} {:.5f} {:.5f} {:.5f}", px,
                           py, pz, x, y, z, w);
        };
        const Posef& a = held.aim;
        pose(a.position.x, a.position.y, a.position.z, a.orientation.x, a.orientation.y, a.orientation.z,
             a.orientation.w);
        fmt::format_to(std::back_inserter(text),
                       " trigger {:.2f} {} grip {:.2f} {} stick {:.2f} {:.2f} {} pad {} joints", held.trigger,
                       held.trigger_touch ? 1 : 0, held.grip, held.grip_touch ? 1 : 0, held.stick.x,
                       held.stick.y, held.stick_click ? 1 : 0, held.thumb_touch ? 1 : 0);
        for (const XrHandJointLocationEXT& j : joints) {
            const auto& p = j.pose;
            fmt::format_to(std::back_inserter(text), " {}", j.locationFlags);
            pose(p.position.x, p.position.y, p.position.z, p.orientation.x, p.orientation.y, p.orientation.z,
                 p.orientation.w);
            fmt::format_to(std::back_inserter(text), " {:.4f}", j.radius);
        }
        raw_lines++;
        LOG_INFO(Frontend, "xr: joints: v1 {} {} {} aim{}", time, h == 0 ? "L" : "R",
                 !motion_range ? "any" : on_controller ? "controller" : "flat", text);
    }
}

} // namespace FrameXr::Input
