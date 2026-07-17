#pragma once

#include "core/types.h"
#include "math/vmath.h"

struct Heightfield;

#define MAPDATA_GRID 64
#define MAPDATA_MAX_CELLS (MAPDATA_GRID * MAPDATA_GRID)

void mapdata_init(const struct Heightfield* hf);
void mapdata_reset(void);
void mapdata_visit(Vec3 pos);
u32  mapdata_reveal_radius(Vec3 center, f32 radius);
u32  mapdata_count(void);
u32  mapdata_total(void);
b32  mapdata_cell(u32 index, f32* out_x, f32* out_z, f32* out_size);
