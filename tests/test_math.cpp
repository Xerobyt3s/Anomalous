#include "test.h"

#include "math/glm_bridge.h"
#include "math/vmath.h"

using namespace anom;

namespace {

constexpr f32 kEps = 1e-4f;

bool vec_near(Vec3 a, Vec3 b, f32 eps)
{
    return f_abs(a.x - b.x) <= eps && f_abs(a.y - b.y) <= eps && f_abs(a.z - b.z) <= eps;
}

} // namespace

TEST(math, cross_and_length)
{
    const Vec3 x{1.0f, 0.0f, 0.0f};
    const Vec3 y{0.0f, 1.0f, 0.0f};
    const Vec3 z{0.0f, 0.0f, 1.0f};
    CHECK(vec_near(cross(x, y), z, kEps));
    CHECK_NEAR(length(Vec3{3.0f, 4.0f, 0.0f}), 5.0f, kEps);
}

TEST(math, operators_match_scaled_adds)
{
    const Vec3 a{1.0f, 2.0f, 3.0f};
    const Vec3 b{-4.0f, 0.5f, 2.0f};
    CHECK(vec_near(a + b, Vec3{-3.0f, 2.5f, 5.0f}, kEps));
    CHECK(vec_near(a - b, Vec3{5.0f, 1.5f, 1.0f}, kEps));
    CHECK(vec_near(a * 2.0f, Vec3{2.0f, 4.0f, 6.0f}, kEps));
    CHECK(vec_near(2.0f * a, a * 2.0f, kEps));
    CHECK(vec_near(-a, Vec3{-1.0f, -2.0f, -3.0f}, kEps));

    Vec3 acc = a;
    acc += b;
    CHECK(vec_near(acc, a + b, kEps));
}

TEST(math, normalize_of_degenerate_is_zero)
{
    CHECK(vec_near(normalize(Vec3{0.0f, 0.0f, 0.0f}), Vec3{0.0f, 0.0f, 0.0f}, kEps));
    CHECK_NEAR(length(normalize(Vec3{0.0f, 5.0f, 0.0f})), 1.0f, kEps);
}

TEST(math, quat_rotation_matches_matrix)
{
    const Vec3 x{1.0f, 0.0f, 0.0f};
    const Quat rot90_y = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, kPi * 0.5f);
    const Vec3 rotated = rotate(rot90_y, x);
    CHECK(vec_near(rotated, Vec3{0.0f, 0.0f, -1.0f}, kEps));

    const Vec3 via_mat = transform_dir(quat_to_mat4(rot90_y), x);
    CHECK(vec_near(via_mat, rotated, kEps));

    const Quat q = quat_from_euler(0.7f, 0.3f, -0.2f);
    const Vec3 p{1.5f, -2.0f, 3.0f};
    CHECK(vec_near(rotate(q, p), quat_to_mat3(q) * p, kEps));
}

TEST(math, mat4_inverse_round_trips)
{
    const Quat q = quat_from_euler(0.7f, 0.3f, -0.2f);
    const Vec3 p{1.5f, -2.0f, 3.0f};
    const Mat4 trs = mat4_trs(Vec3{3.0f, -1.0f, 2.0f}, q, Vec3{1.0f, 1.0f, 1.0f});
    const Mat4 inv = inverse(trs);
    CHECK(vec_near(transform_point(inv, transform_point(trs, p)), p, kEps));
}

TEST(math, mat4_inverse_of_singular_returns_identity)
{
    Mat4 singular{};
    const Mat4 result = inverse(singular);
    const Mat4 identity = mat4_identity();
    for (i32 i = 0; i < 16; i++) {
        CHECK_NEAR(result.m[i], identity.m[i], kEps);
    }
}

TEST(math, slerp_halfway)
{
    const Quat a = quat_identity();
    const Quat b = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, kPi * 0.5f);
    const Vec3 half = rotate(slerp(a, b, 0.5f), Vec3{1.0f, 0.0f, 0.0f});
    const f32 inv_sqrt2 = 0.70710678f;
    CHECK(vec_near(half, Vec3{inv_sqrt2, 0.0f, -inv_sqrt2}, kEps));
}

TEST(math, slerp_takes_shortest_arc)
{
    const Quat a = quat_identity();
    const Quat b{0.0f, 0.0f, 0.0f, -1.0f};
    const Quat mid = slerp(a, b, 0.5f);
    CHECK(vec_near(rotate(mid, Vec3{1.0f, 2.0f, 3.0f}), Vec3{1.0f, 2.0f, 3.0f}, kEps));
}

TEST(math, ray_vs_aabb_respects_max_t)
{
    const Ray down{{0.5f, 5.0f, 0.5f}, {0.0f, -1.0f, 0.0f}};
    const Aabb box{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    f32 t = 0.0f;
    CHECK(ray_vs_aabb(down, box, 100.0f, &t));
    CHECK_NEAR(t, 4.0f, kEps);
    CHECK(!ray_vs_aabb(down, box, 3.0f, &t));
}

TEST(math, ray_vs_triangle_and_sphere)
{
    const Ray down{{0.5f, 5.0f, 0.5f}, {0.0f, -1.0f, 0.0f}};
    RayHitTri hit{};
    CHECK(ray_vs_triangle(down, Vec3{-1.0f, 0.0f, -1.0f}, Vec3{2.0f, 0.0f, -1.0f},
                          Vec3{-1.0f, 0.0f, 2.0f}, 100.0f, &hit));
    CHECK_NEAR(hit.t, 5.0f, kEps);

    f32 t = 0.0f;
    CHECK(ray_vs_sphere(down, Sphere{{0.5f, 0.0f, 0.5f}, 1.0f}, 100.0f, &t));
    CHECK_NEAR(t, 4.0f, kEps);
}

TEST(math, closest_point_on_triangle)
{
    const Vec3 a{-1.0f, 0.0f, -1.0f};
    const Vec3 b{2.0f, 0.0f, -1.0f};
    const Vec3 c{-1.0f, 0.0f, 2.0f};
    CHECK(vec_near(closest_point_on_triangle(Vec3{0.0f, 3.0f, 0.0f}, a, b, c),
                   Vec3{0.0f, 0.0f, 0.0f}, kEps));
    CHECK(vec_near(closest_point_on_triangle(Vec3{-5.0f, 0.0f, -5.0f}, a, b, c), a, kEps));
}

TEST(math, frustum_culls_behind_camera)
{
    const Mat4 proj = mat4_perspective(60.0f * kDegToRad, 16.0f / 9.0f, 0.1f, 100.0f);
    const Mat4 view = mat4_look_at(Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, -1.0f},
                                   Vec3{0.0f, 1.0f, 0.0f});
    const Frustum f = frustum_from_view_proj(proj * view);

    const Aabb in_front{{-1.0f, -1.0f, -11.0f}, {1.0f, 1.0f, -9.0f}};
    const Aabb behind{{-1.0f, -1.0f, 9.0f}, {1.0f, 1.0f, 11.0f}};
    const Aabb far_away{{-1.0f, -1.0f, -1000.0f}, {1.0f, 1.0f, -998.0f}};

    CHECK(frustum_test_aabb(f, in_front));
    CHECK(!frustum_test_aabb(f, behind));
    CHECK(!frustum_test_aabb(f, far_away));
}

TEST(math, aabb_transform_and_overlap)
{
    Aabb box = aabb_empty();
    box = expand(box, Vec3{-1.0f, -1.0f, -1.0f});
    box = expand(box, Vec3{1.0f, 1.0f, 1.0f});
    CHECK(contains(box, Vec3{0.0f, 0.0f, 0.0f}));
    CHECK(!contains(box, Vec3{2.0f, 0.0f, 0.0f}));

    const Mat4 shift = mat4_trs(Vec3{10.0f, 0.0f, 0.0f}, quat_identity(), Vec3{1.0f, 1.0f, 1.0f});
    const Aabb moved = transform(shift, box);
    CHECK(!overlaps(box, moved));
    CHECK(overlaps(moved, Aabb{{8.0f, -1.0f, -1.0f}, {10.0f, 1.0f, 1.0f}}));
}

TEST(math, scalar_helpers)
{
    CHECK(f_wrap_angle(3.0f * kPi) <= kPi + kEps);
    CHECK(f_wrap_angle(3.0f * kPi) >= -kPi - kEps);
    CHECK_NEAR(f_move_toward(0.0f, 10.0f, 3.0f), 3.0f, kEps);
    CHECK_NEAR(f_move_toward(0.0f, 2.0f, 3.0f), 2.0f, kEps);
    CHECK_NEAR(f_remap(5.0f, 0.0f, 10.0f, 0.0f, 1.0f), 0.5f, kEps);
    CHECK_NEAR(f_remap(-5.0f, 0.0f, 10.0f, 0.0f, 1.0f), 0.0f, kEps);
    CHECK_NEAR(f_clamp01(1.5f), 1.0f, kEps);
    CHECK_NEAR(f_sign(-3.0f), -1.0f, kEps);
    CHECK_NEAR(f_sign(0.0f), 0.0f, kEps);
}

TEST(math, quat_integrate_stays_normalized)
{
    Quat q = quat_identity();
    const Vec3 omega{1.3f, -0.7f, 2.2f};
    for (i32 i = 0; i < 2000; i++) {
        q = quat_integrate(q, omega, 1.0f / 120.0f);
    }
    CHECK_NEAR(dot(q, q), 1.0f, 1e-3);
}

TEST(glm_bridge, vectors_quats_and_matrices_round_trip_and_agree_on_transforms)
{
    const Vec3 v{1.5f, -2.0f, 3.25f};
    CHECK(from_glm(to_glm(v)).x == v.x && from_glm(to_glm(v)).y == v.y && from_glm(to_glm(v)).z == v.z);
    const Quat q = quat_from_euler(0.4f, -0.3f, 1.1f);
    const Quat back = from_glm(to_glm(q));
    CHECK(back.x == q.x && back.y == q.y && back.z == q.z && back.w == q.w);
    const Mat4 m = mat4_trs(Vec3{3.0f, -1.0f, 2.0f}, q, Vec3{2.0f, 2.0f, 2.0f});
    const Vec3 ours = transform_point(m, v);
    const glm::vec4 theirs = to_glm(m) * glm::vec4(to_glm(v), 1.0f);
    CHECK_NEAR(ours.x, theirs.x, 1e-4);
    CHECK_NEAR(ours.y, theirs.y, 1e-4);
    CHECK_NEAR(ours.z, theirs.z, 1e-4);
    const glm::vec3 rotated = to_glm(q) * to_glm(v);
    const Vec3 mine = rotate(q, v);
    CHECK_NEAR(mine.x, rotated.x, 1e-4);
    CHECK_NEAR(mine.y, rotated.y, 1e-4);
    CHECK_NEAR(mine.z, rotated.z, 1e-4);
    for (int i = 0; i < 16; i++) {
        CHECK(from_glm(to_glm(m)).m[i] == m.m[i]);
    }
}
