// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "common/alt_core_types.h"
#include "core/autosave.h"
#include "core/frontend/alt_screens.h"
#include "frame_cores/audio_resampler.h"
#include "frame_cores/core_backend.h"
#include "frame_cores/rom_info.h"

namespace Frontend {
class EmuWindow;
}

namespace FrameCores {

/// What a load of a save state came to. Nothing but Ok changes the game.
enum class LoadResult {
    Ok,
    Gone,       ///< no such file, or another state in it since the list showed it
    OtherGame,  ///< its key is another game's
    OtherBuild, ///< made by another build of Azahar, and not asked for `anyway`
    Failed,     ///< damaged, or the core refused it
};

struct LoadDone {
    enum class What { Slot, Autosave, Undo, Import };
    What what = What::Slot;
    LoadResult result = LoadResult::Failed;
    int slot = 0;
    std::string path;    ///< Autosave: its file
    uint64_t time = 0;   ///< when the loaded state was made (Unix seconds)
    std::string version; ///< OtherBuild: Core::FrameVersionOf
};

struct HostStats {
    double game_fps = 0.0;
    double emulation_speed = 0.0; ///< 1.0 = 100%
    double frame_ms = 0.0;
    double frame_peak_ms = 0.0;
    uint32_t slow_frames = 0;
    uint64_t total_frames = 0;
    double audio_buffered_ms = 0.0;
};

/// Optional frontend callbacks (e.g. wired to FrameXr and GMainWindow).
struct HostCallbacks {
    std::function<void(const Common::AltCore::Capabilities&)> on_capabilities;
    std::function<void(const Common::AltCore::ScreenMetrics&)> on_screen_metrics;
    std::function<void(uint64_t)> on_title_id;
    std::function<void(Common::AltCore::InputSink)> on_input_sink;
    std::function<void(float)> on_rumble;
    std::function<void(bool)> on_game_running;
    std::function<void(const HostStats&)> on_stats;
};

/// What the window gives a game: where it is drawn while no headset shows it, and its controls.
struct HostFrontend {
    Frontend::EmuWindow* window = nullptr; ///< both outlive the game
    Frontend::EmuWindow* secondary_window = nullptr;
    /// The controls profile's state, once a frame (emulation thread).
    std::function<Common::AltCore::InputState()> input;
    MicrophoneFactory microphone;
    /// The size of a screen changed, or a Secondary Screen came (emulation thread).
    std::function<void(const Layout::AltScreens&)> on_screens;
    /// A slot or an autosave was written, or not (the writer's thread; the emulation thread when
    /// the core could not make the state).
    std::function<void(const Core::Autosave::Writer::Done&)> on_written;
    /// A load, Undo Load or Import Save was done, or not (emulation thread).
    std::function<void(const LoadDone&)> on_loaded;
};

enum class AltMenuSetting : int {
    SolarLevel = 0,       ///< 0..10 (GBA solar sensor)
    MicBlow = 1,          ///< 0 or 1 (DS/DSi microphone blow toggle)
    LidClosed = 2,        ///< 0 or 1 (DS/DSi sleep lid switch)
    TouchEnabled = 3,     ///< 0 or 1 (DS/DSi touch screen input)
    ScreenRotation = 4,   ///< 0, 90, 270 (DS/DSi Book Mode)
    ConsoleModeDsi = 5,   ///< 0 = DS, 1 = DSi
    FastForwardSpeed = 6, ///< percent (0 = uncapped, 100..1000)
};

enum class AltMenuCommand : int {
    Continue = 0,
    QuickSave = 1,
    QuickLoad = 2,
    SaveSlot = 3,      ///< `value`: slot 1..10
    LoadSlot = 4,      ///< `value`: slot 1..10
    Restart = 5,
    CloseGame = 6,
    ToggleCheat = 7,   ///< `value`: cheat index
    AddCheat = 8,      ///< `text`: "name\ncode" or raw code
    ImportSave = 9,    ///< `text`: path to .sav file
    SetGbaSlotRom = 10,///< `text`: path to .gba ROM (empty to eject)
};

/**
 * High-level host for non-3DS cores (GBA, GB/GBC, DS/DSi), owned by GMainWindow
 * during active play or constructed standalone in headless unit tests.
 *
 * Manages core selection, XR frame pacing, Vulkan::AltXrRenderer presentation,
 * AudioResampler output, controller/touch input delivery, save states,
 * cheats, and clean teardown back to 3DS defaults.
 */
class AltCoreHost {
public:
    AltCoreHost();
    ~AltCoreHost();

    AltCoreHost(const AltCoreHost&) = delete;
    AltCoreHost& operator=(const AltCoreHost&) = delete;

    /// Installs process-wide default callbacks used by newly constructed hosts
    /// when per-instance callbacks are not explicitly overridden.
    static void SetDefaultCallbacks(HostCallbacks callbacks);

    /// Sets callbacks for this host instance (must be called before Boot).
    void SetCallbacks(HostCallbacks callbacks);

    /// Sets the window and the controls (must be called before Boot).
    void SetFrontend(HostFrontend frontend);

    /// Boots a GBA, GB/GBC, DS, or DSi ROM (plain or inside .zip).
    /// When `headless_for_test` is true, skips Vulkan XR renderer creation, uses the
    /// null audio sink, and does not spawn the emulation thread (drive with StepFrames).
    bool Boot(const std::string& rom_path, const std::string& data_dir,
              bool headless_for_test = false, const CoreBootOptions& opts = {});

    /// Stops emulation cleanly, flushes battery SRAM, tears down audio/renderer/core, and
    /// resets XrPacer & capabilities to N3DS.
    void Stop();

    bool IsRunning() const;
    bool IsHeadless() const;

    void Pause(bool paused);
    bool IsPaused() const;
    /// The game stands still behind a menu of its driver's (FrameOutput::held): as paused, but the
    /// frames go on being shown.
    bool IsHeld() const;

    /// Draws the last frame again, for a paused game whose window or layout changed.
    void Redraw();

    /// Calls `use` with the screens of the last frame, whose pixels are valid during the call.
    void WithScreens(const std::function<void(const FrameOutput&)>& use) const;

    void Reset();

    void SetFastForward(bool on, uint32_t speed_pct = 200);
    bool IsFastForward() const;

    /// Delivers the latest touch snapshot in VR (thread-safe; called from XR thread). Its buttons
    /// are added to HostFrontend::input's.
    void PushInput(const Common::AltCore::InputState& input);

    /// Synchronously advances `n` frames (intended for headless unit tests).
    bool StepFrames(int n);

    /// Returns the most recent FrameOutput produced by StepFrames / emulation loop.
    const FrameOutput& LastFrame() const;

    /// Exposes the AudioResampler for inspection in tests.
    AudioResampler& GetAudioResampler();

    // Save states: the 3DS's files (core/savestate.h, core/autosave.h) in the game's own folder,
    // with its key as the program ID. Any thread may ask; nothing here waits for the state.
    //   Emulation thread: makes and loads states between two frames, also while paused.
    //   Writer's thread (Core::Autosave::Writer): compresses, writes atomically, prunes.
    // While the game is being changed (CoreBackend::StateSafeNow) an autosave is dropped, and the
    // rest wait for a frame after which it is not.
    // True: asked for; HostFrontend::on_written and on_loaded tell how it went. Headless there is
    // no emulation thread: done here, true when it went well; what waits is done by StepFrames.
    bool QuickSave();
    bool QuickLoad();
    bool SaveSlot(int slot);
    /// `anyway`: a state from another build too.
    bool LoadSlot(int slot, bool anyway = false);
    bool MakeAutosave(Core::Autosave::Reason reason, uint32_t keep);
    /// `time`, when not 0: the header time the list showed.
    bool LoadAutosave(const std::string& path, uint64_t time, bool anyway = false);
    /// Back to the moment before the last load (a slot, an autosave or Import Save), once.
    bool UndoLoad();
    bool ImportSave(const std::string& sav_path);
    /// Keep was lowered: the oldest autosaves go.
    void PruneAutosaves(uint32_t keep);
    /// A state is asked for and not made or loaded yet.
    bool StatesBusy() const;
    /// The game's folder, with a trailing separator; its autosaves are in Core::Autosave::Dir of it.
    std::string StatesDir() const;
    /// A frame ran in the last second.
    bool FramesRecent() const;

    // Runtime inspection & controls
    const RomInfo& GetRomInfo() const;
    Common::AltCore::Capabilities GetCapabilities() const;
    HostStats GetStats() const;

    std::vector<CheatEntry> ListCheats() const;
    bool SetCheatEnabled(std::size_t idx, bool on);
    bool AddCheat(const std::string& name, const std::string& code);
    /// Downloaded cheats, off (CoreBackend::ImportCheats); returns how many were added.
    int ImportCheats(const std::vector<CheatEntry>& cheats);
    /// The ROM file the game runs from ("" when none runs).
    std::string RomPath() const;

    /// The microphone's settings changed.
    void ReloadMicrophone();

    void SetSolarLevel(int level);
    void SetEditing(bool on);
    /// The Secondary Screen's next page, from the next frame. BtnTurnPage does it too.
    void TurnPage();
    /// An option of the driver's: RotatePages and MapRefresh hold for a driver made later too.
    bool SetDriverOption(Common::AltCore::DriverOption option, uint32_t value);
    void SetMicBlow(bool on);
    void SetLidClosed(bool closed);
    void SetTouchEnabled(bool on);
    void SetScreenRotation(int deg);
    bool SetGbaSlotRom(const std::string& path);
    void SetConsoleMode(bool dsi);

    bool HandleMenuSetting(AltMenuSetting setting, int value);
    bool HandleMenuCommand(AltMenuCommand command, int value = 0, const std::string& text = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace FrameCores
