cmake_minimum_required(VERSION 3.20)

include(CheckLibraryExists)
find_package(Threads REQUIRED)

if (COMMAND alt_cores_compiler) # AzaharVR: Windows
    alt_cores_compiler()
endif()

set(MELONDS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../melonDS/src")

# Detect target architecture for melonDS ARMJIT (supports both Azahar's ARCHITECTURE
# variable and standalone/toolchain builds via CMAKE_SYSTEM_PROCESSOR).
set(MELONDS_ARCH_X64 OFF)
set(MELONDS_ARCH_ARM64 OFF)
if ((DEFINED ARCHITECTURE AND "x86_64" IN_LIST ARCHITECTURE)
    OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(MELONDS_ARCH_X64 ON)
elseif ((DEFINED ARCHITECTURE AND ("arm64" IN_LIST ARCHITECTURE OR "ARM64" IN_LIST ARCHITECTURE))
    OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(MELONDS_ARCH_ARM64 ON)
endif()

set(MELONDS_ENABLE_JIT OFF)
if (MELONDS_ARCH_X64 OR MELONDS_ARCH_ARM64)
    set(MELONDS_ENABLE_JIT ON)
endif()

# Generate version.h expected by melonDS core headers/sources.
set(melonDS_HOMEPAGE_URL "https://melonds.kuribo64.net")
set(melonDS_VERSION "1.1")
set(MELONDS_VERSION_SUFFIX "")
configure_file(
    "${MELONDS_SRC_DIR}/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.h"
)

# melonDS ships a DSi-customized fork of Teakra (inherits melonDS::DSPInterface,
# adds Savestate/SharedMemoryCallback/SampleClock). Build it in a dedicated
# namespace (melonDS_Teakra) so it never collides with Azahar's 3DS externals/teakra.
set(MELONDS_TEAKRA_DIR "${MELONDS_SRC_DIR}/teakra")
add_library(melonds_teakra STATIC
    "${MELONDS_TEAKRA_DIR}/src/ahbm.cpp"
    "${MELONDS_TEAKRA_DIR}/src/apbp.cpp"
    "${MELONDS_TEAKRA_DIR}/src/btdmp.cpp"
    "${MELONDS_TEAKRA_DIR}/src/disassembler.cpp"
    "${MELONDS_TEAKRA_DIR}/src/dma.cpp"
    "${MELONDS_TEAKRA_DIR}/src/memory_interface.cpp"
    "${MELONDS_TEAKRA_DIR}/src/mmio.cpp"
    "${MELONDS_TEAKRA_DIR}/src/parser.cpp"
    "${MELONDS_TEAKRA_DIR}/src/processor.cpp"
    "${MELONDS_TEAKRA_DIR}/src/teakra.cpp"
    "${MELONDS_TEAKRA_DIR}/src/timer.cpp"
)
target_include_directories(melonds_teakra
    PRIVATE
        "${MELONDS_TEAKRA_DIR}/include"
        "${MELONDS_TEAKRA_DIR}/src"
        "${MELONDS_SRC_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}"
)
target_compile_definitions(melonds_teakra PRIVATE
    Teakra=melonDS_Teakra
    NodeAsExpansion=melonDS_NodeAsExpansion
)
target_link_libraries(melonds_teakra PRIVATE Threads::Threads)
set_target_properties(melonds_teakra PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION FALSE
)
if (MSVC)
    target_compile_options(melonds_teakra PRIVATE /W0)
else()
    target_compile_options(melonds_teakra PRIVATE -w "$<$<CONFIG:DEBUG>:-Og>")
endif()

# Core-only melonDS static library (no frontend, no OpenGL renderer, no GDB stub, no LTO).
set(MELONDS_CORE_SOURCES
    "${MELONDS_SRC_DIR}/ARCodeFile.cpp"
    "${MELONDS_SRC_DIR}/ARDatabaseDAT.cpp"
    "${MELONDS_SRC_DIR}/AREngine.cpp"
    "${MELONDS_SRC_DIR}/ARM.cpp"
    "${MELONDS_SRC_DIR}/ARMInterpreter.cpp"
    "${MELONDS_SRC_DIR}/ARMInterpreter_ALU.cpp"
    "${MELONDS_SRC_DIR}/ARMInterpreter_Branch.cpp"
    "${MELONDS_SRC_DIR}/ARMInterpreter_LoadStore.cpp"
    "${MELONDS_SRC_DIR}/CP15.cpp"
    "${MELONDS_SRC_DIR}/CRC32.cpp"
    "${MELONDS_SRC_DIR}/DMA.cpp"
    "${MELONDS_SRC_DIR}/DMA_Timings.cpp"
    "${MELONDS_SRC_DIR}/DSi.cpp"
    "${MELONDS_SRC_DIR}/DSi_AES.cpp"
    "${MELONDS_SRC_DIR}/DSi_Camera.cpp"
    "${MELONDS_SRC_DIR}/DSi_DSP.cpp"
    "${MELONDS_SRC_DIR}/DSi_I2C.cpp"
    "${MELONDS_SRC_DIR}/DSi_I2S.cpp"
    "${MELONDS_SRC_DIR}/DSi_NAND.cpp"
    "${MELONDS_SRC_DIR}/DSi_NDMA.cpp"
    "${MELONDS_SRC_DIR}/DSi_NWifi.cpp"
    "${MELONDS_SRC_DIR}/DSi_SD.cpp"
    "${MELONDS_SRC_DIR}/DSi_SPI_TSC.cpp"
    "${MELONDS_SRC_DIR}/FATIO.cpp"
    "${MELONDS_SRC_DIR}/FATStorage.cpp"
    "${MELONDS_SRC_DIR}/GBACart.cpp"
    "${MELONDS_SRC_DIR}/GBACartMotionPak.cpp"
    "${MELONDS_SRC_DIR}/GPU.cpp"
    "${MELONDS_SRC_DIR}/GPU_Soft.cpp"
    "${MELONDS_SRC_DIR}/GPU2D.cpp"
    "${MELONDS_SRC_DIR}/GPU2D_Soft.cpp"
    "${MELONDS_SRC_DIR}/GPU3D.cpp"
    "${MELONDS_SRC_DIR}/GPU3D_Soft.cpp"
    "${MELONDS_SRC_DIR}/GPU3D_Texcache.cpp"
    "${MELONDS_SRC_DIR}/Mic.cpp"
    "${MELONDS_SRC_DIR}/NDS.cpp"
    "${MELONDS_SRC_DIR}/NDSCart.cpp"
    "${MELONDS_SRC_DIR}/ROMList.cpp"
    "${MELONDS_SRC_DIR}/FreeBIOS.cpp"
    "${MELONDS_SRC_DIR}/RTC.cpp"
    "${MELONDS_SRC_DIR}/Savestate.cpp"
    "${MELONDS_SRC_DIR}/SPI.cpp"
    "${MELONDS_SRC_DIR}/SPI_Firmware.cpp"
    "${MELONDS_SRC_DIR}/SPU.cpp"
    "${MELONDS_SRC_DIR}/Utils.cpp"
    "${MELONDS_SRC_DIR}/Wifi.cpp"
    "${MELONDS_SRC_DIR}/WifiAP.cpp"

    "${MELONDS_SRC_DIR}/DSP_HLE/UcodeBase.cpp"
    "${MELONDS_SRC_DIR}/DSP_HLE/AACUcode.cpp"
    "${MELONDS_SRC_DIR}/DSP_HLE/G711Ucode.cpp"
    "${MELONDS_SRC_DIR}/DSP_HLE/GraphicsUcode.cpp"

    "${MELONDS_SRC_DIR}/NDSCart/CartCommon.cpp"
    "${MELONDS_SRC_DIR}/NDSCart/CartRetail.cpp"
    "${MELONDS_SRC_DIR}/NDSCart/CartRetailNAND.cpp"
    "${MELONDS_SRC_DIR}/NDSCart/CartRetailIR.cpp"
    "${MELONDS_SRC_DIR}/NDSCart/CartRetailBT.cpp"
    "${MELONDS_SRC_DIR}/NDSCart/CartSD.cpp"
    "${MELONDS_SRC_DIR}/NDSCart/CartHomebrew.cpp"
    "${MELONDS_SRC_DIR}/NDSCart/CartR4.cpp"

    "${MELONDS_SRC_DIR}/fatfs/ff.c"
    "${MELONDS_SRC_DIR}/fatfs/ffsystem.c"
    "${MELONDS_SRC_DIR}/fatfs/ffunicode.c"

    "${MELONDS_SRC_DIR}/sha1/sha1.c"
    "${MELONDS_SRC_DIR}/tiny-AES-c/aes.c"
    "${MELONDS_SRC_DIR}/blip-buf/blip_buf.c"
)

add_library(melonds_core STATIC ${MELONDS_CORE_SOURCES})

# Match melonds_teakra's isolated namespace in DSi_DSP.cpp without modifying upstream sources.
set_source_files_properties("${MELONDS_SRC_DIR}/DSi_DSP.cpp"
    PROPERTIES COMPILE_DEFINITIONS "Teakra=melonDS_Teakra"
)

if (MELONDS_ENABLE_JIT)
    if (NOT WIN32) # AzaharVR: clang-cl assembles it, see below
    enable_language(ASM)
    endif()

    target_sources(melonds_core PRIVATE
        "${MELONDS_SRC_DIR}/ARM_InstrInfo.cpp"
        "${MELONDS_SRC_DIR}/ARMJIT.cpp"
        "${MELONDS_SRC_DIR}/ARMJIT_Memory.cpp"
        "${MELONDS_SRC_DIR}/ARMJIT_Global.cpp"
        "${MELONDS_SRC_DIR}/dolphin/CommonFuncs.cpp"
    )

    if (MELONDS_ARCH_X64)
        target_sources(melonds_core PRIVATE
            "${MELONDS_SRC_DIR}/dolphin/x64ABI.cpp"
            "${MELONDS_SRC_DIR}/dolphin/x64CPUDetect.cpp"
            "${MELONDS_SRC_DIR}/dolphin/x64Emitter.cpp"
            "${MELONDS_SRC_DIR}/ARMJIT_x64/ARMJIT_Compiler.cpp"
            "${MELONDS_SRC_DIR}/ARMJIT_x64/ARMJIT_ALU.cpp"
            "${MELONDS_SRC_DIR}/ARMJIT_x64/ARMJIT_LoadStore.cpp"
            "${MELONDS_SRC_DIR}/ARMJIT_x64/ARMJIT_Branch.cpp"
        )
        if (NOT WIN32)
            target_sources(melonds_core PRIVATE "${MELONDS_SRC_DIR}/ARMJIT_x64/ARMJIT_Linkage.S")
        else()
            # AzaharVR: GNU assembler with the C preprocessor. CMake's ASM is cl here, which is neither.
            set(MELONDS_LINKAGE_OBJ "${CMAKE_CURRENT_BINARY_DIR}/ARMJIT_Linkage.obj")
            add_custom_command(
                OUTPUT "${MELONDS_LINKAGE_OBJ}"
                DEPENDS "${MELONDS_SRC_DIR}/ARMJIT_x64/ARMJIT_Linkage.S"
                COMMAND "${CMAKE_C_COMPILER}" --target=x86_64-pc-windows-msvc /nologo /c
                        "-I${MELONDS_SRC_DIR}" "/Fo${MELONDS_LINKAGE_OBJ}"
                        "${MELONDS_SRC_DIR}/ARMJIT_x64/ARMJIT_Linkage.S"
                COMMENT "Assembling ARMJIT_Linkage.S"
                VERBATIM
            )
            set_source_files_properties("${MELONDS_LINKAGE_OBJ}" PROPERTIES
                EXTERNAL_OBJECT TRUE GENERATED TRUE)
            target_sources(melonds_core PRIVATE "${MELONDS_LINKAGE_OBJ}")
        endif()
    elseif (MELONDS_ARCH_ARM64)
        target_sources(melonds_core PRIVATE
            "${MELONDS_SRC_DIR}/dolphin/Arm64Emitter.cpp"
            "${MELONDS_SRC_DIR}/dolphin/MathUtil.cpp"
            "${MELONDS_SRC_DIR}/ARMJIT_A64/ARMJIT_Compiler.cpp"
            "${MELONDS_SRC_DIR}/ARMJIT_A64/ARMJIT_ALU.cpp"
            "${MELONDS_SRC_DIR}/ARMJIT_A64/ARMJIT_LoadStore.cpp"
            "${MELONDS_SRC_DIR}/ARMJIT_A64/ARMJIT_Branch.cpp"
            "${MELONDS_SRC_DIR}/ARMJIT_A64/ARMJIT_Linkage.S"
        )
    endif()

    target_compile_definitions(melonds_core PUBLIC JIT_ENABLED)
    target_compile_definitions(melonds_teakra PRIVATE JIT_ENABLED)

    if (WIN32)
        target_compile_definitions(melonds_core PRIVATE _WIN32_WINNT=_WIN32_WINNT_WIN8)
        # AzaharVR: no onecore.lib: the two functions it was for (VirtualAlloc2, MapViewOfFile3) are
        # looked up at run time, and it would take kernel32's place for the whole program.
    endif()
endif()

# Deduplicate xxHash: reuse Azahar's xxhash target when available (same 0.8.3 ABI);
# otherwise inline xxhash privately so no duplicate XXH* symbols can ever be exported.
if (TARGET xxhash)
    target_link_libraries(melonds_core PRIVATE xxhash)
else()
    target_compile_definitions(melonds_core PRIVATE XXH_INLINE_ALL)
endif()

target_compile_definitions(melonds_core PRIVATE $<$<CONFIG:DEBUG>:XXH_NO_INLINE_HINTS=1>)

target_include_directories(melonds_core
    PUBLIC
        "${MELONDS_SRC_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(melonds_core PRIVATE melonds_teakra Threads::Threads)

if (MSVC)
    # AzaharVR: sha1.c knows the byte order of Linux, the BSDs and MinGW
    set_source_files_properties("${MELONDS_SRC_DIR}/sha1/sha1.c" PROPERTIES COMPILE_DEFINITIONS
        "BYTE_ORDER=1234;LITTLE_ENDIAN=1234;BIG_ENDIAN=4321;PDP_ENDIAN=3412")
    # AzaharVR: an empty dirent.h: FATStorage.cpp includes it and goes by fatfs's own directories.
    target_include_directories(melonds_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/win32")
endif()

set_target_properties(melonds_core PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION FALSE
)

if (MSVC)
    target_compile_definitions(melonds_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
    target_compile_options(melonds_core PRIVATE /W0)
else()
    target_compile_options(melonds_core PRIVATE
        -w
        -fwrapv
        "$<$<COMPILE_LANGUAGE:CXX>:-Wno-invalid-offsetof>"
    )
endif()

find_library(MELONDS_MATH_LIBRARY m)
if (MELONDS_MATH_LIBRARY)
    target_link_libraries(melonds_core PRIVATE ${MELONDS_MATH_LIBRARY})
endif()

if (WIN32)
    target_link_libraries(melonds_core PRIVATE ole32 comctl32 wsock32 ws2_32)
elseif (NOT APPLE AND NOT HAIKU)
    check_library_exists(rt shm_open "" MELONDS_NEED_LIBRT)
    if (MELONDS_NEED_LIBRT)
        target_link_libraries(melonds_core PRIVATE rt)
    endif()
endif()

if (COMMAND target_disable_warnings)
    target_disable_warnings(melonds_core)
endif()

# melonDS's hi-res renderer (GPU_OpenGL, GPU2D_OpenGL, GPU3D_OpenGL and their shaders), as upstream
# has it. It is not given OpenGL: MELONDS_GL_HEADER names the declarations in
# src/frame_cores/ds_gl, which frame_cores carries out on Vulkan. GPU3D_Compute.cpp is compiled
# only because GPU_OpenGL.cpp names its class; it is never made.
set(MELONDS_HIRES_SHADERS
    2DLayerPreVS 2DLayerPreFS 2DSpritePreVS 2DSpritePreFS 2DSpriteVS 2DSpriteFS
    2DCompositorVS 2DCompositorFS FinalPassVS FinalPassFS CaptureVS CaptureFS
    CaptureDownscaleVS CaptureDownscaleFS 3DClearVS 3DClearFS 3DClearBitmapVS 3DClearBitmapFS
    3DRenderVS 3DRenderFS 3DFinalPassVS 3DFinalPassEdgeFS 3DFinalPassFogFS
)
set(MELONDS_HIRES_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/hires/OpenGL_shaders")
file(MAKE_DIRECTORY "${MELONDS_HIRES_SHADER_DIR}")
set(MELONDS_HIRES_SHADER_OUT "")
foreach(SH_NAME ${MELONDS_HIRES_SHADERS})
    set(SH_INPUT "${MELONDS_SRC_DIR}/OpenGL_shaders/${SH_NAME}.glsl")
    set(SH_OUTPUT "${MELONDS_HIRES_SHADER_DIR}/${SH_NAME}.h")
    add_custom_command(
        OUTPUT ${SH_OUTPUT}
        DEPENDS ${SH_INPUT}
        COMMAND ${CMAKE_COMMAND} -D INPUT_FILE=${SH_INPUT} -D OUTPUT_FILE=${SH_OUTPUT}
                -D VAR_NAME=${SH_NAME} -P "${MELONDS_SRC_DIR}/../cmake/MakeEmbed.cmake"
        VERBATIM
    )
    list(APPEND MELONDS_HIRES_SHADER_OUT ${SH_OUTPUT})
endforeach()

add_library(melonds_hires STATIC
    "${MELONDS_SRC_DIR}/GPU_OpenGL.cpp"
    "${MELONDS_SRC_DIR}/GPU2D_OpenGL.cpp"
    "${MELONDS_SRC_DIR}/GPU3D_OpenGL.cpp"
    "${MELONDS_SRC_DIR}/GPU3D_Compute.cpp"
    "${MELONDS_SRC_DIR}/GPU3D_TexcacheOpenGL.cpp"
    "${MELONDS_SRC_DIR}/OpenGLSupport.cpp"
    ${MELONDS_HIRES_SHADER_OUT}
)
target_compile_definitions(melonds_hires PUBLIC
    OGLRENDERER_ENABLED
    "MELONDS_GL_HEADER=\"ds_gl.h\""
)
target_include_directories(melonds_hires
    PUBLIC "${CMAKE_SOURCE_DIR}/src/frame_cores/ds_gl"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/hires"
)
target_link_libraries(melonds_hires PUBLIC melonds_core)
if (TARGET xxhash)
    target_link_libraries(melonds_hires PRIVATE xxhash)
else()
    target_compile_definitions(melonds_hires PRIVATE XXH_INLINE_ALL)
endif()
set_target_properties(melonds_hires PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION FALSE
)
if (MSVC)
    target_compile_definitions(melonds_hires PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_compile_options(melonds_hires PRIVATE /W0)
else()
    target_compile_options(melonds_hires PRIVATE -w -fwrapv)
endif()
if (COMMAND target_disable_warnings)
    target_disable_warnings(melonds_hires)
endif()
