// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#include "frame_cores/alt_core_host.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "common/transient_memory.h"
#include "common/xr_pacer.h"
#include "common/zstd_compression.h"
#include "core/savestate.h"
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

uint64_t UnixNow() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

constexpr std::size_t kStateHeaderSize = 256; // Core::MakeStateHeader

CorePaths BuildCorePaths(const RomInfo& info, const std::string& data_dir) {
    const std::filesystem::path base =
        std::filesystem::path(data_dir) / CoreFolder(info.kind);
    CorePaths paths{};
    paths.data_dir = data_dir;
    paths.game_dir = GameFolder(data_dir, info);
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

/// What AltCoreHost's state functions ask of the emulation thread.
struct StateRequest {
    enum class Kind { Save, Autosave, Load, LoadAutosave, Undo, Import, Prune };
    Kind kind = Kind::Save;
    int slot = 0;
    Core::Autosave::Reason reason = Core::Autosave::Reason::Timer;
    uint32_t keep = 0;
    std::string path;
    uint64_t time = 0;
    bool anyway = false;
    uint32_t restarts = 0; ///< the game's when this was asked for
    bool put_off = false;  ///< it waits for CoreBackend::StateSafeNow

    bool IsSave() const {
        return kind == Kind::Save || kind == Kind::Autosave;
    }
};

/// The moment before a load. Guarded by core_mutex.
struct UndoState {
    std::vector<uint8_t> state;
    uint64_t time = 0;
    bool imported = false; ///< the load was Import Save: the earlier save is in "<save>.bak"
};

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
    bool page_before = false; ///< BtnTurnPage in the last frame. core_mutex held.
    uint32_t hidden_frames = 0; ///< this many frames are not shown. core_mutex held.
    HostStats stats{};

    std::atomic<bool> running{false};
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> redraw{false};
    std::atomic<bool> holding{false}; ///< the last frame was held (FrameOutput::held)
    std::atomic<bool> fast_forward{false};
    std::atomic<uint32_t> ff_speed_pct{200};
    bool headless = false;

    std::thread emu_thread;
    float last_rumble = 0.0f;
    int mic_blow_frames = 0; ///< BlowMic's frames to go

    std::mutex request_mutex;
    std::deque<StateRequest> requests;    ///< guarded by request_mutex
    std::atomic<int> requests_open{0};    ///< waiting or under way
    std::atomic<uint32_t> restarts{0};    ///< Reset's count
    std::atomic<int64_t> last_frame_ns{0}; ///< when a frame ended (steady clock)
    UndoState undo;

    std::atomic<bool> write_ok{true}; ///< the writer's last state
    Core::Autosave::Writer writer;    ///< last: its thread ends before the rest goes

    std::string StatesDir() const {
        return (std::filesystem::path(paths.game_dir) / "").string();
    }

    std::string SlotPath(int slot) const {
        return Core::StatePathIn(StatesDir(), rom_info.title_id, 0, static_cast<u32>(slot));
    }

    /// The battery save, where the cores keep it.
    std::string SavePath() const {
        return (std::filesystem::path(paths.game_dir) / (rom_stem + ".sav")).string();
    }

    bool Ask(StateRequest request) {
        if (!running.load(std::memory_order_acquire)) {
            return false;
        }
        request.restarts = restarts.load(std::memory_order_acquire);
        requests_open.fetch_add(1, std::memory_order_acq_rel);
        if (headless) {
            bool behind = false;
            {
                std::lock_guard lock(request_mutex);
                behind = !requests.empty();
            }
            write_ok.store(true);
            const Turn turn = TakeTurn(request, false, behind);
            if (turn == Turn::Waits) {
                std::lock_guard lock(request_mutex);
                requests.push_back(std::move(request));
                return true;
            }
            requests_open.fetch_sub(1, std::memory_order_acq_rel);
            writer.WaitIdle();
            return turn == Turn::Ok && write_ok.load();
        }
        std::lock_guard lock(request_mutex);
        requests.push_back(std::move(request));
        return true;
    }

    /// Emulation thread, between two frames. `stopping`: only what saves, and nothing waits.
    void RunRequests(bool stopping) {
        std::vector<StateRequest> waiting;
        while (requests_open.load(std::memory_order_acquire) > static_cast<int>(waiting.size())) {
            StateRequest request;
            {
                std::lock_guard lock(request_mutex);
                if (requests.empty()) {
                    break;
                }
                request = std::move(requests.front());
                requests.pop_front();
            }
            if (TakeTurn(request, stopping, !waiting.empty()) == Turn::Waits) {
                waiting.push_back(std::move(request));
            } else {
                requests_open.fetch_sub(1, std::memory_order_acq_rel);
            }
        }
        if (!waiting.empty()) {
            std::lock_guard lock(request_mutex);
            requests.insert(requests.begin(), std::make_move_iterator(waiting.begin()),
                            std::make_move_iterator(waiting.end()));
        }
    }

    enum class Turn { Ok, Failed, Waits };

    /// Runs a request, or not while a state would catch the game half changed: an autosave is
    /// dropped then, anything else waits. `behind`: an earlier one waits (they keep their order).
    Turn TakeTurn(StateRequest& request, bool stopping, bool behind) {
        using Kind = StateRequest::Kind;
        if (stopping && !request.IsSave()) {
            return Turn::Failed;
        }
        if (request.put_off && request.kind == Kind::Save &&
            request.restarts != restarts.load(std::memory_order_acquire)) {
            NotMade(request, SlotPath(request.slot)); // the moment it was for is gone
            return Turn::Failed;
        }
        if (request.kind == Kind::Prune || (!behind && StateSafe())) {
            return RunRequest(request) ? Turn::Ok : Turn::Failed;
        }
        if (request.kind == Kind::Autosave) {
            LOG_INFO(Core, "AltCoreHost: {} autosave dropped: the game is being changed",
                     Core::Autosave::ReasonName(request.reason));
            return Turn::Failed;
        }
        if (stopping) { // no frame comes
            NotMade(request, SlotPath(request.slot));
            return Turn::Failed;
        }
        if (!std::exchange(request.put_off, true)) {
            LOG_INFO(Core, "AltCoreHost: a state request ({}) waits: the game is being changed",
                     static_cast<int>(request.kind));
        }
        return Turn::Waits;
    }

    bool StateSafe() const {
        std::lock_guard lock(core_mutex);
        return !core || core->StateSafeNow();
    }

    bool RunRequest(const StateRequest& request) {
        switch (request.kind) {
        case StateRequest::Kind::Save:
        case StateRequest::Kind::Autosave:
            return MakeState(request);
        case StateRequest::Kind::Load:
        case StateRequest::Kind::LoadAutosave:
            return LoadState(request);
        case StateRequest::Kind::Undo:
            return Undo();
        case StateRequest::Kind::Import:
            return Import(request.path);
        case StateRequest::Kind::Prune:
            writer.WaitIdle(); // a write under way prunes to the old Keep
            Core::Autosave::Prune(Core::Autosave::Dir(StatesDir()), rom_info.title_id, 0,
                                  request.keep);
            return true;
        }
        return false;
    }

    /// The Primary Screen's last picture for an autosave's entry, BGRA. core_mutex held.
    std::vector<u8> PictureLocked(u32& width, u32& height) const {
        const auto& screen = last_frame.primary;
        if (!screen.pixels || screen.width == 0 || screen.height == 0) {
            return {};
        }
        width = screen.width;
        height = screen.height;
        const std::size_t stride = screen.stride_pixels ? screen.stride_pixels : width;
        std::vector<u8> out(static_cast<std::size_t>(width) * height * 4);
        for (std::size_t y = 0; y < height; ++y) {
            std::memcpy(out.data() + y * width * 4, screen.pixels + y * stride,
                        static_cast<std::size_t>(width) * 4);
        }
        if (!screen.bgra) {
            for (std::size_t i = 0; i < out.size(); i += 4) {
                std::swap(out[i], out[i + 2]);
            }
        }
        return out;
    }

    /// Tells that a slot's state or an autosave could not be made.
    void NotMade(const StateRequest& request, const std::string& path) {
        LOG_ERROR(Core, "AltCoreHost: no state for {}", path);
        const bool autosave = request.kind == StateRequest::Kind::Autosave;
        Core::Autosave::Writer::Done done;
        done.path = path;
        done.reason = request.reason;
        done.autosave = autosave;
        done.slot_save = !autosave;
        done.slot = autosave ? 0 : static_cast<u32>(request.slot);
        done.failure = Core::Autosave::Failure::Serialize;
        if (frontend.on_written) {
            frontend.on_written(done);
        }
    }

    /// Makes a slot's state or an autosave and hands it to the writer.
    bool MakeState(const StateRequest& request) {
        namespace Autosave = Core::Autosave;
        const bool autosave = request.kind == StateRequest::Kind::Autosave;
        const uint64_t now = UnixNow();
        Autosave::Writer::Job job;
        job.autosave = autosave;
        job.reason = request.reason;
        job.program_id = rom_info.title_id;
        if (autosave) {
            job.dir = Autosave::Dir(StatesDir());
            job.path = request.reason == Autosave::Reason::MenuOpen
                           ? Autosave::MenuPathFor(job.dir, job.program_id, 0)
                           : Autosave::PathFor(job.dir, job.program_id, 0,
                                               static_cast<s64>(now));
            job.keep = Autosave::ClampKeep(request.keep);
        } else {
            job.path = SlotPath(request.slot);
            job.slot_save = true;
            job.slot = static_cast<u32>(request.slot);
        }

        const auto t0 = std::chrono::steady_clock::now();
        std::vector<uint8_t> state;
        std::vector<u8> picture;
        u32 width = 0;
        u32 height = 0;
        bool ok = false;
        {
            std::lock_guard lock(core_mutex);
            ok = core && core->SaveStateToMemory(state);
            if (ok && autosave) {
                picture = PictureLocked(width, height);
            }
        }
        if (!ok) {
            NotMade(request, job.path);
            return false;
        }
        job.header = Core::CurrentStateHeader(job.program_id, now);
        job.hold = std::make_shared<Common::TransientMemory::Hold>(state.size());
        job.raw.assign(reinterpret_cast<const char*>(state.data()), state.size());
        job.serialize_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - t0)
                               .count();
        std::string thumb = Autosave::ThumbPathFor(job.path);
        writer.Submit(std::move(job));
        if (!picture.empty()) {
            writer.SubmitThumb(std::move(thumb), std::move(picture), width, height);
        }
        return true;
    }

    /// Reads a state file of this game into `state`.
    LoadResult ReadState(const StateRequest& request, const std::string& path,
                         std::vector<uint8_t>& state, LoadDone& done) {
        writer.WaitIdle(); // a save to this file may still be on its way
        const auto info = Core::GetSaveStateInfoAt(path, rom_info.title_id);
        done.time = info.time;
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in || (info.slot == 0 && request.time != 0 && info.time != request.time)) {
            return LoadResult::Gone;
        }
        std::vector<u8> file(static_cast<std::size_t>(in.tellg()));
        in.seekg(0);
        if (!in.read(reinterpret_cast<char*>(file.data()),
                     static_cast<std::streamsize>(file.size()))) {
            return LoadResult::Failed;
        }
        switch (Core::CheckStateHeader(file, rom_info.title_id, Common::g_scm_rev,
                                       request.anyway)) {
        case Core::HeaderCheck::Ok:
            break;
        case Core::HeaderCheck::NotAState:
            return LoadResult::Failed;
        case Core::HeaderCheck::OtherGame:
            return LoadResult::OtherGame;
        case Core::HeaderCheck::OtherBuild:
            done.version = Core::FrameVersionOf(info.build_name, info.build_version);
            return LoadResult::OtherBuild;
        }
        try {
            state = Common::Compression::DecompressDataZSTD(
                std::span<const u8>{file}.subspan(kStateHeaderSize));
        } catch (const std::exception& e) { // a damaged size
            LOG_ERROR(Core, "AltCoreHost: {} not read: {}", path, e.what());
            return LoadResult::Failed;
        }
        return state.empty() ? LoadResult::Failed : LoadResult::Ok;
    }

    /// Before a state from another build is loaded: the battery save, kept beside it.
    bool BackUpSaveLocked() {
        core->FlushBatterySave();
        std::error_code ec;
        const std::string save = SavePath();
        if (!std::filesystem::exists(save, ec)) {
            return true; // nothing to back up
        }
        std::filesystem::copy_file(save, save + ".before-load-anyway",
                                   std::filesystem::copy_options::overwrite_existing, ec);
        return !ec;
    }

    /// Puts a state in place; the core is told, also of one it read only part of.
    bool PutLocked(const std::vector<uint8_t>& state) {
        const bool ok = core->LoadStateFromMemory(state.data(), state.size());
        core->AfterStateLoad();
        // melonDS draws a frame's 3D ahead of it, and a state does not hold it. One frame, or three
        // where a game captures its 3D to show it later (Golden Sun: Dark Dawn).
        const bool ds = rom_info.kind == Common::AltCore::CoreKind::NDS ||
                        rom_info.kind == Common::AltCore::CoreKind::DSi;
        hidden_frames = ds ? 3 : 0;
        return ok;
    }

    /// Loads `state`; on failure the game is as it was.
    bool LoadLocked(const std::vector<uint8_t>& state, const UndoState& before) {
        if (PutLocked(state)) {
            return true;
        }
        // Part of it may have been read.
        if (!PutLocked(before.state)) {
            LOG_ERROR(Core, "AltCoreHost: the moment before the load could not be restored");
        }
        return false;
    }

    /// After a load: no sound of before it, and while paused a frame that is shown, so its picture
    /// is.
    void ShowLoadedLocked() {
        audio.Flush();
        if (headless || !paused.load(std::memory_order_acquire)) {
            return;
        }
        double ms = 0.0;
        for (uint32_t left = hidden_frames + 1; left > 0; --left) {
            ExecuteSingleFrameLocked(ms);
        }
        core->CloseMicrophone();
        audio.Flush();
        if (callbacks.on_rumble && last_rumble > 0.0f) {
            callbacks.on_rumble(0.0f);
            last_rumble = 0.0f;
        }
    }

    bool MomentLocked(UndoState& before) {
        before.time = UnixNow();
        return core->SaveStateToMemory(before.state);
    }

    bool Finish(const LoadDone& done) {
        const bool ok = done.result == LoadResult::Ok;
        if (!ok) {
            LOG_ERROR(Core, "AltCoreHost: not loaded (slot {}, result {}) {}", done.slot,
                      static_cast<int>(done.result), done.path);
        }
        if (frontend.on_loaded) {
            frontend.on_loaded(done);
        }
        return ok;
    }

    bool LoadState(const StateRequest& request) {
        LoadDone done;
        const bool autosave = request.kind == StateRequest::Kind::LoadAutosave;
        done.what = autosave ? LoadDone::What::Autosave : LoadDone::What::Slot;
        done.slot = request.slot;
        done.path = request.path;
        std::vector<uint8_t> state;
        done.result =
            ReadState(request, autosave ? request.path : SlotPath(request.slot), state, done);
        if (done.result == LoadResult::Ok) {
            done.result = LoadResult::Failed;
            std::lock_guard lock(core_mutex);
            UndoState before;
            if (core && (!request.anyway || BackUpSaveLocked()) && MomentLocked(before) &&
                LoadLocked(state, before)) {
                done.result = LoadResult::Ok;
                undo = std::move(before);
                ShowLoadedLocked();
            }
        }
        return Finish(done);
    }

    bool Undo() {
        LoadDone done;
        done.what = LoadDone::What::Undo;
        {
            std::lock_guard lock(core_mutex);
            const UndoState to = std::exchange(undo, {}); // one use
            UndoState before;
            if (core && !to.state.empty() && MomentLocked(before)) {
                done.time = to.time;
                // The import's save goes out first: a GB or GBA state leaves the file alone.
                bool ok = true;
                if (to.imported) {
                    last_frame = FrameOutput{}; // the core starts again
                    core->FlushBatterySave();
                    ok = core->ImportBatterySave(SavePath() + ".bak");
                }
                if (LoadLocked(ok ? to.state : before.state, before) && ok) {
                    done.result = LoadResult::Ok;
                }
                ShowLoadedLocked();
            }
        }
        return Finish(done);
    }

    bool Import(const std::string& path) {
        LoadDone done;
        done.what = LoadDone::What::Import;
        done.path = path;
        {
            std::lock_guard lock(core_mutex);
            UndoState before;
            if (core) {
                core->FlushBatterySave();
                std::error_code ec;
                before.imported = std::filesystem::exists(SavePath(), ec);
            }
            if (core && MomentLocked(before)) {
                last_frame = FrameOutput{}; // the core starts again
                if (core->ImportBatterySave(path)) {
                    core->AfterStateLoad();
                    done.result = LoadResult::Ok;
                    undo = std::move(before);
                } else {
                    // The core may have started again: back to where the game was.
                    PutLocked(before.state);
                }
                ShowLoadedLocked();
            }
        }
        return Finish(done);
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

    /// core_mutex held.
    void NoteOptions(uint32_t has, uint32_t on) {
        caps.driver_options = has;
        caps.driver_options_on = on;
        if (callbacks.on_capabilities) {
            callbacks.on_capabilities(caps);
        }
    }

    void Present(bool skippable) {
        if (renderer && renderer->IsReady() && last_frame.primary.pixels) {
            renderer->Present(last_frame.primary, last_frame.secondary, last_frame.overlay,
                              skippable);
        }
    }

    void ExecuteSingleFrameLocked(double& frame_time_ms_out) {
        const Common::AltCore::InputState in = ReadInput();

        const auto t0 = std::chrono::steady_clock::now();

        const bool page = (in.buttons & Common::AltCore::BtnTurnPage) != 0;
        if (page && !page_before) {
            core->TurnPage();
        }
        page_before = page;

        core->SetInput(in);
        core->RunFrame(last_frame);
        if (mic_blow_frames > 0 && --mic_blow_frames == 0) {
            core->SetMicBlow(false);
        }
        holding.store(last_frame.held, std::memory_order_relaxed);

        if (last_frame.metrics_changed) {
            caps.screens = last_frame.updated_metrics;
            if (callbacks.on_screen_metrics) {
                callbacks.on_screen_metrics(caps.screens);
            } else if (callbacks.on_capabilities) {
                callbacks.on_capabilities(caps);
            }
        }

        if (last_frame.options_changed) {
            NoteOptions(last_frame.updated_options, last_frame.updated_options_on);
        }

        audio.SetCoreSampleRate(core->AudioSampleRate());
        if (!last_frame.audio_pcm.empty()) {
            audio.Push(last_frame.audio_pcm.data(), last_frame.audio_pcm.size() / 2u);
        }

        NoteScreens();
        if (hidden_frames > 0) {
            --hidden_frames;
        } else {
            Present(fast_forward.load(std::memory_order_relaxed));
        }

        const float rumble = last_frame.rumble;
        if (callbacks.on_rumble && (rumble > 0.0f || last_rumble > 0.0f)) {
            callbacks.on_rumble(rumble);
            last_rumble = rumble;
        }

        const auto t1 = std::chrono::steady_clock::now();
        frame_time_ms_out =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        last_frame_ns.store(SteadyNowNs(), std::memory_order_relaxed);
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
            RunRequests(false);
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

            // A held game's menu is read and drawn at the game's own pace, whatever speed is set.
            const bool held = holding.load(std::memory_order_relaxed);
            if (!held) {
                ++window_frames;
                window_frame_ms_sum += frame_ms;
                window_peak_ms = std::max(window_peak_ms, frame_ms);
                if (frame_ms > (nominal_frame_us / 1000.0) * 1.25) {
                    ++window_slow;
                }
            }

            const bool is_ff = !held && fast_forward.load(std::memory_order_relaxed);
            // Percent; 0 is no limit. Emulation Speed applies as to a 3DS game.
            const double limit =
                held    ? 100.0
                : is_ff ? static_cast<double>(ff_speed_pct.load(std::memory_order_relaxed))
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
        RunRequests(true); // the quit's autosave
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
    impl->paths = BuildCorePaths(impl->rom_info, data_dir);
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

    if (!headless_for_test) {
        impl->renderer = std::make_unique<Vulkan::AltXrRenderer>();
        if (!impl->renderer->Init(impl->frontend.window, impl->frontend.secondary_window)) {
            // the game would play with sound and no picture
            LOG_ERROR(Render_Vulkan, "AltCoreHost: no renderer, '{}' is not started",
                      impl->rom_info.title);
            impl->renderer.reset();
            impl->core->Unload();
            impl->core.reset();
            return false;
        }
        impl->renderer->SetSharpPixels(!Common::AltCore::IsDs(impl->rom_info.kind));
    }

    impl->caps = impl->core->GetCapabilities();
    impl->last_frame = FrameOutput{};
    impl->page_before = false;
    impl->hidden_frames = 0;
    impl->last_rumble = 0.0f;
    impl->mic_blow_frames = 0;
    impl->screens = {};
    impl->paused.store(false, std::memory_order_relaxed);
    impl->redraw.store(false, std::memory_order_relaxed);
    impl->holding.store(false, std::memory_order_relaxed);
    impl->fast_forward.store(false, std::memory_order_relaxed);
    impl->stop_requested.store(false, std::memory_order_relaxed);
    {
        std::lock_guard slock(impl->stats_mutex);
        impl->stats = HostStats{};
    }

    // Configure XR frame pacer for the active core's native frame duration
    const double frame_us = 1'000'000.0 / impl->core->NominalFps();
    SetPacerEmuFrameUs(Common::XrPacer::Get(), frame_us);

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

    impl->last_frame_ns.store(0, std::memory_order_relaxed);
    // A game's last state may still be written after it stopped: its own callback each.
    impl->writer.SetOnDone(
        [host = impl.get(), written = impl->frontend.on_written](const auto& done) {
            host->write_ok.store(done.ok);
            if (written) {
                written(done);
            }
        });
    if (!impl->writer.Busy()) { // what a write cut short left
        Core::RemoveStaleStateTemps(impl->StatesDir(), impl->rom_info.title_id);
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
        impl->emu_thread.join(); // makes a state asked for before (the writer writes it on its own)
    }
    {
        std::lock_guard lock(impl->request_mutex);
        impl->requests.clear();
        impl->requests_open.store(0, std::memory_order_release);
    }
    {
        std::lock_guard lock(impl->core_mutex);
        impl->undo = {};
    }

    if (was_running) {
        {
            std::lock_guard lock(impl->core_mutex);
            if (impl->core) {
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
        if (impl->callbacks.on_title_id) {
            impl->callbacks.on_title_id(0); // no running title: its placement key is not kept
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
    if (paused) {
        // A save made just before pausing would otherwise wait in memory for the next frame.
        std::lock_guard lock(impl->core_mutex);
        if (impl->core) {
            impl->core->FlushBatterySave();
        }
    }
    if (paused && impl->callbacks.on_rumble && impl->last_rumble > 0.0f) {
        impl->callbacks.on_rumble(0.0f);
        impl->last_rumble = 0.0f;
    }
}

bool AltCoreHost::IsPaused() const {
    return impl->paused.load(std::memory_order_acquire);
}

bool AltCoreHost::IsHeld() const {
    return impl->holding.load(std::memory_order_relaxed);
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
        impl->undo = {};
        impl->core->Reset();
        impl->restarts.fetch_add(1, std::memory_order_acq_rel);
        impl->audio.Flush();
    }
}

void AltCoreHost::SetFastForward(bool on, uint32_t speed_pct) {
    impl->ff_speed_pct.store(speed_pct, std::memory_order_relaxed);
    impl->fast_forward.store(on, std::memory_order_relaxed);
    impl->audio.SetFastForward(on, Settings::values.fast_forward_audio.GetValue(),
                               static_cast<double>(speed_pct) / 100.0);
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
    double ms_sum = 0.0;
    double peak = 0.0;
    for (int i = 0; i < n; ++i) {
        if (impl->headless && impl->requests_open.load(std::memory_order_acquire) > 0) {
            impl->RunRequests(false); // as the emulation thread, between two frames
            impl->writer.WaitIdle();
        }
        std::lock_guard lock(impl->core_mutex);
        if (!impl->core || !impl->running.load(std::memory_order_acquire)) {
            return false;
        }
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
    return SaveSlot(0);
}

bool AltCoreHost::QuickLoad() {
    return LoadSlot(0);
}

bool AltCoreHost::SaveSlot(int slot) {
    if (slot < 0 || slot > 10) {
        return false;
    }
    StateRequest request;
    request.kind = StateRequest::Kind::Save;
    request.slot = slot;
    return impl->Ask(std::move(request));
}

bool AltCoreHost::LoadSlot(int slot, bool anyway) {
    if (slot < 0 || slot > 10) {
        return false;
    }
    StateRequest request;
    request.kind = StateRequest::Kind::Load;
    request.slot = slot;
    request.anyway = anyway;
    return impl->Ask(std::move(request));
}

bool AltCoreHost::MakeAutosave(Core::Autosave::Reason reason, uint32_t keep) {
    StateRequest request;
    request.kind = StateRequest::Kind::Autosave;
    request.reason = reason;
    request.keep = keep;
    return impl->Ask(std::move(request));
}

bool AltCoreHost::LoadAutosave(const std::string& path, uint64_t time, bool anyway) {
    StateRequest request;
    request.kind = StateRequest::Kind::LoadAutosave;
    request.path = path;
    request.time = time;
    request.anyway = anyway;
    return impl->Ask(std::move(request));
}

bool AltCoreHost::UndoLoad() {
    StateRequest request;
    request.kind = StateRequest::Kind::Undo;
    return impl->Ask(std::move(request));
}

bool AltCoreHost::ImportSave(const std::string& sav_path) {
    StateRequest request;
    request.kind = StateRequest::Kind::Import;
    request.path = sav_path;
    return impl->Ask(std::move(request));
}

void AltCoreHost::PruneAutosaves(uint32_t keep) {
    StateRequest request;
    request.kind = StateRequest::Kind::Prune;
    request.keep = keep;
    impl->Ask(std::move(request));
}

bool AltCoreHost::StatesBusy() const {
    return impl->requests_open.load(std::memory_order_acquire) > 0;
}

std::string AltCoreHost::StatesDir() const {
    return impl->StatesDir();
}

bool AltCoreHost::FramesRecent() const {
    const int64_t last = impl->last_frame_ns.load(std::memory_order_relaxed);
    return last > 0 && SteadyNowNs() - last < 1'000'000'000;
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

int AltCoreHost::ImportCheats(const std::vector<CheatEntry>& cheats) {
    std::lock_guard lock(impl->core_mutex);
    return impl->core ? impl->core->ImportCheats(cheats) : 0;
}

std::string AltCoreHost::RomPath() const {
    std::lock_guard lock(impl->core_mutex);
    return impl->core ? impl->rom_path : std::string{};
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

void AltCoreHost::SetEditing(bool on) {
    std::lock_guard lock(impl->core_mutex);
    if (impl->core) {
        impl->core->SetEditing(on);
    }
}

void AltCoreHost::TurnPage() {
    std::lock_guard lock(impl->core_mutex);
    if (impl->core) {
        impl->core->TurnPage();
    }
}

bool AltCoreHost::SetDriverOption(Common::AltCore::DriverOption option, uint32_t value) {
    std::lock_guard lock(impl->core_mutex);
    if (!impl->core) {
        return false;
    }
    const bool taken = impl->core->SetDriverOption(option, value);
    const auto now = impl->core->GetCapabilities();
    if (now.driver_options != impl->caps.driver_options ||
        now.driver_options_on != impl->caps.driver_options_on) {
        impl->NoteOptions(now.driver_options, now.driver_options_on);
    }
    return taken;
}

void AltCoreHost::SetMicBlow(bool on) {
    std::lock_guard lock(impl->core_mutex);
    impl->mic_blow_frames = 0;
    if (impl->core) {
        impl->core->SetMicBlow(on);
    }
}

void AltCoreHost::BlowMic() {
    std::lock_guard lock(impl->core_mutex);
    if (impl->core) {
        impl->core->SetMicBlow(true);
        impl->mic_blow_frames = kMicBlowFrames;
    }
}

int AltCoreHost::MicBlowFramesLeft() const {
    std::lock_guard lock(impl->core_mutex);
    return impl->mic_blow_frames;
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
    impl->undo = {};                  // a state of the other console
    impl->core->SetConsoleMode(dsi);
    impl->caps = impl->core->GetCapabilities();
    impl->audio.Flush();
    if (impl->callbacks.on_capabilities) {
        impl->callbacks.on_capabilities(impl->caps);
    }
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
    }
    return false;
}

} // namespace FrameCores
