// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

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

void HandJoints::Locate(int h, XrSpace base, XrTime time, TrackedJoints& out) {
    out.active = false;
    const auto i = static_cast<std::size_t>(h);
    if (tracker[i] == XR_NULL_HANDLE) {
        return;
    }
    // Round the controller where the runtime can say so: the hand is on it.
    XrHandJointsMotionRangeInfoEXT range{XR_TYPE_HAND_JOINTS_MOTION_RANGE_INFO_EXT};
    range.handJointsMotionRange = XR_HAND_JOINTS_MOTION_RANGE_CONFORMING_TO_CONTROLLER_EXT;
    XrHandJointsLocateInfoEXT info{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT,
                                   motion_range ? &range : nullptr};
    info.baseSpace = base;
    info.time = time;
    std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> joints{};
    XrHandTrackingDataSourceStateEXT source{XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT};
    XrHandJointLocationsEXT at{XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
                               source_known ? &source : nullptr};
    at.jointCount = XR_HAND_JOINT_COUNT_EXT;
    at.jointLocations = joints.data();
    bool ok = XR_SUCCEEDED(Xr().xrLocateHandJointsEXT(tracker[i], &info, &at)) &&
              at.isActive == XR_TRUE &&
              (!source_known || source.isActive == XR_TRUE);
    for (std::size_t j = 0; ok && j < joints.size(); j++) {
        ok = (joints[j].locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
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

} // namespace FrameXr::Input
