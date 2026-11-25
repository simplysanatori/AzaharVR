// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#pragma once

// The parts of the PokeDaisy driver that need none of PokeDaisy's headers: the edit gate and the
// small rules around it. MakePokeDaisyDriver and PokeDaisyKnows are declared in frame_driver.h.

#ifdef HAVE_POKEDAISY

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "common/alt_core_types.h"

namespace FrameCores {
class FrameDriver;
}

namespace FrameCores::PokeDaisy {

/// Everything the companion can ask for, by what it touches.
enum class Ask : uint8_t {
    Settings,  ///< a settings row: the gate holds the rows that edit (Toggles)
    Nuzlocke,  ///< its own file only
    ItemPick,  ///< USE / GIVE: who it can go to. Calls the game.
    ItemApply, ///< USE / GIVE
    ItemTake,
    BoxMove, ///< withdraw, swap, deposit
    PartySwitch,
    TradeEvolve,
    FlyList, ///< where FLY can go. Calls the game.
    FlyTo,
    Flight, ///< TAKE OFF / LAND
    Pc,
    Repel,
    Bike,
    Flash,
    Toggle,   ///< a Toggles field
    State,    ///< PokeDaisy's own save states and their undo: ours are used
    FastMode, ///< its fast forward: ours is used
    Leave,    ///< close or restart the game: the app's
    Count,
};

enum class Need { Nothing, Editing, Never };

constexpr Need NeedOf(Ask ask) {
    switch (ask) {
    case Ask::Settings:
    case Ask::Nuzlocke:
        return Need::Nothing;
    case Ask::ItemPick:
    case Ask::ItemApply:
    case Ask::ItemTake:
    case Ask::BoxMove:
    case Ask::PartySwitch:
    case Ask::TradeEvolve:
    case Ask::FlyList:
    case Ask::FlyTo:
    case Ask::Flight:
    case Ask::Pc:
    case Ask::Repel:
    case Ask::Bike:
    case Ask::Flash:
    case Ask::Toggle:
        return Need::Editing;
    case Ask::State:
    case Ask::FastMode:
    case Ask::Leave:
    case Ask::Count:
        break;
    }
    return Need::Never;
}

/// Settings that act on the game by themselves.
struct Toggles {
    bool wild_off = false;
    bool run_indoors = false;
    bool auto_run = false; ///< keys only
    bool reusable_tms = false;
    bool instant_text = false;
    bool trade_evos = false;

    bool operator==(const Toggles&) const = default;
    /// One of them writes, or calls the game, without a further ask.
    bool Writes() const {
        return wild_off || run_indoors || reusable_tms || instant_text || trade_evos;
    }
};

/// Which Toggles field a driver option is; null for one that is none of them.
bool Toggles::*FieldOf(Common::AltCore::DriverOption option);

/// What the session sees of the player's toggles: neutral values wherever they may not act.
Toggles ShownToggles(const Toggles& wanted, bool editing, bool open);

/// `kept` with the fields a page changed: those where `now` differs from what it was shown.
Toggles ChangedToggles(const Toggles& kept, const Toggles& shown, const Toggles& now);

/// The one place that says whether the game may be written.
///
/// Off      nothing writes: no ask is taken, the session gets neutral toggles and no flight
///          configuration, the battle's cursor write is refused, and while it runs the core's bus
///          writes and single step are replaced by refusals (a backstop for a path not listed).
/// On       the same until the first edit has the battery save backed up; a failed backup keeps it
///          so for the whole session.
/// Closing  turned off with a step out that would hold the game if it were cut (between FLY's fade
///          and its warp, in the air, ...): only that step runs on, nothing new is taken.
class EditGate {
public:
    explicit EditGate(std::function<bool()> backup_save);

    /// `step_out`: something is half done in the game.
    void SetEditing(bool on, bool step_out);
    /// What Closing waited for is done.
    void StepsDone();

    /// Whether `ask` may go on. Makes the backup if it is the first edit.
    bool Allows(Ask ask);
    /// Whether the game may be written from here on. Makes the backup if it is the first edit.
    bool Admit();

    /// Whether to offer what edits.
    bool Editing() const {
        return state == State::On && backup != Backup::Refused;
    }
    /// Whether the game may be written now.
    bool Open() const {
        return state == State::Closing || (state == State::On && backup == Backup::Made);
    }
    bool Closing() const {
        return state == State::Closing;
    }

private:
    enum class State { Off, On, Closing };
    enum class Backup { NotYet, Made, Refused };

    std::function<bool()> backup_save;
    State state = State::Off;
    Backup backup = Backup::NotYet;
};

/// The quick menu's items, as PokeDaisy numbers them (quick_action).
enum class Quick : uint8_t {
    Pc,
    Fly,
    Repel,
    Bike,
    Wild,
    SaveState,
    LoadState,
    Ff,
    Companion,
    Hud,
    Flash,
    Flight,
    Land,
    Count,
};

constexpr Ask AskOfItem(Quick item) {
    switch (item) {
    case Quick::Pc:
        return Ask::Pc;
    case Quick::Fly:
        return Ask::FlyList; // and FlyTo, once a place is picked
    case Quick::Repel:
        return Ask::Repel;
    case Quick::Bike:
        return Ask::Bike;
    case Quick::Wild:
        return Ask::Toggle;
    case Quick::Flash:
        return Ask::Flash;
    case Quick::Flight:
    case Quick::Land:
        return Ask::Flight;
    case Quick::SaveState:
    case Quick::LoadState:
        return Ask::State;
    case Quick::Ff:
        return Ask::FastMode;
    case Quick::Companion:
    case Quick::Hud:
    case Quick::Count:
        break;
    }
    return Ask::Count;
}

/// Which of the session's items the menu has, by index: those that can be taken.
std::vector<int> MenuItems(const std::vector<Quick>& all, bool editing);

/// How much of PokeDaisy's 400x240 the game's picture has across, over the full height: 360 for a
/// GBA game, 266 for a Game Boy one. The overlay is that much, so it covers the Primary Screen.
constexpr int OverlayWidth(uint32_t game_width, uint32_t game_height) {
    if (game_height == 0) {
        return 400;
    }
    const uint32_t width = game_width * 240u / game_height;
    return width < 400u ? static_cast<int>(width) : 400;
}

/// One pixel of PokeDaisy's overlay (ARGB, straight alpha) as DriverFrame::overlay has them (RGBA,
/// premultiplied). `dim`: over the dark the open menu lays on the game.
constexpr uint32_t OverlayPixel(uint32_t px, bool dim) {
    const uint32_t a = px >> 24;
    const uint32_t behind = dim ? 0x55u : 0u;
    const auto part = [a](uint32_t c) { return ((c & 0xFFu) * a + 127u) / 255u; };
    const uint32_t alpha = a + (behind * (255u - a) + 127u) / 255u;
    return (alpha << 24) | (part(px) << 16) | (part(px >> 8) << 8) | part(px >> 16);
}

/// What keeps a save state from being made or loaded.
struct Pending {
    bool asked = false;    ///< an ask not taken yet
    bool step = false;     ///< FLY or the TM screen between its two steps
    bool job = false;      ///< PC, Repel, bike
    bool keys = false;     ///< keys pressed for the game
    bool flight = false;   ///< in the air
    bool closing = false;  ///< EditGate's Closing
};
bool StateSafe(const Pending& pending);

/// Whether the game stands still this frame: the quick menu is open, and nothing PokeDaisy has
/// under way counts frames or waits for the game. In the air it may.
bool HoldsFrame(bool menu_open, const Pending& pending);

/// pd_ui_touch's calls from a touch that is only a level.
class TouchEdges {
public:
    struct Event {
        int x = 0;
        int y = 0;
        bool down = false;
    };
    /// True with the event to send: every frame while held, then once when let go.
    bool Next(bool touching, int x, int y, Event& event);
    void Forget() {
        held = false;
    }

private:
    bool held = false;
    int last_x = 0;
    int last_y = 0;
};

/// The sound kept from one frame, in sample frames: calls into the game run after the frame and
/// add to it, so the tail is theirs.
std::size_t AudioKept(std::size_t frames, unsigned rate);

/// 0xRRGGBB to RGBA8888 with alpha.
constexpr uint32_t CanvasToRgba(uint32_t px) {
    return 0xFF000000u | ((px & 0xFFu) << 16) | (px & 0xFF00u) | ((px >> 16) & 0xFFu);
}

/// What every settings value the host owns is held at.
struct HeldSettings {
    int language;
    int aspect;
    int game_speed;
    int scene_speeds; ///< the sum
    bool fly_classic;
    bool hud; ///< the player's
    bool cheats;
    bool rewind;
    Toggles toggles;
};
/// For tests: PokeDaisy's defaults, as the driver holds them.
HeldSettings DefaultSettings();

/// A way in beside the quick menu, through the same gate. Only between RunFrame calls.
class Actions {
public:
    virtual ~Actions() = default;

    /// The game has it and the player may use it. Not whether the moment is right.
    virtual bool Offers(Ask ask) const = 0;
    /// Pc, Repel, Bike, Flash, Flight. False: not taken.
    virtual bool Do(Ask ask) = 0;
    /// The player's, whether or not it acts now.
    virtual Toggles GetToggles() const = 0;
    virtual bool SetToggles(const Toggles& toggles) = 0;
};
/// Null if `driver` is not PokeDaisy's.
Actions* ActionsOf(FrameDriver& driver);

} // namespace FrameCores::PokeDaisy

#endif // HAVE_POKEDAISY
