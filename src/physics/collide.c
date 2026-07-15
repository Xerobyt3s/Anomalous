#include "physics/collide.h"
#include "physics/heightfield.h"

b32 collide_sphere_heightfield(const struct Heightfield* hf, Sphere sphere, SphereContact* out_contact)
{
    f32 r = sphere.radius;
    i32 max_ix = (i32)hf->size_x - 2;
    i32 max_iz = (i32)hf->size_z - 2;
    i32 ix0 = (i32)f_clamp((sphere.center.x - r - hf->origin.x) / hf->cell_size, 0.0f, (f32)max_ix);
    i32 ix1 = (i32)f_clamp((sphere.center.x + r - hf->origin.x) / hf->cell_size, 0.0f, (f32)max_ix);
    i32 iz0 = (i32)f_clamp((sphere.center.z - r - hf->origin.z) / hf->cell_size, 0.0f, (f32)max_iz);
    i32 iz1 = (i32)f_clamp((sphere.center.z + r - hf->origin.z) / hf->cell_size, 0.0f, (f32)max_iz);

    b32 found = 0;
    SphereContact best = {0};
    for (i32 iz = iz0; iz <= iz1; iz++) {
        for (i32 ix = ix0; ix <= ix1; ix++) {
            Vec3 tris[6];
            heightfield_cell_triangles(hf, (u32)ix, (u32)iz, tris);
            for (i32 tri = 0; tri < 2; tri++) {
                Vec3 a = tris[tri * 3 + 0];
                Vec3 b = tris[tri * 3 + 1];
                Vec3 c = tris[tri * 3 + 2];
                Vec3 cp = closest_point_on_triangle(sphere.center, a, b, c);
                Vec3 delta = vec3_sub(sphere.center, cp);
                f32 dist_sq = vec3_length_sq(delta);
                if (dist_sq > r * r) {
                    continue;
                }
                Vec3 tri_n = vec3_normalize(vec3_cross(vec3_sub(b, a), vec3_sub(c, a)));
                if (tri_n.y < 0.0f) {
                    tri_n = vec3_negate(tri_n);
                }
                f32 dist = sqrtf(dist_sq);
                Vec3 n = dist > 1e-6f ? vec3_scale(delta, 1.0f / dist) : tri_n;
                if (vec3_dot(n, tri_n) < 0.0f) {
                    continue;
                }
                f32 depth = r - dist;
                if (!found || depth > best.depth) {
                    best.point = cp;
                    best.normal = n;
                    best.depth = depth;
                    found = 1;
                }
            }
        }
    }

    if (!found) {
        f32 surface = heightfield_sample(hf, sphere.center.x, sphere.center.z);
        if (sphere.center.y < surface) {
            best.point = v3(sphere.center.x, surface, sphere.center.z);
            best.normal = heightfield_normal(hf, sphere.center.x, sphere.center.z);
            best.depth = (surface - sphere.center.y) + r;
            found = 1;
        }
    }

    if (found && out_contact) {
        *out_contact = best;
    }
    return found;
}
