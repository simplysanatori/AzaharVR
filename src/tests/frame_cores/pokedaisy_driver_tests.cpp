// Copyright 2026 AzaharVR contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

#ifdef HAVE_POKEDAISY

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "frame_cores/frame_driver.h"
#include "frame_cores/pokedaisy_driver.h"

using namespace FrameCores;
using namespace FrameCores::PokeDaisy;

namespace {

struct Backup {
    int calls = 0;
    bool ok = true;
    EditGate Gate() {
        return EditGate{[this] {
            ++calls;
            return ok;
        }};
    }
};

constexpr Ask kEdits[] = {Ask::ItemPick,    Ask::ItemApply,   Ask::ItemTake, Ask::BoxMove,
                          Ask::PartySwitch, Ask::TradeEvolve, Ask::FlyList,  Ask::FlyTo,
                          Ask::Flight,      Ask::Pc,          Ask::Repel,    Ask::Bike,
                          Ask::Flash,       Ask::Toggle};
constexpr Ask kNever[] = {Ask::State, Ask::FastMode, Ask::Leave, Ask::Count};
constexpr Ask kFree[] = {Ask::Settings, Ask::Nuzlocke};

constexpr Toggles kAllOn{true, true, true, true, true, true};

} // namespace

TEST_CASE("PokeDaisy gate: every ask is in the table", "[frame_cores]") {
    CHECK(std::size(kEdits) + std::size(kNever) + std::size(kFree) ==
          static_cast<std::size_t>(Ask::Count) + 1);
    for (const Ask ask : kEdits) {
        CHECK(NeedOf(ask) == Need::Editing);
    }
    for (const Ask ask : kNever) {
        CHECK(NeedOf(ask) == Need::Never);
    }
    for (const Ask ask : kFree) {
        CHECK(NeedOf(ask) == Need::Nothing);
    }
}

TEST_CASE("PokeDaisy gate: off takes no edit and makes no backup", "[frame_cores]") {
    Backup backup;
    EditGate gate = backup.Gate();
    for (const Ask ask : kEdits) {
        CHECK_FALSE(gate.Allows(ask));
    }
    for (const Ask ask : kFree) {
        CHECK(gate.Allows(ask));
    }
    CHECK_FALSE(gate.Open());
    CHECK_FALSE(gate.Editing());
    CHECK_FALSE(gate.Admit());
    CHECK(backup.calls == 0);
}

TEST_CASE("PokeDaisy gate: the first edit makes the backup, once", "[frame_cores]") {
    Backup backup;
    EditGate gate = backup.Gate();
    gate.SetEditing(true, false);
    CHECK(gate.Editing());
    CHECK_FALSE(gate.Open()); // nothing written yet
    CHECK(gate.Allows(Ask::Settings));
    CHECK(backup.calls == 0);

    for (const Ask ask : kEdits) {
        CHECK(gate.Allows(ask));
    }
    for (const Ask ask : kNever) {
        CHECK_FALSE(gate.Allows(ask));
    }
    CHECK(gate.Open());
    CHECK(backup.calls == 1);

    gate.SetEditing(false, false);
    CHECK_FALSE(gate.Open());
    gate.SetEditing(true, false);
    CHECK(gate.Open());
    CHECK(backup.calls == 1);
}

TEST_CASE("PokeDaisy gate: a failed backup refuses the whole session", "[frame_cores]") {
    Backup backup;
    backup.ok = false;
    EditGate gate = backup.Gate();
    gate.SetEditing(true, false);
    CHECK_FALSE(gate.Allows(Ask::FlyTo));
    backup.ok = true;
    CHECK_FALSE(gate.Allows(Ask::FlyTo));
    CHECK_FALSE(gate.Open());
    CHECK_FALSE(gate.Editing()); // the controls go too
    CHECK(backup.calls == 1);
    gate.SetEditing(false, true);
    CHECK_FALSE(gate.Closing());

    EditGate none{nullptr};
    none.SetEditing(true, false);
    CHECK_FALSE(none.Admit());
}

TEST_CASE("PokeDaisy gate: a step out runs on and nothing else", "[frame_cores]") {
    Backup backup;
    EditGate gate = backup.Gate();
    gate.SetEditing(true, false);
    REQUIRE(gate.Allows(Ask::FlyTo));
    gate.SetEditing(false, true);
    CHECK(gate.Closing());
    CHECK(gate.Open());
    CHECK_FALSE(gate.Editing());
    for (const Ask ask : kEdits) {
        CHECK_FALSE(gate.Allows(ask));
    }
    gate.StepsDone();
    CHECK_FALSE(gate.Closing());
    CHECK_FALSE(gate.Open());

    // Not opened: there is no step of ours to wait for.
    EditGate fresh = backup.Gate();
    fresh.SetEditing(true, false);
    fresh.SetEditing(false, true);
    CHECK_FALSE(fresh.Closing());
}

TEST_CASE("PokeDaisy toggles are neutral unless they may act", "[frame_cores]") {
    CHECK(ShownToggles(kAllOn, false, false) == Toggles{});
    CHECK(ShownToggles(kAllOn, false, true) == Toggles{}); // closing
    Toggles keys;
    keys.auto_run = true;
    CHECK(ShownToggles(kAllOn, true, false) == keys); // no backup yet: keys only
    CHECK(ShownToggles(kAllOn, true, true) == kAllOn);
    CHECK_FALSE(keys.Writes());
    CHECK(kAllOn.Writes());
}

TEST_CASE("PokeDaisy toggles held back stay the player's", "[frame_cores]") {
    // Off, RESET SETTINGS or any other row: shown neutral, still neutral.
    CHECK(ChangedToggles(kAllOn, {}, {}) == kAllOn);
    // On, one row changed.
    Toggles now = kAllOn;
    now.auto_run = false;
    CHECK(ChangedToggles(kAllOn, kAllOn, now) == now);
    // On, reset.
    CHECK(ChangedToggles(kAllOn, kAllOn, {}) == Toggles{});
}

TEST_CASE("PokeDaisy options that edit the game are its toggles", "[frame_cores]") {
    using Option = Common::AltCore::DriverOption;
    for (uint32_t i = 0; i < static_cast<uint32_t>(Option::Count); ++i) {
        const auto option = static_cast<Option>(i);
        INFO("option " << i);
        CHECK((FieldOf(option) != nullptr) == Common::AltCore::OptionEditsGame(option));
    }
    CHECK(FieldOf(Option::AutoRun) == &Toggles::auto_run);
    CHECK(FieldOf(Option::WildEncounters) == &Toggles::wild_off);
    CHECK(FieldOf(Option::TradeEvolutions) == &Toggles::trade_evos);
}

TEST_CASE("PokeDaisy settings the host owns", "[frame_cores]") {
    const HeldSettings s = DefaultSettings();
    CHECK(s.language == 0); // English, never AUTO
    CHECK(s.aspect == 0);   // ORIGINAL
    CHECK(s.game_speed == 0);
    CHECK(s.scene_speeds == 0);
    CHECK(s.fly_classic);
    CHECK(s.hud); // the player's: PokeDaisy's default, on
    CHECK_FALSE(s.cheats);
    CHECK_FALSE(s.rewind);
    CHECK(s.toggles == Toggles{});
}

TEST_CASE("PokeDaisy states wait for everything under way", "[frame_cores]") {
    CHECK(StateSafe({}));
    for (bool Pending::* field : {&Pending::asked, &Pending::step, &Pending::job, &Pending::keys,
                                  &Pending::flight, &Pending::closing}) {
        Pending p;
        p.*field = true;
        CHECK_FALSE(StateSafe(p));
    }
}

TEST_CASE("PokeDaisy overlay: as wide as the game's picture at 240 lines", "[frame_cores]") {
    CHECK(OverlayWidth(240, 160) == 360); // the 3DS's columns 20 to 380
    CHECK(OverlayWidth(160, 144) == 266); // 67 to 333
    CHECK(OverlayWidth(256, 144) == 400); // never wider than PokeDaisy's
    CHECK(OverlayWidth(240, 0) == 400);
}

TEST_CASE("PokeDaisy overlay: colours are premultiplied, the menu's dark under them",
          "[frame_cores]") {
    CHECK(OverlayPixel(0, false) == 0);
    CHECK(OverlayPixel(0x00FFFFFF, false) == 0); // nothing drawn adds nothing
    CHECK(OverlayPixel(0xFF112233, false) == 0xFF332211);
    CHECK(OverlayPixel(0x80FF8000, false) == 0x80004080);
    // Over black at 0x55: where nothing is drawn, behind glass, behind a letter.
    CHECK(OverlayPixel(0, true) == 0x55000000);
    CHECK(OverlayPixel(0x80FF8000, true) == 0xAA004080);
    CHECK(OverlayPixel(0xFF112233, true) == 0xFF332211);
}

TEST_CASE("PokeDaisy quick menu: its items go by the gate", "[frame_cores]") {
    std::vector<Quick> all;
    for (int i = 0; i < static_cast<int>(Quick::Count); ++i) {
        all.push_back(static_cast<Quick>(i));
    }
    CHECK(MenuItems(all, false).empty()); // all of it edits
    const std::vector<int> shown = MenuItems(all, true);
    for (const Quick item : all) {
        INFO(static_cast<int>(item));
        const bool ours = item == Quick::SaveState || item == Quick::LoadState ||
                          item == Quick::Ff || item == Quick::Companion || item == Quick::Hud;
        CHECK((NeedOf(AskOfItem(item)) == Need::Never) == ours);
        CHECK((std::find(shown.begin(), shown.end(), static_cast<int>(item)) == shown.end()) ==
              ours);
        Backup backup;
        EditGate gate = backup.Gate();
        CHECK_FALSE(gate.Allows(AskOfItem(item)));
        gate.SetEditing(true, false);
        CHECK(gate.Allows(AskOfItem(item)) == !ours);
        CHECK(backup.calls == (ours ? 0 : 1));
    }
    // The session's order is kept, with its indices.
    CHECK(MenuItems({Quick::Fly, Quick::SaveState, Quick::Bike, Quick::Ff}, true) ==
          std::vector<int>{0, 2});
}

TEST_CASE("PokeDaisy quick menu: the game stands unless something waits for it", "[frame_cores]") {
    CHECK(HoldsFrame(true, {}));
    CHECK_FALSE(HoldsFrame(false, {}));
    for (bool Pending::* field : {&Pending::asked, &Pending::step, &Pending::job, &Pending::keys,
                                  &Pending::closing}) {
        Pending p;
        p.*field = true;
        CHECK_FALSE(HoldsFrame(true, p));
        CHECK_FALSE(HoldsFrame(false, p));
    }
    Pending flying;
    flying.flight = true;
    CHECK(HoldsFrame(true, flying)); // LAND is in the menu
}

TEST_CASE("PokeDaisy touch: held every frame, let go once", "[frame_cores]") {
    TouchEdges touch;
    TouchEdges::Event e;
    CHECK_FALSE(touch.Next(false, 5, 5, e));

    REQUIRE(touch.Next(true, 10, 20, e));
    CHECK(e.down);
    CHECK(e.x == 10);
    CHECK(e.y == 20);
    REQUIRE(touch.Next(true, 12, 25, e)); // a drag
    CHECK(e.down);

    REQUIRE(touch.Next(false, 0, 0, e));
    CHECK_FALSE(e.down);
    CHECK(e.x == 12);
    CHECK(e.y == 25);
    CHECK_FALSE(touch.Next(false, 0, 0, e));

    REQUIRE(touch.Next(true, 999, 999, e));
    CHECK(e.x == 319);
    CHECK(e.y == 239);
    touch.Forget(); // a state load
    CHECK_FALSE(touch.Next(false, 0, 0, e));
}

TEST_CASE("PokeDaisy sound past a frame's is dropped", "[frame_cores]") {
    CHECK(AudioKept(548, 32768) == 548);
    CHECK(AudioKept(16384, 32768) == 1638);
}

TEST_CASE("PokeDaisy canvas colours", "[frame_cores]") {
    CHECK(CanvasToRgba(0x00112233) == 0xFF332211);
    CHECK(CanvasToRgba(0xFF112233) == 0xFF332211);
}

TEST_CASE("PokeDaisyKnows and the driver without a listed ROM", "[frame_cores]") {
    CHECK_FALSE(PokeDaisyKnows("/nonexistent/game.gba"));
    CHECK(MakePokeDaisyDriver({}) == nullptr);

    const auto path = std::filesystem::temp_directory_path() / "azaharvr_pokedaisy_test.gba";
    const auto write = [&](std::size_t size, const std::string& code) {
        std::vector<char> rom(size, 0);
        if (size >= 0xC0) {
            code.copy(rom.data() + 0xAC, 4);
        }
        std::ofstream(path, std::ios::binary).write(rom.data(), static_cast<std::streamsize>(size));
        return PokeDaisyKnows(path.string());
    };
    CHECK_FALSE(write(16, "BPRE"));
    // Right code, wrong bytes. PokeDaisy itself would read the Ruby, LeafGreen ones by code.
    CHECK_FALSE(write(0x10000, "AXVE"));
    CHECK_FALSE(write(0x10004, "BPGE"));
    CHECK_FALSE(write(0x10008, "BPEE"));
    CHECK_FALSE(write(0x1000C, "BPRE"));
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

#endif // HAVE_POKEDAISY
