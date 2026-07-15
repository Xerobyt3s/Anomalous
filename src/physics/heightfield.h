#pragma once

#include "core/types.h"
#include "math/vmath.h"

struct Arena;

typedef struct Heightfield {
    f32* heights;
    u32 size_x;
    u32 size_z;
    f32 cell_size;
    Vec3 origin;
    f32 min_height;
    f32 max_height;
} Heightfield;

void heightfield_init_procedural(Heightfield* hf, struct Arena* arena, u32 size, f32 cell_size, u32 seed);
f32  heightfield_height_at(const Heightfield* hf, u32 ix, u32 iz);
f32  heightfield_sample(const Heightfield* hf, f32 x, f32 z);
Vec3 heightfield_normal(const Heightfield* hf, f32 x, f32 z);
void heightfield_cell_triangles(const Heightfield* hf, u32 ix, u32 iz, Vec3 out_tris[6]);
b32  heightfield_raycast(const Heightfield* hf, Ray ray, f32 max_t, f32* out_t, Vec3* out_normal);
