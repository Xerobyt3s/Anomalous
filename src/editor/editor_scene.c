#include "editor/editor.h"
#include "editor/gizmo.h"
#include "world/world.h"
#include "world/terrain.h"
#include "physics/physics.h"
#include "physics/heightfield.h"
#include "player/interact.h"
#include "carsys/items.h"
#include "audio/tapes.h"
#include "terminal/disks.h"
#include "vehicle/vehicle.h"
#include "render/camera.h"
#include "render/render.h"
#include "render/debug_draw.h"
#include "platform/platform.h"
#include "ui/ui.h"

#include <stdio.h>
#include <string.h>

#define SCENE_MAX_MESHES 128
#define SCENE_PANEL_W 260.0f
#define SCENE_LIST_PAGE 12

static char s_meshes[SCENE_MAX_MESHES][32];
static u32 s_mesh_count;
static i32 s_mesh_scroll;
static i32 s_palette_tab;
static i32 s_place_mesh = -1;
static i32 s_place_item = -1;
static i32 s_spawn_kind;
static i32 s_outliner_scroll;
static b32 s_preview_valid;
static Vec3 s_preview_pos;
static Handle s_trigger_sync;
static char s_trigger_name[32];
static i32 s_tape_scroll;

void editor_scene_reset(void)
{
    PlatformDirEntry entries[SCENE_MAX_MESHES];
    u32 n = platform_list_dir("assets/meshes", entries, SCENE_MAX_MESHES);
    s_mesh_count = 0;
    for (u32 i = 0; i < n; i++) {
        if (entries[i].is_dir) {
            continue;
        }
        u64 len = strlen(entries[i].name);
        if (len < 6 || strcmp(entries[i].name + len - 5, ".amsh") != 0) {
            continue;
        }
        if (len >= 10 && strcmp(entries[i].name + len - 9, "_col.amsh") == 0) {
            continue;
        }
        if (s_mesh_count >= SCENE_MAX_MESHES || len - 5 >= sizeof(s_meshes[0])) {
            continue;
        }
        memcpy(s_meshes[s_mesh_count], entries[i].name, len - 5);
        s_meshes[s_mesh_count][len - 5] = 0;
        s_mesh_count++;
    }
    s_mesh_scroll = 0;
    s_palette_tab = 0;
    s_place_mesh = -1;
    s_place_item = -1;
    s_outliner_scroll = 0;
    s_preview_valid = 0;
    s_trigger_sync = HANDLE_INVALID;
}

static Ray scene_chassis_ray(const RigidBody* body, Ray world_ray)
{
    Quat inv = quat_conjugate(body->rot);
    Ray local;
    local.origin = quat_rotate_vec3(inv, vec3_sub(world_ray.origin, body->pos));
    local.dir = quat_rotate_vec3(inv, world_ray.dir);
    return local;
}

b32 editor_scene_update(EditorState* ed, const struct GameInput* input, const struct Camera* cam,
                        struct World* world, struct PhysWorld* phys,
                        const struct Terrain* terrain, struct Vehicle* veh)
{
    (void)terrain;
    b32 over_panel = ui_mouse_over_panel(input);
    Vec2 viewport = r_viewport_size();
    s_preview_valid = 0;

    if (ed->car_boxes_on && veh) {
        RigidBody* body = phys_body(phys, veh->body);
        if (body) {
            Vec3 com = veh->cfg.com_offset;
            if (ed->box_sel >= 0) {
                ed->gizmo.mode = GIZMO_TRANSLATE;
                if (!ed->gizmo.dragging) {
                    ed->gizmo.basis = body->rot;
                }
                InteractBox* box = interact_box((u32)ed->box_sel);
                Vec3 world_c = vec3_add(body->pos,
                                        quat_rotate_vec3(body->rot,
                                                         vec3_sub(box->center, com)));
                Vec3 prev = world_c;
                b32 consumed = 0;
                if (!over_panel || ed->gizmo.dragging) {
                    consumed = gizmo_update(&ed->gizmo, input, cam, viewport, &world_c, 0, 0,
                                            0.0f, 0.0f, 0.0f);
                }
                if (prev.x != world_c.x || prev.y != world_c.y || prev.z != world_c.z) {
                    Vec3 local = quat_rotate_vec3(quat_conjugate(body->rot),
                                                  vec3_sub(world_c, body->pos));
                    box->center = vec3_add(local, com);
                }
                if (consumed) {
                    return 1;
                }
            }
            if (!over_panel && input->mouse_pressed[MOUSE_LEFT]
                && !input->mouse_down[MOUSE_RIGHT]) {
                Ray ray = camera_mouse_ray(cam, input->mouse_x, input->mouse_y, viewport);
                Ray local = scene_chassis_ray(body, ray);
                f32 best_t = 1e30f;
                i32 best = -1;
                for (u32 i = 0; i < IBOX_COUNT; i++) {
                    InteractBox* box = interact_box(i);
                    Vec3 c = vec3_sub(box->center, com);
                    Aabb bb;
                    bb.min = vec3_sub(c, box->half);
                    bb.max = vec3_add(c, box->half);
                    f32 t;
                    if (ray_vs_aabb(local, bb, best_t, &t) && t < best_t) {
                        best_t = t;
                        best = (i32)i;
                    }
                }
                if (best >= 0) {
                    ed->box_sel = best;
                    ed->selection = HANDLE_INVALID;
                    return 1;
                }
            }
        }
    }

    if (s_place_mesh >= 0 || s_place_item >= 0) {
        if (input->key_pressed[KEY_ESCAPE]) {
            s_place_mesh = -1;
            s_place_item = -1;
            return 1;
        }
        Ray ray = camera_mouse_ray(cam, input->mouse_x, input->mouse_y, viewport);
        PhysRayHit hit;
        if (phys_raycast(phys, ray, 500.0f, &hit)) {
            s_preview_valid = 1;
            s_preview_pos = hit.point;
        }
        if (s_preview_valid && !over_panel && input->mouse_pressed[MOUSE_LEFT]
            && !input->mouse_down[MOUSE_RIGHT]) {
            if (s_place_mesh >= 0) {
                EntityKind kind = s_spawn_kind == 1 ? ENTITY_TREE
                                  : (s_spawn_kind == 2 ? ENTITY_BUILDING : ENTITY_STATIC_MESH);
                Handle h = world_spawn(world, kind, s_preview_pos, quat_identity(), 1.0f,
                                       s_meshes[s_place_mesh], ENTITY_FLAG_COLLIDES);
                editor_select_entity(ed, world, h);
                editor_push_create(ed, world, phys, h);
                editor_status_msg(ed, "placed (collision after save+reload)");
            } else {
                ItemKind kind = (ItemKind)s_place_item;
                Item item;
                item.kind = kind;
                item.condition = 1.0f;
                item.aux = 0;
                Vec3 pos = s_preview_pos;
                pos.y += item_cargo_half(kind).y + 0.10f;
                Handle h = interact_spawn_pickup(world, phys, item, pos, 0.0f, vec3_zero());
                editor_select_entity(ed, world, h);
                editor_push_create(ed, world, phys, h);
                editor_status_msg(ed, "placed pickup");
            }
        }
        return 1;
    }
    return 0;
}

static void scene_palette_panel(EditorState* ed, struct World* world, struct PhysWorld* phys,
                                const struct Camera* cam, const struct Terrain* terrain,
                                f32 px)
{
    static const char* kind_names[3] = { "kind: static", "kind: tree", "kind: building" };
    ui_panel_begin("palette", px, 16.0f, SCENE_PANEL_W);
    if (ui_list_item(s_palette_tab == 0 ? "[meshes]" : " meshes", s_palette_tab == 0)) {
        s_palette_tab = 0;
    }
    if (ui_list_item(s_palette_tab == 1 ? "[items]" : " items", s_palette_tab == 1)) {
        s_palette_tab = 1;
    }
    if (s_palette_tab == 0) {
        if (ui_list_item(kind_names[s_spawn_kind], 0)) {
            s_spawn_kind = (s_spawn_kind + 1) % 3;
        }
        if (ui_button("scroll")) {
            s_mesh_scroll += SCENE_LIST_PAGE;
            if (s_mesh_scroll >= (i32)s_mesh_count) {
                s_mesh_scroll = 0;
            }
        }
        for (i32 i = s_mesh_scroll;
             i < (i32)s_mesh_count && i < s_mesh_scroll + SCENE_LIST_PAGE; i++) {
            if (ui_list_item(s_meshes[i], i == s_place_mesh)) {
                s_place_mesh = i == s_place_mesh ? -1 : i;
                s_place_item = -1;
            }
        }
    } else {
        for (i32 k = 1; k < (i32)ITEM_KIND_COUNT; k++) {
            if (ui_list_item(item_id((ItemKind)k), k == s_place_item)) {
                s_place_item = k == s_place_item ? -1 : k;
                s_place_mesh = -1;
            }
        }
    }
    if (ui_button("add trigger")) {
        Vec3 fwd = camera_forward(cam);
        Vec3 pos = vec3_add(cam->pos, vec3_scale(fwd, 8.0f));
        pos.y = heightfield_sample(&terrain->hf, pos.x, pos.z) + 1.0f;
        Handle h = world_spawn(world, ENTITY_TRIGGER, pos, quat_identity(), 1.0f, 0,
                               ENTITY_FLAG_INTERACTABLE);
        Entity* e = world_entity(world, h);
        if (e) {
            snprintf(e->mesh_name, sizeof(e->mesh_name), "trigger");
            e->half = v3(1.0f, 1.0f, 1.0f);
        }
        editor_select_entity(ed, world, h);
        editor_push_create(ed, world, phys, h);
    }
    if (s_place_mesh >= 0 || s_place_item >= 0) {
        ui_label("click ground to place, esc stops");
    }
    ui_panel_end();
}

static void scene_outliner_panel(EditorState* ed, struct World* world, f32 px)
{
    ui_panel_begin("entities", px, 512.0f, SCENE_PANEL_W);
    if (ui_button("scroll")) {
        s_outliner_scroll += SCENE_LIST_PAGE;
        if (s_outliner_scroll >= (i32)world->entities.count) {
            s_outliner_scroll = 0;
        }
    }
    i32 row = 0;
    i32 shown = 0;
    for (u32 idx = 0; idx < world->entities.capacity && shown < SCENE_LIST_PAGE; idx++) {
        Entity* e = pool_at(&world->entities, idx);
        if (!e) {
            continue;
        }
        if (row++ < s_outliner_scroll) {
            continue;
        }
        char label[64];
        if (e->kind == ENTITY_PART_PICKUP) {
            snprintf(label, sizeof(label), "%u item %s", idx, item_id((ItemKind)e->aux_kind));
        } else if (e->kind == ENTITY_TRIGGER) {
            snprintf(label, sizeof(label), "%u trig %s", idx, e->mesh_name);
        } else {
            snprintf(label, sizeof(label), "%u %s", idx, e->mesh_name);
        }
        b32 is_sel = ed->selection.idx == idx
                     && ed->selection.gen == world->entities.gens[idx];
        if (ui_list_item(label, is_sel)) {
            Handle h = { idx, world->entities.gens[idx] };
            editor_select_entity(ed, world, h);
            s_place_mesh = -1;
            s_place_item = -1;
        }
        shown++;
    }
    ui_panel_end();
}

static void scene_detail_panel(EditorState* ed, struct World* world)
{
    Entity* sel = world_entity(world, ed->selection);
    b32 want = ed->car_boxes_on
               || (sel && (sel->kind == ENTITY_PART_PICKUP || sel->kind == ENTITY_TRIGGER));
    ui_panel_begin("tuning", 292.0f, 16.0f, 250.0f);
    if (ui_checkbox("car interact boxes", &ed->car_boxes_on) && !ed->car_boxes_on) {
        ed->box_sel = -1;
    }
    if (ed->car_boxes_on) {
        if (ed->box_sel >= 0) {
            InteractBox* box = interact_box((u32)ed->box_sel);
            ui_label("box: %s", box->name);
            ui_label("center %.2f %.2f %.2f", (f64)box->center.x, (f64)box->center.y,
                     (f64)box->center.z);
            ui_slider_f32("half x", &box->half.x, 0.01f, 1.6f);
            ui_slider_f32("half y", &box->half.y, 0.01f, 1.6f);
            ui_slider_f32("half z", &box->half.z, 0.01f, 1.6f);
        } else {
            ui_label("click a box to tune it");
        }
        if (ui_button("save boxes")) {
            editor_status_msg(ed, interact_boxes_save() ? "saved interact boxes"
                                                        : "box save failed");
        }
    }
    if (sel && sel->kind == ENTITY_PART_PICKUP) {
        ItemKind kind = (ItemKind)sel->aux_kind;
        ui_label("pickup: %s", item_name(kind));
        if (ui_slider_f32("condition", &sel->aux_value, 0.0f, 1.0f)) {
            ed->dirty = 1;
        }
        if (kind == ITEM_CASSETTE) {
            ui_label("tape: %s", tape_label((i32)sel->aux_data));
            i32 option_count = (i32)tapes_count() + 1;
            if (option_count > SCENE_LIST_PAGE && ui_button("scroll tapes")) {
                s_tape_scroll += SCENE_LIST_PAGE;
                if (s_tape_scroll >= option_count) {
                    s_tape_scroll = 0;
                }
            }
            for (i32 a = s_tape_scroll;
                 a < option_count && a < s_tape_scroll + SCENE_LIST_PAGE; a++) {
                if (ui_list_item(tape_label(a), (u32)a == sel->aux_data)) {
                    sel->aux_data = (u32)a;
                    ed->dirty = 1;
                }
            }
        } else if (kind == ITEM_FLOPPY) {
            ui_label("disk: %s", disk_label((i32)sel->aux_data));
            for (i32 d = 0; d < DISK_COUNT; d++) {
                if (ui_list_item(disk_label(d), (u32)d == sel->aux_data)) {
                    sel->aux_data = (u32)d;
                    ed->dirty = 1;
                }
            }
        } else {
            f32 aux = (f32)sel->aux_data;
            if (ui_slider_f32("aux", &aux, 0.0f, 62.0f)) {
                sel->aux_data = (u32)(aux + 0.5f);
                ed->dirty = 1;
            }
        }
    }
    if (sel && sel->kind == ENTITY_TRIGGER) {
        if (!(s_trigger_sync.idx == ed->selection.idx && s_trigger_sync.gen == ed->selection.gen)) {
            s_trigger_sync = ed->selection;
            snprintf(s_trigger_name, sizeof(s_trigger_name), "%s", sel->mesh_name);
        }
        ui_text_field("trigger name", s_trigger_name, sizeof(s_trigger_name));
        if (strcmp(s_trigger_name, sel->mesh_name) != 0) {
            snprintf(sel->mesh_name, sizeof(sel->mesh_name), "%s", s_trigger_name);
            ed->dirty = 1;
        }
        if (ui_slider_f32("half x", &sel->half.x, 0.2f, 24.0f)
            || ui_slider_f32("half y", &sel->half.y, 0.2f, 24.0f)
            || ui_slider_f32("half z", &sel->half.z, 0.2f, 24.0f)) {
            ed->dirty = 1;
        }
        f32 action = (f32)sel->aux_kind;
        if (ui_slider_f32("action id", &action, 0.0f, 8.0f)) {
            sel->aux_kind = (u32)(action + 0.5f);
            ed->dirty = 1;
        }
        if (ui_slider_f32("param", &sel->aux_value, 0.0f, 10.0f)) {
            ed->dirty = 1;
        }
    }
    if (!want && !sel) {
        ui_label("nothing selected");
    }
    ui_panel_end();
}

void editor_scene_render(EditorState* ed, const struct GameInput* input,
                         const struct Camera* cam, struct World* world, struct PhysWorld* phys,
                         const struct Terrain* terrain, struct Vehicle* veh)
{
    (void)input;
    Vec2 vp = r_viewport_size();
    f32 px = vp.x - SCENE_PANEL_W - 16.0f;

    if (s_preview_valid) {
        dd_overlay(1);
        dd_sphere(s_preview_pos, 0.35f, DD_GREEN);
        dd_cross(s_preview_pos, 1.2f, DD_GREEN);
        dd_overlay(0);
    }

    if (ed->car_boxes_on && veh) {
        RigidBody* body = phys_body(phys, veh->body);
        if (body) {
            Vec3 com = veh->cfg.com_offset;
            dd_overlay(1);
            for (u32 i = 0; i < IBOX_COUNT; i++) {
                InteractBox* box = interact_box(i);
                Vec3 world_c = vec3_add(body->pos,
                                        quat_rotate_vec3(body->rot,
                                                         vec3_sub(box->center, com)));
                b32 is_sel = (i32)i == ed->box_sel;
                dd_obb(world_c, body->rot, box->half, is_sel ? DD_YELLOW : DD_ORANGE);
                if (is_sel) {
                    dd_text_3d(vec3_add(world_c, v3(0.0f, box->half.y + 0.12f, 0.0f)), 13.0f,
                               DD_YELLOW, "%s", box->name);
                }
            }
            dd_overlay(0);
            if (ed->box_sel >= 0) {
                InteractBox* box = interact_box((u32)ed->box_sel);
                Vec3 world_c = vec3_add(body->pos,
                                        quat_rotate_vec3(body->rot,
                                                         vec3_sub(box->center, com)));
                gizmo_render(&ed->gizmo, cam, world_c);
            }
        }
    }

    scene_palette_panel(ed, world, phys, cam, terrain, px);
    scene_outliner_panel(ed, world, px);
    scene_detail_panel(ed, world);
}
