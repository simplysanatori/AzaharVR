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

using Option = Common::AltCore::DriverOption;

constexpr int kSaveCopies = 3;

Vulkan::AltScreenImage ToImage(const DriverPicture& picture) {
    Vulkan::AltScreenImage image{};
    image.pixels = picture.pixels;
    image.width = picture.width;
    image.height = picture.height;
    image.stride_pixels = picture.stride;
    return image;
}

/// The same colours, whatever the alpha.
bool Shows(const Vulkan::AltScreenImage& image, const DriverPicture& picture) {
    if (!picture.pixels || image.width != picture.width || image.height != picture.height) {
        return false;
    }
    const uint32_t stride = image.stride_pixels ? image.stride_pixels : image.width;
    for (uint32_t y = 0; y < image.height; ++y) {
        const uint32_t* a = image.pixels + static_cast<std::size_t>(y) * stride;
        const uint32_t* b = picture.pixels + static_cast<std::size_t>(y) * picture.stride;
        uint32_t different = 0;
        for (uint32_t x = 0; x < image.width; ++x) {
            different |= a[x] ^ b[x];
        }
        if ((different & 0x00FFFFFFu) != 0) {
            return false;
        }
    }
    return true;
}

/// `kind` becomes what was made.
std::unique_ptr<FrameDriver> MakeDriver(DriverKind& kind, const DriverServices& services,
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
#endif
#ifdef HAVE_GBADS_GAME
    if (kind == DriverKind::GbaDs) {
        if (auto driver = MakeGbaDsDriver(services)) {
            return driver;
        }
        LOG_ERROR(Core, "DriverRunner: gba-ds did not start for {}, its maps alone",
                  services.rom_path);
    }
#endif
    kind = DriverKind::Plain;
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

uint32_t DriverRunner::Known::Choices() const {
    using Common::AltCore::SecondScreenBit;
    if (!pokedaisy && !gbads) {
        return 0;
    }
    uint32_t choices = SecondScreenBit(static_cast<uint32_t>(SecondScreen::Off));
    if (pokedaisy) {
        choices |= SecondScreenBit(static_cast<uint32_t>(SecondScreen::PokeDaisy));
    }
    if (gbads) {
        choices |= SecondScreenBit(static_cast<uint32_t>(SecondScreen::GbaDs));
    }
    return choices;
}

DriverRunner::Known DriverRunner::WhatKnows(const std::string& rom_path, bool gbads) {
    Known known;
    known.gbads = gbads;
#ifdef HAVE_POKEDAISY
    known.pokedaisy = PokeDaisyKnows(rom_path);
#else
    (void)rom_path;
#endif
    return known;
}

DriverKind DriverRunner::Choose(const CoreBootOptions& opts, const Known& known) {
    if (opts.driver.make) {
        return opts.driver.kind;
    }
    return ChooseDriver(static_cast<SecondScreen>(opts.second_screen), known.pokedaisy,
                        known.gbads);
}

void DriverRunner::Start(DriverKind kind, const Known& known, DriverServices services,
                         const RomInfo& info,
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
    auto_fire = {};
    driver->SetEditing(editing);
    if (rotate_pages) {
        driver->SetOption(Option::RotatePages, *rotate_pages);
    }
    if (map_refresh) {
        driver->SetOption(Option::MapRefresh, *map_refresh);
    }
    caps.has_touch = driver->HasTouch();
    caps.second_screen_choices = known.Choices();
    caps.can_edit_game = driver->CanEdit();
    caps.has_quick_menu = driver->HasQuickMenu();
    caps.has_pages = known.gbads;
    caps.driver = driver->Name();
    UpdateOptions(caps);
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
    out.primary_right = {};
    out.layer_shift = 0.0f;
    out.secondary = {};
    out.overlay = {};
    out.held = false;
    out.audio_pcm.clear();
    out.rumble = 0.0f;
    out.metrics_changed = false;
    out.options_changed = false;
    if (!driver) {
        return;
    }

    if (const uint32_t stray = steps.exchange(0, std::memory_order_relaxed)) {
        NoteMisstep(stray, "outside a frame");
    }

    DriverInput in;
    in.keys = auto_fire.Frame(input.buttons) & 0x3FFu; // ButtonBit 0..9 are the GBA's keys
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
    frame.held = false;
    in.quick_menu = (input.buttons & Common::AltCore::BtnQuickMenu) != 0;
    driver->RunFrame(in, frame);
    if (const uint32_t stepped = steps.exchange(0, std::memory_order_relaxed);
        stepped != (frame.held ? 0u : 1u)) {
        NoteMisstep(stepped, "in a frame");
    }

    out.primary = ToImage(frame.primary);
    out.secondary = ToImage(frame.secondary);
    out.overlay = ToImage(frame.overlay);
    out.held = frame.held;
    out.audio_pcm.swap(frame.audio_pcm);
    out.rumble = frame.rumble;
    if (UpdateSecondary(caps)) {
        out.metrics_changed = true;
        out.updated_metrics = caps.screens;
    }
    if (UpdateOptions(caps)) {
        out.options_changed = true;
        out.updated_options = caps.driver_options;
        out.updated_options_on = caps.driver_options_on;
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

void DriverRunner::SetEditing(bool on) {
    editing = on;
    if (driver) {
        driver->SetEditing(on);
    }
}

void DriverRunner::TurnPage() {
    if (driver) {
        driver->TurnPage();
    }
}

bool DriverRunner::SetOption(Option option, uint32_t value,
                             Common::AltCore::Capabilities& caps) {
    if (option == Option::RotatePages) {
        rotate_pages = value;
    } else if (option == Option::MapRefresh) {
        map_refresh = value;
    }
    const bool taken = driver && driver->SetOption(option, value);
    if (driver) {
        UpdateOptions(caps);
    }
    return taken;
}

bool DriverRunner::UpdateOptions(Common::AltCore::Capabilities& caps) {
    const uint32_t has = driver->Options();
    const uint32_t on = driver->OptionsOn() & has;
    if (has == caps.driver_options && on == caps.driver_options_on) {
        return false;
    }
    caps.driver_options = has;
    caps.driver_options_on = on;
    return true;
}

void DriverRunner::SetRightEye(const DriverPicture& right, const DriverPicture& game, float shift,
                               FrameOutput& out) {
    if (!right.pixels || !out.primary.HasPicture()) {
        return;
    }
    if (right.width == game.width && right.height == game.height && Shows(out.primary, game)) {
        out.primary_right = ToImage(right);
        out.layer_shift = shift;
    } else {
        out.primary_right = out.primary;
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
