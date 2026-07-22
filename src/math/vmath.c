#include "math/vmath.h"
#include "core/log.h"

Quat quat_slerp(Quat a, Quat b, f32 t)
{
    f32 dot = quat_dot(a, b);
    if (dot < 0.0f) {
        b = quat(-b.x, -b.y, -b.z, -b.w);
        dot = -dot;
    }
    if (dot > 0.9995f) {
        Quat result = quat(f_lerp(a.x, b.x, t),
                           f_lerp(a.y, b.y, t),
                           f_lerp(a.z, b.z, t),
                           f_lerp(a.w, b.w, t));
        return quat_normalize(result);
    }
    f32 theta = acosf(f_clamp(dot, -1.0f, 1.0f));
    f32 sin_theta = sinf(theta);
    f32 wa = sinf((1.0f - t) * theta) / sin_theta;
    f32 wb = sinf(t * theta) / sin_theta;
    return quat(a.x * wa + b.x * wb,
                a.y * wa + b.y * wb,
                a.z * wa + b.z * wb,
                a.w * wa + b.w * wb);
}

Mat3 quat_to_mat3(Quat q)
{
    f32 xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    f32 xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    f32 wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    Mat3 m;
    m.m[0] = 1.0f - 2.0f * (yy + zz);
    m.m[1] = 2.0f * (xy + wz);
    m.m[2] = 2.0f * (xz - wy);
    m.m[3] = 2.0f * (xy - wz);
    m.m[4] = 1.0f - 2.0f * (xx + zz);
    m.m[5] = 2.0f * (yz + wx);
    m.m[6] = 2.0f * (xz + wy);
    m.m[7] = 2.0f * (yz - wx);
    m.m[8] = 1.0f - 2.0f * (xx + yy);
    return m;
}

Mat4 quat_to_mat4(Quat q)
{
    Mat3 r = quat_to_mat3(q);
    Mat4 m = mat4_identity();
    m.m[0] = r.m[0]; m.m[1] = r.m[1]; m.m[2]  = r.m[2];
    m.m[4] = r.m[3]; m.m[5] = r.m[4]; m.m[6]  = r.m[5];
    m.m[8] = r.m[6]; m.m[9] = r.m[7]; m.m[10] = r.m[8];
    return m;
}

Mat3 mat3_identity(void)
{
    return mat3_diag(1.0f, 1.0f, 1.0f);
}

Mat3 mat3_diag(f32 x, f32 y, f32 z)
{
    Mat3 m = {0};
    m.m[0] = x;
    m.m[4] = y;
    m.m[8] = z;
    return m;
}

Mat3 mat3_mul(Mat3 a, Mat3 b)
{
    Mat3 result;
    for (i32 col = 0; col < 3; col++) {
        for (i32 row = 0; row < 3; row++) {
            f32 sum = 0.0f;
            for (i32 k = 0; k < 3; k++) {
                sum += a.m[k * 3 + row] * b.m[col * 3 + k];
            }
            result.m[col * 3 + row] = sum;
        }
    }
    return result;
}

Mat3 mat3_transpose(Mat3 m)
{
    Mat3 result;
    for (i32 col = 0; col < 3; col++) {
        for (i32 row = 0; row < 3; row++) {
            result.m[col * 3 + row] = m.m[row * 3 + col];
        }
    }
    return result;
}

Vec3 mat3_mul_vec3(Mat3 m, Vec3 v)
{
    return v3(m.m[0] * v.x + m.m[3] * v.y + m.m[6] * v.z,
              m.m[1] * v.x + m.m[4] * v.y + m.m[7] * v.z,
              m.m[2] * v.x + m.m[5] * v.y + m.m[8] * v.z);
}

Mat4 mat4_identity(void)
{
    Mat4 m = {0};
    m.m[0] = 1.0f;
    m.m[5] = 1.0f;
    m.m[10] = 1.0f;
    m.m[15] = 1.0f;
    return m;
}

Mat4 mat4_mul(Mat4 a, Mat4 b)
{
    Mat4 result;
    for (i32 col = 0; col < 4; col++) {
        for (i32 row = 0; row < 4; row++) {
            f32 sum = 0.0f;
            for (i32 k = 0; k < 4; k++) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            result.m[col * 4 + row] = sum;
        }
    }
    return result;
}

Mat4 mat4_transpose(Mat4 m)
{
    Mat4 result;
    for (i32 col = 0; col < 4; col++) {
        for (i32 row = 0; row < 4; row++) {
            result.m[col * 4 + row] = m.m[row * 4 + col];
        }
    }
    return result;
}

Mat4 mat4_inverse(Mat4 mat)
{
    const f32* m = mat.m;
    f32 inv[16];

    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15]
           + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15]
           - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15]
           + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14]
            - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15]
           - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15]
           + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15]
           - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14]
            + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15]
           + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15]
           - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15]
            + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14]
            - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11]
           - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11]
           + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11]
            - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10]
            + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    f32 det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (f_abs(det) < 1e-12f) {
        return mat4_identity();
    }
    f32 inv_det = 1.0f / det;
    Mat4 result;
    for (i32 i = 0; i < 16; i++) {
        result.m[i] = inv[i] * inv_det;
    }
    return result;
}

Mat4 mat4_trs(Vec3 pos, Quat rot, Vec3 scale)
{
    Mat3 r = quat_to_mat3(rot);
    Mat4 m = mat4_identity();
    m.m[0] = r.m[0] * scale.x; m.m[1] = r.m[1] * scale.x; m.m[2]  = r.m[2] * scale.x;
    m.m[4] = r.m[3] * scale.y; m.m[5] = r.m[4] * scale.y; m.m[6]  = r.m[5] * scale.y;
    m.m[8] = r.m[6] * scale.z; m.m[9] = r.m[7] * scale.z; m.m[10] = r.m[8] * scale.z;
    m.m[12] = pos.x; m.m[13] = pos.y; m.m[14] = pos.z;
    return m;
}

Mat4 mat4_look_at(Vec3 eye, Vec3 target, Vec3 up)
{
    Vec3 f = vec3_normalize(vec3_sub(target, eye));
    Vec3 s = vec3_normalize(vec3_cross(f, up));
    Vec3 u = vec3_cross(s, f);
    Mat4 m = mat4_identity();
    m.m[0] = s.x; m.m[4] = s.y; m.m[8] = s.z;
    m.m[1] = u.x; m.m[5] = u.y; m.m[9] = u.z;
    m.m[2] = -f.x; m.m[6] = -f.y; m.m[10] = -f.z;
    m.m[12] = -vec3_dot(s, eye);
    m.m[13] = -vec3_dot(u, eye);
    m.m[14] = vec3_dot(f, eye);
    return m;
}

Mat4 mat4_perspective(f32 fovy_radians, f32 aspect, f32 znear, f32 zfar)
{
    f32 f = 1.0f / tanf(fovy_radians * 0.5f);
    Mat4 m = {0};
    m.m[0] = f / aspect;
    m.m[5] = f;
    m.m[10] = (zfar + znear) / (znear - zfar);
    m.m[11] = -1.0f;
    m.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    return m;
}

Mat4 mat4_ortho(f32 left, f32 right, f32 bottom, f32 top, f32 znear, f32 zfar)
{
    Mat4 m = {0};
    m.m[0] = 2.0f / (right - left);
    m.m[5] = 2.0f / (top - bottom);
    m.m[10] = -2.0f / (zfar - znear);
    m.m[12] = -(right + left) / (right - left);
    m.m[13] = -(top + bottom) / (top - bottom);
    m.m[14] = -(zfar + znear) / (zfar - znear);
    m.m[15] = 1.0f;
    return m;
}

Vec3 mat4_transform_point(Mat4 m, Vec3 p)
{
    return v3(m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
              m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
              m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]);
}

Vec3 mat4_transform_dir(Mat4 m, Vec3 d)
{
    return v3(m.m[0] * d.x + m.m[4] * d.y + m.m[8] * d.z,
              m.m[1] * d.x + m.m[5] * d.y + m.m[9] * d.z,
              m.m[2] * d.x + m.m[6] * d.y + m.m[10] * d.z);
}

Aabb aabb_empty(void)
{
    Aabb box;
    box.min = v3(1e30f, 1e30f, 1e30f);
    box.max = v3(-1e30f, -1e30f, -1e30f);
    return box;
}

Aabb aabb_expand(Aabb box, Vec3 point)
{
    box.min = vec3_min(box.min, point);
    box.max = vec3_max(box.max, point);
    return box;
}

Aabb aabb_union(Aabb a, Aabb b)
{
    Aabb box;
    box.min = vec3_min(a.min, b.min);
    box.max = vec3_max(a.max, b.max);
    return box;
}

Aabb aabb_transform(Mat4 m, Aabb box)
{
    Aabb result = aabb_empty();
    for (i32 i = 0; i < 8; i++) {
        Vec3 corner = v3((i & 1) ? box.max.x : box.min.x,
                         (i & 2) ? box.max.y : box.min.y,
                         (i & 4) ? box.max.z : box.min.z);
        result = aabb_expand(result, mat4_transform_point(m, corner));
    }
    return result;
}

b32 aabb_contains_point(Aabb box, Vec3 p)
{
    return p.x >= box.min.x && p.x <= box.max.x
        && p.y >= box.min.y && p.y <= box.max.y
        && p.z >= box.min.z && p.z <= box.max.z;
}

b32 aabb_vs_aabb(Aabb a, Aabb b)
{
    return a.min.x <= b.max.x && a.max.x >= b.min.x
        && a.min.y <= b.max.y && a.max.y >= b.min.y
        && a.min.z <= b.max.z && a.max.z >= b.min.z;
}

Frustum frustum_from_view_proj(Mat4 view_proj)
{
    const f32* m = view_proj.m;
    Vec4 rows[4];
    for (i32 i = 0; i < 4; i++) {
        rows[i] = v4(m[i], m[4 + i], m[8 + i], m[12 + i]);
    }
    Vec4 raw[6] = {
        vec4_add(rows[3], rows[0]),
        vec4_add(rows[3], vec4_scale(rows[0], -1.0f)),
        vec4_add(rows[3], rows[1]),
        vec4_add(rows[3], vec4_scale(rows[1], -1.0f)),
        vec4_add(rows[3], rows[2]),
        vec4_add(rows[3], vec4_scale(rows[2], -1.0f)),
    };
    Frustum frustum;
    for (i32 i = 0; i < 6; i++) {
        Vec3 normal = v3(raw[i].x, raw[i].y, raw[i].z);
        f32 inv_len = 1.0f / f_max(vec3_length(normal), 1e-8f);
        frustum.planes[i].normal = vec3_scale(normal, inv_len);
        frustum.planes[i].d = raw[i].w * inv_len;
    }
    return frustum;
}

b32 frustum_test_aabb(const Frustum* frustum, Aabb box)
{
    for (i32 i = 0; i < 6; i++) {
        Vec3 n = frustum->planes[i].normal;
        Vec3 far_corner = v3(n.x > 0.0f ? box.max.x : box.min.x,
                             n.y > 0.0f ? box.max.y : box.min.y,
                             n.z > 0.0f ? box.max.z : box.min.z);
        if (vec3_dot(n, far_corner) + frustum->planes[i].d < 0.0f) {
            return 0;
        }
    }
    return 1;
}

f32 closest_point_on_line_to_ray(Vec3 line_point, Vec3 line_dir, Ray ray)
{
    Vec3 u = vec3_normalize(line_dir);
    Vec3 v = vec3_normalize(ray.dir);
    Vec3 w0 = vec3_sub(line_point, ray.origin);
    f32 b = vec3_dot(u, v);
    f32 d = vec3_dot(u, w0);
    f32 e = vec3_dot(v, w0);
    f32 denom = 1.0f - b * b;
    if (denom < 1e-6f) {
        return -d;
    }
    return (b * e - d) / denom;
}

b32 ray_vs_aabb(Ray ray, Aabb box, f32 max_t, f32* out_t)
{
    f32 tmin = 0.0f;
    f32 tmax = max_t;
    for (i32 axis = 0; axis < 3; axis++) {
        f32 origin = vec3_elem(ray.origin, axis);
        f32 dir = vec3_elem(ray.dir, axis);
        f32 slab_min = vec3_elem(box.min, axis);
        f32 slab_max = vec3_elem(box.max, axis);
        if (f_abs(dir) < 1e-9f) {
            if (origin < slab_min || origin > slab_max) {
                return 0;
            }
        } else {
            f32 inv_dir = 1.0f / dir;
            f32 t1 = (slab_min - origin) * inv_dir;
            f32 t2 = (slab_max - origin) * inv_dir;
            if (t1 > t2) {
                f32 swap = t1;
                t1 = t2;
                t2 = swap;
            }
            tmin = f_max(tmin, t1);
            tmax = f_min(tmax, t2);
            if (tmin > tmax) {
                return 0;
            }
        }
    }
    if (out_t) {
        *out_t = tmin;
    }
    return 1;
}

b32 ray_vs_sphere(Ray ray, Sphere sphere, f32 max_t, f32* out_t)
{
    Vec3 oc = vec3_sub(ray.origin, sphere.center);
    f32 b = vec3_dot(oc, ray.dir);
    f32 c = vec3_length_sq(oc) - sphere.radius * sphere.radius;
    f32 discriminant = b * b - c;
    if (discriminant < 0.0f) {
        return 0;
    }
    f32 t = -b - sqrtf(discriminant);
    if (t < 0.0f || t > max_t) {
        return 0;
    }
    if (out_t) {
        *out_t = t;
    }
    return 1;
}

b32 ray_vs_plane(Ray ray, Plane plane, f32 max_t, f32* out_t)
{
    f32 denom = vec3_dot(plane.normal, ray.dir);
    if (f_abs(denom) < 1e-9f) {
        return 0;
    }
    f32 t = (plane.d - vec3_dot(plane.normal, ray.origin)) / denom;
    if (t < 0.0f || t > max_t) {
        return 0;
    }
    if (out_t) {
        *out_t = t;
    }
    return 1;
}

b32 ray_vs_triangle(Ray ray, Vec3 a, Vec3 b, Vec3 c, f32 max_t, RayHitTri* out_hit)
{
    Vec3 edge1 = vec3_sub(b, a);
    Vec3 edge2 = vec3_sub(c, a);
    Vec3 pvec = vec3_cross(ray.dir, edge2);
    f32 det = vec3_dot(edge1, pvec);
    if (f_abs(det) < 1e-9f) {
        return 0;
    }
    f32 inv_det = 1.0f / det;
    Vec3 tvec = vec3_sub(ray.origin, a);
    f32 u = vec3_dot(tvec, pvec) * inv_det;
    if (u < 0.0f || u > 1.0f) {
        return 0;
    }
    Vec3 qvec = vec3_cross(tvec, edge1);
    f32 v = vec3_dot(ray.dir, qvec) * inv_det;
    if (v < 0.0f || u + v > 1.0f) {
        return 0;
    }
    f32 t = vec3_dot(edge2, qvec) * inv_det;
    if (t < 0.0f || t > max_t) {
        return 0;
    }
    if (out_hit) {
        out_hit->t = t;
        out_hit->u = u;
        out_hit->v = v;
    }
    return 1;
}

Vec3 closest_point_on_triangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c)
{
    Vec3 ab = vec3_sub(b, a);
    Vec3 ac = vec3_sub(c, a);
    Vec3 ap = vec3_sub(p, a);
    f32 d1 = vec3_dot(ab, ap);
    f32 d2 = vec3_dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        return a;
    }

    Vec3 bp = vec3_sub(p, b);
    f32 d3 = vec3_dot(ab, bp);
    f32 d4 = vec3_dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        return b;
    }

    f32 vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        f32 v = d1 / (d1 - d3);
        return vec3_add(a, vec3_scale(ab, v));
    }

    Vec3 cp = vec3_sub(p, c);
    f32 d5 = vec3_dot(ab, cp);
    f32 d6 = vec3_dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        return c;
    }

    f32 vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        f32 w = d2 / (d2 - d6);
        return vec3_add(a, vec3_scale(ac, w));
    }

    f32 va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        f32 w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return vec3_add(b, vec3_scale(vec3_sub(c, b), w));
    }

    f32 denom = 1.0f / (va + vb + vc);
    f32 v = vb * denom;
    f32 w = vc * denom;
    return vec3_add(a, vec3_add(vec3_scale(ab, v), vec3_scale(ac, w)));
}

static b32 nearly_equal(f32 a, f32 b, f32 eps)
{
    return f_abs(a - b) <= eps;
}

static b32 vec3_nearly_equal(Vec3 a, Vec3 b, f32 eps)
{
    return nearly_equal(a.x, b.x, eps) && nearly_equal(a.y, b.y, eps) && nearly_equal(a.z, b.z, eps);
}

void math_selftest(void)
{
    f32 eps = 1e-4f;

    Vec3 x_axis = v3(1.0f, 0.0f, 0.0f);
    Vec3 y_axis = v3(0.0f, 1.0f, 0.0f);
    Vec3 z_axis = v3(0.0f, 0.0f, 1.0f);
    ASSERT(vec3_nearly_equal(vec3_cross(x_axis, y_axis), z_axis, eps));
    ASSERT(nearly_equal(vec3_length(v3(3.0f, 4.0f, 0.0f)), 5.0f, eps));

    Quat rot90_y = quat_from_axis_angle(y_axis, PI32 * 0.5f);
    Vec3 rotated = quat_rotate_vec3(rot90_y, x_axis);
    ASSERT(vec3_nearly_equal(rotated, v3(0.0f, 0.0f, -1.0f), eps));

    Mat4 rot_mat = quat_to_mat4(rot90_y);
    Vec3 mat_rotated = mat4_transform_dir(rot_mat, x_axis);
    ASSERT(vec3_nearly_equal(mat_rotated, rotated, eps));

    Quat q = quat_from_euler(0.7f, 0.3f, -0.2f);
    Vec3 test_point = v3(1.5f, -2.0f, 3.0f);
    Vec3 via_quat = quat_rotate_vec3(q, test_point);
    Vec3 via_mat3 = mat3_mul_vec3(quat_to_mat3(q), test_point);
    ASSERT(vec3_nearly_equal(via_quat, via_mat3, eps));

    Mat4 trs = mat4_trs(v3(3.0f, -1.0f, 2.0f), q, v3(1.0f, 1.0f, 1.0f));
    Mat4 inv = mat4_inverse(trs);
    Vec3 round_trip = mat4_transform_point(inv, mat4_transform_point(trs, test_point));
    ASSERT(vec3_nearly_equal(round_trip, test_point, eps));

    Quat slerp_start = quat_identity();
    Quat slerp_end = quat_from_axis_angle(y_axis, PI32 * 0.5f);
    Quat slerp_half = quat_slerp(slerp_start, slerp_end, 0.5f);
    Vec3 half_rotated = quat_rotate_vec3(slerp_half, x_axis);
    f32 inv_sqrt2 = 0.70710678f;
    ASSERT(vec3_nearly_equal(half_rotated, v3(inv_sqrt2, 0.0f, -inv_sqrt2), eps));

    Ray down_ray;
    down_ray.origin = v3(0.5f, 5.0f, 0.5f);
    down_ray.dir = v3(0.0f, -1.0f, 0.0f);
    Aabb unit_box;
    unit_box.min = v3(0.0f, 0.0f, 0.0f);
    unit_box.max = v3(1.0f, 1.0f, 1.0f);
    f32 t = 0.0f;
    ASSERT(ray_vs_aabb(down_ray, unit_box, 100.0f, &t));
    ASSERT(nearly_equal(t, 4.0f, eps));
    ASSERT(!ray_vs_aabb(down_ray, unit_box, 3.0f, &t));

    RayHitTri tri_hit;
    Vec3 tri_a = v3(-1.0f, 0.0f, -1.0f);
    Vec3 tri_b = v3(2.0f, 0.0f, -1.0f);
    Vec3 tri_c = v3(-1.0f, 0.0f, 2.0f);
    ASSERT(ray_vs_triangle(down_ray, tri_a, tri_b, tri_c, 100.0f, &tri_hit));
    ASSERT(nearly_equal(tri_hit.t, 5.0f, eps));

    Sphere sphere;
    sphere.center = v3(0.5f, 0.0f, 0.5f);
    sphere.radius = 1.0f;
    ASSERT(ray_vs_sphere(down_ray, sphere, 100.0f, &t));
    ASSERT(nearly_equal(t, 4.0f, eps));

    Vec3 closest = closest_point_on_triangle(v3(0.0f, 3.0f, 0.0f), tri_a, tri_b, tri_c);
    ASSERT(vec3_nearly_equal(closest, v3(0.0f, 0.0f, 0.0f), eps));
    closest = closest_point_on_triangle(v3(-5.0f, 0.0f, -5.0f), tri_a, tri_b, tri_c);
    ASSERT(vec3_nearly_equal(closest, tri_a, eps));

    ASSERT(nearly_equal(f_wrap_angle(3.0f * PI32), PI32, eps) || nearly_equal(f_wrap_angle(3.0f * PI32), -PI32, eps));
    ASSERT(nearly_equal(f_move_toward(0.0f, 10.0f, 3.0f), 3.0f, eps));
    ASSERT(nearly_equal(f_remap(5.0f, 0.0f, 10.0f, 0.0f, 1.0f), 0.5f, eps));

    log_info("math selftest passed");
}
