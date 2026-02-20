cmake_minimum_required(VERSION 3.16)
project(xboing C)

# Export compile_commands.json for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies -----------------------------------------------------------

find_package(PkgConfig REQUIRED)
pkg_check_modules(X11 REQUIRED x11)
pkg_check_modules(XPM REQUIRED xpm)

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

# --- Compile definitions (match Makefile exactly) ---------------------------

target_compile_definitions(xboing PRIVATE
    "HIGH_SCORE_FILE=\"./.xboing.scr\""
    "LEVEL_INSTALL_DIR=\"./levels\""
    "SOUNDS_DIR=\"./sounds\""
    "READMEP_FILE=\"./docs/problems.doc\""
    "AUDIO_AVAILABLE=\"True\""
    "AUDIO_FILE=\"audio/LINUXaudio.c\""
    NeedFunctionPrototypes=1
)

# --- Include directories ----------------------------------------------------

target_include_directories(xboing PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    /usr/include/X11
    ${X11_INCLUDE_DIRS}
    ${XPM_INCLUDE_DIRS}
)

# --- Compile options (match Makefile) ---------------------------------------
# Strict warnings deferred to xboing-91a.6 — this matches the Makefile flags.

target_compile_options(xboing PRIVATE
    -Wall
    -Wno-unused-result
    -Wno-format-overflow
    -Wno-format-truncation
)

# --- Link libraries ---------------------------------------------------------

target_link_libraries(xboing PRIVATE
    ${XPM_LIBRARIES}
    ${X11_LIBRARIES}
    m
)

# --- Tests ------------------------------------------------------------------

option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    pkg_check_modules(CMOCKA REQUIRED cmocka)
    enable_testing()
    add_subdirectory(tests)
endif()
