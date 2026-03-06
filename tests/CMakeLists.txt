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
# Links score_logic library — no X11, no game binary.
add_executable(test_score test_score.c)
target_compile_options(test_score PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_score PRIVATE score_logic ${CMOCKA_LIBRARIES})
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

# Game state machine tests (bead xboing-cks.2)
# Pure logic tests — no SDL2, video, or audio driver needed.
add_executable(test_sdl2_state test_sdl2_state.c)
target_compile_options(test_sdl2_state PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_sdl2_state PRIVATE sdl2_state ${CMOCKA_LIBRARIES})
add_test(NAME test_sdl2_state COMMAND test_sdl2_state)

# Fixed-timestep game loop tests (bead xboing-cks.1)
# Pure logic tests — no SDL2, video, or audio driver needed.
# Synthetic time injection makes all tests deterministic.
add_executable(test_sdl2_loop test_sdl2_loop.c)
target_compile_options(test_sdl2_loop PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_sdl2_loop PRIVATE sdl2_loop ${CMOCKA_LIBRARIES})
add_test(NAME test_sdl2_loop COMMAND test_sdl2_loop)

# CLI option parsing tests (bead xboing-1fr.4)
# Pure logic tests — no SDL2, video, or audio driver needed.
add_executable(test_sdl2_cli test_sdl2_cli.c)
target_compile_options(test_sdl2_cli PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_sdl2_cli PRIVATE sdl2_cli ${CMOCKA_LIBRARIES})
add_test(NAME test_sdl2_cli COMMAND test_sdl2_cli)

# Ball physics system tests (bead xboing-1ka.1)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against ball_system static library (includes ball_math.c + libm).
add_executable(test_ball_system test_ball_system.c)
target_compile_options(test_ball_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_ball_system PRIVATE ball_system ${CMOCKA_LIBRARIES})
add_test(NAME test_ball_system COMMAND test_ball_system)

# Block grid system tests (bead xboing-1ka.2)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against block_system static library (includes score_logic.c).
add_executable(test_block_system test_block_system.c)
target_compile_options(test_block_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_block_system PRIVATE block_system ${CMOCKA_LIBRARIES})
add_test(NAME test_block_system COMMAND test_block_system)

# Paddle system tests (bead xboing-1ka.3)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against paddle_system static library.
add_executable(test_paddle_system test_paddle_system.c)
target_compile_options(test_paddle_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_paddle_system PRIVATE paddle_system ${CMOCKA_LIBRARIES})
add_test(NAME test_paddle_system COMMAND test_paddle_system)

# Gun/bullet system tests (bead xboing-1ka.4)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against gun_system static library.
add_executable(test_gun_system test_gun_system.c)
target_compile_options(test_gun_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_gun_system PRIVATE gun_system ${CMOCKA_LIBRARIES})
add_test(NAME test_gun_system COMMAND test_gun_system)

# Score display system tests (bead xboing-1ka.5)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against score_system static library (includes score_logic.c).
add_executable(test_score_system test_score_system.c)
target_compile_options(test_score_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_score_system PRIVATE score_system ${CMOCKA_LIBRARIES})
add_test(NAME test_score_system COMMAND test_score_system)

# Level file loading system tests (bead xboing-1ka.6)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against level_system static library.
# Tests load real level files from the source levels/ directory.
add_executable(test_level_system test_level_system.c)
target_compile_definitions(test_level_system PRIVATE
    LEVELS_DIR="${CMAKE_SOURCE_DIR}/levels"
)
target_compile_options(test_level_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_level_system PRIVATE level_system ${CMOCKA_LIBRARIES})
add_test(NAME test_level_system COMMAND test_level_system)

# Special/power-up system tests (bead xboing-qf4.1)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against special_system static library.
add_executable(test_special_system test_special_system.c)
target_compile_options(test_special_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_special_system PRIVATE special_system ${CMOCKA_LIBRARIES})
add_test(NAME test_special_system COMMAND test_special_system)

# Bonus tally sequence tests (bead xboing-qf4.2)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against bonus_system static library.
add_executable(test_bonus_system test_bonus_system.c)
target_compile_options(test_bonus_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_bonus_system PRIVATE bonus_system ${CMOCKA_LIBRARIES})
add_test(NAME test_bonus_system COMMAND test_bonus_system)

# Visual SFX system tests (bead xboing-qf4.3)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against sfx_system static library.
add_executable(test_sfx_system test_sfx_system.c)
target_compile_options(test_sfx_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_sfx_system PRIVATE sfx_system ${CMOCKA_LIBRARIES})
add_test(NAME test_sfx_system COMMAND test_sfx_system)

# EyeDude animated character system tests (bead xboing-qf4.5)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against eyedude_system static library.
add_executable(test_eyedude_system test_eyedude_system.c)
target_compile_options(test_eyedude_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_eyedude_system PRIVATE eyedude_system ${CMOCKA_LIBRARIES})
add_test(NAME test_eyedude_system COMMAND test_eyedude_system)

# Presents/splash screen sequencer tests (bead xboing-dm8.1)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against presents_system static library.
add_executable(test_presents_system test_presents_system.c)
target_compile_options(test_presents_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_presents_system PRIVATE presents_system ${CMOCKA_LIBRARIES})
add_test(NAME test_presents_system COMMAND test_presents_system)

# Intro and instructions screen sequencer tests (bead xboing-dm8.2)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against intro_system static library.
add_executable(test_intro_system test_intro_system.c)
target_compile_options(test_intro_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_intro_system PRIVATE intro_system ${CMOCKA_LIBRARIES})
add_test(NAME test_intro_system COMMAND test_intro_system)

# Demo and preview screen sequencer tests (bead xboing-dm8.3)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against demo_system static library.
add_executable(test_demo_system test_demo_system.c)
target_compile_options(test_demo_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_demo_system PRIVATE demo_system ${CMOCKA_LIBRARIES})
add_test(NAME test_demo_system COMMAND test_demo_system)

# Keys and editor controls screen sequencer tests (bead xboing-dm8.4)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against keys_system static library.
add_executable(test_keys_system test_keys_system.c)
target_compile_options(test_keys_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_keys_system PRIVATE keys_system ${CMOCKA_LIBRARIES})
add_test(NAME test_keys_system COMMAND test_keys_system)

# Modal input dialogue system tests (bead xboing-dm8.5)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against dialogue_system static library.
add_executable(test_dialogue_system test_dialogue_system.c)
target_compile_options(test_dialogue_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_dialogue_system PRIVATE dialogue_system ${CMOCKA_LIBRARIES})
add_test(NAME test_dialogue_system COMMAND test_dialogue_system)

# High score display screen sequencer tests (bead xboing-dm8.6)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against highscore_system static library.
add_executable(test_highscore_system test_highscore_system.c)
target_compile_options(test_highscore_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_highscore_system PRIVATE highscore_system ${CMOCKA_LIBRARIES})
add_test(NAME test_highscore_system COMMAND test_highscore_system)

# Message display system tests (bead xboing-dm8.7)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against message_system static library.
add_executable(test_message_system test_message_system.c)
target_compile_options(test_message_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_message_system PRIVATE message_system ${CMOCKA_LIBRARIES})
add_test(NAME test_message_system COMMAND test_message_system)

# High score JSON I/O tests (bead xboing-1fr.1)
# Tests JSON read/write round-trips, sorting, insertion, ranking, and error handling.
# Links against highscore_io static library (which depends on highscore_system for types).
add_executable(test_highscore_io test_highscore_io.c)
target_compile_options(test_highscore_io PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_highscore_io PRIVATE highscore_io ${CMOCKA_LIBRARIES})
add_test(NAME test_highscore_io COMMAND test_highscore_io)

# Save game JSON I/O tests (bead xboing-1fr.2)
# Tests JSON read/write round-trips, file operations, error handling.
# Links against savegame_io static library.
add_executable(test_savegame_io test_savegame_io.c)
target_compile_options(test_savegame_io PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_savegame_io PRIVATE savegame_io ${CMOCKA_LIBRARIES})
add_test(NAME test_savegame_io COMMAND test_savegame_io)

# Level editor system tests (bead xboing-9pl.1)
# Pure logic tests — no SDL2, X11, video, or audio driver needed.
# Links against editor_system static library.
add_executable(test_editor_system test_editor_system.c)
target_compile_options(test_editor_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_editor_system PRIVATE editor_system ${CMOCKA_LIBRARIES})
add_test(NAME test_editor_system COMMAND test_editor_system)

# TOML config I/O tests (bead xboing-1fr.3)
# Tests TOML read/write round-trips, parsing, file operations, error handling.
# Links against config_io static library.
add_executable(test_config_io test_config_io.c)
target_compile_options(test_config_io PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_config_io PRIVATE config_io ${CMOCKA_LIBRARIES})
add_test(NAME test_config_io COMMAND test_config_io)

# Editor round-trip tests — save level file → load → verify grid matches.
# Tests character mapping exhaustively and file I/O round-trip cycle.
# Links against level_system (for load + char_to_block).
add_executable(test_editor_roundtrip test_editor_roundtrip.c)
target_compile_definitions(test_editor_roundtrip PRIVATE
    LEVELS_DIR="${CMAKE_SOURCE_DIR}/levels"
)
target_compile_options(test_editor_roundtrip PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(test_editor_roundtrip PRIVATE level_system ${CMOCKA_LIBRARIES})
add_test(NAME test_editor_roundtrip COMMAND test_editor_roundtrip)

# Level parser fuzz test (clang only — requires libFuzzer).
# NOT registered as ctest — run manually: ./build/tests/fuzz_level_parse -max_total_time=30
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_executable(fuzz_level_parse fuzz_level_parse.c)
    target_include_directories(fuzz_level_parse PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_options(fuzz_level_parse PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(fuzz_level_parse PRIVATE
        -fsanitize=fuzzer,address,undefined)
    target_link_libraries(fuzz_level_parse PRIVATE level_system)
    # Not a ctest target — fuzz tests run indefinitely by default
endif()

# Integration smoke test — full game_create/destroy lifecycle.
# Links all game_*.c integration sources + all static libraries.
# Uses SDL_VIDEODRIVER=dummy + SDL_AUDIODRIVER=dummy for headless operation.
if(SDL2_FOUND AND SDL2_IMAGE_FOUND AND SDL2_MIXER_FOUND AND SDL2_TTF_FOUND)
    add_executable(test_integration_smoke
        test_integration_smoke.c
        ${CMAKE_SOURCE_DIR}/src/game_callbacks.c
        ${CMAKE_SOURCE_DIR}/src/game_init.c
        ${CMAKE_SOURCE_DIR}/src/game_input.c
        ${CMAKE_SOURCE_DIR}/src/game_modes.c
        ${CMAKE_SOURCE_DIR}/src/game_render.c
        ${CMAKE_SOURCE_DIR}/src/game_render_ui.c
        ${CMAKE_SOURCE_DIR}/src/game_rules.c
    )
    target_include_directories(test_integration_smoke PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${SDL2_INCLUDE_DIRS}
    )
    target_compile_options(test_integration_smoke PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(test_integration_smoke PRIVATE
        # SDL2 platform
        sdl2_renderer sdl2_texture sdl2_font sdl2_audio sdl2_input
        sdl2_cursor sdl2_state sdl2_loop sdl2_cli
        # Game systems
        ball_system block_system paddle_system gun_system score_system
        level_system special_system bonus_system sfx_system eyedude_system
        message_system editor_system
        # Persistence
        highscore_io savegame_io config_io paths
        # Math
        score_logic m
        # UI sequencers
        presents_system intro_system demo_system keys_system
        dialogue_system highscore_system
        # Test framework
        ${CMOCKA_LIBRARIES}
    )
    add_test(NAME test_integration_smoke COMMAND test_integration_smoke)
    set_tests_properties(test_integration_smoke PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    # Attract-mode auto-cycle integration test (bead xboing-3w5.1.1)
    # Ticks the state machine thousands of frames, verifying the full
    # attract sequence: PRESENTS→INTRO→INSTRUCT→DEMO→KEYS→KEYSEDIT→PREVIEW→HIGHSCORE→INTRO
    add_executable(test_integration_autocycle
        test_integration_autocycle.c
        ${CMAKE_SOURCE_DIR}/src/game_callbacks.c
        ${CMAKE_SOURCE_DIR}/src/game_init.c
        ${CMAKE_SOURCE_DIR}/src/game_input.c
        ${CMAKE_SOURCE_DIR}/src/game_modes.c
        ${CMAKE_SOURCE_DIR}/src/game_render.c
        ${CMAKE_SOURCE_DIR}/src/game_render_ui.c
        ${CMAKE_SOURCE_DIR}/src/game_rules.c
    )
    target_include_directories(test_integration_autocycle PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${SDL2_INCLUDE_DIRS}
    )
    target_compile_options(test_integration_autocycle PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(test_integration_autocycle PRIVATE
        sdl2_renderer sdl2_texture sdl2_font sdl2_audio sdl2_input
        sdl2_cursor sdl2_state sdl2_loop sdl2_cli
        ball_system block_system paddle_system gun_system score_system
        level_system special_system bonus_system sfx_system eyedude_system
        message_system editor_system
        highscore_io savegame_io config_io paths
        score_logic m
        presents_system intro_system demo_system keys_system
        dialogue_system highscore_system
        ${CMOCKA_LIBRARIES}
    )
    add_test(NAME test_integration_autocycle COMMAND test_integration_autocycle)
    set_tests_properties(test_integration_autocycle PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 60)

    # Mode handler crash test (bead xboing-3w5.1.2)
    # Force-transitions to each of 12 registered modes, ticks N frames,
    # verifies no crashes.  Also tests dialogue push/pop and rapid transitions.
    add_executable(test_integration_modes
        test_integration_modes.c
        ${CMAKE_SOURCE_DIR}/src/game_callbacks.c
        ${CMAKE_SOURCE_DIR}/src/game_init.c
        ${CMAKE_SOURCE_DIR}/src/game_input.c
        ${CMAKE_SOURCE_DIR}/src/game_modes.c
        ${CMAKE_SOURCE_DIR}/src/game_render.c
        ${CMAKE_SOURCE_DIR}/src/game_render_ui.c
        ${CMAKE_SOURCE_DIR}/src/game_rules.c
    )
    target_include_directories(test_integration_modes PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${SDL2_INCLUDE_DIRS}
    )
    target_compile_options(test_integration_modes PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(test_integration_modes PRIVATE
        sdl2_renderer sdl2_texture sdl2_font sdl2_audio sdl2_input
        sdl2_cursor sdl2_state sdl2_loop sdl2_cli
        ball_system block_system paddle_system gun_system score_system
        level_system special_system bonus_system sfx_system eyedude_system
        message_system editor_system
        highscore_io savegame_io config_io paths
        score_logic m
        presents_system intro_system demo_system keys_system
        dialogue_system highscore_system
        ${CMOCKA_LIBRARIES}
    )
    add_test(NAME test_integration_modes COMMAND test_integration_modes)
    set_tests_properties(test_integration_modes PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    # Helper: add an integration test that links the full game stack.
    # All integration tests share the same source + library set.
    # Extra source files can be passed after the test name.
    function(xboing_add_integration_test NAME)
        add_executable(${NAME}
            ${NAME}.c
            ${ARGN}
            ${CMAKE_SOURCE_DIR}/src/game_callbacks.c
            ${CMAKE_SOURCE_DIR}/src/game_init.c
            ${CMAKE_SOURCE_DIR}/src/game_input.c
            ${CMAKE_SOURCE_DIR}/src/game_modes.c
            ${CMAKE_SOURCE_DIR}/src/game_render.c
            ${CMAKE_SOURCE_DIR}/src/game_render_ui.c
            ${CMAKE_SOURCE_DIR}/src/game_rules.c
        )
        target_include_directories(${NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
            ${SDL2_INCLUDE_DIRS}
        )
        target_compile_options(${NAME} PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
        target_link_libraries(${NAME} PRIVATE
            sdl2_renderer sdl2_texture sdl2_font sdl2_audio sdl2_input
            sdl2_cursor sdl2_state sdl2_loop sdl2_cli
            ball_system block_system paddle_system gun_system score_system
            level_system special_system bonus_system sfx_system eyedude_system
            message_system editor_system
            highscore_io savegame_io config_io paths
            score_logic m
            presents_system intro_system demo_system keys_system
            dialogue_system highscore_system
            ${CMOCKA_LIBRARIES}
        )
        add_test(NAME ${NAME} COMMAND ${NAME})
        set_tests_properties(${NAME} PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    endfunction()

    # Gameplay integration test (bead xboing-3w5.1.3)
    # Enters GAME mode, verifies state initialization, block loading,
    # ball/paddle state, and extended gameplay ticking.
    xboing_add_integration_test(test_integration_gameplay)

    # Editor integration test (bead xboing-3w5.1.4)
    # Enters EDIT mode, draws blocks, clears grid, tests play-test
    # transitions and editor key commands through the full stack.
    xboing_add_integration_test(test_integration_editor)

    # Input replay smoke test (bead xboing-3w5.2.1)
    # Tests the replay infrastructure: synthetic key injection, scripted
    # mode transitions, game start from attract, pause, and extended
    # gameplay with active input.
    xboing_add_integration_test(test_replay_smoke test_replay.c)
endif()
