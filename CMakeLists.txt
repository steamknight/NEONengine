cmake_minimum_required(VERSION 3.14.0)

# Must precede project(): the toolchain file is processed during project() and
# bakes -m${M68K_CPU} into CMAKE_<LANG>_FLAGS_INIT. Setting this afterwards
# updates the cache but leaves the already-initialised flags on 68000.
set(M68K_CPU "68020" CACHE STRING "Target CPU model" FORCE)

project(NEONengine VERSION 1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

# Lowercase project name for binaries and packaging
string(TOLOWER ${PROJECT_NAME} PROJECT_NAME_LOWER)

if(NOT AMIGA)
    message(SEND_ERROR "This project only compiles for Amiga")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAMIGA -Wall -Wextra -fomit-frame-pointer")
set(ACE_TEST_RUNNER OFF CACHE BOOL "Enable the test runner")
set(ACE_USE_AGA_FEATURES ON)
set(ACE_USE_ECS_FEATURES ON)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.c?? src/core/*.c?? src/states/*.c?? src/utils/*.c?? src/mtl/*.c?? src/ace++/*.c??)
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS src/*.h?? src/core/*.h?? src/states/*.h?? src/utils/*.h?? src/mtl/*.h?? src/ace++/*.h??)

include_directories(${PROJECT_SOURCE_DIR}/src)

# Debugging flag exposed via CMake specifically for your game code
if(GAME_DEBUG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DGAME_DEBUG")
endif()

if(ACE_TEST_RUNNER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DACE_TEST_RUNNER")
endif()

# ACE
set(ACE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/modules/ace" CACHE PATH "Path to the ACE source tree (defaults to the modules/ace submodule)")
add_subdirectory(${ACE_SOURCE_DIR} ace)
include_directories(${ACE_SOURCE_DIR}/include)

# Force lower-case binary name for Linux etc.
set(TARGET_NAME ${PROJECT_NAME_LOWER})

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

# CRITICAL: Disable aggressive optimizations for 68020+ - known to cause crashes
# -fno-optimize-sibling-calls: Prevents tail-call optimization that generates invalid branches
target_compile_options(${GAME_LINKED} PRIVATE -fno-optimize-sibling-calls)
target_compile_options(ace PRIVATE -fno-optimize-sibling-calls)

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