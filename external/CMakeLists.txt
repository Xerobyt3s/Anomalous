add_library(anomalous_headers INTERFACE)
target_include_directories(anomalous_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_library(anomalous_external INTERFACE)

add_library(glfw STATIC IMPORTED GLOBAL)
set_target_properties(glfw PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/glfw/lib/glfw3.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/glfw/include"
)

target_link_libraries(anomalous_external INTERFACE anomalous_headers glfw)

if(WIN32)
    target_link_libraries(anomalous_external INTERFACE opengl32)
endif()
