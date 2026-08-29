cmake_minimum_required(VERSION 3.21)

# CMP0091: MSVC runtime selected via CMAKE_MSVC_RUNTIME_LIBRARY, not baked into flags.
cmake_policy(SET CMP0091 NEW)

project(anomalous
    VERSION 0.1.0
    DESCRIPTION "Anomalous - C++ rework"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

# The vendored external/glfw/lib/glfw3.lib is a prebuilt static library carrying
# DEFAULTLIB:"MSVCRT" directives, i.e. it was compiled /MD. Selecting the dynamic
# release runtime in every configuration keeps our objects consistent with it.
#
# The old MSBuild project instead built Debug as /MDd and then force-ignored MSVCRT,
# which mixes two CRT instances in one image. This project manages its own memory
# through arenas and VirtualAlloc rather than the CRT heap, so the debug heap we give
# up here was buying us close to nothing.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# ---------------------------------------------------------------------------
# Shared compile settings
# ---------------------------------------------------------------------------

add_library(anomalous_options INTERFACE)

target_compile_features(anomalous_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(anomalous_options INTERFACE
        /W4                 # the old project's level, kept
        /WX                 # warnings are errors, as before
        /permissive-        # conformance mode
        /Zc:preprocessor    # conforming preprocessor
        /Zc:__cplusplus     # report the real __cplusplus value
        /utf-8              # source and execution charset
        /GR-                # no RTTI
        /EHsc               # see note below
        /MP                 # parallel compilation
    )
    # /EHsc is on so the MSVC standard library headers compile without C4530.
    # It is not an invitation to throw: this codebase does not throw and does not
    # catch, and no destructor is allowed to depend on unwinding. RTTI is genuinely
    # off via /GR-.

    target_compile_definitions(anomalous_options INTERFACE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
    )

    target_compile_options(anomalous_options INTERFACE
        $<$<CONFIG:Debug>:/Od;/Zi;/RTC1>
        $<$<CONFIG:RelWithDebInfo>:/O2;/Zi>
        $<$<CONFIG:Release>:/O2;/GL>
    )
    target_link_options(anomalous_options INTERFACE
        $<$<CONFIG:Debug>:/DEBUG>
        $<$<CONFIG:RelWithDebInfo>:/DEBUG>
        $<$<CONFIG:Release>:/LTCG>
    )
else()
    target_compile_options(anomalous_options INTERFACE
        -Wall -Wextra -Wpedantic -Werror
        -fno-rtti
        -fno-exceptions
    )
endif()

target_compile_definitions(anomalous_options INTERFACE
    $<$<CONFIG:Debug>:ANOMALOUS_DEBUG=1>
    $<$<NOT:$<CONFIG:Debug>>:ANOMALOUS_DEBUG=0>
)

# ---------------------------------------------------------------------------
# Third-party
# ---------------------------------------------------------------------------

add_subdirectory(external)

# ---------------------------------------------------------------------------
# anomalous_core -- every system, no entry point
# ---------------------------------------------------------------------------

# Explicitly listed rather than globbed: CMake cannot see a new file appear during an
# incremental build, and a stale glob is exactly the drift that rotted the old vcxproj.
set(ANOMALOUS_CORE_SOURCES
    src/core/version.cpp
    src/platform/platform_info.cpp
)

set(ANOMALOUS_CORE_HEADERS
    src/core/types.h
    src/core/version.h
    src/platform/platform_info.h
)

add_library(anomalous_core STATIC
    ${ANOMALOUS_CORE_SOURCES}
    ${ANOMALOUS_CORE_HEADERS}
)

target_include_directories(anomalous_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(anomalous_core
    PUBLIC anomalous_options
    PRIVATE anomalous_external
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES
    ${ANOMALOUS_CORE_SOURCES}
    ${ANOMALOUS_CORE_HEADERS})

# ---------------------------------------------------------------------------
# anomalous -- the game
# ---------------------------------------------------------------------------

add_executable(anomalous
    src/game/main.cpp
)

target_link_libraries(anomalous PRIVATE anomalous_core)

set_target_properties(anomalous PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

# ---------------------------------------------------------------------------
# anomalous_tests -- headless, no window, no GL context
# ---------------------------------------------------------------------------

enable_testing()
add_subdirectory(tests)
