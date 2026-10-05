#include "test.h"

#include "engine/render/camera.h"
#include "engine/render/shader.h"
#include "math/glm_bridge.h"
#include "render/camera.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace anom;

namespace {

void write_file(const std::filesystem::path& path, const char* text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

ghost::engine::Camera ghost_from(const Camera& c)
{
    ghost::engine::Camera g;
    g.position = to_glm(c.pos);
    g.yaw = c.yaw;
    g.pitch = c.pitch;
    g.roll = c.roll;
    g.frame = to_glm(c.frame);
    g.fovY = c.fov_y;
    g.nearZ = c.znear;
    g.farZ = c.zfar;
    return g;
}

} // namespace

TEST(render_core, shader_includes_expand_relative_to_the_including_file_and_record_every_file)
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "anom_shader_include";
    std::filesystem::create_directories(dir / "lib");
    write_file(dir / "main.frag", "#version 460 core\n#include \"lib/a.glsl\"\nvoid main() {}\n");
    write_file(dir / "lib" / "a.glsl", "float a;\n  #include \"b.glsl\"\n");
    write_file(dir / "lib" / "b.glsl", "float b;\n");
    std::vector<std::filesystem::path> files;
    const std::string out = ghost::engine::Shader::expandIncludes(dir / "main.frag", files);
    CHECK(out.find("#version 460 core") == 0);
    CHECK(out.find("float a;") != std::string::npos);
    CHECK(out.find("float b;") != std::string::npos);
    CHECK(out.find("#include") == std::string::npos);
    CHECK(files.size() == 3);
}

TEST(render_core, the_ghost_camera_with_a_gravity_frame_matches_the_game_camera)
{
    Camera c;
    c.pos = Vec3{12.0f, 3.0f, -7.0f};
    c.yaw = 0.7f;
    c.pitch = -0.25f;
    const Quat frames[3] = {quat_identity(), quat_from_euler(0.0f, 0.0f, 90.0f * kDegToRad),
                            quat_from_euler(0.4f, 0.3f, 2.6f)};
    for (const Quat& frame : frames) {
        for (f32 roll : {0.0f, 0.2f}) {
            c.frame = frame;
            c.roll = roll;
            const ghost::engine::Camera g = ghost_from(c);
            const Mat4 ours = c.view();
            const Mat4 theirs = from_glm(g.view());
            for (int i = 0; i < 16; i++) {
                CHECK_NEAR(ours.m[i], theirs.m[i], 1e-4);
            }
            const Mat4 p_ours = c.proj(1.6f);
            const Mat4 p_theirs = from_glm(g.projection(1.6f));
            for (int i = 0; i < 16; i++) {
                CHECK_NEAR(p_ours.m[i], p_theirs.m[i], 1e-4);
            }
            const Vec3 f = from_glm(g.forward());
            CHECK_NEAR(dot(f, c.forward()), 1.0f, 1e-5);
        }
    }
}
