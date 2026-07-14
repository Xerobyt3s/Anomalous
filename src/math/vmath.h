#pragma once

#include "core/types.h"
#include <math.h>

#define PI32 3.14159265358979323846f
#define DEG_TO_RAD (PI32 / 180.0f)
#define RAD_TO_DEG (180.0f / PI32)

typedef struct Vec2 { f32 x, y; } Vec2;
typedef struct Vec3 { f32 x, y, z; } Vec3;
typedef struct Vec4 { f32 x, y, z, w; } Vec4;
typedef struct Quat { f32 x, y, z, w; } Quat;
typedef struct Mat3 { f32 m[9]; } Mat3;
typedef struct Mat4 { f32 m[16]; } Mat4;

typedef struct Ray { Vec3 origin; Vec3 dir; } Ray;
typedef struct Aabb { Vec3 min; Vec3 max; } Aabb;
typedef struct Sphere { Vec3 center; f32 radius; } Sphere;
typedef struct Plane { Vec3 normal; f32 d; } Plane;

typedef struct RayHitTri { f32 t, u, v; } RayHitTri;

static inline f32 f_min(f32 a, f32 b) { return a < b ? a : b; }
static inline f32 f_max(f32 a, f32 b) { return a > b ? a : b; }
static inline f32 f_abs(f32 x) { return fabsf(x); }
static inline f32 f_clamp(f32 x, f32 lo, f32 hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline f32 f_clamp01(f32 x) { return f_clamp(x, 0.0f, 1.0f); }
static inline f32 f_lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
static inline f32 f_sign(f32 x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); }

static inline f32 f_remap(f32 x, f32 in_lo, f32 in_hi, f32 out_lo, f32 out_hi)
{
    f32 t = (x - in_lo) / (in_hi - in_lo);
    return f_lerp(out_lo, out_hi, f_clamp01(t));
}

static inline f32 f_move_toward(f32 current, f32 target, f32 max_delta)
{
    f32 delta = target - current;
    if (f_abs(delta) <= max_delta) {
        return target;
    }
    return current + f_sign(delta) * max_delta;
}

static inline f32 f_approach_exp(f32 current, f32 target, f32 rate, f32 dt)
{
    return current + (target - current) * (1.0f - expf(-rate * dt));
}

static inline f32 f_wrap_angle(f32 radians)
{
    f32 wrapped = fmodf(radians + PI32, 2.0f * PI32);
    if (wrapped < 0.0f) {
        wrapped += 2.0f * PI32;
    }
    return wrapped - PI32;
}

static inline Vec2 v2(f32 x, f32 y) { Vec2 v; v.x = x; v.y = y; return v; }
static inline Vec2 vec2_add(Vec2 a, Vec2 b) { return v2(a.x + b.x, a.y + b.y); }
static inline Vec2 vec2_sub(Vec2 a, Vec2 b) { return v2(a.x - b.x, a.y - b.y); }
static inline Vec2 vec2_scale(Vec2 v, f32 s) { return v2(v.x * s, v.y * s); }
static inline f32  vec2_dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
static inline f32  vec2_length_sq(Vec2 v) { return vec2_dot(v, v); }
static inline f32  vec2_length(Vec2 v) { return sqrtf(vec2_length_sq(v)); }
static inline Vec2 vec2_lerp(Vec2 a, Vec2 b, f32 t) { return v2(f_lerp(a.x, b.x, t), f_lerp(a.y, b.y, t)); }

static inline Vec2 vec2_normalize(Vec2 v)
{
    f32 len = vec2_length(v);
    if (len < 1e-8f) {
        return v2(0.0f, 0.0f);
    }
    return vec2_scale(v, 1.0f / len);
}

static inline Vec3 v3(f32 x, f32 y, f32 z) { Vec3 v; v.x = x; v.y = y; v.z = z; return v; }
static inline Vec3 vec3_zero(void) { return v3(0.0f, 0.0f, 0.0f); }
static inline Vec3 vec3_add(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3 vec3_sub(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3 vec3_scale(Vec3 v, f32 s) { return v3(v.x * s, v.y * s, v.z * s); }
static inline Vec3 vec3_mul(Vec3 a, Vec3 b) { return v3(a.x * b.x, a.y * b.y, a.z * b.z); }
static inline Vec3 vec3_negate(Vec3 v) { return v3(-v.x, -v.y, -v.z); }
static inline f32  vec3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline f32  vec3_length_sq(Vec3 v) { return vec3_dot(v, v); }
static inline f32  vec3_length(Vec3 v) { return sqrtf(vec3_length_sq(v)); }
static inline f32  vec3_distance_sq(Vec3 a, Vec3 b) { return vec3_length_sq(vec3_sub(a, b)); }
static inline f32  vec3_distance(Vec3 a, Vec3 b) { return vec3_length(vec3_sub(a, b)); }
static inline Vec3 vec3_lerp(Vec3 a, Vec3 b, f32 t) { return v3(f_lerp(a.x, b.x, t), f_lerp(a.y, b.y, t), f_lerp(a.z, b.z, t)); }
static inline Vec3 vec3_min(Vec3 a, Vec3 b) { return v3(f_min(a.x, b.x), f_min(a.y, b.y), f_min(a.z, b.z)); }
static inline Vec3 vec3_max(Vec3 a, Vec3 b) { return v3(f_max(a.x, b.x), f_max(a.y, b.y), f_max(a.z, b.z)); }

static inline f32 vec3_elem(Vec3 v, i32 index)
{
    return index == 0 ? v.x : (index == 1 ? v.y : v.z);
}

static inline Vec3 vec3_cross(Vec3 a, Vec3 b)
{
    return v3(a.y * b.z - a.z * b.y,
              a.z * b.x - a.x * b.z,
              a.x * b.y - a.y * b.x);
}

static inline Vec3 vec3_normalize(Vec3 v)
{
    f32 len = vec3_length(v);
    if (len < 1e-8f) {
        return vec3_zero();
    }
    return vec3_scale(v, 1.0f / len);
}

static inline Vec3 vec3_reflect(Vec3 v, Vec3 normal)
{
    return vec3_sub(v, vec3_scale(normal, 2.0f * vec3_dot(v, normal)));
}

static inline Vec3 vec3_project_on_plane(Vec3 v, Vec3 plane_normal)
{
    return vec3_sub(v, vec3_scale(plane_normal, vec3_dot(v, plane_normal)));
}

static inline Vec4 v4(f32 x, f32 y, f32 z, f32 w) { Vec4 v; v.x = x; v.y = y; v.z = z; v.w = w; return v; }
static inline Vec4 vec4_from_vec3(Vec3 v, f32 w) { return v4(v.x, v.y, v.z, w); }
static inline Vec4 vec4_add(Vec4 a, Vec4 b) { return v4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
static inline Vec4 vec4_scale(Vec4 v, f32 s) { return v4(v.x * s, v.y * s, v.z * s, v.w * s); }
static inline f32  vec4_dot(Vec4 a, Vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
static inline Vec4 vec4_lerp(Vec4 a, Vec4 b, f32 t) { return vec4_add(a, vec4_scale(vec4_add(b, vec4_scale(a, -1.0f)), t)); }

static inline Quat quat(f32 x, f32 y, f32 z, f32 w) { Quat q; q.x = x; q.y = y; q.z = z; q.w = w; return q; }
static inline Quat quat_identity(void) { return quat(0.0f, 0.0f, 0.0f, 1.0f); }
static inline Quat quat_conjugate(Quat q) { return quat(-q.x, -q.y, -q.z, q.w); }
static inline f32  quat_dot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

static inline Quat quat_normalize(Quat q)
{
    f32 len = sqrtf(quat_dot(q, q));
    if (len < 1e-8f) {
        return quat_identity();
    }
    f32 inv = 1.0f / len;
    return quat(q.x * inv, q.y * inv, q.z * inv, q.w * inv);
}

static inline Quat quat_mul(Quat a, Quat b)
{
    return quat(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

static inline Quat quat_from_axis_angle(Vec3 axis, f32 radians)
{
    f32 half = radians * 0.5f;
    f32 s = sinf(half);
    Vec3 n = vec3_normalize(axis);
    return quat(n.x * s, n.y * s, n.z * s, cosf(half));
}

static inline Quat quat_from_euler(f32 yaw, f32 pitch, f32 roll)
{
    Quat qy = quat_from_axis_angle(v3(0.0f, 1.0f, 0.0f), yaw);
    Quat qp = quat_from_axis_angle(v3(1.0f, 0.0f, 0.0f), pitch);
    Quat qr = quat_from_axis_angle(v3(0.0f, 0.0f, 1.0f), roll);
    return quat_mul(qy, quat_mul(qp, qr));
}

static inline Vec3 quat_rotate_vec3(Quat q, Vec3 v)
{
    Vec3 qv = v3(q.x, q.y, q.z);
    Vec3 t = vec3_scale(vec3_cross(qv, v), 2.0f);
    return vec3_add(vec3_add(v, vec3_scale(t, q.w)), vec3_cross(qv, t));
}

static inline Quat quat_integrate(Quat q, Vec3 angular_vel, f32 dt)
{
    Quat omega = quat(angular_vel.x, angular_vel.y, angular_vel.z, 0.0f);
    Quat delta = quat_mul(omega, q);
    Quat result = quat(q.x + delta.x * 0.5f * dt,
                       q.y + delta.y * 0.5f * dt,
                       q.z + delta.z * 0.5f * dt,
                       q.w + delta.w * 0.5f * dt);
    return quat_normalize(result);
}

Quat quat_slerp(Quat a, Quat b, f32 t);
Mat3 quat_to_mat3(Quat q);
Mat4 quat_to_mat4(Quat q);

Mat3 mat3_identity(void);
Mat3 mat3_diag(f32 x, f32 y, f32 z);
Mat3 mat3_mul(Mat3 a, Mat3 b);
Mat3 mat3_transpose(Mat3 m);
Vec3 mat3_mul_vec3(Mat3 m, Vec3 v);

Mat4 mat4_identity(void);
Mat4 mat4_mul(Mat4 a, Mat4 b);
Mat4 mat4_transpose(Mat4 m);
Mat4 mat4_inverse(Mat4 m);
Mat4 mat4_trs(Vec3 pos, Quat rot, Vec3 scale);
Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up);
Mat4 mat4_perspective(f32 fovy_radians, f32 aspect, f32 znear, f32 zfar);
Vec3 mat4_transform_point(Mat4 m, Vec3 p);
Vec3 mat4_transform_dir(Mat4 m, Vec3 d);

Aabb aabb_empty(void);
Aabb aabb_expand(Aabb box, Vec3 point);
Aabb aabb_union(Aabb a, Aabb b);
Aabb aabb_transform(Mat4 m, Aabb box);
b32  aabb_contains_point(Aabb box, Vec3 point);
b32  aabb_vs_aabb(Aabb a, Aabb b);

b32  ray_vs_aabb(Ray ray, Aabb box, f32 max_t, f32* out_t);
b32  ray_vs_sphere(Ray ray, Sphere sphere, f32 max_t, f32* out_t);
b32  ray_vs_plane(Ray ray, Plane plane, f32 max_t, f32* out_t);
b32  ray_vs_triangle(Ray ray, Vec3 a, Vec3 b, Vec3 c, f32 max_t, RayHitTri* out_hit);
Vec3 closest_point_on_triangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c);

void math_selftest(void);
