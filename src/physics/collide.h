#pragma once

#include "core/types.h"
#include "math/vmath.h"

namespace anom {

class StaticGrid;

struct SphereContact {
    Vec3 point;
    Vec3 normal;
    f32 depth;
    u32 feature;
};

u32 collide_sphere_statics(const StaticGrid& grid, Sphere sphere, SphereContact* out,
                           u32 max_contacts);

} // namespace anom
