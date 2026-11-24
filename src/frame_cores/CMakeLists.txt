add_library(frame_cores STATIC EXCLUDE_FROM_ALL
    alt_core_host.cpp
    alt_core_host.h
    audio_resampler.cpp
    audio_resampler.h
    cheat_store.h
    core_backend.h
    gb_core.cpp
    gb_core.h
    gba_core.cpp
    gba_core.h
    melonds_platform.cpp
    nds_core.cpp
    nds_core.h
    microphone.cpp
    microphone.h
    profile_input.cpp
    profile_input.h
    rom_info.cpp
    rom_info.h
    second_screen.cpp
    second_screen.h
)

create_target_directory_groups(frame_cores)

target_include_directories(frame_cores SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/externals/melonDS/src
    ${CMAKE_SOURCE_DIR}/externals/gba-ds/src
    ${CMAKE_SOURCE_DIR}/externals/mgba/include
)

target_link_libraries(frame_cores
    PUBLIC
        citra_common
        citra_core
        audio_core
        video_core
    PRIVATE
        melonds_core
        gbads_core
        mgba
        Threads::Threads
)

if (USE_SYSTEM_FAAD)
    target_link_libraries(frame_cores PRIVATE faad)
else()
    target_link_libraries(frame_cores PRIVATE faad2)
endif()
