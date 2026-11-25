// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/gbads_driver.h"

#include <algorithm>
#include <iterator>
#include <system_error>

#include "common/logging/log.h"
#include "common/thread.h"
#include "frame_cores/second_screen.h"

extern "C" {
#include "emu.h"
#include "profiles.h"
}

namespace FrameCores {
namespace {

using Common::AltCore::DriverOption;
using Common::AltCore::DriverOptionBit;
using std::chrono::milliseconds;

constexpr uint32_t kOpaque = 0xFF000000u;
constexpr uint32_t kWidth = GBA_W;
constexpr uint32_t kHeight = GBA_H;
constexpr std::size_t kSoundFrames = 4096;
/// A wait is counted in these, so a process that was stopped meanwhile has used up one.
constexpr milliseconds kSlice{20};
/// For a loop told to leave from wherever it is.
constexpr milliseconds kGrace{100};

std::atomic<bool> g_in_use{false};

} // namespace

std::unique_ptr<GbaDsDriver> GbaDsDriver::Make(const DriverServices& services,
                                               const GbadsLoop& loop, const GbaDsLimits& limits) {
    if (!services.emu) {
        return nullptr;
    }
    std::unique_ptr<GbaDsDriver> driver(new GbaDsDriver(services, loop, limits));
    if (!driver->Start()) {
        return nullptr;
    }
    return driver;
}

GbaDsDriver::GbaDsDriver(const DriverServices& services_, const GbadsLoop& loop_,
                         const GbaDsLimits& limits_)
    : services(services_), loop(loop_), limits(limits_),
      logo_dir(services_.game_dir.empty() ? std::string{} : services_.game_dir + "/logo"),
      scratch(kSoundFrames * 2u, 0) {
    host.ctx = this;
    host.emu = services.emu;
    host.rom_path = services.rom_path.c_str();
    host.logo_dir = logo_dir.empty() ? nullptr : logo_dir.c_str();
    host.events = [](void* ctx, GbadsAsk* ask) {
        return static_cast<GbaDsDriver*>(ctx)->OnEvents(ask);
    };
    host.keys = [](void* ctx) { return static_cast<GbaDsDriver*>(ctx)->keys; };
    host.step = [](void* ctx, uint32_t step_keys) {
        return static_cast<GbaDsDriver*>(ctx)->OnStep(step_keys);
    };
    host.present = [](void* ctx) { return static_cast<GbaDsDriver*>(ctx)->OnPresent(); };
    host.alive = [](void* ctx) {
        return !static_cast<GbaDsDriver*>(ctx)->aborting.load(std::memory_order_relaxed);
    };
    host.set_rom = [](void*, const char* code, uint32_t crc) {
        const auto lock = LockProfiles();
        profile_set_rom(code, crc);
    };
    host.log = [](void*, const char* text) { LOG_DEBUG(Core, "gba-ds: {}", text); };
}

GbaDsDriver::~GbaDsDriver() {
    StopLoop();
    if (holds_game) {
        g_in_use.store(false);
    }
}

bool GbaDsDriver::Start() {
    if (g_in_use.exchange(true)) {
        LOG_ERROR(Core, "GbaDsDriver: a game is open already");
        return false;
    }
    holds_game = true;
    try {
        thread = std::thread(&GbaDsDriver::Body, this);
    } catch (const std::system_error& e) {
        LOG_ERROR(Core, "GbaDsDriver: no thread: {}", e.what());
        return false;
    }
    if (!WaitForLoop(limits.wait * 5)) { // it reads the ROM a few times
        LOG_ERROR(Core, "GbaDsDriver: the set-up of {} does not end", services.rom_path);
        return false;
    }
    {
        std::lock_guard lock(mutex);
        if (ended) {
            LOG_ERROR(Core, "GbaDsDriver: {} did not start", services.rom_path);
            return false;
        }
    }
    int w = 0;
    int h = 0;
    if (!loop.output(&w, &h) || w <= 0 || h <= 0) {
        LOG_WARNING(Core, "GbaDsDriver: no Secondary Screen for {}", services.rom_path);
        return false;
    }
    secondary_w = static_cast<uint32_t>(w);
    secondary_h = static_cast<uint32_t>(h);
    LOG_INFO(Core, "GbaDsDriver: started ({}x{})", w, h);
    return true;
}

void GbaDsDriver::RunFrame(const DriverInput& in, DriverFrame& out) {
    const uint32_t frame_keys = in.keys & 0x3FFu;
    if (thread.joinable()) {
        if (!late) {
            keys = frame_keys;
            stepped = false;
            presented = false;
            rumble = 0.0f;
            audio.clear();
            late = !Resume();
            if (late) {
                LOG_ERROR(Core, "GbaDsDriver: the loop does not come back");
                std::lock_guard lock(mutex);
                leaving = true;
                aborting = true;
            }
        }
        if (late && !WaitForLoop(kGrace)) {
            // It may be at the core, so nothing here can be: the last picture stays.
            if (primary.empty()) {
                primary.assign(std::size_t{kWidth} * kHeight, kOpaque);
                primary_w = kWidth;
                primary_h = kHeight;
            }
            out.primary = {primary.data(), primary_w, primary_h, primary_w};
            return;
        }
        late = false;
        if (leaving) {
            Drop("it had to be stopped");
        } else if (std::unique_lock lock(mutex); ended) {
            lock.unlock();
            Drop("it left by itself");
        } else if (stepped && presented) {
            bad_frames = 0;
        } else if (++bad_frames >= limits.frames) {
            lock.unlock();
            Drop("it does not step and show a frame any more");
        }
    } else {
        stepped = false;
        rumble = 0.0f;
        audio.clear();
    }

    DropSound(); // of frames drawn again
    if (!stepped) {
        Step(frame_keys);
    }
    TakePictures();

    if (!primary.empty()) {
        out.primary = {primary.data(), primary_w, primary_h, primary_w};
    }
    if (has_secondary) {
        out.secondary = {secondary.data(), secondary_w, secondary_h, secondary_w};
    }
    out.audio_pcm.swap(audio);
    out.rumble = rumble;
}

void GbaDsDriver::Reset() {
    if (thread.joinable() && !late) {
        loop.restart();
    }
}

bool GbaDsDriver::StateSafeNow() const {
    return !late;
}

void GbaDsDriver::AfterStateLoad() {
    if (thread.joinable() && !late) {
        loop.state_loaded();
    }
}

std::pair<uint32_t, uint32_t> GbaDsDriver::SecondarySize() const {
    return {secondary_w, secondary_h};
}

uint32_t GbaDsDriver::Options() const {
    if (!thread.joinable() || late || !loop.features) {
        return 0;
    }
    const unsigned has = loop.features();
    uint32_t options = 0;
    if (has & GBADS_HAS_PAGES) {
        options |= DriverOptionBit(DriverOption::RotatePages);
    }
    if (has & (GBADS_HAS_PAGES | GBADS_HAS_VIEWS)) {
        options |= DriverOptionBit(DriverOption::TurnPage);
    }
    if (has & GBADS_HAS_MAP) {
        options |= DriverOptionBit(DriverOption::MapRefresh);
    }
    return options;
}

uint32_t GbaDsDriver::OptionsOn() const {
    return rotate.load(std::memory_order_relaxed) ? DriverOptionBit(DriverOption::RotatePages) : 0u;
}

bool GbaDsDriver::SetOption(DriverOption option, uint32_t value) {
    switch (option) {
    case DriverOption::RotatePages:
        rotate.store(value != 0, std::memory_order_relaxed);
        return true;
    case DriverOption::MapRefresh:
        map_interval.store(static_cast<int>(Common::AltCore::kMapRefreshFrames[std::min<std::size_t>(
                               value, std::size(Common::AltCore::kMapRefreshFrames) - 1)]),
                           std::memory_order_relaxed);
        return true;
    case DriverOption::TurnPage:
        TurnPage();
        return true;
    default:
        return false;
    }
}

void GbaDsDriver::StopLoop() {
    if (!thread.joinable()) {
        return;
    }
    {
        std::lock_guard lock(mutex);
        leaving = true;
        if (!ended) {
            turn = Turn::Loop;
        }
    }
    cond.notify_all();
    if (!WaitForLoop(limits.wait)) {
        LOG_ERROR(Core, "GbaDsDriver: the loop does not end, told to leave from where it is");
        aborting = true;
    }
    thread.join();
}

void GbaDsDriver::Drop(const char* why) {
    LOG_ERROR(Core, "GbaDsDriver: the game alone from here on: {}", why);
    StopLoop();
    presented = false;
    has_secondary = false;
    secondary_w = 0;
    secondary_h = 0;
}

void GbaDsDriver::Step(uint32_t step_keys) {
    in_step = true;
    services.step(step_keys);
    in_step = false;
    stepped = true;
    // Now: the loop may run frames of its own after this one.
    for (;;) {
        const std::size_t got = services.drain_audio(scratch.data(), kSoundFrames);
        audio.insert(audio.end(), scratch.begin(),
                     scratch.begin() + static_cast<std::ptrdiff_t>(got * 2u));
        if (got < kSoundFrames) {
            break;
        }
    }
    rumble = services.rumble();
}

void GbaDsDriver::DropSound() {
    while (services.drain_audio(scratch.data(), kSoundFrames) == kSoundFrames) {
    }
}

bool GbaDsDriver::Resume() {
    {
        std::lock_guard lock(mutex);
        turn = Turn::Loop;
    }
    cond.notify_all();
    return WaitForLoop(limits.wait);
}

bool GbaDsDriver::WaitForLoop(milliseconds limit) {
    std::unique_lock lock(mutex);
    const auto back = [this] { return turn == Turn::Host; };
    for (milliseconds left = limit; !cond.wait_for(lock, kSlice, back);) {
        if (in_step.load(std::memory_order_relaxed)) {
            continue;
        }
        left -= kSlice;
        if (left <= milliseconds::zero()) {
            return false;
        }
    }
    return true;
}

void GbaDsDriver::Body() {
    Common::SetCurrentThreadName("GbaDsLoop");
    loop.run(&host);
    {
        std::lock_guard lock(mutex);
        ended = true;
        turn = Turn::Host;
    }
    cond.notify_all();
}

bool GbaDsDriver::Park() {
    std::unique_lock lock(mutex);
    turn = Turn::Host;
    cond.notify_all();
    cond.wait(lock, [this] { return turn == Turn::Loop || leaving; });
    return !leaving;
}

bool GbaDsDriver::OnEvents(GbadsAsk* ask) {
    if (leaving) {
        return false;
    }
    if (!ready) {
        ready = true; // the set-up is done
        if (!Park()) {
            return false;
        }
    }
    ask->turn_page = page_asked.exchange(false, std::memory_order_relaxed);
    ask->rotate = rotate.load(std::memory_order_relaxed);
    ask->map_interval = map_interval.load(std::memory_order_relaxed);
    return true;
}

bool GbaDsDriver::OnStep(uint32_t step_keys) {
    if (leaving) {
        return false;
    }
    if (stepped) { // a pass that showed nothing: this step is the next frame's
        if (!Park()) {
            return false;
        }
        step_keys = keys;
    }
    Step(step_keys & 0x3FFu);
    return true;
}

bool GbaDsDriver::OnPresent() {
    if (leaving) {
        return false;
    }
    presented = true;
    Park();
    return true; // leaving or not: it ends by itself at the top of the next pass
}

bool GbaDsDriver::Copy(const GbadsPicture& from, std::vector<uint32_t>& to) {
    if (!from.pixels || from.width <= 0 || from.height <= 0) {
        return false;
    }
    const std::size_t n = static_cast<std::size_t>(from.width) * from.height;
    to.resize(n);
    if (from.shade <= 0) {
        for (std::size_t i = 0; i < n; ++i) {
            to[i] = from.pixels[i] | kOpaque;
        }
        return true;
    }
    const uint32_t keep = 255u - static_cast<uint32_t>(std::min(from.shade, 255));
    for (std::size_t i = 0; i < n; ++i) {
        const uint32_t c = from.pixels[i];
        const uint32_t r = (c & 0xFFu) * keep / 255u;
        const uint32_t g = ((c >> 8) & 0xFFu) * keep / 255u;
        const uint32_t b = ((c >> 16) & 0xFFu) * keep / 255u;
        to[i] = kOpaque | (b << 16) | (g << 8) | r;
    }
    return true;
}

void GbaDsDriver::TakePictures() {
    GbadsPicture pictures[2]{};
    if (thread.joinable() && presented) {
        loop.pictures(pictures);
        has_secondary = Copy(pictures[1], secondary);
        if (has_secondary) {
            secondary_w = static_cast<uint32_t>(pictures[1].width);
            secondary_h = static_cast<uint32_t>(pictures[1].height);
        }
    }
    // The game's picture is the GBA's size, whatever the loop does
    const bool game = pictures[0].width == static_cast<int>(kWidth) &&
                      pictures[0].height == static_cast<int>(kHeight) &&
                      Copy(pictures[0], primary);
    if (game) {
        primary_w = kWidth;
        primary_h = kHeight;
        return;
    }
    const DriverPicture own = services.picture();
    if (!own.pixels) {
        return;
    }
    primary.resize(static_cast<std::size_t>(own.width) * own.height);
    for (uint32_t y = 0; y < own.height; ++y) {
        const uint32_t* from = own.pixels + static_cast<std::size_t>(y) * own.stride;
        std::transform(from, from + own.width,
                       primary.begin() + static_cast<std::ptrdiff_t>(y) * own.width,
                       [](uint32_t c) { return c | kOpaque; });
    }
    primary_w = own.width;
    primary_h = own.height;
}

std::unique_ptr<FrameDriver> MakeGbaDsDriver(const DriverServices& services) {
    return GbaDsDriver::Make(services, gbads_game_loop, GbaDsLimits{});
}

} // namespace FrameCores
