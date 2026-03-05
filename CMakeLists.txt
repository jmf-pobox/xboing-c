cmake_minimum_required(VERSION 3.16)
project(xboing C)
include(GNUInstallDirs)

# Export compile_commands.json for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies -----------------------------------------------------------

find_package(PkgConfig REQUIRED)
pkg_check_modules(X11 REQUIRED x11)
pkg_check_modules(XPM REQUIRED xpm)

# SDL2 libraries — optional until SDL2 rendering/audio code lands.
pkg_check_modules(SDL2       sdl2)
pkg_check_modules(SDL2_IMAGE SDL2_image)
pkg_check_modules(SDL2_MIXER SDL2_mixer)
pkg_check_modules(SDL2_TTF   SDL2_ttf)

# --- Generated sources ------------------------------------------------------

# audio.c — symlink to the Linux audio driver (matches Makefile)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/audio.c")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink audio/LINUXaudio.c audio.c
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

# version.c — build metadata (matches Makefile: sh ./version.sh xboing)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/version.c")
    execute_process(
        COMMAND sh ./version.sh xboing
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

# --- Executable -------------------------------------------------------------

# Source list matches Makefile SRCS exactly (29 files).
add_executable(xboing
    version.c
    main.c
    score.c
    error.c
    ball.c
    blocks.c
    init.c
    stage.c
    level.c
    paddle.c
    mess.c
    intro.c
    bonus.c
    sfx.c
    highscore.c
    misc.c
    inst.c
    gun.c
    keys.c
    audio.c
    special.c
    presents.c
    demo.c
    file.c
    preview.c
    dialogue.c
    eyedude.c
    editor.c
    keysedit.c
)

# --- Compile definitions ----------------------------------------------------

option(XBOING_USE_INSTALLED_PATHS
    "Set asset paths to installed FHS locations instead of relative (development) paths"
    OFF)

if(XBOING_USE_INSTALLED_PATHS)
    target_compile_definitions(xboing PRIVATE
        "HIGH_SCORE_FILE=\"./.xboing.scr\""
        "LEVEL_INSTALL_DIR=\"${CMAKE_INSTALL_FULL_DATADIR}/xboing/levels\""
        "SOUNDS_DIR=\"${CMAKE_INSTALL_FULL_DATADIR}/xboing/sounds\""
        "READMEP_FILE=\"${CMAKE_INSTALL_FULL_DATADIR}/xboing/docs/problems.doc\""
        "AUDIO_AVAILABLE=\"True\""
        "AUDIO_FILE=\"audio/LINUXaudio.c\""
    )
else()
    target_compile_definitions(xboing PRIVATE
        "HIGH_SCORE_FILE=\"./.xboing.scr\""
        "LEVEL_INSTALL_DIR=\"./levels\""
        "SOUNDS_DIR=\"./sounds\""
        "READMEP_FILE=\"./docs/problems.doc\""
        "AUDIO_AVAILABLE=\"True\""
        "AUDIO_FILE=\"audio/LINUXaudio.c\""
    )
endif()

# --- Include directories ----------------------------------------------------

target_include_directories(xboing PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    /usr/include/X11
    ${X11_INCLUDE_DIRS}
    ${XPM_INCLUDE_DIRS}
)

# --- Compiler warning policy ------------------------------------------------
#
# Two-tier policy (xboing-91a.6):
#   Tier 1 — strict + -Werror: test targets (modernized code, zero warnings).
#   Tier 2 — strict + suppressions, no -Werror: xboing target (legacy code).
#
# Each suppression documents its warning count (from diagnostic build) and
# the bead that will remove it.  -Werror gets added to xboing when the last
# suppression is removed (xboing-cro).

include(CheckCCompilerFlag)

set(XBOING_STRICT_WARNINGS
    -Wall -Wextra -Wpedantic
    -Wconversion -Wshadow -Wdouble-promotion
    -Wformat=2
    -Wnull-dereference -Wuninitialized
    -Wstrict-prototypes -Wold-style-definition
)

# -Wformat-overflow=2 is GCC-only (Clang lacks the =N level syntax).
check_c_compiler_flag(-Wformat-overflow=2 HAVE_WFORMAT_OVERFLOW_2)
if(HAVE_WFORMAT_OVERFLOW_2)
    list(APPEND XBOING_STRICT_WARNINGS -Wformat-overflow=2)
endif()

set(XBOING_LEGACY_SUPPRESSIONS
    # Count | Removal bead   | Category
    -Wno-conversion              # 114 | xboing-cro.2 | implicit narrowing
    -Wno-sign-conversion         # 104 | xboing-cro.2 | signed/unsigned mismatch
    -Wno-unused-parameter        # 106 | xboing-cro.2 | unused function params
    -Wno-shadow                  #  51 | xboing-cro.2 | variable shadowing
    -Wno-double-promotion        #  27 | xboing-cro.2 | float→double promotion
    -Wno-float-conversion        #  18 | xboing-cro.2 | float truncation
    -Wno-misleading-indentation  #  15 | xboing-cro.3 | indentation vs braces
    -Wno-sign-compare            #   4 | xboing-cro.2 | signed/unsigned compare
    -Wno-unused-but-set-variable #   4 | xboing-cro.2 | set but never read
    -Wno-unused-variable         #   3 | xboing-cro.2 | declared but unused
    -Wno-implicit-function-declaration #  3 | xboing-cro.1 | missing #include
    -Wno-format-y2k              #   3 | xboing-cro.2 | %y in strftime
    -Wno-return-type             #   2 | xboing-cro.1 | missing return
    -Wno-pointer-compare         #   2 | xboing-cro.2 | suspicious comparison
    -Wno-overflow                #   1 | xboing-cro.2 | integer overflow
    -Wno-stringop-overread       #   1 | xboing-cro.2 | out-of-bounds read
    -Wno-unused-function         #   1 | xboing-cro.2 | unreferenced static
)

target_compile_options(xboing PRIVATE
    ${XBOING_STRICT_WARNINGS}
    ${XBOING_LEGACY_SUPPRESSIONS}
)

# --- Link libraries ---------------------------------------------------------

target_link_libraries(xboing PRIVATE
    ${XPM_LIBRARIES}
    ${X11_LIBRARIES}
    m
)

# --- Install rules ----------------------------------------------------------

install(TARGETS xboing RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(DIRECTORY levels/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/xboing/levels"
    FILES_MATCHING PATTERN "*.data"
    PATTERN "CVS" EXCLUDE)

install(DIRECTORY sounds/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/xboing/sounds"
    FILES_MATCHING
        PATTERN "*.au"
        PATTERN "*.wav"
    PATTERN "CVS" EXCLUDE)

install(DIRECTORY assets/fonts/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/xboing/fonts"
    FILES_MATCHING
        PATTERN "*.ttf"
        PATTERN "LICENSE"
    PATTERN "CVS" EXCLUDE)

install(FILES docs/problems.doc
    DESTINATION "${CMAKE_INSTALL_DATADIR}/xboing/docs")

install(FILES xboing.man
    DESTINATION "${CMAKE_INSTALL_MANDIR}/man6"
    RENAME xboing.6)

install(FILES packaging/xboing.desktop
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")

install(FILES packaging/com.github.xboing.metainfo.xml
    DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")

install(FILES packaging/icons/xboing-16.png
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/16x16/apps"
    RENAME xboing.png)
install(FILES packaging/icons/xboing-32.png
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/32x32/apps"
    RENAME xboing.png)
install(FILES packaging/icons/xboing-48.png
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/48x48/apps"
    RENAME xboing.png)
install(FILES packaging/icons/xboing-128.png
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps"
    RENAME xboing.png)
install(FILES packaging/icons/xboing-256.png
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps"
    RENAME xboing.png)

# --- SDL2 renderer library (optional) ---------------------------------------

if(SDL2_FOUND)
    add_library(sdl2_renderer STATIC src/sdl2_renderer.c)
    target_include_directories(sdl2_renderer PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${SDL2_INCLUDE_DIRS}
    )
    target_compile_options(sdl2_renderer PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(sdl2_renderer PUBLIC ${SDL2_LIBRARIES})
endif()

# --- SDL2 texture cache library (optional) ----------------------------------

if(SDL2_FOUND AND SDL2_IMAGE_FOUND)
    add_library(sdl2_texture STATIC src/sdl2_texture.c)
    target_include_directories(sdl2_texture PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_IMAGE_INCLUDE_DIRS}
    )
    target_compile_options(sdl2_texture PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(sdl2_texture PUBLIC ${SDL2_LIBRARIES} ${SDL2_IMAGE_LIBRARIES})
endif()

# --- SDL2 font rendering library (optional) ---------------------------------

if(SDL2_FOUND AND SDL2_TTF_FOUND)
    add_library(sdl2_font STATIC src/sdl2_font.c)
    target_include_directories(sdl2_font PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS}
    )
    target_compile_options(sdl2_font PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(sdl2_font PUBLIC ${SDL2_LIBRARIES} ${SDL2_TTF_LIBRARIES})
endif()

# --- SDL2 color system library (optional) ------------------------------------

if(SDL2_FOUND)
    add_library(sdl2_color STATIC src/sdl2_color.c)
    target_include_directories(sdl2_color PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${SDL2_INCLUDE_DIRS}
    )
    target_compile_options(sdl2_color PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(sdl2_color PUBLIC ${SDL2_LIBRARIES})
endif()

# --- SDL2 cursor management library (optional) ------------------------------

if(SDL2_FOUND)
    add_library(sdl2_cursor STATIC src/sdl2_cursor.c)
    target_include_directories(sdl2_cursor PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${SDL2_INCLUDE_DIRS}
    )
    target_compile_options(sdl2_cursor PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(sdl2_cursor PUBLIC ${SDL2_LIBRARIES})
endif()

# --- SDL2 render regions library (optional) ----------------------------------

if(SDL2_FOUND)
    add_library(sdl2_regions STATIC src/sdl2_regions.c)
    target_include_directories(sdl2_regions PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${SDL2_INCLUDE_DIRS}
    )
    target_compile_options(sdl2_regions PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(sdl2_regions PUBLIC ${SDL2_LIBRARIES})
endif()

# --- SDL2 audio library (optional) ------------------------------------------

if(SDL2_FOUND AND SDL2_MIXER_FOUND)
    add_library(sdl2_audio STATIC src/sdl2_audio.c)
    target_include_directories(sdl2_audio PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_MIXER_INCLUDE_DIRS}
    )
    target_compile_options(sdl2_audio PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(sdl2_audio PUBLIC ${SDL2_LIBRARIES} ${SDL2_MIXER_LIBRARIES})
endif()

# --- SDL2 input mapping library (optional) -----------------------------------

if(SDL2_FOUND)
    add_library(sdl2_input STATIC src/sdl2_input.c)
    target_include_directories(sdl2_input PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${SDL2_INCLUDE_DIRS}
    )
    target_compile_options(sdl2_input PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(sdl2_input PUBLIC ${SDL2_LIBRARIES})
endif()

# --- Game state machine library -----------------------------------------------
#
# Pure C module — no SDL2 dependency.  Manages game mode transitions with
# function pointer dispatch, dialogue push/pop, and frame counting.

add_library(sdl2_state STATIC src/sdl2_state.c)
target_include_directories(sdl2_state PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(sdl2_state PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- Fixed-timestep game loop library ----------------------------------------
#
# Pure C module — no SDL2 dependency.  Manages accumulator-based fixed-timestep
# timing with configurable speed levels and spiral-of-death prevention.

add_library(sdl2_loop STATIC src/sdl2_loop.c)
target_include_directories(sdl2_loop PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(sdl2_loop PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- CLI option parsing library ----------------------------------------------
#
# Pure C module — no SDL2 dependency.  Parses command-line arguments into a
# config struct for the SDL2-based game binary.

add_library(sdl2_cli STATIC src/sdl2_cli.c)
target_include_directories(sdl2_cli PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(sdl2_cli PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- Ball physics system library ---------------------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Owns the BALL array, state
# machine dispatch, physics, and queries.  Side effects communicated via
# injected callback table.  Links ball_math.c for extracted physics functions.

add_library(ball_system STATIC src/ball_system.c ball_math.c)
target_include_directories(ball_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(ball_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(ball_system PUBLIC m)

# --- Score arithmetic library ------------------------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Provides multiplier application,
# extra life threshold calculation, bonus scoring, and block hit point lookup.
# Shared by block_system (hit points) and score_system (multiplier/threshold).

add_library(score_logic STATIC src/score_logic.c)
target_include_directories(score_logic PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(score_logic PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- Block grid system library -----------------------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Owns the 18x9 block grid,
# collision geometry (pure C diagonal cross-product collision replacing X11 Regions), block info
# catalog, and grid queries.  Links score_logic for hit point calculation.

add_library(block_system STATIC src/block_system.c)
target_include_directories(block_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(block_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(block_system PUBLIC score_logic)

# --- Paddle system library --------------------------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Owns paddle position, size,
# control flags (reverse, sticky), and movement logic with clamping.

add_library(paddle_system STATIC src/paddle_system.c)
target_include_directories(paddle_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(paddle_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- Gun/bullet system library ----------------------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Owns bullet and tink arrays,
# ammo state, movement physics, and collision dispatch.  Side effects
# communicated via injected callback table.

add_library(gun_system STATIC src/gun_system.c)
target_include_directories(gun_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(gun_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- Score display system library -------------------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Owns the game score value,
# multiplier application, extra life tracking, and digit layout computation
# for rendering.  Links score_logic for arithmetic functions.

add_library(score_system STATIC src/score_system.c)
target_include_directories(score_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(score_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
target_link_libraries(score_system PUBLIC score_logic)

# --- Level file loading library ----------------------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Owns level file parsing,
# character-to-block-type mapping, level number wrapping, and background
# cycling.  Delegates block creation via injected callback.

add_library(level_system STATIC src/level_system.c)
target_include_directories(level_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(level_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- Special/power-up system library ----------------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Owns 7 boolean special flags
# (sticky, saving, fastGun, noWalls, killer, x2, x4) and their toggle logic.
# Side effects communicated via injected callback table.

add_library(special_system STATIC src/special_system.c)
target_include_directories(special_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(special_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- Bonus tally sequence library -------------------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Owns the 10-state bonus screen
# state machine, coin counting, score computation, and save trigger logic.
# Side effects communicated via injected callback table.

add_library(bonus_system STATIC src/bonus_system.c)
target_include_directories(bonus_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(bonus_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

add_library(sfx_system STATIC src/sfx_system.c)
target_include_directories(sfx_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(sfx_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- EyeDude animated character system library -------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Owns the EyeDude state machine
# (walk, turn, die), collision detection, and render info queries.
# Side effects communicated via injected callback table.

add_library(eyedude_system STATIC src/eyedude_system.c)
target_include_directories(eyedude_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(eyedude_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- Presents/splash screen sequencer library --------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Owns the 14-state presents
# animation sequence (flag, credits, letter stamps, sparkle, typewriter
# text, curtain wipe).  Side effects via injected callback table.

add_library(presents_system STATIC src/presents_system.c)
target_include_directories(presents_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(presents_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- Intro and instructions screen sequencer library -------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Owns both the intro screen
# (block descriptions + sparkle) and instructions screen (text + sparkle).
# Static data tables for block descriptions and instruction text.

add_library(intro_system STATIC src/intro_system.c)
target_include_directories(intro_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(intro_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- Demo and preview screen sequencer library --------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Owns both the demo screen
# (gameplay illustration + sparkle loop) and preview screen (random level
# display + timed wait).  Static data tables for ball trail and text.

add_library(demo_system STATIC src/demo_system.c)
target_include_directories(demo_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(demo_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- Keys and editor controls screen sequencer library -----------------------
#
# Pure C module — no SDL2 or X11 dependency.  Owns both the game controls
# screen (key bindings + mouse diagram) and editor controls screen (editor
# instructions + editor key bindings).  Static data tables for key bindings.

add_library(keys_system STATIC src/keys_system.c)
target_include_directories(keys_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(keys_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- Modal input dialogue system library -------------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Owns modal text input with
# 4 validation modes (text, numeric, all printable, yes/no), input buffer
# management, and submit/cancel state machine.

add_library(dialogue_system STATIC src/dialogue_system.c)
target_include_directories(dialogue_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(dialogue_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- High score display screen sequencer library ----------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Owns the high score display
# state machine (title, show, sparkle loop, finish) with title sparkle,
# row sparkle, and specials timing.  Score data provided by integration.

add_library(highscore_system STATIC src/highscore_system.c)
target_include_directories(highscore_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(highscore_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- Message display system library ----------------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Manages the single-line
# message bar: set with optional auto-clear delay, reverts to default
# message (level name) when the delay expires.

add_library(message_system STATIC src/message_system.c)
target_include_directories(message_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(message_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- High score JSON I/O library -------------------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Reads and writes high score
# tables as JSON files, with sorting, insertion, and atomic writes.
# Replaces the legacy binary format with htonl/ntohl byte ordering.

add_library(highscore_io STATIC src/highscore_io.c)
target_include_directories(highscore_io PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(highscore_io PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- Save game JSON I/O library --------------------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Reads and writes game state
# as JSON files, with atomic writes.  Replaces the legacy binary
# saveGameStruct format.

add_library(savegame_io STATIC src/savegame_io.c)
target_include_directories(savegame_io PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(savegame_io PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- Level editor system library -------------------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Owns editor state machine,
# grid editing (draw/erase), board transforms (flip/scroll H/V), palette
# management, and save/load coordination via injected callbacks.

add_library(editor_system STATIC src/editor_system.c)
target_include_directories(editor_system PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(editor_system PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- TOML config I/O library -----------------------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  Reads and writes user
# preferences as minimal TOML (flat key=value, no tables).  Replaces
# compile-time defines and env vars for persistent user settings.

add_library(config_io STATIC src/config_io.c)
target_include_directories(config_io PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(config_io PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- XDG paths library -------------------------------------------------------
#
# Pure C module — no SDL2 or X11 dependency.  XDG Base Directory path resolution
# for levels, sounds, scores, and save files.

add_library(paths STATIC src/paths.c)
target_include_directories(paths PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_options(paths PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)

# --- SDL2 game executable (integration layer) --------------------------------
#
# The new SDL2-based game binary.  Links all pure C system libraries and SDL2
# platform libraries.  Built only when all required SDL2 libs are available.

if(SDL2_FOUND AND SDL2_IMAGE_FOUND AND SDL2_MIXER_FOUND AND SDL2_TTF_FOUND)
    add_executable(xboing_sdl2
        src/game_callbacks.c
        src/game_init.c
        src/game_input.c
        src/game_main.c
        src/game_render.c
    )
    target_include_directories(xboing_sdl2 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${SDL2_INCLUDE_DIRS}
    )
    target_compile_options(xboing_sdl2 PRIVATE ${XBOING_STRICT_WARNINGS} -Werror)
    target_link_libraries(xboing_sdl2 PRIVATE
        # SDL2 platform
        sdl2_renderer
        sdl2_texture
        sdl2_font
        sdl2_audio
        sdl2_input
        sdl2_cursor
        sdl2_state
        sdl2_loop
        sdl2_cli
        # Game systems
        ball_system
        block_system
        paddle_system
        gun_system
        score_system
        level_system
        special_system
        bonus_system
        sfx_system
        eyedude_system
        message_system
        editor_system
        # Persistence
        highscore_io
        savegame_io
        config_io
        paths
        # Math
        score_logic
        m
    )
    # UI sequencers
    target_link_libraries(xboing_sdl2 PRIVATE
        presents_system
        intro_system
        demo_system
        keys_system
        dialogue_system
        highscore_system
    )
endif()

# --- Tests ------------------------------------------------------------------

option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    pkg_check_modules(CMOCKA REQUIRED cmocka)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Configuration summary ---------------------------------------------------

message(STATUS "")
message(STATUS "XBoing configuration summary:")
message(STATUS "  X11:        ${X11_VERSION}")
message(STATUS "  XPM:        ${XPM_VERSION}")
message(STATUS "  SDL2:       ${SDL2_VERSION}")
message(STATUS "  SDL2_image: ${SDL2_IMAGE_VERSION}")
message(STATUS "  SDL2_mixer: ${SDL2_MIXER_VERSION}")
message(STATUS "  SDL2_ttf:   ${SDL2_TTF_VERSION}")
if(BUILD_TESTING)
    message(STATUS "  CMocka:     ${CMOCKA_VERSION}")
else()
    message(STATUS "  CMocka:     (tests disabled)")
endif()
if(XBOING_USE_INSTALLED_PATHS)
    message(STATUS "  Asset paths:    installed (${CMAKE_INSTALL_FULL_DATADIR}/xboing/)")
else()
    message(STATUS "  Asset paths:    development (relative to CWD)")
endif()
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
