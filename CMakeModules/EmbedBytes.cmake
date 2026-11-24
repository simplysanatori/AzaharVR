# Files as C++ byte arrays, made at build time (any compiler).
#
# As a script: cmake "-DIN=a.png;b.png" "-DNAMES=kA;kB" -DOUT=<file.inc> -P EmbedBytes.cmake
# Included: embed_bytes(TARGET OUT_NAME name file [name file]...) makes OUT_NAME in the target's
# build directory, where the target's sources include it.

if (CMAKE_SCRIPT_MODE_FILE AND CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    set(text "")
    foreach (in name IN ZIP_LISTS IN NAMES)
        file(READ "${in}" hex HEX)
        string(LENGTH "${hex}" digits)
        math(EXPR bytes "${digits} / 2")
        string(REGEX REPLACE "(................................)" "\\1\n" hex "${hex}")
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
        string(APPEND text "constexpr unsigned char ${name}[${bytes}] = {\n${hex}\n};\n")
    endforeach()
    file(WRITE "${OUT}" "${text}")
    return()
endif()

set(EMBED_BYTES_SCRIPT "${CMAKE_CURRENT_LIST_FILE}")
function(embed_bytes target out_name)
    set(names "")
    set(files "")
    set(rest ${ARGN})
    while (rest)
        list(POP_FRONT rest name file)
        list(APPEND names "${name}")
        list(APPEND files "${file}")
    endwhile()
    set(out "${CMAKE_CURRENT_BINARY_DIR}/embedded/${out_name}")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND ${CMAKE_COMMAND} "-DIN=${files}" "-DNAMES=${names}" "-DOUT=${out}" -P "${EMBED_BYTES_SCRIPT}"
        DEPENDS ${files} "${EMBED_BYTES_SCRIPT}"
        VERBATIM)
    target_sources(${target} PRIVATE "${out}")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/embedded")
endfunction()
