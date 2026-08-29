set(ANOMALOUS_TEST_SOURCES
    test.cpp
    test.h
    test_sanity.cpp
    test_link.cpp
    test_math.cpp
    test_core.cpp
    test_platform.cpp
    test_assets.cpp
    test_camera.cpp
    test_physics.cpp
    test_vehicle.cpp
    test_carsys.cpp
    test_player.cpp
    test_interact.cpp
    test_audio.cpp
)

add_executable(anomalous_tests ${ANOMALOUS_TEST_SOURCES})

target_link_libraries(anomalous_tests PRIVATE anomalous_core)

target_include_directories(anomalous_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

set_target_properties(anomalous_tests PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(NAME anomalous_tests
         COMMAND anomalous_tests
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
