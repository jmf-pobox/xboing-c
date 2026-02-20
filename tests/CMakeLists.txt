# Test executables for XBoing
#
# Convention: one test file per source module — tests/test_<module>.c
# All tests link against cmocka and the project include/ directory.

# Helper function: add a CMocka test executable and register it with ctest.
function(xboing_add_test NAME)
    add_executable(${NAME} ${NAME}.c)
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        /usr/include/X11
        ${X11_INCLUDE_DIRS}
        ${XPM_INCLUDE_DIRS}
    )
    target_compile_definitions(${NAME} PRIVATE
        NeedFunctionPrototypes=1
    )
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
target_link_libraries(test_score PRIVATE ${CMOCKA_LIBRARIES})
add_test(NAME test_score COMMAND test_score)
