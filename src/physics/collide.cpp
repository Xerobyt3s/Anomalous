#include "physics/collide.h"
#include "physics/body.h"
#include "physics/heightfield.h"
#include "physics/static_grid.h"

namespace anom {

bool collide_sphere_heightfield(const Heightfield& hf, Sphere sphere, SphereContact& out)
{
    const f32 r = sphere.radius;
    const i32 max_ix = static_cast<i32>(hf.size_x()) - 2;
    const i32 max_iz = static_cast<i32>(hf.size_z()) - 2;
    const Vec3 origin = hf.origin();
    const f32 cell = hf.cell_size();

    const i32 ix0 = static_cast<i32>(f_clamp((sphere.center.x - r - origin.x) / cell, 0.0f,
                                             static_cast<f32>(max_ix)));
    const i32 ix1 = static_cast<i32>(f_clamp((sphere.center.x + r - origin.x) / cell, 0.0f,
                                             static_cast<f32>(max_ix)));
    const i32 iz0 = static_cast<i32>(f_clamp((sphere.center.z - r - origin.z) / cell, 0.0f,
                                             static_cast<f32>(max_iz)));
    const i32 iz1 = static_cast<i32>(f_clamp((sphere.center.z + r - origin.z) / cell, 0.0f,
                                             static_cast<f32>(max_iz)));

    bool found = false;
    SphereContact best{};

    for (i32 iz = iz0; iz <= iz1; iz++) {
        for (i32 ix = ix0; ix <= ix1; ix++) {
            Vec3 tris[6];
            hf.cell_triangles(static_cast<u32>(ix), static_cast<u32>(iz), tris);
            for (i32 t = 0; t < 2; t++) {
                const Vec3 a = tris[t * 3 + 0];
                const Vec3 b = tris[t * 3 + 1];
                const Vec3 c = tris[t * 3 + 2];
                const Vec3 cp = closest_point_on_triangle(sphere.center, a, b, c);
                const Vec3 delta = sphere.center - cp;
                const f32 dist_sq = length_sq(delta);
                if (dist_sq > r * r) {
                    continue;
                }
                Vec3 tri_n = normalize(cross(b - a, c - a));
                if (tri_n.y < 0.0f) {
                    tri_n = -tri_n;
                }
                const f32 dist = std::sqrt(dist_sq);
                const Vec3 n = dist > 1e-6f ? delta * (1.0f / dist) : tri_n;
                if (dot(n, tri_n) < 0.0f) {
                    continue;
                }
                const f32 depth = r - dist;
                if (!found || depth > best.depth) {
                    best.point = cp;
                    best.normal = n;
                    best.depth = depth;
                    best.feature = static_cast<u32>(iz) * hf.size_x() * 2
                                 + static_cast<u32>(ix) * 2 + static_cast<u32>(t);
                    found = true;
                }
            }
        }
    }

    if (!found) {
        const f32 surface = hf.sample(sphere.center.x, sphere.center.z);
        if (sphere.center.y < surface) {
            best.point = Vec3{sphere.center.x, surface, sphere.center.z};
            best.normal = hf.normal(sphere.center.x, sphere.center.z);
            best.depth = (surface - sphere.center.y) + r;
            best.feature = 0xFFFFFFFEu;
            found = true;
        }
    }

    if (found) {
        out = best;
    }
    return found;
}

u32 collide_sphere_statics(const StaticGrid& grid, Sphere sphere, SphereContact* out,
                           u32 max_contacts)
{
    if (!grid.built() || !max_contacts) {
        return 0;
    }
    const f32 r = sphere.radius;
    i32 x0 = 0, x1 = 0, z0 = 0, z1 = 0;
    if (!grid.cell_range(sphere.center - Vec3{r, r, r}, sphere.center + Vec3{r, r, r},
                         x0, x1, z0, z1)) {
        return 0;
    }

    u32 count = 0;
    for (i32 z = z0; z <= z1; z++) {
        for (i32 x = x0; x <= x1; x++) {
            for (const u32 index : grid.cell_tris(x, z)) {
                const StaticTri& tri = grid.tri(index);
                const Vec3 cp = closest_point_on_triangle(sphere.center, tri.a, tri.b, tri.c);
                const Vec3 delta = sphere.center - cp;
                const f32 dist_sq = length_sq(delta);
                if (dist_sq > r * r) {
                    continue;
                }
                const f32 dist = std::sqrt(dist_sq);
                const Vec3 n = dist > 1e-6f ? delta * (1.0f / dist)
                                            : normalize(cross(tri.b - tri.a, tri.c - tri.a));

                SphereContact contact;
                contact.point = cp;
                contact.normal = n;
                contact.depth = r - dist;
                contact.feature = index;

                if (count < max_contacts) {
                    out[count++] = contact;
                } else {
                    u32 shallowest = 0;
                    for (u32 i = 1; i < max_contacts; i++) {
                        if (out[i].depth < out[shallowest].depth) {
                            shallowest = i;
                        }
                    }
                    if (contact.depth > out[shallowest].depth) {
                        out[shallowest] = contact;
                    }
                }
            }
        }
    }
    return count;
}

bool collide_sphere_obb(Sphere sphere, const RigidBody& box, SphereContact& out)
{
    const Mat3 rot = quat_to_mat3(box.rot);
    const Vec3 center = box.pos + rot * box.box_offset;
    const Vec3 local = transpose(rot) * (sphere.center - center);
    const Vec3 h = box.half_extents;

    Vec3 clamped{f_clamp(local.x, -h.x, h.x), f_clamp(local.y, -h.y, h.y),
                 f_clamp(local.z, -h.z, h.z)};
    const Vec3 diff = local - clamped;
    const f32 dist_sq = length_sq(diff);
    if (dist_sq > sphere.radius * sphere.radius) {
        return false;
    }

    Vec3 n_local{0.0f, 1.0f, 0.0f};
    f32 depth = 0.0f;
    if (dist_sq > 1e-10f) {
        const f32 dist = std::sqrt(dist_sq);
        n_local = diff * (1.0f / dist);
        depth = sphere.radius - dist;
    } else {
        const f32 px = h.x - f_abs(local.x);
        const f32 py = h.y - f_abs(local.y);
        const f32 pz = h.z - f_abs(local.z);
        if (px < py && px < pz) {
            n_local = Vec3{local.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};
            clamped.x = local.x >= 0.0f ? h.x : -h.x;
            depth = px + sphere.radius;
        } else if (py < pz) {
            n_local = Vec3{0.0f, local.y >= 0.0f ? 1.0f : -1.0f, 0.0f};
            clamped.y = local.y >= 0.0f ? h.y : -h.y;
            depth = py + sphere.radius;
        } else {
            n_local = Vec3{0.0f, 0.0f, local.z >= 0.0f ? 1.0f : -1.0f};
            clamped.z = local.z >= 0.0f ? h.z : -h.z;
            depth = pz + sphere.radius;
        }
    }

    out.normal = rot * n_local;
    out.point = center + rot * clamped;
    out.depth = depth;
    out.feature = 0;
    return true;
}

} // namespace anom
