// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/profile_input.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <tuple>

#include "common/settings.h"
#include "core/frontend/input.h"

namespace FrameCores {
namespace {

using namespace Common::AltCore;
namespace NB = Settings::NativeButton;

struct Binding {
    NB::Values button;
    ButtonBit bit;
};

constexpr std::array kBindings{
    Binding{NB::A, BtnA},         Binding{NB::B, BtnB},       Binding{NB::X, BtnX},
    Binding{NB::Y, BtnY},         Binding{NB::Up, BtnUp},     Binding{NB::Down, BtnDown},
    Binding{NB::Left, BtnLeft},   Binding{NB::Right, BtnRight}, Binding{NB::L, BtnL},
    Binding{NB::R, BtnR},         Binding{NB::Start, BtnStart},
    Binding{NB::Select, BtnSelect},
};

/// The Circle Pad is the D-pad too, past this much of its way.
constexpr float kCirclePadThreshold = 0.35f;

uint16_t TouchPixel(float fraction, uint32_t size) {
    const int pixel = static_cast<int>(fraction * static_cast<float>(size));
    return static_cast<uint16_t>(std::clamp(pixel, 0, static_cast<int>(size) - 1));
}

} // namespace

struct ProfileInput::Impl {
    struct Devices {
        std::array<std::unique_ptr<Input::ButtonDevice>, kBindings.size()> buttons;
        std::unique_ptr<Input::AnalogDevice> circle_pad;
        // In the order the 3DS asks them.
        std::array<std::unique_ptr<Input::TouchDevice>, 3> touch;
    };
    mutable std::mutex mutex;
    Devices devices;
};

ProfileInput::ProfileInput() : impl(std::make_unique<Impl>()) {
    Reload();
}

ProfileInput::~ProfileInput() = default;

void ProfileInput::Reload() {
    const auto& profile = Settings::values.current_input_profile;
    Impl::Devices made;
    for (std::size_t i = 0; i < kBindings.size(); i++) {
        made.buttons[i] =
            Input::CreateDevice<Input::ButtonDevice>(profile.buttons[kBindings[i].button]);
    }
    made.circle_pad = Input::CreateDevice<Input::AnalogDevice>(
        profile.analogs[Settings::NativeAnalog::CirclePad]);
    made.touch[0] = Input::CreateDevice<Input::TouchDevice>(profile.touch_device);
    if (profile.use_touch_from_button) {
        made.touch[1] = Input::CreateDevice<Input::TouchDevice>("engine:touch_from_button");
    }
    if (profile.use_touchpad && !profile.controller_touch_device.empty()) {
        made.touch[2] = Input::CreateDevice<Input::TouchDevice>(profile.controller_touch_device);
    }
    std::scoped_lock lock{impl->mutex};
    impl->devices = std::move(made);
}

InputState ProfileInput::Read(uint32_t touch_width, uint32_t touch_height) const {
    std::scoped_lock lock{impl->mutex};
    const auto& d = impl->devices;
    InputState state{};
    for (std::size_t i = 0; i < kBindings.size(); i++) {
        if (d.buttons[i]->GetStatus()) {
            state.buttons |= kBindings[i].bit;
        }
    }
    const auto [x, y] = d.circle_pad->GetStatus();
    if (x > kCirclePadThreshold) {
        state.buttons |= BtnRight;
    } else if (x < -kCirclePadThreshold) {
        state.buttons |= BtnLeft;
    }
    if (y > kCirclePadThreshold) {
        state.buttons |= BtnUp;
    } else if (y < -kCirclePadThreshold) {
        state.buttons |= BtnDown;
    }
    for (const auto& device : d.touch) {
        if (!device) {
            continue;
        }
        const auto [touch_x, touch_y, pressed] = device->GetStatus();
        if (pressed) {
            state.touching = true;
            state.touch_x = TouchPixel(touch_x, touch_width);
            state.touch_y = TouchPixel(touch_y, touch_height);
            break;
        }
    }
    return state;
}

} // namespace FrameCores
