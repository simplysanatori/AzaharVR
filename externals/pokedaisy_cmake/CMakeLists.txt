cmake_minimum_required(VERSION 3.20)

# PokeDaisy's shared C code (companion pages, quick menu, field editing), built as a static library against
# our mGBA. Needs gbads_cmake's `mgba` target, so add that directory first. frame_cores links it.
# PokeDaisy is GPL-3.0 with section 7(b) terms: see README.md here.

set(PD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../pokedaisy")

if (NOT TARGET mgba)
    message(STATUS "PokeDaisy: skipped (no mgba target; add gbads_cmake first)")
    return()
endif()
if (NOT EXISTS "${PD_DIR}/3ds/host/pd_session.h")
    message(STATUS "PokeDaisy: skipped (externals/pokedaisy has no sources)")
    return()
endif()

set(PD_CPP "${PD_DIR}/app/src/main/cpp")

if (WIN32) # AzaharVR
    alt_cores_compiler()
endif()

add_library(pokedaisy STATIC EXCLUDE_FROM_ALL
    # 3ds/core minus pd_library* and pd_save* (their own library and save files; the host supplies those)
    "${PD_DIR}/3ds/core/pd_binput.c"
    "${PD_DIR}/3ds/core/pd_boxes.c"
    "${PD_DIR}/3ds/core/pd_canvas.c"
    "${PD_DIR}/3ds/core/pd_card.c"
    "${PD_DIR}/3ds/core/pd_dex.c"
    "${PD_DIR}/3ds/core/pd_dex_details.c"
    "${PD_DIR}/3ds/core/pd_experience.c"
    "${PD_DIR}/3ds/core/pd_experience_gen.c"
    "${PD_DIR}/3ds/core/pd_field_input.c"
    "${PD_DIR}/3ds/core/pd_font_gen.c"
    "${PD_DIR}/3ds/core/pd_game.c"
    "${PD_DIR}/3ds/core/pd_games_gen.c"
    "${PD_DIR}/3ds/core/pd_gb_art_gen.c"
    "${PD_DIR}/3ds/core/pd_gb.c"
    "${PD_DIR}/3ds/core/pd_gb_dex.c"
    "${PD_DIR}/3ds/core/pd_gb_gen.c"
    "${PD_DIR}/3ds/core/pd_gb_map.c"
    "${PD_DIR}/3ds/core/pd_gb_polished.c"
    "${PD_DIR}/3ds/core/pd_gb_world.c"
    "${PD_DIR}/3ds/core/pd_gb_world_gen.c"
    "${PD_DIR}/3ds/core/pd_guide.c"
    "${PD_DIR}/3ds/core/pd_guide_gen.c"
    "${PD_DIR}/3ds/core/pd_icon.c"
    "${PD_DIR}/3ds/core/pd_icons_gen.c"
    "${PD_DIR}/3ds/core/pd_map.c"
    "${PD_DIR}/3ds/core/pd_map_gen.c"
    "${PD_DIR}/3ds/core/pd_menu.c"
    "${PD_DIR}/3ds/core/pd_nuzlocke.c"
    "${PD_DIR}/3ds/core/pd_party_art.c"
    "${PD_DIR}/3ds/core/pd_randomized.c"
    "${PD_DIR}/3ds/core/pd_randomized_gen.c"
    "${PD_DIR}/3ds/core/pd_romart.c"
    "${PD_DIR}/3ds/core/pd_settings.c"
    "${PD_DIR}/3ds/core/pd_sha1.c"
    "${PD_DIR}/3ds/core/pd_shiny_gen.c"
    "${PD_DIR}/3ds/core/pd_smol.c"
    "${PD_DIR}/3ds/core/pd_snapshot.c"
    "${PD_DIR}/3ds/core/pd_stats.c"
    "${PD_DIR}/3ds/core/pd_stats_gen.c"
    "${PD_DIR}/3ds/core/pd_style.c"
    "${PD_DIR}/3ds/core/pd_tables.c"
    "${PD_DIR}/3ds/core/pd_tables_gen.c"
    "${PD_DIR}/3ds/core/pd_text_gen.c"
    "${PD_DIR}/3ds/core/pd_tr.c"
    "${PD_DIR}/3ds/core/pd_tr_gen.c"
    "${PD_DIR}/3ds/core/pd_ui_bag.c"
    "${PD_DIR}/3ds/core/pd_ui_battle.c"
    "${PD_DIR}/3ds/core/pd_ui_battle_controls.c"
    "${PD_DIR}/3ds/core/pd_ui_battle_gen5.c"
    "${PD_DIR}/3ds/core/pd_ui_boxes.c"
    "${PD_DIR}/3ds/core/pd_ui.c"
    "${PD_DIR}/3ds/core/pd_ui_card.c"
    "${PD_DIR}/3ds/core/pd_ui_dex.c"
    "${PD_DIR}/3ds/core/pd_ui_guide.c"
    "${PD_DIR}/3ds/core/pd_ui_map.c"
    "${PD_DIR}/3ds/core/pd_ui_nuzlocke.c"
    "${PD_DIR}/3ds/core/pd_ui_party.c"
    "${PD_DIR}/3ds/core/pd_ui_settings.c"
    "${PD_DIR}/3ds/core/pd_ui_states.c"
    "${PD_DIR}/3ds/core/pd_ui_widgets.c"

    # Session and what it needs. pd_mixer.c is left out on purpose: see stubs/pokedaisy_stubs.c.
    "${PD_DIR}/3ds/host/pd_session.c"
    "${PD_DIR}/3ds/host/pd_items.c"
    "${PD_DIR}/3ds/host/pd_rom_options.c"

    "${PD_DIR}/linux/fly.c"
    "${PD_DIR}/linux/flash.c"
    "${PD_DIR}/linux/flight.c"
    "${PD_DIR}/linux/field_items.c"
    "${PD_DIR}/linux/tm_teach.c"
    "${PD_DIR}/linux/wild.c"
    "${PD_DIR}/linux/hud.c"
    "${PD_DIR}/linux/quick_menu.c"
    "${PD_DIR}/linux/gb_field.c"
    "${PD_DIR}/linux/polished_field.c"

    "${PD_CPP}/pk_call.c"
    "${PD_CPP}/pk_flight.c"
    "${PD_CPP}/pk_host.c"
    "${PD_CPP}/pk_gb_field.c"
    "${PD_CPP}/pk_cheats.c"
    "${PD_CPP}/pk_wide.c"
    "${PD_CPP}/pk_wide_gb.c"

    "${CMAKE_CURRENT_SOURCE_DIR}/stubs/pokedaisy_stubs.c"
)

target_include_directories(pokedaisy
    PUBLIC
        "${PD_DIR}"
        "${PD_DIR}/3ds/core"
        "${PD_DIR}/3ds/host"
        "${PD_DIR}/linux"
        "${PD_CPP}"
)

# For consumers: not our warnings to fix.
target_include_directories(pokedaisy SYSTEM INTERFACE
    "${PD_DIR}"
    "${PD_DIR}/3ds/core"
    "${PD_DIR}/3ds/host"
    "${PD_DIR}/linux"
    "${PD_CPP}"
)
target_compile_definitions(pokedaisy PUBLIC HAVE_POKEDAISY)

# mGBA's headers and its INTERFACE feature defines (USE_PTHREADS etc.), which decide struct layouts and so
# must match the mgba target exactly. Taken from the target rather than retyped.
target_link_libraries(pokedaisy PRIVATE mgba)

target_compile_definitions(pokedaisy PRIVATE
    _GNU_SOURCE
    # Without it pd_session.c misses seven pk_wide_* declarations.
    PK_WIDE_CACHE
    # pk_wide_view would install process-wide SIGSEGV/BUS/FPE/ILL handlers over our crash reporter and the JITs.
    PK_WIDE_NO_GUARD
    # EXPAND's buffers: 400x240 (about 10 MB of zeroed data) instead of upstream's 800x800.
    PK_WIDE_MAX_VIEW_W=400
    PK_WIDE_MAX_VIEW_H=240
    # gba-ds's ui.h also exports ui_notice.
    ui_notice=pd_ui_notice
)

# Forced include: the mGBA names PokeDaisy uses that our mGBA renamed.
if (WIN32) # AzaharVR: clang-cl's spelling, and the POSIX names (gbads_cmake/win32)
    target_link_libraries(pokedaisy PRIVATE win32_posix)
    target_compile_options(pokedaisy PRIVATE
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/stubs/pokedaisy_shim.h"
    )
else()
target_compile_options(pokedaisy PRIVATE
    -include "${CMAKE_CURRENT_SOURCE_DIR}/stubs/pokedaisy_shim.h"
)
endif()

set_target_properties(pokedaisy PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    # 170 names have no pd_/pk_ prefix (hud_*, fly_*, ui_*): keep them out of the binary's symbol table.
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION FALSE
)

find_library(POKEDAISY_MATH_LIBRARY m)
if (POKEDAISY_MATH_LIBRARY)
    target_link_libraries(pokedaisy PRIVATE ${POKEDAISY_MATH_LIBRARY})
endif()

# Third-party sources, not ours to fix.
if (MSVC)
    target_compile_options(pokedaisy PRIVATE /W0)
else()
    target_compile_options(pokedaisy PRIVATE -w)
endif()
if (COMMAND target_disable_warnings)
    target_disable_warnings(pokedaisy)
endif()
