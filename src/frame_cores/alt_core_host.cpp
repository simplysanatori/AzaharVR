// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/alt_core_host.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <thread>
#include <utility>

#include "common/logging/log.h"
#include "common/settings.h"
#include "common/xr_pacer.h"
#include "frame_cores/gb_core.h"
#include "frame_cores/gba_core.h"
#include "frame_cores/nds_core.h"
#include "video_core/renderer_vulkan/alt_xr_renderer.h"

namespace FrameCores {
namespace {

std::mutex g_default_cb_mutex;
HostCallbacks g_default_callbacks{};

HostCallbacks CopyDefaultCallbacks() {
    std::lock_guard lock(g_default_cb_mutex);
    return g_default_callbacks;
}

// Updates Common::XrPacer::Shared::emu_frame_us when present in xr_pacer.h.
template <typename SharedT>
void SetPacerEmuFrameUs(SharedT& shared, double frame_us) {
    if constexpr (requires { shared.emu_frame_us.store(frame_us, std::memory_order_relaxed); }) {
        shared.emu_frame_us.store(frame_us, std::memory_order_relaxed);
    }
}

int64_t SteadyNowNs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

std::string FormatFileTime(const std::filesystem::path& p) {
    std::error_code ec;
    const auto ftime = std::filesystem::last_write_time(p, ec);
    if (ec) {
        return {};
    }
    const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min);
    return std::string(buf);
}

CorePaths BuildCorePaths(Common::AltCore::CoreKind kind, const std::string& data_dir) {
    const std::filesystem::path root(data_dir);
    std::string sub = "ds";
    switch (kind) {
    case Common::AltCore::CoreKind::GBA:
        sub = "gba";
        break;
    case Common::AltCore::CoreKind::GB:
        sub = "gb";
        break;
    case Common::AltCore::CoreKind::NDS:
    case Common::AltCore::CoreKind::DSi:
    default:
        sub = "ds";
        break;
    }
    const std::filesystem::path base = root / sub;
    CorePaths paths{};
    paths.data_dir = data_dir;
    paths.save_dir = (base / "saves").string();
    paths.state_dir = (base / "states").string();
    paths.bios_dir = base.string();
    paths.cheat_dir = (base / "cheats").string();
    return paths;
}

std::unique_ptr<CoreBackend> CreateBackend(Common::AltCore::CoreKind kind) {
    switch (kind) {
    case Common::AltCore::CoreKind::GBA:
        return std::make_unique<GbaCore>();
    case Common::AltCore::CoreKind::GB:
        return std::make_unique<GbCore>();
    case Common::AltCore::CoreKind::NDS:
    case Common::AltCore::CoreKind::DSi:
        return std::make_unique<NdsCore>();
    default:
        return nullptr;
    }
}

} // namespace

struct AltCoreHost::Impl {
    HostCallbacks callbacks{};
    HostFrontend frontend{};
    Layout::AltScreens screens{};
    bool custom_callbacks = false;

    std::unique_ptr<CoreBackend> core;
    std::unique_ptr<Vulkan::AltXrRenderer> renderer;
    AudioResampler audio;

    RomInfo rom_info{};
    CorePaths paths{};
    CoreBootOptions boot_opts{};
    std::string rom_path;
    std::string rom_stem;
    Common::AltCore::Capabilities caps = Common::AltCore::Capabilities::ForN3DS();

    mutable std::mutex core_mutex;
    mutable std::mutex input_mutex;
    mutable std::mutex stats_mutex;

    Common::AltCore::InputState latest_input{};
    FrameOutput last_frame{};
    HostStats stats{};

    std::atomic<bool> running{false};
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> redraw{false};
    std::atomic<bool> fast_forward{false};
    std::atomic<uint32_t> ff_speed_pct{200};
    bool headless = false;

    std::thread emu_thread;
    float last_rumble = 0.0f;
    uint32_t rewind_frame_div = 0;

    std::string SlotPath(int slot) const {
        return (std::filesystem::path(paths.state_dir) /
                (rom_stem + ".slot" + std::to_string(slot) + ".state"))
            .string();
    }

    std::string AutosavePath() const {
        return (std::filesystem::path(paths.state_dir) / (rom_stem + ".autosave.state")).string();
    }

    void NotifyToast(const std::string& msg) {
        if (callbacks.on_toast) {
            callbacks.on_toast(msg);
        }
    }

    Common::AltCore::InputState ReadInput() {
        Common::AltCore::InputState in{};
        {
            std::lock_guard in_lock(input_mutex);
            in = latest_input;
        }
        if (frontend.input) {
            const Common::AltCore::InputState own = frontend.input();
            in.buttons |= own.buttons;
            if (!in.touching) {
                in.touching = own.touching;
                in.touch_x = own.touch_x;
                in.touch_y = own.touch_y;
            }
        }
        return in;
    }

    /// Tells the window about a screen's new size. A Secondary Screen that goes (the GBA map in a
    /// game's menu) keeps its place and its last picture.
    void NoteScreens() {
        Layout::AltScreens now = screens;
        if (last_frame.primary.pixels) {
            now.top_width = last_frame.primary.width;
            now.top_height = last_frame.primary.height;
        }
        if (last_frame.secondary.pixels) {
            now.bottom_width = last_frame.secondary.width;
            now.bottom_height = last_frame.secondary.height;
        }
        if (now != screens) {
            screens = now;
            if (frontend.on_screens) {
                frontend.on_screens(screens);
            }
        }
    }

    void Present(bool skippable) {
        if (renderer && renderer->IsReady() && last_frame.primary.pixels) {
            renderer->Present(last_frame.primary, last_frame.secondary, skippable);
        }
    }

    void ExecuteSingleFrameLocked(double& frame_time_ms_out) {
        const Common::AltCore::InputState in = ReadInput();

        const auto t0 = std::chrono::steady_clock::now();

        bool rewound = false;
        if ((in.buttons & Common::AltCore::BtnRewind) != 0 && caps.has_rewind) {
            ++rewind_frame_div;
            if ((rewind_frame_div % 2u) == 0u) {
                rewound = core->RewindStep();
            }
        } else {
            rewind_frame_div = 0;
        }

        core->SetInput(in);
        core->RunFrame(last_frame);

        if (last_frame.metrics_changed) {
            caps.screens = last_frame.updated_metrics;
            if (callbacks.on_screen_metrics) {
                callbacks.on_screen_metrics(caps.screens);
            } else if (callbacks.on_capabilities) {
                callbacks.on_capabilities(caps);
            }
        }

        if (!rewound && !last_frame.audio_pcm.empty()) {
            audio.Push(last_frame.audio_pcm.data(), last_frame.audio_pcm.size() / 2u);
        }

        NoteScreens();
        Present(fast_forward.load(std::memory_order_relaxed));

        const float rumble = last_frame.rumble;
        if (callbacks.on_rumble && (rumble > 0.0f || last_rumble > 0.0f)) {
            callbacks.on_rumble(rumble);
            last_rumble = rumble;
        }

        const auto t1 = std::chrono::steady_clock::now();
        frame_time_ms_out =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
    }

    void RunEmuThread() {
        Common::XrPacer::LimiterLock pacer_lock;
        const double nominal_fps = core ? core->NominalFps() : 59.8261;
        const double nominal_frame_us = 1'000'000.0 / nominal_fps;

        auto next_frame_time = std::chrono::steady_clock::now();
        auto stats_window_start = next_frame_time;
        uint32_t window_frames = 0;
        double window_frame_ms_sum = 0.0;
        double window_peak_ms = 0.0;
        uint32_t window_slow = 0;
        bool was_paused = false;

        while (!stop_requested.load(std::memory_order_acquire)) {
            if (paused.load(std::memory_order_acquire)) {
                if (redraw.exchange(false, std::memory_order_acq_rel) ||
                    (renderer && renderer->NeedsPresent())) {
                    std::lock_guard lock(core_mutex);
                    Present(false);
                }
                if (!std::exchange(was_paused, true)) {
                    std::lock_guard lock(core_mutex);
                    core->CloseMicrophone();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                next_frame_time = std::chrono::steady_clock::now();
                continue;
            }
            was_paused = false;

            double frame_ms = 0.0;
            {
                std::lock_guard lock(core_mutex);
                if (!core || stop_requested.load(std::memory_order_relaxed)) {
                    break;
                }
                ExecuteSingleFrameLocked(frame_ms);
            }

            ++window_frames;
            window_frame_ms_sum += frame_ms;
            window_peak_ms = std::max(window_peak_ms, frame_ms);
            if (frame_ms > (nominal_frame_us / 1000.0) * 1.25) {
                ++window_slow;
            }

            const bool is_ff = fast_forward.load(std::memory_order_relaxed);
            // Percent; 0 is no limit. Emulation Speed applies as to a 3DS game.
            const double limit =
                is_ff ? static_cast<double>(ff_speed_pct.load(std::memory_order_relaxed))
                      : Settings::values.frame_limit.GetValue();

            // Pacing & XR LimiterLock
            const int64_t now_ns = SteadyNowNs();
            auto& pacer_shared = Common::XrPacer::Get();
            const auto step = pacer_lock.Frame(pacer_shared, false, limit, now_ns);

            if (limit > 0) {
                const double speed_scale = limit / 100.0 * step.speed;
                const double target_us =
                    (nominal_frame_us / std::max(0.1, speed_scale)) +
                    static_cast<double>(is_ff ? 0 : step.correction_us);

                next_frame_time += std::chrono::microseconds(
                    std::max<int64_t>(1000, static_cast<int64_t>(std::llround(target_us))));
                const auto now = std::chrono::steady_clock::now();
                const bool slept = next_frame_time > now;
                if (slept) {
                    std::this_thread::sleep_until(next_frame_time);
                } else if (now - next_frame_time > std::chrono::milliseconds(80)) {
                    // Resync clock after hiccup or pause so we don't burst frames
                    next_frame_time = now;
                }
                pacer_lock.NoteSleep(pacer_shared, slept);
                pacer_lock.NoteWake(pacer_shared, SteadyNowNs());
            } else {
                // No limit
                next_frame_time = std::chrono::steady_clock::now();
                pacer_lock.NoteSleep(pacer_shared, false);
            }

            const auto now = std::chrono::steady_clock::now();
            const double elapsed_s =
                std::chrono::duration<double>(now - stats_window_start).count();
            if (elapsed_s >= 0.5) {
                HostStats snap{};
                {
                    std::lock_guard slock(stats_mutex);
                    stats.game_fps = static_cast<double>(window_frames) / elapsed_s;
                    stats.emulation_speed = stats.game_fps / nominal_fps;
                    stats.frame_ms =
                        window_frames > 0 ? (window_frame_ms_sum / window_frames) : 0.0;
                    stats.frame_peak_ms = window_peak_ms;
                    stats.slow_frames += window_slow;
                    stats.total_frames += window_frames;
                    stats.audio_buffered_ms = audio.BufferedMs();
                    snap = stats;
                }
                if (callbacks.on_stats) {
                    callbacks.on_stats(snap);
                }
                stats_window_start = now;
                window_frames = 0;
                window_frame_ms_sum = 0.0;
                window_peak_ms = 0.0;
                window_slow = 0;
            }
        }
    }
};

AltCoreHost::AltCoreHost() : impl(std::make_unique<Impl>()) {}

AltCoreHost::~AltCoreHost() {
    Stop();
}

void AltCoreHost::SetDefaultCallbacks(HostCallbacks callbacks) {
    std::lock_guard lock(g_default_cb_mutex);
    g_default_callbacks = std::move(callbacks);
}

void AltCoreHost::SetCallbacks(HostCallbacks callbacks) {
    impl->callbacks = std::move(callbacks);
    impl->custom_callbacks = true;
}

void AltCoreHost::SetFrontend(HostFrontend frontend) {
    impl->frontend = std::move(frontend);
}

bool AltCoreHost::Boot(const std::string& rom_path, const std::string& data_dir,
                       bool headless_for_test, const CoreBootOptions& opts) {
    Stop();

    if (!impl->custom_callbacks) {
        impl->callbacks = CopyDefaultCallbacks();
    }

    const auto probed = ProbeRom(rom_path);
    if (!probed.has_value()) {
        LOG_ERROR(Core, "AltCoreHost: unsupported or unreadable ROM {}", rom_path);
        return false;
    }

    impl->rom_info = *probed;
    impl->rom_path = rom_path;
    impl->rom_stem = std::filesystem::path(rom_path).stem().string();
    impl->paths = BuildCorePaths(impl->rom_info.kind, data_dir);
    impl->boot_opts = opts;
    impl->headless = headless_for_test;

    impl->core = CreateBackend(impl->rom_info.kind);
    if (!impl->core) {
        return false;
    }

    impl->core->SetMicrophone(impl->frontend.microphone);
    if (!impl->core->Load(rom_path, impl->paths, opts)) {
        impl->core.reset();
        return false;
    }

    impl->caps = impl->core->GetCapabilities();
    impl->last_frame = FrameOutput{};
    impl->last_rumble = 0.0f;
    impl->screens = {};
    impl->paused.store(false, std::memory_order_relaxed);
    impl->redraw.store(false, std::memory_order_relaxed);
    impl->fast_forward.store(false, std::memory_order_relaxed);
    impl->stop_requested.store(false, std::memory_order_relaxed);
    {
        std::lock_guard slock(impl->stats_mutex);
        impl->stats = HostStats{};
    }

    // Configure XR frame pacer for the active core's native frame duration
    const double frame_us = 1'000'000.0 / impl->core->NominalFps();
    SetPacerEmuFrameUs(Common::XrPacer::Get(), frame_us);

    if (!headless_for_test) {
        impl->renderer = std::make_unique<Vulkan::AltXrRenderer>();
        if (!impl->renderer->Init(impl->frontend.window, impl->frontend.secondary_window)) {
            LOG_ERROR(Render_Vulkan, "AltCoreHost: no way to show the game");
            Stop();
            return false;
        }
    }

    impl->audio.Init(impl->core->AudioSampleRate(), headless_for_test);

    if (impl->callbacks.on_title_id) {
        impl->callbacks.on_title_id(impl->rom_info.title_id);
    }
    if (impl->callbacks.on_capabilities) {
        impl->callbacks.on_capabilities(impl->caps);
    }
    if (impl->callbacks.on_input_sink) {
        impl->callbacks.on_input_sink(
            [this](const Common::AltCore::InputState& in) { PushInput(in); });
    }
    if (impl->callbacks.on_game_running) {
        impl->callbacks.on_game_running(true);
    }

    impl->running.store(true, std::memory_order_release);

    if (!headless_for_test) {
        impl->emu_thread = std::thread([this] { impl->RunEmuThread(); });
    }

    LOG_INFO(Core, "AltCoreHost: booted '{}' (kind={}, title_id=0x{:016X}, headless={})",
             impl->rom_info.title, static_cast<int>(impl->rom_info.kind), impl->rom_info.title_id,
             headless_for_test);
    return true;
}

void AltCoreHost::Stop() {
    if (!impl) {
        return;
    }

    const bool was_running = impl->running.exchange(false, std::memory_order_acq_rel);
    impl->stop_requested.store(true, std::memory_order_release);

    if (impl->emu_thread.joinable()) {
        impl->emu_thread.join();
    }

    if (was_running) {
        // Persist autosave state and flush battery SRAM before tearing down the core
        {
            std::lock_guard lock(impl->core_mutex);
            if (impl->core) {
                impl->core->SaveStateToFile(impl->AutosavePath());
                impl->core->FlushBatterySave();
            }
        }

        if (impl->callbacks.on_rumble) {
            impl->callbacks.on_rumble(0.0f);
        }
        if (impl->callbacks.on_input_sink) {
            impl->callbacks.on_input_sink(nullptr);
        }
        if (impl->callbacks.on_capabilities) {
            impl->callbacks.on_capabilities(Common::AltCore::Capabilities::ForN3DS());
        }
        if (impl->callbacks.on_game_running) {
            impl->callbacks.on_game_running(false);
        }
    }

    impl->audio.Shutdown();

    if (impl->renderer) {
        impl->renderer->Shutdown();
        impl->renderer.reset();
    }

    {
        std::lock_guard lock(impl->core_mutex);
        if (impl->core) {
            impl->core->Unload();
            impl->core.reset();
        }
    }

    impl->caps = Common::AltCore::Capabilities::ForN3DS();
    SetPacerEmuFrameUs(Common::XrPacer::Get(), Common::XrPacer::kEmuFrameUs);
}

bool AltCoreHost::IsRunning() const {
    return impl->running.load(std::memory_order_acquire);
}

bool AltCoreHost::IsHeadless() const {
    return impl->headless;
}

void AltCoreHost::Pause(bool paused) {
    impl->paused.store(paused, std::memory_order_release);
    impl->audio.SetPaused(paused);
    if (paused && impl->callbacks.on_rumble && impl->last_rumble > 0.0f) {
        impl->callbacks.on_rumble(0.0f);
        impl->last_rumble = 0.0f;
    }
}

bool AltCoreHost::IsPaused() const {
    return impl->paused.load(std::memory_order_acquire);
}

void AltCoreHost::Redraw() {
    impl->redraw.store(true, std::memory_order_release);
}

void AltCoreHost::WithScreens(const std::function<void(const FrameOutput&)>& use) const {
    std::lock_guard lock(impl->core_mutex);
    use(impl->last_frame);
}

void AltCoreHost::Reset() {
    std::lock_guard lock(impl->core_mutex);
    if (impl->core) {
        impl->last_frame = FrameOutput{}; // its pixels are the core's
        impl->core->Reset();
        impl->audio.Flush();
    }
}

void AltCoreHost::SetFastForward(bool on, uint32_t speed_pct) {
    impl->ff_speed_pct.store(speed_pct, std::memory_order_relaxed);
    impl->fast_forward.store(on, std::memory_order_relaxed);
    impl->audio.SetFastForward(on, Settings::values.fast_forward_audio.GetValue());
}

bool AltCoreHost::IsFastForward() const {
    return impl->fast_forward.load(std::memory_order_relaxed);
}

void AltCoreHost::PushInput(const Common::AltCore::InputState& input) {
    std::lock_guard lock(impl->input_mutex);
    impl->latest_input = input;
}

bool AltCoreHost::StepFrames(int n) {
    if (n <= 0) {
        return true;
    }
    std::lock_guard lock(impl->core_mutex);
    if (!impl->core || !impl->running.load(std::memory_order_acquire)) {
        return false;
    }
    double ms_sum = 0.0;
    double peak = 0.0;
    for (int i = 0; i < n; ++i) {
        double ms = 0.0;
        impl->ExecuteSingleFrameLocked(ms);
        ms_sum += ms;
        peak = std::max(peak, ms);
    }
    {
        std::lock_guard slock(impl->stats_mutex);
        impl->stats.total_frames += static_cast<uint64_t>(n);
        impl->stats.frame_ms = ms_sum / static_cast<double>(n);
        impl->stats.frame_peak_ms = std::max(impl->stats.frame_peak_ms, peak);
        impl->stats.audio_buffered_ms = impl->audio.BufferedMs();
    }
    return true;
}

const FrameOutput& AltCoreHost::LastFrame() const {
    return impl->last_frame;
}

AudioResampler& AltCoreHost::GetAudioResampler() {
    return impl->audio;
}

bool AltCoreHost::QuickSave() {
    const bool ok = SaveSlot(0);
    if (ok) {
        impl->NotifyToast("Quick Saved");
    }
    return ok;
}

bool AltCoreHost::QuickLoad() {
    const bool ok = LoadSlot(0);
    impl->NotifyToast(ok ? "Quick Loaded" : "No Quick Save");
    return ok;
}

bool AltCoreHost::SaveSlot(int slot) {
    if (slot < 0 || slot > 10) {
        return false;
    }
    std::lock_guard lock(impl->core_mutex);
    if (!impl->core) {
        return false;
    }
    return impl->core->SaveStateToFile(impl->SlotPath(slot));
}

bool AltCoreHost::LoadSlot(int slot) {
    if (slot < 0 || slot > 10) {
        return false;
    }
    std::lock_guard lock(impl->core_mutex);
    if (!impl->core) {
        return false;
    }
    const bool ok = impl->core->LoadStateFromFile(impl->SlotPath(slot));
    if (ok) {
        impl->audio.Flush();
    }
    return ok;
}

bool AltCoreHost::SaveAutosave() {
    std::lock_guard lock(impl->core_mutex);
    if (!impl->core) {
        return false;
    }
    return impl->core->SaveStateToFile(impl->AutosavePath());
}

bool AltCoreHost::LoadAutosave() {
    std::lock_guard lock(impl->core_mutex);
    if (!impl->core) {
        return false;
    }
    const bool ok = impl->core->LoadStateFromFile(impl->AutosavePath());
    if (ok) {
        impl->audio.Flush();
    }
    return ok;
}

std::vector<SaveSlotInfo> AltCoreHost::ListSaveSlots() const {
    std::vector<SaveSlotInfo> result;
    std::error_code ec;
    for (int slot = 0; slot <= 10; ++slot) {
        const std::string path = impl->SlotPath(slot);
        if (std::filesystem::is_regular_file(path, ec)) {
            result.push_back(SaveSlotInfo{slot, FormatFileTime(path), path});
        }
    }
    return result;
}

bool AltCoreHost::ImportSave(const std::string& sav_path) {
    std::lock_guard lock(impl->core_mutex);
    if (!impl->core) {
        return false;
    }
    impl->last_frame = FrameOutput{}; // the core may start again
    const bool ok = impl->core->ImportBatterySave(sav_path);
    if (ok) {
        impl->audio.Flush();
        impl->NotifyToast("Save Imported");
    }
    return ok;
}

const RomInfo& AltCoreHost::GetRomInfo() const {
    return impl->rom_info;
}

Common::AltCore::Capabilities AltCoreHost::GetCapabilities() const {
    std::lock_guard lock(impl->core_mutex);
    return impl->caps;
}

HostStats AltCoreHost::GetStats() const {
    std::lock_guard lock(impl->stats_mutex);
    HostStats out = impl->stats;
    out.audio_buffered_ms = impl->audio.BufferedMs();
    return out;
}

std::vector<CheatEntry> AltCoreHost::ListCheats() const {
    std::lock_guard lock(impl->core_mutex);
    if (!impl->core) {
        return {};
    }
    return impl->core->ListCheats();
}

bool AltCoreHost::SetCheatEnabled(std::size_t idx, bool on) {
    std::lock_guard lock(impl->core_mutex);
    if (!impl->core) {
        return false;
    }
    return impl->core->SetCheatEnabled(idx, on);
}

bool AltCoreHost::AddCheat(const std::string& name, const std::string& code) {
    std::lock_guard lock(impl->core_mutex);
    if (!impl->core) {
        return false;
    }
    return impl->core->AddCheat(name, code);
}

void AltCoreHost::ReloadMicrophone() {
    std::lock_guard lock(impl->core_mutex);
    if (impl->core) {
        impl->core->ReloadMicrophone();
    }
}

void AltCoreHost::SetSolarLevel(int level) {
    std::lock_guard lock(impl->core_mutex);
    if (impl->core) {
        impl->core->SetSolarLevel(level);
    }
}

void AltCoreHost::SetMicBlow(bool on) {
    std::lock_guard lock(impl->core_mutex);
    if (impl->core) {
        impl->core->SetMicBlow(on);
    }
}

void AltCoreHost::SetLidClosed(bool closed) {
    std::lock_guard lock(impl->core_mutex);
    if (impl->core) {
        impl->core->SetLidClosed(closed);
    }
}

void AltCoreHost::SetTouchEnabled(bool on) {
    std::lock_guard lock(impl->core_mutex);
    if (impl->core) {
        impl->core->SetTouchEnabled(on);
    }
}

void AltCoreHost::SetScreenRotation(int deg) {
    std::lock_guard lock(impl->core_mutex);
    if (impl->core) {
        impl->core->SetScreenRotation(deg);
        impl->caps = impl->core->GetCapabilities();
        if (impl->callbacks.on_screen_metrics) {
            impl->callbacks.on_screen_metrics(impl->caps.screens);
        } else if (impl->callbacks.on_capabilities) {
            impl->callbacks.on_capabilities(impl->caps);
        }
    }
}

bool AltCoreHost::SetGbaSlotRom(const std::string& path) {
    std::lock_guard lock(impl->core_mutex);
    if (!impl->core) {
        return false;
    }
    return impl->core->SetGbaSlotRom(path);
}

void AltCoreHost::SetConsoleMode(bool dsi) {
    std::lock_guard lock(impl->core_mutex);
    if (!impl->core) {
        return;
    }
    impl->last_frame = FrameOutput{}; // the core starts again
    impl->core->SetConsoleMode(dsi);
    impl->caps = impl->core->GetCapabilities();
    impl->audio.Flush();
    if (impl->callbacks.on_capabilities) {
        impl->callbacks.on_capabilities(impl->caps);
    }
}

bool AltCoreHost::RewindStep() {
    std::lock_guard lock(impl->core_mutex);
    if (!impl->core) {
        return false;
    }
    const bool ok = impl->core->RewindStep();
    if (ok) {
        impl->audio.Flush();
    }
    return ok;
}

bool AltCoreHost::HandleMenuSetting(AltMenuSetting setting, int value) {
    switch (setting) {
    case AltMenuSetting::SolarLevel:
        SetSolarLevel(value);
        return true;
    case AltMenuSetting::MicBlow:
        SetMicBlow(value != 0);
        return true;
    case AltMenuSetting::LidClosed:
        SetLidClosed(value != 0);
        return true;
    case AltMenuSetting::TouchEnabled:
        SetTouchEnabled(value != 0);
        return true;
    case AltMenuSetting::ScreenRotation: {
        // Accept both menu index (0 -> 0°, 1 -> 90°, 2 -> 270°) and direct degrees (0, 90, 270).
        int deg = value;
        if (value == 1) {
            deg = 90;
        } else if (value == 2) {
            deg = 270;
        }
        SetScreenRotation(deg);
        return true;
    }
    case AltMenuSetting::ConsoleModeDsi:
        SetConsoleMode(value != 0);
        return true;
    case AltMenuSetting::FastForwardSpeed:
        impl->ff_speed_pct.store(static_cast<uint32_t>(std::max(0, value)),
                                 std::memory_order_relaxed);
        return true;
    }
    return false;
}

bool AltCoreHost::HandleMenuCommand(AltMenuCommand command, int value, const std::string& text) {
    switch (command) {
    case AltMenuCommand::Continue:
        Pause(false);
        return true;
    case AltMenuCommand::QuickSave:
        return QuickSave();
    case AltMenuCommand::QuickLoad:
        return QuickLoad();
    case AltMenuCommand::SaveSlot:
        return SaveSlot(value);
    case AltMenuCommand::LoadSlot:
        return LoadSlot(value);
    case AltMenuCommand::Restart:
        Reset();
        return true;
    case AltMenuCommand::CloseGame:
        Stop();
        return true;
    case AltMenuCommand::ToggleCheat: {
        const auto cheats = ListCheats();
        if (value < 0 || static_cast<std::size_t>(value) >= cheats.size()) {
            return false;
        }
        return SetCheatEnabled(static_cast<std::size_t>(value),
                               !cheats[static_cast<std::size_t>(value)].enabled);
    }
    case AltMenuCommand::AddCheat: {
        const auto nl = text.find('\n');
        if (nl != std::string::npos) {
            return AddCheat(text.substr(0, nl), text.substr(nl + 1));
        }
        return AddCheat("Custom Cheat", text);
    }
    case AltMenuCommand::ImportSave:
        return ImportSave(text);
    case AltMenuCommand::SetGbaSlotRom:
        return SetGbaSlotRom(text);
    case AltMenuCommand::LoadAutosave:
        return LoadAutosave();
    case AltMenuCommand::RewindStep:
        return RewindStep();
    }
    return false;
}

} // namespace FrameCores
