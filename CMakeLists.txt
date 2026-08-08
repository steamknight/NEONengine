cmake_minimum_required(VERSION 3.14.0)

# Must precede project(): the toolchain file is processed during project() and
# bakes -m${M68K_CPU} into CMAKE_<LANG>_FLAGS_INIT. Setting this afterwards
# updates the cache but leaves the already-initialised flags on 68000.
set(M68K_CPU "68020" CACHE STRING "Target CPU model" FORCE)

# Pin binutils to the same toolchain as the compiler.
if(TOOLCHAIN_PATH AND TOOLCHAIN_PREFIX)
    file(TO_CMAKE_PATH "${TOOLCHAIN_PATH}" _NE_TOOLCHAIN_PATH)
    set(_NE_TOOL_PREFIX "${_NE_TOOLCHAIN_PATH}/bin/${TOOLCHAIN_PREFIX}-")

    if(CMAKE_HOST_WIN32)
        set(_NE_TOOL_SUFFIX ".exe")
    else()
        set(_NE_TOOL_SUFFIX "")
    endif()

    # Only if this tree actually has the prefixed binutils next to the
    # compiler, which is where Bartman's toolchain puts them.
    if(EXISTS "${_NE_TOOL_PREFIX}ar${_NE_TOOL_SUFFIX}")
        foreach(_ne_tool AR RANLIB NM OBJCOPY OBJDUMP STRIP READELF ADDR2LINE LINKER)
            string(TOLOWER "${_ne_tool}" _ne_tool_lower)

            if(_ne_tool STREQUAL "LINKER")
                set(_ne_tool_lower "ld")
            endif()

            set(CMAKE_${_ne_tool} "${_NE_TOOL_PREFIX}${_ne_tool_lower}${_NE_TOOL_SUFFIX}"
                CACHE FILEPATH "${_ne_tool_lower} from TOOLCHAIN_PATH" FORCE)
        endforeach()

        unset(_ne_tool)
        unset(_ne_tool_lower)
    endif()

    unset(_NE_TOOLCHAIN_PATH)
    unset(_NE_TOOL_PREFIX)
    unset(_NE_TOOL_SUFFIX)
endif()

project(NEONengine VERSION 1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

# Lowercase project name for binaries and packaging
string(TOLOWER ${PROJECT_NAME} PROJECT_NAME_LOWER)

if(NOT AMIGA)
    message(SEND_ERROR "This project only compiles for Amiga")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAMIGA -Wall -Wextra -fomit-frame-pointer")

# Silence -Wattributes.
#
# The toolchain builds with -flto -fwhole-program (m68k-bartman.cmake). At -O1
# and above, LTO works out which symbols are not externally visible and promotes
# them to hidden visibility. m68k-amiga-elf has no assemble_visibility in its
# backend, so it warns and ignores each one: 452 lines of noise on the current
# tree, drowning out every real warning.
#
# It is unactionable. Nothing in this tree, ACE, or the toolchain headers asks
# for visibility, and an Amiga hunk executable has no dynamic linker that could
# honour it. Debug builds never show it because -O0 skips the LTO pass that
# assigns visibility at all, which is why it only appeared when we started
# building RelWithDebInfo. Any optimised build has it, Release included.
#
# Suppressing the whole category costs nothing today: this is the only
# -Wattributes warning the build produces. Revisit if that stops being true.
#
# Both languages, because ACE is C and the engine is C++; the flag has to reach
# the link line too, since lto1 emits these during the LTO link.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-attributes")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-attributes")

# No FPU.
#
# The toolchain compiles with -m68020 (m68k-bartman.cmake), which for m68k GCC
# turns on hardware floating point. Nothing here does floating-point maths, but
# the interrupt attribute makes GCC save every call-clobbered register in an
# ISR prologue, and with an FPU in the machine description that includes fp0
# and fp1. Each of ACE's four handlers therefore opens with an fmovem.
#
# A stock A1200 is a 68EC020 and has no FPU, so those are F-line instructions:
# on real hardware the first vertical blank takes exception 11 and gurus
# 8000000B. The flag has to reach the link line as well, since -flto -fwhole-
# program means codegen happens there.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msoft-float")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msoft-float")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -msoft-float")

set(ACE_TEST_RUNNER OFF CACHE BOOL "Enable the test runner")

# ACE declares these with set(... CACHE BOOL ...) in cmake/ace_config.cmake,
# which ignores a normal variable, so a plain set() here silently did nothing.
# FORCE because the engine calls AGA-only ACE functions; it is not a choice.
set(ACE_USE_AGA_FEATURES ON CACHE BOOL "AGA feature set, required by NEONengine" FORCE)
set(ACE_USE_ECS_FEATURES ON CACHE BOOL "ECS feature set, required by NEONengine" FORCE)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.c?? src/core/*.c?? src/states/*.c?? src/utils/*.c?? src/mtl/*.c?? src/ace++/*.c??)
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS src/*.h?? src/core/*.h?? src/states/*.h?? src/utils/*.h?? src/mtl/*.h?? src/ace++/*.h??)

include_directories(${PROJECT_SOURCE_DIR}/src)

# These three reach the sources through add_compile_definitions, not
# CMAKE_C_FLAGS: every engine source is C++, so a C-only flag never arrives.

# The VM's diagnostics: its debug stats, the lines it writes when the frame
# budget runs out, and the bodies of the DBG_ opcodes, which are NOPs without
# it. It costs a 1KB opcode histogram and a count on every instruction, so a
# release build turns it off.
set(GAME_DEBUG ON CACHE BOOL "Enable NoirVM debug diagnostics")

if(GAME_DEBUG)
    add_compile_definitions(GAME_DEBUG)
endif()

if(ACE_TEST_RUNNER)
    add_compile_definitions(ACE_TEST_RUNNER)
endif()

# Engine logging, independent of ACE_DEBUG on purpose. ACE gates its own
# logWrite on ACE_DEBUG, which also turns on bobCheckGood and the bob buffer
# auto-correction scene_bobs relies on, so the two switches have to be separate:
# this one keeps engine lines in an optimised build while ACE says nothing.
# Off, every NE_LOG expands to nothing and its format string leaves the binary.
set(NE_LOGGING ON CACHE BOOL "Enable NEONengine domain logging")

if(NE_LOGGING)
    add_compile_definitions(NE_LOGGING)
endif()

# ACE
set(ACE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/modules/ace" CACHE PATH "Path to the ACE source tree (defaults to the modules/ace submodule)")
add_subdirectory(${ACE_SOURCE_DIR} ace)
include_directories(${ACE_SOURCE_DIR}/include)

# Force lower-case binary name for Linux etc.
set(TARGET_NAME ${PROJECT_NAME_LOWER})

# elf2hunk before 057526f emits a load-time relocation for every PC-relative
# branch. That breaks the 32-bit BRA.L which -foptimize-sibling-calls emits on
# 68020: AmigaDOS adds the segment base to what is a displacement, so the
# branch lands outside the program. The tool bundled with the amiga-debug
# extension still does this, so tail calls stay off unless this points at a
# build of github.com/BartmanAbyss/elf2hunk at 057526f or later.
set(NE_ELF2HUNK "" CACHE FILEPATH "elf2hunk that leaves PC-relative relocations alone")

if(NE_ELF2HUNK)
    set(ELF2HUNK "${NE_ELF2HUNK}")
endif()

if(ELF2HUNK)
    # Add elf2hunk step for Bartman compiler
    set(GAME_LINKED ${TARGET_NAME}.elf) # Intermediate executable
    set(GAME_EXE ${TARGET_NAME}.exe) # Use this to launch the game
    add_executable(${GAME_LINKED} ${SOURCES} ${HEADERS})
    add_custom_command(
        TARGET ${GAME_LINKED} POST_BUILD
        COMMAND ${ELF2HUNK} ${GAME_LINKED} ${GAME_EXE}
    )
else()
    # Just produce the executable with Bebbo compiler
    SET(GAME_LINKED ${TARGET_NAME})
    SET(GAME_EXE ${TARGET_NAME})
    add_executable(${GAME_LINKED} ${SOURCES} ${HEADERS})
endif()

if(NOT NE_ELF2HUNK)
    message(WARNING
        "Using the toolchain's elf2hunk, so tail calls are disabled. Set "
        "NE_ELF2HUNK to a build of github.com/BartmanAbyss/elf2hunk at "
        "057526f or later to enable them.")
    target_compile_options(${GAME_LINKED} PRIVATE -fno-optimize-sibling-calls)
    target_compile_options(ace PRIVATE -fno-optimize-sibling-calls)
endif()

# logWrite() takes char*, so every string literal passed to it trips
# -Wwrite-strings. Kept out of the toolchain flags so a fresh configure gets it.
target_compile_options(${GAME_LINKED} PRIVATE -Wall -Wno-write-strings)
add_custom_command(
    TARGET ${GAME_LINKED}
    PRE_BUILD
    COMMAND ${CMAKE_COMMAND}
    -DLANGUAGE:STRING="C"
    -DHEADER_DIR:PATH="${CMAKE_SOURCE_DIR}/src"
    -DCACHE_DIR:PATH="${CMAKE_SOURCE_DIR}"
    -DCACHE_FILENAME:="BUILD_NUM"
    -DHEADER_GUARD:STRING="__BUILD_NUMBER_H__INCLUDED__"
    -DVERSION_MAJOR="${PROJECT_VERSION_MAJOR}"
    -DVERSION_MINOR="${PROJECT_VERSION_MINOR}"
    -P "${CMAKE_SOURCE_DIR}/build_number.cmake"
)

target_link_libraries(${GAME_LINKED} ace)

# Convenience variables for source/destination paths
set(RES_DIR ${CMAKE_CURRENT_LIST_DIR}/assets)
set(DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)

# Create a directory for converted files next to the built executable
file(MAKE_DIRECTORY ${DATA_DIR})
file(MAKE_DIRECTORY ${DATA_DIR} ${DATA_DIR}/core)
file(MAKE_DIRECTORY ${DATA_DIR} ${DATA_DIR}/music)
file(MAKE_DIRECTORY ${DATA_DIR} ${DATA_DIR}/lang)

# Convert background image
convertBitmaps(
    TARGET ${GAME_LINKED} PALETTE ${RES_DIR}/mpg.act
    SOURCES ${RES_DIR}/mpg.png
    DESTINATIONS ${DATA_DIR}/mpg.bm
)

convertBitmaps(
    TARGET ${GAME_LINKED} PALETTE ${RES_DIR}/core/base.act
    SOURCES ${RES_DIR}/core/frame_9.png
    DESTINATIONS ${DATA_DIR}/core/frame_9.bm
    INTERLEAVED
)

# Copy runtime data files next to the executable at BUILD time, not configure
# time. configure_file() only runs during CMake configure, so re-exporting an
# asset (e.g. game.pak) and running a plain `cmake --build` left a stale copy in
# DATA_DIR while the engine kept loading the old one. A custom command with
# copy_if_different, driven by a DEPENDS on the source, re-copies on every build
# whenever the source has changed.
set(DATA_FILES
    music/theme.mod
    gutter.neon
    game.pak
)

# Language tables are discovered rather than listed: the exporter emits one
# .noir per authored language, so naming them here would mean editing this file
# every time a language is added or dropped. Adding one needs a reconfigure,
# same as adding a source file.
file(GLOB LANG_FILES RELATIVE ${RES_DIR} ${RES_DIR}/lang/*.noir)
list(APPEND DATA_FILES ${LANG_FILES})

set(COPIED_DATA_FILES "")

foreach(rel IN LISTS DATA_FILES)
    add_custom_command(
        OUTPUT ${DATA_DIR}/${rel}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RES_DIR}/${rel} ${DATA_DIR}/${rel}
        DEPENDS ${RES_DIR}/${rel}
        COMMENT "Copying ${rel} to data dir"
        VERBATIM
    )
    list(APPEND COPIED_DATA_FILES ${DATA_DIR}/${rel})
endforeach()

add_custom_target(game_data ALL DEPENDS ${COPIED_DATA_FILES})