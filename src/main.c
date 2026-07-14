#include "core/types.h"
#include "core/arena.h"
#include "core/log.h"
#include "math/vmath.h"
#include "platform/platform.h"
#include "platform/gl_loader.h"

#include <stdio.h>

#define FIXED_DT (1.0f / 120.0f)
#define MAX_FRAME_DT 0.25

typedef struct GameState {
    u64 tick_count;
} GameState;

static GameState s_game;

static void game_tick(f32 dt)
{
    (void)dt;
    s_game.tick_count++;
}

static void game_render(f32 alpha)
{
    i32 width, height;
    platform_framebuffer_size(&width, &height);
    glViewport(0, 0, width, height);

    f32 sim_time = ((f32)s_game.tick_count + alpha) * FIXED_DT;
    f32 pulse = 0.5f + 0.5f * sinf(sim_time * 0.6f);
    glClearColor(f_lerp(0.02f, 0.05f, pulse),
                 f_lerp(0.05f, 0.12f, pulse),
                 f_lerp(0.07f, 0.16f, pulse),
                 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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
            char title[128];
            snprintf(title, sizeof(title), "Anomalous | %.0f fps | tick %llu",
                     (f64)fps_frame_count / fps_timer,
                     (unsigned long long)s_game.tick_count);
            platform_set_title(title);
            fps_timer = 0.0;
            fps_frame_count = 0;
        }
    }

    platform_shutdown();
    arena_release(&g_frame_arena);
    arena_release(&g_perm_arena);
    return 0;
}
