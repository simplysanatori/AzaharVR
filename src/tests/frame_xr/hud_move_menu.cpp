// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// AzaharFrame hudmove: Move HUD In's two rows in the VR menu (the logic: tests/frame_xr/hud_move.cpp).

#include <catch2/catch_test_macros.hpp>
#include "frame_xr/immersive_names.h"
#include "frame_xr/menu/tab_menu.h"
#include "video_core/vr_hud_move.h"

TEST_CASE("FrameXr hudmove: the VR menu's rows", "[frame_xr][hudmove]") {
    using VideoCore::HudMove::kMaxPercent;
    using VideoCore::HudMove::kMinPercent;
    using FrameXr::Menu::Id;
    namespace Menu = FrameXr::Menu;
    for (Id id : {Id::HudAcross, Id::HudUpDown}) {
        REQUIRE(Menu::PerGameSetting(id));
        REQUIRE(Menu::SectionOf(id) == Menu::SettingsSection::Immersive);
        const Menu::SliderRange r = Menu::SliderFor(id);
        REQUIRE(r.min == static_cast<float>(kMinPercent));
        REQUIRE(r.max == static_cast<float>(kMaxPercent));
        REQUIRE(r.step == 1.0f);
        REQUIRE(!Menu::ExplainFor(id).empty());
    }
    REQUIRE(Menu::LabelFor(Id::HudAcross) == FrameXr::Immersive::Names::kMoveHudInHorizontal);
    REQUIRE(Menu::LabelFor(Id::HudUpDown) == FrameXr::Immersive::Names::kMoveHudInVertical);
}
