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
#include "physics/heightfield.h"
#include "physics/physics.h"
#include "vehicle/vehicle.h"
#include "ui/ui.h"

#include <stdio.h>
#include <string.h>

#define FIXED_DT (1.0f / 120.0f)
#define MAX_FRAME_DT 0.25
#define TERRAIN_SIZE 96
#define TERRAIN_CELL 1.0f
#define TERRAIN_SEED 1234u
#define TERRAIN_ROUGHNESS 0.35f
#define WAGON_CFG_PATH "assets/cars/wagon.cfg"
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
static Heightfield s_heightfield;
static PhysWorld s_phys;
static Vehicle s_vehicle;
static Camera s_camera;
static Rng s_spawn_rng;
static f32 s_fps;
static b32 s_show_telemetry = 1;
static b32 s_show_phys_debug;
static b32 s_show_tuning;
static b32 s_slow_mo;
static b32 s_free_cam;
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

static Vec3 vehicle_spawn_pos(const Heightfield* hf, f32 x, f32 z)
{
    return v3(x, heightfield_sample(hf, x, z) + 1.0f, z);
}

static void reset_car(void)
{
    vehicle_teleport(&s_vehicle, &s_phys, vehicle_spawn_pos(&s_heightfield, 0.0f, 0.0f), 0.0f);
}

static void game_tick(f32 dt)
{
    vehicle_tick(&s_vehicle, &s_phys, dt);
    phys_tick(&s_phys, dt);

    telem_push(&s_telem_rpm, drivetrain_rpm(&s_vehicle.train));
    telem_push(&s_telem_slip, (s_vehicle.wheels[WHEEL_RL].slip_ratio + s_vehicle.wheels[WHEEL_RR].slip_ratio) * 0.5f);
    telem_push(&s_telem_load, s_vehicle.wheels[WHEEL_FL].load * 0.001f);
    telem_push(&s_telem_comp, s_vehicle.wheels[WHEEL_FL].compression);
    telem_push(&s_telem_speed, f_abs(vehicle_forward_speed(&s_vehicle, &s_phys)) * 3.6f);

    s_game.tick_count++;
}

static void draw_terrain(void)
{
    const Heightfield* hf = &s_heightfield;
    u32 minor = dd_rgba(40, 70, 50, 255);
    u32 major = dd_rgba(60, 110, 75, 255);
    for (u32 iz = 0; iz < hf->size_z; iz++) {
        u32 color = (iz % 8 == 0) ? major : minor;
        for (u32 ix = 0; ix + 1 < hf->size_x; ix++) {
            Vec3 a = v3(hf->origin.x + (f32)ix * hf->cell_size, heightfield_height_at(hf, ix, iz),
                        hf->origin.z + (f32)iz * hf->cell_size);
            Vec3 b = v3(hf->origin.x + (f32)(ix + 1) * hf->cell_size, heightfield_height_at(hf, ix + 1, iz),
                        hf->origin.z + (f32)iz * hf->cell_size);
            dd_line(a, b, color);
        }
    }
    for (u32 ix = 0; ix < hf->size_x; ix++) {
        u32 color = (ix % 8 == 0) ? major : minor;
        for (u32 iz = 0; iz + 1 < hf->size_z; iz++) {
            Vec3 a = v3(hf->origin.x + (f32)ix * hf->cell_size, heightfield_height_at(hf, ix, iz),
                        hf->origin.z + (f32)iz * hf->cell_size);
            Vec3 b = v3(hf->origin.x + (f32)ix * hf->cell_size, heightfield_height_at(hf, ix, iz + 1),
                        hf->origin.z + (f32)(iz + 1) * hf->cell_size);
            dd_line(a, b, color);
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
        Handle handle = { idx, s_phys.bodies.gens[idx] };
        if (handle.idx == s_vehicle.body.idx) {
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
}

static void draw_status_hud(void)
{
    f32 line_height = text_line_height(16.0f);
    f32 y = 8.0f + line_height;
    dd_text_2d(12.0f, y, 16.0f, DD_WHITE, "%.0f fps | tick %llu%s%s",
               (f64)s_fps, (unsigned long long)s_game.tick_count,
               s_slow_mo ? " | SLOW-MO 0.1x" : "",
               s_free_cam ? " | FREE CAM" : "");
    y += line_height;
    dd_text_2d(12.0f, y, 16.0f, DD_GRAY,
               "wasd drive | space handbrake | r reset | f1 telemetry | f2 forces | f5 slow-mo | f6 free cam | f7 tuning | esc quit");
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
    f32 terrain_floor = heightfield_sample(&s_heightfield, target.x, target.z) + 0.5f;
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

    draw_terrain();
    vehicle_debug_draw(&s_vehicle, &s_phys, alpha, s_show_phys_debug);
    draw_phys_debug(alpha);
    draw_mouse_ray(input);
    draw_status_hud();
    draw_drive_hud();
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
    heightfield_init_procedural(hf, &g_perm_arena, TERRAIN_SIZE, TERRAIN_CELL, SCENE_PHYS_SEED, 1.0f);
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
        if (!vehicle_init(veh, world, WAGON_CFG_PATH, v3(-150.0f, 0.9f, 0.0f), -PI32 * 0.5f)) {
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
        vehicle_init(veh, world, WAGON_CFG_PATH, v3(-150.0f, 0.9f, 0.0f), -PI32 * 0.5f);
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
        vehicle_init(veh, world, WAGON_CFG_PATH,
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
    log_info("scene car_test: scripted maneuvers at dt=%.5f, car %s", (f64)FIXED_DT, WAGON_CFG_PATH);
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

int main(int argc, char** argv)
{
    arena_init(&g_perm_arena, GIGABYTES(1));
    arena_init(&g_frame_arena, MEGABYTES(64));

#if defined(_DEBUG)
    math_selftest();
    config_selftest();
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

    heightfield_init_procedural(&s_heightfield, &g_perm_arena, TERRAIN_SIZE, TERRAIN_CELL, TERRAIN_SEED, TERRAIN_ROUGHNESS);
    phys_init(&s_phys, &g_perm_arena, &s_heightfield);
    rng_seed(&s_spawn_rng, 42u);
    if (!vehicle_init(&s_vehicle, &s_phys, WAGON_CFG_PATH,
                      vehicle_spawn_pos(&s_heightfield, 0.0f, 0.0f), 0.0f)) {
        platform_shutdown();
        return 1;
    }

    camera_init(&s_camera, v3(0.0f, 8.0f, 14.0f));
    camera_look_at(&s_camera, v3(0.0f, 1.0f, 0.0f));

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
        if (input->key_pressed[KEY_F5]) {
            s_slow_mo = !s_slow_mo;
        }
        if (input->key_pressed[KEY_F6]) {
            s_free_cam = !s_free_cam;
        }
        if (input->key_pressed[KEY_F7]) {
            s_show_tuning = !s_show_tuning;
        }

        if (s_free_cam) {
            camera_fly_update(&s_camera, input, (f32)frame_dt);
            VehicleInput coast = {0};
            vehicle_set_input(&s_vehicle, coast);
        } else {
            f32 forward_intent = input->key_down[KEY_W] ? 1.0f : 0.0f;
            f32 reverse_intent = input->key_down[KEY_S] ? 1.0f : 0.0f;
            f32 steer = (input->key_down[KEY_D] ? 1.0f : 0.0f) - (input->key_down[KEY_A] ? 1.0f : 0.0f);
            b32 handbrake = input->key_down[KEY_SPACE];
            vehicle_driver_input(&s_vehicle, &s_phys, forward_intent, reverse_intent, steer, handbrake);
        }

        r_hot_reload_poll(now);
        if (now >= next_cfg_poll) {
            next_cfg_poll = now + 1.0;
            vehicle_poll_config_reload(&s_vehicle, &s_phys);
        }

        accumulator += frame_dt * (s_slow_mo ? 0.1 : 1.0);
        while (accumulator >= FIXED_DT) {
            game_tick(FIXED_DT);
            accumulator -= FIXED_DT;
        }

        RigidBody* car_body = phys_body(&s_phys, s_vehicle.body);
        if (car_body && (car_body->pos.y < s_heightfield.min_height - 15.0f || !body_state_valid(car_body))) {
            reset_car();
        }

        f32 alpha = (f32)(accumulator / FIXED_DT);
        if (!s_free_cam) {
            update_chase_camera((f32)frame_dt, alpha);
        }
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

    text_shutdown();
    dd_shutdown();
    r_shutdown();
    platform_shutdown();
    arena_release(&g_frame_arena);
    arena_release(&g_perm_arena);
    return 0;
}
