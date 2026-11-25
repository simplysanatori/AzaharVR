// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#ifdef HAVE_POKEDAISY

#include "frame_cores/pokedaisy_driver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "common/alt_core_types.h"
#include "common/layer_depth.h"
#include "common/logging/log.h"
#include "frame_cores/frame_driver.h"
#include "frame_cores/rom_info.h"

#include <mgba/core/core.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gba/gba.h>

// The pokedaisy target builds with it; the declarations it hides are needed for the clean-up.
#ifndef PK_WIDE_CACHE
#define PK_WIDE_CACHE
#endif

// PokeDaisy's headers have no extern "C" of their own.
extern "C" {
#include "3ds/core/pd_canvas.h"
#include "3ds/core/pd_font.h"
#include "3ds/core/pd_game.h"
#include "3ds/core/pd_romart.h"
#include "3ds/core/pd_settings.h"
#include "3ds/core/pd_sha1.h"
#include "3ds/core/pd_tr.h"
#include "3ds/host/pd_items.h"
#include "3ds/host/pd_mixer.h"
#include "3ds/host/pd_session.h"
#include "app/src/main/cpp/pk_call.h"
#include "app/src/main/cpp/pk_flight.h"
#include "app/src/main/cpp/pk_host.h"
#include "app/src/main/cpp/pk_wide.h"
#include "linux/field_items.h"
#include "linux/fly.h"
#include "linux/gb_field.h"
#include "linux/hud.h"
#include "linux/quick_menu.h"
}

namespace FrameCores {

namespace PokeDaisy {

Toggles ShownToggles(const Toggles& wanted, bool editing, bool open) {
    Toggles shown;
    shown.auto_run = editing && wanted.auto_run; // keys: no backup needed
    if (editing && open) {
        shown = wanted;
    }
    return shown;
}

bool Toggles::*FieldOf(Common::AltCore::DriverOption option) {
    using Option = Common::AltCore::DriverOption;
    switch (option) {
    case Option::AutoRun:
        return &Toggles::auto_run;
    case Option::RunIndoors:
        return &Toggles::run_indoors;
    case Option::WildEncounters: // the field is whether they are off
        return &Toggles::wild_off;
    case Option::ReusableTms:
        return &Toggles::reusable_tms;
    case Option::InstantText:
        return &Toggles::instant_text;
    case Option::TradeEvolutions:
        return &Toggles::trade_evos;
    default:
        return nullptr;
    }
}

Toggles ChangedToggles(const Toggles& kept, const Toggles& shown, const Toggles& now) {
    const auto pick = [](bool k, bool s, bool n) { return n != s ? n : k; };
    Toggles out;
    out.wild_off = pick(kept.wild_off, shown.wild_off, now.wild_off);
    out.run_indoors = pick(kept.run_indoors, shown.run_indoors, now.run_indoors);
    out.auto_run = pick(kept.auto_run, shown.auto_run, now.auto_run);
    out.reusable_tms = pick(kept.reusable_tms, shown.reusable_tms, now.reusable_tms);
    out.instant_text = pick(kept.instant_text, shown.instant_text, now.instant_text);
    out.trade_evos = pick(kept.trade_evos, shown.trade_evos, now.trade_evos);
    return out;
}

EditGate::EditGate(std::function<bool()> backup_save_) : backup_save(std::move(backup_save_)) {}

void EditGate::SetEditing(bool on, bool step_out) {
    if (on) {
        state = State::On;
    } else {
        state = Open() && step_out ? State::Closing : State::Off;
    }
}

void EditGate::StepsDone() {
    if (state == State::Closing) {
        state = State::Off;
    }
}

bool EditGate::Allows(Ask ask) {
    switch (NeedOf(ask)) {
    case Need::Nothing:
        return true;
    case Need::Editing:
        return Admit();
    case Need::Never:
        break;
    }
    return false;
}

bool EditGate::Admit() {
    if (state != State::On) {
        return false;
    }
    if (backup == Backup::NotYet) {
        backup = backup_save && backup_save() ? Backup::Made : Backup::Refused;
        if (backup == Backup::Refused) {
            LOG_ERROR(Core, "PokeDaisy: the battery save could not be backed up, no edits");
        }
    }
    return backup == Backup::Made;
}

bool StateSafe(const Pending& p) {
    return !(p.asked || p.step || p.job || p.keys || p.flight || p.closing);
}

bool HoldsFrame(bool menu_open, const Pending& p) {
    return menu_open && !(p.asked || p.step || p.job || p.keys || p.closing);
}

std::vector<int> MenuItems(const std::vector<Quick>& all, bool editing) {
    std::vector<int> items;
    for (std::size_t i = 0; i < all.size(); ++i) {
        const Need need = NeedOf(AskOfItem(all[i]));
        if (need == Need::Nothing || (need == Need::Editing && editing)) {
            items.push_back(static_cast<int>(i));
        }
    }
    return items;
}

bool TouchEdges::Next(bool touching, int x, int y, Event& event) {
    if (touching) {
        held = true;
        last_x = std::clamp(x, 0, PD_UI_WIDTH - 1);
        last_y = std::clamp(y, 0, PD_UI_HEIGHT - 1);
        event = {last_x, last_y, true};
        return true;
    }
    if (!held) {
        return false;
    }
    held = false;
    event = {last_x, last_y, false};
    return true;
}

std::size_t AudioKept(std::size_t frames, unsigned rate) {
    // Three frames' worth: the core's own unevenness passes, a call's sound does not.
    return std::min<std::size_t>(frames, rate / 60u * 3u);
}

namespace {

constexpr std::size_t kMaxRom = 32u << 20;
constexpr std::size_t kMinRom = 0x150; // the Game Boy header's end; PokeDaisy reads that far
constexpr uint32_t kWidth = PD_GAME_STRIDE;
constexpr uint32_t kHeight = 160;
/// How long Closing waits for a Game Boy game's encounters to be put back. Past it they come back
/// with the next map.
constexpr int kGbWildWait = 600;
/// pd_session.c's HUD_AFTER_FRAMES.
constexpr int kHudAfterFrames = 20;

static_assert(static_cast<int>(Quick::Pc) == QUICK_PC && static_cast<int>(Quick::Fly) == QUICK_FLY &&
              static_cast<int>(Quick::Repel) == QUICK_REPEL &&
              static_cast<int>(Quick::Bike) == QUICK_BIKE &&
              static_cast<int>(Quick::Wild) == QUICK_WILD &&
              static_cast<int>(Quick::SaveState) == QUICK_SAVE_STATE &&
              static_cast<int>(Quick::LoadState) == QUICK_LOAD_STATE &&
              static_cast<int>(Quick::Ff) == QUICK_FF &&
              static_cast<int>(Quick::Companion) == QUICK_COMPANION &&
              static_cast<int>(Quick::Hud) == QUICK_HUD &&
              static_cast<int>(Quick::Flash) == QUICK_FLASH &&
              static_cast<int>(Quick::Flight) == QUICK_FLIGHT &&
              static_cast<int>(Quick::Land) == QUICK_LAND &&
              static_cast<int>(Quick::Count) == QUICK_COUNT);
static_assert(PD_TOP_W == 400 && PD_TOP_H == 240); // OverlayWidth

class Driver;

/// PokeDaisy's code is full of statics: one driver, and no identifying beside it.
std::mutex g_mutex;
Driver* g_live = nullptr;

Toggles TogglesOf(const pd_settings& s) {
    return {s.wildOff, s.runIndoors, s.autoRun, s.reusableTms, s.instantText, s.tradeEvos};
}

void PutToggles(pd_settings& s, const Toggles& t) {
    s.wildOff = t.wild_off;
    s.runIndoors = t.run_indoors;
    s.autoRun = t.auto_run;
    s.reusableTms = t.reusable_tms;
    s.instantText = t.instant_text;
    s.tradeEvos = t.trade_evos;
}

/// The values the host owns, whatever a file or RESET SETTINGS says.
void HoldSettings(pd_settings& s) {
    s.language = PD_LANG_EN;
    s.aspect = PD_ASPECT_ORIGINAL; // EXPAND hooks the renderer
    s.zoom = 0;
    // One tick is one frame: a speed here would run none or several.
    s.gameSpeed = 0;
    std::fill(std::begin(s.sceneSpeed), std::end(s.sceneSpeed), 0);
    s.flyClassic = true; // the scenic FLY runs at double speed
    s.showPerf = false;
    s.cheats = false;
    s.rewind = false;
}

std::string Sha1(const uint8_t* data, std::size_t size) {
    pd_sha1 sha;
    pd_sha1_init(&sha);
    pd_sha1_update(&sha, data, size);
    char hex[41] = {};
    pd_sha1_hex(&sha, hex);
    return hex;
}

/// Back to no game: the look, the fonts and the tables identifying sets. Some point into the ROM.
void ForgetGame() {
    pd_game none{};
    pd_game_identify(&none);
}

/// Whether PokeDaisy lists exactly these bytes. g_mutex held, no driver live: identifying sets
/// statics. Unlisted files it would read by their header's code (Ruby, Sapphire, LeafGreen, other
/// languages), randomized games and listings it marks unverified do not count.
bool Listed(const uint8_t* rom, std::size_t size, const std::string& sha1) {
    // A boot asks twice: for the file, then for the ROM in memory. An unlisted FireRed or Emerald
    // costs several more passes over it.
    static std::map<std::string, bool> cache;
    if (const auto it = cache.find(sha1); it != cache.end()) {
        return it->second;
    }
    const auto exact = [](const pd_game& game) {
        return game.cfg && !(game.title && std::strstr(game.title, "(unverified)"));
    };
    // With no header read, only a hash can give a configuration.
    pd_game by_hash{};
    by_hash.rom = rom;
    by_hash.romSize = size;
    std::memcpy(by_hash.sha1, sha1.c_str(), sizeof(by_hash.sha1));
    pd_game_identify(&by_hash);
    bool known = exact(by_hash);
    if (!by_hash.cfg) {
        // Retail games get theirs from the header: the hash must be a retail one or a listed one.
        pd_game game;
        pd_game_detect(&game, rom, size);
        game.rom = rom;
        game.romSize = size;
        std::memcpy(game.sha1, sha1.c_str(), sizeof(game.sha1));
        pd_game_identify(&game);
        known = exact(game) && (game.entry || (game.retail && !game.randomized));
    }
    ForgetGame();
    cache[sha1] = known;
    return known;
}

bool ReadRom(const std::string& path, std::vector<uint8_t>& rom) {
    std::ifstream f(path, std::ios::binary);
    char magic[2] = {};
    if (!f.read(magic, sizeof(magic))) {
        return false;
    }
    if (magic[0] == 'P' && magic[1] == 'K') {
        return ExtractRomFromZip(path, rom) && rom.size() <= kMaxRom;
    }
    f.seekg(0, std::ios::end);
    const auto size = static_cast<std::streamoff>(f.tellg());
    if (size < static_cast<std::streamoff>(kMinRom) ||
        size > static_cast<std::streamoff>(kMaxRom)) {
        return false;
    }
    rom.resize(static_cast<std::size_t>(size));
    f.seekg(0, std::ios::beg);
    return static_cast<bool>(f.read(reinterpret_cast<char*>(rom.data()), size));
}

bool AskOf(pd_action action, Ask& ask) {
    switch (action) {
    case PD_ACTION_NONE:
        return false;
    case PD_ACTION_SETTINGS_CHANGED:
        ask = Ask::Settings;
        return true;
    case PD_ACTION_NUZLOCKE_TOGGLE:
    case PD_ACTION_NUZLOCKE_EDIT:
        ask = Ask::Nuzlocke;
        return true;
    case PD_ACTION_ITEM_PICK:
        ask = Ask::ItemPick;
        return true;
    case PD_ACTION_ITEM_APPLY:
        ask = Ask::ItemApply;
        return true;
    case PD_ACTION_ITEM_TAKE:
        ask = Ask::ItemTake;
        return true;
    case PD_ACTION_BOX_WITHDRAW:
    case PD_ACTION_BOX_SWAP:
    case PD_ACTION_BOX_DEPOSIT:
        ask = Ask::BoxMove;
        return true;
    case PD_ACTION_PARTY_SWAP:
        ask = Ask::PartySwitch;
        return true;
    case PD_ACTION_TRADE_EVOLVE:
        ask = Ask::TradeEvolve;
        return true;
    case PD_ACTION_FLY_ASK:
        ask = Ask::FlyList;
        return true;
    case PD_ACTION_FLY_TO:
        ask = Ask::FlyTo;
        return true;
    case PD_ACTION_FLIGHT:
        ask = Ask::Flight;
        return true;
    case PD_ACTION_SAVE_STATE:
    case PD_ACTION_LOAD_STATE:
    case PD_ACTION_STATE_SLOT:
    case PD_ACTION_UNDO_SAVE:
    case PD_ACTION_UNDO_LOAD:
        ask = Ask::State;
        return true;
    case PD_ACTION_TOGGLE_FF:
        ask = Ask::FastMode;
        return true;
    case PD_ACTION_LEAVE_GAME:
    case PD_ACTION_RESTART_GAME:
        ask = Ask::Leave;
        return true;
    }
    ask = Ask::Count; // one we do not know is refused
    return true;
}

class Driver final : public FrameDriver, public Actions {
public:
    /// g_mutex held.
    Driver(const DriverServices& services_, const uint8_t* rom, std::size_t rom_size,
           const std::string& sha1)
        : services(services_), core(services.core), gate(services.backup_save),
          session(std::make_unique<pd_session>()), s(*session) {
        g_live = this;
        is_gba = core->platform(core) == mPLATFORM_GBA;

        std::error_code ec;
        const auto folder = std::filesystem::path(services.game_dir) / "pokedaisy";
        std::filesystem::create_directories(folder, ec);
        dir = folder.string();

        if (is_gba) {
            renderer = static_cast<GBA*>(core->board)->video.renderer;
            draw_scanline = renderer->drawScanline;
            write_vram = renderer->writeVRAM;
            write_oam = renderer->writeOAM;
            write_palette = renderer->writePalette;
        }

        pd_romart_cache_dir("");             // it would keep files of its own
        pd_items_setup(nullptr, nullptr, 0); // start leaves the last game's
        pd_game_detect(&s.game, rom, rom_size);
        // Of the ROM in memory: a patched one differs from its file.
        std::memcpy(s.game.sha1, sha1.c_str(), sizeof(s.game.sha1));

        pd_settings_load(&wanted, SettingsPath().c_str());
        HoldSettings(wanted);
        live = wanted;
        PutToggles(live, shown);

        pd_session_host host{};
        host.statePath = &StatePath;
        host.backupSave = &BackupSave;
        host.settingsChanged = &SettingsChanged;
        host.ctx = this;
        // Without a file, what it keeps lasts for this run. An empty path would be ".tmp" in the
        // current folder, so a path that does not fit its buffers is left out here.
        if (dir.size() + 40 < kPathRoom) {
            host.flightAreaPath = &FlightAreaPath;
            host.nuzlockePath = &NuzlockePath;
        }
        host.companionIntervalMs = PD_SNAPSHOT_INTERVAL_MS;
        pd_session_start(&s, core, video.data(), &live, &host);

        // What start armed.
        pk_field_wild(0, false);
        flight_cfg = s.flightCfg;
        s.flightCfg = nullptr;
        pk_flight_configure(nullptr, 0);
        s.input.write = &GatedWrite;

        can_use = is_gba ? s.host.itemCanUse : nullptr;
        can_give = is_gba ? s.host.itemCanGive : nullptr;
        box_moves = s.ui.boxMoves;
        field_supported = s.host.fieldSupported;

        s.host.hasStates = false;
        s.host.hasExpand = s.host.expandGame = false;
        s.host.hasPerf = false;
        s.host.hasQuickMenu = true; // RING or GRID
        s.host.hasFlightArea = false; // it would show FLY STYLE
        s.host.fullSettings = false;
        s.host.frontendOwnsPlayback = true;
        s.host.hideScreenMode = true;
        s.host.hasCoreRows = true; // AUTO-RUN, DARK MODE, RESET SETTINGS
        s.host.batteryPct = -1;

        pd_canvas_init(&canvas, PD_UI_WIDTH, PD_UI_HEIGHT, canvas_px.data());
        ApplyGate();
        ApplyHost();
        LOG_INFO(Core, "PokeDaisy: {}", s.game.title ? s.game.title : "");
    }

    ~Driver() override {
        std::scoped_lock lock{g_mutex};
        PutToggles(live, {});
        if (s.tmApplied || s.textApplied) {
            pd_session_action(&s, PD_ACTION_SETTINGS_CHANGED); // the ROM's bytes
        }
        if (renderer) {
            // pk_wide's hooks
            renderer->drawScanline = draw_scanline;
            renderer->writeVRAM = write_vram;
            renderer->writeOAM = write_oam;
            renderer->writePalette = write_palette;
        }
        pk_flight_configure(nullptr, 0);
        pk_flight_reset();
        pk_flight_set_area(nullptr, 0);
        pk_flight_set_call(nullptr);
        pk_wide_flight(0, nullptr, 0, 0.0, 0.0);
        pk_wide_frame_wait(nullptr, nullptr);
        pk_wide_reset();
        pk_wide_extras_reset();
        pk_wide_later(0, nullptr);
        pk_wide_hole(0);
        pk_wide_cache(0);
        pk_wide_lines(1); // as it starts out
        pk_wide_configure(0);
        pk_field_wild(0, false);
        pk_field_configure(0);
        pk_rest_reset();
        quick_set_grid(false);
        pd_mixer_release(core);
        pd_mixer_mixes = 0;
        pd_items_setup(nullptr, nullptr, 0);
        std::free(s.cheatText);
        s.cheatText = nullptr;
        ForgetGame();
        g_live = nullptr;
    }

    Driver(const Driver&) = delete;
    Driver& operator=(const Driver&) = delete;

    void RunFrame(const DriverInput& in, DriverFrame& out) override {
        Touch(in);
        TakeAction();
        HoldSettings(live);
        ApplyGate();

        pd_input input{};
        input.held = in.keys;
        input.down = in.keys & ~keys_before;
        input.stickAngle = -1;
        keys_before = in.keys;
        s.ffToggled = false;
        Menu(in.quick_menu && !quick_before, input);
        quick_before = in.quick_menu;
        // PokeDaisy's own hosts let the game run on behind the menu.
        out.held = HoldsFrame(s.quickOpen || input.quickMenu, Underway());
        Tick(input, out.held);

        if (gate.Closing() && !StepOut()) {
            gate.StepsDone();
            ApplyGate();
        }
        ApplyHost();

        for (uint32_t y = 0; y < height; ++y) {
            // After the tick: the flight is drawn into the frame.
            uint32_t* row = video.data() + y * kWidth;
            for (uint32_t x = 0; x < width; ++x) {
                row[x] |= 0xFF000000u;
            }
        }
        out.primary = {video.data(), width, height, kWidth};

        if (s.redraw || s.redrawScroll) {
            pd_session_draw(&s, &canvas);
            std::transform(canvas_px.begin(), canvas_px.end(), secondary.begin(), CanvasToRgba);
        }
        out.secondary = {secondary.data(), PD_UI_WIDTH, PD_UI_HEIGHT, PD_UI_WIDTH};
        Overlay(out);

        Drain(out.audio_pcm);
        out.rumble = services.rumble ? services.rumble() : 0.0f;
        if (out.held) {
            out.audio_pcm.clear(); // calls into the game make some
            out.rumble = 0.0f;
        }
    }

    void Reset() override {
        ClearJobs();
        pk_rest_reset();
        AfterNewMemory();
    }

    bool StateSafeNow() const override {
        return StateSafe(Underway());
    }

    void AfterStateLoad() override {
        ClearJobs();
        // The cart's clock is the save's: an edit like any other.
        if (is_gba && (gate.Editing() ? gate.Admit() : gate.Open())) {
            pk_heal_rtc(core);
        }
        AfterNewMemory();
    }

    bool CanEdit() const override {
        return true;
    }

    void SetEditing(bool on) override {
        if (on == editing_wanted) {
            return;
        }
        editing_wanted = on;
        if (!on) {
            DropQueued();
            PutBack();
            gb_wild_frames = 0;
        }
        gate.SetEditing(on, StepOut());
        ApplyGate();
        ApplyHost();
    }

    std::pair<uint32_t, uint32_t> SecondarySize() const override {
        return {PD_UI_WIDTH, PD_UI_HEIGHT};
    }

    bool HasTouch() const override {
        return true;
    }

    bool HasQuickMenu() const override {
        return true;
    }

    // The options in the VR menu

    Common::AltCore::DriverName Name() const override {
        return Common::AltCore::DriverName::PokeDaisy;
    }

    /// What PokeDaisy's own settings page has for this game. What edits stays listed with editing
    /// off (the menu locks it).
    uint32_t Options() const override {
        uint32_t options = OptionBit(Option::DarkMode) | OptionBit(Option::BattleInfo) |
                           OptionBit(Option::OneTapMoves);
        const auto add = [&options](bool has, Option option) {
            options |= has ? OptionBit(option) : 0u;
        };
        add(field_supported, Option::AutoRun);
        add(field_supported, Option::RunIndoors);
        add(s.host.wildSupported, Option::WildEncounters);
        add(s.host.tmsSupported == 1, Option::ReusableTms);
        add(s.host.textSupported != 0, Option::InstantText);
        add(s.tradeEvoOk, Option::TradeEvolutions);
        add(s.host.hasHud, Option::Hud);
        add(is_gba, Option::BattleGen5);
        add(s.host.hasQuickMenu, Option::QuickGrid);
        add(!wanted.noBattleInfo, Option::BattleHints);
        return options;
    }

    uint32_t OptionsOn() const override {
        uint32_t on = 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>(Option::Count); ++i) {
            const Option option = static_cast<Option>(i);
            on |= IsOn(option) ? OptionBit(option) : 0u;
        }
        return on;
    }

    bool SetOption(Common::AltCore::DriverOption option, uint32_t value) override {
        const bool on = value != 0;
        if (!(Options() & OptionBit(option)) || on == IsOn(option)) {
            return (Options() & OptionBit(option)) != 0;
        }
        if (const auto field = FieldOf(option)) {
            Toggles toggles = GetToggles();
            toggles.*field = option == Option::WildEncounters ? !on : on;
            if (!SetToggles(toggles)) {
                return false;
            }
            s.redraw = true;
            return true;
        }
        const auto [field, inverted] = SettingOf(option);
        live.*field = on != inverted;
        // As its own page does it; what the gate holds back stays the player's.
        Forward(PD_ACTION_SETTINGS_CHANGED);
        return true;
    }

    // Actions

    bool Offers(Ask ask) const override {
        if (!gate.Editing()) {
            return false;
        }
        switch (ask) {
        case Ask::Pc:
            return s.gbField || s.pcOk;
        case Ask::Repel:
            return s.itemsOk;
        case Ask::Bike:
            return s.bikeOk;
        case Ask::Flash:
            return s.flashCfg != nullptr;
        case Ask::Flight:
            return flight_cfg != nullptr;
        case Ask::FlyList:
            return s.gbField || s.flyCfg;
        default:
            return false;
        }
    }

    bool Do(Ask ask) override {
        // One job at a time: a second would lose the bike's register.
        if (!Offers(ask) || s.job != PD_JOB_NONE || !gate.Allows(ask)) {
            return false;
        }
        ApplyGate();
        switch (ask) {
        case Ask::Pc:
            if (s.gbField) {
                if (gb_field_pc(core, s.gbField, GbBusy()) != ITEM_USED) {
                    return false;
                }
                s.job = PD_JOB_GB_TAKEN;
                s.jobFrames = 0;
                return true;
            }
            StartJob(PD_JOB_PC, 0);
            return true;
        case Ask::Repel:
            for (const auto& kind : repel_kinds) { // weakest first, as its menu has it
                if (bag_count(&s.shown, kind[0]) > 0) {
                    StartJob(PD_JOB_REPEL, kind[0]);
                    return true;
                }
            }
            return false;
        case Ask::Bike:
            if (const int bike = bag_bike(&s.shown)) {
                StartJob(PD_JOB_BIKE, bike);
                return true;
            }
            return false;
        case Ask::Flash:
            s.flashAsk = 1;
            return true;
        case Ask::Flight:
            s.flightAsk = 1;
            return true;
        case Ask::FlyList:
            return Forward(PD_ACTION_FLY_ASK);
        default:
            return false;
        }
    }

    Toggles GetToggles() const override {
        return TogglesOf(wanted);
    }

    bool SetToggles(const Toggles& toggles) override {
        if (!gate.Allows(Ask::Toggle)) {
            return false;
        }
        PutToggles(wanted, toggles);
        SaveSettings();
        ApplyGate();
        return true;
    }

private:
    using Option = Common::AltCore::DriverOption;
    static constexpr uint32_t OptionBit(Option option) {
        return Common::AltCore::DriverOptionBit(option);
    }

    /// The setting of an option that edits nothing, and whether it says the opposite.
    static std::pair<bool pd_settings::*, bool> SettingOf(Option option) {
        switch (option) {
        case Option::Hud:
            return {&pd_settings::hud, false};
        case Option::DarkMode:
            return {&pd_settings::darkMode, false};
        case Option::BattleInfo:
            return {&pd_settings::noBattleInfo, true};
        case Option::BattleHints:
            return {&pd_settings::hideHints, true};
        case Option::OneTapMoves:
            return {&pd_settings::oneTapMoves, false};
        case Option::BattleGen5:
            return {&pd_settings::battleGen5, false};
        case Option::QuickGrid:
            return {&pd_settings::quickGrid, false};
        default:
            return {nullptr, false};
        }
    }

    /// The player's, whether or not it acts now.
    bool IsOn(Option option) const {
        if (const auto field = FieldOf(option)) {
            return TogglesOf(wanted).*field != (option == Option::WildEncounters);
        }
        const auto [field, inverted] = SettingOf(option);
        return field && wanted.*field != inverted;
    }

    /// Room for a path and its ".tmp" in PokeDaisy's smallest buffer (pd_session.nuzPath).
    static constexpr std::size_t kPathRoom = 290;

    static void StatePath(void* ctx, int, char* vf, size_t vf_len, char* path, size_t len) {
        // A folder nothing makes: its states stay unreadable and unwritable.
        const std::string at = static_cast<Driver*>(ctx)->dir + "/no-states/slot";
        std::snprintf(vf, vf_len, "%s", at.size() < vf_len ? at.c_str() : "");
        std::snprintf(path, len, "%s", at.size() < len ? at.c_str() : "");
    }

    static void BackupSave(void* ctx) {
        static_cast<Driver*>(ctx)->gate.Admit();
    }

    static void SettingsChanged(void* ctx) {
        static_cast<Driver*>(ctx)->TakeSettings();
    }

    static void FlightAreaPath(void* ctx, uint32_t crc, uint32_t key, char* path, size_t len) {
        std::snprintf(path, len, "%s/flight-%08x-%08x.txt", static_cast<Driver*>(ctx)->dir.c_str(),
                      static_cast<unsigned>(crc), static_cast<unsigned>(key));
    }

    static void NuzlockePath(void* ctx, uint32_t crc, char* path, size_t len) {
        std::snprintf(path, len, "%s/nuzlocke-%08x.txt", static_cast<Driver*>(ctx)->dir.c_str(),
                      static_cast<unsigned>(crc));
    }

    /// The battle's cursor byte. Refused, it steers with key presses instead.
    static bool GatedWrite(void* ctx, uint32_t addr, const void* data, size_t len) {
        return g_live && g_live->gate.Open() && pd_session_write(ctx, addr, data, len);
    }

    static void Hook(mCore*) {
        g_live->Step();
    }
    static void RefuseWrite8(mCore*, uint32_t address, uint8_t) {
        g_live->Refused(address);
    }
    static void RefuseWrite16(mCore*, uint32_t address, uint16_t) {
        g_live->Refused(address);
    }
    static void RefuseWrite32(mCore*, uint32_t address, uint32_t) {
        g_live->Refused(address);
    }
    static void RefuseStep(mCore*) {}

    void Refused(uint32_t address) {
        if (!refused_logged) {
            refused_logged = true;
            LOG_ERROR(Core, "PokeDaisy: refused a write to {:08X} with editing off", address);
        }
    }

    std::string SettingsPath() const {
        return dir + "/settings.txt";
    }

    void SaveSettings() {
        if (!pd_settings_save(&wanted, SettingsPath().c_str())) {
            LOG_WARNING(Core, "PokeDaisy: settings not saved");
        }
    }

    /// A page changed `live`. What the gate held back stays the player's.
    void TakeSettings() {
        const Toggles kept = ChangedToggles(TogglesOf(wanted), shown, TogglesOf(live));
        wanted = live;
        PutToggles(wanted, kept);
        HoldSettings(wanted);
        SaveSettings();
    }

    // The frame

    Pending Underway() const {
        Pending p;
        p.asked = s.flyAsk || s.flashAsk || s.flightAsk || s.scenicAsk || s.itemAsk || s.boxAsk ||
                  s.swapAsk || s.evolveAsk || s.ui.action != PD_ACTION_NONE;
        p.step = s.flyStage || s.tmStage;
        p.job = s.job != PD_JOB_NONE;
        p.keys = s.gameKeyFrames > 0;
        p.flight = InFlight();
        p.closing = gate.Closing();
        return p;
    }

    /// `hold`: a tick without a frame.
    void Tick(const pd_input& input, bool hold) {
        stepped = hold; // the hook's step and the one below are taken
        if (hold) {
            s.speedDebt = -4; // a frame's worth: the session runs none
        }
        run_frame = core->runFrame;
        core->runFrame = &Hook;
        const bool seal = !gate.Open();
        if (seal) {
            Seal();
        }
        pd_session_tick(&s, &input, 16);
        if (seal) {
            Unseal();
        }
        core->runFrame = run_frame;
        if (hold) {
            s.speedDebt = 0;
        }
        if (!stepped) {
            LOG_ERROR(Core, "PokeDaisy: a tick without a frame");
            stepped = true;
            services.step(input.held);
            CopyPicture();
        }
    }

    /// core->runFrame while the session runs. Its own pointers are back for the length of the step.
    void Step() {
        if (stepped) {
            return; // one frame a tick
        }
        stepped = true;
        const bool seal = sealed;
        if (seal) {
            Unseal();
        }
        core->runFrame = run_frame;
        services.step(core->getKeys(core));
        CopyPicture();
        core->runFrame = &Hook;
        if (seal) {
            Seal();
        }
    }

    void Seal() {
        bus_write8 = core->busWrite8;
        bus_write16 = core->busWrite16;
        bus_write32 = core->busWrite32;
        single_step = core->step;
        core->busWrite8 = &RefuseWrite8;
        core->busWrite16 = &RefuseWrite16;
        core->busWrite32 = &RefuseWrite32;
        core->step = &RefuseStep;
        sealed = true;
    }

    void Unseal() {
        core->busWrite8 = bus_write8;
        core->busWrite16 = bus_write16;
        core->busWrite32 = bus_write32;
        core->step = single_step;
        sealed = false;
    }

    /// The session reads, and the flight draws in, rows 240 wide: a Game Boy's are narrower.
    void CopyPicture() {
        const DriverPicture picture = services.picture();
        if (!picture.pixels) {
            return;
        }
        width = std::min(picture.width, kWidth);
        height = std::min(picture.height, kHeight);
        for (uint32_t y = 0; y < height; ++y) {
            std::copy_n(picture.pixels + static_cast<std::size_t>(y) * picture.stride, width,
                        video.begin() + static_cast<std::ptrdiff_t>(y * kWidth));
        }
    }

    void Drain(std::vector<int16_t>& pcm) {
        pcm.clear();
        if (!services.drain_audio) {
            return;
        }
        std::size_t total = 0;
        for (;;) {
            const std::size_t got =
                services.drain_audio(audio_scratch.data(), audio_scratch.size() / 2);
            if (got == 0) {
                break;
            }
            pcm.insert(pcm.end(), audio_scratch.begin(),
                       audio_scratch.begin() + static_cast<std::ptrdiff_t>(got * 2));
            total += got;
            if (got < audio_scratch.size() / 2) {
                break;
            }
        }
        const unsigned rate = core->audioSampleRate(core);
        pcm.resize(AudioKept(total, rate ? rate : 32768u) * 2);
    }

    // The quick menu. The session runs it; what it takes is the gate's, and this is where that is
    // decided, before its tick. Open, editing is on and the save backed up.

    void Menu(bool pressed, pd_input& input) {
        if (s.quickOpen && !gate.Editing()) {
            CloseMenu();
        }
        if (pressed) {
            if (!s.quickOpen) {
                if (!gate.Admit()) { // all of it edits
                    pd_session_show(&s, editing_wanted ? item_reason(ITEM_FAILED)
                                                       : Common::AltCore::kQuickMenuNeedsEditing);
                    return;
                }
                ApplyGate();
            }
            input.quickMenu = true;
            return;
        }
        if (!s.quickOpen) {
            return;
        }
        if (s.flyList || s.repelList) {
            if (s.repelList && s.repelSel >= RepelKinds()) {
                s.repelList = false; // used up behind it
            }
            if ((input.down & PD_KEY_A) && !MayRun(s.flyList ? Ask::FlyTo : Ask::Repel)) {
                input.down &= ~static_cast<uint32_t>(PD_KEY_A);
            }
            return;
        }
        // The session would step over the items it has and the menu has not.
        const std::vector<int> items = Items();
        const int dx = ((input.down & PD_KEY_RIGHT) ? 1 : 0) - ((input.down & PD_KEY_LEFT) ? 1 : 0);
        const int dy = ((input.down & PD_KEY_DOWN) ? 1 : 0) - ((input.down & PD_KEY_UP) ? 1 : 0);
        input.down &=
            ~static_cast<uint32_t>(PD_KEY_UP | PD_KEY_DOWN | PD_KEY_LEFT | PD_KEY_RIGHT);
        if (items.empty()) {
            input.down &= ~static_cast<uint32_t>(PD_KEY_A);
            return;
        }
        int at = Selected(items);
        if (dx || dy) {
            quick_set_grid(live.quickGrid);
            at = quick_step(static_cast<int>(items.size()), at, dx, dy, ShownOverlayWidth(),
                            PD_TOP_H);
        }
        s.quickSel = items[static_cast<std::size_t>(at)];
        const quick_item& item = s.quick[s.quickSel];
        if ((input.down & PD_KEY_A) && !item.why &&
            !MayRun(AskOfItem(static_cast<Quick>(item.action)))) {
            input.down &= ~static_cast<uint32_t>(PD_KEY_A);
        }
    }

    bool MayRun(Ask ask) {
        if (ask != Ask::Toggle &&
            (s.job != PD_JOB_NONE || s.flyAsk || s.flyStage || s.tmStage)) {
            pd_session_show(&s, item_reason(ITEM_NOT_NOW)); // one at a time, as in Do
            return false;
        }
        return gate.Allows(ask);
    }

    int RepelKinds() const {
        return static_cast<int>(
            std::count_if(std::begin(repel_kinds), std::end(repel_kinds),
                          [this](const auto& kind) { return bag_count(&s.shown, kind[0]) > 0; }));
    }

    std::vector<int> Items() const {
        std::vector<Quick> all;
        for (int i = 0; i < s.quickCount; ++i) {
            all.push_back(static_cast<Quick>(s.quick[i].action));
        }
        return MenuItems(all, gate.Editing());
    }

    /// The session's selection among `items`.
    int Selected(const std::vector<int>& items) const {
        const auto it = std::find(items.begin(), items.end(), s.quickSel);
        return it == items.end() ? 0 : static_cast<int>(it - items.begin());
    }

    void CloseMenu() {
        if (s.quickOpen) {
            s.quickHeld |= keys_before; // pressed for the menu, kept from the game until let go
        }
        s.quickOpen = s.flyList = s.repelList = false;
    }

    /// The menu, the HUD and notices: what pd_session's own overlay has, on a canvas as wide as the
    /// game's picture. The session's is 400 wide for a screen that is, and puts the notices, the
    /// lists and two of the HUD's layouts beside the picture too.
    void Overlay(DriverFrame& out) {
        const bool menu = s.quickOpen;
        const bool in_air = s.hudFieldFrames == 0 && s.flightState != PK_FLIGHT_IDLE &&
                            !(live.hudHidden & (1 << PD_HUD_MAP));
        const bool hud =
            !menu && live.hud && s.game.cfg && (s.hudFieldFrames >= kHudAfterFrames || in_air);
        const bool notice = s.noticeFrames > 0;
        if (!menu && !hud && !notice) {
            overlay_drawn = false;
            return;
        }
        const int w = ShownOverlayWidth();
        pd_settings hud_settings{};
        hud_state state{};
        uint32_t key = static_cast<uint32_t>(w);
        if (hud) {
            hud_settings = live;
            if (in_air) {
                hud_settings.hudHidden = ~(1 << PD_HUD_MAP);
            }
            state = s.hud;
            state.compact = state.noButtons = true;
            state.wild = state.focus = -1;
            state.left = 0;
            state.right = w;
            key = key * 31 + hud_key(&s.game, &s.shown, &state, &hud_settings) + (in_air ? 2u : 1u);
        }
        for (const char* p = notice ? s.notice : ""; *p; ++p) {
            key = key * 33 + static_cast<uint8_t>(*p);
        }
        if (!overlay_drawn || menu || key != overlay_key) {
            overlay_shown = DrawOverlay(w, menu, hud ? &state : nullptr, hud_settings, notice);
            overlay_drawn = !menu;
            overlay_key = key;
        }
        if (overlay_shown) {
            out.overlay = {overlay.data(), static_cast<uint32_t>(w), PD_TOP_H,
                           static_cast<uint32_t>(w)};
        }
    }

    /// For the part of the game's picture that is shown (Layer Depth).
    int ShownOverlayWidth() const {
        const float shift = services.layer_shift ? services.layer_shift() : 0.0f;
        const float shown = static_cast<float>(width) * (1.0f - Common::LayerDepth::Cut(shift));
        return OverlayWidth(static_cast<uint32_t>(std::lround(shown)), height);
    }

    bool DrawOverlay(int w, bool menu, const hud_state* hud, const pd_settings& hud_settings,
                     bool notice) {
        pd_canvas c{};
        pd_canvas_init(&c, w, PD_TOP_H, overlay_px.data());
        std::fill(overlay_px.begin(), overlay_px.end(), QUICK_CLEAR);
        bool any = menu || notice;
        // As the session: the menu and the HUD in PokeDaisy's font, notices in the game's.
        const pd_glyph* game_font = nullptr;
        int game_font_count = 0;
        pd_font_get(&game_font, &game_font_count);
        pd_font_set(nullptr, 0);
        pd_font_size_set(1);
        if (menu && s.flyList) {
            quick_list_draw(&c, quick_label(QUICK_FLY), s.flyNames, s.flyTargets.count, s.flySel,
                            live.hudLook);
        } else if (menu && s.repelList) {
            quick_list_draw(&c, quick_label(QUICK_REPEL), s.repelNames, s.repelKindCount,
                            s.repelSel, live.hudLook);
        } else if (menu) {
            const std::vector<int> items = Items();
            std::array<quick_item, QUICK_COUNT> tiles{};
            for (std::size_t i = 0; i < items.size(); ++i) {
                tiles[i] = s.quick[items[i]];
            }
            quick_set_grid(live.quickGrid);
            quick_draw(&c, &s.game, tiles.data(), static_cast<int>(items.size()), Selected(items),
                       live.hudLook);
        } else if (hud) {
            any |= hud_draw(&c, &s.game, &s.shown, hud, &hud_settings, nullptr);
        }
        pd_font_set(game_font, game_font_count);
        if (notice) {
            Notice(c);
        }
        quick_finish(&c);
        if (any) {
            std::transform(overlay_px.begin(), overlay_px.begin() + w * PD_TOP_H, overlay.begin(),
                           [menu](uint32_t px) { return OverlayPixel(px, menu); });
        }
        return any;
    }

    /// Top right, as the session's; on two lines where the picture is too narrow for it.
    void Notice(pd_canvas& c) const {
        constexpr int kPad = 12;
        constexpr int kMargin = 4;
        constexpr int kLine = 16;
        std::array<std::string, 2> lines{s.notice, ""};
        int count = 1;
        if (pd_text_width(s.notice) + kPad > c.w - 2 * kMargin) {
            const std::string all = lines[0];
            std::size_t cut = std::string::npos;
            for (std::size_t i = all.find(' '); i != std::string::npos; i = all.find(' ', i + 1)) {
                const auto off = [&all](std::size_t at) {
                    return std::abs(static_cast<int>(at * 2) - static_cast<int>(all.size()));
                };
                if (cut == std::string::npos || off(i) < off(cut)) {
                    cut = i;
                }
            }
            if (cut != std::string::npos) {
                lines = {all.substr(0, cut), all.substr(cut + 1)};
                count = 2;
            }
        }
        int w = 0;
        for (int i = 0; i < count; ++i) {
            w = std::max(w, pd_text_width(lines[i].c_str()) + kPad);
        }
        const int x = c.w - w - kMargin;
        pd_round_rect(&c, x, kMargin, w, 18 + (count - 1) * kLine, 2, 0xB8121620u);
        for (int i = 0; i < count; ++i) {
            pd_text(&c, x + kPad / 2, kMargin + i * kLine, lines[i].c_str(), 0xF8F8F8, 0x303840);
        }
    }

    // What the player does

    void Touch(const DriverInput& in) {
        TouchEdges::Event event;
        if (!touch.Next(in.touching, in.touch_x, in.touch_y, event)) {
            return;
        }
        if (pd_ui_touch(&s.ui, event.x, event.y, event.down)) {
            (s.ui.touchScrollOnly ? s.redrawScroll : s.redraw) = true;
        }
        if (!event.down) {
            s.snapshotRequested = true;
        }
    }

    void TakeAction() {
        const pd_action action = pd_ui_take_action(&s.ui);
        Ask ask{};
        if (!AskOf(action, ask)) {
            return;
        }
        if (!is_gba && NeedOf(ask) == Need::Editing && ask != Ask::FlyList && ask != Ask::FlyTo) {
            return; // those jobs call an ARM CPU
        }
        if (!gate.Allows(ask)) {
            LOG_DEBUG(Core, "PokeDaisy: action {} refused", static_cast<int>(action));
            s.redraw = true;
            return;
        }
        ApplyGate();
        Forward(action);
    }

    /// An action the gate has let through.
    bool Forward(pd_action action) {
        if (action == PD_ACTION_SETTINGS_CHANGED) {
            TakeSettings();
            HoldSettings(live);
            ApplyGate();
        } else if (action == PD_ACTION_FLIGHT && !flight_cfg) {
            return false;
        } else if (action == PD_ACTION_FLY_TO &&
                   (s.ui.flyPick < 0 || s.ui.flyPick >= s.flyTargets.count)) {
            return false;
        } else if (action == PD_ACTION_FLY_ASK && s.gbField) {
            return GbFlyList(); // the session answers it for its quick menu only
        }
        return pd_session_action(&s, action);
    }

    bool GbBusy() const {
        return s.shown.inBattle || (s.wideKnown && pk_wide_scene() != PK_WIDE_SCENE_FIELD);
    }

    bool GbFlyList() {
        gb_field_fly_targets(core, s.gbField, s.rom, s.romSize, GbBusy(), &s.flyTargets);
        if (s.flyTargets.result != FLY_OK) {
            pd_session_show(&s, fly_reason(s.flyTargets.result));
            return false;
        }
        if (!s.flyTargets.count) {
            return false;
        }
        for (int i = 0; i < s.flyTargets.count; ++i) {
            const char* name = pd_mapsec_name(&s.game, s.flyTargets.towns[i]);
            s.flyNames[i] = name && *name ? name : "?";
        }
        s.ui.flyNames = s.flyNames;
        s.ui.flyCount = s.flyTargets.count;
        s.ui.overlay = PD_OVERLAY_FLY;
        s.ui.placesScroll = 0;
        s.redraw = true;
        return true;
    }

    void StartJob(pd_job job, int item) {
        s.job = job;
        s.jobItem = item;
        s.jobFrames = 0;
        s.jobTries = 15; // pd_session.c's BIKE_TRIES
    }

    // The gate

    /// Everything the gate holds, set from it. The only place that gives the session a way to
    /// write by itself.
    void ApplyGate() {
        const Toggles want = TogglesOf(wanted);
        if (gate.Editing() && want.Writes()) {
            gate.Admit(); // the player has one on: that is the first edit
        }
        const bool open = gate.Open();
        shown = ShownToggles(want, gate.Editing(), open);
        PutToggles(live, shown);

        s.flightCfg = open ? flight_cfg : nullptr;
        if (open != was_open) {
            was_open = open;
            ArmWild();
            if (!open) {
                pk_flight_configure(nullptr, 0);
            }
        }
        if (shown.reusable_tms != rom_tms || shown.instant_text != rom_text) {
            rom_tms = shown.reusable_tms;
            rom_text = shown.instant_text;
            pd_session_action(&s, PD_ACTION_SETTINGS_CHANGED);
        }
    }

    /// pk_field_state's "all encounters" still clears the game's switch, so closed it gets none.
    void ArmWild() {
        pk_field_wild(was_open ? s.wildSwitch : 0, shown.wild_off);
    }

    /// What the pages show. After a tick: it sets some of these itself.
    void ApplyHost() {
        const bool can = gate.Editing();
        pd_host_info& h = s.host;
        h.flyReady = can && (s.gbField ? true : s.flyCfg && fly_flyer(&s.shown) >= 0);
        h.flightReady = can && flight_cfg && fly_flyer(&s.shown) >= 0;
        h.flying = InFlight();
        h.flightSupported = can && flight_cfg;
        h.canTake = can && is_gba && pd_items_can_take();
        h.canSwitchParty = can && is_gba && s.game.cfg && s.game.cfg->playerParty;
        h.itemCanUse = can ? can_use : nullptr;
        h.itemCanGive = can ? can_give : nullptr;
        h.fieldSupported = can && field_supported;
        h.tradeEvoSupported = can && s.tradeEvoOk;
        s.ui.boxMoves = can && box_moves;
        if (!can) {
            std::fill(std::begin(h.evolveTo), std::end(h.evolveTo), 0);
        }
        if (can != could) {
            could = can;
            s.redraw = true; // the hit boxes of what is gone go with the next draw
        }
    }

    bool InFlight() const {
        return s.flightState != PK_FLIGHT_IDLE || pk_flight_state() != PK_FLIGHT_IDLE;
    }

    /// Something of ours is half done in the game and must run on.
    bool StepOut() {
        if (s.flyStage || s.tmStage || s.job != PD_JOB_NONE || InFlight() || s.flightDrawn) {
            return true;
        }
        if (s.gbWildHeld && ++gb_wild_frames > kGbWildWait) {
            s.gbWildHeld = false;
        }
        return s.gbWildHeld;
    }

    /// Asks and jobs that have not touched the game yet.
    void DropQueued() {
        CloseMenu();
        s.flyAsk = s.flashAsk = s.flightAsk = s.scenicAsk = s.itemAsk = s.boxAsk = 0;
        s.flyFromMap = s.swapAsk = s.evolveAsk = false;
        if (s.flyStage == 1) {
            s.flyStage = 0;
        }
        if (s.job == PD_JOB_PC || s.job == PD_JOB_REPEL || s.job == PD_JOB_BIKE) {
            s.job = PD_JOB_NONE;
        }
        s.ui.action = PD_ACTION_NONE;
        s.ui.itemPicking = s.ui.boxSwapPick = s.ui.boxDepositing = s.ui.summarySwitching = false;
        s.ui.itemMoveSlot = -1;
        if (s.ui.overlay == PD_OVERLAY_FLY || s.ui.overlay == PD_OVERLAY_CONFIRM_BOX) {
            s.ui.overlay = PD_OVERLAY_NONE;
        }
        s.redraw = true;
    }

    /// What is on loan, where it can go back at once.
    void PutBack() {
        if (!gate.Open()) {
            return;
        }
        if (s.job == PD_JOB_BIKE_WAIT || s.job == PD_JOB_BIKE_RESTORE) {
            s.job = PD_JOB_NONE;
            s.gameKeyFrames = 0;
            bike_restore(core, s.game.cfg, s.jobItem, s.bikeOld);
        }
        if (s.wildSwitch && (s.fieldFlags & PK_FIELD_WILD_HELD)) {
            const uint8_t zero = 0;
            pd_session_write(&s, s.wildSwitch, &zero, 1);
        }
        if (InFlight()) {
            pk_flight_land();
        }
    }

    /// The game's memory is another one now: nothing of ours is under way in it.
    void ClearJobs() {
        DropQueued();
        s.flyStage = s.tmStage = 0;
        s.job = PD_JOB_NONE;
        s.gameKeys = 0;
        s.gameKeyFrames = 0;
        s.bikeTook = false;
        s.scenic = false;
        s.flightState = PK_FLIGHT_IDLE;
        s.flightDrawn = s.flightPending = false;
        s.speedDebt = 0;
        s.noticeFrames = 0;
        touch.Forget();
        s.ui.touching = s.ui.dragging = s.ui.barDragging = false;
        s.ui.pressedId = -1;
    }

    void AfterNewMemory() {
        pk_flight_reset();
        pk_wide_reset();
        pd_binput_init(&s.input, &s.game, pd_session_read, &GatedWrite, &s);
        // A state made while a Game Boy's encounters were held keeps them held: released once.
        s.gbWildHeld = s.gbField && gate.Open();
        s.fieldFlags = 0;
        gate.StepsDone();
        ApplyGate();
        ArmWild();
        ApplyHost();
        s.snapshotRequested = true;
        s.redraw = true;
    }

    DriverServices services;
    mCore* core;
    EditGate gate;
    std::unique_ptr<pd_session> session; // too big for a stack
    pd_session& s;
    std::string dir;
    bool is_gba = false;

    pd_settings wanted{}; ///< the player's, kept in the file
    pd_settings live{};   ///< the session's
    Toggles shown;        ///< live's toggles
    bool editing_wanted = false;
    bool was_open = false;
    bool could = false;
    bool rom_tms = false;
    bool rom_text = false;
    int gb_wild_frames = 0;

    // What the game has, while the gate hides it.
    const pd_config* flight_cfg = nullptr;
    bool (*can_use)(int) = nullptr;
    bool (*can_give)(int) = nullptr;
    bool box_moves = false;
    bool field_supported = false;

    // The core's own pointers.
    void (*run_frame)(mCore*) = nullptr;
    void (*bus_write8)(mCore*, uint32_t, uint8_t) = nullptr;
    void (*bus_write16)(mCore*, uint32_t, uint16_t) = nullptr;
    void (*bus_write32)(mCore*, uint32_t, uint32_t) = nullptr;
    void (*single_step)(mCore*) = nullptr;
    GBAVideoRenderer* renderer = nullptr;
    decltype(GBAVideoRenderer::drawScanline) draw_scanline = nullptr;
    decltype(GBAVideoRenderer::writeVRAM) write_vram = nullptr;
    decltype(GBAVideoRenderer::writeOAM) write_oam = nullptr;
    decltype(GBAVideoRenderer::writePalette) write_palette = nullptr;
    bool stepped = false;
    bool sealed = false;
    bool refused_logged = false;

    TouchEdges touch;
    uint32_t keys_before = 0;
    bool quick_before = false;
    uint32_t width = kWidth;
    uint32_t height = kHeight;
    std::array<uint32_t, kWidth * kHeight> video{};
    pd_canvas canvas{};
    std::array<uint32_t, PD_UI_WIDTH * PD_UI_HEIGHT> canvas_px{};
    std::array<uint32_t, PD_UI_WIDTH * PD_UI_HEIGHT> secondary{};
    std::array<uint32_t, PD_TOP_W * PD_TOP_H> overlay_px{}; ///< PokeDaisy's, then `overlay`
    std::array<uint32_t, PD_TOP_W * PD_TOP_H> overlay{};
    uint32_t overlay_key = 0;
    bool overlay_drawn = false; ///< `overlay` is what overlay_key names
    bool overlay_shown = false; ///< with something on it
    std::array<int16_t, 4096 * 2> audio_scratch{};
};

} // namespace

std::pair<const uint8_t*, std::size_t> RomOf(mCore* core) {
    if (core->platform(core) == mPLATFORM_GBA) {
        std::size_t size = 0;
        const void* rom = core->getMemoryBlock(core, GBA_REGION_ROM0, &size);
        return {static_cast<const uint8_t*>(rom), rom ? size : 0};
    }
    if (core->platform(core) == mPLATFORM_GB) {
        const auto* gb = static_cast<const GB*>(core->board);
        return {gb->memory.rom, gb->memory.rom ? gb->memory.romSize : 0};
    }
    return {nullptr, 0};
}

void SettleRom(mCore* core) {
    if (RomOf(core).first) {
        const uint32_t start = core->platform(core) == mPLATFORM_GBA ? GBA_BASE_ROM0 : 0;
        core->rawWrite8(core, start, -1, core->rawRead8(core, start, -1));
    }
}

HeldSettings DefaultSettings() {
    pd_settings s{};
    pd_settings_defaults(&s);
    HoldSettings(s);
    HeldSettings held{};
    held.language = s.language;
    held.aspect = s.aspect;
    held.game_speed = s.gameSpeed;
    for (const int speed : s.sceneSpeed) {
        held.scene_speeds += speed;
    }
    held.fly_classic = s.flyClassic;
    held.hud = s.hud;
    held.cheats = s.cheats;
    held.rewind = s.rewind;
    held.toggles = TogglesOf(s);
    return held;
}

Actions* ActionsOf(FrameDriver& driver) {
    return dynamic_cast<Actions*>(&driver);
}

} // namespace PokeDaisy

bool PokeDaisyKnows(const std::string& rom_path) {
    struct Entry {
        std::uintmax_t size;
        std::filesystem::file_time_type mtime;
        bool known;
    };
    static std::map<std::string, Entry> cache; // under g_mutex

    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(rom_path, ec);
    if (ec) {
        return false;
    }
    const auto mtime = std::filesystem::last_write_time(rom_path, ec);
    if (ec) {
        return false;
    }

    std::scoped_lock lock{PokeDaisy::g_mutex};
    const auto it = cache.find(rom_path);
    if (it != cache.end() && it->second.size == size && it->second.mtime == mtime) {
        return it->second.known;
    }
    if (PokeDaisy::g_live) {
        return false; // identifying would change the running game's statics; not cached
    }
    std::vector<uint8_t> rom;
    const bool known =
        PokeDaisy::ReadRom(rom_path, rom) && rom.size() >= PokeDaisy::kMinRom &&
        PokeDaisy::Listed(rom.data(), rom.size(), PokeDaisy::Sha1(rom.data(), rom.size()));
    cache[rom_path] = {size, mtime, known};
    return known;
}

std::unique_ptr<FrameDriver> MakePokeDaisyDriver(const DriverServices& services) {
    mCore* core = services.core;
    if (!core || !services.step || !services.picture) {
        return nullptr;
    }
    std::scoped_lock lock{PokeDaisy::g_mutex};
    if (PokeDaisy::g_live) {
        LOG_ERROR(Core, "PokeDaisy: already running a game");
        return nullptr;
    }
    auto [rom, size] = PokeDaisy::RomOf(core);
    if (!rom || size < PokeDaisy::kMinRom) {
        return nullptr;
    }
    const std::string sha1 = PokeDaisy::Sha1(rom, size);
    if (!PokeDaisy::Listed(rom, size, sha1)) {
        LOG_INFO(Core, "PokeDaisy: not a ROM it lists ({})", sha1);
        return nullptr;
    }
    PokeDaisy::SettleRom(core);
    std::tie(rom, size) = PokeDaisy::RomOf(core);
    return std::make_unique<PokeDaisy::Driver>(services, rom, size, sha1);
}

} // namespace FrameCores

#endif // HAVE_POKEDAISY
