#pragma once

#include "core/types.h"

#include <cmath>

namespace anom {

inline constexpr f32 kPi = 3.14159265358979323846f;
inline constexpr f32 kTau = 2.0f * kPi;
inline constexpr f32 kDegToRad = kPi / 180.0f;
inline constexpr f32 kRadToDeg = 180.0f / kPi;

struct Vec2 {
    f32 x, y;
};

struct Vec3 {
    f32 x, y, z;
};

struct Vec4 {
    f32 x, y, z, w;
};

struct Quat {
    f32 x, y, z, w;
};

struct Mat3 {
    f32 m[9];
};

struct Mat4 {
    f32 m[16];
};

struct Ray {
    Vec3 origin;
    Vec3 dir;
};

struct Aabb {
    Vec3 min;
    Vec3 max;
};

struct Sphere {
    Vec3 center;
    f32 radius;
};

struct Plane {
    Vec3 normal;
    f32 d;
};

struct RayHitTri {
    f32 t, u, v;
};

struct Frustum {
    Plane planes[6];
};

constexpr f32 f_min(f32 a, f32 b) { return a < b ? a : b; }
constexpr f32 f_max(f32 a, f32 b) { return a > b ? a : b; }
constexpr f32 f_clamp(f32 x, f32 lo, f32 hi) { return x < lo ? lo : (x > hi ? hi : x); }
constexpr f32 f_clamp01(f32 x) { return f_clamp(x, 0.0f, 1.0f); }
constexpr f32 f_lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
constexpr f32 f_sign(f32 x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); }

inline f32 f_abs(f32 x) { return std::fabs(x); }

constexpr f32 f_remap(f32 x, f32 in_lo, f32 in_hi, f32 out_lo, f32 out_hi)
{
    return f_lerp(out_lo, out_hi, f_clamp01((x - in_lo) / (in_hi - in_lo)));
}

inline f32 f_move_toward(f32 current, f32 target, f32 max_delta)
{
    const f32 delta = target - current;
    if (f_abs(delta) <= max_delta) {
        return target;
    }
    return current + f_sign(delta) * max_delta;
}

inline f32 f_approach_exp(f32 current, f32 target, f32 rate, f32 dt)
{
    return current + (target - current) * (1.0f - std::exp(-rate * dt));
}

inline f32 f_wrap_angle(f32 radians)
{
    f32 wrapped = std::fmod(radians + kPi, kTau);
    if (wrapped < 0.0f) {
        wrapped += kTau;
    }
    return wrapped - kPi;
}

constexpr Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
constexpr Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
constexpr Vec2 operator-(Vec2 v) { return {-v.x, -v.y}; }
constexpr Vec2 operator*(Vec2 v, f32 s) { return {v.x * s, v.y * s}; }
constexpr Vec2 operator*(f32 s, Vec2 v) { return v * s; }
constexpr Vec2 operator/(Vec2 v, f32 s) { return {v.x / s, v.y / s}; }
constexpr Vec2& operator+=(Vec2& a, Vec2 b) { a = a + b; return a; }
constexpr Vec2& operator-=(Vec2& a, Vec2 b) { a = a - b; return a; }
constexpr Vec2& operator*=(Vec2& v, f32 s) { v = v * s; return v; }
constexpr bool operator==(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }

constexpr f32 dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
constexpr f32 length_sq(Vec2 v) { return dot(v, v); }
inline f32 length(Vec2 v) { return std::sqrt(length_sq(v)); }
constexpr Vec2 lerp(Vec2 a, Vec2 b, f32 t) { return {f_lerp(a.x, b.x, t), f_lerp(a.y, b.y, t)}; }

inline Vec2 normalize(Vec2 v)
{
    const f32 len = length(v);
    if (len < 1e-8f) {
        return {0.0f, 0.0f};
    }
    return v * (1.0f / len);
}

constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator-(Vec3 v) { return {-v.x, -v.y, -v.z}; }
constexpr Vec3 operator*(Vec3 v, f32 s) { return {v.x * s, v.y * s, v.z * s}; }
constexpr Vec3 operator*(f32 s, Vec3 v) { return v * s; }
constexpr Vec3 operator/(Vec3 v, f32 s) { return {v.x / s, v.y / s, v.z / s}; }
constexpr Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }
constexpr Vec3& operator-=(Vec3& a, Vec3 b) { a = a - b; return a; }
constexpr Vec3& operator*=(Vec3& v, f32 s) { v = v * s; return v; }
constexpr bool operator==(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

constexpr Vec3 hadamard(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
constexpr f32 dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr f32 length_sq(Vec3 v) { return dot(v, v); }
inline f32 length(Vec3 v) { return std::sqrt(length_sq(v)); }
constexpr f32 distance_sq(Vec3 a, Vec3 b) { return length_sq(a - b); }
inline f32 distance(Vec3 a, Vec3 b) { return length(a - b); }

constexpr Vec3 lerp(Vec3 a, Vec3 b, f32 t)
{
    return {f_lerp(a.x, b.x, t), f_lerp(a.y, b.y, t), f_lerp(a.z, b.z, t)};
}

constexpr Vec3 vec_min(Vec3 a, Vec3 b)
{
    return {f_min(a.x, b.x), f_min(a.y, b.y), f_min(a.z, b.z)};
}

constexpr Vec3 vec_max(Vec3 a, Vec3 b)
{
    return {f_max(a.x, b.x), f_max(a.y, b.y), f_max(a.z, b.z)};
}

constexpr f32 elem(Vec3 v, i32 index)
{
    return index == 0 ? v.x : (index == 1 ? v.y : v.z);
}

constexpr Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline Vec3 normalize(Vec3 v)
{
    const f32 len = length(v);
    if (len < 1e-8f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return v * (1.0f / len);
}

constexpr f32 luminance(Vec3 c)
{
    return c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f;
}

constexpr Vec3 reflect(Vec3 v, Vec3 normal)
{
    return v - normal * (2.0f * dot(v, normal));
}

constexpr Vec3 project_on_plane(Vec3 v, Vec3 plane_normal)
{
    return v - plane_normal * dot(v, plane_normal);
}

constexpr Vec4 operator+(Vec4 a, Vec4 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}

constexpr Vec4 operator-(Vec4 a, Vec4 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}

constexpr Vec4 operator*(Vec4 v, f32 s) { return {v.x * s, v.y * s, v.z * s, v.w * s}; }
constexpr Vec4 operator*(f32 s, Vec4 v) { return v * s; }
constexpr f32 dot(Vec4 a, Vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
constexpr Vec4 vec4_from_vec3(Vec3 v, f32 w) { return {v.x, v.y, v.z, w}; }
constexpr Vec3 xyz(Vec4 v) { return {v.x, v.y, v.z}; }

constexpr Vec4 lerp(Vec4 a, Vec4 b, f32 t)
{
    return {f_lerp(a.x, b.x, t), f_lerp(a.y, b.y, t), f_lerp(a.z, b.z, t), f_lerp(a.w, b.w, t)};
}

constexpr Quat quat_identity() { return {0.0f, 0.0f, 0.0f, 1.0f}; }
constexpr Quat conjugate(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
constexpr f32 dot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
constexpr bool operator==(Quat a, Quat b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

inline Quat normalize(Quat q)
{
    const f32 len = std::sqrt(dot(q, q));
    if (len < 1e-8f) {
        return quat_identity();
    }
    const f32 inv = 1.0f / len;
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

constexpr Quat operator*(Quat a, Quat b)
{
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

inline Quat quat_from_axis_angle(Vec3 axis, f32 radians)
{
    const f32 half = radians * 0.5f;
    const f32 s = std::sin(half);
    const Vec3 n = normalize(axis);
    return {n.x * s, n.y * s, n.z * s, std::cos(half)};
}

inline Quat quat_from_euler(f32 yaw_rad, f32 pitch_rad, f32 roll_rad)
{
    const Quat qy = quat_from_axis_angle({0.0f, 1.0f, 0.0f}, yaw_rad);
    const Quat qp = quat_from_axis_angle({1.0f, 0.0f, 0.0f}, pitch_rad);
    const Quat qr = quat_from_axis_angle({0.0f, 0.0f, 1.0f}, roll_rad);
    return qy * (qp * qr);
}

constexpr Vec3 rotate(Quat q, Vec3 v)
{
    const Vec3 qv{q.x, q.y, q.z};
    const Vec3 t = cross(qv, v) * 2.0f;
    return v + t * q.w + cross(qv, t);
}

inline f32 quat_yaw(Quat q)
{
    return std::atan2(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.y * q.y + q.x * q.x));
}

inline Quat quat_integrate(Quat q, Vec3 angular_vel, f32 dt)
{
    const Quat omega{angular_vel.x, angular_vel.y, angular_vel.z, 0.0f};
    const Quat delta = omega * q;
    return normalize(Quat{q.x + delta.x * 0.5f * dt,
                          q.y + delta.y * 0.5f * dt,
                          q.z + delta.z * 0.5f * dt,
                          q.w + delta.w * 0.5f * dt});
}

Quat slerp(Quat a, Quat b, f32 t);
Mat3 quat_to_mat3(Quat q);
Mat4 quat_to_mat4(Quat q);

Mat3 mat3_identity();
Mat3 mat3_diag(f32 x, f32 y, f32 z);
Mat3 operator*(const Mat3& a, const Mat3& b);
Vec3 operator*(const Mat3& m, Vec3 v);
Mat3 transpose(const Mat3& m);

Mat4 mat4_identity();
Mat4 operator*(const Mat4& a, const Mat4& b);
Mat4 transpose(const Mat4& m);
Mat4 inverse(const Mat4& m);
Mat4 mat4_trs(Vec3 pos, Quat rot, Vec3 scale);
Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up);
Mat4 mat4_perspective(f32 fovy_radians, f32 aspect, f32 znear, f32 zfar);
Mat4 mat4_ortho(f32 left, f32 right, f32 bottom, f32 top, f32 znear, f32 zfar);
Vec3 transform_point(const Mat4& m, Vec3 p);
Vec3 transform_dir(const Mat4& m, Vec3 d);

Aabb aabb_empty();
Aabb expand(Aabb box, Vec3 point);
Aabb combine(Aabb a, Aabb b);
Aabb transform(const Mat4& m, Aabb box);
bool contains(Aabb box, Vec3 point);
bool overlaps(Aabb a, Aabb b);

Frustum frustum_from_view_proj(const Mat4& view_proj);
bool frustum_test_aabb(const Frustum& frustum, Aabb box);

f32 closest_point_on_line_to_ray(Vec3 line_point, Vec3 line_dir, Ray ray);
bool ray_vs_aabb(Ray ray, Aabb box, f32 max_t, f32* out_t);
bool ray_vs_sphere(Ray ray, Sphere sphere, f32 max_t, f32* out_t);
bool ray_vs_plane(Ray ray, Plane plane, f32 max_t, f32* out_t);
bool ray_vs_triangle(Ray ray, Vec3 a, Vec3 b, Vec3 c, f32 max_t, RayHitTri* out_hit);
Vec3 closest_point_on_triangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c);

} // namespace anom
