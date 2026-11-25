// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiu140: each hand's finger joints as the runtime has them on the controllers
// (XR_EXT_hand_tracking, from the controller where the runtime says so), for Pit's arm. Without
// the extension nothing is read and the arm follows the controller's touches and pulls.

#pragma once

#include <array>
#include <openxr/openxr.h>

#include "frame_xr/input/input_frame.h"

namespace FrameXr::Input {

class HandJoints {
public:
    /// The instance extensions wanted (each when the runtime offers it).
    static constexpr std::array<const char*, 3> kExtensions{
        XR_EXT_HAND_TRACKING_EXTENSION_NAME, XR_EXT_HAND_TRACKING_DATA_SOURCE_EXTENSION_NAME,
        XR_EXT_HAND_JOINTS_MOTION_RANGE_EXTENSION_NAME};
    /// Test switch (read when the app starts): "1" turns finger joints on.
    static constexpr const char* kSwitch = "AZAHARFRAME_KIU_FINGERS";
    static bool Wanted();
    /// Test switch (live): which of a hand's two ranges is read where the runtime has both: "0"
    /// (default) the hand's own (a flat hand to a fist: what SteamVR gives a hand drawn without a
    /// controller), "1" round the controller in it.
    static constexpr const char* kRangeSwitch = "AZAHARFRAME_KIU_FINGERS_RANGE";
    /// Test switch (live): "1" logs the joints as the runtime has them (LogRaw).
    static constexpr const char* kRawSwitch = "AZAHARFRAME_KIU_FINGERS_RAW";

    /// `data_source`, `motion_range`: those extensions are on.
    void Create(XrInstance instance, XrSystemId system, XrSession session, bool data_source,
                bool motion_range);
    void Destroy();
    /// Hand `h`'s joints in `base` at `time` into `out`; not active without all of them.
    void Locate(int h, XrSpace base, XrTime time, TrackedJoints& out);
    /// With kRawSwitch on: hand `h`'s joints, both ranges, one line each, once a second and when
    /// what the hand touches changes (`held`), kRawLines at most: to replay them elsewhere.
    void LogRaw(int h, XrSpace base, XrTime time, const HandInput& held);
    static constexpr int kRawLines = 1500;

private:
    bool LocateRange(int h, XrSpace base, XrTime time, bool on_controller,
                     std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT>& joints);
    int raw_lines = 0;
    XrTime raw_at = 0;
    int raw_touches = -1;
    std::array<XrHandTrackerEXT, 2> tracker{};
    std::array<int, 2> active_seen{-1, -1}; ///< logged when it changes
    bool source_known = false;              ///< the trackers say where their data is from
    bool motion_range = false;
};

} // namespace FrameXr::Input
