# Third-party dependencies.
#
# All of these are vendored rather than fetched: this project builds offline, and the
# GLFW binary in particular has to stay pinned because its CRT linkage (/MD) drives the
# runtime choice for the whole build. See the note in the top-level CMakeLists.txt.

add_library(anomalous_external INTERFACE)

# --- GLFW (prebuilt static library, 3.4, /MD) -------------------------------

add_library(glfw STATIC IMPORTED GLOBAL)
set_target_properties(glfw PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/glfw/lib/glfw3.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/glfw/include"
)

# --- Header-only: miniaudio, stb_image, stb_truetype ------------------------
#
# These are single-header libraries whose implementations are compiled exactly once,
# in a dedicated translation unit, so the STB_*_IMPLEMENTATION defines never leak into
# game code. That TU is added in P2 when the asset system lands; until then only the
# include path is published.

target_include_directories(anomalous_external SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(anomalous_external INTERFACE glfw)

if(WIN32)
    target_link_libraries(anomalous_external INTERFACE opengl32)
endif()
