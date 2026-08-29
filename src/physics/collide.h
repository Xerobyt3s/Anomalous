#pragma once

#include "core/types.h"
#include "math/vmath.h"

namespace anom {

class Heightfield;
class StaticGrid;
struct RigidBody;

struct SphereContact {
    Vec3 point;
    Vec3 normal;
    f32 depth;
    u32 feature;
};

bool collide_sphere_heightfield(const Heightfield& hf, Sphere sphere, SphereContact& out);
u32 collide_sphere_statics(const StaticGrid& grid, Sphere sphere, SphereContact* out,
                           u32 max_contacts);
bool collide_sphere_obb(Sphere sphere, const RigidBody& box, SphereContact& out);

} // namespace anom
