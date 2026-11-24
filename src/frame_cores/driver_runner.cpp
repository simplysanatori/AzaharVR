// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/driver_runner.h"

#include <filesystem>
#include <system_error>
#include <utility>

#include "common/logging/log.h"

namespace FrameCores {
namespace {

constexpr int kSaveCopies = 3;

Vulkan::AltScreenImage ToImage(const DriverPicture& picture) {
    Vulkan::AltScreenImage image{};
    image.pixels = picture.pixels;
    image.width = picture.width;
    image.height = picture.height;
    image.stride_pixels = picture.stride;
    return image;
}

std::unique_ptr<FrameDriver> MakeDriver(DriverKind kind, const DriverServices& services,
                                        const RomInfo& info, const CoreBootOptions& opts) {
    if (opts.driver.make) {
        if (auto driver = opts.driver.make(services)) {
            return driver;
        }
    }
#ifdef HAVE_POKEDAISY
    if (kind == DriverKind::PokeDaisy) {
        if (auto driver = MakePokeDaisyDriver(services)) {
            return driver;
        }
        LOG_ERROR(Core, "DriverRunner: PokeDaisy did not start for {}", services.rom_path);
    }
#else
    (void)kind;
#endif
    // gba-ds's game.c has no driver yet; its maps are the Plain driver's.
    return MakePlainDriver(services, info);
}

} // namespace

bool BackUpSave(const std::string& save_path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto size = fs::file_size(save_path, ec);
    if (ec == std::errc::no_such_file_or_directory || (!ec && size == 0)) {
        return true; // the cores make an empty file
    }
    const auto copy = [&save_path](int n) {
        return save_path + ".before-edit-" + std::to_string(n);
    };
    const std::string made = save_path + ".before-edit.tmp";
    if (!ec) {
        fs::copy_file(save_path, made, fs::copy_options::overwrite_existing, ec);
    }
    for (int n = kSaveCopies; n > 1 && !ec; --n) {
        if (fs::exists(copy(n - 1), ec)) {
            fs::rename(copy(n - 1), copy(n), ec);
        }
    }
    if (!ec) {
        fs::rename(made, copy(1), ec);
    }
    if (ec) {
        LOG_ERROR(Core, "BackUpSave: {} not copied: {}", save_path, ec.message());
        std::error_code ignored;
        fs::remove(made, ignored);
        return false;
    }
    return true;
}

DriverKind DriverRunner::Choose(const CoreBootOptions& opts, const std::string& rom_path,
                                bool gbads_known) {
    if (opts.driver.make) {
        return opts.driver.kind;
    }
    const auto choice = static_cast<SecondScreen>(opts.second_screen);
    bool pokedaisy_known = false;
#ifdef HAVE_POKEDAISY
    pokedaisy_known = choice != SecondScreen::Off && PokeDaisyKnows(rom_path);
#else
    (void)rom_path;
#endif
    return ChooseDriver(choice, pokedaisy_known, gbads_known);
}

void DriverRunner::Start(DriverKind kind, DriverServices services, const RomInfo& info,
                         const CoreBootOptions& opts, const std::string& save_path,
                         Common::AltCore::Capabilities& caps) {
    Stop();
    services.map_capture =
        opts.enable_mapcap && static_cast<SecondScreen>(opts.second_screen) != SecondScreen::Off;
    services.step = [this, step = std::move(services.step)](uint32_t keys) {
        steps.fetch_add(1, std::memory_order_relaxed);
        step(keys);
    };
    services.backup_save = [this, save_path] {
        backed_up = backed_up || BackUpSave(save_path);
        return backed_up;
    };
    driver = MakeDriver(kind, services, info, opts);
    caps.has_touch = driver->HasTouch();
    UpdateSecondary(caps);
}

void DriverRunner::Stop() {
    driver.reset();
    if (missteps > 0) {
        LOG_ERROR(Core, "DriverRunner: {} frames did not step the game once", missteps);
    }
    frame = DriverFrame{};
    secondary_size = {};
    steps.store(0, std::memory_order_relaxed);
    missteps = 0;
    backed_up = false;
}

void DriverRunner::RunFrame(const Common::AltCore::InputState& input, FrameOutput& out,
                            Common::AltCore::Capabilities& caps) {
    out.primary = {};
    out.secondary = {};
    out.audio_pcm.clear();
    out.rumble = 0.0f;
    out.metrics_changed = false;
    if (!driver) {
        return;
    }

    if (const uint32_t stray = steps.exchange(0, std::memory_order_relaxed)) {
        NoteMisstep(stray, "outside a frame");
    }

    DriverInput in;
    in.keys = input.buttons & 0x3FFu; // ButtonBit 0..9 are the GBA's keys
    if (caps.has_touch && input.touching) {
        in.touching = true;
        in.touch_x = input.touch_x;
        in.touch_y = input.touch_y;
    }
    frame.primary = {};
    frame.secondary = {};
    frame.overlay = {};
    frame.audio_pcm.clear();
    frame.rumble = 0.0f;
    driver->RunFrame(in, frame);
    if (const uint32_t stepped = steps.exchange(0, std::memory_order_relaxed); stepped != 1) {
        NoteMisstep(stepped, "in a frame");
    }

    out.primary = ToImage(frame.primary);
    out.secondary = ToImage(frame.secondary);
    out.audio_pcm.swap(frame.audio_pcm);
    out.rumble = frame.rumble;
    if (UpdateSecondary(caps)) {
        out.metrics_changed = true;
        out.updated_metrics = caps.screens;
    }
}

void DriverRunner::Reset() {
    if (driver) {
        driver->Reset();
    }
}

bool DriverRunner::StateSafeNow() const {
    return !driver || driver->StateSafeNow();
}

void DriverRunner::AfterStateLoad() {
    if (driver) {
        driver->AfterStateLoad();
    }
}

bool DriverRunner::UpdateSecondary(Common::AltCore::Capabilities& caps) {
    const auto size = driver->SecondarySize();
    if (size == secondary_size) {
        return false;
    }
    secondary_size = size;
    const auto [width, height] = size;
    auto& screens = caps.screens;
    screens.has_bottom = width > 0 && height > 0;
    if (screens.has_bottom) {
        screens.bottom_aspect = static_cast<float>(width) / static_cast<float>(height);
        if (caps.has_touch) {
            screens.touch_width = width;
            screens.touch_height = height;
        }
    }
    return true;
}

void DriverRunner::NoteMisstep(uint32_t stepped, const char* where) {
    if (++missteps <= 10 || missteps % 600 == 0) { // it may be every frame
        LOG_ERROR(Core, "DriverRunner: the driver stepped the game {} times {}", stepped, where);
    }
}

} // namespace FrameCores
