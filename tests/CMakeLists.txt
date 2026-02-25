# Test executables for XBoing
#
# Convention: one test file per source module — tests/test_<module>.c
# All tests link against cmocka and the project include/ directory.

# Helper function: add a CMocka test executable and register it with ctest.
function(xboing_add_test NAME)
    add_executable(${NAME} ${NAME}.c)
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${X11_INCLUDE_DIRS}
        ${XPM_INCLUDE_DIRS}
    )
    target_compile_options(${NAME} PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(${NAME} PRIVATE ${CMOCKA_LIBRARIES})
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

# --- Test targets -----------------------------------------------------------

xboing_add_test(test_skeleton)

# Ball physics characterization tests (bead n9e.1)
# Links ball_math.c directly — no X11, no game binary.
add_executable(test_ball_math test_ball_math.c ${CMAKE_SOURCE_DIR}/ball_math.c)
target_include_directories(test_ball_math PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_options(test_ball_math PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_ball_math PRIVATE ${CMOCKA_LIBRARIES} m)
add_test(NAME test_ball_math COMMAND test_ball_math)

# Scoring characterization tests (bead n9e.2)
# Links score_logic.c directly — no X11, no game binary.
add_executable(test_score test_score.c ${CMAKE_SOURCE_DIR}/src/score_logic.c)
target_include_directories(test_score PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_options(test_score PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_score PRIVATE ${CMOCKA_LIBRARIES})
add_test(NAME test_score COMMAND test_score)

# Level parsing characterization tests (bead n9e.3)
# Compiles production files (file.c, blocks.c, level.c, error.c) with
# X11 function stubs. Tests call ReadNextLevel(NULL, 0, path, False).
add_executable(test_level_parse
    test_level_parse.c
    stubs_x11_xboing.c
    ${CMAKE_SOURCE_DIR}/file.c
    ${CMAKE_SOURCE_DIR}/blocks.c
    ${CMAKE_SOURCE_DIR}/level.c
    ${CMAKE_SOURCE_DIR}/error.c
)
target_include_directories(test_level_parse PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
    /usr/include/X11
    ${X11_INCLUDE_DIRS}
    ${XPM_INCLUDE_DIRS}
)
target_compile_definitions(test_level_parse PRIVATE
    LEVELS_DIR="${CMAKE_SOURCE_DIR}/levels"
    READMEP_FILE="./docs/problems.doc"
    "HIGH_SCORE_FILE=\"./.xboing.scr\""
    "LEVEL_INSTALL_DIR=\"./levels\""
    "SOUNDS_DIR=\"./sounds\""
    "AUDIO_AVAILABLE=\"True\""
    "AUDIO_FILE=\"audio/LINUXaudio.c\""
)
target_compile_options(test_level_parse PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# Legacy production sources compiled into this test get the full suppression
# list and -Wno-error so they don't block the build.
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/file.c
    ${CMAKE_SOURCE_DIR}/blocks.c
    ${CMAKE_SOURCE_DIR}/level.c
    ${CMAKE_SOURCE_DIR}/error.c
    PROPERTIES COMPILE_OPTIONS "${XBOING_LEGACY_SUPPRESSIONS};-Wno-error"
)

target_link_libraries(test_level_parse PRIVATE ${CMOCKA_LIBRARIES} m)
add_test(NAME test_level_parse COMMAND test_level_parse)

# Block grid and type catalog characterization tests (bead n9e.5)
# Links blocks.c with X11 stubs — tests SetupBlockInfo(), AddNewBlock(draw=False),
# ClearBlock(), ClearBlockArray(), StillActiveBlocks() directly.
add_executable(test_blocks
    test_blocks.c
    stubs_x11_xboing.c
    ${CMAKE_SOURCE_DIR}/blocks.c
)
target_include_directories(test_blocks PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
    /usr/include/X11
    ${X11_INCLUDE_DIRS}
    ${XPM_INCLUDE_DIRS}
)
target_compile_definitions(test_blocks PRIVATE
    LEVELS_DIR="${CMAKE_SOURCE_DIR}/levels"
    READMEP_FILE="./docs/problems.doc"
    "HIGH_SCORE_FILE=\"./.xboing.scr\""
    "LEVEL_INSTALL_DIR=\"./levels\""
    "SOUNDS_DIR=\"./sounds\""
    "AUDIO_AVAILABLE=\"True\""
    "AUDIO_FILE=\"audio/LINUXaudio.c\""
)
target_compile_options(test_blocks PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# Legacy production source gets full suppression list and -Wno-error.
# NOTE: set_source_files_properties is directory-scoped, so this applies to
# blocks.c in ALL targets in this directory (including test_level_parse).
# This is acceptable here because all consumers are test targets. The main
# xboing executable is built from the parent CMakeLists.txt in a different
# directory scope and is unaffected.
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/blocks.c
    PROPERTIES COMPILE_OPTIONS "${XBOING_LEGACY_SUPPRESSIONS};-Wno-error"
)

target_link_libraries(test_blocks PRIVATE ${CMOCKA_LIBRARIES} m)
add_test(NAME test_blocks COMMAND test_blocks)

# XDG path resolution tests (bead xboing-ifb.2)
# Links paths.c directly — no X11, no game binary.
# Tests run from the source root so CWD fallback can find ./levels/ and ./sounds/.
add_executable(test_paths test_paths.c ${CMAKE_SOURCE_DIR}/src/paths.c)
target_include_directories(test_paths PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_options(test_paths PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_paths PRIVATE ${CMOCKA_LIBRARIES})
add_test(NAME test_paths COMMAND test_paths)
set_tests_properties(test_paths PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Save/load round-trip characterization tests (bead n9e.4)
# Pure format tests — no production .c files linked. Tests the binary
# file format at the byte level using struct definitions from headers.
xboing_add_test(test_save_load)

# State machine transition characterization tests (bead n9e.6)
# Pure data-driven tests — no production .c files linked. Characterizes
# mode constants, auto-cycle sequence, transition graph, and dispatch tables.
xboing_add_test(test_state_machine)

# SDL2 renderer lifecycle tests (bead xboing-oaa.1)
# Links against the sdl2_renderer static library.
# Uses SDL_VIDEODRIVER=dummy for headless CI testing.
if(SDL2_FOUND)
    add_executable(test_sdl2_renderer test_sdl2_renderer.c)
    target_compile_options(test_sdl2_renderer PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(test_sdl2_renderer PRIVATE sdl2_renderer ${CMOCKA_LIBRARIES})
    add_test(NAME test_sdl2_renderer COMMAND test_sdl2_renderer)
    set_tests_properties(test_sdl2_renderer PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
endif()

# SDL2 texture cache tests (bead xboing-oaa.2)
# Links against sdl2_texture and sdl2_renderer static libraries.
# Uses SDL_VIDEODRIVER=dummy and real PNGs from assets/images/.
if(SDL2_FOUND AND SDL2_IMAGE_FOUND)
    add_executable(test_sdl2_texture test_sdl2_texture.c)
    target_compile_options(test_sdl2_texture PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(test_sdl2_texture PRIVATE
        sdl2_texture sdl2_renderer ${CMOCKA_LIBRARIES})
    add_test(NAME test_sdl2_texture COMMAND test_sdl2_texture)
    set_tests_properties(test_sdl2_texture PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

# SDL2 font rendering tests (bead xboing-oaa.3)
# Links against sdl2_font and sdl2_renderer static libraries.
# Uses SDL_VIDEODRIVER=dummy and real TTFs from assets/fonts/.
if(SDL2_FOUND AND SDL2_TTF_FOUND)
    add_executable(test_sdl2_font test_sdl2_font.c)
    target_compile_options(test_sdl2_font PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(test_sdl2_font PRIVATE
        sdl2_font sdl2_renderer ${CMOCKA_LIBRARIES})
    add_test(NAME test_sdl2_font COMMAND test_sdl2_font)
    set_tests_properties(test_sdl2_font PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

# SDL2 color system tests (bead xboing-oaa.4)
# Pure data tests — no video driver needed, but SDL2 headers required.
if(SDL2_FOUND)
    add_executable(test_sdl2_color test_sdl2_color.c)
    target_compile_options(test_sdl2_color PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(test_sdl2_color PRIVATE sdl2_color ${CMOCKA_LIBRARIES})
    add_test(NAME test_sdl2_color COMMAND test_sdl2_color)
endif()

# SDL2 cursor management tests (bead xboing-oaa.5)
# Uses SDL_VIDEODRIVER=dummy for headless CI testing.
if(SDL2_FOUND)
    add_executable(test_sdl2_cursor test_sdl2_cursor.c)
    target_compile_options(test_sdl2_cursor PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(test_sdl2_cursor PRIVATE sdl2_cursor ${CMOCKA_LIBRARIES})
    add_test(NAME test_sdl2_cursor COMMAND test_sdl2_cursor)
    set_tests_properties(test_sdl2_cursor PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
endif()

# SDL2 render regions tests (bead xboing-oaa.6)
# Pure data tests — no video driver needed, but SDL2 headers required.
if(SDL2_FOUND)
    add_executable(test_sdl2_regions test_sdl2_regions.c)
    target_compile_options(test_sdl2_regions PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(test_sdl2_regions PRIVATE sdl2_regions ${CMOCKA_LIBRARIES})
    add_test(NAME test_sdl2_regions COMMAND test_sdl2_regions)
endif()

# SDL2 audio system tests (bead xboing-0lu.1)
# Links against sdl2_audio static library.
# Uses SDL_AUDIODRIVER=dummy for headless CI testing.
# Loads real WAV files from sounds/ directory.
if(SDL2_FOUND AND SDL2_MIXER_FOUND)
    add_executable(test_sdl2_audio test_sdl2_audio.c)
    target_compile_options(test_sdl2_audio PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(test_sdl2_audio PRIVATE sdl2_audio ${CMOCKA_LIBRARIES})
    add_test(NAME test_sdl2_audio COMMAND test_sdl2_audio)
    set_tests_properties(test_sdl2_audio PROPERTIES
        ENVIRONMENT "SDL_AUDIODRIVER=dummy"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

# SDL2 input mapping tests (bead xboing-cks.3)
# Pure data tests — synthesizes SDL_Event structs directly.
# No video or audio driver needed.
if(SDL2_FOUND)
    add_executable(test_sdl2_input test_sdl2_input.c)
    target_compile_options(test_sdl2_input PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(test_sdl2_input PRIVATE sdl2_input ${CMOCKA_LIBRARIES})
    add_test(NAME test_sdl2_input COMMAND test_sdl2_input)
endif()
