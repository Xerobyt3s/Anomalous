#pragma once

#include "core/types.h"
#include "math/vmath.h"

struct Heightfield;

typedef struct SphereContact {
    Vec3 point;
    Vec3 normal;
    f32 depth;
} SphereContact;

b32 collide_sphere_heightfield(const struct Heightfield* hf, Sphere sphere, SphereContact* out_contact);
