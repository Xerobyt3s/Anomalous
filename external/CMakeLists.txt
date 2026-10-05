add_library(anomalous_headers INTERFACE)
target_include_directories(anomalous_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_library(anomalous_external INTERFACE)

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_library(anomalous_graphics INTERFACE)
target_link_libraries(anomalous_graphics INTERFACE glfw glad::glad glm::glm imgui::imgui)

set(PHYSICS_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/JoltPhysics")
set(CPP_EXCEPTIONS_ENABLED ON)
set(CPP_RTTI_ENABLED OFF)
set(DOUBLE_PRECISION OFF)
set(ENABLE_OBJECT_STREAM OFF)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF)
set(BUILD_SHARED_LIBS OFF)
include("${PHYSICS_REPO_ROOT}/Jolt/Jolt.cmake")
set_target_properties(Jolt PROPERTIES FOLDER "external"
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${PHYSICS_REPO_ROOT}")
if(MSVC)
    target_compile_options(Jolt PRIVATE /w /MP /fp:fast)
endif()

target_link_libraries(anomalous_external INTERFACE anomalous_headers anomalous_graphics Jolt)

if(WIN32)
    target_link_libraries(anomalous_external INTERFACE opengl32)
endif()
