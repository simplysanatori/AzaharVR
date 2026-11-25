# AzaharVR: the DS and GBA cores in the Windows build. Nothing here is used anywhere else.
#
# melonDS, gba-ds, PokeDaisy and the code that includes their headers are GNU C and C++
# (__attribute__, statement expressions, typeof), which cl.exe does not compile. With MSVC these
# directories, and only these, are compiled by clang-cl: the same ABI, runtime and STL, so the
# objects link with cl's. Everything else stays cl's.
#
#   alt_cores_option()    in the top directory: ENABLE_ALT_CORES, and looks for clang-cl
#   alt_cores_compiler()  in a directory: its targets are compiled by clang-cl
#   alt_cores_restore()   in a directory: back to the build's own compiler
#
# A directory's compiler is read when CMake generates, so the whole directory is clang-cl's, whatever
# the order of its lines; it holds with the Ninja and Makefile generators. project() in a directory
# whose compiler differs from the cache makes CMake throw the cache away: call this after it.

include(CMakeDependentOption)

# The MSVC STL's own minimum: "expected Clang 19.0.0 or newer" (14.4x). A newer STL can want a newer one,
# and says so with STL1000.
set(ALT_CORES_CLANG_MINIMUM 19.0)

# Sets out to an empty string, or to why the cores cannot be built; ALT_CORES_CLANG_CL to clang-cl
# (empty: the build's compiler is Clang itself).
function(alt_cores_find_compiler out)
    set(${out} "" PARENT_SCOPE)
    set(ALT_CORES_CLANG_CL "" PARENT_SCOPE)
    if (NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        set(${out} "only Windows x86-64 has been built" PARENT_SCOPE)
        return()
    endif()
    if (NOT MSVC)
        set(${out} "the compiler has to be MSVC or clang-cl" PARENT_SCOPE)
        return()
    endif()
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        return()
    endif()
    if (NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        set(${out} "the generator has to be Ninja (it is ${CMAKE_GENERATOR})" PARENT_SCOPE)
        return()
    endif()
    find_program(CLANG_CL_EXECUTABLE NAMES clang-cl
        HINTS
            "$ENV{LLVM_PATH}/bin"
            "$ENV{ProgramFiles}/LLVM/bin"
            "$ENV{VCINSTALLDIR}/Tools/Llvm/x64/bin"
            "$ENV{VCINSTALLDIR}/Tools/Llvm/bin"
            "C:/Program Files/LLVM/bin"
        DOC "clang-cl, for the DS and GBA cores")
    if (NOT CLANG_CL_EXECUTABLE)
        set(${out} "clang-cl was not found (winget install LLVM.LLVM, or set CLANG_CL_EXECUTABLE)" PARENT_SCOPE)
        return()
    endif()
    execute_process(COMMAND "${CLANG_CL_EXECUTABLE}" --version
        OUTPUT_VARIABLE version_text ERROR_QUIET RESULT_VARIABLE rc)
    if (NOT rc EQUAL 0 OR NOT version_text MATCHES "clang version ([0-9]+\\.[0-9]+(\\.[0-9]+)?)")
        set(${out} "${CLANG_CL_EXECUTABLE} does not run" PARENT_SCOPE)
        return()
    endif()
    set(version "${CMAKE_MATCH_1}")
    if (version VERSION_LESS ALT_CORES_CLANG_MINIMUM)
        set(${out} "clang-cl ${version} is too old, ${ALT_CORES_CLANG_MINIMUM} is the minimum" PARENT_SCOPE)
        return()
    endif()
    message(STATUS "DS and GBA cores: compiled by clang-cl ${version} (${CLANG_CL_EXECUTABLE})")
    set(ALT_CORES_CLANG_CL "${CLANG_CL_EXECUTABLE}" PARENT_SCOPE)
endfunction()

macro(alt_cores_option)
    # Asked for on the command line: then a missing clang-cl is an error, also when CMake runs again.
    get_property(_alt_cores_help CACHE ENABLE_ALT_CORES PROPERTY HELPSTRING)
    if (_alt_cores_help MATCHES "specified on the command line" OR
        (DEFINED CACHE{ENABLE_ALT_CORES} AND NOT DEFINED CACHE{ALT_CORES_ASKED}))
        set(ALT_CORES_ASKED ON CACHE INTERNAL "ENABLE_ALT_CORES was set by hand")
    elseif (NOT DEFINED CACHE{ALT_CORES_ASKED})
        set(ALT_CORES_ASKED OFF CACHE INTERNAL "ENABLE_ALT_CORES was set by hand")
    endif()
    CMAKE_DEPENDENT_OPTION(ENABLE_ALT_CORES "Build the DS (melonDS) and GBA/GB (gba-ds, mGBA) cores" ON "ENABLE_FRAME_XR" OFF)
    set(ALT_CORES_CLANG_CL "")
    if (ENABLE_ALT_CORES)
        alt_cores_find_compiler(_alt_cores_why_not)
        if (_alt_cores_why_not)
            if (ALT_CORES_ASKED)
                message(FATAL_ERROR "ENABLE_ALT_CORES: ${_alt_cores_why_not}")
            endif()
            message(WARNING "No DS and GBA cores in this build: ${_alt_cores_why_not}")
            # Not into the cache: they are back once clang-cl is there.
            set(ENABLE_ALT_CORES OFF)
        endif()
    endif()
endmacro()

macro(alt_cores_compiler)
    if (ALT_CORES_CLANG_CL)
        if (NOT DEFINED _alt_cores_c)
            set(_alt_cores_c "${CMAKE_C_COMPILER}")
            set(_alt_cores_cxx "${CMAKE_CXX_COMPILER}")
        endif()
        set(CMAKE_C_COMPILER "${ALT_CORES_CLANG_CL}")
        set(CMAKE_CXX_COMPILER "${ALT_CORES_CLANG_CL}")
        # CMake gives these the flags it has for cl: what clang-cl does not know of them is not
        # worth a line for each file. Signed overflow wraps, as with cl and as melonDS and mGBA ask for.
        add_compile_options(-Wno-unknown-argument -Wno-unused-command-line-argument -fwrapv)
    endif()
endmacro()

macro(alt_cores_restore)
    if (ALT_CORES_CLANG_CL AND DEFINED _alt_cores_c)
        set(CMAKE_C_COMPILER "${_alt_cores_c}")
        set(CMAKE_CXX_COMPILER "${_alt_cores_cxx}")
    endif()
endmacro()
