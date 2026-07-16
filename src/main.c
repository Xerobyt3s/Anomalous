#include "core/types.h"
#include "core/arena.h"
#include "core/log.h"
#include "core/rng.h"
#include "core/config.h"
#include "math/vmath.h"
#include "platform/platform.h"
#include "render/camera.h"
#include "render/render.h"
#include "render/debug_draw.h"
#include "render/text.h"
#include "render/terrain_render.h"
#include "physics/heightfield.h"
#include "physics/physics.h"
#include "vehicle/vehicle.h"
#include "vehicle/vehicle_render.h"
#include "assets/assets.h"
#include "world/world.h"
#include "world/terrain.h"
#include "world/zone.h"
#include "player/player.h"
#include "player/interact.h"
#include "carsys/carsys.h"
#include "carsys/carsys_render.h"
#include "ui/ui.h"

#include <stdio.h>
#include <string.h>

#define FIXED_DT (1.0f / 120.0f)
#define MAX_FRAME_DT 0.25
#define ZONE_DIR "assets/zones/testzone"
#define CAR_CFG_PATH "assets/cars/excel.cfg"
#define SCENE_PHYS_SEED 1234u
#define SCENE_PHYS_BODIES 24
#define SCENE_PHYS_TICKS 1440
#define TELEM_SAMPLES 480
#define CHASE_DISTANCE 9.0f
#define CHASE_HEIGHT 3.2f

typedef struct GameState {
    u64 tick_count;
} GameState;

typedef struct Telem {
    f32 samples[TELEM_SAMPLES];
    u32 head;
} Telem;

static GameState s_game;
static Terrain s_terrain;
static World s_world;
static PhysWorld s_phys;
static Vehicle s_vehicle;
static Player s_player;
static CarSys s_carsys;
static Interact s_interact;
static ZoneSpawn s_zone_spawn;
static Camera s_camera;
static f32 s_fps;
static b32 s_show_telemetry;
static b32 s_show_phys_debug;
static b32 s_show_collision;
static b32 s_show_tuning;
static b32 s_show_carsys;
static b32 s_slow_mo;
static b32 s_free_cam;
static b32 s_chase_cam;
static b32 s_pending_jump;
static b32 s_pending_interact;
static Telem s_telem_rpm;
static Telem s_telem_slip;
static Telem s_telem_load;
static Telem s_telem_comp;
static Telem s_telem_speed;

static void telem_push(Telem* telem, f32 value)
{
    telem->samples[telem->head] = value;
    telem->head = (telem->head + 1) % TELEM_SAMPLES;
}

static BodyHandle spawn_box(PhysWorld* world, Vec3 pos, Quat rot, Vec3 half_extents, Vec3 vel)
{
    f32 mass = 200.0f * 8.0f * half_extents.x * half_extents.y * half_extents.z;
    BodyHandle handle = phys_body_create_box(world, pos, rot, half_extents, mass);
    RigidBody* body = phys_body(world, handle);
    if (body) {
        body->vel = vel;
    }
    return handle;
}

static void spawn_random_box(PhysWorld* world, Rng* rng, Vec3 pos, Vec3 vel)
{
    Vec3 half_extents = v3(rng_range(rng, 0.25f, 0.8f),
                           rng_range(rng, 0.25f, 0.8f),
                           rng_range(rng, 0.25f, 0.8f));
    Quat rot = quat_from_euler(rng_range(rng, 0.0f, 2.0f * PI32),
                               rng_range(rng, 0.0f, 2.0f * PI32),
                               rng_range(rng, 0.0f, 2.0f * PI32));
    spawn_box(world, pos, rot, half_extents, vel);
}

static void reset_car(void)
{
    vehicle_teleport(&s_vehicle, &s_phys, s_zone_spawn.car_pos, s_zone_spawn.car_yaw);
}

static void game_tick(f32 dt, PlayerCommand cmd)
{
    carsys_tick(&s_carsys, &s_vehicle, &s_phys, dt);
    vehicle_tick(&s_vehicle, &s_phys, dt);
    phys_tick(&s_phys, dt);
    player_tick(&s_player, &s_phys, &s_vehicle, cmd, dt);

    telem_push(&s_telem_rpm, drivetrain_rpm(&s_vehicle.train));
    telem_push(&s_telem_slip, (s_vehicle.wheels[WHEEL_RL].slip_ratio + s_vehicle.wheels[WHEEL_RR].slip_ratio) * 0.5f);
    telem_push(&s_telem_load, s_vehicle.wheels[WHEEL_FL].load * 0.001f);
    telem_push(&s_telem_comp, s_vehicle.wheels[WHEEL_FL].compression);
    telem_push(&s_telem_speed, f_abs(vehicle_forward_speed(&s_vehicle, &s_phys)) * 3.6f);

    s_game.tick_count++;
}

static void draw_collision_wireframe(void)
{
    const StaticGrid* grid = &s_phys.statics;
    Vec3 cam = r_camera_pos();
    if (grid->built) {
        for (u32 i = 0; i < grid->tri_count; i++) {
            const StaticTri* tri = &grid->tris[i];
            if (vec3_distance_sq(tri->a, cam) > 80.0f * 80.0f) {
                continue;
            }
            dd_line(tri->a, tri->b, DD_ORANGE);
            dd_line(tri->b, tri->c, DD_ORANGE);
            dd_line(tri->c, tri->a, DD_ORANGE);
        }
    }
    const Heightfield* hf = &s_terrain.hf;
    i32 cam_ix = (i32)((cam.x - hf->origin.x) / hf->cell_size);
    i32 cam_iz = (i32)((cam.z - hf->origin.z) / hf->cell_size);
    i32 radius = 14;
    for (i32 iz = cam_iz - radius; iz < cam_iz + radius; iz++) {
        for (i32 ix = cam_ix - radius; ix < cam_ix + radius; ix++) {
            if (ix < 0 || iz < 0 || ix + 1 >= (i32)hf->size_x || iz + 1 >= (i32)hf->size_z) {
                continue;
            }
            Vec3 tris[6];
            heightfield_cell_triangles(hf, (u32)ix, (u32)iz, tris);
            dd_line(tris[0], tris[1], dd_rgba(60, 120, 80, 255));
            dd_line(tris[0], tris[2], dd_rgba(60, 120, 80, 255));
            dd_line(tris[3], tris[5], dd_rgba(60, 120, 80, 255));
        }
    }
}

static void draw_phys_debug(f32 alpha)
{
    for (u32 idx = 0; idx < s_phys.bodies.capacity; idx++) {
        RigidBody* body = pool_at(&s_phys.bodies, idx);
        if (!body) {
            continue;
        }
        if (idx == s_vehicle.body.idx) {
            continue;
        }
        Vec3 pos = vec3_lerp(body->prev_pos, body->pos, alpha);
        Quat rot = quat_slerp(body->prev_rot, body->rot, alpha);
        dd_obb(pos, rot, body->half_extents, dd_rgba(130, 140, 150, 255));
    }
    if (s_show_phys_debug) {
        for (u32 i = 0; i < s_phys.contact_count; i++) {
            const PhysContact* contact = &s_phys.contacts[i];
            dd_cross(contact->point, 0.3f, DD_RED);
            dd_arrow(contact->point, vec3_add(contact->point, vec3_scale(contact->normal, 0.8f)), 0.15f, DD_YELLOW);
        }
    }
}

static Ray camera_mouse_ray(const Camera* cam, f32 mouse_x, f32 mouse_y)
{
    Vec2 vp = r_viewport_size();
    f32 ndc_x = mouse_x / vp.x * 2.0f - 1.0f;
    f32 ndc_y = 1.0f - mouse_y / vp.y * 2.0f;
    f32 tan_half = tanf(cam->fov_y * 0.5f);
    f32 aspect = vp.x / vp.y;
    Vec3 forward = camera_forward(cam);
    Vec3 right = camera_right(cam);
    Vec3 up = vec3_cross(right, forward);
    Ray ray;
    ray.origin = cam->pos;
    ray.dir = vec3_normalize(vec3_add(forward,
                                      vec3_add(vec3_scale(right, ndc_x * tan_half * aspect),
                                               vec3_scale(up, ndc_y * tan_half))));
    return ray;
}

static void draw_mouse_ray(const GameInput* input)
{
    if (!input->mouse_down[MOUSE_LEFT] || input->mouse_down[MOUSE_RIGHT]) {
        return;
    }
    Ray ray = camera_mouse_ray(&s_camera, input->mouse_x, input->mouse_y);
    PhysRayHit hit;
    if (phys_raycast(&s_phys, ray, 500.0f, &hit)) {
        dd_sphere(hit.point, 0.25f, DD_MAGENTA);
        dd_arrow(hit.point, vec3_add(hit.point, vec3_scale(hit.normal, 1.5f)), 0.2f, DD_MAGENTA);
    }
}

static void draw_input_bar(f32 x, f32 y, f32 width, f32 value, f32 center, u32 color, const char* label)
{
    f32 height = 10.0f;
    dd_rect_2d(x, y, x + width, y + height, dd_rgba(80, 90, 100, 255));
    if (center > 0.0f) {
        f32 mid = x + width * 0.5f;
        dd_rect_2d_filled(mid, y + 1.0f, mid + value * width * 0.5f, y + height - 1.0f, color);
        dd_line_2d(mid, y, mid, y + height, dd_rgba(120, 130, 140, 255));
    } else {
        dd_rect_2d_filled(x + 1.0f, y + 1.0f, x + 1.0f + value * (width - 2.0f), y + height - 1.0f, color);
    }
    dd_text_2d(x + width + 6.0f, y + height, 13.0f, DD_GRAY, "%s", label);
}

static void draw_drive_hud(void)
{
    Vec2 vp = r_viewport_size();
    f32 x = 14.0f;
    f32 y = vp.y - 16.0f;

    f32 speed_kmh = f_abs(vehicle_forward_speed(&s_vehicle, &s_phys)) * 3.6f;
    char gear_label[8];
    if (s_vehicle.train.gear == -1) {
        snprintf(gear_label, sizeof(gear_label), "R");
    } else if (s_vehicle.train.gear == 0) {
        snprintf(gear_label, sizeof(gear_label), "N");
    } else {
        snprintf(gear_label, sizeof(gear_label), "%d", s_vehicle.train.gear);
    }
    dd_text_2d(x, y, 28.0f, DD_WHITE, "%3.0f km/h", (f64)speed_kmh);
    dd_text_2d(x + 170.0f, y, 28.0f, s_vehicle.train.shifting ? DD_ORANGE : DD_CYAN, "[%s]", gear_label);
    dd_text_2d(x + 240.0f, y, 28.0f, DD_GRAY, "%4.0f rpm", (f64)drivetrain_rpm(&s_vehicle.train));

    f32 bar_y = y - 82.0f;
    draw_input_bar(x, bar_y, 120.0f, s_vehicle.input.throttle, 0.0f, dd_rgba(60, 220, 90, 255), "throttle");
    draw_input_bar(x, bar_y + 18.0f, 120.0f, s_vehicle.input.brake, 0.0f, dd_rgba(230, 70, 60, 255), "brake");
    draw_input_bar(x, bar_y + 36.0f, 120.0f, s_vehicle.steer_deg / s_vehicle.cfg.steer_max_deg, 1.0f,
                   dd_rgba(200, 210, 230, 255), "steer");
    if (s_vehicle.input.handbrake) {
        dd_text_2d(x, bar_y - 6.0f, 15.0f, DD_ORANGE, "HANDBRAKE");
    }

    f32 gx = x + 240.0f;
    f32 temp_norm = f_clamp01((s_carsys.fluids.coolant_temp - COOLANT_AMBIENT_C)
                              / (COOLANT_MELTDOWN_C - COOLANT_AMBIENT_C));
    b32 hot = s_carsys.fluids.coolant_temp > COOLANT_OVERHEAT_C;
    draw_input_bar(gx, bar_y, 100.0f, s_carsys.fluids.fuel, 0.0f, dd_rgba(90, 160, 240, 255), "fuel");
    draw_input_bar(gx, bar_y + 18.0f, 100.0f, temp_norm, 0.0f,
                   hot ? dd_rgba(240, 60, 50, 255) : dd_rgba(230, 160, 60, 255), "temp");
    draw_input_bar(gx, bar_y + 36.0f, 100.0f, s_carsys.elec.battery_charge, 0.0f,
                   dd_rgba(220, 210, 90, 255), "battery");
    draw_input_bar(gx, bar_y + 54.0f, 100.0f, s_carsys.parts[PART_ENGINE].condition, 0.0f,
                   dd_rgba(140, 220, 140, 255), "engine");

    f32 warn_y = bar_y - 24.0f;
    if (hot) {
        dd_text_2d(gx, warn_y, 15.0f, DD_RED, "TEMP");
    }
    if (s_carsys.fluids.oil < 0.3f) {
        dd_text_2d(gx + 56.0f, warn_y, 15.0f, DD_RED, "OIL");
    }
    if (s_carsys.elec.battery_charge < 0.15f) {
        dd_text_2d(gx + 100.0f, warn_y, 15.0f, DD_ORANGE, "BATT");
    }
    if (s_carsys.headlight_switch) {
        dd_text_2d(gx + 156.0f, warn_y, 15.0f,
                   s_vehicle.effects.headlights_on ? DD_CYAN : DD_GRAY, "LIGHTS");
    }
}

static void draw_carsys_panel(void)
{
    ui_panel_begin("car systems [f4]", 336.0f, 66.0f, 330.0f);
    for (u32 i = 0; i < PART_COUNT; i++) {
        const PartDef* def = part_def((PartKind)i);
        const PartSlot* slot = &s_carsys.parts[i];
        ui_label("%-11s %s %5.1f%%", def->name, slot->installed ? "in " : "OUT",
                 (f64)(slot->condition * 100.0f));
    }
    ui_label("battery %3.0f%% | alt %4.1fA | draw %4.1fA",
             (f64)(s_carsys.elec.battery_charge * 100.0f), (f64)s_carsys.elec.alternator_amps,
             (f64)s_carsys.elec.draw_amps);
    for (u32 i = 0; i < CONSUMER_COUNT; i++) {
        ui_label("%-10s fuse %s | %s", consumer_name((Consumer)i),
                 s_carsys.elec.fuse_ok[i] ? "ok " : "BLOWN",
                 s_carsys.elec.powered[i] ? "powered" : "off");
    }
    ui_label("fuel %3.0f%% | oil %3.0f%% | coolant %3.0fC",
             (f64)(s_carsys.fluids.fuel * 100.0f), (f64)(s_carsys.fluids.oil * 100.0f),
             (f64)s_carsys.fluids.coolant_temp);
    ui_label("engine %s | power x%.2f | hood %s",
             s_carsys.engine_on ? "RUNNING" : "OFF", (f64)s_vehicle.effects.engine_power_mul,
             s_carsys.hood_target ? "open" : "shut");
    ui_label("handbrake %s | trunk %s | cargo %u/%u",
             s_carsys.handbrake_latched ? "SET" : "off",
             s_carsys.trunk_target ? "open" : "shut", carsys_cargo_count(&s_carsys), CARGO_MAX);
    ui_panel_end();
}

static void update_headlights(f32 alpha)
{
    RigidBody* body = phys_body(&s_phys, s_vehicle.body);
    if (!body) {
        r_set_headlights(vec3_zero(), vec3_zero(), v3(0.0f, 0.0f, -1.0f), 0.0f);
        return;
    }
    Vec3 pos = vec3_lerp(body->prev_pos, body->pos, alpha);
    Quat rot = quat_slerp(body->prev_rot, body->rot, alpha);
    Vec3 com = s_vehicle.cfg.com_offset;
    Vec3 left = vec3_add(pos, quat_rotate_vec3(rot, vec3_sub(v3(-0.55f, 0.02f, -2.05f), com)));
    Vec3 right = vec3_add(pos, quat_rotate_vec3(rot, vec3_sub(v3(0.55f, 0.02f, -2.05f), com)));
    Vec3 dir = quat_rotate_vec3(rot, v3(0.0f, -0.10f, -0.99f));
    f32 intensity = s_vehicle.effects.headlights_on ? 5.0f : 0.0f;
    r_set_headlights(left, right, dir, intensity);
}

static void draw_status_hud(void)
{
    f32 line_height = text_line_height(16.0f);
    f32 y = 8.0f + line_height;
    dd_text_2d(12.0f, y, 16.0f, DD_WHITE, "%.0f fps | tick %llu | %u terrain chunks%s%s",
               (f64)s_fps, (unsigned long long)s_game.tick_count,
               terrain_render_chunks_drawn(),
               s_slow_mo ? " | SLOW-MO 0.1x" : "",
               s_free_cam ? " | FREE CAM" : "");
    y += line_height;
    if (player_driving(&s_player)) {
        dd_text_2d(12.0f, y, 16.0f, DD_GRAY,
                   "wasd drive | space handbrake | f engine | l lights | e doors/lever, look out open door to exit | c camera | f1-f7 debug");
    } else {
        dd_text_2d(12.0f, y, 16.0f, DD_GRAY,
                   "wasd walk | shift run | space jump | e interact/enter | g drop | r reset car | f1-f7 debug");
    }
}

static void draw_interact_prompt(void)
{
    Vec2 vp = r_viewport_size();
    f32 cx = vp.x * 0.5f;
    f32 y = vp.y * 0.72f;
    if (s_interact.action != ACTION_NONE) {
        u32 color = s_interact.action == ACTION_INFO ? DD_GRAY : DD_WHITE;
        dd_text_2d(cx - 90.0f, y, 20.0f, color, "%s", s_interact.prompt);
        if (s_interact.hold_progress > 0.0f) {
            dd_rect_2d(cx - 80.0f, y + 10.0f, cx + 80.0f, y + 20.0f, dd_rgba(90, 100, 110, 255));
            dd_rect_2d_filled(cx - 79.0f, y + 11.0f, cx - 79.0f + 158.0f * s_interact.hold_progress,
                              y + 19.0f, dd_rgba(90, 200, 120, 255));
        }
    }
    if (s_player.state == PLAYER_ON_FOOT && s_interact.hands.kind != ITEM_NONE) {
        dd_text_2d(14.0f, vp.y - 44.0f, 16.0f, DD_CYAN, "hands: %s (%.0f%%) | [G] drop",
                   item_name(s_interact.hands.kind), (f64)(s_interact.hands.condition * 100.0f));
    }
    if (s_player.state == PLAYER_DRIVING && !s_carsys.engine_on) {
        dd_text_2d(cx - 90.0f, y + 34.0f, 18.0f, DD_ORANGE,
                   s_carsys.crank_timer > 0.0f ? "cranking..." : "[F] start engine");
    }
}

static void draw_interact_highlights(f32 alpha)
{
    if (s_free_cam || (s_player.state != PLAYER_ON_FOOT && s_player.state != PLAYER_DRIVING)) {
        return;
    }
    RigidBody* body = phys_body(&s_phys, s_vehicle.body);
    if (!body) {
        return;
    }
    Vec3 body_pos = vec3_lerp(body->prev_pos, body->pos, alpha);
    Quat body_rot = quat_slerp(body->prev_rot, body->rot, alpha);
    f32 wave = 0.5f + 0.5f * sinf((f32)s_game.tick_count * 0.1f);
    u8 v = (u8)(160 + 80.0f * wave);
    u32 target_color = dd_rgba(v, v, 255, 255);
    u32 slot_color = dd_rgba(90, (u8)(170 + 70.0f * wave), 100, 255);

    if (s_interact.action == ACTION_PICKUP) {
        Entity* entity = world_entity(&s_world, s_interact.target_entity);
        if (entity && entity->mesh) {
            Vec3 center = vec3_scale(vec3_add(entity->mesh->bounds.min, entity->mesh->bounds.max),
                                     0.5f * entity->scale);
            Vec3 half = vec3_scale(vec3_sub(entity->mesh->bounds.max, entity->mesh->bounds.min),
                                   0.5f * entity->scale);
            dd_obb(vec3_add(entity->pos, quat_rotate_vec3(entity->rot, center)), entity->rot,
                   half, target_color);
        }
    } else if (s_interact.action != ACTION_NONE) {
        dd_obb(vec3_add(body_pos, quat_rotate_vec3(body_rot, s_interact.target_center)), body_rot,
               s_interact.target_half, target_color);
    }

    if (s_interact.hands.kind != ITEM_NONE && s_player.state == PLAYER_ON_FOOT) {
        for (u32 k = 0; k < PART_COUNT; k++) {
            const PartDef* def = part_def((PartKind)k);
            if (s_carsys.parts[k].installed || item_for_part((PartKind)k) != s_interact.hands.kind) {
                continue;
            }
            if (def->engine_bay && s_carsys.hood_open < 0.8f) {
                continue;
            }
            if (s_interact.action == ACTION_INSTALL_PART && s_interact.target_part == (PartKind)k) {
                continue;
            }
            Vec3 local = vec3_sub(def->socket_pos, s_vehicle.cfg.com_offset);
            dd_obb(vec3_add(body_pos, quat_rotate_vec3(body_rot, local)), body_rot,
                   def->socket_half, slot_color);
        }
    }
}

static void draw_cargo_preview(f32 alpha)
{
    if (s_interact.action != ACTION_PLACE_CARGO || s_free_cam) {
        return;
    }
    RigidBody* body = phys_body(&s_phys, s_vehicle.body);
    if (!body) {
        return;
    }
    Vec3 pos = vec3_lerp(body->prev_pos, body->pos, alpha);
    Quat rot = quat_slerp(body->prev_rot, body->rot, alpha);
    Vec3 local = vec3_sub(s_interact.place_pos, s_vehicle.cfg.com_offset);
    Mat4 base = mat4_trs(pos, rot, v3(1.0f, 1.0f, 1.0f));
    Mat4 model = mat4_mul(base, mat4_trs(vec3_add(local, v3(0.0f, 0.02f, 0.0f)),
                                         item_cargo_rot(s_interact.hands.kind),
                                         v3(1.0f, 1.0f, 1.0f)));
    r_draw_mesh(asset_mesh(item_mesh(s_interact.hands.kind)), model);
}

static void draw_viewmodel(void)
{
    if (s_player.state != PLAYER_ON_FOOT || s_interact.hands.kind == ITEM_NONE || s_free_cam) {
        return;
    }
    if (s_interact.action == ACTION_PLACE_CARGO) {
        return;
    }
    Vec3 fwd = camera_forward(&s_camera);
    Vec3 right = camera_right(&s_camera);
    Vec3 pos = vec3_add(s_camera.pos, vec3_add(vec3_scale(fwd, 0.62f), vec3_scale(right, 0.30f)));
    pos.y -= 0.34f;
    f32 scale = s_interact.hands.kind == ITEM_TIRE ? 0.45f : 0.85f;
    Quat rot = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), -s_camera.yaw);
    r_draw_mesh(asset_mesh(item_mesh(s_interact.hands.kind)),
                mat4_trs(pos, rot, v3(scale, scale, scale)));
}

static void spawn_spare(ItemKind kind, f32 condition, Vec3 offset)
{
    Quat place_rot = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), s_zone_spawn.car_yaw);
    Quat rot = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), s_zone_spawn.car_yaw + offset.x * 2.0f);
    Vec3 pos = vec3_add(s_zone_spawn.car_pos, quat_rotate_vec3(place_rot, offset));
    f32 lift = kind == ITEM_TIRE ? 0.28f : 0.16f;
    pos.y = heightfield_sample(&s_terrain.hf, pos.x, pos.z) + lift;
    EntityHandle handle = world_spawn(&s_world, ENTITY_PART_PICKUP, pos, rot, 1.0f,
                                      item_mesh(kind), 0);
    Entity* entity = world_entity(&s_world, handle);
    if (entity) {
        entity->aux_kind = (u32)kind;
        entity->aux_value = condition;
    }
}

static void spawn_spares(void)
{
    spawn_spare(ITEM_BATTERY, 0.9f, v3(2.6f, 0.0f, -1.0f));
    spawn_spare(ITEM_TIRE, 1.0f, v3(3.3f, 0.0f, -0.2f));
    spawn_spare(ITEM_TIRE, 0.65f, v3(3.4f, 0.0f, 0.9f));
    spawn_spare(ITEM_RADIATOR, 0.85f, v3(2.8f, 0.0f, 1.8f));
    spawn_spare(ITEM_ALTERNATOR, 0.8f, v3(3.9f, 0.0f, 1.5f));
    spawn_spare(ITEM_JERRYCAN, 1.0f, v3(2.4f, 0.0f, 2.6f));
    spawn_spare(ITEM_OILCAN, 1.0f, v3(3.1f, 0.0f, 2.8f));
}

static void draw_telemetry_panel(void)
{
    ui_panel_begin("telemetry [f1]", 12.0f, 66.0f, 310.0f);
    static const char* wheel_names[4] = { "FL", "FR", "RL", "RR" };
    for (u32 i = 0; i < VEHICLE_WHEEL_COUNT; i++) {
        Wheel* w = &s_vehicle.wheels[i];
        ui_label("%s %s load %5.0fN sr %+.2f sa %+5.1fdeg", wheel_names[i],
                 w->grounded ? "gnd" : "air", (f64)w->load, (f64)w->slip_ratio,
                 (f64)(w->slip_angle * RAD_TO_DEG));
    }
    ui_graph("rpm", s_telem_rpm.samples, TELEM_SAMPLES, s_telem_rpm.head, 0.0f, 7000.0f, dd_rgba(240, 180, 60, 255));
    ui_graph("slip ratio rear", s_telem_slip.samples, TELEM_SAMPLES, s_telem_slip.head, -1.5f, 1.5f, dd_rgba(240, 90, 70, 255));
    ui_graph("load FL kN", s_telem_load.samples, TELEM_SAMPLES, s_telem_load.head, 0.0f, 12.0f, dd_rgba(90, 200, 120, 255));
    ui_graph("compression FL m", s_telem_comp.samples, TELEM_SAMPLES, s_telem_comp.head, 0.0f, 0.3f, dd_rgba(110, 170, 240, 255));
    ui_graph("speed km/h", s_telem_speed.samples, TELEM_SAMPLES, s_telem_speed.head, 0.0f, 180.0f, dd_rgba(220, 220, 230, 255));
    ui_panel_end();
}

static void draw_tuning_panel(void)
{
    Vec2 vp = r_viewport_size();
    VehicleConfig* cfg = &s_vehicle.cfg;
    ui_panel_begin("tuning [f7] (wagon.cfg hot reloads)", vp.x - 342.0f, 66.0f, 330.0f);
    if (ui_slider_f32("front spring N/m", &cfg->wheels[WHEEL_FL].spring_k, 10000.0f, 80000.0f)) {
        cfg->wheels[WHEEL_FR].spring_k = cfg->wheels[WHEEL_FL].spring_k;
    }
    if (ui_slider_f32("front damper", &cfg->wheels[WHEEL_FL].damper_c, 500.0f, 8000.0f)) {
        cfg->wheels[WHEEL_FR].damper_c = cfg->wheels[WHEEL_FL].damper_c;
    }
    if (ui_slider_f32("rear spring N/m", &cfg->wheels[WHEEL_RL].spring_k, 10000.0f, 80000.0f)) {
        cfg->wheels[WHEEL_RR].spring_k = cfg->wheels[WHEEL_RL].spring_k;
    }
    if (ui_slider_f32("rear damper", &cfg->wheels[WHEEL_RL].damper_c, 500.0f, 8000.0f)) {
        cfg->wheels[WHEEL_RR].damper_c = cfg->wheels[WHEEL_RL].damper_c;
    }
    ui_slider_f32("tire peak mu", &cfg->tire_peak_mu, 0.4f, 2.0f);
    ui_slider_f32("tire slide mu", &cfg->tire_slide_mu, 0.3f, 1.5f);
    ui_slider_f32("tire peak slip", &cfg->tire_peak_slip, 0.05f, 0.3f);
    ui_slider_f32("tire peak angle deg", &cfg->tire_peak_angle_deg, 4.0f, 20.0f);
    ui_slider_f32("brake torque Nm", &cfg->brake_torque, 500.0f, 4000.0f);
    ui_slider_f32("diff lock", &cfg->diff_lock, 0.0f, 1.0f);
    ui_slider_f32("drag coef", &cfg->drag_coef, 0.0f, 3.0f);
    ui_slider_f32("steer max deg", &cfg->steer_max_deg, 15.0f, 45.0f);
    ui_panel_end();
}

static void update_chase_camera(f32 dt, f32 alpha)
{
    RigidBody* body = phys_body(&s_phys, s_vehicle.body);
    if (!body) {
        return;
    }
    Vec3 pos = vec3_lerp(body->prev_pos, body->pos, alpha);
    Quat rot = quat_slerp(body->prev_rot, body->rot, alpha);
    Vec3 fwd = quat_rotate_vec3(rot, v3(0.0f, 0.0f, -1.0f));
    fwd.y = 0.0f;
    if (vec3_length_sq(fwd) < 1e-4f) {
        fwd = v3(0.0f, 0.0f, -1.0f);
    }
    fwd = vec3_normalize(fwd);
    Vec3 target = vec3_add(vec3_sub(pos, vec3_scale(fwd, CHASE_DISTANCE)), v3(0.0f, CHASE_HEIGHT, 0.0f));
    f32 terrain_floor = heightfield_sample(&s_terrain.hf, target.x, target.z) + 0.6f;
    target.y = f_max(target.y, terrain_floor);
    s_camera.pos = v3(f_approach_exp(s_camera.pos.x, target.x, 5.0f, dt),
                      f_approach_exp(s_camera.pos.y, target.y, 4.0f, dt),
                      f_approach_exp(s_camera.pos.z, target.z, 5.0f, dt));
    camera_look_at(&s_camera, vec3_add(pos, v3(0.0f, 1.2f, 0.0f)));
}

static void game_render(f32 alpha, const GameInput* input)
{
    r_begin_frame(&s_camera);
    dd_begin_frame();
    text_begin_frame();
    ui_begin_frame(input);

    terrain_render_draw();
    world_render(&s_world);
    vehicle_render(&s_vehicle, &s_phys, alpha);
    carsys_render(&s_carsys, &s_vehicle, &s_phys, alpha);
    draw_interact_highlights(alpha);
    draw_cargo_preview(alpha);
    draw_viewmodel();
    if (s_show_phys_debug) {
        vehicle_debug_draw(&s_vehicle, &s_phys, alpha, 1);
    }
    draw_phys_debug(alpha);
    if (s_show_collision) {
        draw_collision_wireframe();
    }
    if (s_free_cam) {
        player_debug_draw(&s_player, alpha);
    }
    draw_mouse_ray(input);
    draw_status_hud();
    if (player_driving(&s_player)) {
        draw_drive_hud();
    }
    draw_interact_prompt();
    if (s_show_carsys) {
        draw_carsys_panel();
    }
    if (s_show_telemetry) {
        draw_telemetry_panel();
    }
    if (s_show_tuning) {
        draw_tuning_panel();
    }

    r_end_frame();
}

static void checksum_bytes(u64* hash, const void* data, u64 size)
{
    const u8* bytes = data;
    for (u64 i = 0; i < size; i++) {
        *hash ^= bytes[i];
        *hash *= 1099511628211ull;
    }
}

static u64 phys_state_checksum(PhysWorld* world)
{
    u64 hash = 14695981039346656037ull;
    for (u32 idx = 0; idx < world->bodies.capacity; idx++) {
        RigidBody* body = pool_at(&world->bodies, idx);
        if (!body) {
            continue;
        }
        checksum_bytes(&hash, &body->pos, sizeof(body->pos));
        checksum_bytes(&hash, &body->rot, sizeof(body->rot));
        checksum_bytes(&hash, &body->vel, sizeof(body->vel));
        checksum_bytes(&hash, &body->angular_vel, sizeof(body->angular_vel));
    }
    return hash;
}

static b32 body_state_valid(const RigidBody* body)
{
    f32 speed = vec3_length(body->vel);
    if (body->pos.x != body->pos.x || body->rot.w != body->rot.w || speed != speed) {
        return 0;
    }
    if (f_abs(body->pos.x) > 2000.0f || f_abs(body->pos.y) > 2000.0f || f_abs(body->pos.z) > 2000.0f) {
        return 0;
    }
    return 1;
}

typedef struct ScenePhysResult {
    u64 checksum;
    f32 min_y;
    f32 max_speed;
    u32 settled;
    u32 bodies;
    b32 valid;
} ScenePhysResult;

static ScenePhysResult scene_phys_run(void)
{
    ScenePhysResult result = {0};
    ArenaTemp temp = arena_temp_begin(&g_perm_arena);

    Heightfield* hf = arena_push(&g_perm_arena, Heightfield);
    heightfield_init_procedural(hf, &g_perm_arena, 96, 1.0f, SCENE_PHYS_SEED, 1.0f);
    PhysWorld* world = arena_push(&g_perm_arena, PhysWorld);
    phys_init(world, &g_perm_arena, hf);

    Rng rng;
    rng_seed(&rng, SCENE_PHYS_SEED);
    for (i32 i = 0; i < SCENE_PHYS_BODIES; i++) {
        Vec3 pos = v3(rng_range(&rng, -14.0f, 14.0f),
                      rng_range(&rng, 8.0f, 18.0f),
                      rng_range(&rng, -14.0f, 14.0f));
        spawn_random_box(world, &rng, pos, vec3_zero());
    }

    for (u32 tick = 0; tick < SCENE_PHYS_TICKS; tick++) {
        phys_tick(world, FIXED_DT);
    }

    result.valid = 1;
    result.min_y = 1e30f;
    for (u32 idx = 0; idx < world->bodies.capacity; idx++) {
        RigidBody* body = pool_at(&world->bodies, idx);
        if (!body) {
            continue;
        }
        result.bodies++;
        if (!body_state_valid(body) || body->pos.y < hf->min_height - 2.0f) {
            result.valid = 0;
            continue;
        }
        f32 speed = vec3_length(body->vel);
        result.min_y = f_min(result.min_y, body->pos.y);
        result.max_speed = f_max(result.max_speed, speed);
        if (speed < 0.5f) {
            result.settled++;
        }
    }
    result.checksum = phys_state_checksum(world);

    arena_temp_end(temp);
    return result;
}

static int run_scene_phys(void)
{
    log_info("scene phys: %d bodies, %d ticks at dt=%.5f, seed %u",
             SCENE_PHYS_BODIES, SCENE_PHYS_TICKS, (f64)FIXED_DT, SCENE_PHYS_SEED);
    ScenePhysResult a = scene_phys_run();
    ScenePhysResult b = scene_phys_run();

    log_info("scene phys: run1 checksum %016llx | min_y %.3f | max_speed %.3f | settled %u/%u",
             (unsigned long long)a.checksum, (f64)a.min_y, (f64)a.max_speed, a.settled, a.bodies);
    log_info("scene phys: run2 checksum %016llx", (unsigned long long)b.checksum);

    b32 deterministic = a.checksum == b.checksum;
    b32 pass = deterministic && a.valid && b.valid && a.bodies == SCENE_PHYS_BODIES;
    if (!deterministic) {
        log_error("scene phys: NON-DETERMINISTIC (checksums differ)");
    }
    if (!a.valid || !b.valid) {
        log_error("scene phys: invalid state (nan, escaped, or below terrain)");
    }
    log_info("scene phys: %s", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

static void car_checksum(u64* hash, PhysWorld* world, Vehicle* veh)
{
    RigidBody* body = phys_body(world, veh->body);
    checksum_bytes(hash, &body->pos, sizeof(body->pos));
    checksum_bytes(hash, &body->rot, sizeof(body->rot));
    checksum_bytes(hash, &body->vel, sizeof(body->vel));
    checksum_bytes(hash, &body->angular_vel, sizeof(body->angular_vel));
    for (u32 i = 0; i < VEHICLE_WHEEL_COUNT; i++) {
        checksum_bytes(hash, &veh->wheels[i].omega, sizeof(f32));
    }
    checksum_bytes(hash, &veh->train.engine_omega, sizeof(f32));
}

static void car_run_ticks(PhysWorld* world, Vehicle* veh, VehicleInput input, u32 ticks)
{
    for (u32 i = 0; i < ticks; i++) {
        vehicle_set_input(veh, input);
        vehicle_tick(veh, world, FIXED_DT);
        phys_tick(world, FIXED_DT);
    }
}

typedef struct CarTestReport {
    b32 pass;
    u64 checksum;
} CarTestReport;

static CarTestReport car_test_run(b32 verbose)
{
    CarTestReport report;
    report.pass = 1;
    report.checksum = 14695981039346656037ull;
    VehicleInput idle = {0};

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        Heightfield* hf = arena_push(&g_perm_arena, Heightfield);
        heightfield_init_procedural(hf, &g_perm_arena, 220, 2.0f, 1u, 0.0f);
        PhysWorld* world = arena_push(&g_perm_arena, PhysWorld);
        phys_init(world, &g_perm_arena, hf);
        Vehicle* veh = arena_push(&g_perm_arena, Vehicle);
        if (!vehicle_init(veh, world, CAR_CFG_PATH, v3(-150.0f, 0.9f, 0.0f), -PI32 * 0.5f)) {
            report.pass = 0;
            arena_temp_end(temp);
            return report;
        }
        RigidBody* body = phys_body(world, veh->body);

        car_run_ticks(world, veh, idle, 240);
        f32 z_start = body->pos.z;

        VehicleInput full_throttle = {0};
        full_throttle.throttle = 1.0f;
        car_run_ticks(world, veh, full_throttle, 720);
        f32 accel_speed = vehicle_forward_speed(veh, world);
        f32 drift = f_abs(body->pos.z - z_start);
        b32 accel_ok = body_state_valid(body) && accel_speed >= 15.0f && accel_speed <= 45.0f && drift < 2.0f;
        if (verbose) {
            log_info("car_test accel: speed %.1f m/s (%.0f km/h) in [15,45] | drift %.2f m < 2 | gear %d | %s",
                     (f64)accel_speed, (f64)(accel_speed * 3.6f), (f64)drift, veh->train.gear,
                     accel_ok ? "PASS" : "FAIL");
        }
        report.pass &= accel_ok;
        car_checksum(&report.checksum, world, veh);

        f32 x_brake_start = body->pos.x;
        VehicleInput full_brake = {0};
        full_brake.brake = 1.0f;
        b32 stopped = 0;
        f32 min_fwd_speed = 1e30f;
        for (u32 i = 0; i < 960; i++) {
            vehicle_set_input(veh, full_brake);
            vehicle_tick(veh, world, FIXED_DT);
            phys_tick(world, FIXED_DT);
            f32 fwd_speed = vehicle_forward_speed(veh, world);
            min_fwd_speed = f_min(min_fwd_speed, fwd_speed);
            if (fwd_speed < 0.15f) {
                stopped = 1;
                break;
            }
        }
        f32 stop_dist = f_abs(body->pos.x - x_brake_start);
        f32 avg_decel = accel_speed * accel_speed / f_max(2.0f * stop_dist, 0.1f);
        b32 brake_ok = body_state_valid(body) && stopped && avg_decel >= 6.0f && avg_decel <= 12.0f
                     && min_fwd_speed > -0.5f;
        if (verbose) {
            log_info("car_test brake: stop dist %.1f m from %.1f m/s | avg decel %.1f m/s2 in [6,12] | min fwd %.2f | %s",
                     (f64)stop_dist, (f64)accel_speed, (f64)avg_decel, (f64)min_fwd_speed, brake_ok ? "PASS" : "FAIL");
        }
        report.pass &= brake_ok;
        car_checksum(&report.checksum, world, veh);
        arena_temp_end(temp);
    }

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        Heightfield* hf = arena_push(&g_perm_arena, Heightfield);
        heightfield_init_procedural(hf, &g_perm_arena, 220, 2.0f, 1u, 0.0f);
        PhysWorld* world = arena_push(&g_perm_arena, PhysWorld);
        phys_init(world, &g_perm_arena, hf);
        Vehicle* veh = arena_push(&g_perm_arena, Vehicle);
        vehicle_init(veh, world, CAR_CFG_PATH, v3(-150.0f, 0.9f, 0.0f), -PI32 * 0.5f);
        RigidBody* body = phys_body(world, veh->body);

        car_run_ticks(world, veh, idle, 240);
        VehicleInput full_throttle = {0};
        full_throttle.throttle = 1.0f;
        car_run_ticks(world, veh, full_throttle, 420);

        VehicleInput steer_input = {0};
        steer_input.throttle = 0.25f;
        steer_input.steer = 0.5f;
        f32 yaw_rate_accum = 0.0f;
        f32 min_up_dot = 1.0f;
        for (u32 i = 0; i < 480; i++) {
            vehicle_set_input(veh, steer_input);
            vehicle_tick(veh, world, FIXED_DT);
            phys_tick(world, FIXED_DT);
            if (i >= 360) {
                yaw_rate_accum += f_abs(body->angular_vel.y);
            }
            Vec3 up = quat_rotate_vec3(body->rot, v3(0.0f, 1.0f, 0.0f));
            min_up_dot = f_min(min_up_dot, up.y);
        }
        f32 avg_yaw_rate = yaw_rate_accum / 120.0f;
        f32 end_speed = f_abs(vehicle_forward_speed(veh, world));
        b32 steer_ok = body_state_valid(body) && avg_yaw_rate >= 0.2f && avg_yaw_rate <= 1.6f
                     && min_up_dot > 0.906f && end_speed > 4.0f;
        if (verbose) {
            log_info("car_test steer: avg yaw rate %.2f rad/s in [0.2,1.6] | min up dot %.3f > 0.906 | speed %.1f m/s | %s",
                     (f64)avg_yaw_rate, (f64)min_up_dot, (f64)end_speed, steer_ok ? "PASS" : "FAIL");
        }
        report.pass &= steer_ok;
        car_checksum(&report.checksum, world, veh);
        arena_temp_end(temp);
    }

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        Heightfield* hf = arena_push(&g_perm_arena, Heightfield);
        heightfield_init_slope(hf, &g_perm_arena, 120, 1.0f, 0.28f);
        PhysWorld* world = arena_push(&g_perm_arena, PhysWorld);
        phys_init(world, &g_perm_arena, hf);
        Vehicle* veh = arena_push(&g_perm_arena, Vehicle);
        f32 spawn_x = 20.0f;
        vehicle_init(veh, world, CAR_CFG_PATH,
                     v3(spawn_x, spawn_x * 0.28f + 0.9f, 0.0f), -PI32 * 0.5f);
        RigidBody* body = phys_body(world, veh->body);

        VehicleInput hold = {0};
        hold.brake = 1.0f;
        hold.handbrake = 1;
        car_run_ticks(world, veh, hold, 240);
        Vec3 parked_pos = body->pos;

        VehicleInput handbrake_only = {0};
        handbrake_only.handbrake = 1;
        car_run_ticks(world, veh, handbrake_only, 600);
        f32 creep = vec3_distance(body->pos, parked_pos);
        b32 park_ok = body_state_valid(body) && creep < 0.15f;
        if (verbose) {
            log_info("car_test slope park: creep %.3f m < 0.15 on 15.6 deg slope | %s",
                     (f64)creep, park_ok ? "PASS" : "FAIL");
        }
        report.pass &= park_ok;
        car_checksum(&report.checksum, world, veh);
        arena_temp_end(temp);
    }

    return report;
}

static int run_scene_car_test(void)
{
    log_info("scene car_test: scripted maneuvers at dt=%.5f, car %s", (f64)FIXED_DT, CAR_CFG_PATH);
    CarTestReport a = car_test_run(1);
    CarTestReport b = car_test_run(0);
    b32 deterministic = a.checksum == b.checksum;
    log_info("car_test determinism: run1 %016llx run2 %016llx | %s",
             (unsigned long long)a.checksum, (unsigned long long)b.checksum,
             deterministic ? "PASS" : "FAIL");
    b32 pass = a.pass && b.pass && deterministic;
    log_info("scene car_test: %s", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

typedef struct ZoneCheckResult {
    u64 checksum;
    u32 ray_hits;
    u32 static_tris;
    b32 valid;
} ZoneCheckResult;

static ZoneCheckResult zone_check_run(void)
{
    ZoneCheckResult result = {0};
    ArenaTemp temp = arena_temp_begin(&g_perm_arena);

    World* world = arena_push(&g_perm_arena, World);
    world_init(world, &g_perm_arena);
    Terrain* terrain = arena_push(&g_perm_arena, Terrain);
    PhysWorld* phys = arena_push(&g_perm_arena, PhysWorld);
    phys_init(phys, &g_perm_arena, &terrain->hf);
    ZoneSpawn spawn;
    if (!zone_load(ZONE_DIR, &g_perm_arena, world, phys, terrain, &spawn)) {
        arena_temp_end(temp);
        return result;
    }
    result.static_tris = phys->statics.tri_count;

    u64 hash = 14695981039346656037ull;
    Rng rng;
    rng_seed(&rng, 99u);
    for (u32 i = 0; i < 300; i++) {
        Ray ray;
        ray.origin = v3(rng_range(&rng, -360.0f, 360.0f), 80.0f, rng_range(&rng, -360.0f, 360.0f));
        ray.dir = vec3_normalize(v3(rng_range(&rng, -0.3f, 0.3f), -1.0f, rng_range(&rng, -0.3f, 0.3f)));
        PhysRayHit hit;
        if (phys_raycast(phys, ray, 300.0f, &hit)) {
            result.ray_hits++;
            checksum_bytes(&hash, &hit.t, sizeof(hit.t));
            checksum_bytes(&hash, &hit.normal, sizeof(hit.normal));
        }
    }

    Vehicle* veh = arena_push(&g_perm_arena, Vehicle);
    if (!vehicle_init(veh, phys, CAR_CFG_PATH, spawn.car_pos, spawn.car_yaw)) {
        arena_temp_end(temp);
        return result;
    }
    VehicleInput idle = {0};
    car_run_ticks(phys, veh, idle, 360);
    VehicleInput drive = {0};
    drive.throttle = 0.6f;
    car_run_ticks(phys, veh, drive, 480);
    car_checksum(&hash, phys, veh);

    RigidBody* body = phys_body(phys, veh->body);
    result.valid = body_state_valid(body)
                && body->pos.y > terrain->hf.min_height - 2.0f
                && vec3_distance(body->pos, spawn.car_pos) > 3.0f;
    result.checksum = hash;

    arena_temp_end(temp);
    return result;
}

static int run_scene_zone_check(void)
{
    log_info("scene zone_check: %s", ZONE_DIR);
    assets_init(0);
    ZoneCheckResult a = zone_check_run();
    ZoneCheckResult b = zone_check_run();
    log_info("zone_check: run1 checksum %016llx | %u/300 ray hits | %u static tris | drove ok %d",
             (unsigned long long)a.checksum, a.ray_hits, a.static_tris, a.valid);
    log_info("zone_check: run2 checksum %016llx", (unsigned long long)b.checksum);
    b32 pass = a.checksum == b.checksum && a.valid && b.valid
             && a.ray_hits > 250 && a.static_tris > 1000;
    log_info("scene zone_check: %s", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

static void walk_add_quad(PhysWorld* world, Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3)
{
    phys_add_static_tri(world, p0, p1, p2);
    phys_add_static_tri(world, p0, p2, p3);
}

static void walk_add_wall(PhysWorld* world, Vec3 a, Vec3 b, f32 height)
{
    walk_add_quad(world, a, b, vec3_add(b, v3(0.0f, height, 0.0f)), vec3_add(a, v3(0.0f, height, 0.0f)));
}

static void walk_run_ticks(PhysWorld* world, Player* p, Vehicle* veh, PlayerCommand cmd, u32 ticks)
{
    for (u32 i = 0; i < ticks; i++) {
        if (veh) {
            VehicleInput parked = {0};
            parked.handbrake = 1;
            vehicle_set_input(veh, parked);
            vehicle_tick(veh, world, FIXED_DT);
        }
        phys_tick(world, FIXED_DT);
        player_tick(p, world, veh, cmd, FIXED_DT);
        cmd.jump = 0;
        cmd.interact = 0;
    }
}

static void walk_checksum(u64* hash, const Player* p)
{
    checksum_bytes(hash, &p->pos, sizeof(p->pos));
    checksum_bytes(hash, &p->vel, sizeof(p->vel));
    u32 state = (u32)p->state;
    checksum_bytes(hash, &state, sizeof(state));
}

typedef struct WalkTestReport {
    b32 pass;
    u64 checksum;
} WalkTestReport;

static WalkTestReport walk_test_run(b32 verbose)
{
    WalkTestReport report;
    report.pass = 1;
    report.checksum = 14695981039346656037ull;
    PlayerCommand idle = {0};
    PlayerCommand forward = {0};
    forward.move_z = 1.0f;

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        Heightfield* hf = arena_push(&g_perm_arena, Heightfield);
        heightfield_init_procedural(hf, &g_perm_arena, 96, 1.0f, 5u, 0.0f);
        PhysWorld* world = arena_push(&g_perm_arena, PhysWorld);
        phys_init(world, &g_perm_arena, hf);
        phys_statics_reserve(world, &g_perm_arena, 64);
        walk_add_wall(world, v3(-6.0f, 0.0f, -6.0f), v3(6.0f, 0.0f, -6.0f), 3.0f);
        phys_statics_build(world, &g_perm_arena);

        Player* p = arena_push(&g_perm_arena, Player);
        player_init(p, v3(0.0f, 0.0f, 10.0f), 0.0f);
        walk_run_ticks(world, p, 0, forward, 240);
        f32 walked = 10.0f - p->pos.z;
        b32 walk_ok = walked >= 7.0f && walked <= 8.8f && p->grounded && f_abs(p->pos.y) < 0.2f;
        if (verbose) {
            log_info("walk_test walk: %.2f m in 2 s in [7.0,8.8] | grounded %d | %s",
                     (f64)walked, p->grounded, walk_ok ? "PASS" : "FAIL");
        }
        report.pass &= walk_ok;

        PlayerCommand jump = {0};
        jump.jump = 1;
        walk_run_ticks(world, p, 0, jump, 30);
        f32 jump_peak = p->pos.y;
        walk_run_ticks(world, p, 0, idle, 150);
        b32 jump_ok = jump_peak > 0.35f && p->grounded && f_abs(p->pos.y) < 0.1f;
        if (verbose) {
            log_info("walk_test jump: height %.2f m > 0.35 | landed grounded %d | %s",
                     (f64)jump_peak, p->grounded, jump_ok ? "PASS" : "FAIL");
        }
        report.pass &= jump_ok;

        walk_run_ticks(world, p, 0, forward, 600);
        b32 wall_ok = p->pos.z > -6.01f && p->pos.z < -5.2f && f_abs(p->pos.x) < 0.5f;
        if (verbose) {
            log_info("walk_test wall: stopped at z %.2f in (-6.01,-5.2) | %s",
                     (f64)p->pos.z, wall_ok ? "PASS" : "FAIL");
        }
        report.pass &= wall_ok;
        walk_checksum(&report.checksum, p);
        arena_temp_end(temp);
    }

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        Heightfield* hf = arena_push(&g_perm_arena, Heightfield);
        heightfield_init_procedural(hf, &g_perm_arena, 96, 1.0f, 5u, 0.0f);
        PhysWorld* world = arena_push(&g_perm_arena, PhysWorld);
        phys_init(world, &g_perm_arena, hf);
        phys_statics_reserve(world, &g_perm_arena, 64);
        walk_add_quad(world, v3(-4.0f, 0.25f, -4.0f), v3(4.0f, 0.25f, -4.0f),
                      v3(4.0f, 0.25f, -14.0f), v3(-4.0f, 0.25f, -14.0f));
        phys_statics_build(world, &g_perm_arena);

        Player* p = arena_push(&g_perm_arena, Player);
        player_init(p, v3(0.0f, 0.0f, 0.0f), 0.0f);
        walk_run_ticks(world, p, 0, forward, 300);
        f32 on_step_y = p->pos.y;
        f32 on_step_z = p->pos.z;
        walk_run_ticks(world, p, 0, forward, 180);
        b32 step_ok = on_step_y >= 0.2f && on_step_y <= 0.35f && on_step_z < -6.0f
                    && p->pos.z < -14.5f && f_abs(p->pos.y) < 0.1f;
        if (verbose) {
            log_info("walk_test step: on step y %.2f at z %.1f | off step y %.2f at z %.1f | %s",
                     (f64)on_step_y, (f64)on_step_z, (f64)p->pos.y, (f64)p->pos.z,
                     step_ok ? "PASS" : "FAIL");
        }
        report.pass &= step_ok;
        walk_checksum(&report.checksum, p);
        arena_temp_end(temp);
    }

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        Heightfield* hf = arena_push(&g_perm_arena, Heightfield);
        heightfield_init_slope(hf, &g_perm_arena, 120, 1.0f, 0.28f);
        PhysWorld* world = arena_push(&g_perm_arena, PhysWorld);
        phys_init(world, &g_perm_arena, hf);

        Player* p = arena_push(&g_perm_arena, Player);
        Vec3 start = v3(10.0f, heightfield_sample(hf, 10.0f, 0.0f), 0.0f);
        player_init(p, start, PI32 * 0.5f);
        walk_run_ticks(world, p, 0, idle, 360);
        f32 drift = vec3_distance(p->pos, start);
        walk_run_ticks(world, p, 0, forward, 360);
        f32 climb = p->pos.y - start.y;
        b32 slope_ok = drift < 0.05f && climb > 2.0f && p->grounded;
        if (verbose) {
            log_info("walk_test slope: stand drift %.3f m < 0.05 | climbed %.2f m > 2.0 | %s",
                     (f64)drift, (f64)climb, slope_ok ? "PASS" : "FAIL");
        }
        report.pass &= slope_ok;
        walk_checksum(&report.checksum, p);
        arena_temp_end(temp);
    }

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        Heightfield* hf = arena_push(&g_perm_arena, Heightfield);
        heightfield_init_slope(hf, &g_perm_arena, 120, 1.0f, 0.28f);
        PhysWorld* world = arena_push(&g_perm_arena, PhysWorld);
        phys_init(world, &g_perm_arena, hf);
        Vehicle* veh = arena_push(&g_perm_arena, Vehicle);
        f32 car_x = 20.0f;
        if (!vehicle_init(veh, world, CAR_CFG_PATH,
                          v3(car_x, car_x * 0.28f + 0.9f, 0.0f), -PI32 * 0.5f)) {
            report.pass = 0;
            arena_temp_end(temp);
            return report;
        }
        Player* p = arena_push(&g_perm_arena, Player);
        player_init(p, v3(10.0f, heightfield_sample(hf, 10.0f, 0.0f), 6.0f), 0.0f);
        walk_run_ticks(world, p, veh, idle, 360);

        RigidBody* body = phys_body(world, veh->body);
        Vec3 door = vec3_add(body->pos, quat_rotate_vec3(body->rot, v3(-2.0f, 0.0f, -0.3f)));
        p->pos = v3(door.x, heightfield_sample(hf, door.x, door.z), door.z);
        p->prev_pos = p->pos;
        p->vel = vec3_zero();

        PlayerCommand interact = {0};
        interact.interact = 1;
        walk_run_ticks(world, p, veh, interact, 120);
        b32 entered = p->state == PLAYER_DRIVING;
        walk_run_ticks(world, p, veh, interact, 120);
        f32 horiz_dist = vec3_length(v3(p->pos.x - body->pos.x, 0.0f, p->pos.z - body->pos.z));
        f32 ground_err = f_abs(p->pos.y - heightfield_sample(hf, p->pos.x, p->pos.z));
        b32 exited = p->state == PLAYER_ON_FOOT && p->grounded && horiz_dist > 1.2f && ground_err < 0.4f;
        b32 cycle_ok = entered && exited;
        if (verbose) {
            log_info("walk_test enter/exit slope: entered %d | exited %d at %.2f m from car, ground err %.2f | %s",
                     entered, exited, (f64)horiz_dist, (f64)ground_err, cycle_ok ? "PASS" : "FAIL");
        }
        report.pass &= cycle_ok;
        walk_checksum(&report.checksum, p);
        arena_temp_end(temp);
    }

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        Heightfield* hf = arena_push(&g_perm_arena, Heightfield);
        heightfield_init_procedural(hf, &g_perm_arena, 96, 1.0f, 5u, 0.0f);
        PhysWorld* world = arena_push(&g_perm_arena, PhysWorld);
        phys_init(world, &g_perm_arena, hf);
        phys_statics_reserve(world, &g_perm_arena, 64);
        walk_add_wall(world, v3(-1.95f, 0.0f, -3.5f), v3(-1.95f, 0.0f, 3.5f), 2.5f);
        walk_add_wall(world, v3(1.95f, 0.0f, -3.5f), v3(1.95f, 0.0f, 3.5f), 2.5f);
        walk_add_wall(world, v3(-2.0f, 0.0f, -3.1f), v3(2.0f, 0.0f, -3.1f), 2.5f);
        walk_add_wall(world, v3(-2.0f, 0.0f, 3.1f), v3(2.0f, 0.0f, 3.1f), 2.5f);
        phys_statics_build(world, &g_perm_arena);
        Vehicle* veh = arena_push(&g_perm_arena, Vehicle);
        if (!vehicle_init(veh, world, CAR_CFG_PATH, v3(0.0f, 0.9f, 0.0f), 0.0f)) {
            report.pass = 0;
            arena_temp_end(temp);
            return report;
        }
        Player* p = arena_push(&g_perm_arena, Player);
        player_init(p, v3(1.45f, 0.0f, 0.5f), 0.0f);
        walk_run_ticks(world, p, veh, idle, 240);

        PlayerCommand interact = {0};
        interact.interact = 1;
        walk_run_ticks(world, p, veh, interact, 120);
        b32 entered = p->state == PLAYER_DRIVING;
        walk_run_ticks(world, p, veh, interact, 120);
        b32 blocked_ok = entered && p->state == PLAYER_DRIVING;
        if (verbose) {
            log_info("walk_test blocked exit: entered %d | still driving %d | %s",
                     entered, p->state == PLAYER_DRIVING, blocked_ok ? "PASS" : "FAIL");
        }
        report.pass &= blocked_ok;
        walk_checksum(&report.checksum, p);
        arena_temp_end(temp);
    }

    return report;
}

typedef struct FailRig {
    PhysWorld* world;
    Vehicle* veh;
    CarSys* sys;
    RigidBody* body;
} FailRig;

static b32 fail_rig_init(FailRig* rig)
{
    Heightfield* hf = arena_push(&g_perm_arena, Heightfield);
    heightfield_init_procedural(hf, &g_perm_arena, 400, 2.0f, 3u, 0.0f);
    rig->world = arena_push(&g_perm_arena, PhysWorld);
    phys_init(rig->world, &g_perm_arena, hf);
    rig->veh = arena_push(&g_perm_arena, Vehicle);
    if (!vehicle_init(rig->veh, rig->world, CAR_CFG_PATH, v3(0.0f, 0.9f, 0.0f), -PI32 * 0.5f)) {
        return 0;
    }
    rig->sys = arena_push(&g_perm_arena, CarSys);
    carsys_init(rig->sys);
    rig->body = phys_body(rig->world, rig->veh->body);
    return 1;
}

static void fail_run(FailRig* rig, VehicleInput input, u32 ticks)
{
    for (u32 i = 0; i < ticks; i++) {
        vehicle_set_input(rig->veh, input);
        carsys_tick(rig->sys, rig->veh, rig->world, FIXED_DT);
        vehicle_tick(rig->veh, rig->world, FIXED_DT);
        phys_tick(rig->world, FIXED_DT);
    }
}

static b32 fail_start_engine(FailRig* rig)
{
    VehicleInput idle = {0};
    fail_run(rig, idle, 240);
    carsys_try_start(rig->sys, rig->veh);
    fail_run(rig, idle, 150);
    return rig->sys->engine_on;
}

static void fail_checksum(u64* hash, const FailRig* rig)
{
    for (u32 i = 0; i < PART_COUNT; i++) {
        checksum_bytes(hash, &rig->sys->parts[i].condition, sizeof(f32));
        checksum_bytes(hash, &rig->sys->parts[i].installed, sizeof(b32));
    }
    checksum_bytes(hash, &rig->sys->elec.battery_charge, sizeof(f32));
    checksum_bytes(hash, &rig->sys->fluids, sizeof(Fluids));
    checksum_bytes(hash, &rig->sys->engine_on, sizeof(b32));
    checksum_bytes(hash, &rig->body->pos, sizeof(Vec3));
    checksum_bytes(hash, &rig->body->vel, sizeof(Vec3));
}

typedef struct FailReport {
    b32 pass;
    u64 checksum;
} FailReport;

static b32 fail_check(FailReport* report, b32 ok, b32 verbose, const char* name, const char* detail)
{
    if (verbose) {
        log_info("failure_matrix %-18s %s | %s", name, ok ? "PASS" : "FAIL", detail);
    }
    report->pass &= ok;
    return ok;
}

static FailReport failure_matrix_run(b32 verbose)
{
    FailReport report;
    report.pass = 1;
    report.checksum = 14695981039346656037ull;
    VehicleInput idle = {0};
    VehicleInput full = {0};
    full.throttle = 1.0f;
    char detail[128];

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        FailRig rig;
        if (!fail_rig_init(&rig)) {
            report.pass = 0;
            arena_temp_end(temp);
            return report;
        }
        b32 started = fail_start_engine(&rig);
        fail_run(&rig, full, 480);
        f32 speed = f_abs(vehicle_forward_speed(rig.veh, rig.world));
        snprintf(detail, sizeof(detail), "started %d, speed %.1f m/s > 10", started, (f64)speed);
        fail_check(&report, started && speed > 10.0f, verbose, "baseline", detail);
        fail_checksum(&report.checksum, &rig);
        arena_temp_end(temp);
    }

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        FailRig rig;
        fail_rig_init(&rig);
        fail_start_engine(&rig);
        rig.sys->parts[PART_ENGINE].condition = 0.0f;
        fail_run(&rig, idle, 60);
        b32 stalled = !rig.sys->engine_on;
        carsys_try_start(rig.sys, rig.veh);
        fail_run(&rig, idle, 150);
        snprintf(detail, sizeof(detail), "stalled %d, restart blocked %d", stalled, !rig.sys->engine_on);
        fail_check(&report, stalled && !rig.sys->engine_on, verbose, "engine dead", detail);
        fail_checksum(&report.checksum, &rig);
        arena_temp_end(temp);
    }

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        FailRig rig;
        fail_rig_init(&rig);
        rig.sys->elec.battery_charge = 0.0f;
        VehicleInput quiet = {0};
        fail_run(&rig, quiet, 240);
        carsys_try_start(rig.sys, rig.veh);
        fail_run(&rig, quiet, 150);
        b32 no_crank = !rig.sys->engine_on;

        FailRig rig2;
        fail_rig_init(&rig2);
        b32 started = fail_start_engine(&rig2);
        rig2.sys->elec.battery_charge = 0.0f;
        fail_run(&rig2, idle, 240);
        b32 kept_running = rig2.sys->engine_on;
        snprintf(detail, sizeof(detail), "dead battery no crank %d, alternator keeps engine %d",
                 no_crank, started && kept_running);
        fail_check(&report, no_crank && started && kept_running, verbose, "battery", detail);
        fail_checksum(&report.checksum, &rig);
        fail_checksum(&report.checksum, &rig2);
        arena_temp_end(temp);
    }

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        FailRig rig;
        fail_rig_init(&rig);
        fail_start_engine(&rig);
        rig.sys->parts[PART_ALTERNATOR].condition = 0.0f;
        rig.sys->headlight_switch = 1;
        f32 charge_before = rig.sys->elec.battery_charge;
        fail_run(&rig, idle, 1200);
        f32 drained = charge_before - rig.sys->elec.battery_charge;
        snprintf(detail, sizeof(detail), "drained %.4f > 0.0008 in 10 s", (f64)drained);
        fail_check(&report, drained > 0.0008f, verbose, "alternator dead", detail);
        fail_checksum(&report.checksum, &rig);
        arena_temp_end(temp);
    }

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        FailRig rig;
        fail_rig_init(&rig);
        fail_start_engine(&rig);
        rig.sys->parts[PART_RADIATOR].condition = 0.0f;
        fail_run(&rig, full, 3600);
        f32 temp_c = rig.sys->fluids.coolant_temp;
        f32 power = rig.veh->effects.engine_power_mul;
        f32 engine_cond = rig.sys->parts[PART_ENGINE].condition;
        snprintf(detail, sizeof(detail), "temp %.0fC > 112, power_mul %.2f < 0.95, engine cond %.2f < 1",
                 (f64)temp_c, (f64)power, (f64)engine_cond);
        fail_check(&report, temp_c > COOLANT_OVERHEAT_C && power < 0.95f && engine_cond < 1.0f,
                   verbose, "radiator dead", detail);
        fail_checksum(&report.checksum, &rig);
        arena_temp_end(temp);
    }

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        FailRig rig;
        fail_rig_init(&rig);
        fail_start_engine(&rig);
        rig.sys->fluids.fuel = 0.0015f;
        fail_run(&rig, full, 1200);
        b32 stalled = !rig.sys->engine_on && rig.sys->fluids.fuel <= 0.0f;
        carsys_try_start(rig.sys, rig.veh);
        fail_run(&rig, idle, 150);
        snprintf(detail, sizeof(detail), "ran dry and stalled %d, restart blocked %d",
                 stalled, !rig.sys->engine_on);
        fail_check(&report, stalled && !rig.sys->engine_on, verbose, "fuel empty", detail);
        fail_checksum(&report.checksum, &rig);
        arena_temp_end(temp);
    }

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        FailRig rig;
        fail_rig_init(&rig);
        fail_start_engine(&rig);
        rig.sys->parts[PART_TIRE_FL].condition = 0.0f;
        rig.sys->parts[PART_TIRE_RR].installed = 0;
        fail_run(&rig, idle, 2);
        f32 flat_radius = rig.veh->effects.tire_radius_mul[WHEEL_FL];
        f32 flat_grip = rig.veh->effects.tire_grip_mul[WHEEL_FL];
        f32 rim_radius = rig.veh->effects.tire_radius_mul[WHEEL_RR];
        snprintf(detail, sizeof(detail), "flat radius %.2f < 0.9, flat grip %.2f < 0.6, rim radius %.2f < 0.7",
                 (f64)flat_radius, (f64)flat_grip, (f64)rim_radius);
        fail_check(&report, flat_radius < 0.9f && flat_grip < 0.6f && rim_radius < 0.7f,
                   verbose, "tires", detail);
        fail_checksum(&report.checksum, &rig);
        arena_temp_end(temp);
    }

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        FailRig rig;
        fail_rig_init(&rig);
        fail_start_engine(&rig);
        rig.sys->headlight_switch = 1;
        fail_run(&rig, idle, 2);
        b32 lights_on = rig.veh->effects.headlights_on;
        rig.sys->elec.fuse_ok[CONSUMER_HEADLIGHTS] = 0;
        fail_run(&rig, idle, 2);
        b32 lights_off = !rig.veh->effects.headlights_on;
        snprintf(detail, sizeof(detail), "on with fuse %d, off when blown %d", lights_on, lights_off);
        fail_check(&report, lights_on && lights_off, verbose, "headlight fuse", detail);
        fail_checksum(&report.checksum, &rig);
        arena_temp_end(temp);
    }

    {
        ArenaTemp temp = arena_temp_begin(&g_perm_arena);
        FailRig rig;
        fail_rig_init(&rig);
        phys_statics_reserve(rig.world, &g_perm_arena, 16);
        walk_add_wall(rig.world, v3(26.0f, 0.0f, 6.0f), v3(26.0f, 0.0f, -6.0f), 3.0f);
        phys_statics_build(rig.world, &g_perm_arena);
        fail_start_engine(&rig);
        fail_run(&rig, full, 720);
        f32 radiator_cond = rig.sys->parts[PART_RADIATOR].condition;
        f32 speed = vec3_length(rig.body->vel);
        snprintf(detail, sizeof(detail), "crashed to %.1f m/s, radiator cond %.2f < 0.9, severity %.2f",
                 (f64)speed, (f64)radiator_cond, (f64)rig.sys->last_impact_severity);
        fail_check(&report, radiator_cond < 0.9f && rig.sys->last_impact_severity > 0.0f,
                   verbose, "crash damage", detail);
        fail_checksum(&report.checksum, &rig);
        arena_temp_end(temp);
    }

    return report;
}

static int run_scene_failure_matrix(void)
{
    log_info("scene failure_matrix: carsys parts/electrics/fluids at dt=%.5f", (f64)FIXED_DT);
    FailReport a = failure_matrix_run(1);
    FailReport b = failure_matrix_run(0);
    b32 deterministic = a.checksum == b.checksum;
    log_info("failure_matrix determinism: run1 %016llx run2 %016llx | %s",
             (unsigned long long)a.checksum, (unsigned long long)b.checksum,
             deterministic ? "PASS" : "FAIL");
    b32 pass = a.pass && b.pass && deterministic;
    log_info("scene failure_matrix: %s", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

static int run_scene_walk_test(void)
{
    log_info("scene walk_test: capsule controller + enter/exit at dt=%.5f", (f64)FIXED_DT);
    WalkTestReport a = walk_test_run(1);
    WalkTestReport b = walk_test_run(0);
    b32 deterministic = a.checksum == b.checksum;
    log_info("walk_test determinism: run1 %016llx run2 %016llx | %s",
             (unsigned long long)a.checksum, (unsigned long long)b.checksum,
             deterministic ? "PASS" : "FAIL");
    b32 pass = a.pass && b.pass && deterministic;
    log_info("scene walk_test: %s", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}

int main(int argc, char** argv)
{
    arena_init(&g_perm_arena, GIGABYTES(1));
    arena_init(&g_frame_arena, MEGABYTES(64));

#if defined(_DEBUG)
    math_selftest();
    config_selftest();
    assets_selftest();
#endif

    const char* scene = 0;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--scene=", 8) == 0) {
            scene = argv[i] + 8;
        }
    }
    if (scene) {
        if (strcmp(scene, "phys") == 0) {
            return run_scene_phys();
        }
        if (strcmp(scene, "car_test") == 0) {
            return run_scene_car_test();
        }
        if (strcmp(scene, "zone_check") == 0) {
            return run_scene_zone_check();
        }
        if (strcmp(scene, "walk_test") == 0) {
            return run_scene_walk_test();
        }
        if (strcmp(scene, "failure_matrix") == 0) {
            return run_scene_failure_matrix();
        }
        log_error("unknown scene: %s", scene);
        return 1;
    }

    if (!platform_init("Anomalous", 1600, 900)) {
        return 1;
    }
    if (!r_init() || !dd_init() || !text_init("assets/fonts/mono.ttf")) {
        platform_shutdown();
        return 1;
    }
    assets_init(1);

    world_init(&s_world, &g_perm_arena);
    phys_init(&s_phys, &g_perm_arena, &s_terrain.hf);
    if (!zone_load(ZONE_DIR, &g_perm_arena, &s_world, &s_phys, &s_terrain, &s_zone_spawn)) {
        platform_shutdown();
        return 1;
    }
    if (!terrain_render_init(&s_terrain.hf, s_terrain.roadmask, s_terrain.mask_size)) {
        platform_shutdown();
        return 1;
    }
    if (!vehicle_init(&s_vehicle, &s_phys, CAR_CFG_PATH, s_zone_spawn.car_pos, s_zone_spawn.car_yaw)) {
        platform_shutdown();
        return 1;
    }

    player_init(&s_player, s_zone_spawn.player_pos, s_zone_spawn.player_yaw);
    carsys_init(&s_carsys);
    interact_init(&s_interact);
    spawn_spares();

    camera_init(&s_camera, vec3_add(s_zone_spawn.car_pos, v3(-8.0f, 5.0f, 10.0f)));
    camera_look_at(&s_camera, s_zone_spawn.car_pos);

    f64 prev_time = platform_time_now();
    f64 accumulator = 0.0;
    f64 fps_timer = 0.0;
    f64 next_cfg_poll = 0.0;
    u32 fps_frame_count = 0;

    while (!platform_should_close()) {
        arena_reset(&g_frame_arena);

        f64 now = platform_time_now();
        f64 frame_dt = now - prev_time;
        prev_time = now;
        if (frame_dt > MAX_FRAME_DT) {
            frame_dt = MAX_FRAME_DT;
        }

        platform_poll_input();
        const GameInput* input = platform_input();
        if (input->key_pressed[KEY_ESCAPE]) {
            platform_request_close();
        }
        if (input->key_pressed[KEY_R]) {
            reset_car();
        }
        if (input->key_pressed[KEY_F1]) {
            s_show_telemetry = !s_show_telemetry;
        }
        if (input->key_pressed[KEY_F2]) {
            s_show_phys_debug = !s_show_phys_debug;
        }
        if (input->key_pressed[KEY_F3]) {
            s_show_collision = !s_show_collision;
        }
        if (input->key_pressed[KEY_F4]) {
            s_show_carsys = !s_show_carsys;
        }
        if (input->key_pressed[KEY_F5]) {
            s_slow_mo = !s_slow_mo;
        }
        if (input->key_pressed[KEY_F6]) {
            s_free_cam = !s_free_cam;
        }
        if (input->key_pressed[KEY_F7]) {
            s_show_tuning = !s_show_tuning;
        }
        if (input->key_pressed[KEY_C]) {
            s_chase_cam = !s_chase_cam;
        }

        PlayerCommand frame_cmd = {0};
        if (s_free_cam) {
            camera_fly_update(&s_camera, input, (f32)frame_dt);
            VehicleInput coast = {0};
            vehicle_set_input(&s_vehicle, coast);
            s_pending_jump = 0;
            s_pending_interact = 0;
        } else {
            platform_set_cursor_captured(!s_show_tuning);
            if (platform_cursor_captured()) {
                player_look(&s_player, input->mouse_dx, input->mouse_dy);
            }
            if (player_driving(&s_player)) {
                f32 forward_intent = input->key_down[KEY_W] ? 1.0f : 0.0f;
                f32 reverse_intent = input->key_down[KEY_S] ? 1.0f : 0.0f;
                f32 steer = (input->key_down[KEY_D] ? 1.0f : 0.0f) - (input->key_down[KEY_A] ? 1.0f : 0.0f);
                b32 handbrake = input->key_down[KEY_SPACE] || s_carsys.handbrake_latched;
                vehicle_driver_input(&s_vehicle, &s_phys, forward_intent, reverse_intent, steer, handbrake);
                if (input->key_pressed[KEY_F]) {
                    if (s_carsys.engine_on) {
                        carsys_stop_engine(&s_carsys);
                    } else {
                        carsys_try_start(&s_carsys, &s_vehicle);
                    }
                }
                if (input->key_pressed[KEY_L]) {
                    s_carsys.headlight_switch = !s_carsys.headlight_switch;
                }
            } else {
                VehicleInput parked = {0};
                parked.handbrake = s_carsys.handbrake_latched;
                vehicle_set_input(&s_vehicle, parked);
                frame_cmd.move_x = (input->key_down[KEY_D] ? 1.0f : 0.0f) - (input->key_down[KEY_A] ? 1.0f : 0.0f);
                frame_cmd.move_z = (input->key_down[KEY_W] ? 1.0f : 0.0f) - (input->key_down[KEY_S] ? 1.0f : 0.0f);
                frame_cmd.run = input->key_down[KEY_LEFT_SHIFT];
                if (input->key_pressed[KEY_SPACE]) {
                    s_pending_jump = 1;
                }
                if (input->key_pressed[KEY_G]) {
                    interact_drop(&s_interact, &s_world, &s_phys, s_player.pos, s_player.yaw);
                }
            }
            Ray view_ray;
            view_ray.origin = s_camera.pos;
            view_ray.dir = camera_forward(&s_camera);
            interact_update(&s_interact, &s_player, &s_vehicle, &s_carsys, &s_world, &s_phys,
                            view_ray, input->key_down[KEY_E], input->key_pressed[KEY_E],
                            (f32)frame_dt);
            s_player.speed_mul = f_max(1.0f - item_mass(s_interact.hands.kind) * 0.012f, 0.6f);
            if (input->key_pressed[KEY_E]) {
                if (s_interact.action == ACTION_ENTER_CAR) {
                    s_pending_interact = 1;
                } else if (s_interact.action == ACTION_EXIT_CAR) {
                    s_player.exit_pref = s_interact.target_side == 0 ? -1 : 1;
                    s_pending_interact = 1;
                }
            }
        }

        r_hot_reload_poll(now);
        assets_hot_reload_poll(now);
        if (now >= next_cfg_poll) {
            next_cfg_poll = now + 1.0;
            vehicle_poll_config_reload(&s_vehicle, &s_phys);
        }

        accumulator += frame_dt * (s_slow_mo ? 0.1 : 1.0);
        while (accumulator >= FIXED_DT) {
            PlayerCommand cmd = frame_cmd;
            cmd.jump = s_pending_jump;
            cmd.interact = s_pending_interact;
            s_pending_jump = 0;
            s_pending_interact = 0;
            game_tick(FIXED_DT, cmd);
            accumulator -= FIXED_DT;
        }

        RigidBody* car_body = phys_body(&s_phys, s_vehicle.body);
        if (car_body && (car_body->pos.y < s_terrain.hf.min_height - 15.0f || !body_state_valid(car_body))) {
            reset_car();
        }
        if (s_player.state == PLAYER_ON_FOOT && s_player.pos.y < s_terrain.hf.min_height - 15.0f) {
            player_init(&s_player, s_zone_spawn.player_pos, s_zone_spawn.player_yaw);
        }

        f32 alpha = (f32)(accumulator / FIXED_DT);
        if (!s_free_cam) {
            if (s_chase_cam && player_driving(&s_player)) {
                update_chase_camera((f32)frame_dt, alpha);
            } else {
                player_camera(&s_player, &s_phys, &s_vehicle, alpha, (f32)frame_dt, &s_camera);
            }
        }
        update_headlights(alpha);
        game_render(alpha, input);
        platform_swap_buffers();

        fps_frame_count++;
        fps_timer += frame_dt;
        if (fps_timer >= 0.5) {
            s_fps = (f32)((f64)fps_frame_count / fps_timer);
            char title[128];
            snprintf(title, sizeof(title), "Anomalous | %.0f fps | tick %llu",
                     (f64)s_fps, (unsigned long long)s_game.tick_count);
            platform_set_title(title);
            fps_timer = 0.0;
            fps_frame_count = 0;
        }
    }

    terrain_render_shutdown();
    assets_shutdown();
    text_shutdown();
    dd_shutdown();
    r_shutdown();
    platform_shutdown();
    arena_release(&g_frame_arena);
    arena_release(&g_perm_arena);
    return 0;
}
