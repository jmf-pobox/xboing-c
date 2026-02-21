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
    target_compile_definitions(${NAME} PRIVATE
        NeedFunctionPrototypes=1
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
target_compile_definitions(test_ball_math PRIVATE
    NeedFunctionPrototypes=1
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
target_compile_definitions(test_score PRIVATE
    NeedFunctionPrototypes=1
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
    NeedFunctionPrototypes=1
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

# Save/load round-trip characterization tests (bead n9e.4)
# Pure format tests — no production .c files linked. Tests the binary
# file format at the byte level using struct definitions from headers.
xboing_add_test(test_save_load)
