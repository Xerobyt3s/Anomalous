#include "test.h"

#include "engine/physics/physics_world.h"
#include "world/pickup_body.h"

#include "carsys/items.h"
#include "core/arena.h"
#include "editor/editor.h"
#include "editor/editor_scene.h"
#include "editor/gizmo.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "player/interact.h"
#include "render/camera.h"
#include "world/entity.h"
#include "world/terrain.h"

using namespace anom;

namespace {
struct Bay {
    Arena arena{megabytes(32)};
    Heightfield hf;
    PhysWorld phys;
    ghost::engine::PhysicsWorld jolt;
    World world;
    Editor editor;

    Bay()
    {
        hf.alloc(arena, 64, 4.0f);
        hf.recompute_extents();
        phys.init(arena, &hf);
        phys.set_jolt(&jolt);
        world.init(arena);
        editor.init(arena);
    }

    EntityHandle add_tree(Vec3 pos)
    {
        const EntityHandle h = world.spawn(EntityKind::Tree, pos, quat_identity(), 1.0f,
                                           "tree_pine", kEntityFlagCollides);
        editor.push_create(world, phys, h);
        return h;
    }

    EntityHandle add_pickup(ItemKind kind, Vec3 pos)
    {
        Item item;
        item.kind = kind;
        item.condition = 0.5f;
        return interact_spawn_pickup(world, phys, item, pos, 0.0f, Vec3{});
    }
};

}

TEST(editor, undo_removes_a_created_entity_and_redo_brings_it_back)
{
    Bay bay;
    bay.add_tree(Vec3{4.0f, 0.0f, 6.0f});
    CHECK(bay.world.count() == 1);

    bay.editor.undo(bay.world, bay.phys);
    CHECK(bay.world.count() == 0);

    bay.editor.redo(bay.world, bay.phys);
    CHECK(bay.world.count() == 1);
}

TEST(editor, undo_restores_a_deleted_entity_with_its_fields)
{
    Bay bay;
    const EntityHandle h = bay.add_tree(Vec3{4.0f, 1.0f, 6.0f});
    Entity* e = bay.world.entity(h);
    CHECK(e != nullptr);
    e->scale = 1.75f;

    bay.editor.select(bay.world, h);
    bay.editor.delete_selection(bay.world, bay.phys);
    CHECK(bay.world.count() == 0);
    CHECK(!bay.editor.selection().valid());

    bay.editor.undo(bay.world, bay.phys);
    CHECK(bay.world.count() == 1);

    const Entity* back = bay.world.entities().at(bay.world.entities().live_indices()[0]);
    CHECK(back != nullptr);
    CHECK(back->kind == EntityKind::Tree);
    CHECK(back->mesh_name == "tree_pine");
    CHECK_NEAR(back->scale, 1.75f, 1e-5);
    CHECK_NEAR(back->pos.x, 4.0f, 1e-5);
    CHECK_NEAR(back->pos.z, 6.0f, 1e-5);
}

TEST(editor, a_respawned_entity_keeps_its_place_in_the_history)
{
    Bay bay;
    const EntityHandle h = bay.add_tree(Vec3{4.0f, 0.0f, 6.0f});
    bay.editor.select(bay.world, h);
    bay.editor.delete_selection(bay.world, bay.phys);

    bay.editor.undo(bay.world, bay.phys);
    CHECK(bay.world.count() == 1);

    bay.editor.redo(bay.world, bay.phys);
    CHECK(bay.world.count() == 0);

    bay.editor.undo(bay.world, bay.phys);
    CHECK(bay.world.count() == 1);
}

TEST(editor, a_new_op_truncates_the_redo_tail)
{
    Bay bay;
    bay.add_tree(Vec3{1.0f, 0.0f, 1.0f});
    bay.add_tree(Vec3{2.0f, 0.0f, 2.0f});
    CHECK(bay.editor.op_count() == 2);

    bay.editor.undo(bay.world, bay.phys);
    CHECK(bay.editor.op_cursor() == 1);
    CHECK(bay.editor.op_count() == 2);

    bay.add_tree(Vec3{3.0f, 0.0f, 3.0f});
    CHECK(bay.editor.op_count() == 2);
    CHECK(bay.editor.op_cursor() == 2);

    bay.editor.redo(bay.world, bay.phys);
    CHECK(bay.editor.op_cursor() == 2);
}

TEST(editor, the_history_drops_its_oldest_op_when_full)
{
    Bay bay;
    for (u32 i = 0; i < kEditorMaxOps + 8; i++) {
        bay.add_tree(Vec3{static_cast<f32>(i), 0.0f, 0.0f});
    }
    CHECK(bay.editor.op_count() == kEditorMaxOps);
    CHECK(bay.editor.op_cursor() == kEditorMaxOps);

    u32 undone = 0;
    while (bay.editor.op_cursor() > 0) {
        bay.editor.undo(bay.world, bay.phys);
        undone++;
    }
    CHECK(undone == kEditorMaxOps);
    CHECK(bay.world.count() == 8);
}

TEST(editor, undo_with_nothing_recorded_is_harmless)
{
    Bay bay;
    bay.editor.undo(bay.world, bay.phys);
    CHECK(bay.editor.status_text() == "nothing to undo");
    bay.editor.redo(bay.world, bay.phys);
    CHECK(bay.editor.status_text() == "nothing to redo");
    CHECK(bay.world.count() == 0);
}

TEST(editor, deleting_a_pickup_also_frees_its_body)
{
    Bay bay;
    const EntityHandle h = bay.add_pickup(ITEM_BATTERY, Vec3{3.0f, 1.0f, 3.0f});
    const Entity* e = bay.world.entity(h);
    CHECK(e != nullptr);
    CHECK(e->body != kNoEntityBody);
    CHECK(pickup_has_body(bay.phys, *e));
    const u32 body = e->body;

    bay.editor.select(bay.world, h);
    bay.editor.delete_selection(bay.world, bay.phys);
    CHECK(!bay.jolt.valid(body));

    bay.editor.undo(bay.world, bay.phys);
    CHECK(bay.world.count() == 1);
    const Entity* back = bay.world.entities().at(bay.world.entities().live_indices()[0]);
    CHECK(back != nullptr);
    CHECK(back->kind == EntityKind::PartPickup);
    CHECK(back->aux_kind == static_cast<u32>(ITEM_BATTERY));
    CHECK(pickup_has_body(bay.phys, *back));
}

TEST(editor, duplicating_offsets_the_copy_and_selects_it)
{
    Bay bay;
    const EntityHandle h = bay.add_tree(Vec3{4.0f, 0.0f, 6.0f});
    bay.editor.select(bay.world, h);
    bay.editor.duplicate(bay.world, bay.phys);

    CHECK(bay.world.count() == 2);
    CHECK(bay.editor.selection().valid());
    CHECK(bay.editor.selection() != h);

    const Entity* copy = bay.world.entity(bay.editor.selection());
    CHECK(copy != nullptr);
    CHECK_NEAR(copy->pos.x, 6.0f, 1e-5);
    CHECK_NEAR(copy->pos.z, 8.0f, 1e-5);

    bay.editor.undo(bay.world, bay.phys);
    CHECK(bay.world.count() == 1);
}

TEST(editor, a_snapshot_survives_a_toggle_round_trip)
{
    Bay bay;
    bay.world.spawn(EntityKind::Tree, Vec3{1.0f, 0.0f, 2.0f}, quat_identity(), 1.0f, "pine",
                    kEntityFlagCollides);
    bay.world.spawn(EntityKind::Building, Vec3{5.0f, 0.0f, 7.0f}, quat_identity(), 2.0f,
                    "garage", kEntityFlagCollides);
    bay.editor.snapshot_world(bay.world, bay.phys);
    CHECK(bay.editor.snapshot_count() == 2);

    bay.editor.toggle(bay.world, bay.phys);
    CHECK(bay.editor.active());
    CHECK(bay.world.count() == 2);

    bay.add_tree(Vec3{9.0f, 0.0f, 9.0f});
    CHECK(bay.world.count() == 3);

    bay.editor.toggle(bay.world, bay.phys);
    CHECK(!bay.editor.active());
    CHECK(bay.editor.snapshot_count() == 3);

    bay.editor.toggle(bay.world, bay.phys);
    CHECK(bay.world.count() == 3);
    CHECK(bay.editor.op_count() == 0);
}

TEST(editor, entity_records_compare_by_value)
{
    Bay bay;
    const EntityHandle h = bay.add_tree(Vec3{4.0f, 0.0f, 6.0f});

    EntityRecord a;
    EntityRecord b;
    editor_record_entity(bay.world, bay.phys, h, a);
    editor_record_entity(bay.world, bay.phys, h, b);
    CHECK(a == b);

    bay.world.entity(h)->scale = 2.0f;
    editor_record_entity(bay.world, bay.phys, h, b);
    CHECK(a != b);
}

TEST(editor, picking_returns_the_nearest_entity_under_the_cursor)
{
    Bay bay;
    const EntityHandle near_h = bay.world.spawn(EntityKind::Trigger, Vec3{0.0f, 0.0f, -10.0f},
                                                quat_identity(), 1.0f, "near",
                                                kEntityFlagInteractable);
    bay.world.entity(near_h)->half = Vec3{2.0f, 2.0f, 2.0f};

    const EntityHandle far_h = bay.world.spawn(EntityKind::Trigger, Vec3{0.0f, 0.0f, -30.0f},
                                               quat_identity(), 1.0f, "far",
                                               kEntityFlagInteractable);
    bay.world.entity(far_h)->half = Vec3{2.0f, 2.0f, 2.0f};

    Camera cam;
    cam.pos = Vec3{0.0f, 0.0f, 0.0f};
    cam.yaw = 0.0f;
    cam.pitch = 0.0f;

    const Vec2 viewport{800.0f, 600.0f};
    const EntityHandle hit = bay.editor.pick(cam, bay.world, bay.arena,
                                             Vec2{400.0f, 300.0f}, viewport);
    CHECK(hit == near_h);
}

TEST(editor, picking_empty_space_selects_nothing)
{
    Bay bay;
    const EntityHandle h = bay.world.spawn(EntityKind::Trigger, Vec3{0.0f, 0.0f, -10.0f},
                                           quat_identity(), 1.0f, "box",
                                           kEntityFlagInteractable);
    bay.world.entity(h)->half = Vec3{1.0f, 1.0f, 1.0f};

    Camera cam;
    cam.pos = Vec3{0.0f, 0.0f, 0.0f};

    const EntityHandle hit = bay.editor.pick(cam, bay.world, bay.arena, Vec2{10.0f, 10.0f},
                                             Vec2{800.0f, 600.0f});
    CHECK(!hit.valid());
}

TEST(camera, projection_inverts_the_mouse_ray)
{
    Camera cam;
    cam.pos = Vec3{3.0f, 2.0f, -4.0f};
    cam.yaw = 0.7f;
    cam.pitch = -0.2f;

    const Vec2 viewport{1280.0f, 720.0f};
    const Vec2 mouse{940.0f, 210.0f};
    const Ray ray = cam.mouse_ray(mouse, viewport);
    const Vec3 world = ray.origin + ray.dir * 25.0f;

    Vec2 screen;
    CHECK(cam.project_to_screen(world, viewport, screen));
    CHECK_NEAR(screen.x, mouse.x, 1e-2);
    CHECK_NEAR(screen.y, mouse.y, 1e-2);
}

TEST(camera, a_point_behind_the_camera_does_not_project)
{
    Camera cam;
    cam.pos = Vec3{0.0f, 0.0f, 0.0f};
    cam.yaw = 0.0f;
    cam.pitch = 0.0f;

    Vec2 screen;
    CHECK(!cam.project_to_screen(Vec3{0.0f, 0.0f, 10.0f}, Vec2{800.0f, 600.0f}, screen));
}

TEST(gizmo, snapping_rounds_to_the_nearest_step)
{
    CHECK_NEAR(Gizmo::snap_to(1.24f, 0.5f), 1.0f, 1e-5);
    CHECK_NEAR(Gizmo::snap_to(1.26f, 0.5f), 1.5f, 1e-5);
    CHECK_NEAR(Gizmo::snap_to(-1.26f, 0.5f), -1.5f, 1e-5);
    CHECK_NEAR(Gizmo::snap_to(7.3f, 0.0f), 7.3f, 1e-5);
}

TEST(gizmo, axis_directions_follow_the_basis)
{
    Gizmo g;
    g.init();
    CHECK_NEAR(g.axis_dir(0).x, 1.0f, 1e-5);
    CHECK_NEAR(g.axis_dir(1).y, 1.0f, 1e-5);
    CHECK_NEAR(g.axis_dir(2).z, 1.0f, 1e-5);

    g.set_basis(quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, kPi * 0.5f));
    CHECK_NEAR(g.axis_dir(0).z, -1.0f, 1e-4);
    CHECK_NEAR(g.axis_dir(1).y, 1.0f, 1e-5);
}

TEST(editor_scene, the_mesh_palette_lists_source_meshes_without_collision_hulls)
{
    EditorScene scene;
    scene.reset();
    CHECK(scene.meshes().size() > 0);
    for (const FixedString<32>& name : scene.meshes()) {
        CHECK(name.view().find(".amsh") == std::string_view::npos);
        CHECK(name.view().find("_col") == std::string_view::npos);
    }
}

TEST(editor_scene, selecting_a_palette_entry_twice_clears_it)
{
    EditorScene scene;
    scene.reset();
    CHECK(scene.place_mesh() == -1);

    scene.set_place_mesh(2);
    CHECK(scene.place_mesh() == 2);
    scene.set_place_mesh(2);
    CHECK(scene.place_mesh() == -1);

    scene.set_place_item(static_cast<i32>(ITEM_BATTERY));
    CHECK(scene.place_item() == static_cast<i32>(ITEM_BATTERY));
    scene.set_place_mesh(1);
    CHECK(scene.place_item() == -1);
    CHECK(scene.place_mesh() == 1);
}

TEST(editor_scene, the_spawn_kind_cycles_through_three_values)
{
    EditorScene scene;
    scene.reset();
    CHECK(scene.spawn_kind() == SpawnKind::Static);
    scene.cycle_spawn_kind();
    CHECK(scene.spawn_kind() == SpawnKind::Tree);
    scene.cycle_spawn_kind();
    CHECK(scene.spawn_kind() == SpawnKind::Building);
    scene.cycle_spawn_kind();
    CHECK(scene.spawn_kind() == SpawnKind::Static);
}

TEST(editor_scene, adding_a_trigger_puts_it_on_the_ground_ahead)
{
    Bay bay;
    Terrain terrain;
    terrain.heightfield().alloc(bay.arena, 32, 4.0f);
    terrain.heightfield().recompute_extents();

    Camera cam;
    cam.pos = Vec3{0.0f, 5.0f, 0.0f};
    cam.yaw = 0.0f;
    cam.pitch = 0.0f;

    EditorScene scene;
    scene.reset();
    const EntityHandle h = scene.add_trigger(bay.editor, cam, bay.world, bay.phys, terrain);

    const Entity* e = bay.world.entity(h);
    CHECK(e != nullptr);
    CHECK(e->kind == EntityKind::Trigger);
    CHECK(e->mesh_name == "trigger");
    CHECK_NEAR(e->pos.y, 1.0f, 1e-4);
    CHECK_NEAR(e->pos.z, -8.0f, 1e-3);
    CHECK(bay.editor.selection() == h);
    CHECK(bay.editor.op_count() == 1);

    bay.editor.undo(bay.world, bay.phys);
    CHECK(bay.world.count() == 0);
}
