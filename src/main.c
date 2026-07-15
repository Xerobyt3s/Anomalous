#include "core/types.h"
#include "core/arena.h"
#include "core/log.h"
#include "core/rng.h"
#include "math/vmath.h"
#include "platform/platform.h"
#include "render/camera.h"
#include "render/render.h"
#include "render/debug_draw.h"
#include "render/text.h"
#include "physics/heightfield.h"
#include "physics/physics.h"

#include <stdio.h>
#include <string.h>

#define FIXED_DT (1.0f / 120.0f)
#define MAX_FRAME_DT 0.25
#define TERRAIN_SIZE 96
#define TERRAIN_CELL 1.0f
#define TERRAIN_SEED 1234u
#define PLAYGROUND_SPAWN_SEED 42u
#define PLAYGROUND_BOX_COUNT 14
#define BOX_DENSITY 200.0f
#define SCENE_PHYS_SEED 1234u
#define SCENE_PHYS_BODIES 24
#define SCENE_PHYS_TICKS 1440

typedef struct GameState {
    u64 tick_count;
} GameState;

static GameState s_game;
static Heightfield s_heightfield;
static PhysWorld s_phys;
static Camera s_camera;
static Rng s_spawn_rng;
static f32 s_fps;
static b32 s_show_phys_debug;
static b32 s_slow_mo;

static BodyHandle spawn_box(PhysWorld* world, Vec3 pos, Quat rot, Vec3 half_extents, Vec3 vel)
{
    f32 mass = BOX_DENSITY * 8.0f * half_extents.x * half_extents.y * half_extents.z;
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

static void reset_playground(void)
{
    for (u32 idx = 0; idx < s_phys.bodies.capacity; idx++) {
        if (pool_at(&s_phys.bodies, idx)) {
            Handle handle = { idx, s_phys.bodies.gens[idx] };
            phys_body_destroy(&s_phys, handle);
        }
    }
    rng_seed(&s_spawn_rng, PLAYGROUND_SPAWN_SEED);
    for (i32 i = 0; i < PLAYGROUND_BOX_COUNT; i++) {
        Vec3 pos = v3(rng_range(&s_spawn_rng, -12.0f, 12.0f),
                      rng_range(&s_spawn_rng, 6.0f, 16.0f),
                      rng_range(&s_spawn_rng, -12.0f, 12.0f));
        spawn_random_box(&s_phys, &s_spawn_rng, pos, vec3_zero());
    }
}

static void game_tick(f32 dt)
{
    phys_tick(&s_phys, dt);
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

static void draw_bodies(f32 alpha)
{
    for (u32 idx = 0; idx < s_phys.bodies.capacity; idx++) {
        RigidBody* body = pool_at(&s_phys.bodies, idx);
        if (!body) {
            continue;
        }
        Vec3 pos = vec3_lerp(body->prev_pos, body->pos, alpha);
        Quat rot = quat_slerp(body->prev_rot, body->rot, alpha);
        f32 speed = vec3_length(body->vel);
        u32 color = speed > 0.4f ? DD_WHITE : dd_rgba(130, 140, 150, 255);
        dd_obb(pos, rot, body->half_extents, color);
        if (s_show_phys_debug) {
            Mat3 m = quat_to_mat3(rot);
            for (u32 i = 0; i < body->sphere_count; i++) {
                Vec3 center = vec3_add(pos, mat3_mul_vec3(m, body->sphere_offsets[i]));
                dd_sphere(center, body->sphere_radius, dd_rgba(150, 100, 50, 255));
            }
            if (speed > 0.1f) {
                dd_arrow(pos, vec3_add(pos, vec3_scale(body->vel, 0.3f)), 0.15f, DD_CYAN);
            }
        }
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
        dd_text_3d(vec3_add(hit.point, v3(0.0f, 1.2f, 0.0f)), 16.0f, DD_MAGENTA, "t=%.1f", hit.t);
    }
}

static void draw_hud(void)
{
    f32 line_height = text_line_height(18.0f);
    f32 y = 8.0f + line_height;
    dd_text_2d(12.0f, y, 18.0f, DD_WHITE, "%.0f fps | tick %llu | bodies %u | contacts %u%s",
               s_fps, (unsigned long long)s_game.tick_count,
               s_phys.bodies.count, s_phys.contact_count,
               s_slow_mo ? " | SLOW-MO 0.1x" : "");
    y += line_height;
    dd_text_2d(12.0f, y, 18.0f, DD_GRAY, "cam (%.1f, %.1f, %.1f) speed %.1f",
               s_camera.pos.x, s_camera.pos.y, s_camera.pos.z, s_camera.move_speed);
    y += line_height;
    dd_text_2d(12.0f, y, 18.0f, DD_GRAY,
               "rmb look | wasd+qe move | shift fast | f throw | r reset | lmb raycast | f2 phys debug | f5 slow-mo | esc quit");
}

static void game_render(f32 alpha, const GameInput* input)
{
    r_begin_frame(&s_camera);
    dd_begin_frame();
    text_begin_frame();

    draw_terrain();
    draw_bodies(alpha);
    draw_mouse_ray(input);
    draw_hud();

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
    heightfield_init_procedural(hf, &g_perm_arena, TERRAIN_SIZE, TERRAIN_CELL, SCENE_PHYS_SEED);
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
        f32 speed = vec3_length(body->vel);
        b32 nan = (body->pos.x != body->pos.x) || (body->rot.w != body->rot.w) || (speed != speed);
        if (nan || f_abs(body->pos.x) > 500.0f || f_abs(body->pos.z) > 500.0f) {
            result.valid = 0;
            continue;
        }
        if (body->pos.y < hf->min_height - 2.0f) {
            result.valid = 0;
        }
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

int main(int argc, char** argv)
{
    arena_init(&g_perm_arena, GIGABYTES(1));
    arena_init(&g_frame_arena, MEGABYTES(64));

#if defined(_DEBUG)
    math_selftest();
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

    heightfield_init_procedural(&s_heightfield, &g_perm_arena, TERRAIN_SIZE, TERRAIN_CELL, TERRAIN_SEED);
    phys_init(&s_phys, &g_perm_arena, &s_heightfield);
    reset_playground();

    camera_init(&s_camera, v3(24.0f, 22.0f, 24.0f));
    camera_look_at(&s_camera, v3(0.0f, 2.0f, 0.0f));

    f64 prev_time = platform_time_now();
    f64 accumulator = 0.0;
    f64 fps_timer = 0.0;
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
            reset_playground();
        }
        if (input->key_pressed[KEY_F]) {
            Vec3 forward = camera_forward(&s_camera);
            spawn_random_box(&s_phys, &s_spawn_rng,
                             vec3_add(s_camera.pos, vec3_scale(forward, 2.0f)),
                             vec3_scale(forward, 16.0f));
        }
        if (input->key_pressed[KEY_F2]) {
            s_show_phys_debug = !s_show_phys_debug;
        }
        if (input->key_pressed[KEY_F5]) {
            s_slow_mo = !s_slow_mo;
        }

        camera_fly_update(&s_camera, input, (f32)frame_dt);
        r_hot_reload_poll(now);

        accumulator += frame_dt * (s_slow_mo ? 0.1 : 1.0);
        while (accumulator >= FIXED_DT) {
            game_tick(FIXED_DT);
            accumulator -= FIXED_DT;
        }

        f32 alpha = (f32)(accumulator / FIXED_DT);
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
