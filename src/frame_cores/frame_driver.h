// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct mCore;
struct Emu; // gba-ds's emu.h

namespace FrameCores {

struct RomInfo;

/// What runs a GBA or Game Boy game's frames. One is chosen when the game loads and stays until it
/// unloads: what draws the second screen also decides when the game's frame runs, so no two of
/// these ever share a game.
enum class DriverKind {
    Plain,     ///< the core's own frame, with gba-ds's map capture
    PokeDaisy, ///< PokeDaisy's session
    GbaDs,     ///< gba-ds's game.c
};

/// Settings::values.gba_second_screen
enum class SecondScreen : uint32_t { PokeDaisy = 0, GbaDs = 1, Off = 2 };

struct DriverInput {
    uint32_t keys = 0; ///< GBA keys, bits 0..9
    bool touching = false;
    uint16_t touch_x = 0; ///< in the Secondary Screen's pixels
    uint16_t touch_y = 0;
};

/// A picture the driver owns, valid until its next RunFrame. RGBA8888, alpha set.
struct DriverPicture {
    const uint32_t* pixels = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0; ///< pixels
};

struct DriverFrame {
    DriverPicture primary;   ///< the game
    DriverPicture secondary; ///< none: no Secondary Screen now
    DriverPicture overlay;   ///< drawn over the Primary Screen on its own layer, with alpha
    std::vector<int16_t> audio_pcm; ///< interleaved stereo
    float rumble = 0.0f;
};

/// What the core gives a driver. Never used by two threads at once: from the thread that calls
/// the driver, or from a thread of the driver's own while that call waits for it. A driver is made,
/// reset and destroyed on other threads than RunFrame's, one at a time.
struct DriverServices {
    mCore* core = nullptr;
    Emu* emu = nullptr; ///< GBA: gba-ds's wrapper of `core`
    /// One emulated frame with these keys, as the core's own path runs it. A driver calls this
    /// exactly once in RunFrame; the core counts the frames and logs anything else. It calls
    /// core->runFrame: a driver that hooks that puts the core's own back around this.
    std::function<void(uint32_t keys)> step;
    /// The sound made since the last call.
    std::function<std::size_t(int16_t* out, std::size_t max_frames)> drain_audio;
    std::function<float()> rumble;
    /// The game's picture as the core drew it: the core's own buffer, the same until the game is
    /// unloaded, its alpha not set. A Game Boy's is DriverBufferWidth pixels wide.
    std::function<DriverPicture()> picture;
    /// Copies the battery save aside, at most once a session, before the first edit. False: not
    /// copied, and nothing may be edited.
    std::function<bool()> backup_save;
    std::string rom_path;
    std::string game_dir; ///< the game's own folder (GameFolder)
    bool map_capture = false; ///< the Plain driver: gba-ds's maps
};

class FrameDriver {
public:
    /// Puts every static it used back, so the next game starts clean.
    virtual ~FrameDriver() = default;

    /// `out` comes empty.
    virtual void RunFrame(const DriverInput& in, DriverFrame& out) = 0;
    /// The game was reset (the core already is).
    virtual void Reset() = 0;

    /// False: a state made or loaded now would catch the game half changed. The host drops an
    /// autosave and puts off a save or a load until this is true. Asked between frames.
    virtual bool StateSafeNow() const {
        return true;
    }
    /// A state was put in place behind the driver's back (a load, a failed load put back, Undo
    /// Load, Import Save), or part of one was. Between frames.
    virtual void AfterStateLoad() {}

    /// Allow Game Editing means something for this driver. Read when the game is loaded.
    virtual bool CanEdit() const {
        return false;
    }
    /// Allow Game Editing. Off (as a driver starts), the driver never writes to the game. Between
    /// frames.
    virtual void SetEditing(bool /*on*/) {}
    /// The size of the Secondary Screen the driver draws, {0, 0} for none.
    virtual std::pair<uint32_t, uint32_t> SecondarySize() const = 0;
    /// Read when the game is loaded.
    virtual bool HasTouch() const = 0;
};

/// Which of the driver and the second screen a game uses, from the player's choice. A choice the
/// game has nothing for falls to the next that it has. `pokedaisy_known`: this exact game (by
/// SHA-1, never by game code alone). `gbads_known`: gba-ds has a Secondary Screen for it (its
/// profiles go by game code).
DriverKind ChooseDriver(SecondScreen choice, bool pokedaisy_known, bool gbads_known);

/// How many pixels wide a core's buffer is for a driver, at least `width` (the game's own).
uint32_t DriverBufferWidth(DriverKind kind, uint32_t width);

std::unique_ptr<FrameDriver> MakePlainDriver(const DriverServices& services, const RomInfo& info);
#ifdef HAVE_POKEDAISY
/// Whether PokeDaisy knows this exact ROM and reads it with its own configuration.
bool PokeDaisyKnows(const std::string& rom_path);
std::unique_ptr<FrameDriver> MakePokeDaisyDriver(const DriverServices& services);
#endif
#ifdef HAVE_GBADS_GAME
/// Null: nothing here for this game, or game.c did not start.
std::unique_ptr<FrameDriver> MakeGbaDsDriver(const DriverServices& services);
#endif

} // namespace FrameCores
