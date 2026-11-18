// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame kiumode: Immersive Mode's Angel's Aim (stored 4): drawn as mode 3 everywhere, first
// person only where it is in effect, its registers supplied in memory only.

#include <string>
#include <catch2/catch_test_macros.hpp>
#include "common/settings.h"
#include "frame_xr/immersive_names.h"
#include "frame_xr/kiu_first_person.h"
#include "frame_xr/presets.h"

namespace {

namespace FP = FrameXr::Kiu::FirstPerson;
namespace N = FrameXr::Immersive::Names;

constexpr uint64_t kEur = 0x0004000000030200ull;
constexpr uint64_t kUsa = 0x0004000000030100ull;
constexpr uint64_t kJpnMod = 0x0004000000030000ull;
constexpr uint64_t kAsphalt = 0x0004000000034C00ull; // Asphalt 3D

} // namespace

TEST_CASE("Angel's Aim: stored values and the 4 to 3 mapping", "[frame_xr][kiumode]") {
    STATIC_REQUIRE(Settings::kImmersiveModeAngelsAim == 4);
    STATIC_REQUIRE(Settings::kImmersiveModeOtherGames == 3);
    STATIC_REQUIRE(N::kModeStoredAngelsAim == Settings::kImmersiveModeAngelsAim);
    STATIC_REQUIRE(FP::kModeAngelsAim == Settings::kImmersiveModeAngelsAim);
    for (u32 m : {0u, 1u, 2u, 3u}) {
        CHECK(Settings::ImmersiveRenderModeOf(m) == m);
    }
    CHECK(Settings::ImmersiveRenderModeOf(4) == 3);
    auto& mode = Settings::values.vr_immersive_mode;
    const bool global = mode.UsingGlobal();
    const u32 before = mode.GetValue();
    mode.SetGlobal(false);
    mode.SetValue(4);
    CHECK(mode.GetValue() == 4u); // stored as chosen: never clamped
    CHECK(Settings::ImmersiveRenderMode() == 3u);
    mode.SetValue(before);
    mode.SetGlobal(global);
}

TEST_CASE("Angel's Aim: first person's truth table", "[frame_xr][kiumode]") {
    // In effect: mode 4, the European game, its code the mapped one.
    CHECK(FP::AngelsAimFor(4, kEur, true, true));
    CHECK(!FP::AngelsAimFor(4, kEur, false, true)); // code not checked yet, or another code
    CHECK(!FP::AngelsAimFor(4, kUsa, true, true));  // other regions: plays as Other Games
    CHECK(!FP::AngelsAimFor(4, kJpnMod, true, true));
    CHECK(!FP::AngelsAimFor(4, kAsphalt, true, true)); // a stored 4 elsewhere: as 3
    for (u32 m : {0u, 1u, 2u, 3u}) {
        CHECK(!FP::AngelsAimFor(m, kEur, true, true)); // Other Games is not first person
    }
    // In Flat (no VR session) no Immersive Mode does anything, Angel's Aim neither.
    CHECK(!FP::AngelsAimFor(4, kEur, true, false));
    CHECK(!FP::OnFor(false, 4, kEur, true, false));
    CHECK(FP::OnFor(true, 4, kEur, true, false)); // the developer switch: as before
    // On: Angel's Aim in effect, or the developer switch (whatever the mode).
    CHECK(FP::OnFor(false, 4, kEur, true, true));
    CHECK(!FP::OnFor(false, 3, kEur, true, true));
    CHECK(!FP::OnFor(false, 4, kUsa, true, true));
    CHECK(!FP::OnFor(false, 4, kEur, false, true));
    CHECK(FP::OnFor(true, 0, kEur, true, true));
    CHECK(FP::OnFor(true, 3, kUsa, false, true));
    // Wanted (the hooks say why it is not in effect): any region of the game, or the switch.
    CHECK(FP::WantedFor(false, 4, kUsa));
    CHECK(FP::WantedFor(false, 4, kJpnMod));
    CHECK(FP::WantedFor(false, 4, kEur));
    CHECK(!FP::WantedFor(false, 4, kAsphalt));
    CHECK(!FP::WantedFor(false, 3, kEur));
    CHECK(FP::WantedFor(true, 0, kAsphalt));
}

TEST_CASE("Angel's Aim: the registers are supplied in memory, never stored",
          "[frame_xr][kiumode]") {
    auto& reg = Settings::values.vr_si_mode_register_offset;
    auto& eye = Settings::values.vr_immersive_eye_indicator;
    // The settings keep their keys (the config file's names).
    CHECK(reg.GetLabel() == "vr_si_mode_register_offset");
    CHECK(eye.GetLabel() == "vr_immersive_eye_indicator");
    CHECK(reg.GetDefault() == -1);
    CHECK(eye.GetDefault().empty());
    const bool reg_global = reg.UsingGlobal(), eye_global = eye.UsingGlobal();
    reg.SetGlobal(true);
    eye.SetGlobal(true);
    REQUIRE(reg.GetValue() == -1);
    REQUIRE(eye.GetValue().empty());
    static const int32_t kReg = 90;
    static const std::string kEye = "87,2";
    reg.Supply(&kReg);
    eye.Supply(&kEye);
    // What the renderer reads
    CHECK(reg.GetValue() == 90);
    CHECK(eye.GetValue() == "87,2");
    // What the config writer, the dialogs and the presets' checks read: the stored values
    CHECK(reg.GetValue(false) == -1);
    CHECK(reg.GetValue(true) == -1);
    CHECK(eye.GetValue(false).empty());
    CHECK(reg.UsingGlobal());
    CHECK(eye.UsingGlobal());
    // A game's own value stays its own underneath
    reg.SetGlobal(false);
    reg.SetValue(12);
    CHECK(reg.GetValue() == 90);
    CHECK(reg.GetValue(false) == 12);
    CHECK(reg.GetValue(true) == -1);
    reg.SetGlobal(true);
    // Supply ends: the settings' own again
    reg.Supply(nullptr);
    eye.Supply(nullptr);
    CHECK(reg.GetValue() == -1);
    CHECK(eye.GetValue().empty());
    reg.SetGlobal(reg_global);
    eye.SetGlobal(eye_global);
}

TEST_CASE("Angel's Aim: Kid Icarus: Uprising's preset stays Off", "[frame_xr][kiumode]") {
    for (uint64_t t : {kJpnMod, kUsa, kEur, uint64_t{0x0004000000030300ull}}) {
        const FrameXr::ImmersivePreset* p = FrameXr::FindImmersivePreset(t);
        REQUIRE(p);
        CHECK(p->mode == 0);
        CHECK(p->register_offset == -1);
        CHECK(p->eye_indicator == nullptr);
    }
}

TEST_CASE("Angel's Aim: the list's order, labels and stored values", "[frame_xr][kiumode][kiurows]") {
    // The owner's order, the same in every game and in Global (AzaharFrame kiurows): Off, Wide,
    // Zelda, Angel's Aim, Other Games.
    CHECK(std::string(N::kModeChoices[0]) == "Off");
    CHECK(std::string(N::kModeChoices[1]) == "Wide (3DoF)");
    CHECK(std::string(N::kModeChoices[2]) == "Full (6DoF): Ocarina of Time, Majora's Mask");
    CHECK(std::string(N::kModeChoices[3]) == "Full (6DoF): Kid Icarus: Uprising / Angel's Aim");
    CHECK(std::string(N::kModeChoices[4]) == "Full (6DoF): Other Games");
    CHECK(N::kModeChoices.size() == static_cast<std::size_t>(N::kModeChoiceCount));
    // index <-> stored, both ways
    const u32 values[5] = {0, 1, 2, 4, 3};
    for (int i = 0; i < 5; i++) {
        CHECK(N::ModeValueAt(i) == values[i]);
        CHECK(N::ModeIndexOf(values[i]) == i);
    }
    // A stored 4 keeps its value when the row is left on its choice; another choice changes it
    CHECK(N::ModeValueAt(3, 4) == 4u);
    CHECK(N::ModeValueAt(2, 4) == 2u);
    CHECK(N::ModeValueAt(4, 3) == 3u);
    CHECK(N::ModeValueAt(9) == 3u); // past the list: its last choice
    CHECK(N::ModeIndexOf(9) == 4);  // CitraVR's test value shows as Other Games
    CHECK(N::ModeValueAt(4, 9) == 9u); // and is kept while that choice stays
    // Outside Kid Icarus: Uprising a stored 4 plays as Other Games (stored value unchanged)
    CHECK(Settings::ImmersiveRenderModeOf(4) == 3u);
}
