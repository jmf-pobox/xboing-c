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
