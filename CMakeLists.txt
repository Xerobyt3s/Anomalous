cmake_minimum_required(VERSION 3.21)

cmake_policy(SET CMP0091 NEW)

project(anomalous
    VERSION 0.1.0
    DESCRIPTION "Anomalous"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

add_library(anomalous_options INTERFACE)

target_compile_features(anomalous_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(anomalous_options INTERFACE
        /W4
        /WX
        /permissive-
        /Zc:preprocessor
        /Zc:__cplusplus
        /utf-8
        /GR-
        /EHsc
        /MP
    )

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
        /STACK:8388608
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

add_subdirectory(external)

set(ANOMALOUS_CORE_SOURCES
    src/assets/image.cpp
    src/assets/mesh_data.cpp
    src/assets/watcher.cpp
    src/carsys/cables.cpp
    src/carsys/carsys.cpp
    src/carsys/carsys_render.cpp
    src/carsys/electrics.cpp
    src/carsys/fluids.cpp
    src/carsys/items.cpp
    src/carsys/parts.cpp
    src/core/arena.cpp
    src/editor/brush.cpp
    src/editor/editor.cpp
    src/editor/editor_brush.cpp
    src/editor/editor_scene.cpp
    src/editor/gizmo.cpp
    src/engine/assets/asset_path.cpp
    src/engine/debug/imgui_layer.cpp
    src/engine/render/mesh.cpp
    src/game/fx/draw_order.cpp
    src/game/fx/lightning.cpp
    src/engine/render/post_process.cpp
    src/engine/render/primitives.cpp
    src/engine/render/shader.cpp
    src/engine/render/texture.cpp
    src/core/config.cpp
    src/core/log.cpp
    src/core/version.cpp
    src/math/vmath.cpp
    src/physics/body.cpp
    src/audio/audio.cpp
    src/audio/miniaudio_impl.cpp
    src/audio/tapes.cpp
    src/player/interact.cpp
    src/player/movement.cpp
    src/player/player.cpp
    src/terminal/disk_image.cpp
    src/terminal/comms.cpp
    src/terminal/disks.cpp
    src/terminal/fs.cpp
    src/terminal/mapdata.cpp
    src/terminal/screen.cpp
    src/terminal/shell.cpp
    src/terminal/terminal.cpp
    src/terminal/programs/boot.cpp
    src/terminal/programs/breach.cpp
    src/terminal/programs/comms.cpp
    src/terminal/programs/link.cpp
    src/terminal/programs/map.cpp
    src/terminal/programs/status.cpp
    src/terminal/programs/tapes.cpp
    src/terminal/programs/travel.cpp
    src/terminal/term_font.cpp
    src/terminal/term_render.cpp
    src/terminal/virus.cpp
    src/physics/collide.cpp
    src/physics/gravity_field.cpp
    src/physics/heightfield.cpp
    src/physics/jolt_world.cpp
    src/physics/static_grid.cpp
    src/physics/world.cpp
    src/render/camera.cpp
    src/render/debug_draw.cpp
    src/render/fontchain.cpp
    src/render/text.cpp
    src/render/asset_cache.cpp
    src/render/device.cpp
    src/render/screenshot.cpp
    src/render/shader_set.cpp
    src/render/sky.cpp
    src/render/terrain_render.cpp
    src/render/tree.cpp
    src/ui/ui.cpp
    src/vehicle/drivetrain.cpp
    src/vehicle/tire.cpp
    src/vehicle/vehicle.cpp
    src/vehicle/vehicle_config.cpp
    src/world/destination.cpp
    src/world/entity.cpp
    src/world/islands/island_field.cpp
    src/world/islands/island_shape.cpp
    src/world/islands/sdf_noise.cpp
    src/world/islands/surface_nets.cpp
    src/world/terrain.cpp
    src/world/weather.cpp
    src/world/zone.cpp
    src/platform/clock.cpp
    src/platform/filesystem.cpp
    src/platform/gl_loader.cpp
    src/platform/input.cpp
    src/platform/input_context.cpp
    src/platform/platform_info.cpp
    src/platform/window.cpp
)

set(ANOMALOUS_CORE_HEADERS
    src/assets/amsh.h
    src/assets/image.h
    src/assets/mesh_data.h
    src/assets/watcher.h
    src/carsys/cables.h
    src/carsys/carsys.h
    src/carsys/carsys_render.h
    src/carsys/electrics.h
    src/carsys/fluids.h
    src/carsys/items.h
    src/carsys/parts.h
    src/core/arena.h
    src/editor/brush.h
    src/editor/editor.h
    src/editor/editor_brush.h
    src/editor/editor_scene.h
    src/editor/gizmo.h
    src/engine/assets/asset_path.h
    src/engine/assets/model.h
    src/engine/debug/imgui_layer.h
    src/engine/render/camera.h
    src/engine/render/gl_handle.h
    src/engine/render/mesh.h
    src/game/fx/draw_order.h
    src/game/fx/lightning.h
    src/engine/render/post_process.h
    src/engine/render/primitives.h
    src/engine/render/shader.h
    src/engine/render/texture.h
    src/engine/debug/log.h
    src/math/glm_bridge.h
    src/core/config.h
    src/core/fixed_string.h
    src/core/handle.h
    src/core/log.h
    src/core/pool.h
    src/core/rng.h
    src/core/types.h
    src/core/utf8.h
    src/core/version.h
    src/math/vmath.h
    src/physics/body.h
    src/audio/audio.h
    src/audio/ma.h
    src/audio/tapes.h
    src/player/interact.h
    src/player/movement.h
    src/player/player.h
    src/terminal/disk_image.h
    src/terminal/comms.h
    src/terminal/disks.h
    src/terminal/fs.h
    src/terminal/mapdata.h
    src/terminal/screen.h
    src/terminal/shell.h
    src/terminal/terminal.h
    src/terminal/program.h
    src/terminal/term_view.h
    src/terminal/programs/boot.h
    src/terminal/programs/breach.h
    src/terminal/programs/comms.h
    src/terminal/programs/link.h
    src/terminal/programs/map.h
    src/terminal/programs/status.h
    src/terminal/programs/tapes.h
    src/terminal/programs/travel.h
    src/terminal/term_font.h
    src/terminal/term_render.h
    src/terminal/virus.h
    src/physics/collide.h
    src/physics/gravity_field.h
    src/physics/heightfield.h
    src/physics/jolt_world.h
    src/physics/static_grid.h
    src/physics/world.h
    src/render/camera.h
    src/render/debug_draw.h
    src/render/fontchain.h
    src/render/text.h
    src/render/asset_cache.h
    src/render/device.h
    src/render/gpu_mesh.h
    src/render/screenshot.h
    src/render/shader_set.h
    src/render/sky.h
    src/render/terrain_render.h
    src/render/tree.h
    src/ui/ui.h
    src/vehicle/tire.h
    src/vehicle/vehicle.h
    src/vehicle/vehicle_config.h
    src/world/destination.h
    src/world/entity.h
    src/world/islands/island_field.h
    src/world/islands/island_shape.h
    src/world/islands/sdf_noise.h
    src/world/islands/surface_nets.h
    src/world/terrain.h
    src/world/weather.h
    src/world/zone.h
    src/platform/clock.h
    src/platform/filesystem.h
    src/platform/gl_loader.h
    src/platform/input.h
    src/platform/input_context.h
    src/platform/platform_info.h
    src/platform/window.h
)

add_library(anomalous_core STATIC
    ${ANOMALOUS_CORE_SOURCES}
    ${ANOMALOUS_CORE_HEADERS}
)

target_include_directories(anomalous_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(anomalous_core PUBLIC
    GHOST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

target_link_libraries(anomalous_core
    PUBLIC anomalous_options anomalous_headers anomalous_graphics
    PRIVATE anomalous_external
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES
    ${ANOMALOUS_CORE_SOURCES}
    ${ANOMALOUS_CORE_HEADERS})

add_executable(anomalous
    src/game/game.cpp
    src/game/main.cpp
)

target_link_libraries(anomalous PRIVATE anomalous_core)

set_target_properties(anomalous PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

enable_testing()
add_subdirectory(tests)
