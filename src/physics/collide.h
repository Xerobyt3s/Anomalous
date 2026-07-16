#pragma once

#include "core/types.h"
#include "math/vmath.h"

struct Heightfield;
struct StaticGrid;

typedef struct SphereContact {
    Vec3 point;
    Vec3 normal;
    f32 depth;
} SphereContact;

b32 collide_sphere_heightfield(const struct Heightfield* hf, Sphere sphere, SphereContact* out_contact);
u32 collide_sphere_statics(const struct StaticGrid* grid, Sphere sphere, SphereContact* out_contacts, u32 max_contacts);
