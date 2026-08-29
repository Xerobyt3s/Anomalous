#include "core/log.h"
#include "core/version.h"
#include "platform/clock.h"
#include "platform/gl_loader.h"
#include "platform/platform_info.h"
#include "platform/window.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {

struct Options {
    i32 width = 1600;
    i32 height = 900;
    i64 max_frames = -1;
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
    log_info("glfw %.*s",
             static_cast<int>(glfw_version_string().size()), glfw_version_string().data());

    Window window;
    if (!window.create("Anomalous", options.width, options.height)) {
        return 1;
    }

    f64 prev_time = time_seconds();
    f64 fps_timer = 0.0;
    u32 fps_frames = 0;
    i64 frame_index = 0;

    while (!window.should_close()) {
        const f64 now = time_seconds();
        const f64 frame_dt = now - prev_time;
        prev_time = now;

        window.poll();
        const Input& input = window.input();

        if (input.pressed(Key::Escape)) {
            window.request_close();
        }

        const FramebufferSize size = window.framebuffer_size();
        glViewport(0, 0, size.width, size.height);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        window.swap_buffers();

        fps_frames++;
        fps_timer += frame_dt;
        if (fps_timer >= 0.5) {
            char title[128];
            std::snprintf(title, sizeof(title), "Anomalous | %.0f fps",
                          static_cast<f64>(fps_frames) / fps_timer);
            window.set_title(title);
            fps_timer = 0.0;
            fps_frames = 0;
        }

        frame_index++;
        if (options.max_frames >= 0 && frame_index >= options.max_frames) {
            window.request_close();
        }
    }

    log_info("shutdown after %lld frames", static_cast<long long>(frame_index));
    return 0;
}
