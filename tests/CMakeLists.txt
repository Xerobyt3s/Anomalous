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
    test_gameplay.cpp
    test_net.cpp
    test_pickups.cpp
    test_players.cpp
    test_scene_switch.cpp
    test_synth.cpp
    test_seats.cpp
    test_sim.cpp
    test_zone.cpp
    test_editor.cpp
    test_ui.cpp
    test_brush.cpp
    test_input_context.cpp
    test_fs.cpp
    test_screen.cpp
    test_shell.cpp
    test_terminal.cpp
    test_scenes.cpp
    test_gravity.cpp
    test_islands.cpp
    test_render_core.cpp
    test_haze.cpp
    ghost_test_lightning.cpp
    ghost_test_ammo.cpp
    ghost_test_arena_map.cpp
    ghost_test_audio.cpp
    ghost_test_ballistics.cpp
    ghost_test_body_rig.cpp
    ghost_test_body_rig_hands.cpp
    ghost_test_breath.cpp
    ghost_test_elements_world.cpp
    ghost_test_fog_field.cpp
    ghost_test_game_loop.cpp
    ghost_test_ghost_types.cpp
    ghost_test_ghosts.cpp
    ghost_test_material_shapes.cpp
    ghost_test_mimic_body.cpp
    ghost_test_model.cpp
    ghost_test_necromite.cpp
    ghost_test_net.cpp
    ghost_test_player.cpp
    ghost_test_player_collision.cpp
    ghost_test_reveal.cpp
    ghost_test_revolver_mechanism.cpp
    ghost_test_roster.cpp
    ghost_test_shard_flock.cpp
    ghost_test_vortex_field.cpp
    ghost_test_gravity_fx.cpp
    ghost_test_body_rig_rebase.cpp
    ghost_test_ghost_gravity.cpp
    ghost_test_ghost_wind.cpp
    ghost_test_draw_order.cpp
)

add_executable(anomalous_tests ${ANOMALOUS_TEST_SOURCES})

target_link_libraries(anomalous_tests PRIVATE anomalous_core)

target_include_directories(anomalous_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/shim"
)

set_target_properties(anomalous_tests PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

add_test(NAME anomalous_tests
         COMMAND anomalous_tests
         WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
