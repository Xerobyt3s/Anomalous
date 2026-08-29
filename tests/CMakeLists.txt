set(ANOMALOUS_TEST_SOURCES
    test.cpp
    test.h
    test_sanity.cpp
    test_link.cpp
)

add_executable(anomalous_tests ${ANOMALOUS_TEST_SOURCES})

target_link_libraries(anomalous_tests PRIVATE anomalous_core)

target_include_directories(anomalous_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

set_target_properties(anomalous_tests PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# One ctest entry for the whole suite. Individual cases are reachable directly with
# `anomalous_tests --filter=<substring>`, and `--list` enumerates them.
#
# Tests run from the repo root because later phases load assets by relative path, the
# same way the game does.
add_test(NAME anomalous_tests
         COMMAND anomalous_tests
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
