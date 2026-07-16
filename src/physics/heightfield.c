#include "physics/heightfield.h"
#include "core/arena.h"
#include "core/rng.h"

#define HF_RAY_EPS 1e-4f
#define HF_T_INF 1e30f

static void heightfield_alloc(Heightfield* hf, struct Arena* arena, u32 size, f32 cell_size)
{
    hf->size_x = size;
    hf->size_z = size;
    hf->cell_size = cell_size;
    f32 half_x = (f32)(size - 1) * cell_size * 0.5f;
    hf->origin = v3(-half_x, 0.0f, -half_x);
    hf->heights = arena_push_array(arena, f32, (u64)size * size);
    hf->min_height = HF_T_INF;
    hf->max_height = -HF_T_INF;
}

void heightfield_init_procedural(Heightfield* hf, struct Arena* arena, u32 size, f32 cell_size, u32 seed, f32 roughness)
{
    heightfield_alloc(hf, arena, size, cell_size);

    Rng rng;
    rng_seed(&rng, seed);
    f32 p1 = rng_range(&rng, 0.0f, 2.0f * PI32);
    f32 p2 = rng_range(&rng, 0.0f, 2.0f * PI32);
    f32 p3 = rng_range(&rng, 0.0f, 2.0f * PI32);
    f32 p4 = rng_range(&rng, 0.0f, 2.0f * PI32);
    f32 p5 = rng_range(&rng, 0.0f, 2.0f * PI32);
    f32 p6 = rng_range(&rng, 0.0f, 2.0f * PI32);

    for (u32 iz = 0; iz < size; iz++) {
        for (u32 ix = 0; ix < size; ix++) {
            f32 x = hf->origin.x + (f32)ix * cell_size;
            f32 z = hf->origin.z + (f32)iz * cell_size;
            f32 bowl = (x * x + z * z) * 0.006f;
            f32 waves = 1.6f * sinf(x * 0.19f + p1) * cosf(z * 0.16f + p2)
                      + 0.7f * sinf(x * 0.47f + p3) * sinf(z * 0.41f + p4)
                      + 0.3f * cosf(x * 0.83f + p5) * cosf(z * 0.77f + p6);
            f32 h = roughness * (bowl + waves);
            hf->heights[(u64)iz * size + ix] = h;
            hf->min_height = f_min(hf->min_height, h);
            hf->max_height = f_max(hf->max_height, h);
        }
    }
}

void heightfield_init_slope(Heightfield* hf, struct Arena* arena, u32 size, f32 cell_size, f32 grade)
{
    heightfield_alloc(hf, arena, size, cell_size);
    for (u32 iz = 0; iz < size; iz++) {
        for (u32 ix = 0; ix < size; ix++) {
            f32 x = hf->origin.x + (f32)ix * cell_size;
            f32 h = f_max(x, 0.0f) * grade;
            hf->heights[(u64)iz * size + ix] = h;
            hf->min_height = f_min(hf->min_height, h);
            hf->max_height = f_max(hf->max_height, h);
        }
    }
}

f32 heightfield_height_at(const Heightfield* hf, u32 ix, u32 iz)
{
    ix = ix < hf->size_x ? ix : hf->size_x - 1;
    iz = iz < hf->size_z ? iz : hf->size_z - 1;
    return hf->heights[(u64)iz * hf->size_x + ix];
}

static void heightfield_locate(const Heightfield* hf, f32 x, f32 z, u32* out_ix, u32* out_iz, f32* out_u, f32* out_v)
{
    f32 fx = (x - hf->origin.x) / hf->cell_size;
    f32 fz = (z - hf->origin.z) / hf->cell_size;
    f32 max_fx = (f32)(hf->size_x - 2);
    f32 max_fz = (f32)(hf->size_z - 2);
    fx = f_clamp(fx, 0.0f, max_fx + 0.9999f);
    fz = f_clamp(fz, 0.0f, max_fz + 0.9999f);
    u32 ix = (u32)f_min(fx, max_fx);
    u32 iz = (u32)f_min(fz, max_fz);
    *out_ix = ix;
    *out_iz = iz;
    *out_u = fx - (f32)ix;
    *out_v = fz - (f32)iz;
}

f32 heightfield_sample(const Heightfield* hf, f32 x, f32 z)
{
    u32 ix, iz;
    f32 u, v;
    heightfield_locate(hf, x, z, &ix, &iz, &u, &v);
    f32 h00 = heightfield_height_at(hf, ix, iz);
    f32 h10 = heightfield_height_at(hf, ix + 1, iz);
    f32 h01 = heightfield_height_at(hf, ix, iz + 1);
    f32 h11 = heightfield_height_at(hf, ix + 1, iz + 1);
    if (u >= v) {
        return h00 + (h10 - h00) * u + (h11 - h10) * v;
    }
    return h00 + (h11 - h01) * u + (h01 - h00) * v;
}

void heightfield_cell_triangles(const Heightfield* hf, u32 ix, u32 iz, Vec3 out_tris[6])
{
    f32 x0 = hf->origin.x + (f32)ix * hf->cell_size;
    f32 z0 = hf->origin.z + (f32)iz * hf->cell_size;
    f32 x1 = x0 + hf->cell_size;
    f32 z1 = z0 + hf->cell_size;
    Vec3 p00 = v3(x0, heightfield_height_at(hf, ix, iz), z0);
    Vec3 p10 = v3(x1, heightfield_height_at(hf, ix + 1, iz), z0);
    Vec3 p01 = v3(x0, heightfield_height_at(hf, ix, iz + 1), z1);
    Vec3 p11 = v3(x1, heightfield_height_at(hf, ix + 1, iz + 1), z1);
    out_tris[0] = p00;
    out_tris[1] = p10;
    out_tris[2] = p11;
    out_tris[3] = p00;
    out_tris[4] = p11;
    out_tris[5] = p01;
}

static Vec3 triangle_normal_up(Vec3 a, Vec3 b, Vec3 c)
{
    Vec3 n = vec3_normalize(vec3_cross(vec3_sub(b, a), vec3_sub(c, a)));
    if (n.y < 0.0f) {
        n = vec3_negate(n);
    }
    return n;
}

Vec3 heightfield_normal(const Heightfield* hf, f32 x, f32 z)
{
    u32 ix, iz;
    f32 u, v;
    heightfield_locate(hf, x, z, &ix, &iz, &u, &v);
    Vec3 tris[6];
    heightfield_cell_triangles(hf, ix, iz, tris);
    if (u >= v) {
        return triangle_normal_up(tris[0], tris[1], tris[2]);
    }
    return triangle_normal_up(tris[3], tris[4], tris[5]);
}

b32 heightfield_raycast(const Heightfield* hf, Ray ray, f32 max_t, f32* out_t, Vec3* out_normal)
{
    f32 span_x = (f32)(hf->size_x - 1) * hf->cell_size;
    f32 span_z = (f32)(hf->size_z - 1) * hf->cell_size;
    Aabb bounds;
    bounds.min = v3(hf->origin.x, hf->min_height - 0.5f, hf->origin.z);
    bounds.max = v3(hf->origin.x + span_x, hf->max_height + 0.5f, hf->origin.z + span_z);

    f32 t_cur = 0.0f;
    if (!aabb_contains_point(bounds, ray.origin)) {
        if (!ray_vs_aabb(ray, bounds, max_t, &t_cur)) {
            return 0;
        }
        t_cur += HF_RAY_EPS;
    }

    Vec3 p = vec3_add(ray.origin, vec3_scale(ray.dir, t_cur));
    i32 max_ix = (i32)hf->size_x - 2;
    i32 max_iz = (i32)hf->size_z - 2;
    i32 ix = (i32)f_clamp((p.x - hf->origin.x) / hf->cell_size, 0.0f, (f32)max_ix);
    i32 iz = (i32)f_clamp((p.z - hf->origin.z) / hf->cell_size, 0.0f, (f32)max_iz);

    i32 step_x = ray.dir.x > 0.0f ? 1 : -1;
    i32 step_z = ray.dir.z > 0.0f ? 1 : -1;
    f32 t_max_x = HF_T_INF;
    f32 t_max_z = HF_T_INF;
    f32 t_delta_x = HF_T_INF;
    f32 t_delta_z = HF_T_INF;
    if (f_abs(ray.dir.x) > 1e-9f) {
        f32 boundary_x = hf->origin.x + (f32)(ix + (step_x > 0 ? 1 : 0)) * hf->cell_size;
        t_max_x = t_cur + (boundary_x - p.x) / ray.dir.x;
        t_delta_x = hf->cell_size / f_abs(ray.dir.x);
    }
    if (f_abs(ray.dir.z) > 1e-9f) {
        f32 boundary_z = hf->origin.z + (f32)(iz + (step_z > 0 ? 1 : 0)) * hf->cell_size;
        t_max_z = t_cur + (boundary_z - p.z) / ray.dir.z;
        t_delta_z = hf->cell_size / f_abs(ray.dir.z);
    }

    while (ix >= 0 && ix <= max_ix && iz >= 0 && iz <= max_iz && t_cur <= max_t) {
        Vec3 tris[6];
        heightfield_cell_triangles(hf, (u32)ix, (u32)iz, tris);
        RayHitTri hit;
        f32 best_t = HF_T_INF;
        Vec3 best_normal = v3(0.0f, 1.0f, 0.0f);
        for (i32 tri = 0; tri < 2; tri++) {
            Vec3 a = tris[tri * 3 + 0];
            Vec3 b = tris[tri * 3 + 1];
            Vec3 c = tris[tri * 3 + 2];
            if (ray_vs_triangle(ray, a, b, c, max_t, &hit) && hit.t < best_t) {
                best_t = hit.t;
                best_normal = triangle_normal_up(a, b, c);
            }
        }
        if (best_t < HF_T_INF) {
            if (out_t) {
                *out_t = best_t;
            }
            if (out_normal) {
                *out_normal = best_normal;
            }
            return 1;
        }
        if (t_max_x < t_max_z) {
            t_cur = t_max_x;
            t_max_x += t_delta_x;
            ix += step_x;
        } else {
            t_cur = t_max_z;
            t_max_z += t_delta_z;
            iz += step_z;
        }
    }
    return 0;
}
