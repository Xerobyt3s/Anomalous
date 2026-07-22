#include "editor/editor.h"
#include "editor/editor_brush.h"
#include "editor/gizmo.h"
#include "world/world.h"
#include "world/zone.h"
#include "world/terrain.h"
#include "physics/physics.h"
#include "physics/heightfield.h"
#include "player/interact.h"
#include "carsys/items.h"
#include "assets/assets.h"
#include "render/camera.h"
#include "render/render.h"
#include "render/debug_draw.h"
#include "platform/platform.h"
#include "core/arena.h"
#include "core/log.h"
#include "ui/ui.h"

#include <stdio.h>
#include <string.h>

#define EDITOR_PANEL_X 16.0f
#define EDITOR_PANEL_Y 16.0f
#define EDITOR_PANEL_W 260.0f

void editor_status_msg(EditorState* ed, const char* msg)
{
    snprintf(ed->status, sizeof(ed->status), "%s", msg);
    ed->status_time = 3.0f;
}

static Aabb editor_entity_world_aabb(const Entity* e)
{
    if (e->mesh) {
        Mat4 model = mat4_trs(e->pos, e->rot, v3(e->scale, e->scale, e->scale));
        return aabb_transform(model, e->mesh->bounds);
    }
    Aabb local;
    local.min = vec3_negate(e->half);
    local.max = e->half;
    return aabb_transform(mat4_trs(e->pos, e->rot, v3(1.0f, 1.0f, 1.0f)), local);
}

void editor_record_entity(struct World* world, struct PhysWorld* phys, Handle handle,
                          EntityRecord* out)
{
    memset(out, 0, sizeof(*out));
    Entity* e = world_entity(world, handle);
    if (!e) {
        return;
    }
    out->kind = e->kind;
    out->flags = e->flags;
    out->pos = e->pos;
    out->rot = e->rot;
    out->scale = e->scale;
    out->half = e->half;
    memcpy(out->mesh_name, e->mesh_name, sizeof(out->mesh_name));
    out->aux_kind = e->aux_kind;
    out->aux_value = e->aux_value;
    out->aux_data = e->aux_data;
    out->is_pickup = e->kind == ENTITY_PART_PICKUP;
    RigidBody* body = handle_valid(e->body) ? phys_body(phys, e->body) : 0;
    out->body_pos = body ? body->pos : e->pos;
    out->body_yaw = body ? quat_yaw(body->rot) : quat_yaw(e->rot);
}

static b32 editor_record_differs(const EntityRecord* a, const EntityRecord* b)
{
    return memcmp(a, b, sizeof(*a)) != 0;
}

static void editor_apply_record(struct World* world, struct PhysWorld* phys, Handle handle,
                                const EntityRecord* rec)
{
    Entity* e = world_entity(world, handle);
    if (!e) {
        return;
    }
    e->pos = rec->pos;
    e->rot = rec->rot;
    e->scale = rec->scale;
    e->half = rec->half;
    e->aux_kind = rec->aux_kind;
    e->aux_value = rec->aux_value;
    e->aux_data = rec->aux_data;
    memcpy(e->mesh_name, rec->mesh_name, sizeof(e->mesh_name));
    RigidBody* body = handle_valid(e->body) ? phys_body(phys, e->body) : 0;
    if (body) {
        Quat rot = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), rec->body_yaw);
        body->pos = rec->body_pos;
        body->prev_pos = rec->body_pos;
        body->rot = rot;
        body->prev_rot = rot;
        body->vel = vec3_zero();
        body->angular_vel = vec3_zero();
        body->asleep = 0;
        body->sleep_timer = 0.0f;
    }
}

static Handle editor_respawn(struct World* world, struct PhysWorld* phys,
                             const EntityRecord* rec)
{
    if (rec->is_pickup) {
        Item item;
        item.kind = (ItemKind)rec->aux_kind;
        item.condition = rec->aux_value;
        item.aux = (i32)rec->aux_data;
        return interact_spawn_pickup(world, phys, item, rec->body_pos, rec->body_yaw,
                                     vec3_zero());
    }
    Handle handle = world_spawn(world, rec->kind, rec->pos, rec->rot, rec->scale,
                                rec->mesh_name[0] ? rec->mesh_name : 0, rec->flags);
    Entity* e = world_entity(world, handle);
    if (e) {
        e->half = rec->half;
        e->aux_kind = rec->aux_kind;
        e->aux_value = rec->aux_value;
        e->aux_data = rec->aux_data;
        memcpy(e->mesh_name, rec->mesh_name, sizeof(e->mesh_name));
    }
    return handle;
}

static void editor_destroy_entity(struct World* world, struct PhysWorld* phys, Handle handle)
{
    Entity* e = world_entity(world, handle);
    if (!e) {
        return;
    }
    if (handle_valid(e->body)) {
        phys_body_destroy(phys, e->body);
    }
    world_despawn(world, handle);
}

static b32 editor_handle_eq(Handle a, Handle b)
{
    return a.idx == b.idx && a.gen == b.gen;
}

static void editor_patch_handle(EditorState* ed, Handle old_h, Handle new_h)
{
    for (u32 i = 0; i < ed->op_count; i++) {
        if (editor_handle_eq(ed->ops[i].handle, old_h)) {
            ed->ops[i].handle = new_h;
        }
    }
    if (editor_handle_eq(ed->selection, old_h)) {
        ed->selection = new_h;
    }
}

static void editor_push_op(EditorState* ed, const EditorOp* op)
{
    ed->op_count = ed->op_cursor;
    if (ed->op_count >= EDITOR_MAX_OPS) {
        memmove(ed->ops, ed->ops + 1, (EDITOR_MAX_OPS - 1) * sizeof(EditorOp));
        ed->op_count--;
        ed->op_cursor--;
    }
    ed->ops[ed->op_count++] = *op;
    ed->op_cursor = ed->op_count;
}

void editor_push_create(EditorState* ed, struct World* world, struct PhysWorld* phys,
                        Handle handle)
{
    EditorOp op;
    memset(&op, 0, sizeof(op));
    op.kind = EDITOR_OP_CREATE;
    op.handle = handle;
    editor_record_entity(world, phys, handle, &op.after);
    editor_push_op(ed, &op);
    ed->dirty = 1;
}

static void editor_undo(EditorState* ed, struct World* world, struct PhysWorld* phys)
{
    if (ed->op_cursor == 0) {
        editor_status_msg(ed, "nothing to undo");
        return;
    }
    EditorOp* op = &ed->ops[--ed->op_cursor];
    if (op->kind == EDITOR_OP_TRANSFORM) {
        editor_apply_record(world, phys, op->handle, &op->before);
    } else if (op->kind == EDITOR_OP_CREATE) {
        editor_destroy_entity(world, phys, op->handle);
        if (editor_handle_eq(ed->selection, op->handle)) {
            ed->selection = HANDLE_INVALID;
        }
    } else {
        Handle new_h = editor_respawn(world, phys, &op->before);
        editor_patch_handle(ed, op->handle, new_h);
        op->handle = new_h;
    }
    ed->dirty = 1;
    editor_status_msg(ed, "undo");
}

static void editor_redo(EditorState* ed, struct World* world, struct PhysWorld* phys)
{
    if (ed->op_cursor >= ed->op_count) {
        editor_status_msg(ed, "nothing to redo");
        return;
    }
    EditorOp* op = &ed->ops[ed->op_cursor++];
    if (op->kind == EDITOR_OP_TRANSFORM) {
        editor_apply_record(world, phys, op->handle, &op->after);
    } else if (op->kind == EDITOR_OP_CREATE) {
        Handle new_h = editor_respawn(world, phys, &op->after);
        editor_patch_handle(ed, op->handle, new_h);
        op->handle = new_h;
    } else {
        editor_destroy_entity(world, phys, op->handle);
        if (editor_handle_eq(ed->selection, op->handle)) {
            ed->selection = HANDLE_INVALID;
        }
    }
    ed->dirty = 1;
    editor_status_msg(ed, "redo");
}

static EntityRecord s_world_snapshot[WORLD_MAX_ENTITIES];
static u32 s_world_snapshot_count;

void editor_snapshot_world(struct World* world, struct PhysWorld* phys)
{
    s_world_snapshot_count = 0;
    for (u32 idx = 0; idx < world->entities.capacity; idx++) {
        if (!pool_at(&world->entities, idx)) {
            continue;
        }
        Handle handle = { idx, world->entities.gens[idx] };
        editor_record_entity(world, phys, handle,
                             &s_world_snapshot[s_world_snapshot_count++]);
    }
}

static void editor_restore_snapshot(struct World* world, struct PhysWorld* phys)
{
    for (u32 idx = 0; idx < world->entities.capacity; idx++) {
        Entity* e = pool_at(&world->entities, idx);
        if (!e) {
            continue;
        }
        Handle handle = { idx, world->entities.gens[idx] };
        editor_destroy_entity(world, phys, handle);
    }
    for (u32 i = 0; i < s_world_snapshot_count; i++) {
        editor_respawn(world, phys, &s_world_snapshot[i]);
    }
}

void editor_init(EditorState* ed)
{
    memset(ed, 0, sizeof(*ed));
    gizmo_init(&ed->gizmo);
    ed->selection = HANDLE_INVALID;
    ed->grid_on = 1;
    ed->box_sel = -1;
    editor_scene_reset();
}

void editor_toggle(EditorState* ed, struct World* world, struct PhysWorld* phys)
{
    ed->active = !ed->active;
    if (ed->active) {
        editor_scene_reset();
        editor_restore_snapshot(world, phys);
        ed->selection = HANDLE_INVALID;
        ed->op_count = 0;
        ed->op_cursor = 0;
        ed->drag_was = 0;
    } else {
        editor_snapshot_world(world, phys);
        platform_set_cursor_captured(0);
        ed->gizmo.dragging = 0;
    }
}

static f32 editor_pick_mesh_t(const Entity* e, Ray ray, f32 max_t)
{
    Quat inv = quat_conjugate(e->rot);
    f32 inv_s = 1.0f / f_max(e->scale, 1e-4f);
    Ray local;
    local.origin = vec3_scale(quat_rotate_vec3(inv, vec3_sub(ray.origin, e->pos)), inv_s);
    local.dir = quat_rotate_vec3(inv, ray.dir);

    char path[256];
    snprintf(path, sizeof(path), "assets/meshes/%s.amsh", e->mesh_name);
    ArenaTemp temp = arena_temp_begin(&g_frame_arena);
    MeshData data = {0};
    f32 best = -1.0f;
    if (assets_load_mesh_data(&g_frame_arena, path, &data)) {
        f32 local_max = max_t * inv_s;
        for (u32 i = 0; i + 2 < data.index_count; i += 3) {
            Vec3 a = v3(data.vertices[data.indices[i]].pos[0],
                        data.vertices[data.indices[i]].pos[1],
                        data.vertices[data.indices[i]].pos[2]);
            Vec3 b = v3(data.vertices[data.indices[i + 1]].pos[0],
                        data.vertices[data.indices[i + 1]].pos[1],
                        data.vertices[data.indices[i + 1]].pos[2]);
            Vec3 c = v3(data.vertices[data.indices[i + 2]].pos[0],
                        data.vertices[data.indices[i + 2]].pos[1],
                        data.vertices[data.indices[i + 2]].pos[2]);
            RayHitTri hit;
            if (ray_vs_triangle(local, a, b, c, local_max, &hit)) {
                if (best < 0.0f || hit.t < best) {
                    best = hit.t;
                }
            }
        }
        if (best >= 0.0f) {
            best *= e->scale;
        }
    }
    arena_temp_end(temp);
    return best;
}

static Handle editor_pick(const struct GameInput* input, const Camera* cam, World* world)
{
    Ray ray = camera_mouse_ray(cam, input->mouse_x, input->mouse_y, r_viewport_size());
    Handle best = HANDLE_INVALID;
    f32 best_t = 1e30f;
    for (u32 idx = 0; idx < world->entities.capacity; idx++) {
        Entity* e = pool_at(&world->entities, idx);
        if (!e || (!e->mesh && e->kind != ENTITY_TRIGGER)) {
            continue;
        }
        Aabb box = editor_entity_world_aabb(e);
        f32 t;
        if (!ray_vs_aabb(ray, box, best_t, &t)) {
            continue;
        }
        if (e->mesh && e->mesh_name[0]) {
            f32 exact = editor_pick_mesh_t(e, ray, best_t);
            if (exact < 0.0f) {
                continue;
            }
            t = exact;
        }
        if (t < best_t) {
            best_t = t;
            best.idx = idx;
            best.gen = world->entities.gens[idx];
        }
    }
    return best;
}

void editor_select_entity(EditorState* ed, struct World* world, Handle handle)
{
    ed->selection = handle;
    ed->box_sel = -1;
    Entity* e = world_entity(world, handle);
    if (e) {
        ed->sel_yaw = quat_yaw(e->rot);
    }
}

static void editor_duplicate(EditorState* ed, World* world, PhysWorld* phys)
{
    Entity* e = world_entity(world, ed->selection);
    if (!e) {
        return;
    }
    EntityRecord rec;
    editor_record_entity(world, phys, ed->selection, &rec);
    rec.pos = vec3_add(rec.pos, v3(2.0f, 0.0f, 2.0f));
    rec.body_pos = vec3_add(rec.body_pos, v3(2.0f, 0.0f, 2.0f));
    Handle copy = editor_respawn(world, phys, &rec);
    editor_select_entity(ed, world, copy);
    editor_push_create(ed, world, phys, copy);
    editor_status_msg(ed, "duplicated entity");
}

void editor_update(EditorState* ed, const struct GameInput* input, struct Camera* cam,
                   struct World* world, struct PhysWorld* phys, struct Terrain* terrain,
                   struct Vehicle* veh, const char* zone_dir, f32 dt)
{
    if (!ed->active) {
        return;
    }
    if (ed->status_time > 0.0f) {
        ed->status_time -= dt;
    }

    b32 typing = ui_text_active();
    if (typing || ed->brush_mode) {
        platform_set_cursor_captured(0);
    } else {
        camera_fly_update(cam, input, dt);
    }

    if (!typing && !ed->brush_mode) {
        if (input->key_pressed[KEY_1]) {
            ed->gizmo.mode = GIZMO_TRANSLATE;
        }
        if (input->key_pressed[KEY_2]) {
            ed->gizmo.mode = GIZMO_ROTATE;
        }
        if (input->key_pressed[KEY_3]) {
            ed->gizmo.mode = GIZMO_SCALE;
        }
        if (input->key_pressed[KEY_G]) {
            ed->grid_on = !ed->grid_on;
        }
        if (!ed->brush_mode && input->key_down[KEY_LEFT_CONTROL] && input->key_pressed[KEY_Z]) {
            editor_undo(ed, world, phys);
        }
        if (!ed->brush_mode && input->key_down[KEY_LEFT_CONTROL] && input->key_pressed[KEY_Y]) {
            editor_redo(ed, world, phys);
        }
    }

    if (ed->brush_mode) {
        editor_brush_update(ed, input, cam, terrain);
        return;
    }

    b32 over_panel = ui_mouse_over_panel(input);
    Vec2 viewport = r_viewport_size();

    b32 scene_consumed = editor_scene_update(ed, input, cam, world, phys, terrain, veh);

    b32 gizmo_consumed = 0;
    Entity* sel = world_entity(world, ed->selection);
    if (sel && !scene_consumed) {
        RigidBody* sel_body = handle_valid(sel->body) ? phys_body(phys, sel->body) : 0;
        b32 no_scale = sel_body || sel->kind == ENTITY_TRIGGER;
        Vec3 gpos = sel_body ? sel_body->pos : sel->pos;
        f32 prev_yaw = ed->sel_yaw;
        Vec3 prev_pos = gpos;
        f32 prev_scale = sel->scale;

        b32 drag_was = ed->gizmo.dragging;
        if (!ed->gizmo.dragging) {
            ed->gizmo.basis = quat_identity();
        }
        if (!over_panel || ed->gizmo.dragging) {
            gizmo_consumed = gizmo_update(&ed->gizmo, input, cam, viewport, &gpos, &ed->sel_yaw,
                                          no_scale ? 0 : &sel->scale, ed->snap_pos, ed->snap_ang,
                                          ed->snap_scale);
        }
        if (ed->gizmo.dragging && !drag_was) {
            editor_record_entity(world, phys, ed->selection, &ed->drag_before);
            ed->drag_was = 1;
        }

        b32 moved = prev_pos.x != gpos.x || prev_pos.y != gpos.y || prev_pos.z != gpos.z;
        b32 turned = prev_yaw != ed->sel_yaw;
        if (sel_body) {
            if (moved || turned) {
                Quat rot = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), ed->sel_yaw);
                sel_body->pos = gpos;
                sel_body->prev_pos = gpos;
                sel_body->rot = rot;
                sel_body->prev_rot = rot;
                sel_body->vel = vec3_zero();
                sel_body->angular_vel = vec3_zero();
                sel_body->asleep = 0;
                sel_body->sleep_timer = 0.0f;
                if (sel->kind == ENTITY_PART_PICKUP) {
                    ItemKind kind = (ItemKind)sel->aux_kind;
                    sel->rot = quat_mul(rot, item_cargo_rot(kind));
                    sel->pos = vec3_sub(gpos, quat_rotate_vec3(sel->rot, item_mesh_center(kind)));
                }
            }
        } else {
            if (moved) {
                sel->pos = gpos;
            }
            if (turned) {
                sel->rot = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), ed->sel_yaw);
            }
        }
        if (moved || turned || prev_scale != sel->scale) {
            ed->dirty = 1;
        }

        if (!ed->gizmo.dragging && ed->drag_was) {
            ed->drag_was = 0;
            EntityRecord after;
            editor_record_entity(world, phys, ed->selection, &after);
            if (editor_record_differs(&ed->drag_before, &after)) {
                EditorOp op;
                memset(&op, 0, sizeof(op));
                op.kind = EDITOR_OP_TRANSFORM;
                op.handle = ed->selection;
                op.before = ed->drag_before;
                op.after = after;
                editor_push_op(ed, &op);
            }
        }
    } else if (!sel) {
        ed->drag_was = 0;
    }

    if (!scene_consumed && !gizmo_consumed && !over_panel && input->mouse_pressed[MOUSE_LEFT]
        && !input->mouse_down[MOUSE_RIGHT]) {
        Handle hit = editor_pick(input, cam, world);
        if (handle_valid(hit)) {
            editor_select_entity(ed, world, hit);
        } else {
            ed->selection = HANDLE_INVALID;
        }
    }

    if (!typing && input->key_pressed[KEY_DELETE] && world_entity(world, ed->selection)) {
        EditorOp op;
        memset(&op, 0, sizeof(op));
        op.kind = EDITOR_OP_DELETE;
        op.handle = ed->selection;
        editor_record_entity(world, phys, ed->selection, &op.before);
        editor_destroy_entity(world, phys, ed->selection);
        editor_push_op(ed, &op);
        ed->selection = HANDLE_INVALID;
        ed->dirty = 1;
        editor_status_msg(ed, "deleted entity");
    }
    if (!typing && input->key_down[KEY_LEFT_CONTROL] && input->key_pressed[KEY_D]) {
        editor_duplicate(ed, world, phys);
    }
    if (!typing && input->key_down[KEY_LEFT_CONTROL] && input->key_pressed[KEY_S]) {
        ed->want_save = 1;
    }

    if (ed->want_save) {
        ed->want_save = 0;
        if (zone_save(zone_dir, world, phys, terrain)) {
            ed->dirty = 0;
            editor_status_msg(ed, "saved zone.cfg");
        } else {
            editor_status_msg(ed, "save failed");
        }
    }
    if (ed->want_reload) {
        ed->want_reload = 0;
        ed->selection = HANDLE_INVALID;
        ed->op_count = 0;
        ed->op_cursor = 0;
        ZonePickups pickups;
        if (zone_reload(zone_dir, &g_perm_arena, world, phys, terrain, &pickups)) {
            interact_spawn_zone_pickups(world, phys, terrain, &pickups);
            editor_snapshot_world(world, phys);
            ed->dirty = 0;
            editor_status_msg(ed, "reloaded zone.cfg");
        } else {
            editor_status_msg(ed, "reload failed");
        }
    }
}

static const char* editor_mode_name(GizmoMode mode)
{
    if (mode == GIZMO_TRANSLATE) {
        return "translate";
    }
    if (mode == GIZMO_ROTATE) {
        return "rotate";
    }
    return "scale";
}

void editor_render(EditorState* ed, const struct GameInput* input, const struct Camera* cam,
                   struct World* world, struct PhysWorld* phys, struct Terrain* terrain,
                   struct Vehicle* veh)
{
    if (!ed->active) {
        return;
    }

    if (ed->brush_mode) {
        editor_brush_render(ed, input, cam, terrain);
        return;
    }

    if (ed->grid_on) {
        f32 step = ed->snap_pos > 0.0f ? ed->snap_pos : 1.0f;
        f32 gx = floorf(cam->pos.x / step + 0.5f) * step;
        f32 gz = floorf(cam->pos.z / step + 0.5f) * step;
        f32 gy = heightfield_sample(&terrain->hf, gx, gz);
        dd_grid(v3(gx, gy, gz), 40.0f, step, dd_rgba(70, 80, 95, 120));
    }

    for (u32 idx = 0; idx < world->entities.capacity; idx++) {
        Entity* e = pool_at(&world->entities, idx);
        if (!e || e->kind != ENTITY_TRIGGER) {
            continue;
        }
        b32 is_sel = ed->selection.idx == idx && ed->selection.gen == world->entities.gens[idx];
        dd_obb(e->pos, e->rot, e->half, is_sel ? DD_YELLOW : DD_CYAN);
        dd_text_3d(vec3_add(e->pos, v3(0.0f, e->half.y + 0.3f, 0.0f)), 13.0f, DD_CYAN, "%s",
                   e->mesh_name);
    }

    Entity* sel = world_entity(world, ed->selection);
    if (sel) {
        RigidBody* sel_body = handle_valid(sel->body) ? phys_body(phys, sel->body) : 0;
        if (sel->mesh) {
            Aabb local = sel->mesh->bounds;
            Vec3 c = vec3_scale(vec3_add(local.min, local.max), 0.5f);
            Vec3 half = vec3_scale(vec3_sub(local.max, local.min), 0.5f * sel->scale);
            Vec3 center = vec3_add(sel->pos, quat_rotate_vec3(sel->rot, vec3_scale(c, sel->scale)));
            dd_obb(center, sel->rot, half, DD_YELLOW);
        }
        gizmo_render(&ed->gizmo, cam, sel_body ? sel_body->pos : sel->pos);
    }

    ui_panel_begin("editor [f8]", EDITOR_PANEL_X, EDITOR_PANEL_Y, EDITOR_PANEL_W);
    ui_label("mode: %s  (1/2/3)", editor_mode_name(ed->gizmo.mode));
    ui_checkbox("grid (g)", &ed->grid_on);
    ui_slider_f32("snap pos", &ed->snap_pos, 0.0f, 5.0f);
    ui_slider_f32("snap deg", &ed->snap_ang, 0.0f, 90.0f);
    ui_slider_f32("snap scale", &ed->snap_scale, 0.0f, 1.0f);

    if (sel) {
        ui_label("sel: %s", sel->mesh_name[0] ? sel->mesh_name : "(trigger)");
        ui_label("pos %.2f %.2f %.2f", (f64)sel->pos.x, (f64)sel->pos.y, (f64)sel->pos.z);
        ui_label("yaw %.1f  scale %.2f", (f64)(ed->sel_yaw * RAD_TO_DEG), (f64)sel->scale);
        if (ui_button("duplicate (ctrl+d)")) {
            editor_duplicate(ed, world, phys);
        }
        if (ui_button("delete (del)")) {
            EditorOp op;
            memset(&op, 0, sizeof(op));
            op.kind = EDITOR_OP_DELETE;
            op.handle = ed->selection;
            editor_record_entity(world, phys, ed->selection, &op.before);
            editor_destroy_entity(world, phys, ed->selection);
            editor_push_op(ed, &op);
            ed->selection = HANDLE_INVALID;
            ed->dirty = 1;
            editor_status_msg(ed, "deleted entity");
        }
    } else {
        ui_label("click an entity to select");
    }

    ui_label("undo %u | redo %u", ed->op_cursor, ed->op_count - ed->op_cursor);
    if (ui_button("undo (ctrl+z)")) {
        editor_undo(ed, world, phys);
    }
    if (ui_button("redo (ctrl+y)")) {
        editor_redo(ed, world, phys);
    }

    ui_label("entities: %u", world->entities.count);
    ui_label(ed->dirty ? "unsaved changes *" : "no changes");
    if (ui_button("save zone (ctrl+s)")) {
        ed->want_save = 1;
    }
    if (ui_button("reload zone")) {
        ed->want_reload = 1;
    }
    if (ui_button("structure editor")) {
        ed->brush_mode = 1;
        ed->brush_sel = -1;
        ed->selection = HANDLE_INVALID;
        ed->box_sel = -1;
        ed->gizmo.dragging = 0;
        editor_brush_reset();
    }
    if (ed->status_time > 0.0f) {
        ui_label("%s", ed->status);
    }
    ui_panel_end();

    editor_scene_render(ed, input, cam, world, phys, terrain, veh);
}
