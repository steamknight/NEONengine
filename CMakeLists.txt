cmake_minimum_required(VERSION 3.14.0)
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
add_subdirectory(modules/ace ace)
include_directories(modules/ace/include)

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

# Convert palette and background image
convertPalette(${GAME_LINKED} ${RES_DIR}/mpg.act ${DATA_DIR}/mpg.plt)
convertBitmaps(
    TARGET ${GAME_LINKED} PALETTE ${RES_DIR}/mpg.act
    SOURCES ${RES_DIR}/mpg.png
    DESTINATIONS ${DATA_DIR}/mpg.bm
)

# Convert palette and flags
convertPalette(${GAME_LINKED} ${RES_DIR}/core/base.act ${DATA_DIR}/core/base.plt)
convertBitmaps(
    TARGET ${GAME_LINKED} PALETTE ${RES_DIR}/core/base.act
    SOURCES ${RES_DIR}/core/flags.png
    DESTINATIONS ${DATA_DIR}/core/flags.bm
)

convertPalette(${GAME_LINKED} ${RES_DIR}/core/base.act ${DATA_DIR}/core/base.plt)
convertBitmaps(
    TARGET ${GAME_LINKED} PALETTE ${RES_DIR}/core/pointers.act
    SOURCES ${RES_DIR}/core/pointers.png
    DESTINATIONS ${DATA_DIR}/core/pointers.bm
    INTERLEAVED
)

convertBitmaps(
    TARGET ${GAME_LINKED} PALETTE ${RES_DIR}/core/base.act
    SOURCES ${RES_DIR}/core/frame_9.png
    DESTINATIONS ${DATA_DIR}/core/frame_9.bm
    INTERLEAVED
)

configure_file(
    ${RES_DIR}/music/theme.mod ${DATA_DIR}/music/theme.mod COPYONLY
)

configure_file(
    ${RES_DIR}/gutter.neon ${DATA_DIR}/gutter.neon COPYONLY
)

configure_file(
    ${RES_DIR}/lang/en.noir ${DATA_DIR}/lang/en.noir COPYONLY
)

if(ACE_TEST_RUNNER)
    configure_file(
        ${RES_DIR}/lang/test.noir ${DATA_DIR}/lang/test.noir COPYONLY
    )
endif()

# Fonts
convertFont(
    TARGET ${GAME_LINKED} FIRST_CHAR 32
    SOURCE ${RES_DIR}/font_winds7.png DESTINATION ${DATA_DIR}/font.fnt
)