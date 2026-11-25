// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame hudmove: HUD Spacing's two rows in the VR menu (the logic: tests/frame_xr/hud_spacing.cpp).

#include <catch2/catch_test_macros.hpp>
#include "frame_xr/immersive_names.h"
#include "frame_xr/menu/tab_menu.h"
#include "video_core/vr_hud_spacing.h"

TEST_CASE("FrameXr hudmove: the VR menu's rows", "[frame_xr][hudmove]") {
    using VideoCore::HudSpacing::kMaxSpacing;
    using VideoCore::HudSpacing::kMinSpacing;
    using FrameXr::Menu::Id;
    namespace Menu = FrameXr::Menu;
    for (Id id : {Id::HudSpacingX, Id::HudSpacingY}) {
        REQUIRE(Menu::PerGameSetting(id));
        REQUIRE(Menu::SectionOf(id) == Menu::SettingsSection::Immersive);
        const Menu::SliderRange r = Menu::SliderFor(id);
        REQUIRE(r.min == static_cast<float>(kMinSpacing));
        REQUIRE(r.max == static_cast<float>(kMaxSpacing));
        REQUIRE(r.step == 1.0f);
        REQUIRE(!Menu::ExplainFor(id).empty());
    }
    REQUIRE(Menu::LabelFor(Id::HudSpacingX) == FrameXr::Immersive::Names::kHudSpacingHorizontal);
    REQUIRE(Menu::LabelFor(Id::HudSpacingY) == FrameXr::Immersive::Names::kHudSpacingVertical);
}

// AzaharFrame kiu140: gone while Angel's Aim's own HUD is In View; every other game has them.
TEST_CASE("FrameXr hudmove: not beside Angel's Aim's HUD", "[frame_xr][hudmove][kiu140]") {
    namespace Menu = FrameXr::Menu;
    REQUIRE(Menu::HudSpacingRowsShown(false, 1)); // another game, or another mode
    REQUIRE(Menu::HudSpacingRowsShown(false, 0));
    REQUIRE(Menu::HudSpacingRowsShown(true, 0)); // Game's Own
    REQUIRE_FALSE(Menu::HudSpacingRowsShown(true, 1));
    for (const auto id :
         {Menu::Id::KiuHud, Menu::Id::KiuHudSize, Menu::Id::KiuHudOpacity, Menu::Id::KiuHudLinked,
          Menu::Id::KiuHudAcross, Menu::Id::KiuHudHeight, Menu::Id::KiuBarAcross,
          Menu::Id::KiuBarHeight,
          Menu::Id::KiuScoreAcross, Menu::Id::KiuScoreHeight, Menu::Id::KiuSubtitleHeight,
          Menu::Id::KiuSubtitleSize}) {
        REQUIRE(Menu::PerGameSetting(id));
    }
}
