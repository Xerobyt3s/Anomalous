#include "core/types.h"
#include "core/arena.h"
#include "core/log.h"
#include "math/vmath.h"
#include "platform/platform.h"
#include "platform/gl_loader.h"
#include "render/camera.h"
#include "render/render.h"
#include "render/debug_draw.h"
#include "render/text.h"

#include <stdio.h>

#define FIXED_DT (1.0f / 120.0f)
#define MAX_FRAME_DT 0.25

typedef struct GameState {
    u64 tick_count;
} GameState;

static GameState s_game;
static Camera s_camera;
static f32 s_fps;

static void game_tick(f32 dt)
{
    (void)dt;
    s_game.tick_count++;
}

static void draw_demo_scene(f32 t)
{
    dd_grid(vec3_zero(), 20.0f, 1.0f, DD_DARK);
    dd_line(v3(-20.0f, 0.0f, 0.0f), v3(20.0f, 0.0f, 0.0f), dd_rgba(120, 60, 60, 255));
    dd_line(v3(0.0f, 0.0f, -20.0f), v3(0.0f, 0.0f, 20.0f), dd_rgba(60, 60, 120, 255));

    dd_arrow(vec3_zero(), v3(3.0f, 0.0f, 0.0f), 0.3f, DD_RED);
    dd_arrow(vec3_zero(), v3(0.0f, 3.0f, 0.0f), 0.3f, DD_GREEN);
    dd_arrow(vec3_zero(), v3(0.0f, 0.0f, 3.0f), 0.3f, DD_BLUE);
    dd_text_3d(v3(3.4f, 0.0f, 0.0f), 18.0f, DD_RED, "+X");
    dd_text_3d(v3(0.0f, 3.4f, 0.0f), 18.0f, DD_GREEN, "+Y");
    dd_text_3d(v3(0.0f, 0.0f, 3.4f), 18.0f, DD_BLUE, "+Z");

    f32 orbit_angle = t * 0.8f;
    Vec3 orbit_pos = v3(cosf(orbit_angle) * 8.0f,
                        1.5f + 0.5f * sinf(t * 2.0f),
                        sinf(orbit_angle) * 8.0f);
    dd_circle(vec3_zero(), v3(0.0f, 1.0f, 0.0f), 8.0f, DD_GRAY);
    dd_sphere(orbit_pos, 0.6f, DD_CYAN);
    dd_arrow(orbit_pos,
             vec3_add(orbit_pos, vec3_scale(v3(-sinf(orbit_angle), 0.0f, cosf(orbit_angle)), 2.0f)),
             0.25f, DD_CYAN);
    dd_text_3d(vec3_add(orbit_pos, v3(0.0f, 1.0f, 0.0f)), 16.0f, DD_CYAN, "orbiter");

    f32 bounce = f_abs(sinf(t * 1.8f)) * 2.0f;
    Aabb box;
    box.min = v3(-5.0f - 1.0f, bounce, -5.0f - 0.7f);
    box.max = v3(-5.0f + 1.0f, bounce + 1.2f, -5.0f + 0.7f);
    dd_aabb(box, DD_YELLOW);
    dd_text_3d(v3(-5.0f, bounce + 1.7f, -5.0f), 16.0f, DD_YELLOW, "aabb y=%.2f", bounce);

    for (i32 i = 0; i < 12; i++) {
        f32 angle = (f32)i / 12.0f * 2.0f * PI32;
        Vec3 p = v3(cosf(angle) * 13.0f, 0.5f, sinf(angle) * 13.0f);
        dd_cross(p, 0.6f, (i % 3 == 0) ? DD_ORANGE : DD_GRAY);
    }

    Vec3 pulse_center = v3(6.0f, 0.02f, 6.0f);
    f32 pulse_radius = 1.0f + 0.5f * sinf(t * 3.0f);
    dd_circle(pulse_center, v3(0.0f, 1.0f, 0.0f), pulse_radius, DD_MAGENTA);
    dd_cross(pulse_center, 0.4f, DD_MAGENTA);
}

static void draw_hud(void)
{
    f32 line_height = text_line_height(18.0f);
    f32 y = 8.0f + line_height;
    dd_text_2d(12.0f, y, 18.0f, DD_WHITE, "%.0f fps | tick %llu", s_fps, (unsigned long long)s_game.tick_count);
    y += line_height;
    dd_text_2d(12.0f, y, 18.0f, DD_GRAY, "cam (%.1f, %.1f, %.1f) yaw %.0f pitch %.0f speed %.1f",
               s_camera.pos.x, s_camera.pos.y, s_camera.pos.z,
               s_camera.yaw * RAD_TO_DEG, s_camera.pitch * RAD_TO_DEG, s_camera.move_speed);
    y += line_height;
    dd_text_2d(12.0f, y, 18.0f, DD_GRAY, "rmb look | wasd move | q/e down/up | shift fast | scroll speed | esc quit");
    y += line_height;
    dd_text_2d(12.0f, y, 18.0f, DD_DARK, "edit shaders/*.vert|.frag while running to hot reload");
}

static void game_render(f32 alpha)
{
    r_begin_frame(&s_camera);
    dd_begin_frame();
    text_begin_frame();

    f32 t = ((f32)s_game.tick_count + alpha) * FIXED_DT;
    draw_demo_scene(t);
    draw_hud();

    r_end_frame();
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;

    arena_init(&g_perm_arena, GIGABYTES(1));
    arena_init(&g_frame_arena, MEGABYTES(64));

#if defined(_DEBUG)
    math_selftest();
#endif

    if (!platform_init("Anomalous", 1600, 900)) {
        return 1;
    }
    if (!r_init() || !dd_init() || !text_init("assets/fonts/mono.ttf")) {
        platform_shutdown();
        return 1;
    }

    camera_init(&s_camera, v3(10.0f, 7.0f, 14.0f));
    camera_look_at(&s_camera, v3(0.0f, 1.0f, 0.0f));

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
        accumulator += frame_dt;

        platform_poll_input();
        const GameInput* input = platform_input();
        if (input->key_pressed[KEY_ESCAPE]) {
            platform_request_close();
        }

        camera_fly_update(&s_camera, input, (f32)frame_dt);
        r_hot_reload_poll(now);

        while (accumulator >= FIXED_DT) {
            game_tick(FIXED_DT);
            accumulator -= FIXED_DT;
        }

        f32 alpha = (f32)(accumulator / FIXED_DT);
        game_render(alpha);
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
