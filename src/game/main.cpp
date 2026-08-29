#include "assets/watcher.h"
#include "core/arena.h"
#include "core/config.h"
#include "core/log.h"
#include "core/version.h"
#include "math/vmath.h"
#include "platform/clock.h"
#include "platform/filesystem.h"
#include "platform/gl_loader.h"
#include "platform/platform_info.h"
#include "platform/window.h"
#include "render/camera.h"
#include "render/debug_draw.h"
#include "render/device.h"
#include "render/fontchain.h"
#include "render/screenshot.h"
#include "render/sky.h"
#include "render/terrain_render.h"
#include "render/text.h"
#include "world/terrain.h"

#include <charconv>
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
    bool eye = false;
    f32 rain = 0.0f;
    bool have_campos = false;
    anom::Vec3 campos{0.0f, 0.0f, 0.0f};
    bool have_camlook = false;
    f32 cam_yaw = 0.0f;
    f32 cam_pitch = 0.0f;
};

std::string_view next_token(std::string_view& cursor)
{
    const std::size_t begin = cursor.find_first_not_of(" \t");
    if (begin == std::string_view::npos) {
        cursor = {};
        return {};
    }
    const std::size_t end = cursor.find_first_of(" \t", begin);
    const std::string_view token = cursor.substr(begin, end == std::string_view::npos
                                                            ? std::string_view::npos
                                                            : end - begin);
    cursor = end == std::string_view::npos ? std::string_view{} : cursor.substr(end);
    return token;
}

f32 to_f32(std::string_view text)
{
    f32 value = 0.0f;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

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
        } else if (std::strcmp(argv[i], "--eye") == 0) {
            options.eye = true;
        } else if (std::strcmp(argv[i], "--noretro") == 0) {
            options.retro = false;
        } else if (std::strcmp(argv[i], "--campos") == 0 && i + 3 < argc) {
            options.campos = anom::Vec3{static_cast<f32>(std::atof(argv[i + 1])),
                                  static_cast<f32>(std::atof(argv[i + 2])),
                                  static_cast<f32>(std::atof(argv[i + 3]))};
            options.have_campos = true;
            i += 3;
        } else if (std::strcmp(argv[i], "--camlook") == 0 && i + 2 < argc) {
            options.cam_yaw = static_cast<f32>(std::atof(argv[i + 1])) * anom::kDegToRad;
            options.cam_pitch = static_cast<f32>(std::atof(argv[i + 2])) * anom::kDegToRad;
            options.have_camlook = true;
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

    Config zone_cfg;
    {
        ArenaScope scope(scratch);
        FixedString<256> cfg_path;
        cfg_path.format("%s/zone.cfg", kZoneDir);
        const fs::FileData cfg_file = fs::read_entire_file(scratch, cfg_path.view());
        if (!cfg_file.valid() || !zone_cfg.parse(perm, cfg_file.text())) {
            log_error("failed to load %s", cfg_path.c_str());
            return 1;
        }
    }

    Terrain terrain;
    if (!terrain.load(perm, scratch, kZoneDir, zone_cfg)) {
        return 1;
    }

    TerrainRenderer terrain_renderer;
    if (!terrain_renderer.init(device, scratch, terrain.heightfield(),
                               terrain.roadmask(), terrain.mask_size())) {
        return 1;
    }

    const Vec3 car_pos = zone_cfg.get_vec3("spawn.car_pos", Vec3{0.0f, 0.0f, 0.0f});
    const Vec3 focus{car_pos.x, terrain.heightfield().sample(car_pos.x, car_pos.y), car_pos.y};

    const GpuMesh* building_mesh = nullptr;
    Mat4 building_model = mat4_identity();
    {
        std::string_view rest = zone_cfg.get_str("entities.spawn", "");
        const std::string_view kind = next_token(rest);
        const std::string_view mesh_name = next_token(rest);
        const std::string_view sx = next_token(rest);
        const std::string_view sz = next_token(rest);
        const std::string_view syaw = next_token(rest);
        const std::string_view sscale = next_token(rest);

        if (!mesh_name.empty() && !sz.empty()) {
            const f32 bx = to_f32(sx);
            const f32 bz = to_f32(sz);
            const f32 byaw = to_f32(syaw);
            const f32 bscale = sscale.empty() ? 1.0f : to_f32(sscale);

            building_mesh = device.assets().mesh(mesh_name);
            building_model = mat4_trs(Vec3{bx, terrain.heightfield().sample(bx, bz), bz},
                                      quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f},
                                                           byaw * kDegToRad),
                                      Vec3{bscale, bscale, bscale});
            log_info("zone: %.*s '%.*s' at %.1f %.1f",
                     static_cast<int>(kind.size()), kind.data(),
                     static_cast<int>(mesh_name.size()), mesh_name.data(),
                     static_cast<f64>(bx), static_cast<f64>(bz));
        }
    }

    log_info("spawn focus %.1f %.1f %.1f", static_cast<f64>(focus.x),
             static_cast<f64>(focus.y), static_cast<f64>(focus.z));

    Camera camera;
    camera.pos = focus + Vec3{-18.0f, 12.0f, 18.0f};
    camera.look_at(focus);
    if (options.eye) {
        camera.pos = focus + anom::Vec3{0.0f, 1.7f, 0.0f};
        camera.yaw = 2.2f;
        camera.pitch = -0.05f;
    }
    if (options.have_campos) {
        camera.pos = options.campos;
    }
    if (options.have_camlook) {
        camera.yaw = options.cam_yaw;
        camera.pitch = options.cam_pitch;
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

    device.set_weather(options.rain * 0.85f, options.rain);
    device.set_windshield(options.rain, 0.0f, options.rain * 0.5f);

    const f64 start_time = time_seconds();
    f64 prev_time = start_time;
    f64 fps_timer = 0.0;
    u32 fps_frames = 0;
    i64 frame_index = 0;
    bool show_debug = true;

    while (!window.should_close()) {
        const f64 now = time_seconds();
        const f64 frame_dt = now - prev_time;
        prev_time = now;

        window.poll();
        const Input& input = window.input();

        if (input.pressed(Key::Escape)) {
            window.request_close();
        }
        if (input.pressed(Key::F2)) {
            show_debug = !show_debug;
        }

        watcher.poll(now);

        const bool want_capture = camera.fly_update(input, static_cast<f32>(frame_dt));
        window.set_cursor_captured(want_capture);

        const FramebufferSize size = window.framebuffer_size();
        const f32 time = static_cast<f32>(now);

        if (device.shadow_begin(focus)) {
            terrain_renderer.draw(device);
            device.draw_mesh(building_mesh, building_model);
            device.shadow_end();
        }

        device.set_time(time);
        text.begin_frame();
        debug.begin_frame();
        device.begin_frame(camera, size.width, size.height);
        device.draw_sky(time);
        terrain_renderer.draw(device);
        device.draw_mesh(building_mesh, building_model);
        terrain_renderer.draw_scrub(device, camera.pos, time);
        device.draw_rain(options.rain, 0.35f, Vec3{0.0f, 0.0f, 0.0f}, time);
        device.draw_glass(nullptr, mat4_identity(), time);
        if (show_debug) {
            debug.grid(Vec3{std::floor(camera.pos.x), focus.y + 0.02f, std::floor(camera.pos.z)},
                       20.0f, 2.0f, kDdDark);
            debug.aabb(transform(building_model, building_mesh ? building_mesh->bounds
                                                              : aabb_empty()),
                       kDdOrange);
            debug.cross(focus, 1.5f, kDdCyan);
            debug.text_3d(focus + Vec3{0.0f, 1.0f, 0.0f}, 18.0f, kDdWhite, "spawn");
        }

        debug.flush_world(device);

        const DrawStats& draw_stats = device.stats();
        debug.text_2d(text, 12.0f, 28.0f, 18.0f, kDdWhite,
                      "%.1f ms  %u draws  %u/%u chunks  %u prog  %u vao",
                      frame_dt * 1000.0, draw_stats.draw_calls, terrain_renderer.chunks_drawn(),
                      terrain_renderer.chunk_count(), draw_stats.program_binds, draw_stats.vao_binds);
        debug.text_2d(text, 12.0f, 52.0f, 18.0f, kDdWhite,
                      "cam %.1f %.1f %.1f   tod %.2f   exposure %.2f   retro %s",
                      static_cast<f64>(camera.pos.x), static_cast<f64>(camera.pos.y),
                      static_cast<f64>(camera.pos.z), static_cast<f64>(env.time_of_day),
                      static_cast<f64>(env.exposure), options.retro ? "on" : "off");
        device.end_frame();
        device.post_process(time);

        debug.flush_overlay(device, text);
        text.flush(device);

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

    terrain_renderer.shutdown();
    device.shutdown();
    const f64 elapsed = time_seconds() - start_time;
    log_info("shutdown after %lld frames, %.2f ms/frame avg", static_cast<long long>(frame_index),
             frame_index > 0 ? elapsed * 1000.0 / static_cast<f64>(frame_index) : 0.0);
    return 0;
}
