#include "assets/watcher.h"
#include "core/arena.h"
#include "core/log.h"
#include "core/version.h"
#include "game/game.h"
#include "math/vmath.h"
#include "platform/clock.h"
#include "platform/gl_loader.h"
#include "platform/window.h"
#include "render/debug_draw.h"
#include "render/device.h"
#include "render/fontchain.h"
#include "render/screenshot.h"
#include "render/sky.h"
#include "render/terrain_render.h"
#include "render/text.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr const char* kZoneDir = "assets/zones/testzone";

struct Options {
    i32 width = 1600;
    i32 height = 900;
    i64 max_frames = -1;
    const char* screenshot = nullptr;
    f32 exposure = 1.40f;
    f32 time_of_day = 0.32f;
    bool retro = true;
    bool free_cam = false;
    bool panels = false;
    bool brush = false;
    f32 rain = 0.0f;
    bool have_campos = false;
    anom::Vec3 campos{0.0f, 0.0f, 0.0f};
    bool have_camlook = false;
    f32 cam_yaw = 0.0f;
    f32 cam_pitch = 0.0f;
};

Options parse_options(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--frames=", 9) == 0) {
            options.max_frames = std::atoll(argv[i] + 9);
        } else if (std::strncmp(argv[i], "--width=", 8) == 0) {
            options.width = std::atoi(argv[i] + 8);
        } else if (std::strncmp(argv[i], "--height=", 9) == 0) {
            options.height = std::atoi(argv[i] + 9);
        } else if (std::strncmp(argv[i], "--screenshot=", 13) == 0) {
            options.screenshot = argv[i] + 13;
        } else if (std::strncmp(argv[i], "--exposure=", 11) == 0) {
            options.exposure = static_cast<f32>(std::atof(argv[i] + 11));
        } else if (std::strncmp(argv[i], "--tod=", 6) == 0) {
            options.time_of_day = static_cast<f32>(std::atof(argv[i] + 6));
        } else if (std::strncmp(argv[i], "--rain=", 7) == 0) {
            options.rain = static_cast<f32>(std::atof(argv[i] + 7));
        } else if (std::strcmp(argv[i], "--noretro") == 0) {
            options.retro = false;
        } else if (std::strcmp(argv[i], "--brush") == 0) {
            options.brush = true;
        } else if (std::strcmp(argv[i], "--panels") == 0) {
            options.panels = true;
        } else if (std::strcmp(argv[i], "--freecam") == 0) {
            options.free_cam = true;
        } else if (std::strcmp(argv[i], "--campos") == 0 && i + 3 < argc) {
            options.campos = anom::Vec3{static_cast<f32>(std::atof(argv[i + 1])),
                                        static_cast<f32>(std::atof(argv[i + 2])),
                                        static_cast<f32>(std::atof(argv[i + 3]))};
            options.have_campos = true;
            options.free_cam = true;
            i += 3;
        } else if (std::strcmp(argv[i], "--camlook") == 0 && i + 2 < argc) {
            options.cam_yaw = static_cast<f32>(std::atof(argv[i + 1])) * anom::kDegToRad;
            options.cam_pitch = static_cast<f32>(std::atof(argv[i + 2])) * anom::kDegToRad;
            options.have_camlook = true;
            options.free_cam = true;
            i += 2;
        }
    }
    return options;
}

} // namespace

int main(int argc, char** argv)
{
    using namespace anom;

    const Options options = parse_options(argc, argv);

    log_info("anomalous %.*s (%.*s)",
             static_cast<int>(version_string().size()), version_string().data(),
             static_cast<int>(build_config().size()), build_config().data());

    Arena perm(gigabytes(1));
    Arena scratch(megabytes(256));
    if (!perm.valid() || !scratch.valid()) {
        return 1;
    }

    Window window;
    if (!window.create("Anomalous", options.width, options.height)) {
        return 1;
    }

    FileWatcher watcher;
    RenderDevice device;
    if (!device.init(watcher, scratch)) {
        return 1;
    }

    FontChain fonts;
    TextRenderer text;
    DebugDraw debug;
    if (!fonts.init(perm, scratch) || !text.init(fonts, perm, scratch) || !debug.init(perm)) {
        return 1;
    }

    Game game;
    if (!game.init(device, perm, scratch, kZoneDir)) {
        return 1;
    }

    TerrainRenderer terrain_renderer;
    if (!terrain_renderer.init(device, scratch, game.terrain().heightfield(),
                               game.terrain().roadmask(), game.terrain().mask_size())) {
        return 1;
    }

    Environment env;
    env.time_of_day = options.time_of_day;
    env.exposure = options.exposure;
    env.sun_dir = -sun_direction_for_time(env.time_of_day);
    device.set_environment(env);

    RetroFx retro;
    retro.enabled = options.retro;
    device.set_retro_fx(retro);

    for (const char* name : {"mesh", "glass", "rain", "debug2d", "blit", "screen"}) {
        device.shaders().program(name);
    }

    game.toggles().free_cam = options.free_cam;
    if (options.brush) {
        game.editor().toggle(game.world(), game.phys());
        game.editor().set_brush_mode(true);
        game.editor().brush().reset(scratch);
    }
    game.toggles().carsys = options.panels;
    game.toggles().telemetry = options.panels;
    if (options.have_campos) {
        game.camera().pos = options.campos;
    }
    if (options.have_camlook) {
        game.camera().yaw = options.cam_yaw;
        game.camera().pitch = options.cam_pitch;
    }
    if (options.rain > 0.0f) {
        game.weather().set_mode(WeatherMode::Rain);
    }

    const f64 start_time = time_seconds();
    f64 prev_time = start_time;
    f64 fps_timer = 0.0;
    u32 fps_frames = 0;
    i64 frame_index = 0;

    while (!window.should_close()) {
        scratch.reset();

        const f64 now = time_seconds();
        f64 frame_dt = now - prev_time;
        prev_time = now;
        if (frame_dt > kMaxFrameDt) {
            frame_dt = kMaxFrameDt;
        }

        window.poll();
        const Input& input = window.input();

        watcher.poll(now);
        game.poll_hot_reload(scratch, now);
        game.handle_input(window, input, static_cast<f32>(frame_dt));
        game.advance(static_cast<f32>(frame_dt));

        const FramebufferSize size = window.framebuffer_size();
        const Vec2 viewport{static_cast<f32>(size.width), static_cast<f32>(size.height)};
        game.render(device, terrain_renderer, debug, text, input, viewport,
                    static_cast<f32>(now), static_cast<f32>(frame_dt));

        frame_index++;
        const bool last_frame = options.max_frames >= 0 && frame_index >= options.max_frames;
        if (last_frame && options.screenshot) {
            FrameStats stats;
            if (capture_frame(scratch, size.width, size.height, options.screenshot, &stats)) {
                log_info("frame: mean rgb %.1f %.1f %.1f, nonblack %.1f%%, %u colour buckets",
                         stats.mean_r, stats.mean_g, stats.mean_b,
                         static_cast<f64>(stats.nonblack_fraction) * 100.0,
                         stats.distinct_buckets);
            }
        }

        window.swap_buffers();

        fps_frames++;
        fps_timer += frame_dt;
        if (fps_timer >= 0.5) {
            char title[160];
            std::snprintf(title, sizeof(title),
                          "Anomalous | %.0f fps | %u/%u chunks | %u draws",
                          static_cast<f64>(fps_frames) / fps_timer,
                          terrain_renderer.chunks_drawn(), terrain_renderer.chunk_count(),
                          device.stats().draw_calls);
            window.set_title(title);
            fps_timer = 0.0;
            fps_frames = 0;
        }

        if (last_frame) {
            window.request_close();
        }
    }

    game.audio().shutdown();
    terrain_renderer.shutdown();
    device.shutdown();

    const f64 elapsed = time_seconds() - start_time;
    log_info("shutdown after %lld frames, %.2f ms/frame avg",
             static_cast<long long>(frame_index),
             frame_index > 0 ? elapsed * 1000.0 / static_cast<f64>(frame_index) : 0.0);
    return 0;
}
