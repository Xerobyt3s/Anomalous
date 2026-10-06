#include "physics/collide.h"
#include "physics/static_grid.h"

namespace anom {

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

} // namespace anom
