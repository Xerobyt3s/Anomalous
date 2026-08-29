#include "test.h"

#include "platform/input.h"
#include "render/camera.h"

using namespace anom;

namespace {

constexpr f32 kEps = 1e-4f;

bool vec_near(Vec3 a, Vec3 b, f32 eps)
{
    return f_abs(a.x - b.x) <= eps && f_abs(a.y - b.y) <= eps && f_abs(a.z - b.z) <= eps;
}

} // namespace

TEST(camera, default_faces_negative_z)
{
    Camera cam;
    CHECK(vec_near(cam.forward(), Vec3{0.0f, 0.0f, -1.0f}, kEps));
    CHECK(vec_near(cam.right(), Vec3{1.0f, 0.0f, 0.0f}, kEps));
}

TEST(camera, look_at_points_at_target)
{
    Camera cam;
    cam.pos = Vec3{5.0f, 2.0f, 5.0f};
    cam.look_at(Vec3{0.0f, 0.0f, 0.0f});

    const Vec3 expected = normalize(Vec3{0.0f, 0.0f, 0.0f} - cam.pos);
    CHECK(vec_near(cam.forward(), expected, kEps));
}

TEST(camera, basis_is_orthonormal)
{
    Camera cam;
    cam.yaw = 0.9f;
    cam.pitch = -0.4f;

    const Vec3 f = cam.forward();
    const Vec3 r = cam.right();
    const Vec3 u = cam.up();

    CHECK_NEAR(length(f), 1.0f, kEps);
    CHECK_NEAR(length(r), 1.0f, kEps);
    CHECK_NEAR(length(u), 1.0f, kEps);
    CHECK_NEAR(dot(f, r), 0.0f, kEps);
    CHECK_NEAR(dot(f, u), 0.0f, kEps);
    CHECK_NEAR(dot(r, u), 0.0f, kEps);
}

TEST(camera, view_matrix_moves_world_into_camera_space)
{
    Camera cam;
    cam.pos = Vec3{3.0f, 4.0f, 5.0f};
    const Vec3 in_view = transform_point(cam.view(), cam.pos);
    CHECK(vec_near(in_view, Vec3{0.0f, 0.0f, 0.0f}, kEps));

    const Vec3 ahead = transform_point(cam.view(), cam.pos + cam.forward() * 10.0f);
    CHECK_NEAR(ahead.z, -10.0f, 1e-3f);
}

TEST(camera, centre_mouse_ray_matches_forward)
{
    Camera cam;
    cam.yaw = 0.6f;
    cam.pitch = 0.2f;

    const Vec2 viewport{1600.0f, 900.0f};
    const Ray ray = cam.mouse_ray(Vec2{800.0f, 450.0f}, viewport);
    CHECK(vec_near(ray.origin, cam.pos, kEps));
    CHECK(vec_near(ray.dir, cam.forward(), 1e-3f));
}

TEST(camera, edge_mouse_ray_leans_toward_that_edge)
{
    Camera cam;
    const Vec2 viewport{1600.0f, 900.0f};
    const Ray left = cam.mouse_ray(Vec2{0.0f, 450.0f}, viewport);
    const Ray right = cam.mouse_ray(Vec2{1600.0f, 450.0f}, viewport);

    CHECK(dot(left.dir, cam.right()) < 0.0f);
    CHECK(dot(right.dir, cam.right()) > 0.0f);
    CHECK_NEAR(length(left.dir), 1.0f, kEps);
}

TEST(camera, fly_update_moves_along_forward)
{
    Camera cam;
    cam.move_speed = 10.0f;

    Input in;
    in.begin_frame();
    in.set_key(static_cast<i32>(Key::W), true);
    in.finish_frame();

    const bool capture = cam.fly_update(in, 0.1f);
    CHECK(!capture);
    CHECK(vec_near(cam.pos, Vec3{0.0f, 0.0f, -1.0f}, kEps));
}

TEST(camera, fly_update_reports_capture_intent_without_touching_the_window)
{
    Camera cam;
    Input in;
    in.begin_frame();
    in.set_mouse_button(static_cast<i32>(MouseButton::Right), true);
    in.set_mouse_pos(100.0f, 100.0f);
    in.finish_frame();

    CHECK(cam.fly_update(in, 0.016f));
}

TEST(camera, idle_fly_update_does_not_drift)
{
    Camera cam;
    Input in;
    in.begin_frame();
    in.finish_frame();

    cam.fly_update(in, 0.016f);
    CHECK(vec_near(cam.pos, Vec3{0.0f, 0.0f, 0.0f}, kEps));
}

TEST(camera, scroll_clamps_move_speed)
{
    Camera cam;
    Input in;

    for (i32 i = 0; i < 200; i++) {
        in.begin_frame();
        in.add_scroll(1.0f);
        in.finish_frame();
        cam.fly_update(in, 0.0f);
    }
    CHECK(cam.move_speed <= 200.0f);

    for (i32 i = 0; i < 400; i++) {
        in.begin_frame();
        in.add_scroll(-1.0f);
        in.finish_frame();
        cam.fly_update(in, 0.0f);
    }
    CHECK(cam.move_speed >= 0.5f);
}

TEST(camera, pitch_is_clamped_below_vertical)
{
    Camera cam;
    Input in;
    for (i32 i = 0; i < 100; i++) {
        in.begin_frame();
        in.set_mouse_button(static_cast<i32>(MouseButton::Right), true);
        in.set_mouse_pos(0.0f, static_cast<f32>(-i * 100));
        in.finish_frame();
        cam.fly_update(in, 0.016f);
    }
    CHECK(cam.pitch <= 89.0f * kDegToRad + kEps);
    CHECK(cam.pitch >= -89.0f * kDegToRad - kEps);
}
