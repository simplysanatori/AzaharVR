// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/frame_driver.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>

#include "common/logging/log.h"
#include "frame_cores/rom_info.h"
#include "frame_cores/second_screen.h"

extern "C" {
#include "emu.h"
#include "mapcap.h"
#include "profiles.h"
}

namespace FrameCores {
namespace {

using Common::AltCore::DriverOption;
using Common::AltCore::DriverOptionBit;

constexpr uint32_t kOpaque = 0xFF000000u;
constexpr uint32_t kPokeDaisyWidth = 240; // PD_GAME_STRIDE

/// The core's own frame, and for a GBA game gba-ds's map on the Secondary Screen.
class PlainDriver final : public FrameDriver {
public:
    PlainDriver(const DriverServices& services_, const RomInfo& info)
        : services(services_), audio(4096u * 2u, 0) {
        if (services.map_capture && services.emu &&
            info.kind == Common::AltCore::CoreKind::GBA) {
            StartMap();
        }
    }

    ~PlainDriver() override {
        if (mapcap) {
            mapcap_cancel(mapcap);
            mapcap_destroy(mapcap);
        }
        for (uint32_t* frame : map.frames) {
            std::free(frame);
        }
    }

    void RunFrame(const DriverInput& in, DriverFrame& out) override {
        services.step(in.keys);
        ++frames;

        const DriverPicture game = services.picture();
        if (game.pixels) {
            primary.resize(static_cast<std::size_t>(game.width) * game.height);
            for (uint32_t y = 0; y < game.height; ++y) {
                const uint32_t* from = game.pixels + static_cast<std::size_t>(y) * game.stride;
                uint32_t* to = primary.data() + static_cast<std::size_t>(y) * game.width;
                for (uint32_t x = 0; x < game.width; ++x) {
                    to[x] = from[x] | kOpaque;
                }
            }
            out.primary = {primary.data(), game.width, game.height, game.width};
        }

        const std::size_t got = services.drain_audio(audio.data(), audio.size() / 2u);
        out.audio_pcm.assign(audio.data(), audio.data() + got * 2u);
        out.rumble = services.rumble();

        if (mapcap) {
            RunMap(out);
        }
    }

    void Reset() override {
        if (mapcap) {
            mapcap_cancel(mapcap);
        }
        frames = 0;
        last_room_key = 0;
        last_request = 0;
        has_map_image = false;
        page = 0;
    }

    void AfterStateLoad() override {
        if (mapcap) {
            mapcap_cancel(mapcap);
        }
        last_room_key = 0;
        last_request = 0;
    }

    std::pair<uint32_t, uint32_t> SecondarySize() const override {
        return {map_w, map_h};
    }

    bool HasTouch() const override {
        return false;
    }

    Common::AltCore::DriverName Name() const override {
        return mapcap ? Common::AltCore::DriverName::GbaDs : Common::AltCore::DriverName::None;
    }

    uint32_t Options() const override {
        const MapProfile* prof = mapcap ? mapcap_profile(mapcap) : nullptr;
        return prof && prof->pages ? DriverOptionBit(DriverOption::RotatePages) |
                                         DriverOptionBit(DriverOption::TurnPage)
                                   : 0u;
    }

    uint32_t OptionsOn() const override {
        return rotate ? DriverOptionBit(DriverOption::RotatePages) : 0u;
    }

    bool SetOption(DriverOption option, uint32_t value) override {
        switch (option) {
        case DriverOption::RotatePages:
            if (rotate != (value != 0)) { // the page on view stays
                page += anim_frame / kPageFrames;
                anim_frame = 0;
                rotate = value != 0;
            }
            return true;
        case DriverOption::TurnPage:
            TurnPage();
            return true;
        default:
            return false;
        }
    }

    void TurnPage() override {
        turns.fetch_add(1, std::memory_order_relaxed);
    }

private:
    /// A page's time.
    static constexpr uint64_t kPageFrames = 180;

    void StartMap() {
        if (services.emu->gameCode[0] == '\0') {
            return;
        }
        mapcap = mapcap_create(services.rom_path.c_str(), services.emu->gameCode, true);
        if (!mapcap) {
            return;
        }
        const MapProfile* prof = mapcap_profile(mapcap);
        int w = 0;
        int h = 0;
        mapcap_size(mapcap, &w, &h);
        if (!prof || prof->logoOnly || w <= 0 || h <= 0) {
            mapcap_destroy(mapcap);
            mapcap = nullptr;
            return;
        }
        map_w = static_cast<uint32_t>(w);
        map_h = static_cast<uint32_t>(h);
        LOG_INFO(Core, "PlainDriver: map capture for {} ({}x{})", mapcap_title(mapcap), w, h);
    }

    void RunMap(DriverFrame& out) {
        if (mapcap_poll(mapcap, last_map_serial, &map)) {
            last_map_serial = map.serial;
            has_map_image = map.ok && map.w > 0 && map.h > 0;
            if (!map.pages) {
                anim_frame = 0;
            }
            if (has_map_image) {
                map_w = static_cast<uint32_t>(map.w);
                map_h = static_cast<uint32_t>(map.h);
            }
        }

        if (mapcap_in_menu(mapcap, services.emu)) {
            has_map_image = false;
        } else if (!mapcap_busy(mapcap) && mapcap_live_ok(mapcap, services.emu)) {
            const MapProfile* prof = mapcap_profile(mapcap);
            const uint64_t interval =
                prof && prof->interval > 0 ? static_cast<uint64_t>(prof->interval) : 0;
            const uint32_t room_key = mapcap_room_key(mapcap, services.emu);
            const bool room_changed = room_key != 0 && room_key != last_room_key;
            if (MapCaptureDue(has_map_image, room_changed, frames, frames - last_request,
                              interval) &&
                mapcap_request(mapcap, services.emu)) {
                last_request = frames;
                if (room_key != 0) {
                    last_room_key = room_key;
                }
            }
        }

        if (!has_map_image || !map.ok || map.nFrames <= 0) {
            return;
        }
        int index = 0;
        if (map.nFrames > 1) {
            const uint64_t step =
                map.pages ? kPageFrames : static_cast<uint64_t>(std::max(1, map.frameStep));
            if (map.pages) {
                page += turns.exchange(0, std::memory_order_relaxed);
            }
            index = static_cast<int>((anim_frame / step + (map.pages ? page : 0)) %
                                     static_cast<uint64_t>(map.nFrames));
            if (rotate || !map.pages) {
                ++anim_frame;
            }
        }
        const uint32_t* pixels = map.frames[index];
        if (!pixels) {
            return;
        }
        const std::size_t n = static_cast<std::size_t>(map_w) * map_h;
        secondary.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            secondary[i] = pixels[i] | kOpaque;
        }
        out.secondary = {secondary.data(), map_w, map_h, map_w};
    }

    const DriverServices services;
    std::vector<uint32_t> primary;
    std::vector<uint32_t> secondary;
    std::vector<int16_t> audio;
    uint64_t frames = 0; ///< since the start or a reset

    MapCapture* mapcap = nullptr;
    MapResult map{};
    uint32_t map_w = 0;
    uint32_t map_h = 0;
    uint32_t last_map_serial = 0;
    uint32_t last_room_key = 0;
    uint64_t last_request = 0;
    uint64_t anim_frame = 0;
    uint64_t page = 0;             ///< pages turned by hand
    std::atomic<uint32_t> turns{0}; ///< not taken yet
    bool rotate = false;
    bool has_map_image = false;
};

} // namespace

DriverKind ChooseDriver(SecondScreen choice, bool pokedaisy_known, bool gbads_known) {
    if (choice == SecondScreen::Off) {
        return DriverKind::Plain;
    }
    if (pokedaisy_known && (choice != SecondScreen::GbaDs || !gbads_known)) {
        return DriverKind::PokeDaisy;
    }
    return gbads_known ? DriverKind::GbaDs : DriverKind::Plain;
}

uint32_t DriverBufferWidth(DriverKind kind, uint32_t width) {
    return kind == DriverKind::PokeDaisy ? std::max(width, kPokeDaisyWidth) : width;
}

std::unique_ptr<FrameDriver> MakePlainDriver(const DriverServices& services,
                                             const RomInfo& info) {
    return std::make_unique<PlainDriver>(services, info);
}

} // namespace FrameCores
