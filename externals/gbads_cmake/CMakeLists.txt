cmake_minimum_required(VERSION 3.20)

if (POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()
if (POLICY CMP0126)
    cmake_policy(SET CMP0126 NEW)
endif()
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)

find_package(Threads REQUIRED)

set(MGBA_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../mgba")
set(GBADS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../gba-ds/src")
set(GBADS_RZSTD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../gba-ds/third_party/rzstd")

# Build mGBA's bundled zlib directly as a private static library with hidden
# symbol visibility so we:
#  1) never depend on system zlib or pkg-config,
#  2) never touch mGBA's bundled zlib/CMakeLists.txt (which fails under CMake 4+
#     and renames zconf.h in the source tree),
#  3) never interpose zlib symbols against system shared libs (e.g. Qt/libpng).
set(MGBA_ZLIB_DIR "${MGBA_SRC_DIR}/src/third-party/zlib")
add_library(mgba_zlib STATIC
    "${MGBA_ZLIB_DIR}/adler32.c"
    "${MGBA_ZLIB_DIR}/compress.c"
    "${MGBA_ZLIB_DIR}/crc32.c"
    "${MGBA_ZLIB_DIR}/deflate.c"
    "${MGBA_ZLIB_DIR}/gzclose.c"
    "${MGBA_ZLIB_DIR}/gzlib.c"
    "${MGBA_ZLIB_DIR}/gzread.c"
    "${MGBA_ZLIB_DIR}/gzwrite.c"
    "${MGBA_ZLIB_DIR}/inflate.c"
    "${MGBA_ZLIB_DIR}/infback.c"
    "${MGBA_ZLIB_DIR}/inftrees.c"
    "${MGBA_ZLIB_DIR}/inffast.c"
    "${MGBA_ZLIB_DIR}/trees.c"
    "${MGBA_ZLIB_DIR}/uncompr.c"
    "${MGBA_ZLIB_DIR}/zutil.c"
)
target_include_directories(mgba_zlib PUBLIC "${MGBA_ZLIB_DIR}")
set_target_properties(mgba_zlib PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
    INTERPROCEDURAL_OPTIMIZATION FALSE
)
if (UNIX)
    target_compile_definitions(mgba_zlib PRIVATE HAVE_UNISTD_H=1 Z_HAVE_UNISTD_H=1)
endif()
if (MSVC)
    target_compile_definitions(mgba_zlib PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
    target_compile_options(mgba_zlib PRIVATE /W0)
else()
    target_compile_options(mgba_zlib PRIVATE -w)
endif()

# Save any parent OpenGL cache entries that mGBA's CMakeLists.txt unconditionally
# clears when BUILD_GL / BUILD_GLES2 / BUILD_GLES3 are OFF.
set(_GBADS_SAVED_OPENGL_LIB_CACHED FALSE)
if (DEFINED CACHE{OPENGL_LIBRARY})
    set(_GBADS_SAVED_OPENGL_LIB_CACHED TRUE)
    set(_GBADS_SAVED_OPENGL_LIB "$CACHE{OPENGL_LIBRARY}")
endif()
set(_GBADS_SAVED_OPENGLES2_LIB_CACHED FALSE)
if (DEFINED CACHE{OPENGLES2_LIBRARY})
    set(_GBADS_SAVED_OPENGLES2_LIB_CACHED TRUE)
    set(_GBADS_SAVED_OPENGLES2_LIB "$CACHE{OPENGLES2_LIBRARY}")
endif()

# Configure mGBA via local (non-cache) variables matching gba-ds/scripts/mgba-options.sh
# except M_CORE_GB=ON (GB/GBC enabled). Keeping them non-cache prevents colliding with
# Azahar's own options (e.g. ENABLE_SCRIPTING).
set(LIBMGBA_ONLY OFF)
set(BUILD_STATIC ON)
set(BUILD_SHARED OFF)
set(BUILD_LTO OFF)
set(BUILD_SDL OFF)
set(BUILD_QT OFF)
set(BUILD_LIBRETRO OFF)
set(BUILD_GL OFF)
set(BUILD_GLES2 OFF)
set(BUILD_GLES3 OFF)
set(BUILD_PERF OFF)
set(BUILD_TEST OFF)
set(BUILD_SUITE OFF)
set(BUILD_CINEMA OFF)
set(BUILD_ROM_TEST OFF)
set(BUILD_UPDATER OFF)
set(BUILD_HEADLESS OFF)
set(BUILD_PYTHON OFF)
set(BUILD_EXAMPLE OFF)
set(BUILD_DOCGEN OFF)
set(BUILD_MAINTAINER_TOOLS OFF)
set(USE_EPOXY OFF)
set(ENABLE_DEBUGGERS OFF)
set(ENABLE_GDB_STUB OFF)
set(USE_EDITLINE OFF)
set(USE_SQLITE3 OFF)
set(USE_ELF OFF)
set(USE_LZMA ON)
set(USE_ZLIB ON)
set(USE_PNG OFF)
set(USE_LIBZIP OFF)
set(USE_MINIZIP OFF)
set(USE_FFMPEG OFF)
set(USE_LUA OFF)
set(USE_JSON_C OFF)
set(USE_DISCORD_RPC OFF)
set(USE_FREETYPE OFF)
set(M_CORE_GBA ON)
set(M_CORE_GB ON)
set(ENABLE_SCRIPTING OFF)
set(DISABLE_DEPS OFF)
set(DISTBUILD OFF)
set(QUIET ON)

# Pre-satisfy ZLIB with our bundled mgba_zlib and prevent host pkg-config / markdown discovery.
set(ZLIB_FOUND TRUE)
set(ZLIB_INCLUDE_DIRS "${MGBA_ZLIB_DIR}")
set(ZLIB_INCLUDE_DIR "${MGBA_ZLIB_DIR}")
set(ZLIB_LIBRARIES mgba_zlib)
set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB ON)
set(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig ON)
set(MARKDOWN NOTFOUND)

if (WIN32)
    # AzaharVR: mGBA's CMakeLists.txt stops on Windows without epoxy, unless it is told that only
    # the library is wanted; it then takes zlib and its zip files out, which are added again below.
    set(LIBMGBA_ONLY ON)
    # Its project() has to see the build's own compiler: this file comes right after it.
    set(CMAKE_PROJECT_mGBA_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/win32/mgba_project.cmake")
endif()
add_subdirectory("${MGBA_SRC_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/mgba" EXCLUDE_FROM_ALL)
if (WIN32)
    # AzaharVR: what mGBA's CMakeLists.txt does for USE_ZLIB without a minizip of the system's
    set(MGBA_MINIZIP_DIR "${MGBA_ZLIB_DIR}/contrib/minizip")
    target_sources(mgba PRIVATE
        "${MGBA_SRC_DIR}/src/util/vfs/vfs-zip.c"
        "${MGBA_MINIZIP_DIR}/ioapi.c"
        "${MGBA_MINIZIP_DIR}/unzip.c"
        "${MGBA_MINIZIP_DIR}/zip.c"
    )
    target_compile_definitions(mgba PRIVATE USE_ZLIB USE_MINIZIP HAVE_CRC32)
    target_include_directories(mgba PRIVATE "${MGBA_ZLIB_DIR}" "${MGBA_ZLIB_DIR}/contrib")
    target_link_libraries(mgba mgba_zlib)
    target_compile_options(mgba PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/win32/mgba_shim.h")
    alt_cores_compiler()

    # AzaharVR: the POSIX names gba-ds's and PokeDaisy's files use. Only for targets that link this.
    add_library(win32_posix STATIC EXCLUDE_FROM_ALL
        win32/mgba_shim.h
        win32/posix/win32_posix.c
        win32/posix/win32_posix.h
    )
    target_include_directories(win32_posix PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/win32"
        "${CMAKE_CURRENT_SOURCE_DIR}/win32/posix"
    )
    target_compile_definitions(win32_posix PUBLIC
        WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE
        STBIW_WINDOWS_UTF8
    )
    target_compile_options(win32_posix INTERFACE "/FI${CMAKE_CURRENT_SOURCE_DIR}/win32/posix/win32_posix.h")
    target_link_libraries(win32_posix PRIVATE mgba)
    set_target_properties(win32_posix PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        INTERPROCEDURAL_OPTIMIZATION FALSE
    )
endif()

# Restore parent OpenGL cache entries if mGBA overwrote them.
if (_GBADS_SAVED_OPENGL_LIB_CACHED)
    set(OPENGL_LIBRARY "${_GBADS_SAVED_OPENGL_LIB}" CACHE PATH "" FORCE)
else()
    unset(OPENGL_LIBRARY CACHE)
endif()
if (_GBADS_SAVED_OPENGLES2_LIB_CACHED)
    set(OPENGLES2_LIBRARY "${_GBADS_SAVED_OPENGLES2_LIB}" CACHE PATH "" FORCE)
else()
    unset(OPENGLES2_LIBRARY CACHE)
endif()

# Propagate mGBA's public feature macros to downstream consumers (gbads_core and Azahar).
# mGBA stores these as private COMPILE_DEFINITIONS on the mgba target, not INTERFACE.
# These decide the layout of mGBA's structs, so they must match what mGBA itself is built with
# (its CMakeLists.txt picks USE_PTHREADS by itself). Without USE_PTHREADS, mCoreRewindContext is
# 64 bytes here and 168 in mGBA, which then writes past the end of ours.
target_compile_definitions(mgba INTERFACE
    M_CORE_GBA
    $<$<NOT:$<PLATFORM_ID:Windows>>:USE_PTHREADS>
    M_CORE_GB
    BUILD_STATIC
    ENABLE_VFS
    ENABLE_VFS_FD
    ENABLE_DIRECTORIES
    USE_ZLIB
    USE_MINIZIP
    USE_LZMA
)

# Isolate mGBA's bundled inih symbols from Azahar's externals/inih so linking both
# static libraries into the same binary never produces duplicate ini_parse* symbols.
target_compile_definitions(mgba PRIVATE
    ini_parse=mgba_ini_parse
    ini_parse_file=mgba_ini_parse_file
    ini_parse_stream=mgba_ini_parse_stream
    ini_parse_string=mgba_ini_parse_string
)
set_target_properties(mgba PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION FALSE
)
if (MSVC)
    target_compile_options(mgba PRIVATE /W0)
else()
    target_compile_options(mgba PRIVATE -w)
endif()
if (COMMAND target_disable_warnings)
    target_disable_warnings(mgba)
endif()

# Read gba-ds version string for config.c (keeps externals/gba-ds completely unpatched).
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../gba-ds/VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../gba-ds/VERSION" GBADS_VERSION_STR LIMIT_COUNT 1)
else()
    set(GBADS_VERSION_STR "dev")
endif()

# Build SDL-free reusable C modules from gba-ds + vendored rzstd (for RZIP v2 states)
# + minimal i18n stubs.
add_library(gbads_core STATIC
    "${GBADS_SRC_DIR}/emu.c"
    "${GBADS_SRC_DIR}/mapcap.c"
    "${GBADS_SRC_DIR}/profiles.c"
    "${GBADS_SRC_DIR}/logo_masks.c"
    "${GBADS_SRC_DIR}/cheats.c"
    "${GBADS_SRC_DIR}/patch.c"
    "${GBADS_SRC_DIR}/config.c"
    "${GBADS_SRC_DIR}/clocks.c"
    "${GBADS_RZSTD_DIR}/src/encoding_rzstd.c"
    "${GBADS_RZSTD_DIR}/src/encoding_xxh64.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs/gbads_stubs.c"
)

target_include_directories(gbads_core
    PUBLIC
        "${GBADS_SRC_DIR}"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
        "${GBADS_RZSTD_DIR}/include"
)

# Propagate mGBA's public headers and feature defines (M_CORE_GBA, M_CORE_GB, etc.) to gbads_core.
target_link_libraries(gbads_core
    PUBLIC
        mgba
    PRIVATE
        Threads::Threads
)

target_compile_definitions(gbads_core PRIVATE
    GBADS_VERSION="${GBADS_VERSION_STR}"
    _GNU_SOURCE
)

if (WIN32) # AzaharVR
    target_link_libraries(gbads_core PRIVATE win32_posix)
endif()

find_library(GBADS_MATH_LIBRARY m)
if (GBADS_MATH_LIBRARY)
    target_link_libraries(gbads_core PRIVATE ${GBADS_MATH_LIBRARY})
endif()

set_target_properties(gbads_core PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION FALSE
)

if (MSVC)
    target_compile_options(gbads_core PRIVATE /W0)
else()
    target_compile_options(gbads_core PRIVATE -w)
endif()
if (COMMAND target_disable_warnings)
    target_disable_warnings(gbads_core)
endif()

# gba-ds's game.c (its second screens) and logo.c, whole and unmodified, behind a host interface: README.md.
# POSIX only (pthread.h, dirent.h, typeof): without it a game has its maps alone.
# AzaharVR: on Windows win32/ has what they use of POSIX.
if ((CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32) AND EXISTS "${GBADS_SRC_DIR}/game.c"
    AND EXISTS "${GBADS_SRC_DIR}/logo.c")
    add_library(gbads_game STATIC EXCLUDE_FROM_ALL
        "${CMAKE_CURRENT_SOURCE_DIR}/game/gbads_game.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/game/gbads_game.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/game/gbads_logo.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/game/gbads_logo.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/game/gbads_rename.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/game/gbads_ui_stubs.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/game/sdl/SDL.h"
    )
    # Our SDL.h, ahead of any real one, for this object alone
    target_include_directories(gbads_game BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/game/sdl")
    target_include_directories(gbads_game PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/game")
    target_link_libraries(gbads_game PUBLIC gbads_core PRIVATE Threads::Threads)
    if (GBADS_MATH_LIBRARY)
        target_link_libraries(gbads_game PRIVATE ${GBADS_MATH_LIBRARY})
    endif()
    target_compile_definitions(gbads_game PRIVATE
        GBADS_VERSION="${GBADS_VERSION_STR}"
        _GNU_SOURCE
    )
    set_target_properties(gbads_game PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS ON # typeof
        C_VISIBILITY_PRESET hidden
        POSITION_INDEPENDENT_CODE ON
        INTERPROCEDURAL_OPTIMIZATION FALSE
        UNITY_BUILD OFF
    )
    if (WIN32) # AzaharVR
        target_link_libraries(gbads_game PRIVATE win32_posix)
        target_compile_options(gbads_game PRIVATE /W0)
    else()
    target_compile_options(gbads_game PRIVATE -w)
    endif()
    if (COMMAND target_disable_warnings)
        target_disable_warnings(gbads_game)
    endif()
endif()
