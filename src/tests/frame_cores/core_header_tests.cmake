# The tests that include the cores' headers (see src/frame_cores), as object libraries. Included by
# the tests' directory, on Windows by this one.
# An object of its own: the tests have no melonDS headers
add_library(nds_resolution_tests OBJECT EXCLUDE_FROM_ALL ${CMAKE_CURRENT_LIST_DIR}/nds_resolution_tests.cpp)
target_link_libraries(nds_resolution_tests PRIVATE frame_cores melonds_core citra_common catch2
    Threads::Threads)
# melonDS's shaders, as melonds_hires has them
target_include_directories(nds_resolution_tests PRIVATE
    ${CMAKE_BINARY_DIR}/externals/melonds_cmake/hires)
add_dependencies(nds_resolution_tests melonds_hires)
if (TARGET gbads_game)
    # An object of its own: gba-ds's header names (config.h, emu.h, ui.h) stay out of the others
    add_library(gbads_driver_tests OBJECT EXCLUDE_FROM_ALL ${CMAKE_CURRENT_LIST_DIR}/gbads_driver_tests.cpp)
    target_link_libraries(gbads_driver_tests PRIVATE frame_cores gbads_game citra_common catch2
        Threads::Threads)
endif()
