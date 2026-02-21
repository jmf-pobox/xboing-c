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
)

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

# --- Tests ------------------------------------------------------------------

option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    pkg_check_modules(CMOCKA REQUIRED cmocka)
    enable_testing()
    add_subdirectory(tests)
endif()
